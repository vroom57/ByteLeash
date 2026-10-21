#include "byteleash/app_budget.h"

#include <algorithm>

namespace byteleash {

Counters& Counters::operator+=(const Counters& other) {
    data_packets += other.data_packets;
    payload_bytes += other.payload_bytes;
    data_header_bytes += other.data_header_bytes;
    charged_bytes += other.charged_bytes;
    empty_packets += other.empty_packets;
    empty_header_bytes += other.empty_header_bytes;
    dropped_packets += other.dropped_packets;
    dropped_payload_bytes += other.dropped_payload_bytes;
    received_bytes += other.received_bytes;
    return *this;
}

AppBudget::AppBudget(const Limits& limits, Now now)
    : limits_(limits),
      tokens_(static_cast<double>(limits.burst_bytes)),
      last_refill_(now.instant),
      day_(now.date) {}

Verdict AppBudget::onOutbound(std::uint64_t payload_bytes,
                              std::uint64_t header_bytes, Now now,
                              bool paused) {
    advance(now);

    if (payload_bytes == 0) {
        ++counters_.empty_packets;
        counters_.empty_header_bytes += header_bytes;
        return Verdict::Pass;
    }

    const bool limited = !limits_.unlimited && !paused;
    if (limited) {
        // Cap first: once capped, the app is blocked regardless of the bucket.
        if (counters_.charged_bytes + payload_bytes > effectiveCap()) {
            if (!cap_notice_given_) {
                cap_notice_pending_ = true;
            }
            cap_hit_ = true;
            ++counters_.dropped_packets;
            counters_.dropped_payload_bytes += payload_bytes;
            return Verdict::DropCap;
        }
        const auto cost = static_cast<double>(payload_bytes);
        if (tokens_ < cost) {
            throttled_ = true;
            ++counters_.dropped_packets;
            counters_.dropped_payload_bytes += payload_bytes;
            return Verdict::DropRate;
        }
        tokens_ -= cost;
    }

    ++counters_.data_packets;
    counters_.payload_bytes += payload_bytes;
    counters_.data_header_bytes += header_bytes;
    if (!paused) {
        counters_.charged_bytes += payload_bytes;
    }
    return Verdict::Pass;
}

void AppBudget::onInbound(std::uint64_t bytes, Now now) {
    advance(now);
    counters_.received_bytes += bytes;
}

void AppBudget::setLimits(const Limits& limits, Now now) {
    advance(now);
    const auto old_burst = static_cast<double>(limits_.burst_bytes);
    const auto new_burst = static_cast<double>(limits.burst_bytes);
    if (new_burst > old_burst) {
        tokens_ += new_burst - old_burst;
    }
    tokens_ = std::min(tokens_, new_burst);
    limits_ = limits;
    if (tokens_ >= new_burst) {
        throttled_ = false;
    }
}

void AppBudget::grantExtraToday(std::uint64_t bytes, Now now) {
    advance(now);
    extra_today_ += bytes;
    if (counters_.charged_bytes < effectiveCap()) {
        cap_hit_ = false;
    }
}

bool AppBudget::takeCapNotice() {
    if (!cap_notice_pending_) {
        return false;
    }
    cap_notice_pending_ = false;
    cap_notice_given_ = true;
    return true;
}

Status AppBudget::status(Now now) const {
    if (limits_.unlimited) {
        return Status::Unlimited;
    }
    if (now.date == day_ &&
        (cap_hit_ || counters_.charged_bytes >= effectiveCap())) {
        return Status::Capped;
    }
    if (throttled_ &&
        refilledTokens(now.instant) < static_cast<double>(limits_.burst_bytes)) {
        return Status::Throttled;
    }
    return Status::Bursting;
}

Counters AppBudget::today(Now now) const {
    return now.date == day_ ? counters_ : Counters{};
}

double AppBudget::tokens(Now now) const {
    return refilledTokens(now.instant);
}

void AppBudget::advance(Now now) {
    tokens_ = refilledTokens(now.instant);
    // Ignore a clock that steps backwards rather than losing refill time.
    last_refill_ = std::max(last_refill_, now.instant);
    if (throttled_ && tokens_ >= static_cast<double>(limits_.burst_bytes)) {
        throttled_ = false;
    }

    if (now.date != day_) {
        day_ = now.date;
        counters_ = {};
        extra_today_ = 0;
        cap_hit_ = false;
        cap_notice_pending_ = false;
        cap_notice_given_ = false;
    }
}

double AppBudget::refilledTokens(Instant at) const {
    const auto burst = static_cast<double>(limits_.burst_bytes);
    if (at <= last_refill_) {
        return tokens_;
    }
    const std::chrono::duration<double> elapsed = at - last_refill_;
    return std::min(burst, tokens_ + limits_.rate_bytes_per_sec * elapsed.count());
}

std::uint64_t AppBudget::effectiveCap() const {
    return limits_.daily_cap_bytes + extra_today_;
}

}  // namespace byteleash
