#include <doctest/doctest.h>

#include "byteleash/address.h"

using namespace byteleash;

namespace {

// ::ffff:a.b.c.d
Ipv6Address mapped(std::uint8_t a, std::uint8_t b, std::uint8_t c,
                   std::uint8_t d) {
    return {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, a, b, c, d};
}

// An address starting with the given first two bytes, rest zero except the
// last byte (so it isn't the unspecified address).
Ipv6Address v6(std::uint8_t b0, std::uint8_t b1) {
    return {b0, b1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
}

}  // namespace

TEST_CASE("IPv4 local ranges") {
    CHECK(isLocal(Ipv4Address{127, 0, 0, 1}));
    CHECK(isLocal(Ipv4Address{10, 1, 2, 3}));
    CHECK(isLocal(Ipv4Address{172, 16, 0, 1}));
    CHECK(isLocal(Ipv4Address{172, 31, 255, 255}));
    CHECK(isLocal(Ipv4Address{192, 168, 1, 10}));
    CHECK(isLocal(Ipv4Address{169, 254, 7, 7}));
    CHECK(isLocal(Ipv4Address{224, 0, 0, 251}));      // mDNS
    CHECK(isLocal(Ipv4Address{239, 255, 255, 250}));  // SSDP
    CHECK(isLocal(Ipv4Address{255, 255, 255, 255}));
}

TEST_CASE("IPv4 internet addresses, including range edges") {
    CHECK_FALSE(isLocal(Ipv4Address{8, 8, 8, 8}));
    CHECK_FALSE(isLocal(Ipv4Address{172, 15, 255, 255}));
    CHECK_FALSE(isLocal(Ipv4Address{172, 32, 0, 0}));
    CHECK_FALSE(isLocal(Ipv4Address{192, 167, 1, 1}));
    CHECK_FALSE(isLocal(Ipv4Address{169, 253, 1, 1}));
    CHECK_FALSE(isLocal(Ipv4Address{223, 255, 255, 255}));
    CHECK_FALSE(isLocal(Ipv4Address{240, 0, 0, 1}));
    CHECK_FALSE(isLocal(Ipv4Address{100, 64, 0, 1}));  // CGNAT / Tailscale
}

TEST_CASE("IPv6 local ranges") {
    CHECK(isLocal(Ipv6Address{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
    CHECK(isLocal(v6(0xFE, 0x80)));
    CHECK(isLocal(v6(0xFE, 0xBF)));  // Top of fe80::/10
    CHECK(isLocal(v6(0xFC, 0x00)));
    CHECK(isLocal(v6(0xFD, 0x12)));
    CHECK(isLocal(v6(0xFF, 0x02)));  // Link-local multicast
}

TEST_CASE("IPv6 internet addresses") {
    CHECK_FALSE(isLocal(v6(0x20, 0x01)));  // Global unicast 2001::
    CHECK_FALSE(isLocal(v6(0xFE, 0xC0)));  // Just above fe80::/10
    CHECK_FALSE(isLocal(v6(0xFB, 0xFF)));  // Just below fc00::/7
    CHECK_FALSE(isLocal(Ipv6Address{}));   // Unspecified ::
}

TEST_CASE("IPv4-mapped IPv6 uses the IPv4 rules") {
    CHECK(isLocal(mapped(192, 168, 0, 5)));
    CHECK_FALSE(isLocal(mapped(8, 8, 4, 4)));
}
