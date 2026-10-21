#pragma once

#include <cstdint>

namespace byteleash {

// Upload limits for one app. Sizes are bytes; KB and MB are 1024-based.
struct Limits {
    // Token bucket capacity: payload allowed at full speed before throttling.
    std::uint64_t burst_bytes = 64 * 1024;
    // Token bucket refill rate, and the sustained rate once the burst is used.
    double rate_bytes_per_sec = 30.0;
    // Hard limit on charged payload per local day.
    std::uint64_t daily_cap_bytes = 3 * 1024 * 1024;
    // Ignore burst, rate and cap. Traffic is still counted.
    bool unlimited = false;

    friend bool operator==(const Limits&, const Limits&) = default;
};

}  // namespace byteleash
