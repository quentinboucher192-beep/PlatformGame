#include "tools/tas/Portfolio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "game/tas/TasProgress.h"
#include "tools/tas/Course.h"
#include "tools/tas/Explore.h"
#include "tools/tas/Sim.h"
#include "tools/tas/Timing.h"

namespace cosmic::tas {

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The prior of every arm: as if it had gained kPriorMs in kPriorCpu CPU-seconds (an arm not tried
// yet looks promising), gains counted in events of kEventMs.
constexpr double kPriorCpu = 30.0, kEventMs = 10.0, kPriorEvents = 1.0;

}  // namespace

std::string strategyLabel(const std::string& id) { return tasStrategyLabel(id); }

// ------------------------------------------------------------------ the bandit

Bandit::Bandit(uint64_t seed, double floor, double discount) : rng_(seed * 0x9e3779b97f4a7c15ull + 99u), floor_(floor), discount_(discount) {}

int Bandit::add(const std::string& id) {
    Arm a;
    a.id = id;
    arms_.push_back(a);
    return static_cast<int>(arms_.size()) - 1;
}

int Bandit::find(const std::string& id) const {
    for (size_t i = 0; i < arms_.size(); ++i)
        if (arms_[i].id == id) return static_cast<int>(i);
    return -1;
}

double Bandit::share(int arm) const {
    double total = 0.0;
    for (const Arm& a : arms_) total += a.cpu;
    return total > 0.0 ? arms_[static_cast<size_t>(arm)].cpu / total : 0.0;
}

double Bandit::gamma(double shape) {
    if (shape < 1.0) {
        const double u = std::max(1e-12, static_cast<double>(rng_.unit()));
        return gamma(shape + 1.0) * std::pow(u, 1.0 / shape);
    }
    // Marsaglia and Tsang.
    const double d = shape - 1.0 / 3.0, c = 1.0 / std::sqrt(9.0 * d);
    for (;;) {
        double x, v;
        do {
            const double u1 = std::max(1e-12, static_cast<double>(rng_.unit())), u2 = static_cast<double>(rng_.unit());
            x = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
            v = 1.0 + c * x;
        } while (v <= 0.0);
        v = v * v * v;
        const double u = std::max(1e-12, static_cast<double>(rng_.unit()));
        if (std::log(u) < 0.5 * x * x + d - d * v + d * std::log(v)) return d * v;
    }
}

int Bandit::pick() {
    int enabled = 0;
    double total = 0.0;
    for (const Arm& a : arms_)
        if (a.enabled) ++enabled, total += a.cpu;
    if (enabled == 0) return -1;
    // The floor: an arm under its share of the time first (the least served of them).
    const double floor = std::min(floor_, 0.5 / enabled);
    int starving = -1;
    double least = 1e300;
    for (size_t i = 0; i < arms_.size(); ++i) {
        const Arm& a = arms_[i];
        if (!a.enabled) continue;
        const double s = total > 0.0 ? a.cpu / total : 0.0;
        const double f = a.floor >= 0.0 ? a.floor : floor;
        if ((a.pulls == 0 || s < f) && s / std::max(1e-9, f) < least) least = s / std::max(1e-9, f), starving = static_cast<int>(i);
    }
    if (starving >= 0) return starving;
    // Thompson sampling: each arm's rate of gain drawn from its posterior, the largest wins.
    int best = -1;
    double top = -1.0;
    for (size_t i = 0; i < arms_.size(); ++i) {
        const Arm& a = arms_[i];
        if (!a.enabled) continue;
        const double rate = gamma(kPriorEvents + a.recentGain / kEventMs) / (kPriorCpu + a.recentCpu);
        if (rate > top) top = rate, best = static_cast<int>(i);
    }
    return best;
}

void Bandit::record(int arm, double cpuSeconds, double gainMs) {
    if (arm < 0 || arm >= static_cast<int>(arms_.size())) return;
    for (Arm& a : arms_) {
        a.recentCpu *= discount_;
        a.recentGain *= discount_;
    }
    Arm& a = arms_[static_cast<size_t>(arm)];
    a.cpu += cpuSeconds;
    a.recentCpu += cpuSeconds;
    a.gainMs += std::max(0.0, gainMs);
    a.recentGain += std::max(0.0, gainMs);
    ++a.pulls;
    if (gainMs > 0.0) ++a.wins;
}

// ------------------------------------------------------------------ the portfolio

Portfolio::Portfolio(const ContentDatabase& content, const LevelData& level, RunCategory category, int threads, uint64_t seed)
    : content_(content), level_(level), category_(category), bandit_(seed) {
    course_ = std::make_unique<Course>(level, content, category);
    explore_ = std::make_unique<GoExplore>();
    timing_ = std::make_unique<TimingPolish>();
    for (const char* id : {kBeam, kExplore, kTiming, kRefine}) {
        bandit_.add(id);
        extra_.emplace_back();
    }
    // The exploration keeps a fifth of the time whatever the others gain (the routes beyond those
    // known — a player's included — are what it is for, and its gains come rarely).
    bandit_.setFloor(bandit_.find(kExplore), 0.2);
    (void)threads;
    began_ = nowMs();
}

Portfolio::~Portfolio() = default;

GoExplore& Portfolio::explore() { return *explore_; }
TimingPolish& Portfolio::timing() { return *timing_; }

int Portfolio::addArm(const std::string& id, std::function<void(const Slice&)> run) {
    const int k = bandit_.add(id);
    extra_.push_back(std::move(run));
    return k;
}

void Portfolio::only(const std::vector<std::string>& ids) { only_ = ids; }

int Portfolio::pick(bool beamPart) {
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < bandit_.arms().size(); ++i) {
        const std::string& a = bandit_.arms()[i].id;
        const bool allowed = only_.empty() || std::find(only_.begin(), only_.end(), a) != only_.end();
        bandit_.enable(static_cast<int>(i), allowed && a != (beamPart ? kRefine : kBeam));
    }
    return bandit_.pick();
}

std::vector<Sim*> Portfolio::sims(int n) {
    while (static_cast<int>(sims_.size()) < n) sims_.push_back(std::make_unique<Sim>(content_, level_, *course_, category_));
    std::vector<Sim*> out;
    for (int i = 0; i < n; ++i) out.push_back(sims_[static_cast<size_t>(i)].get());
    return out;
}

bool Portfolio::runSlice(int arm, const Slice& slice) {
    const std::string& id = bandit_.arms()[static_cast<size_t>(arm)].id;
    if (id == kExplore) {
        explore_->run(sims(slice.threads), slice);
        return true;
    }
    if (id == kTiming) {
        timing_->run(sims(slice.threads), slice);
        return true;
    }
    if (static_cast<size_t>(arm) < extra_.size() && extra_[static_cast<size_t>(arm)]) {
        extra_[static_cast<size_t>(arm)](slice);
        return true;
    }
    return false;
}

void Portfolio::record(int arm, double cpuSeconds, float before, float after) {
    std::lock_guard<std::mutex> l(m_);
    bandit_.record(arm, cpuSeconds, before > 0.0f && after > 0.0f ? std::max(0.0, 1000.0 * static_cast<double>(before - after)) : 0.0);
}

void Portfolio::credit(const std::string& arm, float from, float to) {
    std::lock_guard<std::mutex> l(m_);
    gains_.push_back({nowMs() - began_, arm, from, to});
}

std::vector<FoundGain> Portfolio::gains() const {
    std::lock_guard<std::mutex> l(m_);
    return gains_;
}

std::string Portfolio::summary() const {
    std::lock_guard<std::mutex> l(m_);
    std::string out;
    for (size_t i = 0; i < bandit_.arms().size(); ++i) {
        const Bandit::Arm& a = bandit_.arms()[i];
        if (a.pulls == 0) continue;
        char b[160];
        std::snprintf(b, sizeof b, "%s%s %.0f %% du temps, %d tranche(s), %.0f ms gagnées", out.empty() ? "" : " · ", strategyLabel(a.id).c_str(),
                      100.0 * bandit_.share(static_cast<int>(i)), a.pulls, a.gainMs);
        out += b;
    }
    return out;
}

}  // namespace cosmic::tas
