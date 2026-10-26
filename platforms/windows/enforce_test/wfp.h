// WFP block filters for the enforcement test. They live in a dynamic WFP
// session, so Windows removes them when the session closes or this process
// exits, even if it's killed. All filters are named "ByteLeash test".

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flow.h"

namespace byteleash::enforce_test {

class WfpSession {
public:
    WfpSession() = default;
    ~WfpSession();
    WfpSession(const WfpSession&) = delete;
    WfpSession& operator=(const WfpSession&) = delete;

    // Each returns 0 or a Win32 / FWP error code.
    unsigned long open();
    // Blocks the app's connections at the ALE connect layers (IPv4 and
    // IPv6), matched by app ID. Existing connections are affected only if
    // Windows re-authorizes them.
    unsigned long blockApp(const std::wstring& exe_path);
    // Blocks outbound packets of one IPv4 TCP connection at the transport
    // layer, matched by remote address, remote port and local port.
    unsigned long blockConnection(const Endpoint& remote, std::uint16_t local_port);
    unsigned long removeAll();

private:
    void* engine_ = nullptr;
    std::vector<std::uint64_t> filters_;
};

// Deletes any filters named "ByteLeash test" (normally none: dynamic filters
// go away with their process). Returns 0 or an error; `found` is the count.
unsigned long deleteLeftoverFilters(int& found);

}  // namespace byteleash::enforce_test
