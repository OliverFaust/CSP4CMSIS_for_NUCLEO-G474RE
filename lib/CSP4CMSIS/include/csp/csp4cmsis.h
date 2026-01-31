// --- csp4cmsis.h (Finalized SPN Structure with Process Composition) ---
#ifndef CSP4CMSIS_H
#define CSP4CMSIS_H

// ======================================================================
// 1. Core Definitions (Must be available to both C and C++ sections)
// ======================================================================
#include "FreeRTOS.h" // Assuming needed globally
#include "time.h"     // Defines csp::Time, etc. (Must be C++-safe)
#include "process.h"  // Defines csp::internal::Process base (Must be C++-safe)


// ======================================================================
// 2. C++ API Block (Only for C++ Compiler)
// ======================================================================
#ifdef __cplusplus
namespace csp { /* Forward declare namespace content here if needed */ }
// Include all C++-specific headers that define classes/templates.
#include "alt.h"             // Required for ALT functionality
#include "channel_base.h"    // Base classes for internal channel implementations
#include "sync_channel.h"    // Core Rendezvous/Alt implementation
#include "buffered_channel.h"// For future implementation
#include "barrier.h"         // Standard CSP primitive
#include "public_channel.h"  // Includes One2OneChannel<T>
#include "public_task.h"     // Includes CSProcess, Run() function
#include "run.h"             // <--- NEW: Includes InParallel/InSequence helpers

// Note: The file public_task.h should now contain the definition/declaration 
// of the base Run(CSProcess&, UBaseType_t) function signature.

#endif // __cplusplus


// ======================================================================
// 3. C/C++ Linkage Control Block (Wraps only C-callable symbols)
// ======================================================================
#ifdef __cplusplus
// Start extern "C" block for C functions
extern "C" {
#endif

// 4. External Hooks (Declared for both C and C++ linkers)
void csp_app_main_init(void);
void ThreadFuncWrapper(void *pvParameters); // FreeRTOS Task Entry Point

// 5. End of Linkage Control
#ifdef __cplusplus
} // extern "C"
#endif

#endif // CSP4CMSIS_H