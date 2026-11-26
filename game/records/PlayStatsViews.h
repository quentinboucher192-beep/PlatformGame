#pragma once

// 2.91 — what the redesigned « Statistiques » screen computes for its display, from what the game
// already keeps (game/records/PlayStats.h, saves/stats.json): nothing here is recorded, nothing
// changes a run.
//
//  * « Récemment joué » (Vue d'ensemble): the levels and the bosses played last, from the last day
//    of each level and the order of the boss fights;
//  * the days of a period (the calendar of the days played, the small curves of the tiles);
//  * « Répartition du fichier » (Données et sauvegarde): the size of each part of stats.json, read
//    from the file itself, and what each part holds;
//  * « Niveaux »: a segment's time in the record run, the day of a best (the one Sum of Best of a
//    level version is game/ghost/PersonalBests.h's).

#include <cstddef>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/records/PlayStats.h"

namespace cosmic {

namespace playstats {

/// A level or a boss played lately (« Récemment joué »).
struct RecentPlay {
    enum class Kind { Level, Boss };
    Kind kind = Kind::Level;
    std::string id;
    int day = 0;  // the last day it was played (days since 1970-01-01; 0: unknown, a 2.54 attempt)
    int seq = 0;  // a boss: the order of its last fight (higher: more recent)
};

/// The last day a level was played: the later of its slices' and its segments' (-1: no statistics).
int levelLastDay(const PlayStatsData& d, const std::string& levelId);
/// The day of a boss's last attempt kept (0: unknown, or no attempt kept).
int bossLastDay(const BossHistory& b);

/// The `count` levels and bosses played the most recently: every level with statistics (its last
/// day) and every boss fought (the day of its last attempt). The most recent day first; on the same
/// day the levels first (by id), then the bosses, their last fight first; a boss whose day is unknown
/// (attempts kept by 2.54) comes after every dated one, by its last fight.
std::vector<RecentPlay> recentlyPlayed(const PlayStatsData& d, size_t count);

/// The time played on each day of `from`..`to` (seconds, 0 for a day not played), in order.
std::vector<double> dailyPlaySeconds(const PlayStatsData& d, int from, int to);
/// For each value, the sum of it and the `window` − 1 before it (fewer at the start).
std::vector<double> trailingSums(const std::vector<double>& v, int window);

/// « Répartition du fichier »: a part of stats.json, its size and what it holds.
struct FilePart {
    enum class Kind { Days, Bosses, Levels, Segments };
    Kind kind = Kind::Days;
    size_t bytes = 0;  // its share of the file (its text, written as in the file)
    int count = 0;     // days / bosses / levels / classic levels
    int count2 = 0;    // months merged / boss attempts kept / slices per level (40) / segments
};
struct FileBreakdown {
    bool ok = false;     // the file was read
    size_t total = 0;    // the file's size
    std::vector<FilePart> parts;  // in the order above, every one (0 bytes when empty)
    size_t other = 0;    // the rest: the envelope (format, version, dates)
};
/// The parts of the file's contents (`file`: the parsed stats.json, `fileBytes`: its size). A file
/// that is not the statistics' gives ok = false.
FileBreakdown fileBreakdown(const Json& file, size_t fileBytes);

/// The time of the segment `from` → `to` ("" = the start / the exit) in a full run of `time`
/// seconds whose cumulative splits were taken at the checkpoints `ids` (as many); -1 when it is
/// not known (the run did not go through both, the splits and the checkpoints do not match).
float segmentInRun(const std::vector<float>& splits, const std::vector<std::string>& ids, float time, const std::string& from, const std::string& to);
/// « 2026-11-22 18:05 » (FileUtil::nowString) → its day (days since 1970-01-01; 0 when it is not a date).
int dayOfDate(const std::string& date);

}  // namespace playstats
}  // namespace cosmic
