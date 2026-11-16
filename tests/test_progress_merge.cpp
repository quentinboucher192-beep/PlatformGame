// 2.50 « Migrer vers un nouveau compte »: the progression of the old account added to this
// computer's saves (game/online/ProgressMerge), the best of each side kept.

#include "game/online/ProgressMerge.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("progress merge: completed levels united, play time summed, this computer's last level kept") {
    Json mine = {{"completedLevels", {"sylvanox_01", "sylvanox_02"}}, {"playTimeSeconds", 100.0}, {"levelPlayTime", {{"sylvanox_01", 60.0}}},
                 {"lastLevel", "sylvanox_02"}, {"lastPlanet", "sylvanox"}, {"introSeen", false}};
    Json theirs = {{"completedLevels", {"sylvanox_02", "glacior_01"}}, {"playTimeSeconds", 50.5}, {"levelPlayTime", {{"sylvanox_01", 10.0}, {"glacior_01", 40.0}}},
                   {"lastLevel", "glacior_01"}, {"lastPlanet", "glacior"}, {"introSeen", true}};
    Json m = mergeProgressJson(mine, theirs);
    CHECK(m["completedLevels"].size() == 3);
    CHECK(m["playTimeSeconds"].get<double>() == 150.5);
    CHECK(m["levelPlayTime"]["sylvanox_01"].get<double>() == 70.0);
    CHECK(m["levelPlayTime"]["glacior_01"].get<double>() == 40.0);
    CHECK(m["lastLevel"] == "sylvanox_02");
    CHECK(m["introSeen"].get<bool>());
}

TEST_CASE("progress merge: the best time of a level with its splits, only against the same version") {
    LevelRecord a;
    a.attempts = 5;
    a.completions = 2;
    a.bestTime = 80.0f;
    a.bestSplits = {10.0f, 40.0f};
    a.levelHash = "h1";
    a.bestScore = 900;
    a.fewestDeaths = 2;
    a.bestMedal = Medal::Silver;
    LevelRecord b;
    b.attempts = 3;
    b.completions = 1;
    b.bestTime = 70.0f;
    b.bestSplits = {9.0f, 33.0f};
    b.levelHash = "h1";
    b.bestScore = 800;
    b.fewestDeaths = 0;
    b.bestMedal = Medal::Gold;
    Json m = mergeRecordsJson(Json{{"levels", {{"lv", a.toJson()}}}}, Json{{"levels", {{"lv", b.toJson()}, {"other", b.toJson()}}}});
    const LevelRecord r = LevelRecord::fromJson(m["levels"]["lv"]);
    CHECK(r.attempts == 8);
    CHECK(r.completions == 3);
    CHECK(r.bestTime == 70.0f);
    CHECK(r.bestSplits.size() == 2 && r.bestSplits[1] == 33.0f);
    CHECK(r.bestScore == 900);
    CHECK(r.fewestDeaths == 0);
    CHECK(r.bestMedal == Medal::Gold);
    CHECK(m["levels"].contains("other"));
    // Another version of the level: its time never replaces this one's.
    b.levelHash = "h2";
    b.bestTime = 10.0f;
    Json m2 = mergeRecordsJson(Json{{"levels", {{"lv", a.toJson()}}}}, Json{{"levels", {{"lv", b.toJson()}}}});
    CHECK(LevelRecord::fromJson(m2["levels"]["lv"]).bestTime == 80.0f);
}

TEST_CASE("progress merge: boss statistics summed per phase, the shortest victory kept") {
    BossRecord a;
    a.fights = 4;
    a.kills = 1;
    a.phaseClears = {3, 1};
    a.bestKill = 90.0f;
    a.lastKill = "2026-11-10 10:00";
    BossRecord b;
    b.fights = 2;
    b.kills = 2;
    b.phaseClears = {2, 2, 1};
    b.bestKill = 75.0f;
    b.lastKill = "2026-11-12 08:00";
    Json m = mergeBossStatsJson(Json{{"bosses", {{"x", a.toJson()}}}}, Json{{"bosses", {{"x", b.toJson()}}}});
    const BossRecord r = BossRecord::fromJson(m["bosses"]["x"]);
    CHECK(r.fights == 6);
    CHECK(r.kills == 3);
    CHECK(r.phaseClears.size() == 3 && r.phaseClears[0] == 5 && r.phaseClears[2] == 1);
    CHECK(r.bestKill == 75.0f);
    CHECK(r.lastKill == "2026-11-12 08:00");
}

TEST_CASE("progress merge: achievements united with their earliest date, counters summed, maxima kept") {
    Json mine = {{"unlocked", {{"a", "2026-11-10"}, {"b", "2026-11-12"}}}, {"counters", {{"kills", 10}}}, {"maxima", {{"run_kills", 7}}},
                 {"sets", {{"cleared_levels", {"x"}}}}, {"power_uses", {{"dash", 3}}}};
    Json theirs = {{"unlocked", {{"b", "2026-11-01"}, {"c", "2026-11-05"}}}, {"counters", {{"kills", 5}, {"jumps", 2}}}, {"maxima", {{"run_kills", 9}}},
                   {"sets", {{"cleared_levels", {"y"}}}}, {"power_uses", {{"dash", 1}}}};
    Json m = mergeAchievementsJson(mine, theirs);
    CHECK(m["unlocked"].size() == 3);
    CHECK(m["unlocked"]["b"] == "2026-11-01");
    CHECK(m["counters"]["kills"].get<int64_t>() == 15);
    CHECK(m["counters"]["jumps"].get<int64_t>() == 2);
    CHECK(m["maxima"]["run_kills"].get<int64_t>() == 9);
    CHECK(m["sets"]["cleared_levels"].size() == 2);
    CHECK(m["power_uses"]["dash"].get<int64_t>() == 4);
}

TEST_CASE("progress merge: the collection united, what is worn on this computer kept") {
    Json mine = {{"equipped", {{"suit", "aurore"}}}, {"heard", {"music/a.ogg"}}, {"met", {"slime"}}, {"exported", 1}, {"menuMusic", "music/a.ogg"}};
    Json theirs = {{"equipped", {{"suit", "braise"}}}, {"heard", {"music/b.ogg"}}, {"met", {"slime", "golem"}}, {"exported", 2}, {"menuMusic", "music/b.ogg"}};
    Json m = mergeCollectionJson(mine, theirs);
    CHECK(m["equipped"]["suit"] == "aurore");
    CHECK(m["heard"].size() == 2);
    CHECK(m["met"].size() == 2);
    CHECK(m["exported"].get<int64_t>() == 3);
    CHECK(m["menuMusic"] == "music/a.ogg");
}
