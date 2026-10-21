#include <doctest/doctest.h>

#include "byteleash/app_identity.h"

using namespace byteleash;

TEST_CASE("version folders become wildcards") {
    CHECK(wildcardVersionFolders(
              R"(C:\Users\me\AppData\Local\Discord\app-1.0.9031\Discord.exe)") ==
          R"(C:\Users\me\AppData\Local\Discord\app-*\Discord.exe)");
    CHECK(wildcardVersionFolders(
              R"(C:\Program Files\Google\Chrome\Application\120.0.6099.71\elevation.exe)") ==
          R"(C:\Program Files\Google\Chrome\Application\*\elevation.exe)");
    CHECK(wildcardVersionFolders("/opt/app/2.5/bin/app") == "/opt/app/*/bin/app");
}

TEST_CASE("single numbers and the file name are left alone") {
    // "18" and "Python311" have no dotted version.
    CHECK(wildcardVersionFolders(
              R"(C:\Program Files\Microsoft Visual Studio\18\Community\x.exe)") ==
          R"(C:\Program Files\Microsoft Visual Studio\18\Community\x.exe)");
    CHECK(wildcardVersionFolders(R"(C:\Python311\python.exe)") ==
          R"(C:\Python311\python.exe)");
    CHECK(wildcardVersionFolders(R"(C:\Tools\setup-1.2.3.exe)") ==
          R"(C:\Tools\setup-1.2.3.exe)");
}

TEST_CASE("dots without digits on both sides aren't versions") {
    CHECK(wildcardVersionFolders(R"(C:\a\v1.\b\x.exe)") == R"(C:\a\v1.\b\x.exe)");
    CHECK(wildcardVersionFolders(R"(C:\a\.5\x.exe)") == R"(C:\a\.5\x.exe)");
    CHECK(wildcardVersionFolders(R"(C:\a\1.2.x\x.exe)") == R"(C:\a\*.x\x.exe)");
}

TEST_CASE("paths without folders are returned unchanged") {
    CHECK(wildcardVersionFolders("System") == "System");
    CHECK(wildcardVersionFolders("") == "");
}

TEST_CASE("service host identity lists sorted service names") {
    CHECK(serviceHostIdentity("svchost.exe", {"Dnscache"}) ==
          "svchost.exe [Dnscache]");
    CHECK(serviceHostIdentity("svchost.exe", {"NlaSvc", "BITS", "Dnscache"}) ==
          "svchost.exe [BITS, Dnscache, NlaSvc]");
}
