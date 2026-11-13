#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/levels/LevelData.h"
#include "game/planets/PlanetData.h"
#include "game/powers/PowerDefinition.h"

namespace cosmic {

class ContentDatabase;
class ResourceManager;

/// Constants of the .cplanet package format (a ZIP archive).
///
///   COSMIC_LEVEL/manifest.json      package description, file list with SHA-256
///   COSMIC_LEVEL/level.json         the level
///   COSMIC_LEVEL/planet.json        its planet (merged if the planet already exists)
///   COSMIC_LEVEL/enemies/<id>.json  enemy definitions used by the level
///   COSMIC_LEVEL/powers/<id>.json   power definitions used by the level
///   COSMIC_LEVEL/effects/effects.json particle presets
///   COSMIC_LEVEL/assets/...         textures, sprites, animations, sounds, music, fonts
///   COSMIC_LEVEL/metadata/...       thumbnail and export information
///
/// Packages never contain executable code: only data files with whitelisted
/// extensions are accepted on import.
namespace PackageFormat {
constexpr const char* kRoot = "COSMIC_LEVEL/";
constexpr const char* kExtension = ".cplanet";
constexpr int kFormatVersion = 1;
constexpr uint64_t kMaxFileSize = 128ull << 20;    // 128 MB per file
constexpr uint64_t kMaxTotalSize = 512ull << 20;   // 512 MB uncompressed
constexpr size_t kMaxEntries = 4000;
constexpr double kMaxCompressionRatio = 250.0;     // zip-bomb guard
bool allowedExtension(const std::string& path);
/// Compares "1.2.3" style versions: <0 if a<b, 0 if equal, >0 if a>b.
int compareVersions(const std::string& a, const std::string& b);
std::string engineVersion();
}  // namespace PackageFormat

/// Everything a level needs to run on another computer.
struct DependencySet {
    std::set<std::string> assets;   // paths relative to an assets root
    std::set<std::string> enemies;  // enemy definition ids
    std::set<std::string> powers;   // power definition ids
    std::string planet;
    std::vector<std::string> warnings;
};

/// Walks a level (objects, events, cinematics, enemies, powers, backgrounds...) and
/// lists every asset and definition it depends on, recursively.
class DependencyCollector {
public:
    DependencyCollector(const ContentDatabase& content, ResourceManager& res) : content_(content), res_(res) {}
    DependencySet collect(const LevelData& level);
    /// Assets the runtime itself uses for any level (player, effects, HUD, fonts...).
    static std::vector<std::string> coreRuntimeAssets();

private:
    void asset(const std::string& rel);
    void animation(const std::string& rel);
    void enemy(const std::string& id);
    void power(const std::string& id);
    void sound(const std::string& rel) { asset(rel); }

    const ContentDatabase& content_;
    ResourceManager& res_;
    DependencySet out_;
};

}  // namespace cosmic
