#ifndef CSP4CMSIS_ALT_H
#define CSP4CMSIS_ALT_H

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "event_groups.h"
#include <stddef.h> 
#include <initializer_list>
#include "time.h" 

namespace csp {
    // Forward declarations
    template <typename T> class Chanin;
    template <typename T> class Chanout;

    namespace internal {
        class AltScheduler; 

        /**
         * @brief Base Guard Interface.
         */
        class Guard {
        public:
            /**
             * @return true if the guard is ALREADY ready (immediate rendezvous).
             */
            virtual bool enable(AltScheduler* alt, EventBits_t bit) = 0;
            virtual bool disable() = 0;
            virtual void activate() = 0;
            virtual ~Guard() = default;
        };

        class AltScheduler {
        private:
            TaskHandle_t waiting_task_handle = nullptr;
            EventGroupHandle_t event_group = nullptr;
            // API 1.3: backing storage for xEventGroupCreateStatic() --
            // no heap allocation for the event group, matching task
            // creation's move to xTaskCreateStatic(). Unlike TaskCtx (see
            // process.h), this buffer never needs to outlive `this`: it's
            // only ever touched by this object's own methods
            // (select()/wakeUp()), never handed to another independently-
            // scheduled task, so there's no separate lifetime to design
            // around -- it just needs normal member lifetime.
            StaticEventGroup_t event_group_buffer;
        public:
            AltScheduler();
            ~AltScheduler(); 
            void initForCurrentTask(); 
            
            /**
             * @brief The core ALT selection logic.
             * @param offset Used for Fair Alts to prevent starvation.
             * @return The index of the selected guard.
             */
            unsigned int select(Guard** guardArray, size_t amount, size_t offset = 0);
            
            void wakeUp(EventBits_t bit); 
            EventGroupHandle_t getEventGroupHandle() const { return event_group; }
        };

        class TimerGuard : public Guard {
        private:
            AltScheduler* parent_alt;
            TickType_t delay_ticks;
            TimerHandle_t timer_handle;
            EventBits_t assigned_bit; 
            static void TimerCallback(TimerHandle_t xTimer);
        public:
            TimerGuard(csp::Time delay);
            ~TimerGuard() override;
            bool enable(AltScheduler* alt, EventBits_t bit) override;
            bool disable() override; 
            void activate() override; 
        };

        /**
         * @brief A Skip Guard (Always ready).
         * Used internally when a Channel Policy is non-blocking.
         */
        class SkipGuard : public Guard {
        public:
            bool enable(AltScheduler*, EventBits_t) override { return true; }
            bool disable() override { return true; }
            void activate() override {} 
        };

    } // namespace internal

    /**
     * @brief Glue logic for Pipe Syntax (chan | msg).
     */
    template <typename T, typename ChanType>
    struct ChannelBinding {
        ChanType& channel;
        T& data_ref;

        ChannelBinding(ChanType& c, T& d) : channel(c), data_ref(d) {}

        internal::Guard* getInternalGuard() const {
            return channel.getGuard(data_ref); 
        }
    };

    /**
     * @brief Public Wrapper for Guards.
     */
    class Guard {
    public:
        internal::Guard* internal_guard_ptr = nullptr;
        virtual ~Guard() = default; 
    protected:
        Guard(internal::Guard* internal_ptr) : internal_guard_ptr(internal_ptr) {}
    };

    class RelTimeoutGuard : public Guard {
    private:
        internal::TimerGuard timer_storage;
    public:
        RelTimeoutGuard(csp::Time delay) 
            : Guard(&timer_storage), timer_storage(delay) {}
        ~RelTimeoutGuard() override = default;
    };

    /**
     * @brief The Alternative (ALT) construct.
     * Manages multiple guards and selects the first one available.
     */
    class Alternative {
    private:
        static const size_t MAX_GUARDS = 16;
        internal::Guard* internal_guards[MAX_GUARDS];
        size_t num_guards = 0;
        internal::AltScheduler internal_alt; 
        size_t fair_select_start_index = 0; 
        
    public:
        Alternative() : num_guards(0) {}
        
        template <typename... Bindings>
        Alternative(Bindings... bindings) : num_guards(0) {
            (addBinding(bindings), ...);
        }

        Alternative(std::initializer_list<internal::Guard*> guard_list);
        Alternative(std::initializer_list<csp::Guard*> guard_list);

        /**
         * @brief Priority Select: Always checks guards in the order they were added.
         */
        int priSelect();  

        /**
         * @brief Fair Select: Rotates the starting index to ensure all guards get a turn.
         */
        int fairSelect(); 

        // --- Binding Helpers ---

        template <typename T>
        void addBinding(const ChannelBinding<T, Chanin<T>>& b) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = b.getInternalGuard(); 
            }
        }

        template <typename T>
        void addBinding(const ChannelBinding<const T, Chanout<T>>& b) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = b.getInternalGuard();
            }
        }

        void addBinding(RelTimeoutGuard& tg) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = tg.internal_guard_ptr;
            }
        }
        
        void addBinding(internal::Guard* g) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = g;
            }
        }
    };
} 

#endif // CSP4CMSIS_ALT_H