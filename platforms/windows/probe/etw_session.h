#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "byteleash/address.h"

namespace byteleash::probe {

enum class Protocol { Tcp, Udp };
enum class Direction { Send, Receive };

// One TCP/UDP send or receive from Microsoft-Windows-Kernel-Network.
struct NetEvent {
    std::int64_t qpc = 0;    // Event timestamp, QueryPerformanceCounter ticks.
    std::uint32_t pid = 0;   // Process the traffic belongs to.
    std::uint32_t size = 0;  // Payload bytes (no headers).
    Protocol protocol = Protocol::Tcp;
    Direction direction = Direction::Send;
    bool ipv6 = false;
    // Destination of a send. Not filled in for receives.
    Ipv4Address remote4{};
    Ipv6Address remote6{};
    std::uint16_t remote_port = 0;
};

// A completed DNS query from Microsoft-Windows-DNS-Client (event 3008).
struct DnsEvent {
    std::uint32_t pid = 0;
    std::wstring name;     // e.g. "www.example.com"
    std::wstring results;  // e.g. "93.184.215.14;::ffff:93.184.215.14;"
};

// Diagnostics for one event ID, to check the decoding against what Windows
// actually sends.
struct EventIdStats {
    std::wstring task;    // TDH task name, e.g. "TcpIp"
    std::wstring opcode;  // TDH opcode name, e.g. "Send"
    std::uint64_t count = 0;
    bool decoded = false;  // Layout had the fields we need.
};

// A private real-time ETW session with the kernel network and DNS client
// providers enabled. Requires administrator rights.
class EtwSession {
public:
    using NetHandler = std::function<void(const NetEvent&)>;
    using DnsHandler = std::function<void(const DnsEvent&)>;

    EtwSession(NetHandler on_net, DnsHandler on_dns);
    ~EtwSession();
    EtwSession(const EtwSession&) = delete;
    EtwSession& operator=(const EtwSession&) = delete;

    // Returns 0 or a Win32 error code.
    unsigned long start();
    // Delivers events on the calling thread until stop() is called.
    unsigned long process();
    // Stops the session; process() then returns. Safe to call twice.
    void stop();

    std::uint64_t eventsLost() const { return events_lost_; }
    std::uint64_t buffersLost() const { return buffers_lost_; }
    // Kernel-Network event IDs seen. Only touch after process() returns.
    const std::map<unsigned, EventIdStats>& eventIds() const { return ids_; }

    // Internal: called by the ETW callback with a PEVENT_RECORD.
    void onEvent(void* record);

private:
    // Byte offsets of the fields we need in a Kernel-Network event, worked
    // out once per (event ID, version) from the event's TDH schema.
    struct Layout {
        bool valid = false;
        std::size_t pid = 0, size = 0, daddr = 0, dport = 0;
        std::size_t daddr_len = 0;  // 4 (IPv4) or 16 (IPv6)
    };

    void onNetworkEvent(void* record);
    void onDnsEvent(void* record);
    const Layout& layoutFor(void* record);

    NetHandler on_net_;
    DnsHandler on_dns_;
    std::uint64_t session_ = 0;
    std::uint64_t trace_ = ~0ull;
    bool stopped_ = false;
    std::uint64_t events_lost_ = 0;
    std::uint64_t buffers_lost_ = 0;
    std::map<std::pair<unsigned, unsigned>, Layout> layouts_;
    std::map<unsigned, EventIdStats> ids_;
};

}  // namespace byteleash::probe
