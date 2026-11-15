#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class ContentDatabase;
class Progression;
struct LevelEntry;

/// One place of a level where a creature is met: placed there, or brought by a creature
/// placed there (summons).
struct CreaturePlace {
    std::string levelId;
    std::string levelName;
    std::string planetId;      // planet whose campaign lists the level ("" = outside the campaigns)
    int levelNumber = 0;       // its rank in that campaign (1 = the first level; 0: none)
    bool official = false;     // a level of the game (not the player's nor a package's)
    std::string enemyId;       // the creature met there (the boss itself, or one of its echoes)
    std::string objectId;      // its level object ("" when summoned)
    bool summoned = false;     // brought by another creature of the level
    bool boss = false;         // a boss of the level
    int bossRank = 0;          // its order among the level's bosses along the route (1 = the first)
    int bossCount = 0;         // bosses of the level
    Vec2 feet;                 // where it stands (world pixels)
    float at = -1.0f;          // 0..1 along the level's route (-1: routes not computed)
    // The last checkpoint before it along the route ("" = none: from the start of the level).
    std::string checkpointId;
    std::string checkpointLabel;
    Vec2 checkpointFeet;
    float checkpointAt = 0.0f;
    /// Other bosses between that checkpoint (or the start) and this one: fought first.
    int bossesBetween = 0;
    /// The power tablets placed along the route before that checkpoint (or before the start).
    std::vector<std::string> powersBefore;
};

/// Where every creature of the game is met, read from the levels: the campaigns (planets in
/// their order, then their levels in order), then the other official levels. Slow (every
/// level is read; with `routes`, its intended route is computed too): never on the main
/// thread.
struct CreatureAtlas {
    std::map<std::string, std::vector<CreaturePlace>> places;  // enemy id → its places, in order

    /// Planets (campaigns) where the creature is met, in campaign order.
    std::map<std::string, std::vector<std::string>> planetsByCreature() const;
    /// Every official place where the boss `bossId` can be fought: its own places, then
    /// those of its variants (its echoes, `EnemyDefinition::variantOf`).
    std::vector<CreaturePlace> fightsOf(const ContentDatabase& content, const std::string& bossId) const;
};

struct CreatureScanOptions {
    bool routes = false;        // route positions, checkpoints and tablets before each creature
    bool officialOnly = false;  // only the game's own levels
};
CreatureAtlas scanCreatures(const ContentDatabase& content, CreatureScanOptions opt = {});

/// The official places of every creature, with their routes: computed once per session on a
/// worker thread that owns its copy of the content (leaving a screen never waits for it).
/// nullptr while it is being computed.
std::shared_ptr<const CreatureAtlas> officialCreatureAtlas(const ContentDatabase& content);

/// Why a level of the campaign cannot be played yet ("" = it can): the planet still locked
/// (what to finish first), or the previous level not finished, as the level selection says.
std::string levelLockReason(const LevelEntry& level, const ContentDatabase& content, const Progression& progression);
/// The level's planet can be opened (its level selection is reachable).
bool levelPlanetOpen(const LevelEntry& level, const ContentDatabase& content, const Progression& progression);

}  // namespace cosmic
