#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>

#include "flow.h"

#include <format>
#include <vector>

namespace byteleash::enforce_test {
namespace {

SOCKET toSocket(std::uintptr_t s) {
    return static_cast<SOCKET>(s);
}

sockaddr_in toSockaddr(const Endpoint& e) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(e.address);
    addr.sin_port = htons(e.port);
    return addr;
}

// Connects with a timeout and leaves the socket blocking. Returns 0 or a
// Winsock error.
int connectWithTimeout(SOCKET s, const Endpoint& target, int timeout_ms) {
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    const sockaddr_in addr = toSockaddr(target);
    int result = 0;
    if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        result = WSAGetLastError();
        if (result == WSAEWOULDBLOCK) {
            fd_set writable{};
            writable.fd_count = 1;
            writable.fd_array[0] = s;
            fd_set failed = writable;
            timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
            const int ready = select(0, nullptr, &writable, &failed, &timeout);
            if (ready == 0) {
                result = WSAETIMEDOUT;
            } else if (ready == SOCKET_ERROR) {
                result = WSAGetLastError();
            } else if (__WSAFDIsSet(s, &failed)) {
                int so_error = 0;
                int len = sizeof(so_error);
                getsockopt(s, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&so_error), &len);
                result = so_error != 0 ? so_error : WSAECONNREFUSED;
            } else {
                result = 0;
            }
        }
    }
    nonblocking = 0;
    ioctlsocket(s, FIONBIO, &nonblocking);
    return result;
}

}  // namespace

bool parseEndpoint(const std::wstring& text, Endpoint& out) {
    const auto colon = text.rfind(L':');
    if (colon == std::wstring::npos) {
        return false;
    }
    const std::wstring host = text.substr(0, colon);
    const int port = _wtoi(text.c_str() + colon + 1);
    in_addr addr{};
    if (port <= 0 || port > 65535 || InetPtonW(AF_INET, host.c_str(), &addr) != 1) {
        return false;
    }
    out.address = ntohl(addr.s_addr);
    out.port = static_cast<std::uint16_t>(port);
    return true;
}

std::string formatEndpoint(const Endpoint& e) {
    return std::format("{}.{}.{}.{}:{}", e.address >> 24, (e.address >> 16) & 0xff,
                       (e.address >> 8) & 0xff, e.address & 0xff, e.port);
}

UploadFlow::~UploadFlow() {
    close();
}

int UploadFlow::open(const Endpoint& target, int timeout_ms) {
    close();
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return WSAGetLastError();
    }
    // A small send buffer keeps queued data low, so acknowledged bytes follow
    // what the app is trying to send closely.
    const int send_buffer = 16 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&send_buffer),
               sizeof(send_buffer));
    if (const int err = connectWithTimeout(s, target, timeout_ms); err != 0) {
        closesocket(s);
        return err;
    }
    sockaddr_in local{};
    int len = sizeof(local);
    getsockname(s, reinterpret_cast<sockaddr*>(&local), &len);
    local_port_ = ntohs(local.sin_port);
    socket_ = s;
    send_error_ = 0;
    stopping_ = false;
    sender_ = std::thread([this, s] {
        const std::vector<char> chunk(1024, 'x');
        while (!stopping_) {
            if (send(s, chunk.data(), static_cast<int>(chunk.size()), 0) ==
                SOCKET_ERROR) {
                if (!stopping_) {
                    send_error_ = WSAGetLastError();
                }
                return;
            }
        }
    });
    return 0;
}

void UploadFlow::close() {
    if (!isOpen()) {
        return;
    }
    stopping_ = true;
    closesocket(toSocket(socket_));  // also ends a send that's blocked
    if (sender_.joinable()) {
        sender_.join();
    }
    socket_ = kNoSocket;
}

FlowSample UploadFlow::sample() const {
    FlowSample out;
    if (!isOpen()) {
        return out;
    }
    DWORD version = 0;
    TCP_INFO_v0 info{};
    DWORD bytes = 0;
    if (WSAIoctl(toSocket(socket_), SIO_TCP_INFO, &version, sizeof(version), &info,
                 sizeof(info), &bytes, nullptr, nullptr) != 0) {
        return out;
    }
    out.ok = info.State == TCPSTATE_ESTABLISHED && send_error_ == 0;
    const std::uint64_t unacked = info.BytesRetrans + info.BytesInFlight;
    out.acked = info.BytesOut > unacked ? info.BytesOut - unacked : 0;
    out.retrans = info.BytesRetrans;
    out.timeouts = info.TimeoutEpisodes;
    return out;
}

int tryConnect(const Endpoint& target, int timeout_ms) {
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return WSAGetLastError();
    }
    const int result = connectWithTimeout(s, target, timeout_ms);
    closesocket(s);
    return result;
}

}  // namespace byteleash::enforce_test
