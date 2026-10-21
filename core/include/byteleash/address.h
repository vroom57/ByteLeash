#pragma once

#include <array>
#include <cstdint>

namespace byteleash {

// Addresses in network byte order, as they appear in the packet.
using Ipv4Address = std::array<std::uint8_t, 4>;
using Ipv6Address = std::array<std::uint8_t, 16>;

// True if traffic to this destination stays on the local network and is
// exempt from limits. Provisional; see "LAN exemption" in docs/ideas.md.
//
// IPv4: loopback 127/8, private 10/8, 172.16/12, 192.168/16, link-local
// 169.254/16, multicast 224/4, broadcast 255.255.255.255.
// IPv6: loopback ::1, link-local fe80::/10, unique-local fc00::/7, multicast
// ff00::/8, and IPv4-mapped ::ffff:a.b.c.d classified as its IPv4 address.
//
// Not local: CGNAT 100.64/10 (ISP side; also Tailscale) and global IPv6
// addresses, even on-link ones.
bool isLocal(const Ipv4Address& address);
bool isLocal(const Ipv6Address& address);

}  // namespace byteleash
