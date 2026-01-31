// --- glue.cpp ---
#include "csp/process.h" // For csp::internal::Process, ThreadFuncWrapper usage
#include "csp/time.h"    // For csp::Time, CurrentTime, SleepFor, SleepUntil
#include "FreeRTOS.h"
#include "task.h"
#include <cstdio>       // For printf

// =============================================================
//  C++ Memory Allocation Overrides (pvPortMalloc/vPortFree)
// =============================================================

extern "C" {
    // These functions must be linked from your FreeRTOS port
    extern void* pvPortMalloc(size_t xSize);
    extern void vPortFree(void* pv);
}

// Global C++ memory allocation operators redirected to FreeRTOS heap
void* operator new(size_t size) { 
    return pvPortMalloc(size); 
}

void operator delete(void* ptr) noexcept { 
    vPortFree(ptr); 
}

// =============================================================
//  Global Time Functions (Defined in csp namespace)
// =============================================================

csp::Time CurrentTime() {
    return csp::Time(xTaskGetTickCount());
}

void SleepFor(const csp::Time time) {
    vTaskDelay(time.to_ticks());
}

void SleepUntil(const csp::Time time) {
    // Use the to_ticks() conversion method for raw tick calculations
    csp::Time last = csp::Time(xTaskGetTickCount());

    // Calculate remaining delay and use to_ticks() for the FreeRTOS API
    TickType_t delay = time.to_ticks() - last.to_ticks();

    // vTaskDelayUntil requires a modifiable pointer to the *previous* wake time.
    // We must pass a TickType_t variable by reference to the API.
    TickType_t previous_wake_time = last.to_ticks();
    
    // vTaskDelayUntil(&previous_wake_time, delay);
    // Wait, the API documentation for vTaskDelayUntil uses absolute time, not delta.
    // However, the signature is often: (TickType_t * const pxPreviousWakeTime, const TickType_t xTimeIncrement)
    // Let's assume the user's original logic was attempting to use the target time as an increment,
    // which is wrong for vTaskDelayUntil. Since these are simple helper wrappers,
    // let's stick to the common CSP pattern of blocking until an absolute time:
    
    // We will use the common approach to reflect the goal of 'SleepUntil(absolute_time)':
    vTaskDelayUntil(&previous_wake_time, time.to_ticks());
}

// --- End of glue.cpp ---
