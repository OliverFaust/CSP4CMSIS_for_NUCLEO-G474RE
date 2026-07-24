#ifndef CSP4CMSIS_RENDEZVOUS_CHANNEL_H
#define CSP4CMSIS_RENDEZVOUS_CHANNEL_H

#include "channel_base.h"       
#include "alt_channel_sync.h"   
#include "FreeRTOS.h"
#include "task.h"               
#include <cstring>    

namespace csp::internal {

/**
 * @brief Zero-capacity Synchronous Channel (Rendezvous).
 * Updated to support KeepNewest/KeepOldest (Non-blocking handshake).
 */
template <typename T, csp::BufferPolicy P = csp::BufferPolicy::Block>
class RendezvousChannel : public BaseAltChan<T> {
private:
    AltChanSyncBase sync_base;
    internal::ChanInGuard  res_in_guard;
    internal::ChanOutGuard res_out_guard; 

public:
    RendezvousChannel() 
        : res_in_guard(&sync_base, nullptr, sizeof(T)),
          res_out_guard(&sync_base, nullptr, sizeof(T)) {}

    virtual ~RendezvousChannel() override = default;

    // --- Core Contract Overrides ---

    /**
     * @brief In Rendezvous, space is available only if a receiver is waiting.
     * UNLESS the policy is non-blocking, in which case we are "always ready" 
     * because we'll just drop the data if no one is there.
     */
    bool space_available() override {
        if constexpr (P != csp::BufferPolicy::Block) return true;
        
        bool ready = false;
        if (xSemaphoreTake(sync_base.getMutex(), 0) == pdTRUE) {
            ready = (sync_base.getWaitingInTask() != nullptr) || 
                    (sync_base.getAltInScheduler() != nullptr);
            xSemaphoreGive(sync_base.getMutex());
        }
        return ready;
    }

    bool pending() override {
        bool has_partner = false;
        if (xSemaphoreTake(sync_base.getMutex(), 0) == pdTRUE) {
            has_partner = (sync_base.getWaitingInTask() != nullptr) || 
                          (sync_base.getWaitingOutTask() != nullptr) ||
                          (sync_base.getAltInScheduler() != nullptr) ||
                          (sync_base.getAltOutScheduler() != nullptr);
            xSemaphoreGive(sync_base.getMutex());
        }
        return has_partner;
    }

    // --- Blocking Input (Receiver) ---
    // Receiver always blocks in Rendezvous, regardless of policy.
    void input(T* const dest) override {
        xTaskNotifyStateClear(NULL);

        if (xSemaphoreTake(sync_base.getMutex(), portMAX_DELAY) == pdTRUE) {
            if (sync_base.tryHandshake((void*)dest, sizeof(T), false)) {
                xSemaphoreGive(sync_base.getMutex());
                return; 
            }

            if (sync_base.getAltOutScheduler() != nullptr) {
                sync_base.getAltOutScheduler()->wakeUp(sync_base.getAltOutBit());
            }

            sync_base.registerWaitingTask((void*)dest, false);
            xSemaphoreGive(sync_base.getMutex());
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // --- Policy-Aware Output (Sender) ---
    void output(const T* const source) override {
        xTaskNotifyStateClear(NULL);

        if (xSemaphoreTake(sync_base.getMutex(), portMAX_DELAY) == pdTRUE) {
            // 1. Try immediate handshake (Standard or ALT reader)
            if (sync_base.getWaitingInTask() != nullptr) {
                sync_base.tryHandshake((void*)const_cast<T*>(source), sizeof(T), true);
                xSemaphoreGive(sync_base.getMutex());
                return; 
            }

            if (sync_base.getAltInScheduler() != nullptr) {
                sync_base.getAltInScheduler()->wakeUp(sync_base.getAltInBit());
                // In Rendezvous, we still block until they 'activate' the ALT
            } 

            // 2. Policy Check: If we reach here, no receiver was immediately ready.
            if constexpr (P != csp::BufferPolicy::Block) {
                // Non-blocking policy: Drop the data and exit.
                xSemaphoreGive(sync_base.getMutex());
                return; 
            }

            // 3. Blocking Path: Register and wait
            sync_base.registerWaitingTask((void*)const_cast<T*>(source), true);
            xSemaphoreGive(sync_base.getMutex());
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // --- ISR Output ---
    bool putFromISR(const T& data) override {
        bool success = false;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();

        if (sync_base.getWaitingInTask() != nullptr) {
            std::memcpy(sync_base.getNonAltInDataPtr(), &data, sizeof(T));
            TaskHandle_t toWake = sync_base.getWaitingInTask();
            sync_base.clearWaitingIn();
            vTaskNotifyGiveFromISR(toWake, &xHigherPriorityTaskWoken);
            success = true;
        } 
        else if (sync_base.getAltInScheduler() != nullptr) {
            sync_base.getAltInScheduler()->wakeUp(sync_base.getAltInBit());
            success = true; 
        }
        else if constexpr (P != csp::BufferPolicy::Block) {
            // Non-blocking ISR: Treat "dropped" as "handled successfully"
            success = true;
        }

        taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return success;
    }

    virtual internal::Guard* getInputGuard(T& dest) override {
        res_in_guard.updateBuffer(&dest); 
        return &res_in_guard;
    }
    
    virtual internal::Guard* getOutputGuard(const T& source) override { 
        res_out_guard.updateBuffer(const_cast<T*>(&source));
        return &res_out_guard; 
    }

    void beginExtInput(T* const dest) override {}
    void endExtInput() override {}
};

} // namespace csp::internal

#endif