#include "game/tas/TasTraining.h"

#include <SDL3/SDL.h>

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineService.h"
#include "game/online/Replay.h"

namespace cosmic {

namespace {

using Clock = std::chrono::steady_clock;

/// The server's rhythm (server/src/tas.ts): a heartbeat a minute, the lease lost after three
/// minutes without one. A training stops by itself a little before, when its heartbeats fail.
constexpr auto kBeatEvery = std::chrono::seconds(60);
constexpr auto kBeatRetry = std::chrono::seconds(20);
constexpr auto kLeaseSafe = std::chrono::seconds(165);
/// A child asked to stop that is still there after this long (its last verification) is ended.
constexpr auto kStopGrace = std::chrono::seconds(120);
/// A training asked to stop gives its lease back when its child has exited (its last best time on
/// the curve goes with the release), or after this long.
constexpr auto kReleaseGrace = std::chrono::seconds(20);
/// The length of a round of the search (cosmic_tas --round): its best run is verified and written
/// at the end of each one.
constexpr int kRoundMinutes = 10;

int64_t msSince(Clock::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count(); }

fs::path trainingDir() { return TasTraining::localRoot() / ".training"; }

std::string fileStem(const TasPair& p) { return p.levelId + "_" + categoryId(p.category); }

}  // namespace

const char* tasJobStateLabel(TasTraining::JobState s) {
    switch (s) {
        case TasTraining::JobState::Acquiring: return "Réservation…";
        case TasTraining::JobState::Running: return "En entraînement";
        case TasTraining::JobState::Stopping: return "Arrêt en cours";
        case TasTraining::JobState::Finished: return "Terminé";
        case TasTraining::JobState::Failed: return "Échec";
    }
    return "";
}

int TasTrainingSettings::defaultParallel(int threadsPerJob, int cores) {
    return std::clamp(std::max(1, cores) / std::max(1, threadsPerJob), 1, kMaxParallel);
}

Json TasTrainingSettings::toJson() const {
    return {{"format", "cosmic.tas.training"}, {"threadsPerJob", threadsPerJob}, {"parallel", parallel}, {"budgetMinutes", budgetMinutes}};
}

TasTrainingSettings TasTrainingSettings::fromJson(const Json& j, int cores) {
    TasTrainingSettings s;
    s.threadsPerJob = std::clamp(jget<int>(j, "threadsPerJob", 2), 1, kMaxThreads);
    s.parallel = std::clamp(jget<int>(j, "parallel", defaultParallel(s.threadsPerJob, cores)), 1, kMaxParallel);
    s.budgetMinutes = std::clamp(jget<int>(j, "budgetMinutes", 30), 0, 24 * 60);
    return s;
}

int64_t TasTraining::Job::sessionMs() const {
    if (state == JobState::Acquiring || started == Clock::time_point{}) return 0;
    return msSince(started);
}

int64_t TasTraining::Job::trainingTotalMs() const {
    if (leaseId.empty() || lastBeatOk == Clock::time_point{}) return serverTotalMs;
    return serverTotalMs + std::max<int64_t>(0, msSince(lastBeatOk));
}

int64_t TasTraining::Job::exploredAt(int64_t sessionExplored) const { return std::max<int64_t>(0, serverExplored + sessionExplored - exploredReported); }

TasTraining::TasTraining(OnlineService& online) : online_(online) {}

TasTraining::~TasTraining() {
    // The game normally called shutdown(): here only the processes still tracked are let go
    // (their input closes: they stop by themselves).
    for (auto& j : jobs_)
        if (j->process) SDL_DestroyProcess(j->process);
}

fs::path TasTraining::toolPath() {
#ifdef _WIN32
    const char* name = "cosmic_tas.exe";
#else
    const char* name = "cosmic_tas";
#endif
    const char* base = SDL_GetBasePath();
    if (!base) return {};
    fs::path p = Paths::fromUtf8(base) / name;
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : fs::path();
}

fs::path TasTraining::localRoot() { return Paths::savesDir() / "tas_local"; }

int TasTraining::cores() { return std::max(1, SDL_GetNumLogicalCPUCores()); }

void TasTraining::loadSettings() {
    curves_.open(trainingDir());  // the training curves, read on a thread
    Json j = Json::object();
    if (auto text = FileUtil::readText(localRoot() / "settings.json")) {
        Json parsed = Json::parse(*text, nullptr, false);
        if (parsed.is_object()) j = parsed;
    }
    settings_ = TasTrainingSettings::fromJson(j, cores());
}

void TasTraining::saveSettings() const { FileUtil::writeTextAtomic(localRoot() / "settings.json", settings_.toJson().dump(1) + "\n"); }

std::string TasTraining::leasePath(const TasPair& p) { return "/api/v1/admin/tas/" + p.levelId + "/" + categoryId(p.category) + "/lease"; }

const TasTraining::Job* TasTraining::jobFor(const TasPair& p) const {
    for (const auto& j : jobs_)
        if (j->pair == p) return j.get();
    return nullptr;
}

std::string TasTraining::failureOf(const TasPair& p) const {
    auto it = failures_.find(p.key());
    return it == failures_.end() ? std::string() : it->second;
}

bool TasTraining::queued(const TasPair& p) const { return std::find(queue_.begin(), queue_.end(), p) != queue_.end(); }

int TasTraining::running() const {
    int n = 0;
    for (const auto& j : jobs_)
        if (j->state != JobState::Finished && j->state != JobState::Failed) ++n;
    return n;
}

void TasTraining::say(const std::string& m, bool error) {
    if (error) Log::warn("TAS : {}", m);
    if (onMessage) onMessage(m, error);
}

void TasTraining::train(const TasPair& p) {
    if (jobFor(p)) return;
    unqueue(p);
    queue_.push_front(p);
    startNext();
}

int TasTraining::enqueue(const std::vector<TasPair>& pairs) {
    int added = 0;
    for (const TasPair& p : pairs)
        if (!jobFor(p) && !queued(p)) {
            queue_.push_back(p);
            ++added;
        }
    startNext();
    return added;
}

void TasTraining::unqueue(const TasPair& p) { queue_.erase(std::remove(queue_.begin(), queue_.end(), p), queue_.end()); }

void TasTraining::stop(const TasPair& p) {
    unqueue(p);
    for (auto& j : jobs_)
        if (j->pair == p) {
            j->budgetReached = false;
            requestStop(*j);  // its lease given back once cosmic_tas has exited (update, ended)
        }
}

void TasTraining::stopAll() {
    queue_.clear();
    for (auto& j : jobs_) {
        j->budgetReached = false;
        requestStop(*j);
    }
}

void TasTraining::startNext() {
    if (!online_.usable() || !online_.loggedIn()) return;
    while (running() < settings_.parallel && !queue_.empty()) {
        const TasPair p = queue_.front();
        queue_.pop_front();
        if (jobFor(p)) continue;
        failures_.erase(p.key());
        auto j = std::make_unique<Job>();
        j->pair = p;
        Job& ref = *j;
        jobs_.push_back(std::move(j));
        acquire(ref);
    }
}

void TasTraining::acquire(Job& j) {
    const TasPair p = j.pair;
    j.levelHash = hashFor ? hashFor(p.levelId) : std::string();
    if (j.levelHash.empty()) {
        j.state = JobState::Failed;
        j.error = "empreinte du niveau inconnue : pas de course de la machine pour ce niveau";
        say(p.levelId + " · " + categoryLabel(p.category) + " : " + j.error, true);
        return;
    }
    // The lease of this version of the level: the fingerprint this game plays, its simulation.
    const Json body = {{"level_hash", j.levelHash}, {"sim_version", kSimulationVersion}};
    online_.send("POST", leasePath(p), body, OnlineService::guard(life_, [this, p](const ApiResult& r) {
                     Job* j = nullptr;
                     for (auto& x : jobs_)
                         if (x->pair == p && x->state == JobState::Acquiring) j = x.get();
                     if (!j) {
                         // Stopped while it waited: a lease given meanwhile is given back.
                         if (r.ok && !jstr(r.data, "lease_id").empty())
                             online_.send("POST", leasePath(p) + "/release", {{"lease_id", jstr(r.data, "lease_id")}, {"explored", 0}}, [](const ApiResult&) {});
                         return;
                     }
                     if (!r.ok) {
                         const Json holder = jfield(r.details, "holder", Json());
                         if (r.code == "TAS_LOCKED")
                             say(p.levelId + " · " + categoryLabel(p.category) + " : en entraînement par " + jstr(holder, "username", "un autre administrateur") +
                                     ", passé.",
                                 false);
                         else say(p.levelId + " · " + categoryLabel(p.category) + " : " + r.message, true);
                         j->state = JobState::Failed;
                         j->error = r.code == "TAS_LOCKED" ? std::string() : r.message;
                         return;
                     }
                     j->leaseId = jstr(r.data, "lease_id");
                     const Json& t = jfield(r.data, "training", Json::object());
                     j->serverTotalMs = jget<int64_t>(t, "total_ms", 0);
                     j->serverExplored = jget<int64_t>(t, "total_explored", 0);
                     j->session = jget<int>(t, "sessions", 0);  // this lease's session (the count includes it)
                     j->lastBeatOk = j->lastBeatSent = Clock::now();
                     launch(*j);
                 }));
}

SDL_Process* TasTraining::spawn(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    // Its input: a pipe of this game (closed when the game closes, even by a crash: the child
    // stops). No console window, no output.
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    return p;
}

void TasTraining::launch(Job& j) {
    const fs::path tool = toolPath();
    if (tool.empty()) {
        j.error = "cosmic_tas est introuvable à côté du jeu.";
        say(j.error, true);
        j.state = JobState::Failed;
        return release(j);
    }
    std::error_code ec;
    fs::create_directories(trainingDir(), ec);
    j.progressFile = trainingDir() / (fileStem(j.pair) + ".progress.json");
    j.stopFile = trainingDir() / (fileStem(j.pair) + ".stop");
    fs::remove(j.progressFile, ec);
    fs::remove(j.stopFile, ec);
    const fs::path seed = seedFor ? seedFor(j.pair) : fs::path();
    j.hasSeed = !seed.empty();
    std::vector<std::string> args = {Paths::toUtf8(tool),
                                     j.pair.levelId,
                                     categoryId(j.pair.category),
                                     "--until-stopped",
                                     "--round",
                                     std::to_string(kRoundMinutes),
                                     "--threads",
                                     std::to_string(settings_.threadsPerJob),
                                     "--seed",
                                     std::to_string(SDL_GetTicksNS() % 1000000007ull),
                                     "--out",
                                     Paths::toUtf8(localRoot()),
                                     "--progress",
                                     Paths::toUtf8(j.progressFile),
                                     "--stop-file",
                                     Paths::toUtf8(j.stopFile),
                                     "--stop-on-stdin",
                                     "--user-dir",
                                     Paths::toUtf8(Paths::userDir())};
    if (!online_.config().serverUrl.empty()) {
        args.push_back("--server");
        args.push_back(online_.config().serverUrl);
    }
    // The run so far after each section, kept for the next training of the pair: a level whose
    // first run takes longer than a session goes on from where the last one got to (only a run of
    // this very version of the level: its fingerprint and simulation).
    const fs::path partial = trainingDir() / (fileStem(j.pair) + ".partial.cprz");
    args.push_back("--save-partial");
    args.push_back(Paths::toUtf8(partial));
    if (j.hasSeed) {
        args.push_back("--seed-replay");
        args.push_back(Paths::toUtf8(seed));
    } else if (auto bytes = FileUtil::readBinary(partial)) {
        auto rep = ReplayData::decode(*bytes);
        if (rep && rep->exact() && rep->levelId == j.pair.levelId && rep->levelHash == j.levelHash && rep->simVersion == kSimulationVersion) {
            args.push_back("--seed-replay");
            args.push_back(Paths::toUtf8(partial));
        }
    }
    j.process = spawn(args);
    if (!j.process) {
        j.error = std::string("cosmic_tas ne démarre pas : ") + SDL_GetError();
        say(j.error, true);
        j.state = JobState::Failed;
        return release(j);
    }
    j.state = JobState::Running;
    j.started = Clock::now();
    Log::info("TAS : entraînement de {} lancé ({} fils{})", j.pair.key(), settings_.threadsPerJob, j.hasSeed ? ", depuis la meilleure course" : "");
}

TasTraining::CurveSend TasTraining::curveSend(const TasCurveVersion& v, const std::string& leaseId, int64_t nowMs) {
    CurveSend out;
    for (const TasCurvePoint& p : v.points) {
        if (p.share != TasShare::Pending || p.leaseId != leaseId || !p.best()) continue;
        if (out.pointIds.size() >= kSendPoints) break;
        out.points.push_back({{"kind", tasCurveKindId(p.kind)},
                              {"time_ms", p.timeMs},
                              {"age_ms", std::clamp<int64_t>(nowMs - p.foundAtMs, 0, 86400000)},
                              {"explored", p.sessionExplored}});
        out.pointIds.push_back(p.foundAtMs);
    }
    // The windows: their runs of the same round and section, then the smallest neighbours
    // merged until kSendWindows are left (a summary: this computer keeps the fine ones).
    struct Group {
        TasCurveWindow w;
        int64_t endAt = 0;
    };
    std::vector<Group> groups;
    for (const TasCurveWindow& w : v.windows) {
        if (w.share != TasShare::Pending || w.leaseId != leaseId) continue;
        out.windowIds.push_back(w.stats.id);
        if (!groups.empty() && groups.back().w.stats.round == w.stats.round && groups.back().w.stats.section == w.stats.section) {
            Group& g = groups.back();
            g.w.stats = TasAttemptWindow::merged(g.w.stats, w.stats);
            g.w.fromMs = std::min(g.w.fromMs, w.fromMs);
            g.w.toMs = std::max(g.w.toMs, w.toMs);
            g.endAt = std::max(g.endAt, w.endAtMs);
        } else {
            groups.push_back({w, w.endAtMs});
        }
    }
    while (groups.size() > kSendWindows) {
        size_t at = 0;
        int64_t least = -1;
        for (size_t i = 0; i + 1 < groups.size(); ++i)
            if (const int64_t n = groups[i].w.stats.attempts + groups[i + 1].w.stats.attempts; least < 0 || n < least) least = n, at = i;
        Group& a = groups[at];
        const Group& b = groups[at + 1];
        a.w.stats = TasAttemptWindow::merged(a.w.stats, b.w.stats);
        a.w.fromMs = std::min(a.w.fromMs, b.w.fromMs);
        a.w.toMs = std::max(a.w.toMs, b.w.toMs);
        a.endAt = std::max(a.endAt, b.endAt);
        groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(at) + 1);
    }
    for (const Group& g : groups) {
        const TasAttemptWindow& s = g.w.stats;
        out.windows.push_back({{"age_ms", std::clamp<int64_t>(nowMs - g.endAt, 0, 86400000)},
                               {"duration_ms", std::clamp<int64_t>(g.w.toMs - g.w.fromMs, 1, 600000)},
                               {"round", std::max(1, s.round)},
                               {"section", s.section},
                               {"sections", s.sections},
                               {"attempts", s.attempts},
                               {"finished", s.finished},
                               {"best_ms", s.bestMs},
                               {"median_ms", s.medianMs},
                               {"worst_ms", s.worstMs},
                               {"route_median", s.routeMedian},
                               {"route_max", s.routeMax},
                               {"stopped", s.stopped},
                               {"broken", s.broken}});
    }
    return out;
}

void TasTraining::noteCurve(Job& j) {
    if (!j.progress || j.levelHash.empty()) return;
    const TasProgress& p = *j.progress;
    TasCurve& curve = curves_.at(j.pair.levelId, j.pair.category);
    const int64_t now = tasNowMs(), total = j.trainingTotalMs();
    const TasShare share = j.leaseId.empty() ? TasShare::Unshared : TasShare::Pending;
    bool changed = false, point = false;
    // A new best time: where cosmic_tas verified it (its elapsed time then), never in between.
    if (p.bestMs > 0 && (j.lastBest == 0 || p.bestMs < j.lastBest)) {
        j.lastBest = p.bestMs;
        const int64_t age = p.bestElapsedMs > 0 ? std::max<int64_t>(0, p.elapsedMs - p.bestElapsedMs) : 0;
        TasCurvePoint pt;
        pt.kind = p.improvements == 0 && p.bestMs == p.seedMs ? TasCurveKind::Start : TasCurveKind::Improvement;
        pt.timeMs = p.bestMs;
        pt.trainingMs = std::max<int64_t>(0, total - age);
        pt.explored = j.exploredAt(p.bestExplored);
        pt.at = tasIsoUtc(now - age);
        pt.by = online_.username();
        pt.session = j.session;
        pt.share = share;
        pt.leaseId = j.leaseId;
        pt.foundAtMs = now - age;
        pt.sessionExplored = p.bestExplored;
        point = curve.addBest(j.levelHash, kSimulationVersion, pt);
        changed = changed || point;
    }
    // The new windows of attempts.
    for (const TasAttemptWindow& w : p.windows) {
        if (w.id <= j.lastWindow) continue;
        j.lastWindow = w.id;
        const int64_t age = std::max<int64_t>(0, p.elapsedMs - w.endMs);
        TasCurveWindow cw;
        cw.stats = w;
        cw.toMs = std::max<int64_t>(0, total - age);
        cw.fromMs = std::max<int64_t>(0, cw.toMs - (w.endMs - w.startMs));
        cw.session = j.session;
        cw.by = online_.username();
        cw.at = tasIsoUtc(now - age);
        cw.local = true;
        cw.share = share;
        cw.leaseId = j.leaseId;
        cw.endAtMs = now - age;
        curve.addWindow(j.levelHash, kSimulationVersion, std::move(cw));
        changed = true;
    }
    if (changed) curves_.changed(j.pair.levelId, j.pair.category, point);
}

void TasTraining::curveAnswer(const TasPair& p, const std::string& hash, const std::string& lease, const CurveSend& sent, const ApiResult& r) {
    if (sent.pointIds.empty() && sent.windowIds.empty()) return;
    TasCurve& curve = curves_.at(p.levelId, p.category);
    if (r.ok) {
        std::vector<TasCurvePoint> added;
        const Json pts = jfield(jfield(r.data, "curve", Json::object()), "points", Json::array());
        if (pts.is_array())
            for (const Json& x : pts)
                if (auto pt = TasCurvePoint::fromServer(x)) added.push_back(std::move(*pt));
        curve.acknowledge(hash, kSimulationVersion, lease, sent.pointIds, added, sent.windowIds);
    } else if (!r.networkFailure()) {
        curve.abandon(lease);  // the lease is gone: they stay on this computer only
    } else {
        return;  // sent again with the next heartbeat (a release: they stay pending, then unshared at the next start)
    }
    curves_.changed(p.levelId, p.category, true);
}

bool TasTraining::curveRefused(const CurveSend& sent, const ApiResult& r) {
    if (r.ok || r.status != 400 || r.code != "VALIDATION_ERROR" || (sent.pointIds.empty() && sent.windowIds.empty())) return false;
    // A server that does not take the curve (not updated yet): the lease goes on without it.
    if (!curveUnsupported_) Log::warn("TAS : le serveur refuse la courbe d'entraînement ({}) ; elle reste sur cet ordinateur", r.message);
    curveUnsupported_ = true;
    return true;
}

TasTraining::CurveSend TasTraining::pendingCurve(const TasPair& p, const std::string& hash, const std::string& lease) const {
    if (curveUnsupported_) return {};
    if (const TasCurve* c = curves_.find(p.levelId, p.category))
        if (const TasCurveVersion* v = c->find(hash, kSimulationVersion)) return curveSend(*v, lease, tasNowMs());
    return {};
}

void TasTraining::heartbeat(Job& j) {
    if (j.leaseId.empty() || j.releasing) return;
    j.lastBeatSent = Clock::now();
    const TasPair p = j.pair;
    const std::string lease = j.leaseId, hash = j.levelHash;
    const int64_t explored = j.progress ? j.progress->explored : 0;
    const CurveSend sent = pendingCurve(p, hash, lease);
    Json body = {{"lease_id", lease}, {"explored", explored}};
    if (!sent.pointIds.empty()) body["curve"] = sent.points;
    if (!sent.windowIds.empty()) body["attempts"] = sent.windows;
    online_.send("POST", leasePath(p) + "/heartbeat", body,
                 OnlineService::guard(life_, [this, p, lease, hash, explored, sent](const ApiResult& r) {
                     const bool refused = curveRefused(sent, r);
                     if (refused) curves_.at(p.levelId, p.category).abandon(lease);
                     else curveAnswer(p, hash, lease, sent, r);
                     Job* j = nullptr;
                     for (auto& x : jobs_)
                         if (x->pair == p && x->leaseId == lease) j = x.get();
                     if (!j) return;
                     if (refused) {
                         j->lastBeatSent = Clock::now() - kBeatEvery;  // again at once, without the curve
                         return;
                     }
                     if (r.ok) {
                         j->lastBeatOk = Clock::now();
                         j->exploredReported = explored;
                         const Json& t = jfield(r.data, "training", Json::object());
                         j->serverTotalMs = jget<int64_t>(t, "total_ms", j->serverTotalMs);
                         j->serverExplored = jget<int64_t>(t, "total_explored", j->serverExplored);
                         return;
                     }
                     if (r.networkFailure()) {
                         j->lastBeatSent = Clock::now() - kBeatEvery + kBeatRetry;  // again sooner
                         return;
                     }
                     // The lease is no longer ours (expired, taken): the training stops at once.
                     j->leaseId.clear();
                     j->error = "verrou perdu : " + r.message;
                     say(p.levelId + " · " + categoryLabel(p.category) + " : " + j->error, true);
                     requestStop(*j);
                 }));
}

void TasTraining::requestStop(Job& j) {
    if (j.state == JobState::Acquiring) {
        j.state = JobState::Finished;  // its lease, if it comes, is given back (acquire)
        return;
    }
    if (j.state != JobState::Running) return;
    j.state = JobState::Stopping;
    j.stopAsked = Clock::now();
    if (!j.stopFile.empty()) FileUtil::writeTextAtomic(j.stopFile, "stop\n");
    if (j.process)
        if (SDL_IOStream* in = SDL_GetProcessInput(j.process)) {
            SDL_WriteIO(in, "stop\n", 5);
            SDL_CloseIO(in);  // the end of its input: it stops even if it missed the line
        }
}

void TasTraining::release(Job& j) {
    if (j.leaseId.empty() || j.releasing) return;
    j.releasing = true;
    ++releasesPending_;
    sendRelease(j.pair, j.leaseId, j.levelHash, j.progress ? j.progress->explored : 0, true);
}

void TasTraining::sendRelease(const TasPair& p, const std::string& lease, const std::string& hash, int64_t explored, bool withCurve) {
    const CurveSend sent = withCurve ? pendingCurve(p, hash, lease) : CurveSend{};
    Json body = {{"lease_id", lease}, {"explored", explored}};
    if (!sent.pointIds.empty()) body["curve"] = sent.points;
    if (!sent.windowIds.empty()) body["attempts"] = sent.windows;
    online_.send("POST", leasePath(p) + "/release", body,
                 OnlineService::guard(life_, [this, p, lease, hash, explored, sent](const ApiResult& r) {
                     // No later call of this lease: what it did not take stays on this computer.
                     if (curveRefused(sent, r)) {
                         curves_.at(p.levelId, p.category).abandon(lease);
                         return sendRelease(p, lease, hash, explored, false);  // the lease given back all the same
                     }
                     --releasesPending_;
                     curveAnswer(p, hash, lease, sent, r);
                     if (!r.ok && r.networkFailure()) curves_.at(p.levelId, p.category).abandon(lease);
                     for (auto& x : jobs_)
                         if (x->pair == p && x->leaseId == lease) {
                             x->leaseId.clear();
                             x->releasing = false;
                             if (r.ok) {
                                 const Json& t = jfield(r.data, "training", Json::object());
                                 x->serverTotalMs = jget<int64_t>(t, "total_ms", x->serverTotalMs);
                                 x->serverExplored = jget<int64_t>(t, "total_explored", x->serverExplored);
                             }
                         }
                 }));
}

void TasTraining::readProgress(Job& j) {
    if (j.progressFile.empty()) return;
    auto text = FileUtil::readText(j.progressFile);
    if (!text) return;
    auto p = TasProgress::parse(*text);
    if (p && p->levelId == j.pair.levelId && p->category == j.pair.category && (!j.progress || p->sequence >= j.progress->sequence)) {
        j.progress = std::move(p);
        noteCurve(j);
    }
}

void TasTraining::ended(Job& j) {
    readProgress(j);
    if (j.process) {
        SDL_DestroyProcess(j.process);
        j.process = nullptr;
    }
    if (j.state == JobState::Running) {
        // Ended by itself (a failure: the level, the seed...): its last words say why.
        j.state = JobState::Failed;
        j.error = j.progress && !j.progress->message.empty() ? j.progress->message : std::string("cosmic_tas s'est arrêté");
        say(j.pair.levelId + " · " + categoryLabel(j.pair.category) + " : " + j.error, true);
    } else if (j.state == JobState::Stopping) {
        j.state = JobState::Finished;
    }
    release(j);
    std::error_code ec;
    fs::remove(j.stopFile, ec);
    if (j.budgetReached && j.state == JobState::Finished) queue_.push_back(j.pair);  // round-robin: back later
    if (onEnded) onEnded(j.pair);
}

void TasTraining::update(float dt) {
    pollTimer_ += dt;
    const bool poll = pollTimer_ >= 0.5f;
    if (poll) pollTimer_ = 0.0f;
    const auto now = Clock::now();
    for (auto& jp : jobs_) {
        Job& j = *jp;
        if (j.state != JobState::Running && j.state != JobState::Stopping) continue;
        if (poll) readProgress(j);
        if (j.state == JobState::Running) {
            if (!j.leaseId.empty() && !j.releasing) {
                if (now - j.lastBeatSent >= kBeatEvery) heartbeat(j);
                if (now - j.lastBeatOk >= kLeaseSafe) {
                    // No heartbeat answered for almost the lease's life: it can no longer be ours.
                    j.error = "connexion perdue : le verrou a expiré";
                    say(j.pair.levelId + " · " + categoryLabel(j.pair.category) + " : " + j.error, true);
                    j.leaseId.clear();
                    requestStop(j);
                }
            }
            if (settings_.budgetMinutes > 0 && j.sessionMs() >= int64_t(settings_.budgetMinutes) * 60000 && j.state == JobState::Running) {
                j.budgetReached = true;
                requestStop(j);  // its lease given back once cosmic_tas has exited (below, ended)
            }
        }
        // Stopping: the lease given back when the child has exited (ended), at the latest after kReleaseGrace.
        if (j.state == JobState::Stopping && now - j.stopAsked >= kReleaseGrace) release(j);
        if (j.state == JobState::Stopping && j.process && now - j.stopAsked >= kStopGrace) SDL_KillProcess(j.process, true);
        int code = 0;
        if (poll && j.process && SDL_WaitProcess(j.process, false, &code)) ended(j);
    }
    // The ended trainings leave the list (a failure stays said in its pair's line: failureOf).
    for (const auto& j : jobs_)
        if (j->state == JobState::Failed && !j->error.empty()) failures_[j->pair.key()] = j->error;
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                               [](const std::unique_ptr<Job>& j) {
                                   return (j->state == JobState::Finished || j->state == JobState::Failed) && !j->process && !j->releasing;
                               }),
                jobs_.end());
    startNext();
    curves_.update(dt);
}

void TasTraining::shutdown(int waitMs) {
    queue_.clear();
    for (auto& j : jobs_) {
        j->budgetReached = false;
        requestStop(*j);
        release(*j);
    }
    // The releases answered (others see the pairs free at once), a few seconds at most.
    const auto until = Clock::now() + std::chrono::milliseconds(waitMs);
    while (releasesPending_ > 0 && Clock::now() < until) {
        online_.update(0.02f);
        SDL_Delay(20);
    }
    // The children finish their last verification and exit by themselves (their input closed).
    for (auto& j : jobs_)
        if (j->process) {
            SDL_DestroyProcess(j->process);
            j->process = nullptr;
        }
    jobs_.clear();
    curves_.flush();
}

}  // namespace cosmic
