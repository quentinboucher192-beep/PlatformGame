#include "game/tas/TasOnline.h"

#include <algorithm>
#include <atomic>
#include <cctype>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineService.h"
#include "game/online/Replay.h"
#include "packaging/Sha256.h"

namespace cosmic {

namespace {

constexpr const char* kIndexFormat = "cosmic.tas.online";
/// The largest files accepted (the server's own bounds: docs/online/API.md « TAS »).
constexpr int64_t kMaxRecordBytes = 1024 * 1024;
constexpr int64_t kMaxReplayBytes = 1024 * 1024;
/// The list again after this long; after a failure, sooner.
constexpr float kListEvery = 6.0f * 3600.0f;
constexpr float kListRetry = 10.0f * 60.0f;
constexpr float kFirstList = 60.0f;

bool hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

/// A path of this API's runs ("/api/v1/tas/..."), nothing else.
bool tasPath(const std::string& p) {
    if (p.rfind("/api/v1/tas/", 0) != 0 || p.size() > 200 || p.find("..") != std::string::npos) return false;
    for (char c : p)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

}  // namespace

std::optional<TasOnlineEntry> TasOnlineEntry::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    TasOnlineEntry e;
    e.levelId = jstr(j, "level_id");
    if (e.levelId.empty() || e.levelId.size() > 64 || !Paths::isSafeRelative(e.levelId) || e.levelId.find('/') != std::string::npos) return std::nullopt;
    if (!categoryFromId(jstr(j, "category"), e.category)) return std::nullopt;
    e.revision = jget<int>(j, "revision", 0);
    e.levelHash = jstr(j, "level_hash");
    e.levelVersion = std::max(1, jget<int>(j, "level_version", 1));
    e.simVersion = jget<int>(j, "sim_version", 0);
    e.timeMs = jget<int64_t>(j, "time_ms", 0);
    e.recordSha256 = jstr(j, "record_sha256");
    e.replaySha256 = jstr(j, "replay_sha256");
    e.recordSize = jget<int64_t>(j, "record_size", 0);
    e.replaySize = jget<int64_t>(j, "replay_size", 0);
    e.recordPath = jstr(j, "record_path");
    e.replayPath = jstr(j, "replay_path");
    e.publishedAt = jstr(j, "published_at");
    if (e.revision <= 0 || !hex64(e.levelHash) || e.simVersion <= 0 || e.timeMs <= 0) return std::nullopt;
    if (!hex64(e.recordSha256) || !hex64(e.replaySha256)) return std::nullopt;
    if (e.recordSize <= 0 || e.recordSize > kMaxRecordBytes || e.replaySize <= 0 || e.replaySize > kMaxReplayBytes) return std::nullopt;
    if (!tasPath(e.recordPath) || !tasPath(e.replayPath)) return std::nullopt;
    return e;
}

// ------------------------------------------------------------------ the verifier's thread

TasVerifier::~TasVerifier() {
    {
        std::lock_guard<std::mutex> l(m_);
        stop_ = true;
        jobs_.clear();
    }
    cancel_ = true;  // a check under way stops at its next step
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void TasVerifier::submit(Job job) {
    {
        std::lock_guard<std::mutex> l(m_);
        jobs_.push_back(std::move(job));
        if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
    }
    cv_.notify_one();
}

std::vector<TasVerifier::Result> TasVerifier::poll() {
    std::lock_guard<std::mutex> l(m_);
    std::vector<Result> out = std::move(done_);
    done_.clear();
    return out;
}

bool TasVerifier::busy() const {
    std::lock_guard<std::mutex> l(m_);
    return running_ > 0 || !jobs_.empty() || !done_.empty();
}

void TasVerifier::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++running_;
        }
        Result res;
        res.entry = job.entry;
        res.record = job.record;
        // Its own content, as this computer plays the level (the online revisions downloaded):
        // nothing shared with the main thread.
        ContentDatabase content;
        content.reload();
        applyOnlineRevisions(content, job.userDir, job.server);
        std::string err;
        auto level = content.loadLevel(job.record.levelId, &err);
        LevelFingerprints fps;
        const LevelFingerprint* fp = level ? fps.get(content, job.record.levelId) : nullptr;
        if (!level) res.check.why = "niveau introuvable dans ce jeu";
        else if (!fp || fp->hash != job.record.levelHash) res.check.why = "course d'une autre version du niveau";
        else if (auto replay = ReplayData::decode(std::vector<uint8_t>(job.replayBytes.begin(), job.replayBytes.end()), &err); !replay)
            res.check.why = "replay illisible : " + err;
        else
            res.check = verifyTasReplay(content, *level, job.record, *replay, &cancel_);
        res.recordText = std::move(job.recordText);
        res.replayBytes = std::move(job.replayBytes);
        std::lock_guard<std::mutex> l(m_);
        done_.push_back(std::move(res));
        --running_;
    }
}

// ------------------------------------------------------------------ the store

void TasOnlineStore::load() {
    index_ = Json::object();
    auto text = FileUtil::readText(dir_ / "index.json");
    if (!text) return;
    Json j = Json::parse(*text, nullptr, false);
    if (j.is_object() && jstr(j, "format") == kIndexFormat) index_ = j;
}

bool TasOnlineStore::save() const {
    if (dir_.empty()) return false;
    Json j = index_;
    j["format"] = kIndexFormat;
    return FileUtil::writeTextAtomic(dir_ / "index.json", j.dump(1) + "\n");
}

std::string TasOnlineStore::statusOf(const TasOnlineEntry& e) const {
    const Json& runs = jfield(index_, "runs", Json::object());
    if (!runs.is_object() || !runs.contains(e.key())) return {};
    const Json& k = runs[e.key()];
    if (jget<int>(k, "revision", 0) != e.revision || jstr(k, "record_sha256") != e.recordSha256 || jstr(k, "replay_sha256") != e.replaySha256) return {};
    return jstr(k, "status");
}

std::string TasOnlineStore::whyOf(const std::string& key) const {
    const Json& runs = jfield(index_, "runs", Json::object());
    return runs.is_object() && runs.contains(key) ? jstr(runs[key], "why") : std::string();
}

std::vector<TasOnlineEntry> TasOnlineStore::wanted(const Json& data, const std::string& server, const std::function<std::string(const std::string&)>& hashOf) {
    std::vector<TasOnlineEntry> out;
    if (jstr(index_, "server") != server) {
        // Another server: its runs are its own (and so are its revision numbers).
        std::error_code ec;
        fs::remove_all(runsDir(), ec);
        index_ = Json::object();
        index_["server"] = server;
        save();
    }
    const Json& list = jfield(data, "runs", Json::array());
    if (!list.is_array()) return out;
    for (const Json& raw : list) {
        auto e = TasOnlineEntry::fromJson(raw);
        if (!e || e->simVersion != kSimulationVersion) continue;
        const std::string hash = hashOf(e->levelId);
        if (hash.empty() || hash != e->levelHash) continue;  // another version of the level than this game's
        if (!statusOf(*e).empty()) continue;                 // verified or refused at this revision already
        // Only a run faster than the fastest valid one here (shipped, or downloaded before).
        if (auto have = tasFor(e->levelId, e->category, hash); have && have->timeMs <= e->timeMs) continue;
        out.push_back(*e);
        if (out.size() >= 64) break;
    }
    return out;
}

std::optional<TasRecord> TasOnlineStore::readDownload(const TasOnlineEntry& e, const std::string& recordText, const std::string& replayBytes, std::string* why) {
    auto fail = [&](const std::string& w) -> std::optional<TasRecord> {
        if (why) *why = w;
        return std::nullopt;
    };
    if (static_cast<int64_t>(recordText.size()) != e.recordSize || Sha256::hex(recordText) != e.recordSha256) return fail("fiche différente de celle du serveur");
    if (static_cast<int64_t>(replayBytes.size()) != e.replaySize || Sha256::hex(replayBytes) != e.replaySha256) return fail("replay différent de celui du serveur");
    Json j = Json::parse(recordText, nullptr, false);
    std::string err;
    auto r = j.is_discarded() ? std::nullopt : TasRecord::fromJson(j, &err);
    if (!r) return fail("fiche illisible" + (err.empty() ? std::string() : " : " + err));
    if (r->levelId != e.levelId || r->category != e.category || r->levelHash != e.levelHash || r->simVersion != e.simVersion || r->timeMs != e.timeMs)
        return fail("la fiche ne dit pas ce que dit la liste");
    r->replay = tasReplayFileName(r->category);  // its name here, next to its JSON
    return r;
}

void TasOnlineStore::noteRefused(const TasOnlineEntry& e, const std::string& why) {
    Json& runs = index_["runs"];
    if (!runs.is_object()) runs = Json::object();
    Json k = runs.contains(e.key()) && jstr(runs[e.key()], "status") == "verified" ? runs[e.key()] : Json::object();
    // A refused revision is remembered (never downloaded again); a run verified before stays.
    k["refused"] = {{"revision", e.revision}, {"record_sha256", e.recordSha256}, {"replay_sha256", e.replaySha256}, {"why", why}};
    if (jstr(k, "status") != "verified") {
        k["revision"] = e.revision;
        k["record_sha256"] = e.recordSha256;
        k["replay_sha256"] = e.replaySha256;
        k["status"] = "refused";
    }
    k["why"] = why;
    k["checked_at"] = FileUtil::nowString();
    runs[e.key()] = k;
    Log::warn("Course de la machine {} révision {} ignorée : {}", e.key(), e.revision, why);
    save();
}

void TasOnlineStore::noteResult(const TasVerifier::Result& r) {
    if (!r.check.ok) return noteRefused(r.entry, r.check.why);
    TasRecord rec = r.record;
    rec.sourceRoot.clear();
    const fs::path file = tasFilePath(rec.levelId, rec.category, runsDir());
    if (!FileUtil::writeBinary(tasReplayPath(rec, runsDir()), std::vector<uint8_t>(r.replayBytes.begin(), r.replayBytes.end())) || !writeTasFile(file, rec))
        return noteRefused(r.entry, "écriture impossible");
    Json& runs = index_["runs"];
    if (!runs.is_object()) runs = Json::object();
    runs[r.entry.key()] = {{"revision", r.entry.revision},
                           {"record_sha256", r.entry.recordSha256},
                           {"replay_sha256", r.entry.replaySha256},
                           {"time_ms", rec.timeMs},
                           {"status", "verified"},
                           {"checked_at", FileUtil::nowString()}};
    Log::info("Course de la machine {} révision {} vérifiée : {}", r.entry.key(), r.entry.revision, formatRunTime(rec.timeMs));
    save();
}

// ------------------------------------------------------------------ the game's side

void TasOnline::start(const fs::path& dir, bool fetch) {
    store_.setDir(dir);
    store_.load();
    setTasDownloadedRoot(store_.runsDir());
    fetch_ = fetch;
    timer_ = kListEvery - kFirstList;
}

void TasOnline::refresh() {
    if (listing_ || !online_.usable()) return;
    listing_ = true;
    timer_ = 0.0f;
    online_.get(
        "/api/v1/tas",
        OnlineService::guard(life_, [this](const ApiResult& r) {
            listing_ = false;
            // A cached answer (no network) brings nothing: the runs verified are on disk.
            if (!r.ok || r.fromCache) {
                // A server without the list (404, before it) has none: asked again later.
                timer_ = kListEvery - kListRetry;
                return;
            }
            const std::string server = online_.config().serverUrl;
            auto todo = store_.wanted(r.data, server, [this](const std::string& id) {
                const LevelFingerprint* fp = fingerprints_.get(content_, id);
                return fp ? fp->hash : std::string();
            });
            for (auto& e : todo)
                if (std::none_of(queue_.begin(), queue_.end(), [&](const TasOnlineEntry& q) { return q.key() == e.key(); })) queue_.push_back(std::move(e));
            next();
        }),
        false);
}

void TasOnline::next() {
    if (downloading_ || verifier_.busy() || queue_.empty() || !online_.usable()) return;
    TasOnlineEntry e = queue_.front();
    queue_.pop_front();
    downloading_ = true;
    std::weak_ptr<int> alive = life_;
    online_.getBinary(
        e.recordPath,
        [this, e, alive](const HttpResponse& h) {
            if (alive.expired()) return;
            if (h.networkFailure() || h.status != 200) {
                downloading_ = false;  // asked again with the next list
                return next();
            }
            std::string recordText = h.body;
            online_.getBinary(
                e.replayPath,
                [this, e, alive, recordText](const HttpResponse& h2) {
                    if (alive.expired()) return;
                    downloading_ = false;
                    if (h2.networkFailure() || h2.status != 200) return next();
                    std::string why;
                    auto rec = TasOnlineStore::readDownload(e, recordText, h2.body, &why);
                    if (!rec) {
                        store_.noteRefused(e, why);
                        return next();
                    }
                    TasVerifier::Job job;
                    job.entry = e;
                    job.record = *rec;
                    job.recordText = recordText;
                    job.replayBytes = h2.body;
                    job.server = online_.config().serverUrl;
                    job.userDir = Paths::userDir();
                    verifier_.submit(std::move(job));
                },
                static_cast<size_t>(e.replaySize) + 1024);
        },
        static_cast<size_t>(e.recordSize) + 1024);
}

void TasOnline::update(float dt) {
    bool any = false;
    for (const TasVerifier::Result& r : verifier_.poll()) {
        store_.noteResult(r);
        any = true;
    }
    if (any) next();
    if (!fetch_ || !online_.usable()) return;
    timer_ += dt;
    if (timer_ >= kListEvery && !busy()) refresh();
}

}  // namespace cosmic
