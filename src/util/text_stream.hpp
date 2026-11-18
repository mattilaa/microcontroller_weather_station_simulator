#pragma once

#include <stddef.h>
#include <stdint.h>

namespace util
{

// Formatting helpers, e.g. out << dec(42, 6) << ' ' << fixed(1234, 2)
// prints "    42 12.34".
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

// A fixed-point number: value / 10^decimals, e.g. fixed(-1234, 2) is "-12.34".
struct fixed_fmt
{
    int64_t value;
    uint8_t decimals;
};

constexpr dec_fmt dec(uint64_t v, uint8_t width = 0, char pad = ' ')
{
    return {v, width, pad};
}

constexpr hex_fmt hex(uint64_t v, uint8_t width = 0, char pad = '0')
{
    return {v, width, pad};
}

constexpr fixed_fmt fixed(int64_t v, uint8_t decimals)
{
    return {v, decimals};
}

// Type-safe text output (replaces printf). Derived classes decide where the
// characters go: the UART console, or a buffer for a network reply.
class text_stream
{
public:
    text_stream(const text_stream&) = delete;
    text_stream& operator=(const text_stream&) = delete;

    text_stream& operator<<(char c)
    {
        put(c);
        return *this;
    }

    text_stream& operator<<(const char* s);
    text_stream& operator<<(bool b);

    text_stream& operator<<(int v)
    {
        return *this << static_cast<long>(v);
    }

    text_stream& operator<<(unsigned v)
    {
        return *this << static_cast<unsigned long>(v);
    }

    text_stream& operator<<(long v);
    text_stream& operator<<(unsigned long v);
    text_stream& operator<<(dec_fmt d);
    text_stream& operator<<(hex_fmt h);
    text_stream& operator<<(fixed_fmt f);

protected:
    constexpr text_stream() = default;
    ~text_stream() = default;

    virtual void put(char c) = 0;

private:
    void put_uint(uint64_t v, unsigned base, uint8_t width, char pad);
};

// Writes into a caller-provided char buffer; output beyond the capacity is
// dropped and reported by overflowed().
class buffer_stream final : public text_stream
{
public:
    constexpr buffer_stream(char* buffer, size_t capacity)
        : _buffer(buffer), _capacity(capacity)
    {
    }

    const char* data() const
    {
        return _buffer;
    }

    size_t size() const
    {
        return _size;
    }

    bool overflowed() const
    {
        return _overflowed;
    }

private:
    void put(char c) override;

    char* _buffer;
    size_t _capacity;
    size_t _size = 0;
    bool _overflowed = false;
};

} // namespace util
