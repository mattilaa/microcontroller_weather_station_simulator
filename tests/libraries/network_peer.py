"""Robot Framework library that acts as a second host on the firmware's network.

QEMU's `dgram` netdev carries every Ethernet frame of the guest's virtio-net
device as one UDP datagram between QEMU and this library. That gives the
tests raw Ethernet access without root rights or TAP devices: they build ARP,
ICMP and UDP frames here, send them to the firmware, and check every field
(including checksums) of what comes back.

Usage:
    ${args}=    Open Network Peer        # returns the QEMU arguments
    Start Firmware    @{args}
    ...
    Close Network Peer
"""

import queue
import socket
import struct
import threading
import time

from robot.api import logger
from robot.utils import timestr_to_secs

FIRMWARE_MAC = "52:54:00:12:34:56"
FIRMWARE_IP = "10.0.2.15"
PEER_MAC = "02:00:00:00:00:02"  # locally administered
PEER_IP = "10.0.2.2"
SERVICE_PORT = 5000

ETHERTYPE_IPV4 = 0x0800
ETHERTYPE_ARP = 0x0806
PROTO_ICMP = 1
PROTO_UDP = 17


def mac_bytes(mac):
    return bytes(int(part, 16) for part in mac.split(":"))


def mac_str(raw):
    return ":".join(f"{b:02x}" for b in raw)


def ip_bytes(ip):
    return socket.inet_aton(ip)


def ip_str(raw):
    return socket.inet_ntoa(raw)


def checksum(data):
    """RFC 1071 Internet checksum."""
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return ~total & 0xFFFF


def udp_checksum(src_ip, dst_ip, segment):
    pseudo = ip_bytes(src_ip) + ip_bytes(dst_ip) + struct.pack(
        "!BBH", 0, PROTO_UDP, len(segment)
    )
    return checksum(pseudo + segment)


class Frame:
    """A parsed Ethernet frame (only the fields the tests look at)."""

    def __init__(self, raw):
        self.raw = raw
        self.dst = mac_str(raw[0:6])
        self.src = mac_str(raw[6:12])
        self.ethertype = struct.unpack("!H", raw[12:14])[0]
        self.payload = raw[14:]

    def is_arp(self, operation=None):
        if self.ethertype != ETHERTYPE_ARP or len(self.payload) < 28:
            return False
        return operation is None or struct.unpack("!H", self.payload[6:8])[0] == operation

    def is_ipv4(self, protocol=None):
        if self.ethertype != ETHERTYPE_IPV4 or len(self.payload) < 20:
            return False
        return protocol is None or self.payload[9] == protocol

    def __str__(self):
        return f"{self.src} -> {self.dst} type {self.ethertype:#06x} ({len(self.raw)} bytes)"


class NetworkPeer:
    ROBOT_LIBRARY_SCOPE = "GLOBAL"

    def __init__(self):
        self._socket = None
        self._qemu_address = None
        self._frames = queue.Queue()
        self._reader = None
        self._ip_id = 0

    # ---- lifecycle ----------------------------------------------------------

    def open_network_peer(self):
        """Bind the peer's socket and return the QEMU arguments that attach a
        virtio-net device to it."""
        self.close_network_peer()
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind(("127.0.0.1", 0))
        peer_port = self._socket.getsockname()[1]
        qemu_port = self._free_udp_port()
        self._qemu_address = ("127.0.0.1", qemu_port)
        self._frames = queue.Queue()
        self._reader = threading.Thread(target=self._receive_loop, daemon=True)
        self._reader.start()
        return [
            "-global", "virtio-mmio.force-legacy=false",
            "-netdev",
            "dgram,id=net0,"
            f"local.type=inet,local.host=127.0.0.1,local.port={qemu_port},"
            f"remote.type=inet,remote.host=127.0.0.1,remote.port={peer_port}",
            "-device", f"virtio-net-device,netdev=net0,mac={FIRMWARE_MAC}",
        ]

    def close_network_peer(self):
        if self._socket is not None:
            self._socket.close()
            self._socket = None
            self._reader.join(timeout=2)

    # ---- ARP ----------------------------------------------------------------

    def wait_for_gratuitous_arp(self, timeout="5s"):
        """Wait for the firmware's start-up ARP announcement and check it."""
        frame = self._wait_for(lambda f: f.is_arp(1), timeout, "gratuitous ARP")
        arp = frame.payload
        sender_mac, sender_ip = mac_str(arp[8:14]), ip_str(arp[14:18])
        target_ip = ip_str(arp[24:28])
        self._expect(frame.dst, "ff:ff:ff:ff:ff:ff", "destination MAC")
        self._expect(sender_mac, FIRMWARE_MAC, "sender MAC")
        self._expect(sender_ip, FIRMWARE_IP, "sender IP")
        self._expect(target_ip, FIRMWARE_IP, "target IP (announcement)")

    def send_arp_request(self, target_ip=FIRMWARE_IP):
        arp = struct.pack("!HHBBH", 1, ETHERTYPE_IPV4, 6, 4, 1)
        arp += mac_bytes(PEER_MAC) + ip_bytes(PEER_IP)
        arp += bytes(6) + ip_bytes(target_ip)
        self._send_ethernet("ff:ff:ff:ff:ff:ff", ETHERTYPE_ARP, arp)

    def wait_for_arp_reply(self, timeout="2s"):
        """Wait for an ARP reply to the peer; returns the MAC it announces."""
        frame = self._wait_for(lambda f: f.is_arp(2), timeout, "ARP reply")
        arp = frame.payload
        self._expect(frame.dst, PEER_MAC, "destination MAC")
        self._expect(frame.src, FIRMWARE_MAC, "source MAC")
        self._expect(ip_str(arp[14:18]), FIRMWARE_IP, "sender IP")
        self._expect(mac_str(arp[18:24]), PEER_MAC, "target MAC")
        self._expect(ip_str(arp[24:28]), PEER_IP, "target IP")
        return mac_str(arp[8:14])

    # ---- ICMP ---------------------------------------------------------------

    def ping_firmware(self, payload="robot", sequence=1, timeout="2s"):
        """Send an ICMP echo request and verify the echo reply. `payload` is
        text, or an integer meaning that many bytes of a test pattern."""
        data = self._payload(payload)
        identifier = 0x4242
        sequence = int(sequence)
        header = struct.pack("!BBHHH", 8, 0, 0, identifier, sequence)
        message = header[:2] + struct.pack("!H", checksum(header + data)) + header[4:] + data
        self._send_ipv4(PROTO_ICMP, message)

        def is_reply(frame):
            if not frame.is_ipv4(PROTO_ICMP):
                return False
            icmp = self._ipv4_payload(frame)
            return icmp[0] == 0 and struct.unpack("!HH", icmp[4:8]) == (identifier, sequence)

        frame = self._wait_for(is_reply, timeout, f"echo reply #{sequence}")
        self._check_ipv4_reply(frame)
        icmp = self._ipv4_payload(frame)
        if checksum(icmp) != 0:
            raise AssertionError("Bad ICMP checksum in echo reply")
        if icmp[8:] != data:
            raise AssertionError(f"Echo payload differs: sent {len(data)} bytes, got {len(icmp) - 8}")

    # ---- UDP ----------------------------------------------------------------

    def send_udp_command(self, command, port=SERVICE_PORT, timeout="2s"):
        """Send a text command to the firmware's UDP service and return the
        reply text (without the trailing newline)."""
        source_port = 40000 + (self._ip_id % 1000)
        self._send_udp(command.encode(), source_port, int(port))

        def is_reply(frame):
            if not frame.is_ipv4(PROTO_UDP):
                return False
            udp = self._ipv4_payload(frame)
            return struct.unpack("!H", udp[2:4])[0] == source_port

        frame = self._wait_for(is_reply, timeout, f"UDP reply to {command!r}")
        self._check_ipv4_reply(frame)
        udp = self._ipv4_payload(frame)
        src_port, dst_port, length, _ = struct.unpack("!HHHH", udp[:8])
        self._expect(src_port, int(port), "UDP source port")
        self._expect(length, len(udp), "UDP length")
        if udp_checksum(FIRMWARE_IP, PEER_IP, udp) != 0:
            raise AssertionError("Bad UDP checksum in reply")
        return udp[8:].decode().rstrip("\n")

    def fetch_weather_log(self, start=0):
        """Page through the firmware's reading log ("log <sequence>") and
        return the readings as a list of dictionaries with numeric values.
        Also checks that sequence numbers are consecutive."""
        columns = ["seq", "day", "time", "temperature", "humidity", "wind_speed", "wind_direction", "rain",
                   "cloud_cover", "lcl", "cirrus", "altostratus", "cumulus", "cumulus_top", "cumulonimbus",
                   "cirrus_base", "cirrus_top", "altostratus_base", "altostratus_top"]
        readings, next_sequence = [], int(start)
        while True:
            header, *rows = self.send_udp_command(f"log {next_sequence}").split("\n")
            fields = dict(item.split("=") for item in header.split()[1:])
            if int(fields["count"]) != len(rows if rows != [""] else []):
                raise AssertionError(f"Header says {fields['count']} rows, got {len(rows)}: {header}")
            for row in rows[: int(fields["count"])]:
                values = dict(zip(columns, row.split(",")))
                for key in ("seq", "day", "wind_direction", "cloud_cover", "lcl", "cirrus", "altostratus",
                            "cumulus", "cumulus_top", "cumulonimbus", "cirrus_base", "cirrus_top",
                            "altostratus_base", "altostratus_top"):
                    values[key] = int(values[key])
                for key in ("temperature", "humidity", "wind_speed", "rain"):
                    values[key] = float(values[key])
                readings.append(values)
            next_sequence = int(fields["next"])
            if int(fields["count"]) == 0:
                break
        for previous, current in zip(readings, readings[1:]):
            if current["seq"] != previous["seq"] + 1:
                raise AssertionError(f"Sequence gap: {previous['seq']} -> {current['seq']}")
        return readings

    # ---- malformed traffic --------------------------------------------------

    def send_ipv4_with_bad_header_checksum(self):
        """An otherwise valid echo request whose IP header checksum is wrong."""
        message = self._echo_request(b"bad header")
        packet = bytearray(self._ipv4_packet(PROTO_ICMP, message))
        packet[10] ^= 0xFF
        self._send_ethernet(FIRMWARE_MAC, ETHERTYPE_IPV4, bytes(packet))

    def send_udp_with_bad_checksum(self, command="ping"):
        segment = bytearray(self._udp_segment(command.encode(), 40999, SERVICE_PORT))
        segment[6] ^= 0xFF
        self._send_ethernet(FIRMWARE_MAC, ETHERTYPE_IPV4, self._ipv4_packet(PROTO_UDP, bytes(segment)))

    def send_udp_without_checksum(self, command="ping"):
        """UDP over IPv4 may omit the checksum (field 0); return the reply."""
        segment = bytearray(self._udp_segment(command.encode(), 40998, SERVICE_PORT))
        segment[6:8] = b"\0\0"
        self._send_ethernet(FIRMWARE_MAC, ETHERTYPE_IPV4, self._ipv4_packet(PROTO_UDP, bytes(segment)))
        frame = self._wait_for(
            lambda f: f.is_ipv4(PROTO_UDP)
            and struct.unpack("!H", self._ipv4_payload(f)[2:4])[0] == 40998,
            "2s",
            "UDP reply",
        )
        return self._ipv4_payload(frame)[8:].decode().rstrip("\n")

    def send_ping_to_other_mac(self):
        message = self._echo_request(b"not for you")
        self._send_ethernet("02:00:00:00:00:99", ETHERTYPE_IPV4, self._ipv4_packet(PROTO_ICMP, message))

    def send_ping_to_other_ip(self):
        message = self._echo_request(b"wrong address")
        self._send_ethernet(
            FIRMWARE_MAC, ETHERTYPE_IPV4, self._ipv4_packet(PROTO_ICMP, message, dst_ip="10.0.2.99")
        )

    def send_udp_to_port(self, port, command="ping"):
        self._send_udp(command.encode(), 40997, int(port))

    def no_reply_should_arrive(self, duration="1s"):
        """Fail if the firmware sends any frame to the peer within `duration`."""
        deadline = time.monotonic() + timestr_to_secs(duration)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            try:
                frame = Frame(self._frames.get(timeout=remaining))
            except queue.Empty:
                return
            if frame.dst == PEER_MAC:
                raise AssertionError(f"Unexpected reply: {frame}")

    # ---- internals ----------------------------------------------------------

    @staticmethod
    def _free_udp_port():
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    @staticmethod
    def _payload(payload):
        if isinstance(payload, int) or str(payload).isdigit():
            return bytes(i % 256 for i in range(int(payload)))
        return str(payload).encode()

    def _receive_loop(self):
        sock = self._socket
        while True:
            try:
                data, _ = sock.recvfrom(65535)
            except OSError:
                return
            self._frames.put(data)

    def _wait_for(self, predicate, timeout, what):
        deadline = time.monotonic() + timestr_to_secs(timeout)
        skipped = []
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(
                    f"No {what} within {timeout}. Other frames: "
                    + (", ".join(skipped) or "none")
                )
            try:
                frame = Frame(self._frames.get(timeout=remaining))
            except queue.Empty:
                continue
            if predicate(frame):
                logger.debug(f"Received {what}: {frame}")
                return frame
            skipped.append(str(frame))

    @staticmethod
    def _expect(actual, expected, what):
        if actual != expected:
            raise AssertionError(f"{what}: expected {expected}, got {actual}")

    @staticmethod
    def _ipv4_payload(frame):
        ip = frame.payload
        header_length = (ip[0] & 0x0F) * 4
        total_length = struct.unpack("!H", ip[2:4])[0]
        return ip[header_length:total_length]

    def _check_ipv4_reply(self, frame):
        ip = frame.payload
        header_length = (ip[0] & 0x0F) * 4
        self._expect(frame.dst, PEER_MAC, "destination MAC")
        self._expect(frame.src, FIRMWARE_MAC, "source MAC")
        self._expect(ip[0] >> 4, 4, "IP version")
        if checksum(ip[:header_length]) != 0:
            raise AssertionError("Bad IPv4 header checksum in reply")
        self._expect(ip_str(ip[12:16]), FIRMWARE_IP, "source IP")
        self._expect(ip_str(ip[16:20]), PEER_IP, "destination IP")

    def _send_ethernet(self, dst_mac, ethertype, payload):
        frame = mac_bytes(dst_mac) + mac_bytes(PEER_MAC) + struct.pack("!H", ethertype) + payload
        if len(frame) < 60:
            frame += bytes(60 - len(frame))
        self._socket.sendto(frame, self._qemu_address)

    def _ipv4_packet(self, protocol, payload, dst_ip=FIRMWARE_IP):
        self._ip_id += 1
        header = struct.pack(
            "!BBHHHBBH4s4s",
            0x45, 0, 20 + len(payload), self._ip_id, 0, 64, protocol, 0,
            ip_bytes(PEER_IP), ip_bytes(dst_ip),
        )
        header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
        return header + payload

    def _send_ipv4(self, protocol, payload):
        self._send_ethernet(FIRMWARE_MAC, ETHERTYPE_IPV4, self._ipv4_packet(protocol, payload))

    @staticmethod
    def _echo_request(data):
        header = struct.pack("!BBHHH", 8, 0, 0, 1, 1)
        return header[:2] + struct.pack("!H", checksum(header + data)) + header[4:] + data

    @staticmethod
    def _udp_segment(data, src_port, dst_port):
        segment = struct.pack("!HHHH", src_port, dst_port, 8 + len(data), 0) + data
        value = udp_checksum(PEER_IP, FIRMWARE_IP, segment) or 0xFFFF
        return segment[:6] + struct.pack("!H", value) + segment[8:]

    def _send_udp(self, data, src_port, dst_port):
        self._send_ipv4(PROTO_UDP, self._udp_segment(data, src_port, dst_port))
