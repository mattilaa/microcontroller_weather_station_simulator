#pragma once

#include <stdint.h>

#include "hal/pl011.hpp"

namespace app
{

// Formatting helpers: console << dec(ms, 6) << hex(addr, 8, '0');
struct dec_fmt
{
    uint64_t value;
    uint8_t width;
    char pad;
};

struct hex_fmt
{
    uint64_t value;
    uint8_t width;
    char pad;
};

constexpr dec_fmt dec(uint64_t v, uint8_t width = 0, char pad = ' ')
{
    return {v, width, pad};
}

constexpr hex_fmt hex(uint64_t v, uint8_t width = 0, char pad = '0')
{
    return {v, width, pad};
}

// Type-safe text output stream on top of a UART (replaces printf).
class console
{
public:
    explicit constexpr console(const hal::pl011& uart) : _uart(uart) {}

    console& operator<<(char c);
    console& operator<<(const char* s);
    console& operator<<(bool b);

    console& operator<<(int v)
    {
        return *this << static_cast<long>(v);
    }

    console& operator<<(unsigned v)
    {
        return *this << static_cast<unsigned long>(v);
    }

    console& operator<<(long v);
    console& operator<<(unsigned long v);
    console& operator<<(dec_fmt d);
    console& operator<<(hex_fmt h);

private:
    void put_uint(uint64_t v, unsigned base, uint8_t width, char pad);

    const hal::pl011& _uart;
};

} // namespace app
