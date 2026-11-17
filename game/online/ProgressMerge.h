#pragma once
// 2.50 « Migrer vers un nouveau compte » (docs/online/DEVICES.md): the progression of the old
// account (the last copy its game sent, `progress_copies`) added to this computer's saves, the
// best of each side kept. Works on the JSON of each save (what the classes write with toJson and
// read with fromJson), so that the classes stay as they are.

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// progress.json: completed levels (union), play time (summed, per level too), the intro seen;
/// the last planet and level stay those of this computer.
Json mergeProgressJson(const Json& mine, const Json& theirs);
/// records.json: per level, the best time with its splits, powers and level fingerprint (only
/// against a best time of the same fingerprint, or none), the best score, medal, collectibles...;
/// attempts and completions summed; the local top runs merged (by time, 10 kept).
Json mergeRecordsJson(const Json& mine, const Json& theirs);
/// boss_stats.json: fights, victories, defeats, flawless victories summed, per phase too; the best
/// victory time (the shortest) and the last victory date (the latest); the giga bosses whose
/// story was seen (2.51) united.
Json mergeBossStatsJson(const Json& mine, const Json& theirs);
/// achievements.json: achievements unlocked (union, the earliest date kept); counters and power
/// uses summed, maxima the highest, sets united; the achievements followed united (2.51: this
/// computer's first, none unlocked, at most 5).
Json mergeAchievementsJson(const Json& mine, const Json& theirs);
/// collection.json: what was heard, met and announced (union), the planet pictures saved (summed);
/// what is worn, the menu music and the profile badges stay those of this computer.
Json mergeCollectionJson(const Json& mine, const Json& theirs);

/// stats.json (2.55, the « Statistiques » screen): merged without counting anything twice —
/// mergePlayStats (game/records/PlayStats.h): each day's counters the highest of both sides, the
/// boss attempts united by identifier, the segment times united, the best times the better ones…
/// `today`: this computer's day (days since 1970-01-01).
Json mergeStatsJson(const Json& mine, const Json& theirs, int today);

/// The saves a copy of the progression carries (the keys of its `files`, each <key>.json in saves/).
const std::vector<std::string>& progressCopyParts();
/// The `files` of a copy (`theirs`, as the server keeps it) added to this computer's (`mine`, the
/// same keys): each part with its merge above. A part the copy lacks, or that is not an object,
/// stays as it is; the result has every part of `progressCopyParts()`. `today` (the statistics):
/// 0 for this computer's calendar.
Json mergeProgressCopyFiles(const Json& mine, const Json& theirs, int today = 0);

/// Before a restoration: the save files <part>.json of `savesDir` that exist, copied byte for byte
/// into a new folder `savesDir`/avant_restauration_<stamp> (another name if it exists). False,
/// with `error`, when one could not be copied: nothing should be restored then.
bool backupSaveFiles(const fs::path& savesDir, const std::string& stamp, fs::path* folder, std::string* error);

/// The copies already added to this computer's saves, by their SHA-256 (`file`, a JSON list): the
/// same copy is never added twice (« Restaurer » again after a « done » that did not reach the
/// server would add the play time twice).
bool progressCopyRestored(const fs::path& file, const std::string& sha256);
bool noteProgressCopyRestored(const fs::path& file, const std::string& sha256);

}  // namespace cosmic
