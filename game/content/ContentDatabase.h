#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/levels/LevelData.h"
#include "game/planets/PlanetData.h"
#include "game/powers/PowerDefinition.h"

namespace cosmic {

enum class ContentSource { Builtin, User, Package };
const char* contentSourceLabel(ContentSource s);

struct LevelEntry {
    std::string id;
    std::string name;
    std::string description;
    std::string planet;
    std::string author;
    int difficulty = 1;
    fs::path path;
    ContentSource source = ContentSource::Builtin;
    std::string packageId;
    fs::path assetRoot;        // assets folder mounted while playing / editing (packages)
    std::string assetPackage;  // user copy of an imported level: package providing its assets
    /// 2.45: the file shipped with the game when `path` is a campaign revision published
    /// online by an administrator and downloaded from Cosmic Online ("" otherwise).
    fs::path bundledPath;
};

struct PackageInfo {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string levelId;
    std::string installedAt;
    fs::path root;
};

/// Registry of every piece of game content: planets, levels, enemies, powers and
/// particle effects, from the installation (built-in), the user folder (editor
/// creations) and imported packages.
class ContentDatabase {
public:
    void reload();

    // Planets
    std::vector<const PlanetData*> planets() const;
    const PlanetData* planet(const std::string& id) const;
    /// The planets of the campaign (levels to play, no teaser) in the order they are
    /// played: each after the planets (or the level's planet) its `unlock` waits for, the
    /// easiest first among those open at the same time, then the nearest to the star.
    std::vector<const PlanetData*> campaignPlanets() const;

    // Levels
    std::vector<const LevelEntry*> levels() const;
    /// The campaign of a planet: only the levels its planet file declares, in that order.
    /// Levels made in the editor or imported alone stay in the WORKSHOP even when their
    /// "planet" field names this planet (it only chooses their look).
    std::vector<const LevelEntry*> levelsForPlanet(const std::string& planetId) const;
    /// Planet whose campaign lists this level ("" = a WORKSHOP level).
    std::string campaignPlanetOf(const std::string& levelId) const;
    const LevelEntry* level(const std::string& id) const;
    std::optional<LevelData> loadLevel(const std::string& id, std::string* error = nullptr) const;
    bool saveUserLevel(const LevelData& level);
    bool deleteUserLevel(const std::string& id);
    bool levelIdExists(const std::string& id) const { return level(id) != nullptr; }
    /// 2.45: a campaign level reads this file (a revision downloaded from Cosmic Online,
    /// game/online/CampaignRevisions.h) instead of its bundled one: same id, still a built-in
    /// level; its name and description come from that file. An empty path: the bundled file
    /// again. Kept across reload(). False for a level that is not a built-in one, or a file
    /// that is not a level of that id.
    bool setCampaignRevision(const std::string& levelId, const fs::path& file);
    /// The file the game shipped for this built-in level ("" for any other level).
    fs::path bundledLevelPath(const std::string& levelId) const;

    // Enemies. Native (built-in) definitions are the ones the campaign uses: they can be
    // placed in any level but never replaced; a user file or a package that reuses a native
    // id is ignored (the editor saves a copy under a new id instead).
    std::vector<const EnemyDefinition*> enemies() const;
    const EnemyDefinition* enemy(const std::string& id) const;
    bool isNativeEnemy(const std::string& id) const;
    /// Fails for a native id.
    bool saveUserEnemy(const EnemyDefinition& def);
    bool deleteUserEnemy(const std::string& id);
    /// A free id derived from `base` (slugified) that is not used by any enemy.
    std::string freeEnemyId(const std::string& base) const;

    // Enemy revisions (2.45, ADMINISTRATION › Ennemis, docs/online/ENEMY_REVISIONS.md): the
    // definitions of native enemies published online by an administrator. Never global: a
    // level of the campaign plays a revision only through its binding (`metadata.enemyRevisions`
    // of its file, {"<enemy id>": <revision>}, part of its fingerprint), so every revision of a
    // level keeps the definitions it was published with (records, replays, ghosts, challenges).
    // Any other level (community, the player's own, a campaign level without that binding)
    // plays the game's own definition (enemy()). Kept across reload(); thread-safe.
    /// Adds (or replaces) revision `revision` of the native enemy `def.id`. False for a
    /// non-native id or a revision below 1.
    bool setEnemyRevision(int revision, EnemyDefinition def);
    void removeEnemyRevision(const std::string& id, int revision);
    const EnemyDefinition* enemyRevision(const std::string& id, int revision) const;
    /// The binding of a level: {} unless it is a built-in level (campaign); only native ids.
    std::map<std::string, int> enemyBindingOf(const std::string& levelId, const Json& metadata) const;
    std::map<std::string, int> enemyBindingOf(const LevelData& level) const { return enemyBindingOf(level.id, level.metadata); }
    /// The definition `id` plays with this binding: the bound revision (nullptr when this game
    /// does not have it), else the game's own definition.
    const EnemyDefinition* enemyFor(const std::string& id, const std::map<std::string, int>& binding) const;
    const EnemyDefinition* enemyFor(const std::string& id, const LevelData& level) const { return enemyFor(id, enemyBindingOf(level)); }

    // Powers (native powers cannot be replaced by packages either)
    std::vector<const PowerDefinition*> powers() const;
    const PowerDefinition* power(const std::string& id) const;
    bool isNativePower(const std::string& id) const { return nativePowers_.count(id) > 0; }

    // Effects (particle presets) merged from every source
    const Json& effects() const { return effects_; }
    const Json& playerConfig() const { return playerConfig_; }

    // Packages
    const std::vector<PackageInfo>& packages() const { return packages_; }
    const PackageInfo* package(const std::string& id) const;
    bool removePackage(const std::string& id);

private:
    void loadPlanetsFrom(const fs::path& dir, const fs::path& assetRoot);
    void loadLevelFile(const fs::path& file, ContentSource src, const std::string& pkg, const fs::path& assetRoot);
    bool applyCampaignRevision(LevelEntry& e, const fs::path& file);
    /// A user level copied from an imported package keeps using that package's assets.
    void resolveAssetPackage(LevelEntry& e) const;
    void loadEnemiesFrom(const fs::path& dir, ContentSource src, const fs::path& assetRoot);
    void loadPowersFrom(const fs::path& dir, bool native);
    void loadEffectsFrom(const fs::path& dir, bool native);
    void loadPackage(const fs::path& root);

    std::map<std::string, PlanetData> planets_;
    std::map<std::string, LevelEntry> levels_;
    std::map<std::string, fs::path> campaignRevisions_;  // level id -> downloaded revision (2.45)
    std::map<std::string, EnemyDefinition> enemies_;
    /// Enemy revisions published online (2.45): id -> revision -> definition. Guarded (a
    /// revision is prepared off the main thread while the game may download another one).
    std::map<std::string, std::map<int, EnemyDefinition>> enemyRevisions_;
    std::shared_ptr<std::mutex> enemyRevisionsMutex_ = std::make_shared<std::mutex>();
    std::map<std::string, PowerDefinition> powers_;
    std::vector<PackageInfo> packages_;
    std::set<std::string> nativePowers_;
    std::set<std::string> nativeEffects_;
    Json effects_ = Json::object();
    Json playerConfig_ = Json::object();
};

}  // namespace cosmic
