#pragma once

// The branch of a training (docs/TAS.md « La branche »): it COMMITS to an early prefix (a run's
// first sections, an arrival seconds ahead of the best run) and trains the rest for a long time,
// windows and all, from its OWN best run — kept even when it is slower than the best run (its
// later sections may still be the fastest ever played from that prefix). Its runs are given back
// to the best run's rounds (a guide, the phase splice); a branch run faster than the best run is
// the best run. Generic: nothing here knows a level.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "tools/tas/Search.h"

namespace cosmic::tas {

class BranchPlan {
public:
    BranchPlan() = default;
    /// `prefix`: the frames from the load through the end of section `through` (1-based count),
    /// kept exactly; `sections`: the level's; `share`: the rounds it gets (0..1).
    BranchPlan(std::vector<InputFrame> prefix, size_t through, int sections, double share);

    bool active() const { return !prefix_.empty() && through_ > 0 && static_cast<int>(through_) < sections_; }
    const std::vector<InputFrame>& prefix() const { return prefix_; }
    size_t through() const { return through_; }
    /// The run starts with the prefix (a run of the branch).
    bool owns(const std::vector<InputFrame>& run) const;
    /// Round `round` (1-based) is the branch's: `share` of the rounds, spread evenly.
    bool roundIsBranch(int round) const;
    /// The window of a branch round (1-based sections of the whole level, both after the prefix);
    /// they rotate over the sections after it (WindowPlanner).
    std::pair<int, int> window(int round) const;
    /// What a branch round did: its window (as window() gave it) and the time it gained (ms).
    void done(int round, int first, int last, int64_t gainMs);
    /// A run of the branch, verified, at `ms`: kept as the branch's best when faster than its own
    /// best (whatever the best run's time). False: not a run of the branch, or not faster.
    bool offer(const std::vector<InputFrame>& run, int64_t ms);
    const std::vector<InputFrame>& best() const { return best_; }
    int64_t bestMs() const { return bestMs_; }

private:
    std::vector<InputFrame> prefix_;
    size_t through_ = 0;
    int sections_ = 0;
    double share_ = 0.5;
    WindowPlanner planner_;
    std::vector<InputFrame> best_;
    int64_t bestMs_ = 0;
};

}  // namespace cosmic::tas
