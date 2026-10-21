#include "byteleash/engine.h"

namespace byteleash {

Engine::Engine(const Limits& defaults, Mode mode)
    : defaults_(defaults), mode_(mode) {}

Verdict Engine::onOutbound(std::string_view app, std::uint64_t payload_bytes,
                           std::uint64_t header_bytes, Now now) {
    return budgetFor(app, now).onOutbound(payload_bytes, header_bytes, now,
                                          enforcement());
}

void Engine::setMode(Mode mode) {
    if (mode == Mode::Learning && mode_ != Mode::Learning) {
        for (auto& [name, budget] : apps_) {
            budget.resetDryRun();
        }
    }
    mode_ = mode;
}

void Engine::onInbound(std::string_view app, std::uint64_t bytes, Now now) {
    budgetFor(app, now).onInbound(bytes, now);
}

void Engine::setDefaultLimits(const Limits& limits, Now now) {
    defaults_ = limits;
    for (auto& [name, budget] : apps_) {
        if (!hasCustomLimits(name)) {
            budget.setLimits(limits, now);
        }
    }
}

void Engine::setCustomLimits(std::string_view app, const Limits& limits,
                             Now now) {
    if (auto it = custom_.find(app); it != custom_.end()) {
        it->second = limits;
    } else {
        custom_.emplace(std::string(app), limits);
    }
    if (auto* budget = find(app)) {
        budget->setLimits(limits, now);
    }
}

void Engine::clearCustomLimits(std::string_view app, Now now) {
    if (auto it = custom_.find(app); it != custom_.end()) {
        custom_.erase(it);
    }
    if (auto* budget = find(app)) {
        budget->setLimits(defaults_, now);
    }
}

bool Engine::hasCustomLimits(std::string_view app) const {
    return custom_.find(app) != custom_.end();
}

AppBudget* Engine::find(std::string_view app) {
    auto it = apps_.find(app);
    return it == apps_.end() ? nullptr : &it->second;
}

const AppBudget* Engine::find(std::string_view app) const {
    auto it = apps_.find(app);
    return it == apps_.end() ? nullptr : &it->second;
}

Counters Engine::totals(Now now) const {
    Counters sum;
    for (const auto& [name, budget] : apps_) {
        sum += budget.today(now);
    }
    return sum;
}

AppBudget& Engine::budgetFor(std::string_view app, Now now) {
    if (auto it = apps_.find(app); it != apps_.end()) {
        return it->second;
    }
    return apps_.emplace(std::string(app), AppBudget(limitsFor(app), now))
        .first->second;
}

const Limits& Engine::limitsFor(std::string_view app) const {
    auto it = custom_.find(app);
    return it == custom_.end() ? defaults_ : it->second;
}

Enforcement Engine::enforcement() const {
    if (paused_) {
        return Enforcement::Paused;
    }
    return mode_ == Mode::Learning ? Enforcement::DryRun : Enforcement::On;
}

}  // namespace byteleash
