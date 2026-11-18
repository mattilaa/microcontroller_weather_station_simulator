#include "hal/virtio_mmio.hpp"

namespace hal
{

virtio_mmio virtio_mmio::find(uint32_t device_id)
{
    for(unsigned slot = 0; slot < SLOTS; ++slot)
    {
        const virtio_mmio candidate{FIRST_BASE + slot * STRIDE};
        if(candidate.read(MAGIC_VALUE) == MAGIC &&
           candidate.read(DEVICE_ID) == device_id)
            return candidate;
    }
    return virtio_mmio{};
}

uint64_t virtio_mmio::negotiate_features(uint64_t wanted) const
{
    write(DEVICE_FEATURES_SEL, 0);
    uint64_t offered = read(DEVICE_FEATURES);
    write(DEVICE_FEATURES_SEL, 1);
    offered |= static_cast<uint64_t>(read(DEVICE_FEATURES)) << 32;

    const uint64_t accepted = offered & wanted;
    write(DRIVER_FEATURES_SEL, 0);
    write(DRIVER_FEATURES, static_cast<uint32_t>(accepted));
    write(DRIVER_FEATURES_SEL, 1);
    write(DRIVER_FEATURES, static_cast<uint32_t>(accepted >> 32));
    return accepted;
}

} // namespace hal
