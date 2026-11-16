#pragma once
// 2.50 « Migrer vers un nouveau compte » (docs/online/DEVICES.md): the progression of the old
// account (the last copy its game sent, `progress_copies`) added to this computer's saves, the
// best of each side kept. Works on the JSON of each save (what the classes write with toJson and
// read with fromJson), so that the classes stay as they are.

#include "engine/core/Json.h"

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
/// uses summed, maxima the highest, sets united.
Json mergeAchievementsJson(const Json& mine, const Json& theirs);
/// collection.json: what was heard, met and announced (union), the planet pictures saved (summed);
/// what is worn, the menu music and the profile badges stay those of this computer.
Json mergeCollectionJson(const Json& mine, const Json& theirs);

}  // namespace cosmic
