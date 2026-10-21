#include <doctest/doctest.h>

#include "byteleash/app_budget.h"
#include "test_time.h"

using namespace byteleash;
using byteleash::test::at;
using byteleash::test::kDay1;
using byteleash::test::kDay2;

namespace {

constexpr std::uint64_t kKB = 1024;
constexpr std::uint64_t kMB = 1024 * 1024;
constexpr std::uint64_t kHeader = 40;

// Sends `count` packets of `payload` bytes at `now`; returns how many passed.
int sendMany(AppBudget& budget, int count, std::uint64_t payload, Now now) {
    int passed = 0;
    for (int i = 0; i < count; ++i) {
        if (budget.onOutbound(payload, kHeader, now) == Verdict::Pass) {
            ++passed;
        }
    }
    return passed;
}

// Burst is ample and refills almost instantly, so only the cap matters.
Limits capOnly(std::uint64_t cap) {
    Limits limits;
    limits.burst_bytes = 1 * kMB;
    limits.rate_bytes_per_sec = 1e9;
    limits.daily_cap_bytes = cap;
    return limits;
}

}  // namespace

TEST_CASE("defaults match docs/behavior.md") {
    const Limits limits;
    CHECK(limits.burst_bytes == 64 * kKB);
    CHECK(limits.rate_bytes_per_sec == 30.0);
    CHECK(limits.daily_cap_bytes == 3 * kMB);
    CHECK_FALSE(limits.unlimited);
}

TEST_CASE("bucket starts full and allows exactly the burst") {
    AppBudget budget({}, at(0));
    CHECK(budget.status(at(0)) == Status::Bursting);
    CHECK(sendMany(budget, 64, 1 * kKB, at(0)) == 64);
    CHECK(budget.onOutbound(1, kHeader, at(0)) == Verdict::DropRate);
    CHECK(budget.status(at(0)) == Status::Throttled);
}

TEST_CASE("empty packets always pass, are counted and never charged") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));  // Drain the bucket.
    CHECK(budget.onOutbound(0, kHeader, at(0)) == Verdict::Pass);
    CHECK(budget.onOutbound(0, kHeader, at(0)) == Verdict::Pass);

    const Counters c = budget.today(at(0));
    CHECK(c.empty_packets == 2);
    CHECK(c.empty_header_bytes == 2 * kHeader);
    CHECK(c.charged_bytes == 64 * kKB);
    CHECK(budget.tokens(at(0)) == 0.0);
}

TEST_CASE("dropped packets aren't charged") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));
    CHECK(budget.onOutbound(500, kHeader, at(0)) == Verdict::DropRate);

    const Counters c = budget.today(at(0));
    CHECK(c.charged_bytes == 64 * kKB);
    CHECK(c.payload_bytes == 64 * kKB);
    CHECK(c.data_packets == 64);
    CHECK(c.data_header_bytes == 64 * kHeader);
    CHECK(c.dropped_packets == 1);
    CHECK(c.dropped_payload_bytes == 500);
}

TEST_CASE("bucket refills at the throttled rate") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));

    // 1024 B at 30 B/s needs 34.13 s.
    CHECK(budget.onOutbound(1 * kKB, kHeader, at(34)) == Verdict::DropRate);
    CHECK(budget.onOutbound(1 * kKB, kHeader, at(35)) == Verdict::Pass);
    CHECK(budget.tokens(at(35)) == doctest::Approx(1050.0 - 1024.0));
}

TEST_CASE("refill stops at the burst size") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));
    CHECK(budget.tokens(at(86400)) == 64.0 * kKB);
    CHECK(sendMany(budget, 65, 1 * kKB, at(86400)) == 64);
}

TEST_CASE("throttled until the burst fully refills") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));
    budget.onOutbound(1, kHeader, at(0));  // Dropped: now throttled.

    // 64 KB at 30 B/s takes ~2184.5 s (~36 min).
    CHECK(budget.status(at(2184)) == Status::Throttled);
    CHECK(budget.status(at(2185)) == Status::Bursting);
}

TEST_CASE("using burst without hitting the limit stays Bursting") {
    AppBudget budget({}, at(0));
    sendMany(budget, 60, 1 * kKB, at(0));
    CHECK(budget.status(at(0)) == Status::Bursting);
}

TEST_CASE("packets larger than the burst never pass") {
    AppBudget budget({}, at(0));
    CHECK(budget.onOutbound(64 * kKB + 1, kHeader, at(0)) == Verdict::DropRate);
    CHECK(budget.onOutbound(64 * kKB + 1, kHeader, at(86400)) == Verdict::DropRate);
}

TEST_CASE("daily cap blocks payload and raises one notice") {
    AppBudget budget(capOnly(3 * kMB), at(0));
    int passed = 0;
    for (int i = 0; i < 48; ++i) {  // 48 x 64 KB = 3 MB.
        if (budget.onOutbound(64 * kKB, kHeader, at(i * 0.01)) == Verdict::Pass) {
            ++passed;
        }
    }
    CHECK(passed == 48);
    CHECK(budget.status(at(1)) == Status::Capped);
    CHECK_FALSE(budget.takeCapNotice());  // Reached, not yet exceeded.

    CHECK(budget.onOutbound(1, kHeader, at(1)) == Verdict::DropCap);
    CHECK(budget.onOutbound(0, kHeader, at(1)) == Verdict::Pass);
    CHECK(budget.takeCapNotice());
    CHECK_FALSE(budget.takeCapNotice());

    CHECK(budget.onOutbound(1, kHeader, at(2)) == Verdict::DropCap);
    CHECK_FALSE(budget.takeCapNotice());  // Once per day.
    CHECK(budget.today(at(2)).charged_bytes == 3 * kMB);
}

TEST_CASE("cap resets at local midnight, the bucket doesn't") {
    Limits limits;
    limits.daily_cap_bytes = 32 * kKB;
    AppBudget budget(limits, at(0));
    CHECK(sendMany(budget, 33, 1 * kKB, at(0)) == 32);
    CHECK(budget.takeCapNotice());

    // Midnight: usage and notice reset; the bucket keeps its level (32 KB).
    const Now next = at(1, kDay2);
    CHECK(budget.status(next) == Status::Bursting);
    CHECK(budget.today(next) == Counters{});
    CHECK(budget.tokens(next) == doctest::Approx(32.0 * kKB + 30.0));
    CHECK(sendMany(budget, 33, 1 * kKB, next) == 32);
    CHECK(budget.today(next).charged_bytes == 32 * kKB);
    CHECK(budget.takeCapNotice());  // New day, new notice.
}

TEST_CASE("cap reached exactly then exceeded on a new day") {
    AppBudget budget(capOnly(64 * kKB), at(0));
    CHECK(budget.onOutbound(64 * kKB, kHeader, at(0)) == Verdict::Pass);
    CHECK(budget.onOutbound(1, kHeader, at(1)) == Verdict::DropCap);
    CHECK(budget.onOutbound(1, kHeader, at(2, kDay2)) == Verdict::Pass);
}

TEST_CASE("give it more today lifts the cap until midnight") {
    AppBudget budget(capOnly(64 * kKB), at(0));
    budget.onOutbound(64 * kKB, kHeader, at(0));
    CHECK(budget.onOutbound(1 * kKB, kHeader, at(1)) == Verdict::DropCap);
    CHECK(budget.takeCapNotice());

    budget.grantExtraToday(1 * kMB, at(2));
    CHECK(budget.status(at(2)) == Status::Bursting);
    CHECK(budget.onOutbound(1 * kKB, kHeader, at(3)) == Verdict::Pass);

    // Hitting the raised cap doesn't notify again the same day.
    CHECK(budget.onOutbound(1 * kMB - 1 * kKB, kHeader, at(4)) == Verdict::Pass);
    CHECK(budget.onOutbound(1, kHeader, at(5)) == Verdict::DropCap);
    CHECK_FALSE(budget.takeCapNotice());

    // The grant expires at midnight.
    budget.onOutbound(64 * kKB, kHeader, at(6, kDay2));
    CHECK(budget.onOutbound(1, kHeader, at(7, kDay2)) == Verdict::DropCap);
}

TEST_CASE("paused: counted, not charged, bucket untouched") {
    AppBudget budget({}, at(0));
    CHECK(budget.onOutbound(1 * kMB, kHeader, at(0), true) == Verdict::Pass);

    const Counters c = budget.today(at(0));
    CHECK(c.payload_bytes == 1 * kMB);
    CHECK(c.data_packets == 1);
    CHECK(c.charged_bytes == 0);
    CHECK(budget.tokens(at(0)) == 64.0 * kKB);
}

TEST_CASE("unlimited: everything passes and is counted") {
    Limits limits;
    limits.unlimited = true;
    AppBudget budget(limits, at(0));
    CHECK(sendMany(budget, 10, 1 * kMB, at(0)) == 10);
    CHECK(budget.status(at(0)) == Status::Unlimited);
    CHECK(budget.today(at(0)).charged_bytes == 10 * kMB);
}

TEST_CASE("raising the burst tops up the bucket, lowering trims it") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));

    Limits bigger;
    bigger.burst_bytes = 256 * kKB;
    budget.setLimits(bigger, at(0));
    CHECK(budget.tokens(at(0)) == 192.0 * kKB);

    Limits smaller;
    smaller.burst_bytes = 16 * kKB;
    budget.setLimits(smaller, at(0));
    CHECK(budget.tokens(at(0)) == 16.0 * kKB);
    CHECK(budget.limits() == smaller);
}

TEST_CASE("raising the rate speeds up refill") {
    AppBudget budget({}, at(0));
    sendMany(budget, 64, 1 * kKB, at(0));
    budget.onOutbound(1, kHeader, at(0));

    Limits faster;
    faster.rate_bytes_per_sec = 1024;
    budget.setLimits(faster, at(0));
    CHECK(budget.onOutbound(1 * kKB, kHeader, at(1)) == Verdict::Pass);
}

TEST_CASE("a clock stepping backwards adds no tokens") {
    AppBudget budget({}, at(100));
    sendMany(budget, 64, 1 * kKB, at(100));
    CHECK(budget.onOutbound(1, kHeader, at(50)) == Verdict::DropRate);
    CHECK(budget.tokens(at(50)) == 0.0);
    // Refill resumes from the latest time seen, not the earlier one.
    CHECK(budget.tokens(at(110)) == doctest::Approx(300.0));
}

TEST_CASE("inbound bytes are counted, and daily counters go stale at midnight") {
    AppBudget budget({}, at(0));
    budget.onInbound(5 * kMB, at(0));
    CHECK(budget.today(at(0)).received_bytes == 5 * kMB);
    CHECK(budget.today(at(0, kDay2)).received_bytes == 0);
}
