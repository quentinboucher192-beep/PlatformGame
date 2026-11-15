#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/Replay.h"

namespace cosmic {

struct LevelProgressTrack;

/// Where a ghost comes from and what it represents (shown above it and in the menus). Kept
/// next to each replay file (".json"), never inside it: the replay stays the one the server
/// knows.
struct GhostMeta {
    std::string source;      // "pb", "last", "world", "friend", "record", "favorite", "file"
    std::string label;       // the player's name (shown above the ghost)
    std::string playerId;    // online player (profile), empty for local runs
    std::string levelId;
    std::string levelHash;   // exact version of the level the run was made on
    std::string recordId;    // online record (cache key, favourites)
    std::string category;    // ANY_PERCENT, SPEEDRUN... (online records)
    int levelVersion = 0;
    int64_t timeMs = 0;
    std::string date;
    std::vector<float> splits;  // cumulative seconds at each checkpoint
    bool validated = false;     // verified by the server (not a proof by itself)
    bool community = false;     // a community level's record
    std::string gameVersion;
    /// Look of the suit its player wore online (2.23: « givre »…, from the leaderboard entry
    /// it was started from). Display only, never part of the replay; the player's own runs
    /// wear the suit equipped now instead.
    std::string suit;

    /// The player's own run on this computer (best, previous best, last).
    bool mine() const { return source == "pb" || source == "prev" || source == "last"; }
    Json toJson() const;
    static GhostMeta fromJson(const Json& j);
    /// "Record mondial", "Mon meilleur temps"... (menus).
    std::string sourceLabel() const;
};

/// A replay checked before it is raced against: readable, of this exact version of the
/// level, finished, consistent in time and space. A replay is never taken as the proof of a
/// legitimate record: the server's checks stay the only ones for the rankings.
struct GhostCheck {
    bool ok = false;
    std::string reason;  // why it was refused (French)
};
GhostCheck checkGhostReplay(const ReplayData& r, const std::string& expectedLevelHash, const Rect& levelBounds);

/// Animation clip of a ghost frame (same choice as the replay viewer).
const char* ghostClipFor(const ReplayFrame& f, Vec2 vel);

/// A ghost's run, ready to be raced: positions sampled at the replay's rate and
/// interpolated for display (the recorded positions, not a re-simulation), checkpoint
/// splits, and the time at which it first reached each part of the level's route.
class GhostTrack {
public:
    struct Pose {
        Vec2 feet;
        Vec2 vel;
        uint8_t state = 0;
        uint8_t flags = 0;
        bool visible = false;
    };

    void load(const ReplayData& r);
    /// Maps the run onto the level's route (progress bar route): what time the ghost needed
    /// to reach each point. Without a route, only the splits compare.
    void mapProgress(const LevelProgressTrack& track);

    /// Its pose `t` seconds after the start (allocation-free, used every frame).
    Pose poseAt(float t) const;
    float duration() const { return duration_; }
    const std::vector<float>& splits() const { return splits_; }
    /// Splits read to the millisecond (Split events, 2.18+) or estimated from the
    /// checkpoint samples (older replays: about 1/30 s).
    bool splitsExact() const { return splitsExact_; }
    /// The moment the ghost was nearest to `p` (training without a split there).
    float nearestTime(Vec2 p) const;
    /// First time the ghost reached `progress` (0..1) of the route; -1 if never or unmapped.
    float timeAtProgress(float progress) const;
    /// The furthest part of the route (0..1) the ghost had reached `t` seconds after its
    /// start; -1 if unmapped (the inverse of timeAtProgress, replay comparison).
    float progressAtTime(float t) const;
    bool mapped() const { return !progressTimes_.empty(); }
    bool empty() const { return frames_.empty(); }

private:
    std::vector<ReplayFrame> frames_;
    std::vector<uint32_t> cuts_;  // frames after a death / respawn / teleport (no interpolation)
    float hz_ = 30.0f;
    float duration_ = 0.0f;
    std::vector<float> splits_;
    bool splitsExact_ = false;
    static constexpr int kProgressSteps = 512;
    std::vector<float> progressTimes_;  // per 1/512 of the route: first time reached (-1)
};

}  // namespace cosmic
