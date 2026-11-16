#pragma once
// 2.52: suggestions of the Succès screen (five achievements worth aiming at now, each with why)
// and what the followed achievements need (their progress written out, whether it moves during
// a run). Everything comes from the player's own saves: no number is made up.

#include <map>
#include <string>
#include <vector>

#include "game/achievements/Achievements.h"

namespace cosmic {

/// What the suggestions know besides the saves.
struct SuggestionContext {
    std::string recentLevel;   // the level played last (Progression::lastLevel)
    std::string recentPlanet;  // the planet played last (Progression::lastPlanet)
    /// Boss id → the levels where it is fought (CreatureAtlas::fightsOf). `bossLevelsKnown`
    /// false: the levels are still being read (a boss never fought counts as not sure yet).
    std::map<std::string, std::vector<std::string>> bossLevels;
    bool bossLevelsKnown = false;
    bool online = false;  // connected to Cosmic Online
};

struct AchievementSuggestion {
    const AchievementDef* def = nullptr;
    AchievementProgress progress;
    double score = 0.0;
    std::string reason;  // one short line: « Plus que 3 victoires », « 80 % fait »…
};

constexpr size_t kSuggestionCount = 5;
constexpr int kSuggestionsPerCategory = 2;

/// Up to `count` achievements not unlocked yet, best first (the same state always gives the
/// same list). Left out: unlocked, their requirements not unlocked, unknown conditions, a level
/// or planet that does not exist or is still locked, a boss whose levels are all locked.
/// Score: closeness (value / target), how little is left to do (an estimate per kind of
/// condition, never shown), the stars, what was played last, already started; what needs
/// Cosmic Online comes after the rest while logged out. At most kSuggestionsPerCategory per
/// category; ties in the catalogue's order.
std::vector<AchievementSuggestion> suggestAchievements(const AchievementCatalog& catalog, const AchievementBook& book,
                                                       const AchievementInputs& in, const SuggestionContext& ctx,
                                                       size_t count = kSuggestionCount);

/// « 1 234 / 5 000 » (the time played: « 2 h 05 min / 5 h 00 min »), never above the target.
std::string achievementProgressText(const AchievementDef& def, const AchievementProgress& p);
/// The condition moves while a level is played (the counters of the run, the bosses' records):
/// otherwise it is counted after the run (levels finished, medals, collection, online...).
bool achievementMovesDuringRun(const AchievementCondition& c);
/// Needs Cosmic Online (an account, friends, other players' levels, comments, world records...).
bool achievementNeedsOnline(const AchievementCondition& c);

}  // namespace cosmic
