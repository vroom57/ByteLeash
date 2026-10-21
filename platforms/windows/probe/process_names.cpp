#include "process_names.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>

#include "byteleash/app_identity.h"

namespace byteleash::probe {

namespace {

using Clock = std::chrono::steady_clock;

// Re-check a cached PID this often, to notice PID reuse.
constexpr auto kRecheck = std::chrono::seconds(5);
// Reload the service list at most this often.
constexpr auto kServiceReload = std::chrono::seconds(2);

struct HandleCloser {
    void operator()(HANDLE h) const { CloseHandle(h); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

struct ScHandleCloser {
    void operator()(SC_HANDLE h) const { CloseServiceHandle(h); }
};
using UniqueScHandle = std::unique_ptr<SC_HANDLE__, ScHandleCloser>;

std::uint64_t creationTime(HANDLE process) {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        return 0;
    }
    return (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}

bool equalsIgnoreCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

}  // namespace

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

const std::string& ProcessNames::identity(std::uint32_t pid) {
    const auto now = Clock::now();
    auto it = cache_.find(pid);
    if (it != cache_.end() && now - it->second.checked < kRecheck) {
        return it->second.identity;
    }

    std::uint64_t create_time = 0;
    std::string name = lookUp(pid, create_time);
    if (it != cache_.end()) {
        // Same process still running, or it has exited (keep the old name
        // for traffic still arriving from it): keep the entry.
        if (name.empty() || create_time == it->second.create_time) {
            it->second.checked = now;
            return it->second.identity;
        }
    }
    if (name.empty()) {
        ++unresolved_;
        name = "<exited process, PID " + std::to_string(pid) + ">";
    }
    Entry& entry = cache_[pid];
    entry.identity = std::move(name);
    entry.create_time = create_time;
    entry.checked = now;
    return entry.identity;
}

std::string ProcessNames::lookUp(std::uint32_t pid, std::uint64_t& create_time) {
    if (pid == 0 || pid == 4) {
        return "System";
    }
    UniqueHandle process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) {
        return {};
    }
    create_time = creationTime(process.get());

    std::wstring path(MAX_PATH, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    while (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || path.size() > 32768) {
            return {};
        }
        path.resize(path.size() * 2);
        size = static_cast<DWORD>(path.size());
    }
    path.resize(size);

    const std::wstring exe = path.substr(path.find_last_of(L'\\') + 1);
    if (equalsIgnoreCase(exe, L"svchost.exe")) {
        return serviceHost(pid, toUtf8(exe));
    }
    return wildcardVersionFolders(toUtf8(path));
}

std::string ProcessNames::serviceHost(std::uint32_t pid,
                                      const std::string& exe_name) {
    auto it = services_.find(pid);
    if (it == services_.end() &&
        Clock::now() - services_loaded_ > kServiceReload) {
        loadServices();
        it = services_.find(pid);
    }
    if (it == services_.end()) {
        return exe_name + " [unknown service, PID " + std::to_string(pid) + "]";
    }
    return serviceHostIdentity(exe_name, it->second);
}

void ProcessNames::loadServices() {
    services_loaded_ = Clock::now();
    UniqueScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE));
    if (!scm) {
        return;
    }
    DWORD needed = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm.get(), SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_ACTIVE, nullptr, 0, &needed, &count, &resume,
                          nullptr);
    std::vector<BYTE> buffer(needed);
    resume = 0;
    if (!EnumServicesStatusExW(scm.get(), SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                               SERVICE_ACTIVE, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &needed,
                               &count, &resume, nullptr)) {
        return;
    }
    services_.clear();
    const auto* list =
        reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    for (DWORD i = 0; i < count; ++i) {
        const DWORD service_pid = list[i].ServiceStatusProcess.dwProcessId;
        if (service_pid != 0) {
            services_[service_pid].push_back(toUtf8(list[i].lpServiceName));
        }
    }
}

}  // namespace byteleash::probe
