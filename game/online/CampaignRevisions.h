#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;

/// A revision of a campaign level published online by an administrator (2.45, ADMINISTRATION ›
/// Campagne): its file is downloaded from Cosmic Online and played instead of the bundled one.
struct CampaignRevisionFile {
    std::string levelId;
    int version = 0;
    std::string contentHash;  // ranked fingerprint (fingerprintRankedLevel), as the server knows it
    std::string sha256;       // of the file's bytes
    int64_t size = 0;
    std::string minGameVersion;
    std::string path;  // where to download it (server path)
};

/// The campaign revisions kept on this computer (<user>/online/campaign/): the last manifest
/// received (GET /campaign/revisions: which revision of each level is current online) and the
/// files downloaded and checked. It works offline: the last revision downloaded keeps being
/// played. A file is only used when its SHA-256 is the server's, it is a level of that id and
/// revision, and its ranked fingerprint in THIS game is the server's content hash; otherwise
/// (broken, unknown, made for another version of the game) the bundled level is played.
/// Nothing here changes the simulation: a revision is a level file like the bundled ones.
class CampaignRevisionStore {
public:
    explicit CampaignRevisionStore(fs::path dir = {}) : dir_(std::move(dir)) {}
    void setDir(fs::path dir) { dir_ = std::move(dir); }
    const fs::path& dir() const { return dir_; }
    /// Reads index.json (nothing kept when it is missing or unreadable).
    void load();
    bool save() const;

    /// The manifest just received from `server`: kept (offline use), and the revisions this game
    /// should have and has not yet (the current ones, and those a challenge still plays) are
    /// returned, to download then install(). A revision for a newer game is skipped; a level
    /// whose bundled revision is newer than every one the server knows keeps its own.
    std::vector<CampaignRevisionFile> applyManifest(const Json& data, const std::string& server, const ContentDatabase& content,
                                                    const std::string& gameVersion);
    /// Checks a downloaded file and keeps it (online/campaign/<level>-<version>.json). On
    /// failure nothing is kept and `error` says why.
    bool install(const ContentDatabase& content, const CampaignRevisionFile& f, const std::string& bytes, std::string* error = nullptr);
    /// The files of the current revisions to play instead of the bundled ones (level id ->
    /// file), as the last manifest of `server` says; every file is checked again (its SHA-256,
    /// and its fingerprint when the game changed since it was checked). Levels absent: bundled.
    std::map<std::string, fs::path> currentFiles(const ContentDatabase& content, const std::string& server, const std::string& gameVersion);
    /// A file of this level whose ranked fingerprint in this game is `hash`: the bundled one,
    /// or a revision kept here (a challenge keeps the revision it was drawn on).
    std::optional<fs::path> fileFor(const ContentDatabase& content, const std::string& levelId, const std::string& hash) const;
    /// The difficulty the publisher's game computed for the current revision (null when none).
    const Json* ratingOf(const std::string& levelId) const;
    /// Removes the files that are neither current nor still played by a challenge.
    int prune();

    /// Ranked fingerprint of a level file (tests, checks); "" when it is not a level.
    static std::string rankedHashOf(const ContentDatabase& content, const std::string& text);
    /// The revision number of a level file (metadata.revision, 1 by default; 0 unreadable).
    static int revisionOf(const fs::path& file);

private:
    fs::path fileOf(const std::string& levelId, int version) const;

    fs::path dir_;
    /// {"format", "server", "current": {id: {version, content_hash, max_version, file?, rating?}},
    ///  "pinned": {id: [hash…]}, "files": {"<id>:<version>": {content_hash, sha256, size, checked_with}}}
    Json index_ = Json::object();
    mutable std::map<std::string, std::string> bundledHash_;  // level id -> fingerprint of its bundled file
};

}  // namespace cosmic
