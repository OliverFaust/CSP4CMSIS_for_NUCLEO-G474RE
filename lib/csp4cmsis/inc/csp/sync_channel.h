#ifndef CSP4CMSIS_SYNC_CHANNEL_H
#define CSP4CMSIS_SYNC_CHANNEL_H

#include "channel_base.h"
#include "alt.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

namespace csp::internal {

    // Forward declaration with template param
    template <csp::BufferPolicy P> class SyncChannel;

    /**
     * @brief Guard for receiving from a synchronous channel.
     */
    template <csp::BufferPolicy P>
    class SyncChannelInputGuard : public Guard {
    private:
        SyncChannel<P>* channel = nullptr;
        void* user_data_dest = nullptr;
        size_t data_size = 0;
    public:
        SyncChannelInputGuard(SyncChannel<P>* chan) : channel(chan) {}
        void bind(void* dest, size_t size) { user_data_dest = dest; data_size = size; }
        
        bool enable(AltScheduler* alt, EventBits_t bit) override; 
        bool disable() override;
        void activate() override;
    };

    /**
     * @brief Guard for sending to a synchronous channel.
     */
    template <csp::BufferPolicy P>
    class SyncChannelOutputGuard : public Guard {
    private:
        SyncChannel<P>* channel = nullptr;
        const void* user_data_source = nullptr;
        size_t data_size = 0;
    public:
        SyncChannelOutputGuard(SyncChannel<P>* chan) : channel(chan) {}
        void bind(const void* src, size_t size) { user_data_source = src; data_size = size; }
        
        bool enable(AltScheduler* alt, EventBits_t bit) override;
        bool disable() override;
        void activate() override;
    };

    /**
     * @brief Synchronous Signal Channel (void data).
     * Template policy P allows for Blocking (Standard CSP) or Sampling (KeepNewest/Oldest).
     */
    template <csp::BufferPolicy P>
    class SyncChannel : public internal::BaseAltChan<void> {
    public:
        enum State { IDLE, SENDER_WAITING, RECEIVER_WAITING };

    private:
        SemaphoreHandle_t mutex;
        State state;
        const void* data_ptr = nullptr;
        size_t data_len = 0;
        
        AltScheduler* waiting_alt_in = nullptr;
        EventBits_t waiting_alt_bit_in = 0;
        SyncChannelInputGuard<P>* waiting_guard_in = nullptr;

        AltScheduler* waiting_alt_out = nullptr;
        EventBits_t waiting_alt_bit_out = 0;
        SyncChannelOutputGuard<P>* waiting_guard_out = nullptr;
        
        QueueHandle_t sender_queue; 
        QueueHandle_t receiver_queue; 

        SyncChannelInputGuard<P>  res_in_guard;
        SyncChannelOutputGuard<P> res_out_guard;

    public:
        // Constructor and Destructor bodies removed (they are defined in sync_channel.cpp)
        SyncChannel();
        ~SyncChannel() override;

        // --- Core Logic ---
        void reset(); // Required by .cpp implementation
        
        bool pending() override;
        bool space_available() override;
        bool putFromISR() override;

        internal::Guard* getInputGuard() override { return &res_in_guard; }
        internal::Guard* getOutputGuard() override { return &res_out_guard; }
        
        void input(void* const dest) override;
        void output(const void* const source) override;

        void beginExtInput(void* const dest) override { input(dest); }
        void endExtInput() override {}

        // --- Alt Registration helpers ---
        bool registerAltIn(AltScheduler* alt, EventBits_t bit, SyncChannelInputGuard<P>* guard);
        bool unregisterAltIn(AltScheduler* alt);
        bool registerAltOut(AltScheduler* alt, EventBits_t bit, SyncChannelOutputGuard<P>* guard);
        bool unregisterAltOut(AltScheduler* alt);

        // --- Getters ---
        SemaphoreHandle_t getMutex() const { return mutex; }
        State getState() const { return state; }
        QueueHandle_t getSenderQueue() const { return sender_queue; }
        QueueHandle_t getReceiverQueue() const { return receiver_queue; }
        const void* getDataPtr() const { return data_ptr; } // Required by Guards
    };

} // namespace csp::internal

#endif // CSP4CMSIS_SYNC_CHANNEL_H