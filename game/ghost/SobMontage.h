#pragma once

#include <string>
#include <vector>

#include "game/gameplay/BossTraining.h"
#include "game/ghost/PersonalBests.h"

namespace cosmic {

// « Regarder » (section training, 2.60.3; 2.91: from the ONE personal Sum of Best): the best of
// each segment of a range of the route watched one after the other, each played from the exact
// replay of the attempt that did it (ReplayPlayback: the real simulation of that attempt, from the
// step it reached the segment's start to the step it reached its end) — a full run, a section
// training, an enchaînement, an attempt from a StartPos, a verified online run. Display only:
// nothing here is ranked, sent, nor changes a record or the simulation. Never two versions of a
// level: everything comes from one GhostStore::levelKey.

/// A full run of this level version whose exact replay is on this computer (the record, the
/// previous one, the last run): a best of before 2.60.3 without a replay may be one of its segments.
struct MontageRun {
    std::string file;  // its replay (UTF-8 path), empty: none
    std::string date;
    std::vector<float> splits;  // cumulative seconds at each checkpoint
    float time = 0.0f;          // final time (<= 0: no run)
    std::vector<std::string> checkpoints;  // ids of its splits' checkpoints, empty: the route's when as many
};

/// One segment of the montage.
struct MontageSegment {
    int index = 0;            // its place in the route (0: from the start)
    std::string from, to;     // its ends (checkpoint ids; "" = the start, the exit)
    float duration = -1.0f;   // its best duration: its part of the Sum of Best (-1: unknown)
    BestSource source = BestSource::Run;  // where that best was done
    bool training = false;    // its replay starts as its attempt did (`trainingStart`), not at the level's start
    std::string date;         // of that attempt
    std::string file;         // the replay that holds it (UTF-8 path), empty: none on this computer
    float start = 0.0f;       // the segment on that replay's chronometer
    float end = 0.0f;
    SectionTrainingStart trainingStart;
    /// Found in one of the player's kept full runs with exactly the best time: PersonalBests::
    /// adoptReplay keeps a copy of it as that best's replay.
    bool adopt = false;
    bool hasReplay() const { return !file.empty(); }
};

struct MontagePlan {
    std::vector<MontageSegment> segments;
    float total = -1.0f;     // the sum of their bests (the Sum of Best for the whole route), -1 when one is unknown
    float pbTime = -1.0f;    // the record of that range (the PB for the whole route; -1: none)
    bool pbOnRoute = false;  // that record went through the same checkpoints (the gain is real)
    int withReplay = 0;      // segments shown from a replay
    /// What the sum could still take off the record (-1 when they do not compare).
    float possibleGain() const;
};

/// The montage of the route's segments between boundaries `from` and `to` (0 = the level's start,
/// k = the route's k-th checkpoint, n + 1 = its exit; `to` < 0: the exit), read from the personal
/// bests of `levelKey`: each segment's best and the replay that holds it (its full path, when that
/// file is on this computer). A best without a replay is looked for in `kept` (exactly its time).
/// Empty when the route is not known.
MontagePlan buildMontagePlan(const PersonalBests& bests, const std::string& levelKey, const std::vector<MontageRun>& kept = {}, int from = 0,
                             int to = -1);

}  // namespace cosmic
