#pragma once

// The TAS of the levels as the screens show them: « meilleur temps trouvé par la machine ». Found offline
// by the project's own search (tools/tas), shipped with the levels (game/tas/TasFile.h: data/tas/<level>/
// <CATEGORY>.json and its replay). Never ranked, never sent: a reference to look at, watch and race.
//
// A TAS is shown only for the exact version of the level it was found on and the simulation that played
// it: after an update of the level or of the simulation it is HIDDEN until it is computed again
// (game/tas/TasFile.h's tasFor() gives nothing). A level without TAS file shows no TAS at all: no empty
// box, no placeholder. The screens ask every frame: the files are read once per level version, kept here.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Paths.h"
#include "game/ghost/GhostStore.h"
#include "game/online/OnlineTypes.h"
#include "game/tas/TasFile.h"

namespace cosmic {

class GameServices;

namespace tas {

/// One TAS as the screens use it: the record (game/tas/TasFile.h) and where its replay is.
struct TasRun : TasRecord {
    fs::path replayFile;
    /// Its time is the ANY% run's (SANS MORT / SANS DÉGÂT found by the ANY% run itself): « = ANY% ».
    bool sameAsAny = false;

    float seconds() const { return static_cast<float>(timeMs) / 1000.0f; }
    /// The level's clock at a checkpoint ("" = the start: 0, not passed: -1).
    int64_t at(const std::string& checkpoint) const;
    /// The time of the segment `from` → `to` ("" = the start / the finish), -1 when the run does not
    /// pass both in this order.
    int64_t segment(const std::string& from, const std::string& to) const;
    /// The run as a ghost of this level version (« Course contre le fantôme TAS », the comparisons).
    GhostEntry ghost() const;
};

/// The TAS shipped with the game, read once per level version and kept.
class TasCatalog {
public:
    /// Where the TAS are (default: game/tas/TasFile.h's tasRoot()). Tests point it at their fixtures.
    void setRoot(fs::path root);
    fs::path root() const;
    void clear();

    /// The TAS of `levelId` in category `c` for the installed version of the level (its rankings'
    /// fingerprint) and this simulation; nullptr when there is none, or when it is hidden.
    const TasRun* tasFor(GameServices& g, const std::string& levelId, RunCategory c);
    /// The same for a version of the level given (its ranked fingerprint).
    const TasRun* tasFor(const std::string& levelId, RunCategory c, const std::string& levelHash);
    /// Whether the level has a TAS shown in any category.
    bool any(GameServices& g, const std::string& levelId);

private:
    struct Level {
        std::string hash;  // the version read
        std::array<std::optional<TasRun>, 4> runs;
    };
    fs::path root_;
    bool rootSet_ = false;
    std::map<std::string, Level> levels_;
};

TasCatalog& catalog();

/// « 4:02,517 » (to the millisecond, as the TAS is timed).
std::string timeText(int64_t ms);
/// « +38,795 s » / « -1:15,854 » (the gap of a time to the TAS).
std::string gapText(int64_t ms);
/// « 19/11/2026 » from « 2026-11-19… » (empty when it is not a date).
std::string dateText(const std::string& iso);
/// « 412 millions », « 3,4 milliards », « 12 500 » (the tries explored, in words).
std::string countText(uint64_t n);

/// My record of this level version in the TAS's category (seconds, -1 none): local for ANY% (the record
/// of PersonalBests, else my best ghost), verified online for the others.
float myRecord(GameServices& g, const TasRun& t);

}  // namespace tas
}  // namespace cosmic
