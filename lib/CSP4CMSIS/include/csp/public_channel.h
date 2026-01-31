#ifndef CSP4CMSIS_PUBLIC_CHANNEL_H
#define CSP4CMSIS_PUBLIC_CHANNEL_H

#include "rendezvous_channel.h"
#include "buffered_channel.h"
#include "overwriting_channel.h"

namespace csp {

// Forward declarations
template <typename T> class Chanin;
template <typename T> class Chanout;

/**
 * @brief Pipe Operators for Alternative Syntax.
 * These create a ChannelBinding (defined in alt_channel_sync.h)
 * using the unified getGuard() interface.
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
    
    // Blocking write
    void operator<<(const T& data) { internal_ptr->output(&data); }
    void write(const T& data) { internal_ptr->output(&data); }
    
    /**
     * @brief Unified Guard accessor for ChannelBinding.
     */
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
    
    // Blocking read
    void operator>>(T& dest) { internal_ptr->input(&dest); }
    void read(T& dest) { internal_ptr->input(&dest); }
    
    /**
     * @brief Unified Guard accessor for ChannelBinding.
     */
    internal::Guard* getGuard(T& dest) { 
        return internal_ptr->getInputGuard(dest); 
    }
};

// =============================================================
// Static Channel Containers
// =============================================================

/**
 * @brief Zero-capacity Rendezvous Channel.
 */
template <typename T>
class One2OneChannel {
private:
    internal::RendezvousChannel<T> internal_chan;
public:
    One2OneChannel() = default;
    
    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
};

template <typename T>
using Channel = One2OneChannel<T>;

/**
 * @brief Buffered Channel with Static Capacity.
 */
template <typename T, size_t SIZE>
class BufferedOne2OneChannel {
private:
    internal::BufferedChannel<T> internal_chan;
public:
    BufferedOne2OneChannel() : internal_chan(SIZE) {}
    
    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
};

// --- Standard CSP Aliases ---
template <typename T> 
using Any2OneChannel = One2OneChannel<T>;

template <typename T, size_t S> 
using BufferedAny2OneChannel = BufferedOne2OneChannel<T, S>;

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_CHANNEL_H