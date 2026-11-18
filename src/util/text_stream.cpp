#include "util/text_stream.hpp"

namespace util
{

extern "C"
{
    void write_text__ptr_u8_str8(text_stream*, const char*);
    void write_int__ptr_u8_i64(text_stream*, int64_t);
    void write_fixed__ptr_u8_i64_u32(text_stream*, int64_t, uint32_t);
    void write_uint__ptr_u8_u64_u64_u32_u32(
        text_stream*,
        uint64_t,
        uint64_t,
        uint32_t,
        uint32_t);
}

text_stream& text_stream::operator<<(const char* s)
{
    write_text__ptr_u8_str8(this, s ? s : "(null)");
    return *this;
}

text_stream& text_stream::operator<<(bool b)
{
    return *this << (b ? "true" : "false");
}

text_stream& text_stream::operator<<(long value)
{
    write_int__ptr_u8_i64(this, value);
    return *this;
}

text_stream& text_stream::operator<<(unsigned long value)
{
    put_uint(value, 10, 0, ' ');
    return *this;
}

text_stream& text_stream::operator<<(dec_fmt d)
{
    put_uint(d.value, 10, d.width, d.pad);
    return *this;
}

text_stream& text_stream::operator<<(hex_fmt h)
{
    put_uint(h.value, 16, h.width, h.pad);
    return *this;
}

text_stream& text_stream::operator<<(fixed_fmt f)
{
    write_fixed__ptr_u8_i64_u32(this, f.value, f.decimals);
    return *this;
}

void text_stream::put_uint(
    uint64_t value,
    unsigned base,
    uint8_t width,
    char pad)
{
    write_uint__ptr_u8_u64_u64_u32_u32(
        this,
        value,
        base,
        width,
        static_cast<uint8_t>(pad));
}

void buffer_stream::put(char c)
{
    if(_size < _capacity)
        _buffer[_size++] = c;
    else
        _overflowed = true;
}

} // namespace util
