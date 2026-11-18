#pragma once

#include <stdint.h>

namespace hal
{

// Typed access to a memory-mapped 32-bit register.
class reg32
{
public:
    explicit constexpr reg32(uintptr_t addr) : _addr(addr) {}

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
        return reinterpret_cast<volatile uint32_t*>(_addr);
    }

    uintptr_t _addr;
};

// A bit field inside a 32-bit register: Width bits starting at bit Shift.
// The layout is checked at compile time, and so are constant values written
// with value<V>(), so typos in register definitions don't build.
template <unsigned Shift, unsigned Width>
struct bit_field
{
    static_assert(Width > 0, "field must be at least one bit wide");
    static_assert(Shift + Width <= 32, "field exceeds 32 bits");

    static constexpr uint32_t MASK =
        static_cast<uint32_t>((uint64_t{1} << Width) - 1) << Shift;

    // Field value as it appears in the register (checked at compile time).
    template <uint32_t V>
    static constexpr uint32_t value()
    {
        static_assert(V <= (MASK >> Shift), "value does not fit in field");
        return V << Shift;
    }

    // Extract the field from a register value.
    static constexpr uint32_t get(uint32_t reg)
    {
        return (reg & MASK) >> Shift;
    }

    // Return reg with the field replaced by v (extra bits of v are dropped).
    static constexpr uint32_t set(uint32_t reg, uint32_t v)
    {
        return (reg & ~MASK) | ((v << Shift) & MASK);
    }
};

// A single-bit flag.
template <unsigned Bit>
using flag = bit_field<Bit, 1>;

} // namespace hal
