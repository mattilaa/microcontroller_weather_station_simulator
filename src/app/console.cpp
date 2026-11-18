#include "app/console.hpp"

namespace app
{

console& console::operator<<(char c)
{
    if(c == '\n')
        _uart.put('\r');
    _uart.put(c);
    return *this;
}

console& console::operator<<(const char* s)
{
    if(!s)
        s = "(null)";
    while(*s)
        *this << *s++;
    return *this;
}

console& console::operator<<(bool b)
{
    return *this << (b ? "true" : "false");
}

console& console::operator<<(long v)
{
    uint64_t magnitude = static_cast<uint64_t>(v);
    if(v < 0)
    {
        *this << '-';
        magnitude = 0 - magnitude;
    }
    put_uint(magnitude, 10, 0, ' ');
    return *this;
}

console& console::operator<<(unsigned long v)
{
    put_uint(v, 10, 0, ' ');
    return *this;
}

console& console::operator<<(dec_fmt d)
{
    put_uint(d.value, 10, d.width, d.pad);
    return *this;
}

console& console::operator<<(hex_fmt h)
{
    put_uint(h.value, 16, h.width, h.pad);
    return *this;
}

void console::put_uint(uint64_t v, unsigned base, uint8_t width, char pad)
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
        *this << buf[--i];
}

} // namespace app
