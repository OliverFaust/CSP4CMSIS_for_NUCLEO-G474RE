// --- barrier.cpp ---

#include "csp/barrier.h" // Barrier definition
#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdio>

namespace csp::internal {

// =============================================================
//  Barrier Implementation
// =============================================================

/**
 * @brief Constructs a reusable barrier for N processes.
 * @param N The maximum number of processes required to synchronize.
 */
Barrier::Barrier(size_t N) 
    // The barrier needs N arrivals (max_processes)
    : max_processes(N), count(0)
{
    // Create the Mutex to protect the shared counter
    xCountMutex = xSemaphoreCreateMutex();
    
    // Create the Semaphore used for blocking and release.
    // Initial count is 0 (all tasks block). Max count is N (allows N tasks to be released).
    xWaitSemaphore = xSemaphoreCreateCounting(N, 0); 

    if (xCountMutex == nullptr || xWaitSemaphore == nullptr) {
        printf("ERROR: Barrier synchronization object creation failed!\r\n");
    }
}

Barrier::~Barrier() {
    // 1. Check and delete the counting semaphore used for blocking/releasing
    if (xWaitSemaphore) { 
        vSemaphoreDelete(xWaitSemaphore);
    }
    // 2. Check and delete the mutex used for protecting the count variable
    if (xCountMutex) { 
        vSemaphoreDelete(xCountMutex);
    }
}

/**
 * @brief Blocks the calling task until all N processes have reached the barrier.
 */
void Barrier::sync() {
    // 1. Acquire the mutex to safely update the count
    xSemaphoreTake(xCountMutex, portMAX_DELAY);
    
    // Increment the arrival count
    count++;
    
    bool last_arrival = (count == max_processes);
    
    // Release the mutex
    xSemaphoreGive(xCountMutex);
    
    if (last_arrival) {
        // Last one in: Release all waiting tasks and reset the barrier.
        
        // Release N tasks
        for (size_t i = 0; i < max_processes; ++i) {
            // Give the semaphore N times to unblock all tasks blocked on xSemaphoreTake
            xSemaphoreGive(xWaitSemaphore); 
        }
        
        // Reset the counter for the next phase
        // NOTE: No mutex needed here as no other task is competing for the count yet.
        count = 0; 
        
    } else {
        // Not the last one in: Block and wait for the last arrival to release us.
        
        // Block until the xWaitSemaphore is available (given by the last arrival).
        // portMAX_DELAY ensures we wait indefinitely.
        xSemaphoreTake(xWaitSemaphore, portMAX_DELAY); 
    }
}

// Add the default destructor implementation if necessary to make the class concrete
// (Note: Already defined in the constructor/destructor block above)

} // namespace csp::internal

