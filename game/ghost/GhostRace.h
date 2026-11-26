#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/Ghost.h"

namespace cosmic {

/// Durations of the segments of a run: from the start to the first checkpoint, between
/// checkpoints, from the last one to the exit (`finalTime` <= 0: the run is not finished,
/// no last segment). Not to be confused with the cumulative splits.
std::vector<float> segmentDurations(const std::vector<float>& splits, float finalTime);

/// "+2,450 s" (behind), "-1,230 s" (ahead), "0,000 s" (level); without `unit`: "+2,450".
std::string formatGhostDelta(float seconds, bool unit = true);
/// A run time with hundredths, French style: "1:02,35" ("1:02:03,40" beyond an hour).
std::string formatGhostTime(float seconds);
/// 2.91: a short duration to the millisecond, without a sign: "3,182 s" ("1:04,930" beyond a minute).
std::string formatGhostSeconds(float seconds);

/// Comparison of the current run with one or more ghosts, on the level timer.
///
/// Live gap: the time the player needed to reach the furthest point of the route minus the
/// time the ghost needed for the same point (both measured on the progress bar's route). At
/// each checkpoint the gap is exact (the two splits). Display only: nothing here touches the
/// simulation, and the ghosts never interact with the level.
class GhostRace {
public:
    struct Racer {
        GhostMeta meta;
        GhostTrack track;
        Color color{120, 220, 255, 255};
        bool sameVersion = true;  // made on this exact version of the level (official comparison)
        // Live values (update()).
        float liveDelta = 0.0f;
        bool hasLiveDelta = false;
        float timeHere = -1.0f;  // when the ghost was where the player is (-1: unknown)
        GhostTrack::Pose pose;
        float animTime = 0.0f;  // seconds since the pose's state began (animation)
    };
    struct SplitResult {
        int index = 0;
        float player = 0.0f;        // cumulative (run time)
        float ghost = -1.0f;        // the reference ghost's split (-1: none)
        float delta = 0.0f;         // player - ghost
        float segment = 0.0f;       // the player's segment duration
        float ghostSegment = -1.0f;
        float bestSegment = -1.0f;  // the player's best before this run
        int ghostIndex = -1;        // the reference ghost's split matched
        bool comparable = false;    // the ghost passed the same checkpoint
        bool goldSegment = false;   // better than every previous one
        bool aheadOfRecord = false; // ahead of the personal best at this point
    };

    void clear();
    /// Adds a ghost; returns its index. Up to `kMaxGhosts`.
    static constexpr int kMaxGhosts = 8;
    int add(GhostMeta meta, const ReplayData& replay, bool sameVersion, Color color);
    /// A ghost already loaded (and mapped on the route) off the main thread.
    int add(GhostMeta meta, GhostTrack track, bool sameVersion, Color color);
    int size() const { return static_cast<int>(racers_.size()); }
    bool empty() const { return racers_.empty(); }
    Racer& racer(int i) { return racers_[static_cast<size_t>(i)]; }
    const Racer& racer(int i) const { return racers_[static_cast<size_t>(i)]; }
    /// The ghost whose gaps the HUD shows (the first one by default).
    int reference() const { return reference_; }
    void setReference(int i);

    /// Maps every ghost on the route (once it has been computed, off the main thread).
    void setRoute(const LevelProgressTrack& track);
    /// Training from a checkpoint: the run starts `seconds` into the ghost's run, and the
    /// player's first split is the full run's split `splitBase`.
    void setOffset(float seconds, int splitBase) {
        offset_ = seconds;
        splitBase_ = splitBase;
    }
    float offset() const { return offset_; }
    /// The personal reference for gold segments and "ahead of the record".
    void setPersonal(std::vector<float> bestSegments, std::vector<float> pbSplits, float pbTime);

    /// Every frame (no allocation): the player's run time and feet.
    void update(float runTime, Vec2 feet, float dt);
    /// The player reached a checkpoint (the timer's split `index`, cumulative `runTime`).
    void onSplit(int index, float runTime, Vec2 feet);
    /// The run is over (completed).
    void finish(float runTime);

    const std::vector<SplitResult>& splits() const { return results_; }
    float progress() const { return bestProgress_; }
    /// The ghosts' clock: the run time, plus the training offset.
    float ghostTime() const { return runTime_ + offset_; }
    const std::vector<float>& personalSplits() const { return pbSplits_; }
    const std::vector<float>& personalBestSegments() const { return bestSegments_; }
    float personalTime() const { return pbTime_; }
    bool finished() const { return finished_; }
    float finalTime() const { return finalTime_; }
    /// Estimated time at the exit: the run time plus what the reference ghost still needed
    /// from here (-1 if unknown).
    float estimatedFinish() const;
    /// Remaining time estimated (-1 if unknown).
    float estimatedRemaining() const;
    /// Seconds since the last split result (HUD flash).
    float sinceSplit() const { return sinceSplit_; }

private:
    std::vector<Racer> racers_;
    int reference_ = 0;
    float offset_ = 0.0f;
    LevelProgressTrack track_;
    int splitBase_ = 0;
    size_t hint_ = 0;
    float bestProgress_ = 0.0f;
    float runTime_ = 0.0f;
    float sinceSplit_ = 1e9f;
    bool finished_ = false;
    float finalTime_ = 0.0f;
    std::vector<SplitResult> results_;
    std::vector<float> bestSegments_, pbSplits_;
    float pbTime_ = 0.0f;
};

}  // namespace cosmic
