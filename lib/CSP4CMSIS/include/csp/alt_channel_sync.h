#ifndef ALT_CHANNEL_SYNC_H
#define ALT_CHANNEL_SYNC_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "alt.h"      
#include <cstdio> 

namespace csp::internal {

    class AltScheduler;

    /**
     * @brief Represents an Alternative (ALT) operation currently waiting on a channel.
     */
    struct WaitingAlt {
        AltScheduler* alt_ptr;
        EventBits_t assigned_bit;
        void* data_ptr;
        size_t data_size;

        WaitingAlt() : alt_ptr(nullptr), assigned_bit(0), data_ptr(nullptr), data_size(0) {}

        /**
         * @brief Atomically configure the ALT registration.
         */
        void set(AltScheduler* a, EventBits_t b, void* d, size_t s) {
            alt_ptr = a;
            assigned_bit = b;
            data_ptr = d;
            data_size = s;
        }

        /**
         * @brief Clear the registration.
         */
        void clear() {
            alt_ptr = nullptr;
            assigned_bit = 0;
            data_ptr = nullptr;
            data_size = 0;
        }
    };

    /**
     * @brief Base synchronization primitive for channels supporting ALT.
     */
    class AltChanSyncBase {
    protected:
        SemaphoreHandle_t mutex; 
        
        // Slots for processes currently blocked in an Alternative (ALT) select
        WaitingAlt waiting_in_alt;
        WaitingAlt waiting_out_alt;

        // Slots for standard blocking processes (input() / output())
        TaskHandle_t waiting_in_task;
        TaskHandle_t waiting_out_task;
        void* non_alt_in_data_ptr;
        const void* non_alt_out_data_ptr;

    public:
        AltChanSyncBase();
        virtual ~AltChanSyncBase();

        // Perform or verify a rendezvous
        bool tryHandshake(void* data_ptr, size_t size, bool is_writer);
        
        // Register a standard task for blocking I/O
        void registerWaitingTask(void* data_ptr, bool is_writer);
        
        void clearWaitingIn() { waiting_in_task = nullptr; non_alt_in_data_ptr = nullptr; }
        void clearWaitingOut() { waiting_out_task = nullptr; non_alt_out_data_ptr = nullptr; }

        // Getters for thread safety and logic
        SemaphoreHandle_t getMutex() { return mutex; }
        TaskHandle_t getWaitingInTask() const { return waiting_in_task; }
        TaskHandle_t getWaitingOutTask() const { return waiting_out_task; }
        void* getNonAltInDataPtr() const { return non_alt_in_data_ptr; }
        const void* getNonAltOutDataPtr() const { return non_alt_out_data_ptr; }
        
        AltScheduler* getAltInScheduler() const { return waiting_in_alt.alt_ptr; }
        EventBits_t   getAltInBit() const       { return waiting_in_alt.assigned_bit; }
        AltScheduler* getAltOutScheduler() const { return waiting_out_alt.alt_ptr; }
        EventBits_t   getAltOutBit() const       { return waiting_out_alt.assigned_bit; }

        WaitingAlt& getWaitingInAlt() { return waiting_in_alt; }
        WaitingAlt& getWaitingOutAlt() { return waiting_out_alt; }
    };

    // =============================================================
    // Guards: Interfaces between Channels and the AltScheduler
    // =============================================================

    class ChanInGuard : public Guard {
    private: 
        AltChanSyncBase* parent_channel;
        void* user_data_dest; 
        size_t data_size;
    public:
        ChanInGuard(AltChanSyncBase* parent, void* dest = nullptr, size_t size = 0) 
            : parent_channel(parent), user_data_dest(dest), data_size(size) {}
        
        bool enable(AltScheduler* alt, EventBits_t bit) override;
        bool disable() override;
        void activate() override;
        void updateBuffer(void* new_dest) { user_data_dest = new_dest; }
    };

    class ChanOutGuard : public Guard { 
    private: 
        AltChanSyncBase* parent_channel;
        const void* user_data_source; 
        size_t data_size;
    public:
        ChanOutGuard(AltChanSyncBase* parent, const void* src = nullptr, size_t size = 0) 
            : parent_channel(parent), user_data_source(src), data_size(size) {}
        
        bool enable(AltScheduler* alt, EventBits_t bit) override;
        bool disable() override;
        void activate() override;
        void updateBuffer(const void* new_src) { user_data_source = new_src; }
    };
}
#endif