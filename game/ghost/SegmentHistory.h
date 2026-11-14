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
    };
    static constexpr size_t kRunsKept = 20;
    static constexpr size_t kLevelsKept = 400;

    /// `file` empty: saves/segments.json of the user folder (set by load()).
    explicit SegmentHistory(fs::path file = {});
    void load();
    bool save() const;

    /// A finished run of `levelKey` (GhostStore::levelKey: one history per level version).
    void add(const std::string& levelKey, const std::vector<float>& splits, float time, const std::string& date);
    /// Most recent first.
    const std::vector<Run>* runs(const std::string& levelKey) const;
    /// Best duration of each segment over every run kept with the same number of splits as
    /// `splitCount` (-1 when unknown); the last one is the exit's segment.
    std::vector<float> bestSegments(const std::string& levelKey, size_t splitCount) const;

private:
    struct Level {
        std::vector<Run> runs;
        std::vector<float> best;  // best segments (index: segment), kept even when runs go
        size_t bestSplitCount = 0;
        std::string touched;      // date of the last run (the oldest levels go first)
    };
    fs::path file_;
    std::map<std::string, Level> levels_;
};

}  // namespace cosmic
