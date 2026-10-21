#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace byteleash {

// App identity rules (provisional; see "App identity" in docs/ideas.md).
// Strings are UTF-8.

// Replaces version numbers in an exe path's folder names with "*", so an app
// that updates into a new version folder keeps the same identity:
//   C:\...\Discord\app-1.0.9031\Discord.exe -> C:\...\Discord\app-*\Discord.exe
// A version number is two or more digit groups joined by dots (1.2, 120.0.1).
// The file name itself is left alone. Accepts '\' and '/' separators.
std::string wildcardVersionFolders(std::string_view path);

// Identity for a shared service host, e.g. "svchost.exe [BITS, Dnscache]".
// Service names are sorted so the identity doesn't depend on start order.
std::string serviceHostIdentity(std::string_view exe_name,
                                std::vector<std::string> services);

}  // namespace byteleash
