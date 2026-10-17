#!/usr/bin/env python3
"""Browse for a DNS-SD service type over mDNS and print the instances found.

Sends a one-shot (legacy unicast) PTR query to 224.0.0.251:5353, so any
responder on this host or network answers straight back to us. Exits 1 if
nothing answers. Used by the CI smoke tests; standard library only.

usage: mdns_query.py _airplay._tcp [timeout-seconds]
"""
import socket
import struct
import sys
import time


def encode_name(name):
    out = b""
    for label in name.rstrip(".").split("."):
        out += bytes([len(label)]) + label.encode()
    return out + b"\0"


def read_name(data, pos):
    labels, end, jumped = [], None, False
    for _ in range(128):
        length = data[pos]
        if length & 0xC0 == 0xC0:
            if not jumped:
                end = pos + 2
            jumped = True
            pos = ((length & 0x3F) << 8) | data[pos + 1]
            continue
        if length == 0:
            return ".".join(labels), (end if jumped else pos + 1)
        labels.append(data[pos + 1:pos + 1 + length].decode("utf-8", "replace"))
        pos += 1 + length
    raise ValueError("compression loop")


def instances(data, service):
    _, flags, qd, an, _, _ = struct.unpack(">HHHHHH", data[:12])
    if not flags & 0x8000:
        return []
    pos = 12
    for _ in range(qd):
        _, pos = read_name(data, pos)
        pos += 4
    found = []
    for _ in range(an):
        name, pos = read_name(data, pos)
        rtype, _, _, rdlen = struct.unpack(">HHIH", data[pos:pos + 10])
        pos += 10
        if rtype == 12 and name.lower() == service.lower():
            found.append(read_name(data, pos)[0])
        pos += rdlen
    return found


def main():
    service = sys.argv[1].rstrip(".") + ".local"
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    query = struct.pack(">HHHHHH", 0x4153, 0, 1, 0, 0, 0) + encode_name(service) + struct.pack(">HH", 12, 1)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    sock.bind(("", 0))
    sock.settimeout(0.5)

    found = set()
    deadline = time.time() + timeout
    while time.time() < deadline and not found:
        sock.sendto(query, ("224.0.0.251", 5353))
        until = time.time() + 1
        while time.time() < until:
            try:
                data, addr = sock.recvfrom(9000)
            except socket.timeout:
                continue
            try:
                for name in instances(data, service):
                    if name not in found:
                        print(f"{name} (from {addr[0]})")
                    found.add(name)
            except (ValueError, IndexError, struct.error):
                pass
    if not found:
        print(f"no answer for {service}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
