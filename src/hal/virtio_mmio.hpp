#pragma once

#include <stdint.h>

namespace hal
{

// Split virtqueue memory shared with the device (virtio 1.x, section 2.7).
// Layout and alignment follow the spec; fields keep the spec's names.
template <uint16_t N>
struct virtqueue
{
    static_assert(
        N > 0 && (N & (N - 1)) == 0,
        "queue size must be a power of two");

    struct descriptor
    {
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        uint16_t next;
    };

    struct used_element
    {
        uint32_t id;
        uint32_t len;
    };

    alignas(16) descriptor desc[N];

    alignas(2) struct
    {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[N];
        uint16_t used_event;
    } avail;

    alignas(4) struct
    {
        uint16_t flags;
        uint16_t idx;
        used_element ring[N];
        uint16_t avail_event;
    } used;
};

inline constexpr uint16_t VIRTQ_DESC_F_WRITE = 2; // device writes the buffer

// Full system barrier: orders our ring updates against the device.
inline void memory_barrier()
{
    asm volatile("dmb sy" ::: "memory");
}

// One virtio-mmio transport (virtio 1.x, section 4.2). QEMU virt has 32 of
// them at 0x0a000000, 0x200 apart. Only the modern interface (version 2) is
// supported: run QEMU with -global virtio-mmio.force-legacy=false.
class virtio_mmio
{
public:
    static constexpr uintptr_t FIRST_BASE = 0x0a00'0000;
    static constexpr uintptr_t STRIDE = 0x200;
    static constexpr unsigned SLOTS = 32;
    static constexpr uint32_t MAGIC = 0x7472'6976; // "virt"

    // Status bits.
    static constexpr uint32_t STATUS_ACKNOWLEDGE = 1;
    static constexpr uint32_t STATUS_DRIVER = 2;
    static constexpr uint32_t STATUS_DRIVER_OK = 4;
    static constexpr uint32_t STATUS_FEATURES_OK = 8;
    static constexpr uint32_t STATUS_FAILED = 128;

    static constexpr uint64_t F_VERSION_1 = uint64_t{1} << 32;

    constexpr virtio_mmio() = default;

    explicit constexpr virtio_mmio(uintptr_t base) : _base(base) {}

    // Find the first transport with a device of `device_id` (1 = network).
    // Returns a transport with base 0 if there is none.
    static virtio_mmio find(uint32_t device_id);

    bool present() const
    {
        return _base != 0;
    }

    uint32_t version() const
    {
        return read(VERSION);
    }

    void reset() const
    {
        write(STATUS, 0);
    }

    void add_status(uint32_t bits) const
    {
        write(STATUS, read(STATUS) | bits);
    }

    uint32_t status() const
    {
        return read(STATUS);
    }

    // Accept the subset of `wanted` that the device offers; returns it.
    uint64_t negotiate_features(uint64_t wanted) const;

    // Configure queue `index` to use `queue`. False if the device doesn't
    // have the queue or it is too small.
    template <uint16_t N>
    bool setup_queue(uint16_t index, virtqueue<N>& queue) const
    {
        write(QUEUE_SEL, index);
        if(read(QUEUE_READY) != 0 || read(QUEUE_NUM_MAX) < N)
            return false;
        write(QUEUE_NUM, N);
        set_address(QUEUE_DESC_LOW, &queue.desc);
        set_address(QUEUE_DRIVER_LOW, &queue.avail);
        set_address(QUEUE_DEVICE_LOW, &queue.used);
        write(QUEUE_READY, 1);
        return true;
    }

    void notify(uint16_t queue_index) const
    {
        write(QUEUE_NOTIFY, queue_index);
    }

    uint8_t config_byte(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint8_t*>(_base + CONFIG + offset);
    }

private:
    static constexpr uint32_t MAGIC_VALUE = 0x000;
    static constexpr uint32_t VERSION = 0x004;
    static constexpr uint32_t DEVICE_ID = 0x008;
    static constexpr uint32_t DEVICE_FEATURES = 0x010;
    static constexpr uint32_t DEVICE_FEATURES_SEL = 0x014;
    static constexpr uint32_t DRIVER_FEATURES = 0x020;
    static constexpr uint32_t DRIVER_FEATURES_SEL = 0x024;
    static constexpr uint32_t QUEUE_SEL = 0x030;
    static constexpr uint32_t QUEUE_NUM_MAX = 0x034;
    static constexpr uint32_t QUEUE_NUM = 0x038;
    static constexpr uint32_t QUEUE_READY = 0x044;
    static constexpr uint32_t QUEUE_NOTIFY = 0x050;
    static constexpr uint32_t STATUS = 0x070;
    static constexpr uint32_t QUEUE_DESC_LOW = 0x080;
    static constexpr uint32_t QUEUE_DRIVER_LOW = 0x090;
    static constexpr uint32_t QUEUE_DEVICE_LOW = 0x0a0;
    static constexpr uint32_t CONFIG = 0x100;

    uint32_t read(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint32_t*>(_base + offset);
    }

    void write(uint32_t offset, uint32_t value) const
    {
        *reinterpret_cast<volatile uint32_t*>(_base + offset) = value;
    }

    // Write a 64-bit physical address to a LOW/HIGH register pair. The MMU
    // is off, so physical addresses equal our pointers.
    void set_address(uint32_t low_offset, const volatile void* p) const
    {
        const auto addr = reinterpret_cast<uintptr_t>(p);
        write(low_offset, static_cast<uint32_t>(addr));
        write(low_offset + 4, static_cast<uint32_t>(addr >> 32));
    }

    uintptr_t _base = 0;
};

} // namespace hal
