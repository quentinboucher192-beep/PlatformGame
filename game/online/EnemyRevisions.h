#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

class ContentDatabase;

/// A revision of a native enemy published online by an administrator (2.45, ADMINISTRATION ›
/// Ennemis, docs/online/ENEMY_REVISIONS.md): its definition file, downloaded from Cosmic Online.
struct EnemyRevisionFile {
    std::string enemyId;
    int revision = 0;
    std::string sha256;  // of the file's bytes
    int64_t size = 0;
    std::string minGameVersion;
    std::string path;  // where to download it (server path)
};

/// The enemy revisions kept on this computer (<user>/online/enemies/): the last manifest read
/// (GET /enemies/revisions) and the definitions downloaded and checked (SHA-256 of the bytes =
/// the server's, the game's format, this id, a native of this game). Every revision is kept:
/// a campaign level revision names the revisions it plays (its binding), and an older level
/// revision (a challenge, a replay) keeps playing the definitions it was published with. They
/// are never global: ContentDatabase::enemyFor() gives them to the levels bound to them only.
/// Works offline after one reading; a revision made for a newer game is ignored.
class EnemyRevisionStore {
public:
    explicit EnemyRevisionStore(fs::path dir = {}) : dir_(std::move(dir)) {}
    void setDir(fs::path dir) { dir_ = std::move(dir); }
    const fs::path& dir() const { return dir_; }
    /// Reads index.json (nothing kept when it is missing or unreadable).
    void load();
    bool save() const;

    /// The manifest just received from `server`: kept, and the revisions this game does not have
    /// yet are returned, to download then install(). A revision for a newer game, or of an enemy
    /// that is not a native of this game, is skipped. Another server than the one of the files
    /// kept: they are forgotten (its revision numbers are its own), the content too.
    std::vector<EnemyRevisionFile> applyManifest(const Json& data, const std::string& server, ContentDatabase& content, const std::string& gameVersion);
    /// Checks a downloaded definition and keeps it (online/enemies/<id>-<revision>.json); the
    /// content gets it at once. On failure nothing is kept and `error` says why.
    bool install(ContentDatabase& content, const EnemyRevisionFile& f, const std::string& bytes, std::string* error = nullptr);
    /// Gives the content every revision kept for `server` (startup, offline too; again after each
    /// reading). A damaged file, or one made for a newer game, is skipped. Returns how many.
    int applyTo(ContentDatabase& content, const std::string& server, const std::string& gameVersion) const;
    /// The current revision online of a native enemy (0: the game's own definition), as the last
    /// manifest of `server` says.
    int currentRevision(const std::string& enemyId, const std::string& server) const;
    /// Every current revision online (enemy id -> revision), as the last manifest of `server` says.
    std::map<std::string, int> currentRevisions(const std::string& server) const;

    /// A definition file as the game reads it: the enemy format, of `enemyId`, a native of this
    /// game. nullopt (and `error`) otherwise.
    static std::optional<EnemyDefinition> parseDefinition(const ContentDatabase& content, const std::string& enemyId, const std::string& text,
                                                          std::string* error = nullptr);

private:
    fs::path fileOf(const std::string& enemyId, int revision) const;
    void forget(ContentDatabase& content);

    fs::path dir_;
    /// {"format", "server", "current": {id: revision}, "files": {"<id>:<revision>": {enemy_id,
    ///  revision, sha256, size, min_game_version}}}
    Json index_ = Json::object();
};

}  // namespace cosmic
