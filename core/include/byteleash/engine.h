#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "byteleash/app_budget.h"
#include "byteleash/limits.h"
#include "byteleash/time.h"

namespace byteleash {

enum class Mode {
    // Default on first run: nothing is limited; every app's budget runs as a
    // dry run to show what Leash mode would have done.
    Learning,
    // Limits are enforced. Unleashed apps have unlimited custom limits.
    Leash,
};

// Tracks every app's budget. Apps are keyed by an opaque identity string
// (UTF-8) chosen by the platform layer, e.g. an exe path or
// "svchost.exe [DiagTrack]". An app is created on its first packet with its
// custom limits if it has them, otherwise the defaults.
//
// Not thread-safe; the caller serializes access.
class Engine {
public:
    using AppMap = std::map<std::string, AppBudget, std::less<>>;

    explicit Engine(const Limits& defaults = {}, Mode mode = Mode::Learning);

    Verdict onOutbound(std::string_view app, std::uint64_t payload_bytes,
                       std::uint64_t header_bytes, Now now);
    void onInbound(std::string_view app, std::uint64_t bytes, Now now);

    // Applies to every app without custom limits.
    void setDefaultLimits(const Limits& limits, Now now);
    const Limits& defaultLimits() const { return defaults_; }

    void setCustomLimits(std::string_view app, const Limits& limits, Now now);
    // Reverts the app to the defaults.
    void clearCustomLimits(std::string_view app, Now now);
    bool hasCustomLimits(std::string_view app) const;

    // Entering learning mode resets every app's dry-run counters.
    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    // While paused, traffic is counted but not charged to any app, in either
    // mode. Pause takes precedence over the learning-mode dry run.
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // nullptr if the app hasn't sent or received anything yet.
    AppBudget* find(std::string_view app);
    const AppBudget* find(std::string_view app) const;
    const AppMap& apps() const { return apps_; }

    // Today's counters summed across all apps.
    Counters totals(Now now) const;

private:
    AppBudget& budgetFor(std::string_view app, Now now);
    const Limits& limitsFor(std::string_view app) const;
    Enforcement enforcement() const;

    Limits defaults_;
    Mode mode_;
    std::map<std::string, Limits, std::less<>> custom_;
    AppMap apps_;
    bool paused_ = false;
};

}  // namespace byteleash
