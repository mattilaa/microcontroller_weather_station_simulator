#include "util/text_stream.hpp"

namespace util
{

text_stream& text_stream::operator<<(const char* s)
{
    if(!s)
        s = "(null)";
    while(*s)
        put(*s++);
    return *this;
}

text_stream& text_stream::operator<<(bool b)
{
    return *this << (b ? "true" : "false");
}

text_stream& text_stream::operator<<(long v)
{
    uint64_t magnitude = static_cast<uint64_t>(v);
    if(v < 0)
    {
        put('-');
        magnitude = 0 - magnitude;
    }
    put_uint(magnitude, 10, 0, ' ');
    return *this;
}

text_stream& text_stream::operator<<(unsigned long v)
{
    put_uint(v, 10, 0, ' ');
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
    uint64_t scale = 1;
    for(uint8_t i = 0; i < f.decimals; ++i)
        scale *= 10;
    uint64_t magnitude = static_cast<uint64_t>(f.value);
    if(f.value < 0)
    {
        put('-');
        magnitude = 0 - magnitude;
    }
    put_uint(magnitude / scale, 10, 0, ' ');
    if(f.decimals > 0)
    {
        put('.');
        put_uint(magnitude % scale, 10, f.decimals, '0');
    }
    return *this;
}

void text_stream::put_uint(uint64_t v, unsigned base, uint8_t width, char pad)
{
    char buf[24];
    unsigned i = 0;
    do
    {
        const auto digit = static_cast<unsigned>(v % base);
        buf[i++] =
            static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
        v /= base;
    } while(v != 0);
    while(i < width && i < sizeof(buf))
        buf[i++] = pad;
    while(i != 0)
        put(buf[--i]);
}

void buffer_stream::put(char c)
{
    if(_size < _capacity)
        _buffer[_size++] = c;
    else
        _overflowed = true;
}

} // namespace util
