#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/core/Paths.h"

namespace cosmic {

/// The player's finished runs of each level version, segment by segment (saves/segments.json):
/// the last runs (cumulative splits and final time) and the best duration of each segment
/// ever done. Local only, never sent.
///
/// 2.60.3: the run each gold segment comes from keeps its exact replay (a copy, user folder
/// « ghosts/golds/<level version>/ »), so that the best segments can be watched one after the
/// other (« Regarder », section training). A replay stays while a gold segment refers to it;
/// one no gold refers to any more is deleted when the history is saved. Golds of before
/// 2.60.3 have none, unless one of the player's kept runs did exactly that time (adoptReplay).
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
    /// Where a gold segment was done (2.60.3).
    struct GoldSource {
        std::string replay;  // its run's replay in the level's folder ("<name>.cprz"); empty: none
        std::string date;    // of that run
        float start = 0.0f;  // that run's chronometer when the segment started (0, or its previous split)
        float end = 0.0f;    // and when it ended (its split, or its final time for the exit's segment)
        bool has() const { return !replay.empty(); }
    };
    static constexpr size_t kRunsKept = 20;
    static constexpr size_t kLevelsKept = 400;
    /// The gold replays kept in all: beyond, those of the least recently played levels go
    /// (their gold segments stay, without a replay).
    static constexpr uint64_t kMaxReplayBytes = 192ull << 20;
    static constexpr uint64_t kMaxReplayFileBytes = 4ull << 20;

    /// `file` empty: saves/segments.json of the user folder, and the replays in « ghosts/golds »
    /// (set by load()). A `file` given without `replayDir`: no replay is kept.
    explicit SegmentHistory(fs::path file = {}, fs::path replayDir = {});
    void load();
    /// Writes the history, then deletes the gold replays nothing refers to any more.
    bool save();

    /// A finished run of `levelKey` (GhostStore::levelKey: one history per level version).
    /// `checkpoints`: the ids of its splits' checkpoints (kept when there is one per split).
    /// `replay`: the run's exact replay (encoded), kept as the source of the gold segments it
    /// sets; without it, the golds it sets have no replay.
    void add(const std::string& levelKey, const std::vector<float>& splits, float time, const std::string& date,
             const std::vector<std::string>& checkpoints = {}, const std::vector<uint8_t>* replay = nullptr);
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
    /// Where each gold segment comes from (as many as goldSegments; `has()` false: no replay).
    std::vector<GoldSource> goldSources(const std::string& levelKey) const;
    /// The file of a gold replay of `levelKey` (empty when replays are not kept).
    fs::path replayPath(const std::string& levelKey, const std::string& replay) const;

    /// A gold segment without a replay (done before 2.60.3) that one of the player's kept runs
    /// did in exactly that time (to the half millisecond): that run's replay (`replayFile`,
    /// copied once) becomes its source. `segment`, `start` and `end` as in GoldSource.
    struct Adoption {
        size_t segment = 0;
        float start = 0.0f, end = 0.0f;
    };
    /// True when at least one gold segment took it (save() to keep it).
    bool adoptReplay(const std::string& levelKey, const fs::path& replayFile, const std::string& date, const std::vector<Adoption>& segments);

private:
    struct Level {
        std::vector<Run> runs;
        std::vector<float> best;  // best segments (index: segment), kept even when runs go
        size_t bestSplitCount = 0;
        std::vector<std::string> bestCheckpoints;  // ids of the best segments' route (empty: unknown)
        std::vector<GoldSource> sources;           // where each best segment comes from (index: segment)
        std::string touched;      // date of the last run (the oldest levels go first)
    };
    /// Writes a gold replay of `levelKey` in a new file: its name, empty when not written.
    std::string writeReplay(const std::string& levelKey, const std::vector<uint8_t>& bytes, const std::string& date) const;
    /// The folder of a level's gold replays (its key, made file-name safe).
    fs::path levelDir(const std::string& levelKey) const;
    /// Deletes the gold replays no gold segment refers to, then the oldest levels' ones beyond
    /// kMaxReplayBytes (their sources are dropped).
    void collectReplays();
    fs::path file_;
    fs::path replayDir_;
    std::map<std::string, Level> levels_;
};

}  // namespace cosmic
