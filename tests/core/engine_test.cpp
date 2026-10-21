#include <doctest/doctest.h>

#include "byteleash/engine.h"
#include "test_time.h"

using namespace byteleash;
using byteleash::test::at;

namespace {

constexpr std::uint64_t kKB = 1024;
constexpr std::uint64_t kHeader = 40;
constexpr std::string_view kApp = R"(C:\Program Files\App\app.exe)";
constexpr std::string_view kOther = "svchost.exe [DiagTrack]";

}  // namespace

TEST_CASE("unknown apps don't exist until they communicate") {
    Engine engine;
    CHECK(engine.find(kApp) == nullptr);
    engine.onInbound(kApp, 100, at(0));
    REQUIRE(engine.find(kApp) != nullptr);
    CHECK(engine.find(kApp)->limits() == Limits{});
}

TEST_CASE("each app has its own bucket") {
    Engine engine;
    for (int i = 0; i < 64; ++i) {
        engine.onOutbound(kApp, 1 * kKB, kHeader, at(0));
    }
    CHECK(engine.onOutbound(kApp, 1, kHeader, at(0)) == Verdict::DropRate);
    CHECK(engine.onOutbound(kOther, 1 * kKB, kHeader, at(0)) == Verdict::Pass);
    CHECK(engine.apps().size() == 2);
}

TEST_CASE("custom limits apply to new and existing apps") {
    Limits unlimited;
    unlimited.unlimited = true;

    Engine engine;
    engine.setCustomLimits(kApp, unlimited, at(0));
    CHECK(engine.hasCustomLimits(kApp));
    engine.onOutbound(kApp, 1, kHeader, at(0));
    CHECK(engine.find(kApp)->status(at(0)) == Status::Unlimited);

    engine.onOutbound(kOther, 1, kHeader, at(0));
    engine.setCustomLimits(kOther, unlimited, at(1));
    CHECK(engine.find(kOther)->status(at(1)) == Status::Unlimited);

    engine.clearCustomLimits(kApp, at(2));
    CHECK_FALSE(engine.hasCustomLimits(kApp));
    CHECK(engine.find(kApp)->limits() == Limits{});
}

TEST_CASE("changing defaults skips apps with custom limits") {
    Limits custom;
    custom.rate_bytes_per_sec = 500;
    Limits defaults;
    defaults.rate_bytes_per_sec = 10;

    Engine engine;
    engine.setCustomLimits(kApp, custom, at(0));
    engine.onOutbound(kApp, 1, kHeader, at(0));
    engine.onOutbound(kOther, 1, kHeader, at(0));

    engine.setDefaultLimits(defaults, at(1));
    CHECK(engine.defaultLimits() == defaults);
    CHECK(engine.find(kApp)->limits() == custom);
    CHECK(engine.find(kOther)->limits() == defaults);
}

TEST_CASE("pause applies to every app") {
    Engine engine;
    engine.setPaused(true);
    CHECK(engine.onOutbound(kApp, 1024 * kKB, kHeader, at(0)) == Verdict::Pass);
    CHECK(engine.find(kApp)->today(at(0)).charged_bytes == 0);

    engine.setPaused(false);
    CHECK(engine.onOutbound(kApp, 1024 * kKB, kHeader, at(1)) == Verdict::DropRate);
}

TEST_CASE("totals sum today's counters across apps") {
    Engine engine;
    engine.onOutbound(kApp, 100, kHeader, at(0));
    engine.onOutbound(kApp, 0, kHeader, at(0));
    engine.onOutbound(kOther, 200, kHeader, at(0));
    engine.onInbound(kOther, 5000, at(0));

    const Counters total = engine.totals(at(1));
    CHECK(total.payload_bytes == 300);
    CHECK(total.data_packets == 2);
    CHECK(total.empty_packets == 1);
    CHECK(total.received_bytes == 5000);

    // Apps that haven't communicated since midnight contribute nothing.
    CHECK(engine.totals(at(2, byteleash::test::kDay2)) == Counters{});
}
