#include "csp/alt.h"
#include <cstdio>
// Ensure the CMSIS compiler intrinsics are available (often included via device headers)
extern "C" {
    #include <cmsis_compiler.h>
}

namespace csp::internal {

// =============================================================
// AltScheduler Implementation
// =============================================================
AltScheduler::AltScheduler() { 
    initForCurrentTask(); 
}

AltScheduler::~AltScheduler() { 
    if (event_group) vEventGroupDelete(event_group); 
}

void AltScheduler::initForCurrentTask() {
    waiting_task_handle = xTaskGetCurrentTaskHandle();
    event_group = xEventGroupCreate();
}

unsigned int AltScheduler::select(Guard** guardArray, size_t amount, size_t offset) {
    if (amount == 0) return 0;

    EventBits_t wait_mask = 0;
    for(size_t i = 0; i < amount; ++i) wait_mask |= (1 << i);
    
    xEventGroupClearBits(event_group, wait_mask); 

    int ready_idx = -1;

    // Phase 1: Enable
    // We poll guards starting from 'offset' to support Fair ALT
    for(size_t i = 0; i < amount; ++i) {
        size_t idx = (i + offset) % amount; 
        
        // If enable() returns true, that guard is ready IMMEDIATELY
        if(guardArray[idx]->enable(this, (1 << idx))) { 
            ready_idx = (int)idx; 
            break; 
        }
    }

    // Phase 2: Wait
    EventBits_t fired = 0;
    if (ready_idx != -1) {
        // We already found a winner in the Enable phase
        fired = (1 << ready_idx);
    } else {
        // Block until a channel becomes ready or a timer expires
        fired = xEventGroupWaitBits(event_group, wait_mask, pdTRUE, pdFALSE, portMAX_DELAY);
    }

    // Phase 2.5: Identify which guard fired using GCC intrinsic for O(1) selection
    size_t selected = 0;
    if (fired != 0) {
        // Create a mask to only look at bits from 'offset' up to 31
        uint32_t offset_mask = 0xFFFFFFFFU << offset;
        uint32_t masked_fired = fired & offset_mask;
        
        if (masked_fired != 0) {
            // A guard at or above the fairness offset fired.
            uint32_t lowest_set_bit = masked_fired & -(int32_t)masked_fired;
            selected = 31 - __builtin_clz(lowest_set_bit);
        } else {
            // Wrap-around: only guards strictly below the offset fired.
            uint32_t lowest_set_bit = fired & -(int32_t)fired;
            selected = 31 - __builtin_clz(lowest_set_bit);
        }
    }
    
    // Phase 3: Disable
    // Crucial: We tell guards we are leaving. If disable() returns true for a 
    // guard we didn't 'select', it means a rendezvous almost happened but we 
    // missed it—this is handled by the internal channel state machines.
    for(size_t i = 0; i < amount; ++i) {
        guardArray[i]->disable();
    }

    // Phase 4: Activate
    // The "Commit" phase. For Rendezvous, this moves the data.
    // For sampling channels, this might result in a 'no-op' if data was dropped.
    guardArray[selected]->activate();
    
    return (unsigned int)selected;
}

void AltScheduler::wakeUp(EventBits_t bit) {
    if(!event_group) return;

    if (xPortIsInsideInterrupt()) {
        BaseType_t woken = pdFALSE;
        xEventGroupSetBitsFromISR(event_group, bit, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        xEventGroupSetBits(event_group, bit);
    }
}

// =============================================================
// TimerGuard Implementation
// =============================================================
TimerGuard::TimerGuard(csp::Time delay) 
    : parent_alt(nullptr), delay_ticks(delay.to_ticks()), assigned_bit(0) 
{
    timer_handle = xTimerCreate("CspTmr", delay_ticks, pdFALSE, this, TimerCallback);
}

TimerGuard::~TimerGuard() { 
    if (timer_handle) xTimerDelete(timer_handle, 0); 
}

void TimerGuard::TimerCallback(TimerHandle_t x) {
    auto* s = static_cast<TimerGuard*>(pvTimerGetTimerID(x));
    if(s && s->parent_alt) {
        s->parent_alt->wakeUp(s->assigned_bit);
    }
}

bool TimerGuard::enable(AltScheduler* a, EventBits_t b) {
    parent_alt = a; 
    assigned_bit = b;
    if (delay_ticks == 0) return true; // Instant timeout
    xTimerStart(timer_handle, 0);
    return false; 
}

bool TimerGuard::disable() { 
    if (timer_handle) xTimerStop(timer_handle, 0); 
    return true; 
}

void TimerGuard::activate() {}

} // namespace csp::internal

namespace csp {

// =============================================================
// Alternative Implementation
// =============================================================

Alternative::Alternative(std::initializer_list<internal::Guard*> list) {
    num_guards = 0;
    for (auto* g : list) {
        if (num_guards < MAX_GUARDS) internal_guards[num_guards++] = g;
    }
}

Alternative::Alternative(std::initializer_list<Guard*> list) {
    num_guards = 0;
    for (auto* g : list) {
        if (num_guards < MAX_GUARDS) internal_guards[num_guards++] = g->internal_guard_ptr;
    }
}

int Alternative::priSelect() {
    return (int)internal_alt.select(internal_guards, num_guards, 0);
}

int Alternative::fairSelect() {
    if (num_guards <= 1) return priSelect();

    size_t actual_index = internal_alt.select(internal_guards, num_guards, fair_select_start_index);
    
    // Update fairness index to ensure next guard has priority next time
    fair_select_start_index = (actual_index + 1) % num_guards;

    return (int)actual_index;
}

} // namespace csp

