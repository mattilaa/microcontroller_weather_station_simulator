#pragma once

#include <stdint.h>

#include "util/text_stream.hpp"

namespace net
{

struct mac_address
{
    uint8_t bytes[6];

    bool operator==(const mac_address&) const = default;
};

struct ipv4_address
{
    uint8_t bytes[4];

    bool operator==(const ipv4_address&) const = default;
};

inline constexpr mac_address BROADCAST_MAC{
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
inline constexpr ipv4_address BROADCAST_IPV4{{255, 255, 255, 255}};

struct ipv4_config
{
    ipv4_address address;
    ipv4_address netmask;
    ipv4_address gateway;
};

// "52:54:00:12:34:56"
util::text_stream& operator<<(util::text_stream& out, const mac_address& mac);
// "10.0.2.15"
util::text_stream& operator<<(util::text_stream& out, const ipv4_address& ip);

} // namespace net
