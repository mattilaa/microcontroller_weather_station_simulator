#pragma once

#include <stdint.h>

#include "hal/pl011.hpp"

namespace app
{

// Formatting helpers: console << dec(ms, 6) << hex(addr, 8, '0');
struct Dec
{
    uint64_t value;
    uint8_t width;
    char pad;
};

struct Hex
{
    uint64_t value;
    uint8_t width;
    char pad;
};

constexpr Dec dec(uint64_t v, uint8_t width = 0, char pad = ' ')
{
    return {v, width, pad};
}

constexpr Hex hex(uint64_t v, uint8_t width = 0, char pad = '0')
{
    return {v, width, pad};
}

// Type-safe text output stream on top of a UART (replaces printf).
class Console
{
public:
    explicit constexpr Console(const hal::Pl011& uart) : uart_(uart) {}

    Console& operator<<(char c);
    Console& operator<<(const char* s);
    Console& operator<<(bool b);

    Console& operator<<(int v)
    {
        return *this << static_cast<long>(v);
    }

    Console& operator<<(unsigned v)
    {
        return *this << static_cast<unsigned long>(v);
    }

    Console& operator<<(long v);
    Console& operator<<(unsigned long v);
    Console& operator<<(Dec d);
    Console& operator<<(Hex h);

private:
    void put_uint(uint64_t v, unsigned base, uint8_t width, char pad);

    const hal::Pl011& uart_;
};

} // namespace app
