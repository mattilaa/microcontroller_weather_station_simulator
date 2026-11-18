#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal/virtio_mmio.hpp"
#include "net/nic.hpp"

namespace hal
{

// Polled virtio-net driver over virtio-mmio (no interrupts).
//
// QEMU: -global virtio-mmio.force-legacy=false
//       -device virtio-net-device,netdev=<id>
//
// Queue 0 receives, queue 1 transmits. Every receive descriptor owns one
// static buffer that is handed back to the device after the frame is copied
// out; transmit buffers are reused round-robin once the device is done.
class virtio_net final : public net::nic
{
public:
    // Sized for a 32 KiB RAM budget: every descriptor owns a full-frame
    // buffer (1526 bytes), so these six buffers are most of the driver's RAM.
    static constexpr uint16_t RX_QUEUE_SIZE = 4;
    static constexpr uint16_t TX_QUEUE_SIZE = 2;
    static constexpr size_t MAX_FRAME = 1514; // Ethernet header + 1500 MTU

    // Every virtio-net 1.x buffer starts with this 12-byte header
    // (struct virtio_net_hdr with num_buffers). We send it all zero.
    static constexpr size_t HEADER_SIZE = 12;
    static constexpr size_t BUFFER_SIZE = HEADER_SIZE + MAX_FRAME;

    // Memory shared with the device (rings and frame buffers). Kept out of
    // the driver object: the driver is polymorphic, so its vtable pointer
    // would put the whole object, buffers included, into .data. Define this
    // as a separate zero-initialized global so it lands in .bss.
    struct dma_memory
    {
        virtqueue<RX_QUEUE_SIZE> rx;
        virtqueue<TX_QUEUE_SIZE> tx;
        alignas(16) uint8_t rx_buffers[RX_QUEUE_SIZE][BUFFER_SIZE];
        alignas(16) uint8_t tx_buffers[TX_QUEUE_SIZE][BUFFER_SIZE];
    };

    explicit constexpr virtio_net(dma_memory& memory) : _memory(memory) {}

    enum class status
    {
        not_initialized,
        ready,
        no_device,
        legacy_device,
        unsupported_features,
        queue_unavailable,
    };

    // Find the device and bring it up.
    status init();

    status state() const
    {
        return _state;
    }

    bool ready() const
    {
        return _state == status::ready;
    }

    const net::mac_address& mac() const override
    {
        return _mac;
    }

    bool send(util::span<const uint8_t> frame) override;
    size_t receive(util::span<uint8_t> frame) override;

    uint32_t frames_sent() const
    {
        return _frames_sent;
    }

    uint32_t frames_received() const
    {
        return _frames_received;
    }

private:
    static constexpr uint16_t RX_QUEUE = 0;
    static constexpr uint16_t TX_QUEUE = 1;
    static constexpr uint64_t F_MAC = uint64_t{1} << 5;

    void reclaim_tx();

    virtio_mmio _transport;
    status _state = status::not_initialized;
    net::mac_address _mac{};

    dma_memory& _memory;

    uint16_t _rx_used_seen = 0;  // used.idx entries consumed so far
    uint16_t _tx_avail_next = 0; // our avail.idx shadow
    uint16_t _tx_used_seen = 0;

    uint32_t _frames_sent = 0;
    uint32_t _frames_received = 0;
};

const char* to_string(virtio_net::status s);

} // namespace hal
