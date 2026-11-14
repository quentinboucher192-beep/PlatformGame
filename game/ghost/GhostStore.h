#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Paths.h"
#include "game/ghost/Ghost.h"

namespace cosmic {

/// A replay file the player can race against, with what it represents.
struct GhostEntry {
    GhostMeta meta;
    fs::path file;      // the replay (".cprz", the same container as the uploads)
    uint64_t bytes = 0;
};

/// The ghosts kept on this computer (user folder, "ghosts/"):
///   local/<level>/pb, prev, last   the player's best run, the one it replaced, the last
///                                  finished run (per exact version of the level);
///   cache/<record>                 replays downloaded from the rankings (bounded, the
///                                  oldest go first, favourites stay);
///   files/                         replays the player put there or chose ("Importer").
/// Every replay keeps its own ".json" description next to it; the replay itself is never
/// changed. Everything read back is checked (size, container, fields): a file can be
/// damaged or hand-made.
class GhostStore {
public:
    static constexpr uint64_t kMaxReplayBytes = 4ull * 1024 * 1024;

    GhostStore() = default;
    explicit GhostStore(fs::path root) { open(std::move(root)); }
    /// Uses `root` (normally the user folder's "ghosts/") and reads the favourites.
    void open(fs::path root);
    const fs::path& root() const { return root_; }

    /// Folder name of a level's exact version ("noyau_01-1a2b3c4d5e6f").
    static std::string levelKey(const std::string& levelId, const std::string& levelHash);

    /// After a finished run: becomes "last"; with `newBest` it also becomes "pb" and the
    /// previous best becomes "prev". Returns false if nothing could be written.
    bool recordRun(const ReplayData& replay, GhostMeta meta, bool newBest);

    /// The player's ghosts of a level (any version: `meta.levelHash` tells which one).
    std::vector<GhostEntry> localGhosts(const std::string& levelId) const;

    /// A downloaded replay (raw ".cprz" bytes, checked here) and its description.
    bool storeDownload(const std::vector<uint8_t>& bytes, GhostMeta meta, std::string* error = nullptr);
    std::optional<GhostEntry> cached(const std::string& recordId) const;
    /// Every cached download of a level.
    std::vector<GhostEntry> cachedFor(const std::string& levelId) const;
    /// Removes the oldest downloads beyond `maxBytes` (favourites are kept).
    void trimCache(uint64_t maxBytes);
    uint64_t cacheBytes() const;

    /// Replays chosen by the player: copies `source` into files/ (checked first).
    std::optional<GhostEntry> importFile(const fs::path& source, std::string* error = nullptr);
    std::vector<GhostEntry> files() const;

    /// Favourite downloaded replays (kept in the cache, listed first).
    bool isFavorite(const std::string& recordId) const;
    void setFavorite(const std::string& recordId, bool on);
    std::vector<std::string> favorites() const { return favorites_; }

    /// Reads and decodes a replay (size limit, container checks). Off the main thread for
    /// the big ones (see GhostLoader).
    static std::optional<ReplayData> loadReplay(const fs::path& file, std::string* error = nullptr);

private:
    void loadFavorites();
    void saveFavorites() const;
    static std::optional<GhostEntry> readEntry(const fs::path& replayFile);
    static bool writeEntry(const fs::path& replayFile, const std::vector<uint8_t>& bytes, const GhostMeta& meta);
    static std::string safeName(const std::string& s);

    fs::path root_;
    std::vector<std::string> favorites_;
};

}  // namespace cosmic
