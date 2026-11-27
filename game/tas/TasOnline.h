#pragma once

// The machine's runs published on Cosmic Online (docs/TAS.md « Courses publiées en ligne »): an
// administrator improves a run on his own computer (ADMINISTRATION › TAS) and publishes it; every
// game reads the list now and then (GET /api/v1/tas, a few hundred bytes per run), downloads the
// runs faster than the ones it already has, and PLAYS EACH ONE AGAIN before it ever shows it: on
// a worker thread, its replay by the normal replay playback, its time and its category checked
// (game/tas/TasVerify.h). A run that does not reproduce is ignored (and remembered, so that it is
// not downloaded again). The verified runs are kept in <saves>/tas_online/runs/ with the layout
// of data/tas: tasFor() picks the fastest valid run of the two (TasFile.h), offline too.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/OnlineTypes.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasVerify.h"

namespace cosmic {

class OnlineService;
class ContentDatabase;
class LevelFingerprints;

/// One run of the list GET /api/v1/tas.
struct TasOnlineEntry {
    std::string levelId;
    RunCategory category = RunCategory::AnyPercent;
    int revision = 0;  // the server's number of this publication (1, 2, ...)
    std::string levelHash;
    int levelVersion = 1;
    int simVersion = 0;
    int64_t timeMs = 0;
    std::string recordSha256, replaySha256;
    int64_t recordSize = 0, replaySize = 0;
    std::string recordPath, replayPath;  // server paths
    std::string publishedAt;

    std::string key() const { return levelId + "/" + categoryId(category); }
    /// Nullopt for anything incomplete or out of bounds (sizes, hashes, paths of this API).
    static std::optional<TasOnlineEntry> fromJson(const Json& j);
};

/// Plays downloaded runs again, one at a time, on its own thread with its own content (reloaded,
/// with this computer's online revisions, for each batch): never on the main thread.
class TasVerifier {
public:
    struct Job {
        TasOnlineEntry entry;
        TasRecord record;
        std::string recordText;  // the JSON as downloaded
        std::string replayBytes;
        std::string server;
        fs::path userDir;
    };
    struct Result {
        TasOnlineEntry entry;
        TasRecord record;
        std::string recordText, replayBytes;
        TasCheck check;
    };
    TasVerifier() = default;
    ~TasVerifier();
    TasVerifier(const TasVerifier&) = delete;
    TasVerifier& operator=(const TasVerifier&) = delete;

    void submit(Job job);
    /// Main thread: the checks finished since the last call.
    std::vector<Result> poll();
    /// A job queued or being played.
    bool busy() const;

private:
    void run();
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::vector<Result> done_;
    int running_ = 0;
    bool stop_ = false;
    std::atomic<bool> cancel_{false};  // read by verifyTasReplay: the destructor stops a check under way
    std::thread thread_;
};

/// The runs downloaded and their verdicts (<dir>/index.json), the verified ones in <dir>/runs/.
class TasOnlineStore {
public:
    explicit TasOnlineStore(fs::path dir = {}) : dir_(std::move(dir)) {}
    void setDir(fs::path dir) { dir_ = std::move(dir); }
    const fs::path& dir() const { return dir_; }
    /// Where the verified runs are (the layout of data/tas): tasDownloadedRoot().
    fs::path runsDir() const { return dir_ / "runs"; }
    void load();
    bool save() const;

    /// The list just received from `server` (GET /api/v1/tas → data): the runs to download, the
    /// ones of this game's simulation, for the level as this game has it now (`hashOf`: its
    /// ranked fingerprint, "" when unknown), never verified or refused at this revision, and
    /// faster than the fastest valid run already on this computer (shipped or downloaded).
    /// Another server than the one of the files kept: they are forgotten first.
    std::vector<TasOnlineEntry> wanted(const Json& data, const std::string& server, const std::function<std::string(const std::string&)>& hashOf);
    /// A downloaded run, before its check: the bytes are the server's (SHA-256 and size), the
    /// record reads and says what the list says. Nullopt (and why) otherwise.
    static std::optional<TasRecord> readDownload(const TasOnlineEntry& e, const std::string& recordText, const std::string& replayBytes,
                                                 std::string* why = nullptr);
    /// The verdict of a check: a verified run is written in runs/ (replacing a slower one), a
    /// refused one only remembered. Saves the index.
    void noteResult(const TasVerifier::Result& r);
    /// The same, refused before its check (download, bytes): remembered, never shown.
    void noteRefused(const TasOnlineEntry& e, const std::string& why);
    /// The verdict kept for a run of the list ("verified", "refused" or "").
    std::string statusOf(const TasOnlineEntry& e) const;
    std::string whyOf(const std::string& key) const;

private:
    fs::path dir_;
    /// {"format", "server", "runs": {"<level>/<CAT>": {revision, record_sha256, replay_sha256,
    ///  time_ms, status, why, checked_at}}}
    Json index_ = Json::object();
};

/// The game's side: the list read now and then, the runs downloaded one by one and checked on
/// the verifier's thread, the verdicts kept. Main thread (update()).
class TasOnline {
public:
    TasOnline(OnlineService& online, ContentDatabase& content, LevelFingerprints& fingerprints)
        : online_(online), content_(content), fingerprints_(fingerprints) {}
    /// <saves>/tas_online: the runs verified before (offline too) become visible to tasFor().
    void start(const fs::path& dir, bool fetch);
    /// Every frame: the list again every 6 hours (the first time a minute after the start),
    /// the downloads, the checks finished.
    void update(float dt);
    /// Reads the list now (ADMINISTRATION › TAS after a publication).
    void refresh();
    TasOnlineStore& store() { return store_; }
    bool busy() const { return listing_ || downloading_ || verifier_.busy() || !queue_.empty(); }

private:
    void next();
    OnlineService& online_;
    ContentDatabase& content_;
    LevelFingerprints& fingerprints_;
    TasOnlineStore store_;
    TasVerifier verifier_;
    std::deque<TasOnlineEntry> queue_;
    bool fetch_ = false, listing_ = false, downloading_ = false;
    float timer_ = 0.0f;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
