#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Paths.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/ReplayGap.h"

namespace cosmic {

class GameServices;

namespace replaygap {

// The references of the replay viewer's gap (2.91): the player's record (PB) of this version
// of the level, the Sum of Best (the one of PersonalBests: each best segment from the replay
// that holds it, whatever its source), the TAS (the machine's run of the level, its category the
// replay's: ui/screens/TasCatalog.h; only when the level has one for this version), or another
// replay chosen. The saves are read on the
// main thread (prepare…: names, files, bests), the replays off it (loadReference: decoded,
// checked, mapped on the route). Read only: nothing is written.

enum class RefKind { Record = 0, SumOfBest = 1, Other = 2, Tas = 3 };
inline constexpr size_t kRefKinds = 4;

/// A part of a reference's ghost: `track` from `start` to `end` on its own clock, shown from
/// `from` on the reference's clock (the whole run for a record; one best segment each for the
/// Sum of Best).
struct GhostPiece {
    std::shared_ptr<const GhostTrack> track;
    double from = 0.0, start = 0.0, end = 0.0;
};

struct LoadedRef {
    bool ok = false;
    std::string why;   // when not ok: what is missing (shown)
    std::string name;  // « Record (PB) · 22/11 », « Somme des meilleurs », the other replay's
    GapReference ref;
    std::shared_ptr<const ReplayData> replay;  // a whole run's (its measures A → B)
    std::vector<GhostPiece> ghost;
    int segments = 0, withReplay = 0;  // the Sum of Best: its segments, those with a replay here
    bool sameRun = false;              // the record is the very run watched
    /// Its ghost at its own time `t` (false between pieces or without one).
    bool poseAt(double t, GhostTrack::Pose& out) const;
};

struct RefRequest {
    RefKind kind = RefKind::Record;
    LevelProgressTrack route;
    std::shared_ptr<const ReplayData> run;                  // the replay watched
    std::vector<std::pair<std::string, Vec2>> checkpoints;  // the level's (id, feet), in the route's order
    Rect bounds;
    // The record, another replay.
    fs::path file;
    std::shared_ptr<const ReplayData> memory;
    GhostMeta meta;
    std::string missing;  // nothing to load: why
    // The Sum of Best: the segments of the route in order, each with its best and the replay
    // that holds it (its file, empty when none is on this computer; the segment on its clock).
    struct SobSegment {
        std::string from, to;  // checkpoint ids ("" = the start, the exit)
        float best = -1.0f;
        std::string file;
        float start = 0.0f, end = 0.0f;
    };
    std::vector<SobSegment> sob;
};

/// « 22/11 » from « 2026-11-22 … » (empty when it is not a date).
std::string shortDate(const std::string& date);
/// The player's record of this exact version (`run`'s), for the reference.
RefRequest prepareRecord(GameServices& g, const std::string& levelId, const std::string& levelHash);
/// The Sum of Best of this exact version: the segment history, the trainings, the kept runs.
RefRequest prepareSumOfBest(GameServices& g, const std::string& levelId, const std::string& levelHash);
/// The TAS of this exact version in `category` (« ANY_PERCENT »…; empty or unknown: ANY%): its replay.
RefRequest prepareTas(GameServices& g, const std::string& levelId, const std::string& levelHash, const std::string& category);
/// Off the main thread.
LoadedRef loadReference(RefRequest req);

}  // namespace replaygap
}  // namespace cosmic
