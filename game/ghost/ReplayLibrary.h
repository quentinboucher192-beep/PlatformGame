#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/Replay.h"

namespace cosmic {

class ContentDatabase;

/// A replay downloaded from Cosmic Online to be watched again on this computer, without a
/// connection nor an account (2.45, « Replays téléchargés », Collection › Replays). The
/// level and its exact version are read from the replay itself, never from the server's
/// answer; the rest (player, category, dates) is what the screen it came from showed.
struct DownloadedReplay {
    std::string id;            // "record-<id>", "community-<id>", "race-<id>", "challenge-<id>"… (a safe file name)
    std::string source;        // "record", "community", "race", "challenge"
    std::string player;        // its player's pseudonym
    std::string playerId;
    std::string levelId;       // the level installed here it plays on
    std::string levelHash;     // exact version of the level the run was made on (from the replay)
    std::string levelName;     // the level's name when downloaded (shown if it is no longer installed)
    std::string category;      // « Any% », the challenge's title, « Course à la vérification – 87,5 % »…
    int64_t timeMs = 0;
    std::string achievedAt;    // date of the run (ISO-8601 from the server), may be empty
    int64_t downloadedAt = 0;  // seconds since 1970 (UTC)
    std::string gameVersion;   // version of the game that recorded it
    uint64_t bytes = 0;
    std::string file;          // its file in the folder ("<id>.cprz")

    Json toJson() const;
    /// Every field is checked (the index is a file of the user folder: it may be edited).
    static std::optional<DownloadedReplay> fromJson(const Json& j);
};

/// The level of a downloaded replay on this computer.
enum class ReplayLevelMatch {
    Same,           // installed, in the version the run was made on: it plays exactly
    OtherRevision,  // installed, but in another version: the run would not play the same
    NotInstalled,   // missing from this game
};
/// `installedHashes`: the exact versions of the installed level as the replay viewer checks
/// them (ranked and gameplay fingerprints); `installed` false when the level is missing.
ReplayLevelMatch matchReplayLevel(const std::string& replayHash, bool installed, const std::vector<std::string>& installedHashes);
/// Those versions of an installed level (nullopt when it is not installed). Reads the level:
/// call it off the main thread.
std::optional<std::vector<std::string>> installedLevelVersions(const ContentDatabase& content, const std::string& levelId);

/// The downloaded replays (user folder, "replays/downloaded/"): one "<id>.cprz" per replay,
/// the bytes exactly as Cosmic Online sent them, and "index.json" describing them. Bounded
/// (kMaxCount replays, kMaxTotalBytes in all); nothing is ever removed without the player.
/// Every replay is decoded before it is kept and again when it is read back.
class ReplayLibrary {
public:
    static constexpr size_t kMaxCount = 200;
    static constexpr uint64_t kMaxTotalBytes = 256ull << 20;
    /// A replay is at most 4 MB once decompressed (ReplayData::kMaxRawBytes).
    static constexpr uint64_t kMaxFileBytes = 5ull << 20;

    ReplayLibrary() = default;
    explicit ReplayLibrary(fs::path root) { open(std::move(root)); }
    /// Uses `root` and reads its index (entries whose file is missing are left out).
    void open(fs::path root);
    const fs::path& root() const { return root_; }

    /// Newest download first.
    const std::vector<DownloadedReplay>& entries() const { return entries_; }
    const DownloadedReplay* find(const std::string& id) const;
    bool has(const std::string& id) const { return find(id) != nullptr; }
    uint64_t totalBytes() const;
    /// Why one more replay of `bytes` cannot be kept (French), empty when it can.
    std::string roomFor(uint64_t bytes) const;

    /// `source` + "-" + the server's id: letters, digits, '-' and '_' only, at most 96
    /// characters; empty when `remoteId` is not such an id (never turned into a file name).
    static std::string makeId(const std::string& source, const std::string& remoteId);

    /// Checks downloaded bytes (size, container, decoding) and completes `meta` from the
    /// replay itself (level, exact version, duration, game version). No state: it runs on a
    /// worker thread.
    static std::optional<DownloadedReplay> check(const std::vector<uint8_t>& bytes, DownloadedReplay meta, std::string* error = nullptr);
    /// Writes the file of a checked replay into `root` (worker thread).
    static bool writeFile(const fs::path& root, const DownloadedReplay& meta, const std::vector<uint8_t>& bytes, std::string* error = nullptr);
    /// Lists a replay whose file was written and saves the index. Refused beyond the limits
    /// (its file is then removed).
    bool commit(DownloadedReplay meta, std::string* error = nullptr);
    /// check + writeFile + commit at once.
    bool add(const std::vector<uint8_t>& bytes, DownloadedReplay meta, std::string* error = nullptr);
    bool remove(const std::string& id, std::string* error = nullptr);

    /// Reads and decodes a downloaded replay; its level becomes the one noted at download.
    std::optional<ReplayData> load(const std::string& id, std::string* error = nullptr) const;
    /// The same from a file (worker thread): refused when it is not the version noted.
    static std::optional<ReplayData> loadFile(const fs::path& file, const std::string& expectedHash, std::string* error = nullptr);
    fs::path pathOf(const DownloadedReplay& e) const { return root_ / e.file; }

private:
    bool saveIndex() const;
    void sort();

    fs::path root_;
    std::vector<DownloadedReplay> entries_;
};

}  // namespace cosmic
