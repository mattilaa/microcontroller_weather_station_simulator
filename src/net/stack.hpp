#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/addresses.hpp"
#include "net/nic.hpp"
#include "util/span.hpp"

namespace net
{

// Where a UDP datagram came from (replies go back there).
struct udp_endpoint
{
    mac_address mac;
    ipv4_address ip;
    uint16_t port;
};

// Application side of a UDP port.
class udp_handler
{
public:
    udp_handler(const udp_handler&) = delete;
    udp_handler& operator=(const udp_handler&) = delete;

    // Handle one request; write the reply payload into `reply` and return
    // its length (0 = send no reply).
    virtual size_t handle(
        util::span<const uint8_t> request,
        const udp_endpoint& from,
        util::span<uint8_t> reply) = 0;

protected:
    constexpr udp_handler() = default;
    ~udp_handler() = default;
};

// Minimal IPv4 host stack with a static address:
// - ARP: answers requests for our address, announces itself at start-up
// - ICMP: answers echo requests (ping)
// - UDP: one service port, handled by a udp_handler
// Replies go straight back to the sender's MAC address, so no ARP cache is
// needed. Fragmented IP packets and IP options are dropped/ignored.
class stack
{
public:
    struct counters
    {
        uint32_t frames_received;
        uint32_t frames_sent;
        uint32_t arp_replies;
        uint32_t echo_replies;
        uint32_t udp_requests;
        uint32_t dropped;
    };

    constexpr stack(
        nic& nic,
        const ipv4_config& config,
        udp_handler& handler,
        uint16_t udp_port)
        : _nic(nic), _config(config), _handler(handler), _udp_port(udp_port)
    {
    }

    // Send a gratuitous ARP announcement so neighbours learn our MAC.
    void announce();

    // Process every frame the NIC has received.
    void poll();

    const ipv4_config& config() const
    {
        return _config;
    }

    uint16_t udp_port() const
    {
        return _udp_port;
    }

    const counters& stats() const
    {
        return _stats;
    }

private:
    void handle_frame(util::span<const uint8_t> frame);
    void handle_arp(util::span<const uint8_t> frame);
    void handle_ipv4(util::span<const uint8_t> frame);
    void handle_icmp(
        const uint8_t* src_mac,
        const uint8_t* src_ip,
        util::span<const uint8_t> message);
    void handle_udp(
        const uint8_t* src_mac,
        const uint8_t* src_ip,
        const uint8_t* dst_ip,
        util::span<const uint8_t> segment);

    // _tx holds the IPv4 payload at IPV4_PAYLOAD; fill in the headers and
    // send it.
    void send_ipv4(
        const uint8_t* dst_mac,
        const uint8_t* dst_ip,
        uint8_t protocol,
        size_t payload_size);
    void send_frame(size_t size);
    void write_ethernet_header(const uint8_t* dst_mac, uint16_t ethertype);

    void drop()
    {
        ++_stats.dropped;
    }

    static constexpr size_t ETHERNET_HEADER = 14;
    static constexpr size_t IPV4_HEADER = 20;
    static constexpr size_t IPV4_PAYLOAD = ETHERNET_HEADER + IPV4_HEADER;
    static constexpr size_t MAX_FRAME = 1514;

    nic& _nic;
    ipv4_config _config;
    udp_handler& _handler;
    uint16_t _udp_port;
    uint16_t _next_ip_id = 1;
    counters _stats{};
    uint8_t _rx[MAX_FRAME]{};
    uint8_t _tx[MAX_FRAME]{};
};

} // namespace net
