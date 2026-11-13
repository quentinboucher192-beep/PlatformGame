#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// Versioned JSON save file with forward migrations.
///
/// On disk: { "format": "cosmic.<kind>", "version": N, "savedAt": "...", "data": {...} }
/// `migrations[i]` upgrades the data of version i+1 to version i+2. Loading an older
/// file runs every needed migration and keeps a backup of the original
/// (`<file>.v<old>.bak`). Files from a newer game version are not overwritten.
class SaveFile {
public:
    using Migration = std::function<Json(const Json&)>;

    SaveFile(std::string kind, int currentVersion, std::vector<Migration> migrations = {})
        : kind_(std::move(kind)), version_(currentVersion), migrations_(std::move(migrations)) {}

    /// Returns the (migrated) data, or nullopt if the file does not exist / is corrupt
    /// (a corrupt file is renamed to `.corrupt` so the player can recover it).
    std::optional<Json> load(const fs::path& path, bool* migrated = nullptr) const;
    bool save(const fs::path& path, const Json& data) const;

    /// Pure function used by load(): applies migrations to an envelope.
    std::optional<Json> migrate(const Json& envelope, int* fromVersion = nullptr) const;

    int version() const { return version_; }
    const std::string& kind() const { return kind_; }

private:
    std::string kind_;
    int version_;
    std::vector<Migration> migrations_;
};

}  // namespace cosmic
