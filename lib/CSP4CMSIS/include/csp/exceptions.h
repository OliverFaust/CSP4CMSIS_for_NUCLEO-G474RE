// --- exceptions.h ---
#ifndef CSP4CMSIS_EXCEPTIONS_H
#define CSP4CMSIS_EXCEPTIONS_H

#include <stdexcept>

namespace csp {

/**
 * @brief Exception thrown when an attempt is made to use a poisoned channel.
 * This is the standard termination mechanism in CSP.
 */
class PoisonException : public std::runtime_error {
public:
    PoisonException() : std::runtime_error("Channel operation failed due to poison.") {}
    
    // Virtual destructor is good practice
    virtual ~PoisonException() noexcept {}
};

// We will reserve DeadlockError for a later step (Idle Hook detection)
// class DeadlockError : public std::runtime_error {...};

} // namespace csp

#endif // CSP4CMSIS_EXCEPTIONS_H
