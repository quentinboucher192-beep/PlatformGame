#include "tools/tas/Branch.h"

#include <algorithm>
#include <cmath>

namespace cosmic::tas {

BranchPlan::BranchPlan(std::vector<InputFrame> prefix, size_t through, int sections, double share)
    : prefix_(std::move(prefix)),
      through_(through),
      sections_(sections),
      share_(std::clamp(share, 0.0, 1.0)),
      planner_(std::max(0, sections - static_cast<int>(through))) {}

bool BranchPlan::owns(const std::vector<InputFrame>& run) const {
    return active() && run.size() >= prefix_.size() && std::equal(prefix_.begin(), prefix_.end(), run.begin());
}

bool BranchPlan::roundIsBranch(int round) const {
    if (!active() || round < 1) return false;
    return std::floor(round * share_ + 1e-9) != std::floor((round - 1) * share_ + 1e-9);
}

std::pair<int, int> BranchPlan::window(int round) const {
    if (!active() || planner_.sections() <= 0) return {0, 0};
    const auto [a, b] = planner_.next(round);
    if (a <= 0) return {0, 0};
    return {a + static_cast<int>(through_), b + static_cast<int>(through_)};
}

void BranchPlan::done(int round, int first, int last, int64_t gainMs) {
    if (!active()) return;
    const int t = static_cast<int>(through_);
    if (first <= t) planner_.done(round, 0, 0, gainMs);  // (from the prefix: the whole rest)
    else planner_.done(round, first - t, last - t, gainMs);
}

bool BranchPlan::offer(const std::vector<InputFrame>& run, int64_t ms) {
    if (!owns(run) || ms <= 0 || (bestMs_ > 0 && ms >= bestMs_)) return false;
    best_ = run;
    bestMs_ = ms;
    return true;
}

}  // namespace cosmic::tas
