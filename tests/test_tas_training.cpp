// The machine's runs improved by the administrators and published online (docs/TAS.md
// « Entraînement depuis ADMINISTRATION », « Courses publiées en ligne », « Quand un niveau
// change »): the progress file of `cosmic_tas` read by the game; tasFor() giving the fastest
// VALID run of the shipped and the downloaded ones; a downloaded run played again before it is
// ever shown (refused when its replay does not reproduce what it claims); a level that changes
// hides every run of its old version; the statuses of ADMINISTRATION › TAS; its page fitting at
// 1280 × 720 and 1920 × 1080, in French and in English.

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineService.h"
#include "game/online/Replay.h"
#include "game/planets/PlanetData.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasOnline.h"
#include "game/tas/TasProgress.h"
#include "game/tas/TasTraining.h"
#include "game/tas/TasVerify.h"
#include "packaging/Sha256.h"
#include "tests/TestFramework.h"
#include "ui/screens/AdminTas.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

fs::path freshDir(const char* tag) {
    fs::path root = Paths::userDir() / (std::string("tas_training_") + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

TasRecord fakeRecord(const std::string& hash, int64_t timeMs, RunCategory c = RunCategory::NoDeath) {
    TasRecord r;
    r.levelId = "sylvanox_01";
    r.levelHash = hash;
    r.simVersion = kSimulationVersion;
    r.category = c;
    r.timeMs = timeMs;
    r.replay = tasReplayFileName(c);
    return r;
}

void writeFake(const TasRecord& r, const fs::path& root) {
    ReplayData d;
    d.levelId = r.levelId;
    d.levelHash = r.levelHash;
    d.attemptId = makeUuidV4();
    d.seed = 7;
    d.keysPerSlot = true;
    d.inputs.assign(10, InputFrame{});
    d.frames.push_back(ReplayFrame{});
    d.durationMs = static_cast<uint32_t>(r.timeMs);
    CHECK(FileUtil::writeBinary(tasReplayPath(r, root), d.encode()));
    CHECK(writeTasFile(tasFilePath(r.levelId, r.category, root), r));
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}

}  // namespace

TEST_CASE("tas training: the progress file of cosmic_tas reads back, and refuses what is not one") {
    TasProgress p;
    p.phase = TasPhase::Searching;
    p.levelId = "sylvanox_01";
    p.category = RunCategory::NoDamage;
    p.elapsedMs = 61250;
    p.explored = 30144;
    p.seedMs = 86692;
    p.bestMs = 86650;
    p.candidateMs = 86601;
    p.improvements = 1;
    p.round = 2;
    p.threads = 4;
    p.sequence = 123;
    p.message = "Course trouvée";
    const std::string text = p.toJson().dump();
    auto back = TasProgress::parse(text);
    CHECK(back.has_value());
    CHECK(back->phase == TasPhase::Searching && back->levelId == "sylvanox_01" && back->category == RunCategory::NoDamage);
    CHECK(back->elapsedMs == 61250 && back->explored == 30144 && back->seedMs == 86692 && back->bestMs == 86650);
    CHECK(back->candidateMs == 86601 && back->improvements == 1 && back->round == 2 && back->threads == 4 && back->sequence == 123);
    CHECK(back->message == "Course trouvée" && !back->finished());
    for (TasPhase ph : {TasPhase::Starting, TasPhase::Searching, TasPhase::Verifying, TasPhase::Stopped, TasPhase::Failed}) {
        TasPhase out = TasPhase::Starting;
        CHECK(tasPhaseFromId(tasPhaseId(ph), out) && out == ph);
    }
    Json j = p.toJson();
    j["phase"] = "stopped";
    CHECK(TasProgress::fromJson(j)->finished());
    // Negative numbers clamped, a long message cut on a character boundary.
    j["explored"] = -5;
    j["message"] = std::string(399, 'a') + "é" + std::string(50, 'b');
    auto c = TasProgress::fromJson(j);
    CHECK(c && c->explored == 0 && c->message.size() <= 400 && c->message.size() >= 399);
    // Not a progress file: refused.
    CHECK(!TasProgress::parse("{nope"));
    j = p.toJson();
    j["format"] = "cosmic.tas";
    CHECK(!TasProgress::fromJson(j));
    j = p.toJson();
    j["phase"] = "dancing";
    CHECK(!TasProgress::fromJson(j));
    j = p.toJson();
    j["category"] = "SPEEDRUN";
    CHECK(!TasProgress::fromJson(j));
}

// The child process exactly as a training launches it: it writes its progress, and the end of its
// input (« Arrêter », the game closed or crashed) stops it cleanly, its last progress « stopped ».
TEST_CASE("tas training: cosmic_tas, launched as a child, reports its progress and stops when its input closes") {
    const fs::path tool = TasTraining::toolPath();
    if (tool.empty()) {
        std::printf("  AVERTISSEMENT : cosmic_tas absent à côté des tests (COSMIC_BUILD_TAS=OFF) : non testé\n");
        return;
    }
    const fs::path dir = freshDir("child");
    const fs::path progress = dir / "progress.json";
    SDL_Process* p = TasTraining::spawn({Paths::toUtf8(tool), "sylvanox_01", "NO_DAMAGE", "--until-stopped", "--round", "0.2", "--threads", "1",
                                         "--progress", Paths::toUtf8(progress), "--stop-on-stdin", "--out", Paths::toUtf8(dir / "out"), "--user-dir",
                                         Paths::toUtf8(Paths::userDir())});
    CHECK(p != nullptr);
    if (!p) return;
    std::optional<TasProgress> seen;
    for (int i = 0; i < 1200 && !(seen && seen->phase == TasPhase::Searching && seen->explored > 0); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (auto t = FileUtil::readText(progress)) seen = TasProgress::parse(*t);
    }
    CHECK(seen && seen->phase == TasPhase::Searching && seen->levelId == "sylvanox_01" && seen->category == RunCategory::NoDamage);
    CHECK(seen && seen->threads == 1 && seen->round >= 1);
    // « Arrêter »: the end of its input.
    if (SDL_IOStream* in = SDL_GetProcessInput(p)) SDL_CloseIO(in);
    bool exited = false;
    int code = 0;
    for (int i = 0; i < 1200 && !exited; ++i) {
        exited = SDL_WaitProcess(p, false, &code);
        if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(exited);
    if (!exited) SDL_KillProcess(p, true);
    SDL_DestroyProcess(p);
    auto last = FileUtil::readText(progress);
    auto end = last ? TasProgress::parse(*last) : std::nullopt;
    CHECK(end && end->phase == TasPhase::Stopped && end->explored > 0);
}

TEST_CASE("tas training: the settings share the cores between the trainings") {
    CHECK(TasTrainingSettings::defaultParallel(2, 8) == 4);
    CHECK(TasTrainingSettings::defaultParallel(4, 2) == 1);
    CHECK(TasTrainingSettings::defaultParallel(1, 64) == TasTrainingSettings::kMaxParallel);
    auto s = TasTrainingSettings::fromJson(Json::object(), 8);
    CHECK(s.threadsPerJob == 2 && s.parallel == 4 && s.budgetMinutes == 30);
    s = TasTrainingSettings::fromJson({{"threadsPerJob", 99}, {"parallel", -3}, {"budgetMinutes", -1}}, 8);
    CHECK(s.threadsPerJob == TasTrainingSettings::kMaxThreads && s.parallel == 1 && s.budgetMinutes == 0);
    auto back = TasTrainingSettings::fromJson(s.toJson(), 8);
    CHECK(back.threadsPerJob == s.threadsPerJob && back.parallel == s.parallel && back.budgetMinutes == s.budgetMinutes);
    CHECK(TasTraining::leasePath({"sylvanox_01", RunCategory::HundredPercent}) == "/api/v1/admin/tas/sylvanox_01/HUNDRED_PERCENT/lease");
}

TEST_CASE("tas training: tasFor gives the fastest valid run of the shipped and the downloaded ones") {
    const std::string hash(64, 'a');
    const fs::path shipped = freshDir("shipped"), downloaded = freshDir("downloaded");
    writeFake(fakeRecord(hash, 41250), shipped);
    writeFake(fakeRecord(hash, 40000), downloaded);
    auto best = bestTasAmong("sylvanox_01", RunCategory::NoDeath, hash, {shipped, downloaded});
    CHECK(best && best->timeMs == 40000 && best->sourceRoot == downloaded);
    // Its replay is found where it was read, whatever root is given.
    auto replay = loadTasReplay(*best, shipped);
    CHECK(replay && replay->durationMs == 40000);
    // A faster run that is not valid (another version of the level) never wins.
    writeFake(fakeRecord(std::string(64, 'b'), 30000), downloaded);
    best = bestTasAmong("sylvanox_01", RunCategory::NoDeath, hash, {shipped, downloaded});
    CHECK(best && best->timeMs == 41250 && best->sourceRoot == shipped);
    // Nor one of other rules of the simulation.
    TasRecord oldSim = fakeRecord(hash, 30000);
    oldSim.simVersion = kSimulationVersion - 1;
    writeFake(oldSim, downloaded);
    best = bestTasAmong("sylvanox_01", RunCategory::NoDeath, hash, {shipped, downloaded});
    CHECK(best && best->timeMs == 41250);
    // The default tasFor (data/tas) looks at the downloaded runs too.
    writeFake(fakeRecord(hash, 39000), downloaded);
    setTasDownloadedRoot(downloaded);
    auto viaDefault = tasFor("sylvanox_01", RunCategory::NoDeath, hash);
    CHECK(viaDefault && viaDefault->timeMs == 39000 && viaDefault->sourceRoot == downloaded);
    CHECK(loadTasReplay(*viaDefault).has_value());
    setTasDownloadedRoot({});
    CHECK(!tasFor("sylvanox_01", RunCategory::NoDeath, hash));
}

TEST_CASE("tas training: when a level changes, its runs of the old version are hidden, shipped or downloaded") {
    ContentDatabase content;
    content.reload();
    const LevelEntry* e = content.level("sylvanox_01");
    CHECK(e != nullptr);
    if (!e) return;
    auto text = FileUtil::readText(e->path);
    CHECK(text.has_value());
    Json level = Json::parse(*text, nullptr, false);
    auto before = fingerprintRankedLevel(content, level);
    CHECK(before.has_value());
    // One solid of the level one tile further: another level, another fingerprint.
    bool moved = false;
    for (Json& o : level["objects"])
        if (!moved && jstr(o, "type") == "solid" && o.contains("rect") && o["rect"].is_array() && o["rect"].size() == 4) {
            o["rect"][0] = o["rect"][0].get<double>() + 32.0;
            moved = true;
        }
    CHECK(moved);
    auto after = fingerprintRankedLevel(content, level);
    CHECK(after.has_value() && after->hash != before->hash);
    if (!before || !after) return;
    const fs::path shipped = freshDir("change_shipped"), downloaded = freshDir("change_downloaded");
    writeFake(fakeRecord(before->hash, 41250, RunCategory::AnyPercent), shipped);
    writeFake(fakeRecord(before->hash, 40000, RunCategory::AnyPercent), downloaded);
    CHECK(bestTasAmong("sylvanox_01", RunCategory::AnyPercent, before->hash, {shipped, downloaded}).has_value());
    CHECK(!bestTasAmong("sylvanox_01", RunCategory::AnyPercent, after->hash, {shipped, downloaded}));
    // The list of Cosmic Online: a run of the old version is not even downloaded.
    TasOnlineStore store(freshDir("change_store"));
    Json entry = {{"level_id", "sylvanox_01"},
                  {"category", "ANY_PERCENT"},
                  {"revision", 3},
                  {"level_hash", before->hash},
                  {"level_version", 1},
                  {"sim_version", kSimulationVersion},
                  {"time_ms", 30000},
                  {"record_sha256", std::string(64, '1')},
                  {"record_size", 900},
                  {"replay_sha256", std::string(64, '2')},
                  {"replay_size", 4000},
                  {"record_path", "/api/v1/tas/sylvanox_01/ANY_PERCENT/3/record"},
                  {"replay_path", "/api/v1/tas/sylvanox_01/ANY_PERCENT/3/replay"}};
    Json list = {{"runs", Json::array({entry})}};
    CHECK(store.wanted(list, "https://srv", [&](const std::string&) { return before->hash; }).size() == 1);
    CHECK(store.wanted(list, "https://srv", [&](const std::string&) { return after->hash; }).empty());
    entry["sim_version"] = kSimulationVersion + 1;  // other rules of the simulation: neither
    CHECK(store.wanted({{"runs", Json::array({entry})}}, "https://srv", [&](const std::string&) { return before->hash; }).empty());
}

// The run shipped with sylvanox_01, played again as every game plays a downloaded run; the same
// replay claiming another time, or with its inputs changed, is refused, and never shown.
TEST_CASE("tas training: a downloaded run is played again before it is shown; one that does not reproduce is refused") {
    ContentDatabase content;
    content.reload();
    LevelFingerprints fps;
    auto shipped = tasFor(content, fps, "sylvanox_01", RunCategory::AnyPercent);
    if (!shipped) {
        std::printf("  AVERTISSEMENT : data/tas/sylvanox_01/ANY_PERCENT est à recalculer (niveau modifié) : vérification non testée\n");
        return;
    }
    auto level = content.loadLevel("sylvanox_01");
    auto replay = loadTasReplay(*shipped);
    CHECK(level.has_value() && replay.has_value());
    if (!level || !replay) return;
    const TasCheck good = verifyTasReplay(content, *level, *shipped, *replay);
    CHECK(good.ok && good.timeMs == shipped->timeMs);
    // Claims a faster time (the replay's header says it too): refused.
    TasRecord lie = *shipped;
    lie.timeMs -= 500;
    ReplayData lying = *replay;
    lying.durationMs = static_cast<uint32_t>(lie.timeMs);
    const TasCheck bad = verifyTasReplay(content, *level, lie, lying);
    CHECK(!bad.ok);
    std::printf("  temps annoncé faux : %s\n", bad.why.c_str());
    // Its inputs changed (no jump for 2 s): desynchronised, or not the run it claims.
    ReplayData tampered = *replay;
    for (size_t i = 1200; i < 1440 && i < tampered.inputs.size(); ++i) tampered.inputs[i].held &= ~(1u << static_cast<int>(Action::Jump));
    const TasCheck bad2 = verifyTasReplay(content, *level, *shipped, tampered);
    CHECK(!bad2.ok);
    std::printf("  entrées changées : %s\n", bad2.why.c_str());
    // Category rules: an ANY% run with damage is not a « Sans dégât » run.
    if (shipped->damageTaken > 0) {
        TasRecord noDamage = *shipped;
        noDamage.category = RunCategory::NoDamage;
        CHECK(!verifyTasReplay(content, *level, noDamage, *replay).ok);
    }

    // Through the store and the verifier's thread, as the game does it.
    const fs::path dir = freshDir("store");
    TasOnlineStore store(dir);
    TasVerifier verifier;
    auto submit = [&](const TasRecord& rec, const ReplayData& rep, int revision) {
        const std::vector<uint8_t> bytes = rep.encode();
        const std::string replayBytes(bytes.begin(), bytes.end());
        const std::string recordText = rec.toJson().dump(1);
        TasOnlineEntry e;
        e.levelId = rec.levelId;
        e.category = rec.category;
        e.revision = revision;
        e.levelHash = rec.levelHash;
        e.simVersion = rec.simVersion;
        e.timeMs = rec.timeMs;
        e.recordSha256 = Sha256::hex(recordText);
        e.replaySha256 = Sha256::hex(replayBytes);
        e.recordSize = static_cast<int64_t>(recordText.size());
        e.replaySize = static_cast<int64_t>(replayBytes.size());
        auto read = TasOnlineStore::readDownload(e, recordText, replayBytes);
        CHECK(read.has_value());
        // Bytes other than the server's: refused before any check.
        CHECK(!TasOnlineStore::readDownload(e, recordText + " ", replayBytes));
        TasVerifier::Job job;
        job.entry = e;
        job.record = *read;
        job.recordText = recordText;
        job.replayBytes = replayBytes;
        job.userDir = Paths::userDir();
        verifier.submit(std::move(job));
        std::vector<TasVerifier::Result> results;
        for (int i = 0; i < 600 && results.empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            results = verifier.poll();
        }
        CHECK(results.size() == 1);
        if (results.empty()) return TasCheck{};
        store.noteResult(results.front());
        return results.front().check;
    };
    const TasCheck refused = submit(lie, lying, 1);
    CHECK(!refused.ok);
    CHECK(!bestTasAmong("sylvanox_01", RunCategory::AnyPercent, shipped->levelHash, {store.runsDir()}));
    CHECK(!store.whyOf("sylvanox_01/ANY_PERCENT").empty());
    const TasCheck accepted = submit(*shipped, *replay, 2);
    CHECK(accepted.ok);
    auto kept = bestTasAmong("sylvanox_01", RunCategory::AnyPercent, shipped->levelHash, {store.runsDir()});
    CHECK(kept && kept->timeMs == shipped->timeMs);
}

TEST_CASE("tas training: the statuses of ADMINISTRATION > TAS, a changed level « À recalculer »") {
    using namespace tasadmin;
    const std::string hash(64, 'a');
    Row r;
    r.local.levelId = "sylvanox_01";
    r.local.category = RunCategory::AnyPercent;
    r.local.hash = hash;
    OnlineService offline;  // never started: the trainings wait in the queue
    TasTraining training(offline);
    CHECK(statusOf(r, training) == Status::Free);
    // The level changed: only runs of its old version → to compute again, as never trained.
    r.local.oldShippedMs = 86692;
    CHECK(recompute(r) && statusOf(r, training) == Status::Recompute);
    CHECK(statusLabel(Status::Recompute) == "À recalculer (niveau modifié)");
    r.local.oldShippedMs = 0;
    r.server.archiveMs = 86000;
    CHECK(statusOf(r, training) == Status::Recompute);
    // A run of this version here: no longer.
    r.local.shippedMs = 90000;
    CHECK(!recompute(r) && statusOf(r, training) == Status::Free);
    // A faster run of this computer, not published.
    r.local.localMs = 85000;
    CHECK(unpublished(r) && statusOf(r, training) == Status::Unpublished);
    r.server.publishedMs = 84000;
    CHECK(!unpublished(r));
    // Another administrator trains it.
    r.server.holder = "bob";
    CHECK(statusOf(r, training) == Status::HeldByOther);
    r.server.mine = true;
    CHECK(statusOf(r, training) == Status::HeldByMe);
    // Queued here.
    r.server.holder.clear();
    training.train({r.local.levelId, r.local.category});
    CHECK(training.queued({r.local.levelId, r.local.category}) && statusOf(r, training) == Status::Queued);
    training.enqueue({{r.local.levelId, r.local.category}, {"sylvanox_02", RunCategory::NoDeath}});
    CHECK(training.queue().size() == 2);
    training.stop({r.local.levelId, r.local.category});
    CHECK(training.queue().size() == 1 && statusOf(r, training) == Status::Free);

    // The server's list read for this game's version: its run, the old one apart, its training.
    Json pair = {{"level_id", "sylvanox_01"},
                 {"category", "ANY_PERCENT"},
                 {"current_hash", hash},
                 {"published", {{"revision", 3}, {"time_ms", 84000}, {"level_hash", hash}, {"sim_version", kSimulationVersion}, {"published_at", "2026-11-27T10:00:00Z"}, {"published_by", {{"username", "ada"}}}}},
                 {"archive", {{"revision", 1}, {"time_ms", 80000}, {"level_hash", std::string(64, 'b')}, {"sim_version", kSimulationVersion}}},
                 {"trainings", Json::array({{{"level_hash", hash}, {"sim_version", kSimulationVersion}, {"total_ms", 3600000}, {"total_explored", 12345}, {"sessions", 2},
                                             {"lease", {{"holder", {{"username", "bob"}}}, {"since", "2026-11-27T09:00:00Z"}, {"mine", false}}}},
                                            {{"level_hash", std::string(64, 'b')}, {"sim_version", kSimulationVersion}, {"total_ms", 99}, {"total_explored", 1}, {"sessions", 1}, {"lease", nullptr}}})}};
    Server s = serverFor(pair, hash);
    CHECK(s.publishedMs == 84000 && s.revision == 3 && s.publishedBy == "ada" && s.archiveMs == 80000);
    CHECK(s.trainingMs == 3600000 && s.explored == 12345 && s.sessions == 2 && s.holder == "bob" && !s.mine);
    // An admin whose game has the OTHER version: the « published » run is not his; nothing trained for it but 99 ms.
    Server other = serverFor(pair, std::string(64, 'b'));
    CHECK(other.publishedMs == 80000 && other.archiveMs == 84000 && other.trainingMs == 99 && other.holder.empty());
    // A version nobody published nor trained: nothing of the others counts.
    Server none = serverFor(pair, std::string(64, 'c'));
    CHECK(none.publishedMs == 0 && none.archiveMs > 0 && none.trainingMs == 0);
    CHECK(tasadmin::durationText(3600000 + 5 * 60000) == "1 h 05 min" && tasadmin::durationText(45000) == "45 s");
}

namespace {

/// A headless game (no window shown), over its menu.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 1280;
        cfg.height = 720;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~Rig() {
        app.shutdown();
        app.onSceneRender = nullptr;
    }
};

}  // namespace

TEST_CASE("tas training: ADMINISTRATION > TAS fits at 1280 x 720 and 1920 x 1080, in French and in English") {
    Rig r;
    GameServices& g = *r.g;
    AdminTas page(g, [](const std::string&, bool) {}, [](const ApiResult&) { return false; });
    // The worst cases: long names, every status, an old version, a lease of another administrator.
    const std::string hash(64, 'a');
    std::vector<tasadmin::Local> local;
    // The real names (the audit below judges their English), the longest one first.
    std::vector<const LevelEntry*> levels;
    for (const LevelEntry* e : g.content.levels())
        if (e->source == ContentSource::Builtin) levels.push_back(e);
    std::sort(levels.begin(), levels.end(), [](const LevelEntry* a, const LevelEntry* b) { return a->name.size() > b->name.size(); });
    CHECK(levels.size() >= 4);
    if (levels.size() < 4) return;
    for (int i = 0; i < 4; ++i)
        for (RunCategory c : kAllCategories) {
            tasadmin::Local l;
            l.levelId = "level_" + std::to_string(i);
            l.levelName = levels[static_cast<size_t>(i)]->name;
            l.planetId = levels[static_cast<size_t>(i)]->planet;
            l.planetName = levels[static_cast<size_t>(i)]->planet;
            for (const PlanetData* pl : g.content.planets())
                if (pl->id == l.planetId) l.planetName = pl->name;
            l.category = c;
            l.hash = hash;
            if (i == 0) l.shippedMs = 86692;
            if (i == 1) l.oldShippedMs = 3599999;
            if (i == 2) l.localMs = 1234567;
            local.push_back(l);
        }
    page.setLocal(local);
    Json pairs = Json::array();
    pairs.push_back({{"level_id", "level_0"},
                     {"category", "ANY_PERCENT"},
                     {"published", {{"revision", 12}, {"time_ms", 86000}, {"level_hash", hash}, {"sim_version", kSimulationVersion}, {"published_at", "2026-11-27T10:00:00Z"}, {"published_by", {{"username", "UnTresLongPseudo"}}}}},
                     {"trainings", Json::array({{{"level_hash", hash}, {"sim_version", kSimulationVersion}, {"total_ms", 987654321}, {"total_explored", 123456789012LL}, {"sessions", 9},
                                                 {"lease", {{"holder", {{"username", "Administrateur_Tres_Long"}}}, {"since", "2026-11-27T09:00:00Z"}, {"mine", false}}}}})}});
    pairs.push_back({{"level_id", "level_1"}, {"category", "NO_DEATH"}, {"archive", {{"revision", 1}, {"time_ms", 80000}, {"level_hash", std::string(64, 'b')}, {"sim_version", kSimulationVersion}}}});
    page.applyServer({{"pairs", pairs}});
    g.tasTraining.enqueue({{"level_3", RunCategory::HundredPercent}});  // offline: stays in the queue
    CHECK(tasadmin::statusOf(page.rows()[4 + 2], g.tasTraining) == tasadmin::Status::Recompute);  // level_1, 100 %

    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            r.app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            r.app.renderer().refreshOutputSize();
            i18n::setLanguage(lang);
            r.app.runFrames(1);
            r.app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(r.app, 1.0f / 60.0f);
            // The body of ADMINISTRATION as AdminScene places it (its tabs on their own line: the least room).
            const float W = std::min(ui.screenW() - ui.s(96), ui.s(1760)), x0 = (ui.screenW() - W) * 0.5f;
            const float bodyY = ui.s(36) + ui.s(52) + ui.s(22) + ui.s(52) + ui.s(16);
            const Rect body{x0, bodyY, W, ui.screenH() - bodyY - ui.s(64)};
            const Rect inner{body.x + ui.s(22), body.y + ui.s(18), body.w - ui.s(44), body.h - ui.s(36)};
            if (lang == Language::English) i18n::startAudit();
            page.draw(inner);
            ui.end();
            if (lang == Language::English) {
                // Nothing of the page left in French (the names of the administrators excepted: verbatim).
                for (const auto& f : i18n::auditFindings()) {
                    std::printf("  [en %.0f] %s : « %s » (%s)\n", size.x, f.reason.c_str(), f.text.c_str(), f.part.c_str());
                    CHECK(false);
                }
                i18n::stopAudit();
            }
            const AdminTasLayout& L = page.lastLayout();
            const char* where = lang == Language::French ? "fr" : "en";
            CHECK(L.rowsDrawn >= 3);  // the list keeps room for its rows
            CHECK(L.list.y < inner.bottom() - ui.s(150));
            int checked = 0;
            for (size_t i = 0; i < L.texts.size(); ++i) {
                const auto& t = L.texts[i];
                if (t.r.y >= L.list.y - 0.5f && t.r.bottom() > L.list.bottom() + 0.5f) continue;  // a row cut by the bottom of the list
                ++checked;
                CHECK(t.r.x >= inner.x - 0.5f && t.r.right() <= inner.right() + 0.5f && t.r.y >= inner.y - 0.5f);
                i18n::Verbatim v(t.verbatim);
                const float w = ui.text().measure(t.text, t.face, ui.s(t.size)).x;
                if (w > t.r.w + 0.5f) std::printf("  [%s %.0f] « %s » : %.1f > %.1f\n", where, size.x, t.text.c_str(), w, t.r.w);
                CHECK(w <= t.r.w + 0.5f);
                CHECK(ui.s(t.size) <= t.r.h + 0.5f);
                for (size_t j = i + 1; j < L.texts.size(); ++j)
                    if (overlaps(t.r, L.texts[j].r)) {
                        std::printf("  [%s %.0f] « %s » sur « %s »\n", where, size.x, t.text.c_str(), L.texts[j].text.c_str());
                        CHECK(false);
                    }
                for (const Rect& b : L.buttons)
                    if (overlaps(t.r, b)) {
                        std::printf("  [%s %.0f] « %s » sous un bouton\n", where, size.x, t.text.c_str());
                        CHECK(false);
                    }
            }
            CHECK(checked > 20);
            for (size_t i = 0; i < L.buttons.size(); ++i) {
                const Rect& b = L.buttons[i];
                if (b.y >= L.list.y - 0.5f && b.bottom() > L.list.bottom() + 0.5f) continue;
                CHECK(b.x >= inner.x - 0.5f && b.right() <= inner.right() + 0.5f);
                for (size_t j = i + 1; j < L.buttons.size(); ++j) CHECK(!overlaps(b, L.buttons[j]));
            }
        }
    i18n::setLanguage(Language::French);
    g.tasTraining.clearQueue();
}
