#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace byteleash::probe {

// Maps process IDs to ByteLeash app identities (UTF-8), following the
// provisional rules in docs/ideas.md:
//   - normal programs: full exe path, version folders wildcarded
//   - svchost.exe: "svchost.exe [ServiceA, ServiceB]"
//   - PID 4 (and 0): "System"
class ProcessNames {
public:
    const std::string& identity(std::uint32_t pid);

    // PIDs whose process had already exited when first looked up.
    std::uint64_t unresolved() const { return unresolved_; }

private:
    struct Entry {
        std::string identity;
        std::uint64_t create_time = 0;  // FILETIME; detects PID reuse.
        std::chrono::steady_clock::time_point checked;
    };

    std::string lookUp(std::uint32_t pid, std::uint64_t& create_time);
    std::string serviceHost(std::uint32_t pid, const std::string& exe_name);
    void loadServices();

    std::unordered_map<std::uint32_t, Entry> cache_;
    std::map<std::uint32_t, std::vector<std::string>> services_;
    std::chrono::steady_clock::time_point services_loaded_{};
    std::uint64_t unresolved_ = 0;
};

// UTF-16 to UTF-8.
std::string toUtf8(const std::wstring& text);

}  // namespace byteleash::probe
