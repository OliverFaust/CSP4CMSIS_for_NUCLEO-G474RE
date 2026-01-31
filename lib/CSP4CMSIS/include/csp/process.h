// --- process.h (MODIFIED) ---
#ifndef CSP4CMSIS_PROCESS_H
#define CSP4CMSIS_PROCESS_H

#include <stddef.h> // For size_t, NULL definition

extern "C" {
    void ThreadFuncWrapper(void* pvParameters);
}

namespace csp {
    // Forward declarations of core internal classes
    namespace internal {
        class Kernel;
    }

    // =============================================================
    // CSP Public API Definition
    // =============================================================
    
    /**
     * @brief The abstract base class for all user-defined concurrent tasks (Processes).
     */
    class CSProcess {
    public:
        virtual ~CSProcess() = default;

        /**
         * @brief Returns the name of the process for FreeRTOS task registration.
         * Users can override this in their derived classes for better debugging.
         */
        virtual const char* name() const { return "csp_task"; } // FIX: Added default name

    protected:
        // C++CSP Standard: The primary process logic.
        virtual void run() = 0; 
        
        // C++CSP4CMSIS Extension: Called by the ThreadFuncWrapper upon completion.
        virtual void endProcess() {} 

    private:
        // The FreeRTOS wrapper function needs to access the protected run() method.
        friend void ::ThreadFuncWrapper(void* pvParameters);
    };

} // namespace csp

namespace csp::internal {
    
    // Alias 'Process' to the new public name 'CSProcess'
    using Process = csp::CSProcess;

    // Type alias for convenience when passing process pointers
    using ProcessPtr = Process*;
    
    #define NullProcessPtr (static_cast<csp::internal::ProcessPtr>(NULL))

} // namespace csp::internal

#endif // CSP4CMSIS_PROCESS_H