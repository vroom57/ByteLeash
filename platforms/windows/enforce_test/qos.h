// Windows QoS throttle policies for the enforcement test, applied through
// PowerShell's NetQos cmdlets. The policy is named "ByteLeash test". Unlike
// the WFP filters, it isn't tied to this process: if the test is killed, the
// policy stays until `cleanup` (or, in ActiveStore, a reboot).

#pragma once

#include <cstdint>
#include <string>

namespace byteleash::enforce_test {

// Throttles the app at exe_path. store is "ActiveStore" (not persistent) or
// "localhost" (persistent). Returns PowerShell's exit code; 0 means added.
int addQosThrottle(const std::wstring& exe_path, std::uint64_t bits_per_second,
                   const std::wstring& store);

// Returns 1 if a policy was removed, 0 if there was none, -1 on error.
int removeQosThrottle(const std::wstring& store);

}  // namespace byteleash::enforce_test
