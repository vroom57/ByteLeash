#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "qos.h"

#include <format>

namespace byteleash::enforce_test {
namespace {

constexpr wchar_t kPolicy[] = L"'ByteLeash test'";

// A PowerShell single-quoted string literal.
std::wstring quoted(const std::wstring& text) {
    std::wstring out = L"'";
    for (const wchar_t c : text) {
        out += c;
        if (c == L'\'') {
            out += L'\'';
        }
    }
    return out + L"'";
}

// Runs a script (no double quotes) and returns PowerShell's exit code, or -1
// if it couldn't start. Its output goes to this console.
int runPowerShell(const std::wstring& script) {
    wchar_t system_dir[MAX_PATH]{};
    GetSystemDirectoryW(system_dir, MAX_PATH);
    const std::wstring exe =
        std::wstring(system_dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command =
        L"\"" + exe + L"\" -NoProfile -NonInteractive -Command \"" + script + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

}  // namespace

int addQosThrottle(const std::wstring& exe_path, std::uint64_t bits_per_second,
                   const std::wstring& store) {
    return runPowerShell(std::format(
        L"New-NetQosPolicy -Name {} -AppPathNameMatchCondition {} "
        L"-ThrottleRateActionBitsPerSecond {} -PolicyStore {} -ErrorAction Stop "
        L"| Out-Null",
        kPolicy, quoted(exe_path), bits_per_second, store));
}

int removeQosThrottle(const std::wstring& store) {
    const int code = runPowerShell(std::format(
        L"if (Get-NetQosPolicy -Name {0} -PolicyStore {1} -ErrorAction SilentlyContinue) "
        L"{{ Remove-NetQosPolicy -Name {0} -PolicyStore {1} -Confirm:$false "
        L"-ErrorAction Stop; exit 3 }} else {{ exit 0 }}",
        kPolicy, store));
    return code == 3 ? 1 : code == 0 ? 0 : -1;
}

}  // namespace byteleash::enforce_test
