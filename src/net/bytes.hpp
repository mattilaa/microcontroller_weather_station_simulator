#pragma once

#include <stddef.h>
#include <stdint.h>

#include "util/span.hpp"

// Byte-wise access to packet fields in network (big-endian) byte order.
// Packet fields are often unaligned, and with the MMU off all memory is
// Device memory where unaligned accesses fault, so never cast a packet
// pointer to a wider type: always go through these helpers.
namespace net
{

constexpr uint16_t load_be16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

constexpr uint32_t load_be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

constexpr void store_be16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

constexpr void store_be32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

constexpr void copy_bytes(uint8_t* dst, const uint8_t* src, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        dst[i] = src[i];
}

constexpr void fill_bytes(uint8_t* dst, uint8_t value, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        dst[i] = value;
}

// RFC 1071 Internet checksum. Accumulate with checksum_add() (also for the
// UDP pseudo-header), then checksum_finish(). A received header/segment is
// valid when the checksum over it, including its checksum field, is 0.
constexpr uint32_t checksum_add(uint32_t sum, util::span<const uint8_t> data)
{
    size_t i = 0;
    for(; i + 1 < data.size(); i += 2)
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
    if(i < data.size())
        sum += static_cast<uint32_t>(data[i] << 8); // pad odd byte
    return sum;
}

constexpr uint16_t checksum_finish(uint32_t sum)
{
    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

constexpr uint16_t internet_checksum(util::span<const uint8_t> data)
{
    return checksum_finish(checksum_add(0, data));
}

} // namespace net
