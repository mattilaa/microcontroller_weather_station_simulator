#include "app/console.hpp"

namespace app
{

Console& Console::operator<<(char c)
{
    if(c == '\n')
        uart_.put('\r');
    uart_.put(c);
    return *this;
}

Console& Console::operator<<(const char* s)
{
    if(!s)
        s = "(null)";
    while(*s)
        *this << *s++;
    return *this;
}

Console& Console::operator<<(bool b)
{
    return *this << (b ? "true" : "false");
}

Console& Console::operator<<(long v)
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

Console& Console::operator<<(unsigned long v)
{
    put_uint(v, 10, 0, ' ');
    return *this;
}

Console& Console::operator<<(Dec d)
{
    put_uint(d.value, 10, d.width, d.pad);
    return *this;
}

Console& Console::operator<<(Hex h)
{
    put_uint(h.value, 16, h.width, h.pad);
    return *this;
}

void Console::put_uint(uint64_t v, unsigned base, uint8_t width, char pad)
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
