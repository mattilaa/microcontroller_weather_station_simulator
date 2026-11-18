#pragma once

#include <stdint.h>

#include "hal/mmio.hpp"

namespace hal
{

// ARM PrimeCell PL011 UART, polled (no interrupts).
class Pl011
{
public:
    explicit constexpr Pl011(uintptr_t base) : base_(base) {}

    void init() const;
    void put(char c) const;
    bool try_get(char& c) const; // non-blocking

private:
    constexpr Reg32 reg(uintptr_t offset) const
    {
        return Reg32{base_ + offset};
    }

    uintptr_t base_;
};

// QEMU virt: UART0 lives at 0x09000000.
inline constexpr Pl011 uart0{0x0900'0000};

} // namespace hal
