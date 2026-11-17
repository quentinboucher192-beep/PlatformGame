#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/gameplay/BossTraining.h"
#include "game/ghost/SegmentHistory.h"

namespace cosmic {

// « Regarder » (section training, 2.60.3): the Sum of Best of a level version watched — the
// best of each segment of its route, full runs and section trainings together, one after the
// other, each played from the exact replay of the run or the attempt that did it (ReplayPlayback:
// the real simulation of that run, from the step it reached the segment's start to the step it
// reached its end). Display only: nothing here is ranked, sent, nor changes a record or the
// simulation. Never two versions of a level: everything comes from one GhostStore::levelKey.

/// A full run of this level version whose exact replay is on this computer.
struct MontageRun {
    std::string file;  // its replay (UTF-8 path), empty: none
    std::string date;
    std::vector<float> splits;  // cumulative seconds at each checkpoint
    float time = 0.0f;          // final time (<= 0: no run)
    std::vector<std::string> checkpoints;  // ids of its splits' checkpoints, empty: unknown
};

/// The best section-training attempt of a segment (TrainingRecords).
struct MontageTraining {
    float best = -1.0f;
    std::string date;
    std::string file;  // its replay (UTF-8 path), empty: none (a best of before 2.60.3)
    SectionTrainingStart start;
};
using MontageTrainings = std::map<std::pair<std::string, std::string>, MontageTraining>;

/// One segment of the montage.
struct MontageSegment {
    int index = 0;
    std::string from, to;     // its ends (checkpoint ids; "" = the start, the exit; both "" when the route is unknown)
    float duration = -1.0f;   // its best duration: its part of the Sum of Best (-1: unknown)
    bool training = false;    // done in a section training (else in a full run)
    std::string date;         // of that run / attempt
    std::string file;         // the replay that holds it (UTF-8 path), empty: none on this computer
    float start = 0.0f;       // the segment on that replay's chronometer
    float end = 0.0f;
    SectionTrainingStart trainingStart;  // a training's replay starts that way (ReplayPlayback's setup)
    /// Found in one of the player's kept full runs (record, previous record, last run) with
    /// exactly the gold time: SegmentHistory::adoptReplay keeps a copy of it as the gold's source.
    bool adopt = false;
    bool hasReplay() const { return !file.empty(); }
};

struct MontagePlan {
    std::vector<MontageSegment> segments;
    float total = -1.0f;     // the Sum of Best (as SegmentReferences::sob), -1 when a segment is unknown
    float pbTime = -1.0f;    // the record (-1: none)
    bool pbOnRoute = false;  // the record went through the same checkpoints (the gain is real)
    int withReplay = 0;      // segments shown from a replay
    /// What the Sum of Best could still take off the record (-1 when they do not compare).
    float possibleGain() const;
};

/// The montage of a level version: `gold`, `goldCheckpoints` and `sources` from its segment
/// history (each source's `replay` being the full path of its file, empty when none); `pb`
/// the record run, `others` the player's other kept runs (previous record, last run), `route`
/// the level's checkpoints in order, `trainings` the best section-training attempts. The total
/// is the Sum of Best the training screen shows (buildSegmentReferences): the gold segments,
/// completed with the record's when it went the same way, each replaced by the training's best
/// when that one is faster (to the millisecond).
MontagePlan buildMontagePlan(const std::vector<float>& gold, const std::vector<std::string>& goldCheckpoints,
                             const std::vector<SegmentHistory::GoldSource>& sources, const MontageRun& pb, const std::vector<MontageRun>& others,
                             const std::vector<std::string>& route, const MontageTrainings& trainings = {});

}  // namespace cosmic
