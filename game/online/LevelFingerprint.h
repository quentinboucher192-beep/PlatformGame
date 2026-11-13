#pragma once

#include <map>
#include <optional>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;

/// Identity of a level revision for online rankings: id, integer revision
/// (`metadata.revision` of the level file, 1 by default) and SHA-256 of the canonical
/// JSON of the file (parsed then re-serialised: independent of whitespace and line
/// endings, so Windows and Linux builds agree).
struct LevelFingerprint {
    std::string levelId;
    int version = 1;
    std::string hash;
};

std::optional<LevelFingerprint> fingerprintLevelFile(const fs::path& file, std::string* error = nullptr);
std::optional<LevelFingerprint> fingerprintLevelJson(const Json& level);

/// Fingerprints of the built-in levels (cached after the first computation).
class LevelFingerprints {
public:
    const LevelFingerprint* get(const ContentDatabase& content, const std::string& levelId);
    void clear() { cache_.clear(); }

private:
    std::map<std::string, std::optional<LevelFingerprint>> cache_;
};

/// Builds the manifest of every built-in level (fingerprint, totals, spawn and exit, time
/// limits) used to register level revisions on the server (`--online-manifest`). The
/// spawn is measured by running the intro of the level in a headless world.
Json buildOnlineManifest(ContentDatabase& content, std::string* error = nullptr);

}  // namespace cosmic
