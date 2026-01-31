// --- csp_wrapper.cpp (New Source File) ---

#include "csp/run.h" // Includes the declaration of ThreadFuncWrapper and the definition of csp::TaskCtx
#include "FreeRTOS.h" // May be needed if run.h doesn't include it implicitly
#include "task.h"

// Define the function using the definition that was removed from the header.
extern "C" {
    void ThreadFuncWrapper(void* pvParameters) {
        // Use the fully qualified name to ensure proper scope resolution
        csp::TaskCtx* ctx = static_cast<csp::TaskCtx*>(pvParameters); 
        
        // 1. Run the process logic 
        ctx->process->run();
        
        // 2. Signal completion
        if (ctx->completion_sem) {
            xSemaphoreGive(ctx->completion_sem);
        }
        
        // 3. Clean up generic wrapper info
        delete ctx;
        
        // 4. Delete this FreeRTOS task
        vTaskDelete(NULL);
    }
}
