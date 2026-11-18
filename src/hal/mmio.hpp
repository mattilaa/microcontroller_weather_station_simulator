#pragma once

#include <stdint.h>

namespace hal
{

// Typed access to a memory-mapped 32-bit register.
class Reg32
{
public:
    explicit constexpr Reg32(uintptr_t addr) : addr_(addr) {}

    uint32_t read() const
    {
        return *ptr();
    }

    void write(uint32_t v) const
    {
        *ptr() = v;
    }

    bool test(uint32_t mask) const
    {
        return (read() & mask) != 0;
    }

private:
    volatile uint32_t* ptr() const
    {
        return reinterpret_cast<volatile uint32_t*>(addr_);
    }

    uintptr_t addr_;
};

} // namespace hal
