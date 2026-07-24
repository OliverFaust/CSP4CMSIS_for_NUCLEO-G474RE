#ifndef CSP4CMSIS_CHANNEL_BASE_H
#define CSP4CMSIS_CHANNEL_BASE_H

#include <stddef.h> 

namespace csp {

    enum class BufferPolicy {
        Block,      // Standard CSP: block until partner arrives
        KeepNewest, // Sampling: overwrite oldest if full/no partner
        KeepOldest  // Sampling: discard new if full/no partner
    };
    
    template <typename T> class Chanin;
    template <typename T> class Chanout;
    class Alternative; 
}

namespace csp::internal {

    class Guard; 

    /**
     * @brief The core contract for CSP communication.
     * Updated to support both Synchronous (Rendezvous) and Asynchronous (Buffered) logic.
     */
    template <typename DATA_TYPE>
    class BaseChan 
    {
    public:
        template <typename U>
        friend class ::csp::Chanin; 

        template <typename U>
        friend class ::csp::Chanout;
        
    protected:
        inline virtual ~BaseChan() = default;

        virtual void input(DATA_TYPE* const dest) = 0;
        virtual void output(const DATA_TYPE* const source) = 0;
        
        virtual void beginExtInput(DATA_TYPE* const dest) = 0;
        virtual void endExtInput() = 0;
    }; 
    
    /**
     * @brief Extends BaseChan with methods required for ALT, Polling, and ISRs.
     */
    template <typename DATA_TYPE>
    class BaseAltChan : public BaseChan<DATA_TYPE>
    {
    public:
        /** @brief Returns true if data is waiting to be read. */
        virtual bool pending() = 0;

        /** @brief Returns true if a write operation will not block. 
         * For KeepNewest/KeepOldest, this is effectively always true. 
         */
        virtual bool space_available() = 0;

        /** @brief ISR-safe non-blocking write. */
        virtual bool putFromISR(const DATA_TYPE& data) = 0;

        virtual internal::Guard* getInputGuard(DATA_TYPE& dest) = 0;
        virtual internal::Guard* getOutputGuard(const DATA_TYPE& source) = 0;
        
    public:
        inline virtual ~BaseAltChan() = default;

        friend class ::csp::Alternative; 
    };

    // =============================================================
    // BaseAltChan (FULL SPECIALIZATION for void)
    // =============================================================
    template <>
    class BaseAltChan<void> : public BaseChan<void> {
    public:
        virtual bool pending() = 0;
        virtual bool space_available() = 0;
        virtual bool putFromISR() = 0; // Added for signal consistency
        virtual Guard* getInputGuard() = 0;
        virtual Guard* getOutputGuard() = 0;
    };

} // namespace csp::internal

#endif // CSP4CMSIS_CHANNEL_BASE_H