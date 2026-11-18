#include "hal/virtio_net.hpp"

#include "net/bytes.hpp"

namespace hal
{

namespace
{
constexpr uint32_t DEVICE_ID_NET = 1;

// The rings are shared with the device, which reads and writes them behind
// the compiler's back: access them only through volatile references.
template <typename T>
volatile T& shared(T& object)
{
    return object;
}
} // namespace

const char* to_string(virtio_net::status s)
{
    switch(s)
    {
    case virtio_net::status::not_initialized:
        return "not initialized";
    case virtio_net::status::ready:
        return "ready";
    case virtio_net::status::no_device:
        return "no virtio-net device";
    case virtio_net::status::legacy_device:
        return "legacy virtio-mmio device (use -global "
               "virtio-mmio.force-legacy=false)";
    case virtio_net::status::unsupported_features:
        return "device lacks required features";
    case virtio_net::status::queue_unavailable:
        return "queue setup failed";
    }
    return "?";
}

virtio_net::status virtio_net::init()
{
    _transport = virtio_mmio::find(DEVICE_ID_NET);
    if(!_transport.present())
        return _state = status::no_device;
    if(_transport.version() != 2)
        return _state = status::legacy_device;

    // Initialization sequence, virtio 1.x section 3.1.1.
    _transport.reset();
    _transport.add_status(virtio_mmio::STATUS_ACKNOWLEDGE);
    _transport.add_status(virtio_mmio::STATUS_DRIVER);

    const uint64_t accepted =
        _transport.negotiate_features(virtio_mmio::F_VERSION_1 | F_MAC);
    if(!(accepted & virtio_mmio::F_VERSION_1))
    {
        _transport.add_status(virtio_mmio::STATUS_FAILED);
        return _state = status::unsupported_features;
    }
    _transport.add_status(virtio_mmio::STATUS_FEATURES_OK);
    if(!(_transport.status() & virtio_mmio::STATUS_FEATURES_OK))
    {
        _transport.add_status(virtio_mmio::STATUS_FAILED);
        return _state = status::unsupported_features;
    }

    // Hand every receive buffer to the device up front.
    volatile auto& rx = shared(_memory.rx);
    for(uint16_t i = 0; i < QUEUE_SIZE; ++i)
    {
        rx.desc[i].addr = reinterpret_cast<uintptr_t>(_memory.rx_buffers[i]);
        rx.desc[i].len = BUFFER_SIZE;
        rx.desc[i].flags = VIRTQ_DESC_F_WRITE;
        rx.avail.ring[i] = i;
    }
    rx.avail.idx = QUEUE_SIZE;

    if(!_transport.setup_queue(RX_QUEUE, _memory.rx) ||
       !_transport.setup_queue(TX_QUEUE, _memory.tx))
    {
        _transport.add_status(virtio_mmio::STATUS_FAILED);
        return _state = status::queue_unavailable;
    }

    if(accepted & F_MAC)
        for(uint32_t i = 0; i < 6; ++i)
            _mac.bytes[i] = _transport.config_byte(i);

    memory_barrier();
    _transport.add_status(virtio_mmio::STATUS_DRIVER_OK);
    _transport.notify(RX_QUEUE);
    return _state = status::ready;
}

void virtio_net::reclaim_tx()
{
    memory_barrier();
    _tx_used_seen = shared(_memory.tx).used.idx;
}

bool virtio_net::send(util::span<const uint8_t> frame)
{
    if(!ready() || frame.size() > MAX_FRAME)
        return false;

    // Wait (briefly) for a free transmit slot.
    for(int attempts = 0;
        static_cast<uint16_t>(_tx_avail_next - _tx_used_seen) >= QUEUE_SIZE;
        ++attempts)
    {
        if(attempts > 1'000'000)
            return false;
        reclaim_tx();
    }

    const uint16_t slot = _tx_avail_next % QUEUE_SIZE;
    uint8_t* buffer = _memory.tx_buffers[slot];
    net::fill_bytes(buffer, 0, HEADER_SIZE);
    net::copy_bytes(buffer + HEADER_SIZE, frame.data(), frame.size());

    volatile auto& tx = shared(_memory.tx);
    tx.desc[slot].addr = reinterpret_cast<uintptr_t>(buffer);
    tx.desc[slot].len = static_cast<uint32_t>(HEADER_SIZE + frame.size());
    tx.desc[slot].flags = 0;
    tx.avail.ring[slot] = slot;
    memory_barrier(); // descriptor before index
    tx.avail.idx = ++_tx_avail_next;
    memory_barrier(); // index before notify
    _transport.notify(TX_QUEUE);
    ++_frames_sent;
    return true;
}

size_t virtio_net::receive(util::span<uint8_t> frame)
{
    if(!ready())
        return 0;

    volatile auto& rx = shared(_memory.rx);
    if(rx.used.idx == _rx_used_seen)
        return 0;
    memory_barrier(); // read the index before the element

    const uint16_t position = _rx_used_seen % QUEUE_SIZE;
    const auto id = static_cast<uint16_t>(rx.used.ring[position].id);
    const uint32_t length = rx.used.ring[position].len;
    ++_rx_used_seen;

    size_t frame_size = length > HEADER_SIZE ? length - HEADER_SIZE : 0;
    if(frame_size > frame.size())
        frame_size = frame.size();
    net::copy_bytes(
        frame.data(),
        _memory.rx_buffers[id] + HEADER_SIZE,
        frame_size);

    // Give the buffer back to the device.
    const uint16_t avail = rx.avail.idx;
    rx.avail.ring[avail % QUEUE_SIZE] = id;
    memory_barrier();
    rx.avail.idx = static_cast<uint16_t>(avail + 1);
    memory_barrier();
    _transport.notify(RX_QUEUE);

    ++_frames_received;
    return frame_size;
}

} // namespace hal
