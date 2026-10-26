// byteleash-enforce-test: tries driverless ways to enforce Leash mode
// (docs/plan.md, M1). An upload runs from this PC to a sink on another PC
// while a WFP block filter or a Windows QoS throttle is applied and removed;
// the bytes the sink acknowledges show what actually got through.
//
// Usage:
//   byteleash-enforce-test sink [--port 5201]              (on the other PC)
//   byteleash-enforce-test wfp --target IP:PORT [--mode app|connection]
//       [--before 5] [--block 10] [--after 15] [--out FILE]
//   byteleash-enforce-test qos --target IP:PORT [--rates 240,1600,8000,...]
//       [--step 20] [--before 5] [--after 15] [--reconnect]
//       [--store ActiveStore|localhost] [--out FILE]
//   byteleash-enforce-test cleanup
// wfp, qos and cleanup need an administrator terminal.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "flow.h"
#include "qos.h"
#include "wfp.h"

namespace byteleash::enforce_test {
namespace {

constexpr char kUsage[] =
    "Usage:\n"
    "  byteleash-enforce-test sink [--port 5201]              (on the other PC)\n"
    "  byteleash-enforce-test wfp --target IP:PORT [--mode app|connection]\n"
    "      [--before 5] [--block 10] [--after 15] [--out FILE]\n"
    "  byteleash-enforce-test qos --target IP:PORT [--rates 240,1600,8000,...]\n"
    "      [--step 20] [--before 5] [--after 15] [--reconnect]\n"
    "      [--store ActiveStore|localhost] [--out FILE]\n"
    "  byteleash-enforce-test cleanup\n"
    "wfp, qos and cleanup need an administrator terminal.\n";

constexpr int kConnectTimeoutMs = 5000;

HANDLE g_stop = nullptr;
std::atomic<std::uint64_t> g_sink_bytes{0};
std::atomic<int> g_sink_connections{0};

BOOL WINAPI onConsoleCtrl(DWORD) {
    SetEvent(g_stop);
    return TRUE;
}

bool stopRequested() {
    return WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0;
}

void say(const std::string& text) {
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);
}

struct Options {
    std::wstring command;
    Endpoint target;
    bool has_target = false;
    int port = 5201;
    std::wstring mode = L"app";
    int before = 5;
    int block = 10;
    int after = 15;
    std::vector<std::uint64_t> rates{240, 1600, 8000, 80000, 800000};
    int step = 20;
    bool reconnect = false;
    std::wstring store = L"ActiveStore";
    std::filesystem::path out;
};

bool parseRates(const std::wstring& text, std::vector<std::uint64_t>& rates) {
    rates.clear();
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t comma = std::min(text.find(L',', pos), text.size());
        const std::uint64_t rate =
            _wcstoui64(text.substr(pos, comma - pos).c_str(), nullptr, 10);
        if (rate == 0) {
            return false;
        }
        rates.push_back(rate);
        pos = comma + 1;
    }
    return !rates.empty();
}

bool parseOptions(int argc, wchar_t** argv, Options& opt) {
    if (argc < 2) {
        return false;
    }
    opt.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == L"--target" && has_value) {
            if (!parseEndpoint(argv[++i], opt.target)) {
                return false;
            }
            opt.has_target = true;
        } else if (arg == L"--port" && has_value) {
            opt.port = _wtoi(argv[++i]);
        } else if (arg == L"--mode" && has_value) {
            opt.mode = argv[++i];
        } else if (arg == L"--before" && has_value) {
            opt.before = _wtoi(argv[++i]);
        } else if (arg == L"--block" && has_value) {
            opt.block = _wtoi(argv[++i]);
        } else if (arg == L"--after" && has_value) {
            opt.after = _wtoi(argv[++i]);
        } else if (arg == L"--rates" && has_value) {
            if (!parseRates(argv[++i], opt.rates)) {
                return false;
            }
        } else if (arg == L"--step" && has_value) {
            opt.step = _wtoi(argv[++i]);
        } else if (arg == L"--reconnect") {
            opt.reconnect = true;
        } else if (arg == L"--store" && has_value) {
            opt.store = argv[++i];
        } else if (arg == L"--out" && has_value) {
            opt.out = argv[++i];
        } else {
            return false;
        }
    }
    if (opt.command == L"sink") {
        return opt.port > 0 && opt.port <= 65535;
    }
    if (opt.command == L"wfp") {
        return opt.has_target && (opt.mode == L"app" || opt.mode == L"connection") &&
               opt.before >= 0 && opt.block > 0 && opt.after >= 0;
    }
    if (opt.command == L"qos") {
        return opt.has_target && opt.step > 0 && opt.before >= 0 && opt.after >= 0 &&
               (opt.store == L"ActiveStore" || opt.store == L"localhost");
    }
    return opt.command == L"cleanup";
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

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring exePath() {
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        const DWORD size =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (size < path.size()) {
            path.resize(size);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::string windowsBuild() {
    constexpr wchar_t kKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    wchar_t build[32]{};
    DWORD size = sizeof(build);
    RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"CurrentBuild", RRF_RT_REG_SZ, nullptr,
                 build, &size);
    DWORD revision = 0;
    size = sizeof(revision);
    RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"UBR", RRF_RT_REG_DWORD, nullptr,
                 &revision, &size);
    return std::format("{}.{}", narrow(build), revision);
}

bool memoryIntegrityOn() {
    DWORD enabled = 0;
    DWORD size = sizeof(enabled);
    RegGetValueW(HKEY_LOCAL_MACHINE,
                 L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
                 L"HypervisorEnforcedCodeIntegrity",
                 L"Enabled", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
    return enabled == 1;
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
}

// Prints lines as they come and keeps them for the --out file.
class Report {
public:
    void line(const std::string& text) {
        say(text + "\n");
        text_ += text;
        text_ += '\n';
    }

    void save(const std::filesystem::path& path) const {
        if (path.empty()) {
            return;
        }
        std::ofstream(path, std::ios::binary) << text_;
        say(std::format("\nResults written to {}\n", narrow(path.wstring())));
    }

private:
    std::string text_;
};

void writeHeader(Report& report, const Options& opt) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    report.line(std::format("byteleash-enforce-test {}, {:04}-{:02}-{:02} {:02}:{:02}",
                            narrow(opt.command), t.wYear, t.wMonth, t.wDay, t.wHour,
                            t.wMinute));
    report.line(std::format("Windows build {}, Memory Integrity {}", windowsBuild(),
                            memoryIntegrityOn() ? "on" : "off"));
    report.line(std::format("Sink {}", formatEndpoint(opt.target)));
}

struct Phase {
    std::string name;
    int seconds = 0;
    std::function<std::string()> start;  // runs first; returns a note for the report
};

std::string progressText(double t) {
    return t < 0 ? std::string("none") : std::format("at {:.1f} s", t);
}

// Runs each phase while sampling the flow every 100 ms, printing one line per
// second. At the start of each phase a fresh connection is also attempted,
// to see whether new connections are allowed.
void runPhases(UploadFlow& flow, const Endpoint& target, const std::vector<Phase>& phases,
               Report& report) {
    for (const Phase& phase : phases) {
        if (stopRequested()) {
            report.line("Stopped early.");
            return;
        }
        report.line(std::format("--- {} ({} s)", phase.name, phase.seconds));
        if (phase.start) {
            if (const std::string note = phase.start(); !note.empty()) {
                report.line("    " + note);
            }
        }
        auto fresh = std::async(std::launch::async, [&target] {
            return tryConnect(target, kConnectTimeoutMs);
        });
        const auto start = std::chrono::steady_clock::now();
        const FlowSample initial = flow.sample();
        std::uint64_t acked = initial.acked;
        std::uint64_t acked_at_print = acked;
        double first_progress = -1;
        double last_progress = -1;
        bool lost = !initial.ok;
        if (lost) {
            report.line("    no upload connection at phase start");
        }
        int next_print = 1;
        while (true) {
            WaitForSingleObject(g_stop, 100);
            const double t = secondsSince(start);
            const FlowSample s = flow.sample();
            if (s.ok && s.acked > acked) {
                if (first_progress < 0) {
                    first_progress = t;
                }
                last_progress = t;
                acked = s.acked;
            }
            if (!s.ok && !lost) {
                lost = true;
                report.line(std::format("    {:5.1f} s  upload connection lost (send error {})",
                                        t, flow.sendError()));
            }
            const bool done = t >= phase.seconds || stopRequested();
            if (t >= next_print || done) {
                report.line(std::format(
                    "    {:5.1f} s  +{:>9} B  acked {:>11} B  retransmitted {:>9} B  "
                    "timeouts {}",
                    t, acked - acked_at_print, acked - initial.acked, s.retrans,
                    s.timeouts));
                acked_at_print = acked;
                next_print = static_cast<int>(t) + 1;
            }
            if (done) {
                const double elapsed = std::max(t, 0.1);
                report.line(std::format(
                    "    total {} B acked, {:.0f} B/s average; first progress {}, "
                    "last progress {}",
                    acked - initial.acked,
                    static_cast<double>(acked - initial.acked) / elapsed,
                    progressText(first_progress), progressText(last_progress)));
                break;
            }
        }
        const int fresh_result = fresh.get();
        report.line(fresh_result == 0
                        ? std::string("    new connection: allowed")
                        : std::format("    new connection: failed, Winsock error {}",
                                      fresh_result));
    }
}

int runSink(const Options& opt) {
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(opt.port));
    if (listener == INVALID_SOCKET ||
        bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) ==
            SOCKET_ERROR ||
        listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        std::fputs(std::format("Listening on TCP port {} failed: Winsock error {}\n",
                               opt.port, WSAGetLastError())
                       .c_str(),
                   stderr);
        return 1;
    }
    say(std::format("Sink listening on TCP port {}. Ctrl+C stops.\n", opt.port));
    std::thread acceptor([listener] {
        while (true) {
            sockaddr_in peer{};
            int len = sizeof(peer);
            const SOCKET c = accept(listener, reinterpret_cast<sockaddr*>(&peer), &len);
            if (c == INVALID_SOCKET) {
                return;
            }
            char ip[INET_ADDRSTRLEN]{};
            InetNtopA(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            say(std::format("Connection from {}:{}\n", ip, ntohs(peer.sin_port)));
            ++g_sink_connections;
            std::thread([c] {
                std::vector<char> buffer(64 * 1024);
                int received = 0;
                while ((received = recv(c, buffer.data(), static_cast<int>(buffer.size()),
                                        0)) > 0) {
                    g_sink_bytes += static_cast<std::uint64_t>(received);
                }
                closesocket(c);
                --g_sink_connections;
            }).detach();
        }
    });
    std::uint64_t last = 0;
    int seconds = 0;
    while (WaitForSingleObject(g_stop, 1000) == WAIT_TIMEOUT) {
        ++seconds;
        const std::uint64_t total = g_sink_bytes;
        const int connections = g_sink_connections;
        if (total != last || connections > 0) {
            say(std::format("{:6} s  received {:>10} B/s  total {:>12} B  connections {}\n",
                            seconds, total - last, total, connections));
        }
        last = total;
    }
    closesocket(listener);
    acceptor.join();
    return 0;
}

int runWfp(const Options& opt) {
    Report report;
    writeHeader(report, opt);
    report.line(opt.mode == L"app"
                    ? "Mode app: block this app at the ALE connect layers (app ID)"
                    : "Mode connection: block this one connection at the outbound "
                      "transport layer (5-tuple)");
    WfpSession wfp;
    if (const unsigned long err = wfp.open(); err != 0) {
        report.line(std::format("Opening the WFP engine failed: error {:#x}", err));
        return 1;
    }
    UploadFlow flow;
    if (const int err = flow.open(opt.target, kConnectTimeoutMs); err != 0) {
        report.line(std::format("Connecting to the sink failed: Winsock error {}", err));
        return 1;
    }
    const std::wstring exe = exePath();
    std::vector<Phase> phases;
    phases.push_back({"before: no filter", opt.before, {}});
    phases.push_back({"blocked", opt.block, [&] {
        const auto start = std::chrono::steady_clock::now();
        const unsigned long result =
            opt.mode == L"app" ? wfp.blockApp(exe)
                               : wfp.blockConnection(opt.target, flow.localPort());
        const double ms = secondsSince(start) * 1000;
        return result == 0
                   ? std::format("block filter added in {:.1f} ms", ms)
                   : std::format("adding the block filter failed: error {:#x}", result);
    }});
    phases.push_back({"unblocked: filter removed", opt.after, [&] {
        const unsigned long result = wfp.removeAll();
        return result == 0
                   ? std::string("filter removed")
                   : std::format("removing the filter failed: error {:#x}", result);
    }});
    runPhases(flow, opt.target, phases, report);
    flow.close();
    report.save(opt.out);
    return 0;
}

int runQos(const Options& opt) {
    Report report;
    writeHeader(report, opt);
    report.line(std::format("QoS policy store {}; {}", narrow(opt.store),
                            opt.reconnect ? "a new connection for each rate"
                                          : "one connection throughout"));
    removeQosThrottle(opt.store);
    UploadFlow flow;
    if (const int err = flow.open(opt.target, kConnectTimeoutMs); err != 0) {
        report.line(std::format("Connecting to the sink failed: Winsock error {}", err));
        return 1;
    }
    const std::wstring exe = exePath();
    std::vector<Phase> phases;
    phases.push_back({"before: no policy", opt.before, {}});
    for (const std::uint64_t rate : opt.rates) {
        const std::string name =
            std::format("throttle {} bit/s (expect {:.1f} B/s)", rate,
                        static_cast<double>(rate) / 8);
        phases.push_back({name, opt.step, [&, rate] {
            removeQosThrottle(opt.store);
            const auto start = std::chrono::steady_clock::now();
            const int code = addQosThrottle(exe, rate, opt.store);
            std::string note =
                code == 0 ? std::format("policy added in {:.0f} ms",
                                        secondsSince(start) * 1000)
                          : std::format("New-NetQosPolicy failed (exit code {})", code);
            if (opt.reconnect || !flow.sample().ok) {
                const int result = flow.open(opt.target, kConnectTimeoutMs);
                note += result == 0
                            ? std::string("; new upload connection opened")
                            : std::format("; reconnecting failed: Winsock error {}", result);
            }
            return note;
        }});
    }
    phases.push_back({"after: policy removed", opt.after, [&] {
        return removeQosThrottle(opt.store) >= 0 ? std::string("policy removed")
                                                 : std::string("removing the policy failed");
    }});
    runPhases(flow, opt.target, phases, report);
    removeQosThrottle(opt.store);  // covers Ctrl+C mid-run
    flow.close();
    report.save(opt.out);
    return 0;
}

int runCleanup() {
    int found = 0;
    if (const unsigned long err = deleteLeftoverFilters(found); err != 0) {
        say(std::format("Checking WFP filters failed: error {:#x}\n", err));
    } else {
        say(std::format("WFP filters named \"ByteLeash test\": {}\n",
                        found == 0 ? std::string("none")
                                   : std::format("{} found and deleted", found)));
    }
    for (const wchar_t* store : {L"localhost", L"ActiveStore"}) {
        const int removed = removeQosThrottle(store);
        say(std::format("QoS policy \"ByteLeash test\" in {}: {}\n", narrow(store),
                        removed > 0    ? "found and removed"
                        : removed == 0 ? "none"
                                       : "check failed"));
    }
    return 0;
}

int run(int argc, wchar_t** argv) {
    Options opt;
    if (!parseOptions(argc, argv, opt)) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    if (opt.command != L"sink" && !isElevated()) {
        std::fputs("wfp, qos and cleanup need an administrator terminal.\n", stderr);
        return 1;
    }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fputs("Winsock failed to start.\n", stderr);
        return 1;
    }
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    int result = 0;
    if (opt.command == L"sink") {
        result = runSink(opt);
    } else if (opt.command == L"wfp") {
        result = runWfp(opt);
    } else if (opt.command == L"qos") {
        result = runQos(opt);
    } else {
        result = runCleanup();
    }
    WSACleanup();
    return result;
}

}  // namespace
}  // namespace byteleash::enforce_test

int wmain(int argc, wchar_t** argv) {
    return byteleash::enforce_test::run(argc, argv);
}
