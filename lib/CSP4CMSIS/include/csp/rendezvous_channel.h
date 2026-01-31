#ifndef CSP4CMSIS_RENDEZVOUS_CHANNEL_H
#define CSP4CMSIS_RENDEZVOUS_CHANNEL_H

#include "channel_base.h"       
#include "alt_channel_sync.h"   
#include "FreeRTOS.h"
#include "task.h"               
#include <cstring>    
#include <cstdio>  

namespace csp::internal {

template <typename T>
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

    // --- Blocking Input (Receiver) ---
    virtual void input(T* const dest) override {
        xTaskNotifyStateClear(NULL);

        if (xSemaphoreTake(sync_base.getMutex(), portMAX_DELAY) == pdTRUE) {
            // 1. Check if a standard sender is already waiting
            if (sync_base.tryHandshake((void*)dest, sizeof(T), false)) {
                xSemaphoreGive(sync_base.getMutex());
                return; 
            }

            // 2. NEW: Check if a sender is currently in an ALT on this channel
            if (sync_base.getAltOutScheduler() != nullptr) {
                // Wake up the ALTed sender
                sync_base.getAltOutScheduler()->wakeUp(sync_base.getAltOutBit());
            }

            // 3. No partner ready yet: Register and block
            sync_base.registerWaitingTask((void*)dest, false);
            xSemaphoreGive(sync_base.getMutex());
        }

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // --- Blocking Output (Sender) ---
    virtual void output(const T* const source) override {
        xTaskNotifyStateClear(NULL);
        // printf("[Producer] Channel %p: Entering output()\n", (void*)this);

        if (xSemaphoreTake(sync_base.getMutex(), portMAX_DELAY) == pdTRUE) {
            // 1. Check for standard waiter
            if (sync_base.getWaitingInTask() != nullptr) {
                // printf("[Producer] Channel %p: Found standard blocking receiver.\r\n", (void*)this);
                sync_base.tryHandshake((void*)const_cast<T*>(source), sizeof(T), true);
                xSemaphoreGive(sync_base.getMutex());
                return; 
            }

            // 2. Check for ALT waiter (The Critical Path)
            if (sync_base.getAltInScheduler() != nullptr) {
                // printf("[Producer] Channel %p: FOUND ALTed receiver! Waking bit %lu\r\n", (void*)this, (unsigned long)sync_base.getAltInBit());
                
                sync_base.getAltInScheduler()->wakeUp(sync_base.getAltInBit());
                
                // Note: In Rendezvous, we must still block until the receiver calls activate()
                // printf("[Producer] Channel %p: Partner signaled. Registering to block...\r\n", (void*)this);
            } else {
                // printf("[Producer] Channel %p: No receiver found. Registering and blocking.\r\n", (void*)this);
            }

            sync_base.registerWaitingTask((void*)const_cast<T*>(source), true);
            xSemaphoreGive(sync_base.getMutex());
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // printf("[Producer] Channel %p: Output complete.\n", (void*)this);
    }

    // --- Resident Guard Implementation ---
    virtual internal::Guard* getInputGuard(T& dest) override {
        res_in_guard.updateBuffer(&dest); 
        return &res_in_guard;
    }
    
    virtual internal::Guard* getOutputGuard(const T& source) override { 
        res_out_guard.updateBuffer(const_cast<void*>(static_cast<const void*>(&source)));
        return &res_out_guard; 
    }
    
    virtual bool pending() override {
        bool has_partner = false;
        if (xSemaphoreTake(sync_base.getMutex(), 0) == pdTRUE) {
            // Pending is true if someone is waiting to block OR someone is ALTing
            has_partner = (sync_base.getWaitingInTask() != nullptr) || 
                          (sync_base.getWaitingOutTask() != nullptr) ||
                          (sync_base.getAltInScheduler() != nullptr) ||
                          (sync_base.getAltOutScheduler() != nullptr);
            xSemaphoreGive(sync_base.getMutex());
        }
        return has_partner;
    }
    
    virtual void beginExtInput(T* const dest) override {}
    virtual void endExtInput() override {}
};

} // namespace csp::internal

#endif