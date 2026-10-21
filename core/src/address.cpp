#include "byteleash/address.h"

#include <algorithm>

namespace byteleash {

bool isLocal(const Ipv4Address& a) {
    return a[0] == 127                                   // Loopback
           || a[0] == 10                                 // Private
           || (a[0] == 172 && (a[1] & 0xF0) == 16)       // Private 172.16/12
           || (a[0] == 192 && a[1] == 168)               // Private
           || (a[0] == 169 && a[1] == 254)               // Link-local
           || (a[0] & 0xF0) == 224                       // Multicast 224/4
           || a == Ipv4Address{255, 255, 255, 255};      // Broadcast
}

bool isLocal(const Ipv6Address& a) {
    constexpr Ipv6Address kLoopback{0, 0, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 1};
    if (a == kLoopback) {
        return true;
    }
    if (a[0] == 0xFE && (a[1] & 0xC0) == 0x80) {  // Link-local fe80::/10
        return true;
    }
    if ((a[0] & 0xFE) == 0xFC) {  // Unique-local fc00::/7
        return true;
    }
    if (a[0] == 0xFF) {  // Multicast ff00::/8
        return true;
    }
    // IPv4-mapped ::ffff:a.b.c.d
    const bool mapped = std::all_of(a.begin(), a.begin() + 10,
                                    [](std::uint8_t b) { return b == 0; }) &&
                        a[10] == 0xFF && a[11] == 0xFF;
    if (mapped) {
        return isLocal(Ipv4Address{a[12], a[13], a[14], a[15]});
    }
    return false;
}

}  // namespace byteleash
