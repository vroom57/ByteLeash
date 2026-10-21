// byteleash-probe: watch-only prototype. Records which apps send traffic,
// how much and where, using Windows' own ETW events (no driver), and runs
// the ByteLeash engine in learning mode to show what Leash mode would do.
//
// Usage (as administrator):
//   byteleash-probe [--seconds N] [--out report.txt] [--csv sends.csv]
// --seconds 0 runs until Ctrl+C. Default: 120.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "byteleash/address.h"
#include "byteleash/engine.h"
#include "etw_session.h"
#include "process_names.h"

namespace byteleash::probe {
namespace {

constexpr std::uint32_t kMtuPayload = 1460;
constexpr std::uint32_t kDefaultBurst = 64 * 1024;
constexpr int kLiveRows = 12;

HANDLE g_stop = nullptr;

BOOL WINAPI onConsoleCtrl(DWORD) {
    SetEvent(g_stop);
    return TRUE;
}

struct Options {
    int seconds = 120;
    std::filesystem::path out;
    std::filesystem::path csv;
};

bool parseOptions(int argc, wchar_t** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == L"--seconds" && has_value) {
            opt.seconds = _wtoi(argv[++i]);
        } else if (arg == L"--out" && has_value) {
            opt.out = argv[++i];
        } else if (arg == L"--csv" && has_value) {
            opt.csv = argv[++i];
        } else {
            return false;
        }
    }
    return opt.seconds >= 0;
}

bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

LocalDate today() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return std::chrono::year{t.wYear} / std::chrono::month{t.wMonth} /
           std::chrono::day{t.wDay};
}

std::string formatIp(const Ipv4Address& a) {
    return std::format("{}.{}.{}.{}", a[0], a[1], a[2], a[3]);
}

std::string formatIp(const Ipv6Address& a) {
    wchar_t text[INET6_ADDRSTRLEN] = {};
    InetNtopW(AF_INET6, a.data(), text, INET6_ADDRSTRLEN);
    return toUtf8(text);
}

// Canonical form of an address string, or "" if it isn't an address.
std::string canonicalIp(std::wstring text) {
    if (text.starts_with(L"::ffff:")) {
        text = text.substr(7);
    }
    Ipv4Address v4;
    if (InetPtonW(AF_INET, text.c_str(), v4.data()) == 1) {
        return formatIp(v4);
    }
    Ipv6Address v6;
    if (InetPtonW(AF_INET6, text.c_str(), v6.data()) == 1) {
        return formatIp(v6);
    }
    return {};
}

std::string formatBytes(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::format("{} B", bytes);
    }
    if (bytes < 1024 * 1024) {
        return std::format("{:.1f} KB", bytes / 1024.0);
    }
    if (bytes < 1024ull * 1024 * 1024) {
        return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    }
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

// As the GUI will show it.
std::string formatCount(std::uint64_t n) {
    return n > 999 ? "999+" : std::to_string(n);
}

struct AppStats {
    std::uint64_t sends = 0;
    std::uint64_t send_bytes = 0;  // To the internet.
    std::uint64_t udp_send_bytes = 0;
    std::uint64_t lan_sends = 0;
    std::uint64_t lan_send_bytes = 0;
    std::uint64_t receives = 0;
    std::uint64_t receive_bytes = 0;
    std::uint32_t max_send = 0;
    std::uint64_t sends_over_mtu = 0;
    std::uint64_t sends_over_burst = 0;
    std::map<std::string, std::uint64_t> destinations;  // IP -> bytes sent
    std::set<std::uint32_t> pids;
};

class Probe {
public:
    explicit Probe(const Options& opt) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        qpc_freq_ = freq.QuadPart;
        if (!opt.csv.empty()) {
            csv_.open(opt.csv, std::ios::binary);
            csv_ << "seconds,app,pid,protocol,ip,destination,port,bytes,lan\n";
        }
    }

    void onNet(const NetEvent& e) {
        std::lock_guard lock(mu_);
        ++events_;
        if (first_qpc_ == 0) {
            first_qpc_ = e.qpc;
        }
        const std::string& app = names_.identity(e.pid);
        AppStats& stats = apps_[app];
        stats.pids.insert(e.pid);
        const Now now{toInstant(e.qpc), today()};

        if (e.direction == Direction::Receive) {
            ++receives_;
            ++stats.receives;
            stats.receive_bytes += e.size;
            engine_.onInbound(app, e.size, now);
            return;
        }

        ++sends_;
        const bool lan = e.ipv6 ? isLocal(e.remote6) : isLocal(e.remote4);
        const std::string ip = e.ipv6 ? formatIp(e.remote6) : formatIp(e.remote4);
        if (csv_.is_open()) {
            csv_ << std::format("{:.3f},\"{}\",{},{},{},{},{},{},{}\n",
                                seconds(e.qpc), app, e.pid,
                                e.protocol == Protocol::Udp ? "UDP" : "TCP",
                                e.ipv6 ? 6 : 4, ip, e.remote_port, e.size,
                                lan ? 1 : 0);
        }
        if (lan) {
            ++stats.lan_sends;
            stats.lan_send_bytes += e.size;
            return;
        }
        ++stats.sends;
        stats.send_bytes += e.size;
        if (e.protocol == Protocol::Udp) {
            stats.udp_send_bytes += e.size;
        }
        stats.max_send = std::max(stats.max_send, e.size);
        stats.sends_over_mtu += e.size > kMtuPayload;
        stats.sends_over_burst += e.size > kDefaultBurst;
        stats.destinations[ip] += e.size;
        port_bytes_[e.remote_port] += e.size;
        // ETW reports payload only, so there are no header bytes to pass.
        engine_.onOutbound(app, e.size, 0, now);
    }

    void onDns(const DnsEvent& e) {
        std::lock_guard lock(mu_);
        ++dns_answers_;
        const std::string name = toUtf8(e.name);
        std::wstringstream results(e.results);
        std::wstring token;
        while (std::getline(results, token, L';')) {
            if (std::string ip = canonicalIp(token); !ip.empty()) {
                hostnames_[ip] = name;
            }
        }
    }

    void printLive(double elapsed) {
        std::lock_guard lock(mu_);
        std::string out = std::format(
            "\n[{:.0f} s] {} network events ({} sends, {} receives), {} DNS "
            "answers\n{:<60} {:>10} {:>10} {:>9}\n",
            elapsed, events_, sends_, receives_, dns_answers_, "App",
            "Sent", "Received", "Throttle");
        for (const auto* entry : topApps(kLiveRows)) {
            std::string name = entry->first;
            if (name.size() > 60) {
                name = "..." + name.substr(name.size() - 57);
            }
            out += std::format("{:<60} {:>10} {:>10} {:>9}\n", name,
                               formatBytes(entry->second.send_bytes),
                               formatBytes(entry->second.receive_bytes),
                               formatCount(throttleEvents(entry->first)));
        }
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }

    std::string report(const EtwSession& session, double elapsed) {
        std::lock_guard lock(mu_);
        std::string r;
        r += "ByteLeash ETW probe report\n==========================\n\n";
        r += std::format("Duration: {:.0f} s\n", elapsed);
        r += std::format(
            "Network events: {} ({} sends, {} receives)\n"
            "ETW events lost: {}, buffers lost: {}\n"
            "DNS answers seen: {} ({} addresses mapped to names)\n"
            "PIDs already exited when looked up: {}\n\n",
            events_, sends_, receives_, session.eventsLost(),
            session.buffersLost(), dns_answers_, hostnames_.size(),
            names_.unresolved());

        r += "Kernel-Network event IDs seen\n-----------------------------\n";
        for (const auto& [id, s] : session.eventIds()) {
            r += std::format("  {:>3}  {}/{}  count {}  {}\n", id, toUtf8(s.task),
                             toUtf8(s.opcode), s.count,
                             s.decoded ? "decoded" : "not decoded");
        }

        r += "\nTop destination ports by bytes sent (checks port decoding)\n"
             "-----------------------------------------------------------\n";
        std::vector<std::pair<std::uint16_t, std::uint64_t>> ports(
            port_bytes_.begin(), port_bytes_.end());
        std::sort(ports.begin(), ports.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t i = 0; i < std::min<std::size_t>(ports.size(), 10); ++i) {
            r += std::format("  {:>5}  {}\n", ports[i].first,
                             formatBytes(ports[i].second));
        }

        r += "\nApps, by bytes sent to the internet\n"
             "-----------------------------------\n";
        r += "(Would-be counts use the default limits: 64 KB burst, 30 B/s, "
             "3 MB/day.)\n";
        for (const auto* entry : topApps(apps_.size())) {
            const auto& [app, s] = *entry;
            const AppBudget* budget = engine_.find(app);
            r += std::format("\n{}\n", app);
            std::string pids;
            for (std::uint32_t pid : s.pids) {
                pids += (pids.empty() ? "" : ", ") + std::to_string(pid);
            }
            r += std::format("  PIDs: {}\n", pids);
            r += std::format(
                "  Sent to internet: {} in {} sends (UDP {}); to LAN: {} in {} "
                "sends\n",
                formatBytes(s.send_bytes), s.sends, formatBytes(s.udp_send_bytes),
                formatBytes(s.lan_send_bytes), s.lan_sends);
            r += std::format("  Received: {} in {} receives\n",
                             formatBytes(s.receive_bytes), s.receives);
            r += std::format(
                "  Largest send: {} B; sends over {} B: {}; over 64 KB: {}\n",
                s.max_send, kMtuPayload, s.sends_over_mtu, s.sends_over_burst);
            if (budget) {
                r += std::format(
                    "  Would have been throttled: {} times; would have hit "
                    "daily cap: {} days\n",
                    formatCount(budget->dryRun().throttle_events),
                    formatCount(budget->dryRun().cap_days));
            }
            std::vector<std::pair<std::string, std::uint64_t>> dests(
                s.destinations.begin(), s.destinations.end());
            std::sort(dests.begin(), dests.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });
            for (std::size_t i = 0; i < std::min<std::size_t>(dests.size(), 5);
                 ++i) {
                const auto host = hostnames_.find(dests[i].first);
                r += std::format(
                    "    -> {}{}  {}\n", dests[i].first,
                    host == hostnames_.end() ? "" : " (" + host->second + ")",
                    formatBytes(dests[i].second));
            }
            if (dests.size() > 5) {
                r += std::format("    ... and {} more destinations\n",
                                 dests.size() - 5);
            }
        }
        return r;
    }

private:
    using AppEntry = std::pair<const std::string, AppStats>;

    std::vector<const AppEntry*> topApps(std::size_t count) const {
        std::vector<const AppEntry*> list;
        for (const auto& entry : apps_) {
            list.push_back(&entry);
        }
        std::sort(list.begin(), list.end(), [](const auto* a, const auto* b) {
            return a->second.send_bytes + a->second.lan_send_bytes >
                   b->second.send_bytes + b->second.lan_send_bytes;
        });
        if (list.size() > count) {
            list.resize(count);
        }
        return list;
    }

    std::uint64_t throttleEvents(const std::string& app) const {
        const AppBudget* budget = engine_.find(app);
        return budget ? budget->dryRun().throttle_events : 0;
    }

    Instant toInstant(std::int64_t qpc) const {
        const auto whole = std::chrono::seconds(qpc / qpc_freq_);
        const auto part = std::chrono::nanoseconds(
            (qpc % qpc_freq_) * 1'000'000'000 / qpc_freq_);
        return Instant{} + whole + part;
    }

    double seconds(std::int64_t qpc) const {
        return static_cast<double>(qpc - first_qpc_) /
               static_cast<double>(qpc_freq_);
    }

    std::mutex mu_;
    ProcessNames names_;
    Engine engine_{Limits{}, Mode::Learning};
    std::map<std::string, AppStats> apps_;
    std::unordered_map<std::string, std::string> hostnames_;  // IP -> name
    std::map<std::uint16_t, std::uint64_t> port_bytes_;
    std::uint64_t events_ = 0, sends_ = 0, receives_ = 0, dns_answers_ = 0;
    std::int64_t qpc_freq_ = 1;
    std::int64_t first_qpc_ = 0;
    std::ofstream csv_;
};

int run(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    Options opt;
    if (!parseOptions(argc, argv, opt)) {
        std::fputs("Usage: byteleash-probe [--seconds N] [--out report.txt] "
                   "[--csv sends.csv]\n",
                   stderr);
        return 2;
    }
    if (!isElevated()) {
        std::fputs("byteleash-probe must run as administrator (ETW kernel "
                   "network events require it).\n",
                   stderr);
        return 1;
    }

    Probe probe(opt);
    EtwSession session([&](const NetEvent& e) { probe.onNet(e); },
                       [&](const DnsEvent& e) { probe.onDns(e); });
    if (const unsigned long err = session.start(); err != 0) {
        std::fprintf(stderr, "Starting the ETW session failed: error %lu\n", err);
        return 1;
    }

    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    std::thread worker([&] {
        session.process();
        SetEvent(g_stop);
    });

    std::printf("Watching network traffic (watch only, nothing is blocked). %s\n",
                opt.seconds > 0 ? std::format("Running for {} s; Ctrl+C stops early.",
                                              opt.seconds).c_str()
                                : "Ctrl+C stops.");
    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             start)
            .count();
    };
    while (true) {
        DWORD wait = 10'000;
        if (opt.seconds > 0) {
            const double left = opt.seconds - elapsed();
            if (left <= 0) {
                break;
            }
            wait = static_cast<DWORD>(std::min(10'000.0, left * 1000.0));
        }
        if (WaitForSingleObject(g_stop, wait) == WAIT_OBJECT_0) {
            break;
        }
        probe.printLive(elapsed());
    }

    session.stop();
    worker.join();
    const std::string report = probe.report(session, elapsed());
    if (!opt.out.empty()) {
        std::ofstream(opt.out, std::ios::binary) << report;
        std::printf("\nReport written to %s\n", opt.out.string().c_str());
    } else {
        std::fwrite(report.data(), 1, report.size(), stdout);
    }
    return 0;
}

}  // namespace
}  // namespace byteleash::probe

int wmain(int argc, wchar_t** argv) {
    return byteleash::probe::run(argc, argv);
}
