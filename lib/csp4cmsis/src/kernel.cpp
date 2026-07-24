// Required headers for CMSIS-RTOS V2 and FreeRTOS
//#include "cmsis_os2.h" 
#include "FreeRTOS.h"
#include "task.h" 

// --- Conceptual CSP Classes (Forward Declarations) ---
// These declarations allow the wrapper to interact with the C++ objects.
namespace csp
{
	namespace internal
	{
		class Process 
		{
		public:
			// The primary method that contains the CSP sequential logic (the task body).
			virtual void runProcess() = 0; 
			// Cleanup method for resources owned by the CSP process object.
			virtual void endProcess() = 0; 
            // Virtual destructor ensures proper cleanup of derived classes.
            virtual ~Process() = default; 
		};
		using ProcessPtr = Process*;
	}
}

// ----------------------------------------------------
// The C-to-C++ Task Wrapper Function
// ----------------------------------------------------

/**
 * @brief The FreeRTOS task entry point for all CSP processes.
 * 
 * This function handles the C-to-C++ type bridging, executes the CSP process logic,
 * and ensures the RTOS task is cleaned up afterward. It MUST have C linkage.
 * 
 * @param pvParameters Pointer to the csp::internal::Process object instance.
 */
/*extern "C" void ThreadFuncWrapper(void *pvParameters)
{
    // CORRECTED LINE: Access ProcessPtr directly from the csp::internal namespace.
    // REMOVE the class scope resolution: ::Process::
	csp::internal::ProcessPtr mainProcess = 
        static_cast<csp::internal::ProcessPtr>(pvParameters);

	// Ensure a valid pointer was passed before proceeding (critical safety check)
	if (mainProcess)
	{
		// 1. Execute the C++ Process Logic (The Task Body)
        // Note: The original Kent code used try/catch. This is removed here
        // as exceptions are typically disabled (-fno-exceptions) in embedded C++.
        // Any unhandled error inside runProcess() will rely on the RTOS's fatal error handler.
		mainProcess->runProcess();

		// 2. Process Cleanup
		// Call Kent's end-of-process methods to clean up internal CSP resources.
		mainProcess->endProcess();
        
        // 3. Delete the C++ object associated with the task (assuming dynamic allocation
        // and that the CSP kernel owns the lifetime of the process object).
        delete mainProcess;

		// 4. Terminate the RTOS Task
		// Use the FreeRTOS API function to delete the task itself, reclaiming its 
        // TCB and stack memory back to the heap.[2, 1]
		vTaskDelete(NULL); 
	}
	else
	{
		// Critical Failure: Invalid process pointer received.
		// In a high-assurance system, this should trigger a system assert/halt.
        // configASSERT(pdFALSE); 
	}
}*/

// ----------------------------------------------------
