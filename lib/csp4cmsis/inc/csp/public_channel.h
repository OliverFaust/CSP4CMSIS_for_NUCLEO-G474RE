#ifndef CSP4CMSIS_PUBLIC_CHANNEL_H
#define CSP4CMSIS_PUBLIC_CHANNEL_H

#include "rendezvous_channel.h"
#include "buffered_channel.h"
#include "sync_channel.h"

namespace csp {

// Forward declarations
template <typename T> class Chanin;
template <typename T> class Chanout;

/**
 * @brief Pipe Operators for Alternative Syntax.
 */
template <typename T>
ChannelBinding<T, Chanin<T>> operator|(Chanin<T>& chan, T& dest) {
    return ChannelBinding<T, Chanin<T>>(chan, dest);
}

template <typename T>
ChannelBinding<const T, Chanout<T>> operator|(Chanout<T>& chan, const T& source) {
    return ChannelBinding<const T, Chanout<T>>(chan, source);
}

// =============================================================
// Channel End Wrappers (Chanout / Chanin)
// =============================================================

template <typename T>
class Chanout {
private:
    internal::BaseAltChan<T>* internal_ptr;
public:
    Chanout(internal::BaseAltChan<T>* ptr) : internal_ptr(ptr) {}
    
    void operator<<(const T& data) { internal_ptr->output(&data); }
    void write(const T& data) { internal_ptr->output(&data); }
    
    bool putFromISR(const T& data) { 
        return internal_ptr->putFromISR(data); 
    }
    
    internal::Guard* getGuard(const T& source) { 
        return internal_ptr->getOutputGuard(source); 
    }
};

template <typename T>
class Chanin {
private:
    internal::BaseAltChan<T>* internal_ptr;
public:
    Chanin(internal::BaseAltChan<T>* ptr) : internal_ptr(ptr) {}
    
    void operator>>(T& dest) { internal_ptr->input(&dest); }
    void read(T& dest) { internal_ptr->input(&dest); }
    
    internal::Guard* getGuard(T& dest) { 
        return internal_ptr->getInputGuard(dest); 
    }
};

// =============================================================
// Static Channel Containers (v1.1 Sampling API)
// =============================================================

/**
 * @brief Zero-capacity Synchronization Primitive.
 * In KeepNewest/Oldest modes, it behaves as a pure sampling port.
 */
template <typename T, BufferPolicy P = BufferPolicy::Block>
class SamplingChannel {
private:
    internal::RendezvousChannel<T, P> internal_chan;
public:
    SamplingChannel() = default;
    
    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
};

/**
 * @brief Buffered Asynchronous Primitive.
 * Decouples timing. Supports Lossy policies (KeepNewest/Oldest).
 */
template <typename T, size_t SIZE, BufferPolicy P = BufferPolicy::Block>
class SamplingBufferedChannel {
private:
    internal::BufferedChannel<T, P> internal_chan;
public:
    SamplingBufferedChannel() : internal_chan(SIZE) {}
    
    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
};

/**
 * @brief Synchronous Signal Channel (void data).
 */
template <BufferPolicy P = BufferPolicy::Block>
class SignalChannel {
private:
    internal::SyncChannel<P> internal_chan;
public:
    SignalChannel() = default;
    internal::SyncChannel<P>* getInternal() { return &internal_chan; }
};

// =============================================================
// Public Aliases & Legacy Support
// =============================================================

/**
 * @brief Standard CSP rendezvous channel (Blocking).
 */
template <typename T>
using Channel = SamplingChannel<T, BufferPolicy::Block>;

/**
 * @brief Standard CSP buffered channel (Blocking).
 */
template <typename T, size_t SIZE>
using BufferedChannel = SamplingBufferedChannel<T, SIZE, BufferPolicy::Block>;

/**
 * @brief Semantic alias for shared input ports.
 */
template <typename T, BufferPolicy P = BufferPolicy::Block> 
using Any2OneChannel = SamplingChannel<T, P>;

template <typename T, size_t S, BufferPolicy P = BufferPolicy::Block> 
using BufferedAny2OneChannel = SamplingBufferedChannel<T, S, P>;

/**
 * @brief Legacy API 1.0 Compatibility Aliases.
 */
template <typename T, BufferPolicy P = BufferPolicy::Block>
using One2OneChannel = SamplingChannel<T, P>;

template <typename T, size_t S, BufferPolicy P = BufferPolicy::Block>
using BufferedOne2OneChannel = SamplingBufferedChannel<T, S, P>;

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_CHANNEL_H