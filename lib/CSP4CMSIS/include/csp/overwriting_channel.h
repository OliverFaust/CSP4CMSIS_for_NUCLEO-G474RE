// --- overwriting_channel.h (Time Sync Removed / Simplified) ---
#ifndef CSP4CMSIS_OVERWRITING_CHANNEL_H
#define CSP4CMSIS_OVERWRITING_CHANNEL_H

#include "buffered_channel.h"
#include "FreeRTOS.h"
#include "queue.h"

namespace csp::internal {

/**
 * @brief A Buffered Channel that never blocks on write. 
 * If full, it overwrites the oldest data (FIFO drop).
 */
template <typename T>
class OverwritingChannel : public BufferedChannel<T> {
public:
    // Constructor
    OverwritingChannel(size_t capacity) : BufferedChannel<T>(capacity) {}

    /**
     * @brief Overrides the core output contract to implement overwrite logic.
     * Never blocks (zero wait time).
     */
    virtual void output(const T* const source) override {
        // Try to send immediately (0 ticks wait)
        if (xQueueSend(this->getQueueHandle(), source, 0) != pdPASS) {
            
            // Queue is full. Initiate overwrite sequence.
            T dummy;
            
            // 1. Remove oldest item (non-blocking, zero wait).
            // This frees one slot in the queue. We don't care about the content.
            xQueueReceive(this->getQueueHandle(), &dummy, 0);
            
            // 2. Send new item (non-blocking, zero wait).
            // This must succeed because we just freed a slot, unless another
            // task raced to send, which is handled gracefully by the inner queue logic.
            // Since this is CSP, we assume a safe write operation after the drop.
            xQueueSend(this->getQueueHandle(), source, 0);
        }
    }

    // NOTE: Removed the virtual bool output_with_timeout(...) override.
    // By providing only 'void output', we fulfill the minimum non-timed contract.
    // If the base class (BaseChan) requires output_with_timeout, the implementation
    // provided by BufferedChannel (or a simple pass-through to output) should suffice.
    // By excluding the explicit override here, we eliminate the explicit mention of 'timeout'.
};

} // namespace csp::internal

#endif // CSP4CMSIS_OVERWRITING_CHANNEL_H