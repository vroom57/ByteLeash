#pragma once

#include <cstdint>

#include "byteleash/limits.h"
#include "byteleash/time.h"

namespace byteleash {

// What to do with one outbound packet.
enum class Verdict {
    Pass,
    DropRate,  // Not enough burst/rate budget.
    DropCap,   // Daily cap reached.
};

// How a packet's verdict is applied.
enum class Enforcement {
    On,      // Leash mode: over-budget packets are dropped.
    DryRun,  // Learning mode: every packet passes; would-be drops are counted.
    Paused,  // Protection paused: every packet passes and nothing is charged.
};

// An app's current state, as shown in the tray. In learning mode it shows the
// simulated state, i.e. what Leash mode would be doing.
enum class Status {
    Bursting,   // Sending within its burst budget.
    Throttled,  // Burst used up; held to its rate until the burst refills.
    Capped,     // Hit its daily cap; uploads blocked until local midnight.
    Unlimited,  // Limits are off for this app.
};

// Traffic counters for one local day.
struct Counters {
    std::uint64_t data_packets = 0;       // Passed packets carrying payload.
    std::uint64_t payload_bytes = 0;      // Payload of passed packets.
    std::uint64_t data_header_bytes = 0;  // Header bytes of passed data packets.
    std::uint64_t charged_bytes = 0;      // Payload counted toward the daily cap.
    std::uint64_t empty_packets = 0;      // Pure ACKs, SYN, FIN, RST, keepalives.
    std::uint64_t empty_header_bytes = 0;
    std::uint64_t dropped_packets = 0;
    std::uint64_t dropped_payload_bytes = 0;
    std::uint64_t received_bytes = 0;

    Counters& operator+=(const Counters& other);
    friend bool operator==(const Counters&, const Counters&) = default;
};

// What Leash mode would have done, counted during learning mode. Not reset at
// midnight; reset when learning mode starts.
struct DryRunCounters {
    std::uint64_t throttle_events = 0;  // Transitions into Throttled.
    std::uint64_t cap_days = 0;         // Days the daily cap would have been hit.

    friend bool operator==(const DryRunCounters&, const DryRunCounters&) = default;
};

// Upload budget for one app: a token bucket (capacity = burst, refill = rate)
// shared by all of the app's connections, plus a daily cap on charged payload.
//
// Only payload is charged. Empty packets always pass and are counted
// separately. Dropped packets aren't charged. See docs/behavior.md.
//
// Not thread-safe; the caller serializes access.
class AppBudget {
public:
    // The bucket starts full.
    AppBudget(const Limits& limits, Now now);

    // Decides one outbound packet and updates the counters. payload_bytes == 0
    // means an empty packet.
    //
    // DryRun runs the budget exactly as On would (a would-be-dropped packet
    // doesn't drain the bucket or count toward the cap), but lets every
    // packet through and records would-be drops in dryRun() instead of the
    // dropped counters. No cap notice is raised.
    //
    // Paused: packets pass and are counted but aren't charged and don't
    // drain the bucket.
    Verdict onOutbound(std::uint64_t payload_bytes, std::uint64_t header_bytes,
                       Now now, Enforcement enforcement = Enforcement::On);

    // Counts inbound bytes. Downloads are never limited.
    void onInbound(std::uint64_t bytes, Now now);

    // Changes the limits, keeping today's usage. Raising the burst adds the
    // difference to the bucket right away; lowering it trims the bucket.
    void setLimits(const Limits& limits, Now now);

    // "Give it more today": raises today's cap by `bytes` until local midnight.
    void grantExtraToday(std::uint64_t bytes, Now now);

    // True once per app per day, the first time it hits its daily cap.
    // Clears on read.
    bool takeCapNotice();

    const Limits& limits() const { return limits_; }
    Status status(Now now) const;
    Counters today(Now now) const;
    // Current bucket level in bytes (may be fractional).
    double tokens(Now now) const;

    const DryRunCounters& dryRun() const { return dry_run_; }
    void resetDryRun() { dry_run_ = {}; }

private:
    void advance(Now now);
    // Charges the bucket and cap if the packet fits; otherwise records the
    // would-be drop (cap notice when enforcing, dry-run counters otherwise).
    Verdict take(std::uint64_t payload_bytes, Enforcement enforcement);
    double refilledTokens(Instant at) const;
    std::uint64_t effectiveCap() const;

    Limits limits_;
    double tokens_;
    Instant last_refill_;
    bool throttled_ = false;

    LocalDate day_;
    Counters counters_;
    std::uint64_t extra_today_ = 0;
    bool cap_hit_ = false;
    bool cap_notice_pending_ = false;
    bool cap_notice_given_ = false;

    DryRunCounters dry_run_;
};

}  // namespace byteleash
