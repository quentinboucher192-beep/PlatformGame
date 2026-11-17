#pragma once

// 2.63.5: the levels a team can play (docs/COOP.md § 9): the EXTRÊME levels of the campaign and
// the EXTRÊME (one life) community levels installed from Cosmic Online, each in one version. A
// room plays one level in one version for everybody: a community level is found on a PC by its
// online id, its published version and its gameplay fingerprint, never by its name.

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;

/// An EXTRÊME level (one life), as the co-op lobby offers it.
struct CoopLevel {
    std::string id;  // the level on this PC
    std::string name, planet, planetId, author;
    std::vector<std::string> bosses;
    bool community = false;
    std::string communityId;  // community: its Cosmic Online id
    int version = 0;          // community: its published version
    std::string hash;         // community: its gameplay fingerprint (the published one)
    fs::path package;         // community: its package kept on this PC ("" : none)
};

/// The package downloaded for a community level's version (NIVEAUX EN LIGNE, the lobby).
fs::path communityPackagePath(const fs::path& downloads, const std::string& communityId, int version);

/// The campaign's EXTRÊME levels, then the community levels installed from Cosmic Online
/// (`installs`: local id → {id, version, level_hash, title}) that are EXTRÊME, one life, and
/// whose copy is the published version (its fingerprint is the published `level_hash`).
/// Parses every level file: called off the main thread.
std::vector<CoopLevel> findCoopLevels(const ContentDatabase& content, const Json& installs, const fs::path& downloads);

/// The community level of a room on this PC: the same online id, the same version and the
/// same fingerprint; nullptr otherwise (another version would not play the same run).
const CoopLevel* findRoomLevel(const std::vector<CoopLevel>& levels, const std::string& communityId, int version, const std::string& hash);

}  // namespace cosmic
