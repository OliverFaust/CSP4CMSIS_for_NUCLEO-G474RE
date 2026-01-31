#include "csp/alt_channel_sync.h"
#include <cstring>
#include <cstdio>

namespace csp::internal {

AltChanSyncBase::AltChanSyncBase() : 
    mutex(nullptr), waiting_in_task(nullptr), waiting_out_task(nullptr),
    non_alt_in_data_ptr(nullptr), non_alt_out_data_ptr(nullptr) 
{
    mutex = xSemaphoreCreateMutex();
}

AltChanSyncBase::~AltChanSyncBase() {
    if (mutex != nullptr) vSemaphoreDelete(mutex);
}

bool AltChanSyncBase::tryHandshake(void* data_ptr, size_t size, bool is_writer) {
    if (is_writer) {
        // 1. Check for a standard blocking receiver
        if (waiting_in_task != nullptr) {
            if (non_alt_in_data_ptr && data_ptr) memcpy(non_alt_in_data_ptr, data_ptr, size);
            TaskHandle_t t = waiting_in_task;
            clearWaitingIn();
            xTaskNotifyGive(t);
            return true; 
        }
        // 2. Check for a receiver waiting in an ALT
        if (waiting_in_alt.alt_ptr != nullptr) {
            if (waiting_in_alt.data_ptr && data_ptr) memcpy(waiting_in_alt.data_ptr, data_ptr, size);
            // Note: We don't notify here; the Producer's output() call will call wakeUp()
            return true;
        }
    } else {
        // 1. Check for a standard blocking sender
        if (waiting_out_task != nullptr) {
            if (data_ptr && non_alt_out_data_ptr) memcpy(data_ptr, non_alt_out_data_ptr, size);
            TaskHandle_t t = waiting_out_task;
            clearWaitingOut();
            xTaskNotifyGive(t);
            return true;
        }
        // 2. Check for a sender waiting in an ALT
        if (waiting_out_alt.alt_ptr != nullptr) {
            if (data_ptr && waiting_out_alt.data_ptr) memcpy(data_ptr, waiting_out_alt.data_ptr, size);
            return true;
        }
    }
    return false;
}

void AltChanSyncBase::registerWaitingTask(void* data_ptr, bool is_writer) {
    if (is_writer) {
        waiting_out_task = xTaskGetCurrentTaskHandle();
        non_alt_out_data_ptr = data_ptr;
    } else {
        waiting_in_task = xTaskGetCurrentTaskHandle();
        non_alt_in_data_ptr = data_ptr;
    }
}

// --- ChanInGuard Implementation ---
bool ChanInGuard::enable(AltScheduler* alt, EventBits_t bit) {
    if (xSemaphoreTake(parent_channel->getMutex(), portMAX_DELAY) != pdTRUE) return false;

    // Check if a sender is already waiting (Standard output() call)
    if (parent_channel->getWaitingOutTask() != nullptr) {
        xSemaphoreGive(parent_channel->getMutex());
        return true; 
    }

    // Register our AltScheduler for wake-up
    parent_channel->getWaitingInAlt().set(alt, bit, user_data_dest, data_size);
    
    xSemaphoreGive(parent_channel->getMutex());
    return false;
}

void ChanInGuard::activate() {
    if (xSemaphoreTake(parent_channel->getMutex(), portMAX_DELAY) != pdTRUE) return;
    
    TaskHandle_t sender = parent_channel->getWaitingOutTask();
    if (sender != nullptr) {
        if (user_data_dest && parent_channel->getNonAltOutDataPtr()) 
            memcpy(user_data_dest, parent_channel->getNonAltOutDataPtr(), data_size);
        parent_channel->clearWaitingOut();
        xSemaphoreGive(parent_channel->getMutex());
        xTaskNotifyGive(sender);
    } else {
        // Data was already copied during tryHandshake in output()
        xSemaphoreGive(parent_channel->getMutex());
    }
}

bool ChanInGuard::disable() {
    if (xSemaphoreTake(parent_channel->getMutex(), portMAX_DELAY) != pdTRUE) return false;
    bool was_ready = (parent_channel->getWaitingOutTask() != nullptr);
    parent_channel->getWaitingInAlt().clear();
    xSemaphoreGive(parent_channel->getMutex());
    return was_ready;
}

// --- ChanOutGuard Implementation ---
bool ChanOutGuard::enable(AltScheduler* alt, EventBits_t bit) {
    if (xSemaphoreTake(parent_channel->getMutex(), portMAX_DELAY) != pdTRUE) return false;

    if (parent_channel->getWaitingInTask() != nullptr) {
        xSemaphoreGive(parent_channel->getMutex());
        return true;
    }

    parent_channel->getWaitingOutAlt().set(alt, bit, const_cast<void*>(user_data_source), data_size);
    
    xSemaphoreGive(parent_channel->getMutex());
    return false;
}

void ChanOutGuard::activate() {
    if (xSemaphoreTake(parent_channel->getMutex(), portMAX_DELAY) != pdTRUE) return;
    
    TaskHandle_t receiver = parent_channel->getWaitingInTask();
    if (receiver != nullptr) {
        if (parent_channel->getNonAltInDataPtr() && user_data_source)
            memcpy(parent_channel->getNonAltInDataPtr(), user_data_source, data_size);
        parent_channel->clearWaitingIn();
        xSemaphoreGive(parent_channel->getMutex());
        xTaskNotifyGive(receiver);
    } else {
        xSemaphoreGive(parent_channel->getMutex());
    }
}

bool ChanOutGuard::disable() {
    if (xSemaphoreTake(parent_channel->getMutex(), portMAX_DELAY) != pdTRUE) return false;
    bool was_ready = (parent_channel->getWaitingInTask() != nullptr);
    parent_channel->getWaitingOutAlt().clear();
    xSemaphoreGive(parent_channel->getMutex());
    return was_ready;
}

} // namespace csp::internal
