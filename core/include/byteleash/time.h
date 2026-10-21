#pragma once

#include <chrono>

namespace byteleash {

using Clock = std::chrono::steady_clock;
using Instant = Clock::time_point;

// The PC's local calendar date. Daily usage resets when it changes.
using LocalDate = std::chrono::year_month_day;

// The current time as core sees it. The platform layer supplies both parts:
// a monotonic instant for the token bucket (immune to clock changes) and the
// local date for the daily reset at local midnight.
struct Now {
    Instant instant;
    LocalDate date;
};

}  // namespace byteleash
