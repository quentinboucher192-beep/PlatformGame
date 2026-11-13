#pragma once

#include <map>
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

    // Levels
    std::vector<const LevelEntry*> levels() const;
    std::vector<const LevelEntry*> levelsForPlanet(const std::string& planetId) const;
    const LevelEntry* level(const std::string& id) const;
    std::optional<LevelData> loadLevel(const std::string& id, std::string* error = nullptr) const;
    bool saveUserLevel(const LevelData& level);
    bool deleteUserLevel(const std::string& id);
    bool levelIdExists(const std::string& id) const { return level(id) != nullptr; }

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
    /// A user level copied from an imported package keeps using that package's assets.
    void resolveAssetPackage(LevelEntry& e) const;
    void loadEnemiesFrom(const fs::path& dir, ContentSource src, const fs::path& assetRoot);
    void loadPowersFrom(const fs::path& dir, bool native);
    void loadEffectsFrom(const fs::path& dir, bool native);
    void loadPackage(const fs::path& root);

    std::map<std::string, PlanetData> planets_;
    std::map<std::string, LevelEntry> levels_;
    std::map<std::string, EnemyDefinition> enemies_;
    std::map<std::string, PowerDefinition> powers_;
    std::vector<PackageInfo> packages_;
    std::set<std::string> nativePowers_;
    std::set<std::string> nativeEffects_;
    Json effects_ = Json::object();
    Json playerConfig_ = Json::object();
};

}  // namespace cosmic
