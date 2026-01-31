#include "csp/alt.h"
#include <cstdio>

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
    // Using standard FreeRTOS event group creation
    event_group = xEventGroupCreate();
}

unsigned int AltScheduler::select(Guard** guardArray, size_t amount, size_t offset) {
    if (amount == 0) return 0;

    const char* tname = pcTaskGetName(NULL);
    // printf("[%s] ALT: select start (guards: %u, offset: %u)\r\n", tname, amount, offset);

    EventBits_t wait_mask = 0;
    for(size_t i = 0; i < amount; ++i) wait_mask |= (1 << i);
    
    xEventGroupClearBits(event_group, wait_mask); 

    int ready_idx = -1;
    // Phase 1: Enable
    for(size_t i = 0; i < amount; ++i) {
        size_t idx = (i + offset) % amount; 
        // printf("[%s] ALT: Enabling guard %u...\r\n", tname, idx);
        
        if(guardArray[idx]->enable(this, (1 << idx))) { 
            // printf("[%s] ALT: Guard %u was ALREADY ready.\r\n", tname, idx);
            ready_idx = (int)idx; 
            break; 
        }
    }

    // Phase 2: Wait
    EventBits_t fired = 0;
    if (ready_idx != -1) {
        fired = (1 << ready_idx);
    } else {
        // printf("[%s] ALT: No guard ready. Sleeping on event group...\r\n", tname);
        fired = xEventGroupWaitBits(event_group, wait_mask, pdTRUE, pdFALSE, portMAX_DELAY);
        // printf("[%s] ALT: Woke up! Fired bits: 0x%lx\r\n", tname, fired);
    }

    // Identify which guard fired
    size_t selected = 0;
    for(size_t i = 0; i < amount; ++i) {
        if (fired & (1 << i)) { 
            selected = i; 
            break; 
        }
    }

    // Phase 3: Disable
    // printf("[%s] ALT: Disabling all guards.\r\n", tname);
    for(size_t i = 0; i < amount; ++i) {
        guardArray[i]->disable();
    }

    // Phase 4: Activate
    // printf("[%s] ALT: Activating guard %u.\r\n", tname, selected);
    guardArray[selected]->activate();
    
    return (unsigned int)selected;
}

void AltScheduler::wakeUp(EventBits_t bit) {
    if(!event_group) return;
    // printf("[%s] ALT: wakeUp called for bit 0x%lx\r\n", pcTaskGetName(NULL), bit);
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
    return (int)internal_alt.select(internal_guards, num_guards);
}

int Alternative::fairSelect() {
    if (num_guards <= 1) return priSelect();

    // Perform selection starting from our fairness index
    size_t actual_index = internal_alt.select(internal_guards, num_guards, fair_select_start_index);
    
    // Update index for next time
    fair_select_start_index = (actual_index + 1) % num_guards;

    return (int)actual_index;
}

} // namespace csp
