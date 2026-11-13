#pragma once

#include <map>
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
    fs::path assetRoot;  // package assets folder (empty for builtin / user levels)
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

    // Enemies (user definitions override built-in ones with the same id)
    std::vector<const EnemyDefinition*> enemies() const;
    const EnemyDefinition* enemy(const std::string& id) const;
    bool saveUserEnemy(const EnemyDefinition& def);
    bool deleteUserEnemy(const std::string& id);

    // Powers
    std::vector<const PowerDefinition*> powers() const;
    const PowerDefinition* power(const std::string& id) const;

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
    void loadEnemiesFrom(const fs::path& dir, ContentSource src, const fs::path& assetRoot);
    void loadPowersFrom(const fs::path& dir);
    void loadEffectsFrom(const fs::path& dir);
    void loadPackage(const fs::path& root);

    std::map<std::string, PlanetData> planets_;
    std::map<std::string, LevelEntry> levels_;
    std::map<std::string, EnemyDefinition> enemies_;
    std::map<std::string, PowerDefinition> powers_;
    std::vector<PackageInfo> packages_;
    Json effects_ = Json::object();
    Json playerConfig_ = Json::object();
};

}  // namespace cosmic
