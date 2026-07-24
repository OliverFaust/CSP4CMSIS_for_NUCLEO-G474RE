// --- time.h (CORRECTED) ---
#ifndef CSP4CMSIS_TIME_H
#define CSP4CMSIS_TIME_H

// These two includes should remain outside the C++ block as they are C headers
// and provide the C type TickType_t.
#include "FreeRTOS.h"
#include <stdint.h>

// ------------------------------------------------------------------
// CRITICAL FIX: Only expose C++ syntax when compiling with a C++ compiler
// ------------------------------------------------------------------
#ifdef __cplusplus 
namespace csp {

/**
 * @brief Represents a duration or absolute time point in a type-safe manner.
 * Encapsulates the underlying FreeRTOS TickType_t and provides conversion helpers.
 */
struct Time {
    // The internal representation is the raw tick count
    TickType_t ticks;

    // Default constructor for Time()
    Time() : ticks(0) {}

    // Constructor required for the Time unit helpers (e.g., Seconds())
    explicit Time(TickType_t t) : ticks(t) {}

    /**
    * @brief Converts the Time object into the raw TickType_t for FreeRTOS API calls.
    */
    TickType_t to_ticks() const {
        return ticks;
    }
};

// ----------------------------------------------------
// C++CSP Style Time Unit Helpers
// ----------------------------------------------------

/**
 * @brief Creates a csp::Time duration representing a number of seconds.
 */
inline Time Seconds(uint32_t s) {
    return Time((TickType_t)s * configTICK_RATE_HZ);
}

/**
 * @brief Creates a csp::Time duration representing a number of milliseconds.
 */
inline Time Milliseconds(uint32_t ms) {
    return Time((TickType_t) ((ms * configTICK_RATE_HZ) / 1000));
}

} // namespace csp
#endif // __cplusplus

#endif // CSP4CMSIS_TIME_H
