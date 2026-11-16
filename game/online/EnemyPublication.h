#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ContentDatabase;

/// A campaign level as the server knows it (GET /admin/enemies/{id}/revisions, `levels`).
struct ServerCampaignLevel {
    int version = 0;      // its current revision online
    int nextVersion = 0;  // the number its next revision takes (computed by the server)
    std::string contentHash;
    int bests = 0;        // best times on its current revision
};

/// One level of a publication of an enemy revision (2.45, ADMINISTRATION › Ennemis): the next
/// revision of a campaign level that uses the enemy. Same content, `metadata.revision` bumped and
/// its binding (`metadata.enemyRevisions`) naming the new enemy revision.
struct EnemyPublicationLevel {
    std::string levelId;
    std::string planetId;
    int baseVersion = 0;  // its revision online now
    int version = 0;      // the new revision
    int instances = 0;    // the enemy placed in it (0: used through another entity: a summon, a split…)
    int bests = 0;        // best times on its current revision (server)
    std::string text;     // the level file exactly as it is sent and stored
    std::string hash;     // its ranked fingerprint, with the new definition
    Json manifest;        // its --online-manifest entry
    Json rating;          // rateDifficulty: the enemies changed, the level is rated again
};

struct EnemyPublicationPlan {
    std::vector<EnemyPublicationLevel> levels;
    std::string error;  // "" when every level could be prepared
};

/// The binding a level revision gets when `enemyId` is published as `revision`: every native it
/// uses (recursively, with the definitions it would play) at its current revision online
/// (`current`: enemy id -> revision, 0 / absent: the game's own definition), `enemyId` at
/// `revision`. The level's own binding is the starting point. An empty `enemyId`: the current
/// revisions only (a campaign revision published from the level editor).
std::map<std::string, int> enemyBindingFor(const ContentDatabase& content, const LevelData& level, const std::string& enemyId, int revision,
                                           const std::map<std::string, int>& current);
/// The binding as `metadata.enemyRevisions` writes it (removed when empty).
void setEnemyBinding(LevelData& level, const std::map<std::string, int>& binding);

/// The native enemies a campaign level uses with the definitions it plays now (placed ones, and
/// those another entity brings: summons, splits, variants).
std::set<std::string> enemiesUsedBy(const ContentDatabase& content, const LevelData& level);

/// The campaign levels that use `enemyId` (on their current revision, which this game must play:
/// its fingerprint must be the server's `contentHash`), each prepared as its next revision (see
/// EnemyPublicationLevel). The definition `revision` of `enemyId` must already be in the content
/// (ContentDatabase::setEnemyRevision): the fingerprints, manifests and ratings are computed with
/// it. A level this game does not play as the server knows it stops everything (`error`).
/// Runs off the main thread (a world runs each level's intro, the rating analyses its route).
EnemyPublicationPlan planEnemyPublication(const ContentDatabase& content, const std::string& enemyId, int revision,
                                          const std::map<std::string, ServerCampaignLevel>& server, const std::map<std::string, int>& current);

}  // namespace cosmic
