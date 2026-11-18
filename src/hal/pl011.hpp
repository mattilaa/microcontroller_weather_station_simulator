#pragma once

#include <stdint.h>

#include "hal/mmio.hpp"

namespace hal
{

// ARM PrimeCell PL011 UART, polled (no interrupts).
class pl011
{
public:
    explicit constexpr pl011(uintptr_t base) : _base(base) {}

    void init() const;
    void put(char c) const;
    bool try_get(char& c) const; // non-blocking

private:
    constexpr reg32 reg(uintptr_t offset) const
    {
        return reg32{_base + offset};
    }

    uintptr_t _base;
};

// QEMU virt: UART0 lives at 0x09000000.
inline constexpr pl011 uart0{0x0900'0000};

} // namespace hal
