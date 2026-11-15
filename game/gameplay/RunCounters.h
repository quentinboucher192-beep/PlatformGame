#pragma once

namespace cosmic {

class GameWorld;
struct AchievementStats;
struct RunResult;

/// The achievement counters a run adds from its world (kills, deaths, shards, relics, secrets,
/// powers carried, the run's kills, runs finished). A training run (GameWorld::training())
/// adds nothing: returns false.
bool noteRunCounters(AchievementStats& a, const GameWorld& w, bool completed);

/// The result of a run as the records and the progression see it (time, deaths, collectibles,
/// kills, splits), marked as training when the world is.
RunResult runResultOf(const GameWorld& w, bool completed);

}  // namespace cosmic
