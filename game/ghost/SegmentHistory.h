#pragma once

#include <map>
#include <string>
#include <vector>

#include "engine/core/Paths.h"

namespace cosmic {

/// The player's finished runs of each level version, segment by segment (saves/segments.json):
/// the last runs (cumulative splits and final time) and the best duration of each segment
/// ever done. Local only, never sent.
class SegmentHistory {
public:
    struct Run {
        std::string date;
        std::vector<float> splits;  // cumulative seconds at each checkpoint
        float time = 0.0f;          // final time
        /// The checkpoints of the splits (ids, in the order reached; 2.30.2+, empty before):
        /// which segment is which (section training).
        std::vector<std::string> checkpoints;
    };
    static constexpr size_t kRunsKept = 20;
    static constexpr size_t kLevelsKept = 400;

    /// `file` empty: saves/segments.json of the user folder (set by load()).
    explicit SegmentHistory(fs::path file = {});
    void load();
    bool save() const;

    /// A finished run of `levelKey` (GhostStore::levelKey: one history per level version).
    /// `checkpoints`: the ids of its splits' checkpoints (kept when there is one per split).
    void add(const std::string& levelKey, const std::vector<float>& splits, float time, const std::string& date,
             const std::vector<std::string>& checkpoints = {});
    /// Most recent first.
    const std::vector<Run>* runs(const std::string& levelKey) const;
    /// Best duration of each segment over every run kept with the same number of splits as
    /// `splitCount` (-1 when unknown); the last one is the exit's segment.
    std::vector<float> bestSegments(const std::string& levelKey, size_t splitCount) const;
    /// The best segments as kept (their route's split count + 1 values, -1 unknown); empty
    /// when no run of `levelKey` is known.
    std::vector<float> goldSegments(const std::string& levelKey) const;
    /// The checkpoints of the gold segments' route (ids), empty when unknown.
    std::vector<std::string> goldCheckpoints(const std::string& levelKey) const;

private:
    struct Level {
        std::vector<Run> runs;
        std::vector<float> best;  // best segments (index: segment), kept even when runs go
        size_t bestSplitCount = 0;
        std::vector<std::string> bestCheckpoints;  // ids of the best segments' route (empty: unknown)
        std::string touched;      // date of the last run (the oldest levels go first)
    };
    fs::path file_;
    std::map<std::string, Level> levels_;
};

}  // namespace cosmic
