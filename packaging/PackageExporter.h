#pragma once

#include <string>
#include <vector>

#include "packaging/Package.h"

namespace cosmic {

struct ExportOptions {
    std::string packageName;
    std::string author;
    std::string version = "1.0";
    std::string description;
    fs::path destination;               // full path of the .cplanet file
    std::vector<uint8_t> thumbnailPng;  // optional preview image
};

struct ExportItem {
    std::string archivePath;  // relative to COSMIC_LEVEL/
    fs::path source;          // file on disk (empty when `data` is used)
    std::vector<uint8_t> data;
    std::string kind;         // level, planet, enemy, power, effects, asset, metadata
    uint64_t size = 0;
    std::string id;           // definition id (enemies, powers)
};

/// Result of the dependency check, shown in the export dialog before writing.
struct ExportPlan {
    std::string packageId;
    std::vector<ExportItem> items;
    std::vector<std::string> missing;   // referenced but not found (blocking)
    std::vector<std::string> warnings;
    uint64_t totalSize = 0;
    size_t countKind(const std::string& kind) const;
    bool ok() const { return missing.empty(); }
};

/// Builds self-contained .cplanet packages: the level plus every file it depends on,
/// with a manifest listing sizes and SHA-256 fingerprints.
class PackageExporter {
public:
    PackageExporter(const ContentDatabase& content, ResourceManager& res) : content_(content), res_(res) {}

    /// `assetRoot` is the package assets folder of an imported level being re-exported.
    ExportPlan plan(const LevelData& level, const ExportOptions& opt, const fs::path& assetRoot = {});
    bool write(const LevelData& level, const ExportPlan& plan, const ExportOptions& opt, std::string* error);
    static std::string makePackageId(const LevelData& level, const std::string& author);

private:
    const ContentDatabase& content_;
    ResourceManager& res_;
};

}  // namespace cosmic
