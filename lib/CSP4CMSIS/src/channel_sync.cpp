// --- channel_sync.cpp (Final Corrected Signatures) ---
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <cstdio>

// This file previously contained conflicting definitions for AltChanSyncBase.
// Those have been consolidated into alt_channel_sync.cpp and alt_channel_sync.h.

namespace csp::internal {

    // Placeholder for future purely blocking channel synchronization (ChanSyncBase)
    // if required to be separate from the Alt-capable logic.
    
    // Currently empty to prevent Linker "multiple definition" errors.

} // namespace csp::internal