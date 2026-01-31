#include "csp/sync_channel.h"
#include <cstring>
#include <cstdio>

namespace csp::internal {

// Interval to check for task suspension or timeouts during blocking
#define WAIT_SLICE_TICKS pdMS_TO_TICKS(100)

// =============================================================
// SyncChannel Core Implementation
// =============================================================

SyncChannel::SyncChannel()
    : mutex(xSemaphoreCreateMutex()),
      state(IDLE),
      data_ptr(nullptr),
      data_len(0),
      waiting_alt_in(nullptr),
      waiting_alt_bit_in(0),
      waiting_guard_in(nullptr),
      waiting_alt_out(nullptr),
      waiting_alt_bit_out(0),
      waiting_guard_out(nullptr),
      sender_queue(xQueueCreate(1, 0)),
      receiver_queue(xQueueCreate(1, 0)),
      res_in_guard(this),  
      res_out_guard(this)  
{}

SyncChannel::~SyncChannel() {
    if (mutex) vSemaphoreDelete(mutex);
    if (sender_queue) vQueueDelete(sender_queue);
    if (receiver_queue) vQueueDelete(receiver_queue);
}

void SyncChannel::reset() {
    state = IDLE;
    data_ptr = nullptr;
    waiting_alt_in = nullptr;
    waiting_alt_bit_in = 0;
    waiting_guard_in = nullptr;
    waiting_alt_out = nullptr;
    waiting_alt_bit_out = 0;
    waiting_guard_out = nullptr;
    // We do not reset queues here to avoid clearing pending ACKs during a race
}

// --- Blocking Output (Sender) ---
void SyncChannel::output(const void* const data_ptr_in) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;

    if (state == RECEIVER_WAITING) {
        // A receiver is already waiting (either blocking or in an ALT)
        data_ptr = data_ptr_in;
        
        AltScheduler* rx_alt = waiting_alt_in; 
        EventBits_t rx_alt_bit = waiting_alt_bit_in;
        bool is_alt_waiter = (waiting_alt_in != nullptr);

        waiting_alt_in = nullptr; 
        xSemaphoreGive(mutex);

        if (is_alt_waiter) {
            // Wake up the Alternative selection loop
            xEventGroupSetBits(rx_alt->getEventGroupHandle(), rx_alt_bit);
        } else {
            // Wake up a blocking input() call
            xQueueSend(receiver_queue, nullptr, 0);
        }

        // BLOCK: Wait for the Receiver to finish copying data and acknowledge
        while (xQueueReceive(sender_queue, nullptr, WAIT_SLICE_TICKS) != pdPASS);
    }
    else {
        // No receiver present, wait here as the primary sender
        state = SENDER_WAITING;
        data_ptr = data_ptr_in;
        xSemaphoreGive(mutex);
        
        // Wait until a receiver arrives and signals this queue
        while (xQueueReceive(sender_queue, nullptr, WAIT_SLICE_TICKS) != pdPASS);
    }
}

// --- Blocking Input (Receiver) ---
void SyncChannel::input(void* const data_ptr_out) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;

    if (state == SENDER_WAITING) {
        // Sender is already waiting; perform immediate transfer
        if (data_ptr != nullptr && data_ptr_out != nullptr) {
            memcpy(data_ptr_out, data_ptr, data_len);
        }

        AltScheduler* tx_alt = waiting_alt_out;
        bool is_alt_waiter = (waiting_alt_out != nullptr);
        EventBits_t tx_alt_bit = waiting_alt_bit_out;
        
        waiting_alt_out = nullptr; 

        if (is_alt_waiter) {
            xEventGroupSetBits(tx_alt->getEventGroupHandle(), tx_alt_bit);
        } else {
            // Release the blocking sender
            xQueueSend(sender_queue, nullptr, 0);
        }
        
        reset(); 
        xSemaphoreGive(mutex);
    }
    else {
        // No sender present, register as the waiting receiver
        state = RECEIVER_WAITING;
        xSemaphoreGive(mutex);

        // Wait for a sender to signal the receiver_queue
        while (xQueueReceive(receiver_queue, nullptr, WAIT_SLICE_TICKS) != pdPASS);

        // After waking, sender has provided data_ptr. Hold mutex to copy and ACK.
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (data_ptr != nullptr && data_ptr_out != nullptr) {
            memcpy(data_ptr_out, data_ptr, data_len);
        }
        xQueueSend(sender_queue, nullptr, 0); // Release Sender
        reset();
        xSemaphoreGive(mutex);
    }
}

// =============================================================
// ALT Registration Logic
// =============================================================

bool SyncChannel::registerAltIn(AltScheduler* alt, EventBits_t bit, SyncChannelInputGuard* guard) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    
    if (state == SENDER_WAITING) { 
        xSemaphoreGive(mutex); 
        return true; // Already ready for immediate activation
    }
    
    state = RECEIVER_WAITING;
    waiting_alt_in = alt;
    waiting_alt_bit_in = bit;
    waiting_guard_in = guard;
    xSemaphoreGive(mutex);
    return false;
}

bool SyncChannel::unregisterAltIn(AltScheduler* alt) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    bool completed = (state != RECEIVER_WAITING);
    if (!completed && waiting_alt_in == alt) reset();
    xSemaphoreGive(mutex);
    return completed;
}

bool SyncChannel::registerAltOut(AltScheduler* alt, EventBits_t bit, SyncChannelOutputGuard* guard) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    
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

bool SyncChannel::unregisterAltOut(AltScheduler* alt) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    bool completed = (state != SENDER_WAITING);
    if (!completed && waiting_alt_out == alt) reset();
    xSemaphoreGive(mutex);
    return completed;
}

// =============================================================
// Guard Implementation (The "Activation" Phase)
// =============================================================



bool SyncChannelInputGuard::enable(AltScheduler* alt, EventBits_t bit) {
    this->parent_alt = alt;
    return channel->registerAltIn(alt, bit, this);
}

bool SyncChannelInputGuard::disable() { 
    return channel->unregisterAltIn(parent_alt); 
}

void SyncChannelInputGuard::activate() {
    xSemaphoreTake(channel->getMutex(), portMAX_DELAY);
    
    // Transfer data from the waiting sender to the bound destination
    if (channel->getDataPtr() != nullptr && user_data_dest != nullptr) {
        memcpy(user_data_dest, channel->getDataPtr(), data_size);
    }
    
    // Finalize the rendezvous: Release the blocked sender
    xQueueSend(channel->getSenderQueue(), nullptr, 0);
    
    channel->reset();
    xSemaphoreGive(channel->getMutex());
}

bool SyncChannelOutputGuard::enable(AltScheduler* alt, EventBits_t bit) {
    this->parent_alt = alt;
    return channel->registerAltOut(alt, bit, this);
}

bool SyncChannelOutputGuard::disable() { 
    return channel->unregisterAltOut(parent_alt); 
}

void SyncChannelOutputGuard::activate() {
    xSemaphoreTake(channel->getMutex(), portMAX_DELAY);
    
    // Set the data pointer for the arriving receiver to find
    channel->setChannelData(user_data_source, data_size);
    
    // If a receiver was already blocking, wake them now
    if (channel->getState() == SyncChannel::RECEIVER_WAITING) {
        xQueueSend(channel->getReceiverQueue(), nullptr, 0);
    }
    
    // Note: We do NOT call reset() here; the receiver will call it after copying data.
    xSemaphoreGive(channel->getMutex());
}

} // namespace csp::internal
