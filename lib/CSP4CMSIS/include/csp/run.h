#ifndef CSP_WRAPPER_H
#define CSP_WRAPPER_H

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <tuple>
#include <vector>
#include "csp4cmsis.h" 

// --- 1. START CSP NAMESPACE (For Definitions) ---
namespace csp {
    class CSProcess; // Defined in process.h
    
    // *** NEW: Execution Mode for Run Overload ***
    enum class ExecutionMode {
        TerminatingNetwork, // Blocking: Executes first process, waits for the others. (Original 'Run')
        StaticNetwork       // Non-blocking: Spawns ALL processes as new tasks, returns immediately. (New requirement)
    };
    
    // --- Internal Task Context DEFINITION ---
    struct TaskCtx {
        CSProcess* process;
        SemaphoreHandle_t completion_sem;
    };
} // end namespace csp definition block

// --- 2. The Globally Friended Task Wrapper (DECLARATION ONLY) ---
extern "C" {
    void ThreadFuncWrapper(void* pvParameters);
}


// --- 3. Continue CSP Namespace (For Template Logic) ---
namespace csp { 

// --- Parallel Helper ---
template <typename... Processes>
class ParallelHelper {
private:
    std::tuple<Processes&...> procs;

    // Helper to spawn a task for a specific process index
    template <std::size_t I>
    void spawn_task(SemaphoreHandle_t sem, UBaseType_t priority) {
        // Uses the now-defined TaskCtx
        TaskCtx* ctx = new TaskCtx{ &std::get<I>(procs), sem };
        
        xTaskCreate(
            (TaskFunction_t)ThreadFuncWrapper, 
            std::get<I>(procs).name(),
            256, 
            ctx,
            priority,
            NULL
        );
    }

    // Recursive spawner: Spawns tasks for indices 1 to N (skipping 0)
    template <std::size_t I>
    void spawn_others(SemaphoreHandle_t sem, UBaseType_t priority) {
        if constexpr (I < sizeof...(Processes)) {
            spawn_task<I>(sem, priority);
            spawn_others<I + 1>(sem, priority);
        }
    }
    
    // *** Spawns ALL processes (for non-blocking SPN launch) ***
    template <std::size_t I>
    void spawn_all(SemaphoreHandle_t sem, UBaseType_t priority) {
        if constexpr (I < sizeof...(Processes)) {
            spawn_task<I>(sem, priority);
            spawn_all<I + 1>(sem, priority);
        }
    }

public:
    explicit ParallelHelper(Processes&... p) : procs(p...) {}

    // 1. *** Renamed/Modified: Standard Blocking Run (ExecutionMode::TerminatingNetwork) ***
    void execute_terminating(UBaseType_t priority) {
        constexpr size_t num_procs = sizeof...(Processes);
        
        SemaphoreHandle_t done_sem = NULL;
        if constexpr (num_procs > 1) {
             done_sem = xSemaphoreCreateCounting(num_procs - 1, 0);
             spawn_others<1>(done_sem, priority);
        }

        // Run the first process on the current stack
        std::get<0>(procs).run(); 

        if (done_sem) {
            for (size_t i = 1; i < num_procs; ++i) {
                xSemaphoreTake(done_sem, portMAX_DELAY);
            }
            vSemaphoreDelete(done_sem);
        }
    }

    // 2. *** MODIFIED: Non-Blocking Run (ExecutionMode::StaticNetwork) ***
    void execute_static(UBaseType_t priority) {
        constexpr size_t num_procs = sizeof...(Processes);
        
        // 1. Spawn all processes *except* the first one (the intended orchestrator)
        if constexpr (num_procs > 1) {
             // Use spawn_others to launch w1, w2, w3, c1. 
             // Pass NULL for the semaphore since these tasks are perpetual and won't signal completion.
             spawn_others<1>(NULL, priority); 
        }

        // 2. Run the first process (f1) on the current stack. 
        // This thread (MainApp_Task) will be BLOCKED until f1.run() returns.
        std::get<0>(procs).run(); 
        
        // 3. The current thread (MainApp_Task) unblocks here when f1 finishes.
        
        // NOTE: There is no blocking wait for the spawned tasks (w1, w2, w3, c1) 
        // because they are perpetual SPN elements.
    }
};

// --- Public API Syntax ---

template <typename... Processes>
ParallelHelper<Processes...> InParallel(Processes&... procs) {
    return ParallelHelper<Processes...>(procs...);
}

// 1. Overloaded Run for Terminating Networks (Original behavior, implicitly uses TerminatingNetwork mode)
template <typename... Processes>
void Run(ParallelHelper<Processes...> helper) {
    helper.execute_terminating(tskIDLE_PRIORITY + 2);
}

// 2. *** NEW Overloaded Run (The requested change) ***
template <typename... Processes>
void Run(ParallelHelper<Processes...> helper, ExecutionMode mode) {
    if (mode == ExecutionMode::StaticNetwork) {
        helper.execute_static(tskIDLE_PRIORITY + 2);
    } else {
        // Fallback or explicit selection of TerminatingNetwork
        helper.execute_terminating(tskIDLE_PRIORITY + 2);
    }
}

} // namespace csp

#endif // CSP_WRAPPER_H