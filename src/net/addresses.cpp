#include "net/addresses.hpp"

namespace net
{

util::text_stream& operator<<(util::text_stream& out, const mac_address& mac)
{
    for(int i = 0; i < 6; ++i)
    {
        if(i != 0)
            out << ':';
        out << util::hex(mac.bytes[i], 2);
    }
    return out;
}

util::text_stream& operator<<(util::text_stream& out, const ipv4_address& ip)
{
    return out << ip.bytes[0] << '.' << ip.bytes[1] << '.' << ip.bytes[2] << '.'
               << ip.bytes[3];
}

} // namespace net
