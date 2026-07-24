#include "csp/sync_channel.h"
#include <cstring>

namespace csp::internal {

// Interval to check for task suspension or timeouts during blocking
#define WAIT_SLICE_TICKS pdMS_TO_TICKS(100)

// =============================================================
// SyncChannel Core Implementation
// =============================================================

template <csp::BufferPolicy P>
SyncChannel<P>::SyncChannel()
    : state(IDLE),
      data_ptr(nullptr),
      data_len(0),
      waiting_alt_in(nullptr),
      waiting_alt_bit_in(0),
      waiting_guard_in(nullptr),
      waiting_alt_out(nullptr),
      waiting_alt_bit_out(0),
      waiting_guard_out(nullptr),
      res_in_guard(this),  
      res_out_guard(this)  
{
    mutex = xSemaphoreCreateMutex();
    sender_queue = xQueueCreate(1, 0);
    receiver_queue = xQueueCreate(1, 0);
}

template <csp::BufferPolicy P>
SyncChannel<P>::~SyncChannel() {
    if (mutex) vSemaphoreDelete(mutex);
    if (sender_queue) vQueueDelete(sender_queue);
    if (receiver_queue) vQueueDelete(receiver_queue);
}

template <csp::BufferPolicy P>
void SyncChannel<P>::reset() {
    state = IDLE;
    data_ptr = nullptr;
    waiting_alt_in = nullptr;
    waiting_alt_bit_in = 0;
    waiting_guard_in = nullptr;
    waiting_alt_out = nullptr;
    waiting_alt_bit_out = 0;
    waiting_guard_out = nullptr;
}

// --- Blocking/Sampling Output (Sender) ---
template <csp::BufferPolicy P>
void SyncChannel<P>::output(const void* const data_ptr_in) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;

    if (state == RECEIVER_WAITING) {
        data_ptr = data_ptr_in;
        
        AltScheduler* rx_alt = waiting_alt_in; 
        EventBits_t rx_alt_bit = waiting_alt_bit_in;
        bool is_alt_waiter = (waiting_alt_in != nullptr);

        waiting_alt_in = nullptr; 
        xSemaphoreGive(mutex);

        if (is_alt_waiter) {
            rx_alt->wakeUp(rx_alt_bit);
        } else {
            xQueueSend(receiver_queue, nullptr, 0);
        }

        // BLOCK: Wait for receiver to finish copying
        while (xQueueReceive(sender_queue, nullptr, WAIT_SLICE_TICKS) != pdPASS);
    }
    else {
        if constexpr (P != csp::BufferPolicy::Block) {
            xSemaphoreGive(mutex);
            return; 
        } else {
            state = SENDER_WAITING;
            data_ptr = data_ptr_in;
            xSemaphoreGive(mutex);
            
            while (xQueueReceive(sender_queue, nullptr, WAIT_SLICE_TICKS) != pdPASS);
        }
    }
}

// --- Blocking Input (Receiver) ---
template <csp::BufferPolicy P>
void SyncChannel<P>::input(void* const data_ptr_out) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;

    if (state == SENDER_WAITING) {
        if (data_ptr != nullptr && data_ptr_out != nullptr) {
            memcpy(data_ptr_out, data_ptr, data_len);
        }

        AltScheduler* tx_alt = waiting_alt_out;
        bool is_alt_waiter = (waiting_alt_out != nullptr);
        EventBits_t tx_alt_bit = waiting_alt_bit_out;
        
        waiting_alt_out = nullptr; 

        if (is_alt_waiter) {
            tx_alt->wakeUp(tx_alt_bit);
        } else {
            xQueueSend(sender_queue, nullptr, 0);
        }
        
        this->reset(); 
        xSemaphoreGive(mutex);
    }
    else {
        state = RECEIVER_WAITING;
        xSemaphoreGive(mutex);

        while (xQueueReceive(receiver_queue, nullptr, WAIT_SLICE_TICKS) != pdPASS);

        xSemaphoreTake(mutex, portMAX_DELAY);
        if (data_ptr != nullptr && data_ptr_out != nullptr) {
            memcpy(data_ptr_out, data_ptr, data_len);
        }
        xQueueSend(sender_queue, nullptr, 0); 
        this->reset();
        xSemaphoreGive(mutex);
    }
}

template <csp::BufferPolicy P>
bool SyncChannel<P>::pending() {
    return (state != IDLE);
}

template <csp::BufferPolicy P>
bool SyncChannel<P>::space_available() {
    if constexpr (P != csp::BufferPolicy::Block) return true;
    return (state == RECEIVER_WAITING || waiting_alt_in != nullptr);
}

template <csp::BufferPolicy P>
bool SyncChannel<P>::putFromISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    bool success = false;

    if (state == RECEIVER_WAITING) {
        // Since we can't notify via sender_queue comfortably here, 
        // Sync Signals from ISR are usually simplified or handled via Buffered channels.
        success = true; 
    } else if (waiting_alt_in != nullptr) {
        waiting_alt_in->wakeUp(waiting_alt_bit_in);
        success = true;
    } else if constexpr (P != csp::BufferPolicy::Block) {
        success = true; 
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return success;
}

// =============================================================
// ALT Registration Logic
// =============================================================

template <csp::BufferPolicy P>
bool SyncChannel<P>::registerAltIn(AltScheduler* alt, EventBits_t bit, SyncChannelInputGuard<P>* guard) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    
    if (state == SENDER_WAITING) { 
        xSemaphoreGive(mutex); 
        return true; 
    }
    
    state = RECEIVER_WAITING;
    waiting_alt_in = alt;
    waiting_alt_bit_in = bit;
    waiting_guard_in = guard;
    xSemaphoreGive(mutex);
    return false;
}

template <csp::BufferPolicy P>
bool SyncChannel<P>::unregisterAltIn(AltScheduler* alt) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    bool completed = (state != RECEIVER_WAITING);
    // Passing nullptr from Guard disable() means we check by current state
    if (!completed && (alt == nullptr || waiting_alt_in == alt)) this->reset();
    xSemaphoreGive(mutex);
    return completed;
}

template <csp::BufferPolicy P>
bool SyncChannel<P>::registerAltOut(AltScheduler* alt, EventBits_t bit, SyncChannelOutputGuard<P>* guard) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    
    if constexpr (P != csp::BufferPolicy::Block) {
        xSemaphoreGive(mutex);
        return true;
    }

    if (state == RECEIVER_WAITING) { 
        xSemaphoreGive(mutex); 
        return true; 
    }
    
    state = SENDER_WAITING;
    waiting_alt_out = alt;
    waiting_alt_bit_out = bit;
    waiting_guard_out = guard;
    xSemaphoreGive(mutex);
    return false;
}

template <csp::BufferPolicy P>
bool SyncChannel<P>::unregisterAltOut(AltScheduler* alt) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    bool completed = (state != SENDER_WAITING);
    if (!completed && (alt == nullptr || waiting_alt_out == alt)) this->reset();
    xSemaphoreGive(mutex);
    return completed;
}

// =============================================================
// Guard Implementation
// =============================================================

template <csp::BufferPolicy P>
bool SyncChannelInputGuard<P>::enable(AltScheduler* alt, EventBits_t bit) {
    return channel->registerAltIn(alt, bit, this);
}

template <csp::BufferPolicy P>
bool SyncChannelInputGuard<P>::disable() { 
    return channel->unregisterAltIn(nullptr);
}

template <csp::BufferPolicy P>
void SyncChannelInputGuard<P>::activate() {
    xSemaphoreTake(channel->getMutex(), portMAX_DELAY);
    if (channel->getDataPtr() != nullptr && user_data_dest != nullptr) {
        // data_size should be set during bind()
        memcpy(user_data_dest, channel->getDataPtr(), data_size);
    }
    xQueueSend(channel->getSenderQueue(), nullptr, 0);
    channel->reset();
    xSemaphoreGive(channel->getMutex());
}

template <csp::BufferPolicy P>
bool SyncChannelOutputGuard<P>::enable(AltScheduler* alt, EventBits_t bit) {
    return channel->registerAltOut(alt, bit, this);
}

template <csp::BufferPolicy P>
bool SyncChannelOutputGuard<P>::disable() { 
    return channel->unregisterAltOut(nullptr); 
}

template <csp::BufferPolicy P>
void SyncChannelOutputGuard<P>::activate() {
    channel->output(user_data_source);
}

// =============================================================
// Explicit Instantiations
// =============================================================

template class SyncChannel<csp::BufferPolicy::Block>;
template class SyncChannel<csp::BufferPolicy::KeepNewest>;
template class SyncChannel<csp::BufferPolicy::KeepOldest>;

template class SyncChannelInputGuard<csp::BufferPolicy::Block>;
template class SyncChannelInputGuard<csp::BufferPolicy::KeepNewest>;
template class SyncChannelInputGuard<csp::BufferPolicy::KeepOldest>;

template class SyncChannelOutputGuard<csp::BufferPolicy::Block>;
template class SyncChannelOutputGuard<csp::BufferPolicy::KeepNewest>;
template class SyncChannelOutputGuard<csp::BufferPolicy::KeepOldest>;

} // namespace csp::internal
