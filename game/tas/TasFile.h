#pragma once

// The runs computed by the machine (docs/TAS.md): for a campaign level and a category of the
// rankings (ANY%, 100 %, Sans mort, Sans dégât), the fastest run an offline search
// (tools/tas, `cosmic_tas`) found by playing the deterministic engine. Never a proven optimum:
// « meilleur temps trouvé par la machine ». Never in the official rankings.
//
// One file per level and category, shipped with the levels:
//
//   data/tas/<levelId>/<CATEGORY_ID>.json   (categoryId(): ANY_PERCENT, HUNDRED_PERCENT, ...)
//   data/tas/<levelId>/<CATEGORY_ID>.cprz   (its replay, in the game's own format: game/online/Replay.h)
//
// The JSON (format "cosmic.tas", every field below; unknown fields are ignored):
//
//   {
//     "format": "cosmic.tas",
//     "level": "sylvanox_01",
//     "levelVersion": 1,                 // metadata.revision of the level
//     "levelHash": "<64 hex>",           // fingerprintRankedLevel: the rankings' fingerprint
//     "simVersion": 19,                  // kSimulationVersion of the game that computed it
//     "category": "ANY_PERCENT",
//     "timeMs": 41250,                   // the level's clock at the end, as a record counts it
//     "splits": [{"checkpoint": "cp_1", "timeMs": 8125}, ...],
//     "explored": 1843200,               // attempts simulated by the search
//     "computedAt": "2026-11-27",
//     "tool": "cosmic_tas 1 (jeu 2.92.0, simulation 19)",
//     "routeIntervalMs": 250,            // the route: Nova's feet every 0.25 s of the clock
//     "route": [[x, y], ...],
//     "deaths": 0, "damageTaken": 0, "collectibles": 12, "totalCollectibles": 75,
//     "replay": "ANY_PERCENT.cprz"       // file name, next to the JSON
//   }
//
// The rule of the screens (the player's): a run is shown only for the level and the simulation
// it was computed for. tasFor() returns nothing when the file is missing, unreadable, or its
// levelHash or simVersion is not the running game's (the level or the rules changed since): it
// stays hidden until it is computed again.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class ContentDatabase;
class LevelFingerprints;
struct ReplayData;

inline constexpr const char* kTasFormat = "cosmic.tas";
/// The route of a run: one point every this many ms of the level's clock.
inline constexpr int kTasRouteIntervalMs = 250;

struct TasSplit {
    std::string checkpoint;  // id of the checkpoint object
    int64_t timeMs = 0;      // the level's clock when it was reached
};

struct TasRecord {
    std::string levelId;
    int levelVersion = 1;
    std::string levelHash;
    int simVersion = 0;
    RunCategory category = RunCategory::AnyPercent;
    int64_t timeMs = 0;
    std::vector<TasSplit> splits;
    int64_t explored = 0;
    std::string computedAt;  // "YYYY-MM-DD"
    std::string tool;
    int routeIntervalMs = kTasRouteIntervalMs;
    std::vector<Vec2> route;  // feet, world pixels
    int deaths = 0;
    int damageTaken = 0;
    int collectibles = 0;
    int totalCollectibles = 0;
    std::string replay;  // file name of the replay, in the folder of the JSON
    /// Not in the JSON: the folder of the files the record was read from by tasFor() (data/tas,
    /// or the runs downloaded from Cosmic Online and verified on this computer, TasOnline.h).
    /// tasReplayPath() and loadTasReplay() find its replay there; empty: the `root` they are given.
    fs::path sourceRoot;

    Json toJson() const;
    /// Nullopt (and why, in `error`) for anything that is not a complete, coherent record:
    /// another format, no level, an unknown category, no time, a replay name with a folder in it.
    static std::optional<TasRecord> fromJson(const Json& j, std::string* error = nullptr);
};

/// data/tas (the installed game's data folder).
fs::path tasRoot();
/// <root>/<levelId>/<CATEGORY_ID>.json
fs::path tasFilePath(const std::string& levelId, RunCategory c, const fs::path& root = tasRoot());
/// The replay's usual file name: "<CATEGORY_ID>.cprz".
std::string tasReplayFileName(RunCategory c);
/// Where the replay of `r` is (next to its JSON in <root>/<levelId>/; in r.sourceRoot when set).
fs::path tasReplayPath(const TasRecord& r, const fs::path& root = tasRoot());

std::optional<TasRecord> readTasFile(const fs::path& file, std::string* error = nullptr);
/// Writes the JSON (atomically; the folder is created).
bool writeTasFile(const fs::path& file, const TasRecord& r);

/// The machine's run of `levelId` in `c`, when it can be shown in this game: its file exists
/// and reads, it is for that level and category, its levelHash is `levelHash` (the level's
/// ranked fingerprint now) and its simVersion is kSimulationVersion, and its replay file is
/// there. Otherwise nothing: hidden until it is computed again.
/// With the default `root` (data/tas), the runs downloaded from Cosmic Online and verified on
/// this computer (tasDownloadedRoot()) count too: the fastest valid run of the two is returned.
std::optional<TasRecord> tasFor(const std::string& levelId, RunCategory c, const std::string& levelHash,
                                const fs::path& root = tasRoot());
/// The fastest valid run (the rule of tasFor) among the folders `roots` (the first one wins a tie).
std::optional<TasRecord> bestTasAmong(const std::string& levelId, RunCategory c, const std::string& levelHash,
                                      const std::vector<fs::path>& roots);
/// The folder of the runs published on Cosmic Online, downloaded and verified on this computer
/// (game/tas/TasOnline.h: the game sets it at startup); empty: none.
void setTasDownloadedRoot(const fs::path& root);
fs::path tasDownloadedRoot();
/// The same, the fingerprint taken from `fingerprints` (the rankings' cache: only the levels
/// shipped with the game have one, so a level of the workshop never has a machine's run).
std::optional<TasRecord> tasFor(const ContentDatabase& content, LevelFingerprints& fingerprints, const std::string& levelId,
                                RunCategory c, const fs::path& root = tasRoot());
/// Every category of the level that has a run shown (in the order of kAllCategories).
std::vector<TasRecord> tasForLevel(const ContentDatabase& content, LevelFingerprints& fingerprints, const std::string& levelId,
                                   const fs::path& root = tasRoot());

/// Reads the replay of a record (exact replay of the same level hash and simulation), nullopt
/// otherwise. Play it with ReplaySimulator on a world that has just loaded the level.
std::optional<ReplayData> loadTasReplay(const TasRecord& r, const fs::path& root = tasRoot(), std::string* error = nullptr);

/// The honest wording of a machine's time, for every screen that shows one (French source,
/// translated at display): « Meilleur temps trouvé par la machine ». The time itself:
/// formatRunTime(timeMs).
const char* tasTimeLabel();

}  // namespace cosmic
