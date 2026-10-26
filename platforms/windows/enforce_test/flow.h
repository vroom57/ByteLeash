// A TCP upload to the sink, sending filler data as fast as TCP allows, with
// acknowledged-byte counts from SIO_TCP_INFO.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace byteleash::enforce_test {

struct Endpoint {
    std::uint32_t address = 0;  // IPv4, host byte order
    std::uint16_t port = 0;
};

// Parses "a.b.c.d:port".
bool parseEndpoint(const std::wstring& text, Endpoint& out);
std::string formatEndpoint(const Endpoint& e);

struct FlowSample {
    bool ok = false;             // false once the connection is gone
    std::uint64_t acked = 0;     // payload bytes the sink acknowledged (approx.)
    std::uint64_t retrans = 0;   // bytes retransmitted
    std::uint32_t timeouts = 0;  // retransmission timeout episodes
};

class UploadFlow {
public:
    UploadFlow() = default;
    ~UploadFlow();
    UploadFlow(const UploadFlow&) = delete;
    UploadFlow& operator=(const UploadFlow&) = delete;

    // Connects (closing any previous connection) and starts sending.
    // Returns 0 or a Winsock error.
    int open(const Endpoint& target, int timeout_ms);
    void close();
    bool isOpen() const { return socket_ != kNoSocket; }
    std::uint16_t localPort() const { return local_port_; }
    FlowSample sample() const;
    int sendError() const { return send_error_; }

private:
    static constexpr std::uintptr_t kNoSocket = ~std::uintptr_t{0};

    std::uintptr_t socket_ = kNoSocket;
    std::uint16_t local_port_ = 0;
    std::thread sender_;
    std::atomic<int> send_error_{0};
    std::atomic<bool> stopping_{false};
};

// Opens and closes a fresh connection. Returns 0 if it connected, otherwise
// a Winsock error (WSAETIMEDOUT after timeout_ms).
int tryConnect(const Endpoint& target, int timeout_ms);

}  // namespace byteleash::enforce_test
