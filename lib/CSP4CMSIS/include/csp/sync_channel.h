#ifndef CSP4CMSIS_SYNC_CHANNEL_H
#define CSP4CMSIS_SYNC_CHANNEL_H

#include "channel_base.h"
#include "alt.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

namespace csp::internal {

    class SyncChannel;

    class SyncChannelInputGuard : public Guard {
    private:
        SyncChannel* channel = nullptr;
        void* user_data_dest = nullptr;
        size_t data_size = 0;
        AltScheduler* parent_alt = nullptr; // Required for disable()
    public:
        SyncChannelInputGuard(SyncChannel* chan) : channel(chan) {}
        void bind(void* dest, size_t size) { user_data_dest = dest; data_size = size; }
        bool enable(AltScheduler* alt, EventBits_t bit) override; 
        bool disable() override;
        void activate() override;
    };

    class SyncChannelOutputGuard : public Guard {
    private:
        SyncChannel* channel = nullptr;
        const void* user_data_source = nullptr;
        size_t data_size = 0;
        AltScheduler* parent_alt = nullptr; // Required for disable()
    public:
        SyncChannelOutputGuard(SyncChannel* chan) : channel(chan) {}
        void bind(const void* src, size_t size) { user_data_source = src; data_size = size; }
        bool enable(AltScheduler* alt, EventBits_t bit) override;
        bool disable() override;
        void activate() override;
    };

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
        SyncChannelInputGuard* waiting_guard_in = nullptr; // ADDED

        AltScheduler* waiting_alt_out = nullptr;
        EventBits_t waiting_alt_bit_out = 0;
        SyncChannelOutputGuard* waiting_guard_out = nullptr; // ADDED
        
        QueueHandle_t sender_queue; 
        QueueHandle_t receiver_queue; 

        SyncChannelInputGuard  res_in_guard;
        SyncChannelOutputGuard res_out_guard;

    public:
        SyncChannel();
        ~SyncChannel() override;
        void reset();

        Guard* getInputGuard() override { return &res_in_guard; }
        Guard* getOutputGuard() override { return &res_out_guard; }
        
        void input(void* const dest) override;
        void output(const void* const source) override;
        void beginExtInput(void* const dest) override { input(dest); }
        void endExtInput() override {}

        bool registerAltIn(AltScheduler* alt, EventBits_t bit, SyncChannelInputGuard* guard);
        bool unregisterAltIn(AltScheduler* alt);
        bool registerAltOut(AltScheduler* alt, EventBits_t bit, SyncChannelOutputGuard* guard);
        bool unregisterAltOut(AltScheduler* alt);

        SemaphoreHandle_t getMutex() { return mutex; }
        State getState() { return state; }
        const void* getDataPtr() { return data_ptr; }
        QueueHandle_t getSenderQueue() { return sender_queue; }
        QueueHandle_t getReceiverQueue() { return receiver_queue; }
        void setChannelData(const void* ptr, size_t len) { data_ptr = ptr; data_len = len; }
    };
}
#endif