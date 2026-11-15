#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Paths.h"

namespace cosmic {

class GhostTrack;

// Section training (« Répéter un segment », 2.30.2): which segment of a full run a training
// segment is, the references shown for it (its best duration in full runs, its duration in
// the personal best run, the best training time) and the Sum of Best of the level. Display
// only: nothing here touches the simulation, the records, the medals, the achievements, nor
// the gold segments of the full runs.

/// The ends of a segment: checkpoint ids, "" = the level's start (`from`) or its exit (`to`).
/// Index of the segment from `from` to `to` in a run that reached `checkpoints` in this order
/// (segment 0: from the start to the first checkpoint; the last one: from the last checkpoint
/// to the exit). -1 when that run did not go straight from one to the other (a checkpoint
/// skipped, another order, another route).
int segmentIndexIn(const std::vector<std::string>& checkpoints, const std::string& from, const std::string& to);

/// The checkpoints a finished run reached, in order: those recorded with it (as many as its
/// splits); else those of the route when the run has exactly one split per checkpoint of the
/// route (every checkpoint in the route's order: the usual run, and the runs recorded before
/// 2.30.2 without them); else nothing (unknown: its segments cannot be matched). A run without
/// any split went from the start to the exit without a checkpoint: an empty list, known.
std::optional<std::vector<std::string>> resolveRunCheckpoints(const std::vector<std::string>& recorded, size_t splitCount,
                                                              const std::vector<std::string>& route);

/// Which checkpoint each split of a ghost's run was taken at: the one of `checkpoints` (id,
/// feet) where the ghost stood at that moment, within 320 px (as the race matches them).
/// Empty when one split cannot be told.
std::vector<std::string> checkpointsOfTrack(const GhostTrack& track, const std::vector<std::pair<std::string, Vec2>>& checkpoints);

/// Sum of Best: the sum of the best duration of every segment of a route; -1 when one of
/// them is unknown (< 0) or when there is none.
float sumOfBest(const std::vector<float>& bestSegments);

/// The references of the segments of one level version, from full runs only.
struct SegmentReferences {
    /// One full run: cumulative splits, final time, checkpoints reached (ids, may be empty).
    struct RunInput {
        std::vector<float> splits;
        float time = 0.0f;
        std::vector<std::string> checkpoints;
    };
    std::map<std::pair<std::string, std::string>, float> best;  // (from, to) → best duration in full runs
    std::map<std::pair<std::string, std::string>, float> pb;    // (from, to) → duration in the PB run
    float pbTime = -1.0f;        // the personal best (-1: none)
    float sob = -1.0f;           // Sum of Best (-1: unknown)
    int sobSegments = 0;         // segments of the route the Sum of Best goes through
    bool pbOnSobRoute = false;   // the PB went through the same checkpoints (PB − SoB is a real gain)
    bool anyRun = false;         // at least one full run of this version is known

    /// Best duration of the segment in full runs, -1 unknown (no run went straight through it).
    float bestOf(const std::string& from, const std::string& to) const;
    /// Its duration in the PB run, -1 unknown.
    float pbOf(const std::string& from, const std::string& to) const;
    /// What the Sum of Best could still take off the PB (-1 when they do not compare).
    float possibleGain() const { return pbOnSobRoute && sob > 0.0f && pbTime > 0.0f ? std::max(0.0f, pbTime - sob) : -1.0f; }
};

/// Built from the segment history: `gold` (the best segments of its route, SegmentHistory::
/// bestSegments) with `goldCheckpoints` (ids of that route, may be empty); `runs` (the runs it
/// kept); the PB run `pb` (time <= 0: none); and the route's checkpoints (ids, in order).
SegmentReferences buildSegmentReferences(const std::vector<float>& gold, const std::vector<std::string>& goldCheckpoints,
                                         const std::vector<SegmentReferences::RunInput>& runs, const SegmentReferences::RunInput& pb,
                                         const std::vector<std::string>& route);

/// Best training time of each segment, per level version (saves/training.json), and the
/// powers last chosen for it: local only, never sent, never a record nor a gold segment. A damaged file is set aside
/// (".corrupt") and the history starts again; a file written by a newer game is read but
/// never overwritten.
class TrainingRecords {
public:
    struct Entry {
        float best = -1.0f;  // seconds (-1: no attempt finished yet)
        int attempts = 0;    // finished attempts, every session
        std::string date;    // of the best one
        /// The powers last chosen for this segment (2.30.2); `customPowers` false: the
        /// route's ones (the start powers and the tablets before the start).
        bool customPowers = false;
        std::vector<std::string> powers;
    };
    static constexpr int kVersion = 1;
    static constexpr size_t kLevelsKept = 400;
    static constexpr size_t kSegmentsPerLevel = 256;

    /// `file` empty: saves/training.json of the user folder (set by load()).
    explicit TrainingRecords(fs::path file = {});
    void load();
    bool save() const;

    /// A finished attempt of the segment from `from` to `to` ("" = start / exit) of
    /// `levelKey` (GhostStore::levelKey). True when it is the best one so far.
    bool add(const std::string& levelKey, const std::string& from, const std::string& to, float seconds, const std::string& date);
    const Entry* get(const std::string& levelKey, const std::string& from, const std::string& to) const;
    /// The powers chosen for the segment (nullptr: the route's ones, the default).
    const std::vector<std::string>* powers(const std::string& levelKey, const std::string& from, const std::string& to) const;
    /// Remembers the powers chosen for the segment; nullptr: back to the route's ones.
    void setPowers(const std::string& levelKey, const std::string& from, const std::string& to, const std::vector<std::string>* powers,
                   const std::string& date);
    static constexpr size_t kMaxPowers = 8;
    /// The file was unreadable when loaded (set aside, started again).
    bool recovered() const { return recovered_; }
    /// Written by a newer game: kept as it is (never overwritten).
    bool readOnly() const { return readOnly_; }
    const fs::path& file() const { return file_; }

private:
    Entry* entry(const std::string& levelKey, const std::string& from, const std::string& to, const std::string& date);
    struct Level {
        std::string touched;
        std::map<std::pair<std::string, std::string>, Entry> segments;
    };
    fs::path file_;
    std::map<std::string, Level> levels_;
    bool recovered_ = false;
    bool readOnly_ = false;
};

}  // namespace cosmic
