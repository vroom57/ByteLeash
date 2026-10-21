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

// An app's current state, as shown in the tray.
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
    // means an empty packet. While paused, packets pass and are counted but
    // aren't charged and don't drain the bucket.
    Verdict onOutbound(std::uint64_t payload_bytes, std::uint64_t header_bytes,
                       Now now, bool paused = false);

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

private:
    void advance(Now now);
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
};

}  // namespace byteleash
