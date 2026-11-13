#pragma once

#include <map>
#include <string>
#include <vector>

#include "packaging/Package.h"

namespace cosmic {

enum class ConflictKind { Level, Enemy, Power, Package };

struct ImportConflict {
    ConflictKind kind;
    std::string id;
    std::string detail;
};

/// Everything learned (and verified) about a package before installing it.
struct PackageInspection {
    fs::path file;
    bool valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> checks;  // human readable list of passed verifications
    Json manifest;
    std::string packageId, name, author, version, description, levelId, engineVersion, minEngineVersion;
    std::optional<LevelData> level;
    std::optional<PlanetData> planet;
    std::vector<EnemyDefinition> enemies;
    std::vector<PowerDefinition> powers;
    std::vector<ImportConflict> conflicts;
    size_t fileCount = 0;
    uint64_t totalSize = 0;
    std::vector<uint8_t> thumbnail;
    std::map<std::string, std::vector<uint8_t>> files;  // verified contents (path under COSMIC_LEVEL/)
};

enum class ConflictPolicy {
    Rename,   // keep existing content, rename the imported ids
    Replace,  // the imported content replaces the existing package / ids
};

/// Validates and installs .cplanet packages: structure, versions, file hashes and
/// sizes, dangerous paths (absolute, "..", devices, symlinks), file types, zip bombs,
/// identifier conflicts. Nothing from a package is ever executed.
class PackageImporter {
public:
    explicit PackageImporter(const ContentDatabase& content) : content_(content) {}
    PackageInspection inspect(const fs::path& file);
    /// Installs a valid inspection into the user's packages folder.
    bool install(PackageInspection& insp, ConflictPolicy policy, std::string* installedLevelId, std::string* error);

private:
    const ContentDatabase& content_;
};

}  // namespace cosmic
