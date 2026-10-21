#pragma once

#include <chrono>

#include "byteleash/time.h"

namespace byteleash::test {

inline constexpr LocalDate kDay1 = std::chrono::year{2026} / 10 / 21;
inline constexpr LocalDate kDay2 = std::chrono::year{2026} / 10 / 22;

// A point `seconds` after an arbitrary start, on `date`.
inline Now at(double seconds, LocalDate date = kDay1) {
    return {Instant{} + std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>(seconds)),
            date};
}

}  // namespace byteleash::test
