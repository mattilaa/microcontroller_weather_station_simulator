#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/addresses.hpp"
#include "util/span.hpp"

namespace net
{

// A network interface that sends and receives raw Ethernet frames
// (without the FCS). Implemented by hal::virtio_net.
class nic
{
public:
    nic(const nic&) = delete;
    nic& operator=(const nic&) = delete;

    virtual const mac_address& mac() const = 0;

    // Queue a frame for transmission; false if it couldn't be sent.
    virtual bool send(util::span<const uint8_t> frame) = 0;

    // Copy the next received frame into `frame`; returns its length, or 0 if
    // nothing has arrived. Frames longer than the buffer are truncated.
    virtual size_t receive(util::span<uint8_t> frame) = 0;

protected:
    constexpr nic() = default;
    ~nic() = default;
};

} // namespace net
