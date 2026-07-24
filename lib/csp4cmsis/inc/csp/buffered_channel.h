#ifndef CSP4CMSIS_BUFFERED_CHANNEL_H
#define CSP4CMSIS_BUFFERED_CHANNEL_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "channel_base.h" 
#include "alt.h"         
#include <cstdlib> 

namespace csp::internal {

    template <typename T, csp::BufferPolicy P> class BufferedInputGuard;
    template <typename T, csp::BufferPolicy P> class BufferedOutputGuard;

    /**
     * @brief A policy-based buffered channel implementation using FreeRTOS Queues.
     */
    template <typename T, csp::BufferPolicy P = csp::BufferPolicy::Block>
    class BufferedChannel : public internal::BaseAltChan<T>
    {
    private:
        QueueHandle_t queue_handle; 
        
        AltScheduler* alt_reader = nullptr;
        EventBits_t   read_bit = 0;
        
        AltScheduler* alt_writer = nullptr;
        EventBits_t   write_bit = 0;

        BufferedInputGuard<T, P>  res_in_guard;
        BufferedOutputGuard<T, P> res_out_guard;

        /** @brief Internal helper to notify an ALTed reader that data is available. */
        void _notifyReader() {
            taskENTER_CRITICAL();
            if (alt_reader != nullptr) {
                alt_reader->wakeUp(read_bit);
            }
            taskEXIT_CRITICAL();
        }

        /** @brief Internal helper to notify an ALTed writer that space is available. */
        void _notifyWriter() {
            taskENTER_CRITICAL();
            if (alt_writer != nullptr) {
                alt_writer->wakeUp(write_bit);
            }
            taskEXIT_CRITICAL();
        }
        
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

        // --- BaseAltChan Overrides ---

        bool pending() override { 
            return uxQueueMessagesWaiting(queue_handle) > 0; 
        } 

        /**
         * @brief Checks if a write operation will block.
         * For KeepNewest/KeepOldest, this always returns true as they are non-blocking.
         */
        bool space_available() override { 
            if constexpr (P == csp::BufferPolicy::Block) {
                return uxQueueSpacesAvailable(queue_handle) > 0; 
            }
            return true; 
        }

        /**
         * @brief Policy-aware ISR output. 
         * Useful for high-speed peripherals like the STM32N6 DCMIPP (Camera).
         */
        bool putFromISR(const T& data) override {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            bool result = false;

            if (xQueueSendFromISR(queue_handle, &data, &xHigherPriorityTaskWoken) == pdPASS) {
                result = true;
            } else {
                if constexpr (P == csp::BufferPolicy::KeepNewest) {
                    T dummy;
                    xQueueReceiveFromISR(queue_handle, &dummy, &xHigherPriorityTaskWoken);
                    xQueueSendFromISR(queue_handle, &data, &xHigherPriorityTaskWoken);
                    result = true;
                } else if constexpr (P == csp::BufferPolicy::KeepOldest) {
                    result = true; // Handled by discarding
                }
            }

            if (result && alt_reader != nullptr) {
                alt_reader->wakeUp(read_bit);
            }

            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            return result;
        }

        // --- Core I/O ---

        void input(T* const dest) override {
            if (xQueueReceive(queue_handle, dest, portMAX_DELAY) == pdPASS) {
                _notifyWriter();
            }
        }

        void output(const T* const source) override {
            if constexpr (P == csp::BufferPolicy::Block) {
                if (xQueueSend(queue_handle, source, portMAX_DELAY) == pdPASS) {
                    _notifyReader();
                }
            } else {
                // Non-blocking branch: 0 timeout
                if (xQueueSend(queue_handle, source, 0) == errQUEUE_FULL) {
                    if constexpr (P == csp::BufferPolicy::KeepNewest) {
                        T dummy;
                        xQueueReceive(queue_handle, &dummy, 0); // Drop oldest
                        xQueueSend(queue_handle, source, 0);    // Push newest
                    }
                    // KeepOldest does nothing
                }
                _notifyReader();
            }
        }

        void beginExtInput(T* const dest) override { this->input(dest); }
        void endExtInput() override { }
        
        // --- Guard Factories ---

        internal::Guard* getInputGuard(T& dest) override {
            res_in_guard.setTarget(&dest);
            return &res_in_guard;
        }

        internal::Guard* getOutputGuard(const T& source) override {
            res_out_guard.setTarget(&source);
            return &res_out_guard;
        }
        
        // --- ALT Registration ---

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
    // Guards
    // =============================================================

    template <typename T, csp::BufferPolicy P>
    class BufferedInputGuard : public Guard {
    private:
        BufferedChannel<T, P>* channel;
        T* dest_ptr = nullptr; 
    public:
        BufferedInputGuard(BufferedChannel<T, P>* chan) : channel(chan) {}
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
    
    template <typename T, csp::BufferPolicy P>
    class BufferedOutputGuard : public Guard {
    private:
        BufferedChannel<T, P>* channel;
        const T* source_ptr = nullptr;
    public:
        BufferedOutputGuard(BufferedChannel<T, P>* chan) : channel(chan) {}
        void setTarget(const T* source) { source_ptr = source; }

        bool enable(AltScheduler* alt, EventBits_t bit) override {
            // KeepNewest/KeepOldest are always ready to accept output
            if (channel->space_available()) return true;
            
            channel->registerOutputAlt(alt, bit);
            return false;
        }
        bool disable() override {
            if constexpr (P != csp::BufferPolicy::Block) return true;
            channel->unregisterOutputAlt();
            return channel->space_available();
        }
        void activate() override {
            channel->output(source_ptr);
        }
    };

} // namespace csp::internal

#endif // CSP4CMSIS_BUFFERED_CHANNEL_H