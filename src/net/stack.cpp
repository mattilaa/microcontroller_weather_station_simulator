#include "net/stack.hpp"

#include "net/bytes.hpp"

namespace net
{

namespace
{
constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_ARP = 0x0806;

constexpr uint16_t ARP_REQUEST = 1;
constexpr uint16_t ARP_REPLY = 2;
constexpr size_t ARP_PACKET = 28;

constexpr uint8_t PROTOCOL_ICMP = 1;
constexpr uint8_t PROTOCOL_UDP = 17;
constexpr uint8_t ICMP_ECHO_REPLY = 0;
constexpr uint8_t ICMP_ECHO_REQUEST = 8;

constexpr size_t MIN_FRAME = 60; // without FCS; shorter frames are padded
constexpr size_t UDP_HEADER = 8;

bool equal(const uint8_t* a, const uint8_t* b, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        if(a[i] != b[i])
            return false;
    return true;
}

// Sum of the UDP pseudo-header (RFC 768).
uint32_t udp_pseudo_header_sum(
    const uint8_t* src_ip,
    const uint8_t* dst_ip,
    uint16_t udp_length)
{
    uint32_t sum = checksum_add(0, {src_ip, 4});
    sum = checksum_add(sum, {dst_ip, 4});
    return sum + PROTOCOL_UDP + udp_length;
}
} // namespace

void stack::announce()
{
    // Gratuitous ARP (RFC 5227 announcement): a broadcast request for our
    // own address.
    write_ethernet_header(BROADCAST_MAC.bytes, ETHERTYPE_ARP);
    uint8_t* arp = _tx + ETHERNET_HEADER;
    store_be16(arp + 0, 1);              // hardware: Ethernet
    store_be16(arp + 2, ETHERTYPE_IPV4); // protocol: IPv4
    arp[4] = 6;
    arp[5] = 4;
    store_be16(arp + 6, ARP_REQUEST);
    copy_bytes(arp + 8, _nic.mac().bytes, 6);
    copy_bytes(arp + 14, _config.address.bytes, 4);
    fill_bytes(arp + 18, 0, 6);
    copy_bytes(arp + 24, _config.address.bytes, 4);
    send_frame(ETHERNET_HEADER + ARP_PACKET);
}

void stack::poll()
{
    for(;;)
    {
        const size_t size = _nic.receive(_rx);
        if(size == 0)
            return;
        ++_stats.frames_received;
        handle_frame({_rx, size});
    }
}

void stack::handle_frame(util::span<const uint8_t> frame)
{
    if(frame.size() < ETHERNET_HEADER)
        return drop();
    const uint8_t* dst = frame.data();
    if(!equal(dst, _nic.mac().bytes, 6) && !equal(dst, BROADCAST_MAC.bytes, 6))
        return drop(); // not for us

    switch(load_be16(frame.data() + 12))
    {
    case ETHERTYPE_ARP:
        return handle_arp(frame);
    case ETHERTYPE_IPV4:
        return handle_ipv4(frame);
    default:
        return drop();
    }
}

void stack::handle_arp(util::span<const uint8_t> frame)
{
    if(frame.size() < ETHERNET_HEADER + ARP_PACKET)
        return drop();
    const uint8_t* arp = frame.data() + ETHERNET_HEADER;
    if(load_be16(arp) != 1 || load_be16(arp + 2) != ETHERTYPE_IPV4 ||
       arp[4] != 6 || arp[5] != 4)
        return drop();
    if(load_be16(arp + 6) != ARP_REQUEST ||
       !equal(arp + 24, _config.address.bytes, 4))
        return drop(); // not asking for us

    const uint8_t* sender_mac = arp + 8;
    const uint8_t* sender_ip = arp + 14;

    write_ethernet_header(sender_mac, ETHERTYPE_ARP);
    uint8_t* reply = _tx + ETHERNET_HEADER;
    copy_bytes(reply, arp, 6); // htype, ptype, hlen, plen
    store_be16(reply + 6, ARP_REPLY);
    copy_bytes(reply + 8, _nic.mac().bytes, 6);
    copy_bytes(reply + 14, _config.address.bytes, 4);
    copy_bytes(reply + 18, sender_mac, 6);
    copy_bytes(reply + 24, sender_ip, 4);
    send_frame(ETHERNET_HEADER + ARP_PACKET);
    ++_stats.arp_replies;
}

void stack::handle_ipv4(util::span<const uint8_t> frame)
{
    const util::span<const uint8_t> packet = frame.subspan(ETHERNET_HEADER);
    if(packet.size() < IPV4_HEADER)
        return drop();
    const uint8_t* ip = packet.data();
    const size_t header_size = (ip[0] & 0x0fu) * 4u;
    const size_t total_size = load_be16(ip + 2);
    if((ip[0] >> 4) != 4 || header_size < IPV4_HEADER ||
       total_size < header_size || total_size > packet.size())
        return drop();
    if(internet_checksum({ip, header_size}) != 0)
        return drop(); // corrupted header
    if((load_be16(ip + 6) & 0x3fff) != 0)
        return drop(); // fragment (MF set or non-zero offset)

    const uint8_t* src_ip = ip + 12;
    const uint8_t* dst_ip = ip + 16;
    if(!equal(dst_ip, _config.address.bytes, 4) &&
       !equal(dst_ip, BROADCAST_IPV4.bytes, 4))
        return drop();

    const util::span<const uint8_t>
        payload{ip + header_size, total_size - header_size};
    const uint8_t* src_mac = frame.data() + 6;
    switch(ip[9])
    {
    case PROTOCOL_ICMP:
        return handle_icmp(src_mac, src_ip, payload);
    case PROTOCOL_UDP:
        return handle_udp(src_mac, src_ip, dst_ip, payload);
    default:
        return drop();
    }
}

void stack::handle_icmp(
    const uint8_t* src_mac,
    const uint8_t* src_ip,
    util::span<const uint8_t> message)
{
    if(message.size() < 8 || message[0] != ICMP_ECHO_REQUEST ||
       message[1] != 0 || internet_checksum(message) != 0)
        return drop();
    if(message.size() > MAX_FRAME - IPV4_PAYLOAD)
        return drop();

    // Echo reply: same identifier, sequence number and data.
    uint8_t* reply = _tx + IPV4_PAYLOAD;
    copy_bytes(reply, message.data(), message.size());
    reply[0] = ICMP_ECHO_REPLY;
    store_be16(reply + 2, 0);
    store_be16(reply + 2, internet_checksum({reply, message.size()}));

    // src_mac/src_ip point into _rx, which send_ipv4 doesn't touch.
    send_ipv4(src_mac, src_ip, PROTOCOL_ICMP, message.size());
    ++_stats.echo_replies;
}

void stack::handle_udp(
    const uint8_t* src_mac,
    const uint8_t* src_ip,
    const uint8_t* dst_ip,
    util::span<const uint8_t> segment)
{
    if(segment.size() < UDP_HEADER)
        return drop();
    const uint16_t udp_length = load_be16(segment.data() + 4);
    if(udp_length < UDP_HEADER || udp_length > segment.size())
        return drop();
    segment = segment.first(udp_length);

    // A zero checksum means "not computed" (allowed for UDP over IPv4).
    if(load_be16(segment.data() + 6) != 0)
    {
        const uint32_t sum = udp_pseudo_header_sum(src_ip, dst_ip, udp_length);
        if(checksum_finish(checksum_add(sum, segment)) != 0)
            return drop();
    }

    const uint16_t src_port = load_be16(segment.data());
    const uint16_t dst_port = load_be16(segment.data() + 2);
    if(dst_port != _udp_port)
        return drop();
    ++_stats.udp_requests;

    udp_endpoint from{};
    copy_bytes(from.mac.bytes, src_mac, 6);
    copy_bytes(from.ip.bytes, src_ip, 4);
    from.port = src_port;

    uint8_t* reply = _tx + IPV4_PAYLOAD;
    const util::span<uint8_t> reply_payload{
        reply + UDP_HEADER,
        MAX_FRAME - IPV4_PAYLOAD - UDP_HEADER};
    const size_t reply_size =
        _handler.handle(segment.subspan(UDP_HEADER), from, reply_payload);
    if(reply_size == 0)
        return;

    const auto reply_length = static_cast<uint16_t>(UDP_HEADER + reply_size);
    store_be16(reply + 0, _udp_port);
    store_be16(reply + 2, src_port);
    store_be16(reply + 4, reply_length);
    store_be16(reply + 6, 0);
    uint16_t checksum = checksum_finish(checksum_add(
        udp_pseudo_header_sum(_config.address.bytes, src_ip, reply_length),
        {reply, reply_length}));
    if(checksum == 0)
        checksum = 0xffff; // 0 would mean "no checksum"
    store_be16(reply + 6, checksum);

    send_ipv4(from.mac.bytes, from.ip.bytes, PROTOCOL_UDP, reply_length);
}

void stack::send_ipv4(
    const uint8_t* dst_mac,
    const uint8_t* dst_ip,
    uint8_t protocol,
    size_t payload_size)
{
    write_ethernet_header(dst_mac, ETHERTYPE_IPV4);
    uint8_t* ip = _tx + ETHERNET_HEADER;
    ip[0] = 0x45; // IPv4, 20-byte header
    ip[1] = 0;
    store_be16(ip + 2, static_cast<uint16_t>(IPV4_HEADER + payload_size));
    store_be16(ip + 4, _next_ip_id++);
    store_be16(ip + 6, 0x4000); // don't fragment
    ip[8] = 64;                 // TTL
    ip[9] = protocol;
    store_be16(ip + 10, 0);
    copy_bytes(ip + 12, _config.address.bytes, 4);
    copy_bytes(ip + 16, dst_ip, 4);
    store_be16(ip + 10, internet_checksum({ip, IPV4_HEADER}));
    send_frame(IPV4_PAYLOAD + payload_size);
}

void stack::write_ethernet_header(const uint8_t* dst_mac, uint16_t ethertype)
{
    copy_bytes(_tx, dst_mac, 6);
    copy_bytes(_tx + 6, _nic.mac().bytes, 6);
    store_be16(_tx + 12, ethertype);
}

void stack::send_frame(size_t size)
{
    if(size < MIN_FRAME)
    {
        fill_bytes(_tx + size, 0, MIN_FRAME - size);
        size = MIN_FRAME;
    }
    if(_nic.send({_tx, size}))
        ++_stats.frames_sent;
}

} // namespace net
