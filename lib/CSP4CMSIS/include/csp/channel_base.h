// --- channel_base.h (HARD SYNCHRONIZATION ONLY) ---
#ifndef CSP4CMSIS_CHANNEL_BASE_H
#define CSP4CMSIS_CHANNEL_BASE_H

#include <stddef.h> 

namespace csp {
    template <typename T> class Chanin;
    template <typename T> class Chanout;
    class Alternative; 
}

namespace csp::internal {

    class Guard; 

    /**
     * @brief The core contract for a CSP communication channel.
     */
    template <typename DATA_TYPE>
    class BaseChan 
    {
    public:
        template <typename U>
        friend class csp::Chanin; 

        template <typename U>
        friend class csp::Chanout;
        
    protected:
        inline virtual ~BaseChan() = default;

        virtual void input(DATA_TYPE* const dest) = 0;
        virtual void output(const DATA_TYPE* const source) = 0;
        
        virtual void beginExtInput(DATA_TYPE* const dest) = 0;
        virtual void endExtInput() = 0;
    }; 
    
    /**
     * @brief Extends BaseChan with methods required for ALT and Polling.
     */
    template <typename DATA_TYPE>
    class BaseAltChan : public BaseChan<DATA_TYPE>
    {
    public:
        /**
         * @brief Polling method to check if a communication partner is ready.
         * Added to resolve 'override' errors in derived channel implementations.
         */
        virtual bool pending() = 0;

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
        virtual Guard* getInputGuard() = 0;
        virtual Guard* getOutputGuard() = 0;
    };

} // namespace csp::internal

#endif // CSP4CMSIS_CHANNEL_BASE_H