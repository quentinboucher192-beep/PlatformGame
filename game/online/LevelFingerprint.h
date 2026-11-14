#pragma once

#include <map>
#include <optional>
#include <string>

#include "game/levels/LevelData.h"

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;
struct LevelData;

/// Identity of a level revision for online rankings: id, integer revision
/// (`metadata.revision` of the level file, 1 by default) and SHA-256 of the canonical
/// JSON of the file (parsed then re-serialised: independent of whitespace and line
/// endings, so Windows and Linux builds agree).
/// Version of the simulation rules. Bump it whenever a code change can alter how a run
/// plays out (physics, AI, damage, timers...): every ranked level then gets a new
/// fingerprint, so records and replays of incompatible engines are never mixed.
constexpr int kSimulationVersion = 4;  // 4: smarter bosses (prediction, combos, new attacks)

struct LevelFingerprint {
    std::string levelId;
    int version = 1;
    std::string hash;
};

std::optional<LevelFingerprint> fingerprintLevelFile(const fs::path& file, std::string* error = nullptr);
std::optional<LevelFingerprint> fingerprintLevelJson(const Json& level);
/// Fingerprint of a ranked level: the level, every enemy and power definition it uses
/// (recursively), the player configuration and kSimulationVersion. Two runs with the same
/// fingerprint, seed and inputs play out identically.
std::optional<LevelFingerprint> fingerprintRankedLevel(const ContentDatabase& content, const Json& level);

/// Fingerprint of what a community level plays like: geometry, objects, events, cinematics,
/// timer, start powers, the definitions of its enemies and powers, the player and the
/// simulation version - but not its id, name, description, author, music or metadata, and
/// enemy / power references are replaced by a digest of their definition. So a copy of the
/// level installed on another computer (where the importer may rename the level or its
/// definitions) has the same fingerprint as the author's: the author's completion replay and
/// the players' replays are matched with it (Cosmic Online, community levels).
std::string fingerprintGameplay(const ContentDatabase& content, const LevelData& level);

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
/// Manifest entry of one level (also used by the integration tests with their own level).
Json onlineManifestEntry(const ContentDatabase& content, const LevelData& level, const LevelFingerprint& fp);

}  // namespace cosmic
