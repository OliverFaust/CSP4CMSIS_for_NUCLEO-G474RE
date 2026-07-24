// --- public_task.h (Refactored for SPN, API 1.3: static allocation) ---
#ifndef CSP4CMSIS_PUBLIC_TASK_H
#define CSP4CMSIS_PUBLIC_TASK_H

#include "csp4cmsis.h" // Includes CSProcess, ThreadFuncWrapper, TaskCtx, etc.
#include "FreeRTOS.h"
#include "task.h"
#include <cstdio>

// Define a default priority for user processes
#define CSP_DEFAULT_TASK_PRIORITY (configMAX_PRIORITIES - 1)

extern "C" void ThreadFuncWrapper(void* pvParameters);

namespace csp {

/**
 * @brief Launches a single CSProcess as a FreeRTOS task, enforcing the Static Process Network (SPN) model.
 * * CRITICAL SPN REQUIREMENT: The CSProcess object MUST be allocated STATICALLY
 * by the application (e.g., as a global or static local variable). Its stack
 * buffer, TCB, and TaskCtx (all owned by CSProcess/CSProcessStatic<N>, see
 * process.h) inherit that same static storage duration -- xTaskCreateStatic()
 * performs no heap allocation of its own.
 * * @param process Reference to the STATICALLY allocated CSProcess object.
 * @param priority The FreeRTOS priority for this task.
 */
inline void Run(CSProcess& process, UBaseType_t priority = CSP_DEFAULT_TASK_PRIORITY) {

    UBaseType_t effective_priority = resolveTaskPriority(process, priority);

    // API 1.3: TaskCtx now lives inside `process` itself (static storage,
    // same lifetime as its stack/TCB) -- prepareTaskCtx() just fills it in
    // and hands back a pointer. No allocation, no caller-owned storage.
    TaskCtx* ctx = process.prepareTaskCtx(/*completion_sem=*/nullptr);

    TaskHandle_t handle = xTaskCreateStatic(
        ThreadFuncWrapper,
        "CSP_PROC",
        process.stackWords(),
        ctx,
        effective_priority,
        process.stackBuffer(),
        process.taskBuffer()
    );

    process.setTaskHandle(handle); // NULL on failure -- fine, stackHighWaterMarkWords() treats that as "unavailable"

    if (handle == NULL) {
        printf("FATAL ERROR: Failed to create FreeRTOS task for CSProcess "
               "(xTaskCreateStatic returned NULL -- check stack/TCB buffers).\r\n");
    }
}

/**
 * @brief Pauses the current process for a specified number of ticks.
 * Maps directly to FreeRTOS vTaskDelay.
 * @param ticks_to_sleep The number of RTOS ticks to pause.
 */
inline void SleepFor(TickType_t ticks_to_sleep) {
    vTaskDelay(ticks_to_sleep);
}

// NOTE: For full C++CSP compatibility, you would also define helper time functions here,
// but they are omitted for simplicity as per the plan.

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_TASK_H