#ifndef CSP_WRAPPER_H
#define CSP_WRAPPER_H

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <tuple>
#include <utility>
#include <cstdio>
#include "csp4cmsis.h"

// --- 1. START CSP NAMESPACE (For Definitions) ---
namespace csp {
    class CSProcess; // Defined in process.h
    // TaskCtx is defined in process.h (needed there so public_task.h can
    // see it too -- csp4cmsis.h includes public_task.h before run.h).

    enum class ExecutionMode {
        TerminatingNetwork, // Blocking: spawns all processes, waits for all to finish.
        StaticNetwork        // Non-blocking: spawns all processes, returns immediately.
    };
} // end namespace csp definition block

// --- 2. The Globally Friended Task Wrapper (DECLARATION ONLY) ---
extern "C" {
    void ThreadFuncWrapper(void* pvParameters);
}


// --- 3. Continue CSP Namespace (For Template Logic) ---
namespace csp {

// Historical composition-wide default priority (unchanged from pre-1.2).
#ifndef CSP_LEGACY_PARALLEL_PRIORITY
#define CSP_LEGACY_PARALLEL_PRIORITY (tskIDLE_PRIORITY + 2)
#endif

// --- Parallel Helper ---
template <typename... Processes>
class ParallelHelper {
private:
    std::tuple<Processes&...> procs;
    static constexpr size_t num_procs = sizeof...(Processes);

    // API 1.3: spawns process I with ITS OWN declared stack/priority.
    // Stack/TCB/TaskCtx are all owned by the process itself
    // (CSProcessStatic<N>, see process.h) -- xTaskCreateStatic() makes no
    // heap allocation, and neither does preparing its TaskCtx.
    //
    // Note: TaskCtx deliberately does NOT live as a ParallelHelper member.
    // ParallelHelper instances (and the temporaries InParallel(...)
    // produces) do not have a lifetime guarantee matching the tasks they
    // spawn -- Run() below takes one by value, for instance. Only the
    // CSProcess objects themselves are contractually static, so that's
    // where TaskCtx storage has to live.
    template <std::size_t I>
    void spawn_task(SemaphoreHandle_t sem, UBaseType_t composition_priority) {
        CSProcess& proc = std::get<I>(procs);

        TaskCtx* ctx = proc.prepareTaskCtx(sem);
        UBaseType_t priority = resolveTaskPriority(proc, composition_priority);

        TaskHandle_t handle = xTaskCreateStatic(
            (TaskFunction_t)ThreadFuncWrapper,
            proc.name(),
            proc.stackWords(),
            ctx,
            priority,
            proc.stackBuffer(),
            proc.taskBuffer()
        );

        proc.setTaskHandle(handle);

        if (handle == NULL) {
            printf("FATAL ERROR: Failed to create FreeRTOS task for CSProcess '%s' "
                   "(xTaskCreateStatic returned NULL -- check stack/TCB buffers).\r\n",
                   proc.name());
        }
    }

    // Spawns ALL processes, indices 0..N-1.
    template <std::size_t I>
    void spawn_all(SemaphoreHandle_t sem, UBaseType_t composition_priority) {
        if constexpr (I < sizeof...(Processes)) {
            spawn_task<I>(sem, composition_priority);
            spawn_all<I + 1>(sem, composition_priority);
        }
    }

    template <std::size_t I, typename Func>
    void forEachProcessImpl(Func&& f) {
        if constexpr (I < sizeof...(Processes)) {
            f(static_cast<CSProcess&>(std::get<I>(procs)));
            forEachProcessImpl<I + 1>(std::forward<Func>(f));
        }
    }

public:
    explicit ParallelHelper(Processes&... p) : procs(p...) {}

    // 1. Blocking Run (ExecutionMode::TerminatingNetwork).
    // API 1.2: spawns ALL N processes (including index 0) as their own
    // tasks and blocks the CALLING task until all N have completed.
    // Previously, index 0 ran inline on the caller's stack; the caller
    // now does no CSP work of its own and can safely self-delete once
    // this returns, if it has nothing further to do.
    void execute_terminating(UBaseType_t composition_priority) {
        // API 1.3: static semaphore buffer instead of
        // xSemaphoreCreateCounting(), which allocates from the heap.
        // Local (automatic) storage is fine: this function blocks until
        // every process has signaled done_sem, so the buffer only needs
        // to outlive that wait, not the ParallelHelper itself.
        static StaticSemaphore_t done_sem_storage;
        SemaphoreHandle_t done_sem =
            xSemaphoreCreateCountingStatic(num_procs, 0, &done_sem_storage);

        spawn_all<0>(done_sem, composition_priority);

        for (size_t i = 0; i < num_procs; ++i) {
            xSemaphoreTake(done_sem, portMAX_DELAY);
        }
        vSemaphoreDelete(done_sem); // releases the handle, not done_sem_storage's memory
    }

    // 2. Non-Blocking Run (ExecutionMode::StaticNetwork).
    // API 1.2: spawns ALL N processes (including index 0) and returns
    // immediately. No process runs on the calling task's stack.
    void execute_static(UBaseType_t composition_priority) {
        spawn_all<0>(NULL, composition_priority);
    }

    /**
     * @brief Invokes f(CSProcess&) for every process in this composition,
     * in declaration order. Useful for post-spawn bookkeeping such as a
     * periodic stack-usage report -- see csp4cmsis_spn.cpp.
     */
    template <typename Func>
    void forEachProcess(Func&& f) {
        forEachProcessImpl<0>(std::forward<Func>(f));
    }
};

// --- Public API Syntax ---

template <typename... Processes>
ParallelHelper<Processes...> InParallel(Processes&... procs) {
    return ParallelHelper<Processes...>(procs...);
}

// 1. Terminating-network Run(). 'priority' is the COMPOSITION-WIDE
// default: it applies to any process that hasn't overridden
// taskPriority(). The default value matches pre-1.2 behavior exactly.
template <typename... Processes>
void Run(ParallelHelper<Processes...> helper,
         UBaseType_t priority = CSP_LEGACY_PARALLEL_PRIORITY) {
    helper.execute_terminating(priority);
}

// 2. Explicit ExecutionMode selection. Same priority semantics as (1).
template <typename... Processes>
void Run(ParallelHelper<Processes...> helper, ExecutionMode mode,
         UBaseType_t priority = CSP_LEGACY_PARALLEL_PRIORITY) {
    if (mode == ExecutionMode::StaticNetwork) {
        helper.execute_static(priority);
    } else {
        helper.execute_terminating(priority);
    }
}

} // namespace csp

#endif // CSP_WRAPPER_H