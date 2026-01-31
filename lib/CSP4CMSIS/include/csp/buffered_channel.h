#ifndef CSP4CMSIS_BUFFERED_CHANNEL_H
#define CSP4CMSIS_BUFFERED_CHANNEL_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "channel_base.h" 
#include "alt.h"         
#include <cstdlib> 

namespace csp::internal {

    template <typename T> class BufferedInputGuard;
    template <typename T> class BufferedOutputGuard;

    template <typename T>
    class BufferedChannel : public internal::BaseAltChan<T>
    {
    private:
        QueueHandle_t queue_handle; 
        
        // Use AltScheduler pointers to remain consistent with your Alt system
        AltScheduler* alt_reader = nullptr;
        EventBits_t   read_bit = 0;
        
        AltScheduler* alt_writer = nullptr;
        EventBits_t   write_bit = 0;

        BufferedInputGuard<T>  res_in_guard;
        BufferedOutputGuard<T> res_out_guard;
        
    public:
        BufferedChannel(size_t capacity) 
            : res_in_guard(this), res_out_guard(this) 
        {
            if (capacity == 0) std::abort(); 
            queue_handle = xQueueCreate(capacity, sizeof(T));
        }

        ~BufferedChannel() override {
            if (queue_handle) vQueueDelete(queue_handle);
        }

        // --- Required by BaseAltChan ---
        // Matches the signature: virtual bool pending() = 0;
        bool pending() override { 
            return uxQueueMessagesWaiting(queue_handle) > 0; 
        } 

        bool space_available() { 
            return uxQueueSpacesAvailable(queue_handle) > 0; 
        }

        // --- Core I/O ---
        void input(T* const dest) override {
            if (xQueueReceive(queue_handle, dest, portMAX_DELAY) == pdPASS) {
                // If a sender was ALTed waiting for space, wake them
                taskENTER_CRITICAL();
                if (alt_writer) alt_writer->wakeUp(write_bit);
                taskEXIT_CRITICAL();
            }
        }

        void output(const T* const source) override {
            if (xQueueSend(queue_handle, source, portMAX_DELAY) == pdPASS) {
                // If a receiver was ALTed waiting for data, wake them
                taskENTER_CRITICAL();
                if (alt_reader) alt_reader->wakeUp(read_bit);
                taskEXIT_CRITICAL();
            }
        }

        void beginExtInput(T* const dest) override { this->input(dest); }
        void endExtInput() override { }
        
        Guard* getInputGuard(T& dest) override {
            res_in_guard.setTarget(&dest);
            return &res_in_guard;
        }

        Guard* getOutputGuard(const T& source) override {
            res_out_guard.setTarget(&source);
            return &res_out_guard;
        }
        
        // Registration Helpers
        void registerInputAlt(AltScheduler* alt, EventBits_t b) {
            taskENTER_CRITICAL(); alt_reader = alt; read_bit = b; taskEXIT_CRITICAL();
        }
        void unregisterInputAlt() {
            taskENTER_CRITICAL(); alt_reader = nullptr; taskEXIT_CRITICAL();
        }
        void registerOutputAlt(AltScheduler* alt, EventBits_t b) {
            taskENTER_CRITICAL(); alt_writer = alt; write_bit = b; taskEXIT_CRITICAL();
        }
        void unregisterOutputAlt() {
            taskENTER_CRITICAL(); alt_writer = nullptr; taskEXIT_CRITICAL();
        }

        QueueHandle_t getQueueHandle() const { return queue_handle; }
    };
    
    // =============================================================
    // Guards (Updated to use AltScheduler* pointer directly)
    // =============================================================
    template <typename T>
    class BufferedInputGuard : public Guard {
    private:
        BufferedChannel<T>* channel;
        T* dest_ptr = nullptr; 
    public:
        BufferedInputGuard(BufferedChannel<T>* chan) : channel(chan) {}
        void setTarget(T* dest) { dest_ptr = dest; }

        bool enable(AltScheduler* alt, EventBits_t bit) override {
            if (channel->pending()) return true;
            channel->registerInputAlt(alt, bit);
            return false;
        }
        bool disable() override {
            channel->unregisterInputAlt();
            return channel->pending();
        }
        void activate() override {
            xQueueReceive(channel->getQueueHandle(), dest_ptr, 0);
        }
    };
    
    template <typename T>
    class BufferedOutputGuard : public Guard {
    private:
        BufferedChannel<T>* channel;
        const T* source_ptr = nullptr;
    public:
        BufferedOutputGuard(BufferedChannel<T>* chan) : channel(chan) {}
        void setTarget(const T* source) { source_ptr = source; }

        bool enable(AltScheduler* alt, EventBits_t bit) override {
            if (channel->space_available()) return true;
            channel->registerOutputAlt(alt, bit);
            return false;
        }
        bool disable() override {
            channel->unregisterOutputAlt();
            return channel->space_available();
        }
        void activate() override {
            xQueueSend(channel->getQueueHandle(), source_ptr, 0);
        }
    };

} // namespace csp::internal

#endif