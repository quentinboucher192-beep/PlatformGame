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
#include "game/tas/TasCurve.h"
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
    // Searching, and its first window of attempts counted (every 5 s): the real counts of the search.
    for (int i = 0; i < 1200 && !(seen && seen->phase == TasPhase::Searching && seen->explored > 0 && !seen->windows.empty()); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (auto t = FileUtil::readText(progress)) seen = TasProgress::parse(*t);
    }
    CHECK(seen && seen->phase == TasPhase::Searching && seen->levelId == "sylvanox_01" && seen->category == RunCategory::NoDamage);
    CHECK(seen && seen->threads == 1 && seen->round >= 1);
    CHECK(seen && !seen->windows.empty() && seen->sections >= 1 && seen->section >= 1);
    if (seen && !seen->windows.empty()) {
        const TasAttemptWindow& w = seen->windows.front();
        CHECK(w.attempts > 0 && w.attempts <= seen->explored && w.round >= 1 && w.sections == seen->sections && w.endMs > w.startMs);
        std::printf("  première fenêtre : %lld essais, %lld finis, %lld arrêtés en section %d (%lld morts), parcours médian %d ‰\n", static_cast<long long>(w.attempts),
                    static_cast<long long>(w.finished), static_cast<long long>(w.stopped.empty() ? 0 : w.stopped[static_cast<size_t>(std::max(0, w.section - 1))]),
                    w.section, static_cast<long long>(w.brokenTotal()), w.routeMedian);
    }
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

// The attempts counted in windows (cosmic_tas --progress): exact counts, a median from a bounded
// sample, sections that add up; refused when inconsistent; merged.
TEST_CASE("tas curve: the windows of attempts of the progress file") {
    TasAttemptStats st;
    st.setSections(3);
    for (int i = 0; i < 1000; ++i) st.addFinished(40000 + i);  // more than the sample
    st.addStopped(0, 100, true);
    st.addStopped(1, 500, false);
    st.addStopped(1, 600, true);
    st.addStopped(7, 990, false);  // beyond the last section: counted in it
    TasAttemptWindow w = st.take();
    CHECK(w.attempts == 1004 && w.finished == 1000 && w.bestMs == 40000 && w.worstMs == 40999);
    CHECK(w.medianMs > 40200 && w.medianMs < 40800);  // the sample's median, close to the true one
    CHECK(w.stopped == std::vector<int64_t>({1, 2, 1}) && w.broken == std::vector<int64_t>({1, 1, 0}));
    CHECK(w.unfinished() == 4 && w.brokenTotal() == 2 && w.routeMax == 990);
    CHECK(w.reached(0) == 1004 && w.reached(1) == 1003 && w.reached(2) == 1001 && w.reached(3) == 1000);
    CHECK(st.empty() && st.take().attempts == 0);  // taken: empty again
    // Through the JSON of the progress file.
    w.id = 3;
    w.startMs = 5000;
    w.endMs = 10000;
    w.round = 2;
    w.section = 2;
    auto back = TasAttemptWindow::fromJson(w.toJson());
    CHECK(back && back->attempts == 1004 && back->round == 2 && back->section == 2 && back->stopped == w.stopped && back->medianMs == w.medianMs);
    // Inconsistent: refused.
    Json bad = w.toJson();
    bad["finished"] = 2000;
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["stopped"] = Json::array({1, 2, 2});
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["broken"] = Json::array({2, 0, 0});
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["bestMs"] = 50000;
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["sections"] = 40;
    CHECK(!TasAttemptWindow::fromJson(bad));
    // Merged: counts added, best and worst kept, the median weighted.
    TasAttemptWindow a = w, b = w;
    b.finished = 0;
    b.bestMs = b.medianMs = b.worstMs = 0;
    b.attempts = 10;
    b.stopped = {10, 0, 0};
    b.broken = {5, 0, 0};
    const TasAttemptWindow m = TasAttemptWindow::merged(a, b);
    CHECK(m.attempts == 1014 && m.finished == 1000 && m.bestMs == 40000 && m.medianMs == a.medianMs && m.stopped[0] == 11 && m.broken[0] == 6);
    // In the progress file: the last kMaxWindows, in order.
    TasProgress p;
    p.levelId = "sylvanox_01";
    p.bestElapsedMs = 4000;
    p.bestExplored = 1200;
    p.elapsedMs = 9000;
    p.explored = 3000;
    p.section = 2;
    p.sections = 3;
    for (int i = 1; i <= 30; ++i) {
        TasAttemptWindow x = w;
        x.id = i;
        p.windows.push_back(x);
    }
    auto read = TasProgress::parse(p.toJson().dump());
    CHECK(read && read->windows.size() == TasProgress::kMaxWindows && read->windows.front().id == 7 && read->windows.back().id == 30);
    CHECK(read && read->bestElapsedMs == 4000 && read->bestExplored == 1200 && read->section == 2 && read->sections == 3);
}

// The curve of a pair: a point only when the best time improves, per version of the level, the
// runs published as marks, the cap; the server's points and windows taken in.
TEST_CASE("tas curve: points only on improvement, per version, publish marks, cap, server merge") {
    const std::string h1(64, 'a'), h2(64, 'b');
    TasCurve c;
    c.levelId = "sylvanox_01";
    c.category = RunCategory::AnyPercent;
    auto pt = [](int64_t time, int64_t training, TasCurveKind k = TasCurveKind::Improvement) {
        TasCurvePoint p;
        p.kind = k;
        p.timeMs = time;
        p.trainingMs = training;
        return p;
    };
    CHECK(c.addBest(h1, kSimulationVersion, pt(90000, 1000, TasCurveKind::Start)));
    CHECK(c.addBest(h1, kSimulationVersion, pt(89000, 5000)));
    CHECK(!c.addBest(h1, kSimulationVersion, pt(89000, 6000)));  // not faster: never a point
    CHECK(!c.addBest(h1, kSimulationVersion, pt(89500, 7000)));
    CHECK(!c.addBest(h1, kSimulationVersion, pt(0, 8000)));
    CHECK(c.addBest(h1, kSimulationVersion, pt(88000, 2000)));  // never before the last point
    const TasCurveVersion* v = c.find(h1, kSimulationVersion);
    CHECK(v && v->bests() == 3 && v->bestMs() == 88000 && v->points.back().trainingMs == 5000);
    for (size_t i = 1; v && i < v->points.size(); ++i) CHECK(v->points[i].timeMs < v->points[i - 1].timeMs && v->points[i].trainingMs >= v->points[i - 1].trainingMs);
    // Another version of the level: another curve; the old one untouched.
    CHECK(c.addBest(h2, kSimulationVersion, pt(120000, 100)));
    CHECK(c.versions.size() == 2 && c.find(h1, kSimulationVersion)->bests() == 3 && c.find(h2, kSimulationVersion)->bestMs() == 120000);
    // The server's list: its points (a publication among them) replace the shared ones; a point
    // of this computer still pending stays while it is the fastest.
    TasCurvePoint pending = pt(87000, 9000);
    pending.share = TasShare::Pending;
    pending.leaseId = "lease-1";
    pending.foundAtMs = 1234;
    CHECK(c.addBest(h1, kSimulationVersion, pending));
    std::vector<TasCurvePoint> server = {pt(90000, 1000, TasCurveKind::Start), pt(88500, 4000), pt(88400, 8000, TasCurveKind::Publish)};
    server.back().revision = 2;
    CHECK(c.mergeServerPoints(h1, kSimulationVersion, server));
    v = c.find(h1, kSimulationVersion);
    CHECK(v && v->points.size() == 4 && v->bestMs() == 87000);
    CHECK(v && v->points[2].kind == TasCurveKind::Publish && v->points[2].revision == 2 && v->points.back().share == TasShare::Pending);
    CHECK(!c.mergeServerPoints(h1, kSimulationVersion, server));  // the same again: nothing changed
    // What a heartbeat carries, then its answer: the pending point replaced by the server's.
    const TasTraining::CurveSend sent = TasTraining::curveSend(*v, "lease-1", 1234 + 5000);
    CHECK(sent.points.size() == 1 && sent.pointIds.size() == 1 && sent.points[0]["age_ms"] == 5000 && sent.points[0]["time_ms"] == 87000);
    TasCurvePoint placed = pt(87000, 9100);
    placed.by = "ada";
    c.acknowledge(h1, kSimulationVersion, "lease-1", sent.pointIds, {placed}, {});
    v = c.find(h1, kSimulationVersion);
    CHECK(v && v->points.back().share == TasShare::Shared && v->points.back().trainingMs == 9100 && v->points.back().by == "ada");
    CHECK(TasTraining::curveSend(*v, "lease-1", 99999).points.empty());
    // A lease that ended: its pending points stay here only.
    TasCurvePoint lost = pt(86000, 9500);
    lost.share = TasShare::Pending;
    lost.leaseId = "lease-2";
    CHECK(c.addBest(h1, kSimulationVersion, lost));
    c.abandon("lease-2");
    CHECK(c.find(h1, kSimulationVersion)->points.back().share == TasShare::Unshared);
    // The cap: at kMaxPoints the last one gives way; the start and the best stay.
    TasCurve big;
    for (int i = 0; i < static_cast<int>(TasCurve::kMaxPoints) + 50; ++i) big.addBest(h1, kSimulationVersion, pt(200000 - i, i));
    const TasCurveVersion* bv = big.find(h1, kSimulationVersion);
    CHECK(bv && bv->bests() == TasCurve::kMaxPoints && bv->points.front().timeMs == 200000 && bv->bestMs() == 200000 - (static_cast<int>(TasCurve::kMaxPoints) + 49));
    // Through the file: what was pending comes back unshared (its lease ended with the game).
    TasCurveWindow w;
    w.fromMs = 1000;
    w.toMs = 6000;
    w.session = 3;
    w.local = true;
    w.share = TasShare::Pending;
    w.leaseId = "lease-3";
    w.stats.id = 1;
    w.stats.round = 1;
    w.stats.section = 1;
    w.stats.sections = 2;
    w.stats.attempts = 100;
    w.stats.finished = 1;
    w.stats.bestMs = w.stats.medianMs = w.stats.worstMs = 88000;
    w.stats.stopped = {90, 9};
    w.stats.broken = {3, 0};
    c.addWindow(h1, kSimulationVersion, w);
    auto file = TasCurve::fromJson(c.toJson());
    CHECK(file && file->versions.size() == 2);
    const TasCurveVersion* fv = file ? file->find(h1, kSimulationVersion) : nullptr;
    CHECK(fv && fv->bestMs() == 86000 && fv->windows.size() == 1 && fv->windows[0].share == TasShare::Unshared && fv->windows[0].stats.attempts == 100);
    CHECK(!TasCurve::fromJson(Json::object()));
    // The server's windows: those of a session counted here are left out (its finer ones stay).
    TasCurveWindow mine = w, theirs = w;
    theirs.local = false;
    theirs.session = 4;
    theirs.fromMs = 7000;
    theirs.toMs = 67000;
    CHECK(c.mergeServerWindows(h1, kSimulationVersion, {mine, theirs}));
    const TasCurveVersion* wv = c.find(h1, kSimulationVersion);
    CHECK(wv && wv->windows.size() == 2 && wv->windows[0].local && !wv->windows[1].local && wv->windows[1].session == 4);
    const TasAttemptWindow total = tasAttemptTotal({&wv->windows[0], &wv->windows[1]});
    CHECK(total.attempts == 200 && total.finished == 2 && total.stopped[0] == 180);
    // The rounds marked along the training time.
    const auto starts = tasadmin::roundStarts(wv->windows);
    CHECK(starts.size() == 2 && starts[0].atMs == 1000 && starts[1].atMs == 7000 && starts[1].session == 4);
}

TEST_CASE("tas curve: a heartbeat's summary of the windows is bounded; the book writes and reads the curves") {
    const std::string h(64, 'c');
    TasCurve c;
    c.levelId = "sylvanox_02";
    c.category = RunCategory::NoDeath;
    for (int i = 1; i <= 12; ++i) {
        TasCurveWindow w;
        w.fromMs = i * 5000;
        w.toMs = w.fromMs + 5000;
        w.local = true;
        w.share = TasShare::Pending;
        w.leaseId = "L";
        w.endAtMs = 100000 + i * 5000;
        w.stats.id = i;
        w.stats.round = i <= 6 ? 1 : 2;
        w.stats.section = i % 3;
        w.stats.sections = 3;
        w.stats.attempts = 1000;
        w.stats.stopped = {500, 400, 100};
        w.stats.broken = {10, 0, 0};
        c.addWindow(h, kSimulationVersion, w);
    }
    const TasCurveVersion* v = c.find(h, kSimulationVersion);
    const auto sent = TasTraining::curveSend(*v, "L", 200000);
    CHECK(sent.windows.size() == TasTraining::kSendWindows && sent.windowIds.size() == 12);
    int64_t attempts = 0;
    for (const Json& w : sent.windows) {
        attempts += w["attempts"].get<int64_t>();
        CHECK(w["duration_ms"].get<int64_t>() > 0 && w["stopped"].size() == 3);
    }
    CHECK(attempts == 12000);
    c.acknowledge(h, kSimulationVersion, "L", {}, {}, sent.windowIds);
    CHECK(TasTraining::curveSend(*c.find(h, kSimulationVersion), "L", 200000).windows.empty());
    CHECK(c.find(h, kSimulationVersion)->windows.size() == 12);  // this computer keeps its fine ones
    // The book: written on its thread, read back on another.
    const fs::path dir = freshDir("curves");
    {
        TasCurveBook book;
        book.open(dir);
        book.at(c.levelId, c.category) = c;
        book.changed(c.levelId, c.category, true);
        book.update(0.0f);
        book.flush();
    }
    CHECK(fs::exists(TasCurveBook::fileOf(dir, c.levelId, c.category)));
    TasCurveBook again;
    again.open(dir);
    for (int i = 0; i < 200 && !again.loaded(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        again.update(0.01f);
    }
    const TasCurve* read = again.find(c.levelId, c.category);
    CHECK(again.loaded() && read && read->find(h, kSimulationVersion) && read->find(h, kSimulationVersion)->windows.size() == 12);
    // The axes' helpers.
    CHECK(tasadmin::durationTick(0) == "0" && tasadmin::durationTick(720000) == "12 min" && tasadmin::durationTick(5400000) == "1 h 30" && tasadmin::durationTick(7200000) == "2 h");
    CHECK(tasadmin::compactCount(9999) == "9 999" && tasadmin::compactCount(12345) == "12,3 k" && tasadmin::compactCount(12300000) == "12,3 M");
    CHECK(tasadmin::percentText(4, 10000) == "0,04 %" && tasadmin::percentText(1, 2) == "50 %" && tasadmin::percentText(0, 0) == "—");
    const auto ticks = tasadmin::axisTicks(0, 3600000, 5, tasadmin::durationSteps());
    CHECK(!ticks.empty() && ticks.size() <= 6 && ticks.front() == 0);
    for (size_t i = 1; i < ticks.size(); ++i) CHECK(ticks[i] - ticks[i - 1] == ticks[1] - ticks[0]);
    const auto yt = tasadmin::axisTicks(86000, 87000, 5, tasadmin::runTimeSteps());
    CHECK(yt.size() >= 2 && yt.size() <= 6 && yt.front() >= 86000 && yt.back() <= 87000);
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

    // The training curve of level_0 ANY% (shipped and published: two reference lines): a start,
    // improvements, a publication, windows of attempts on three sections over two sessions and
    // rounds; an older version kept apart. Opened under the table, as a click on its row does.
    {
        TasCurve& c = g.tasTraining.curves().at("level_0", RunCategory::AnyPercent);
        c.versions.clear();
        auto pt = [](TasCurveKind k, int64_t t, int64_t x) {
            TasCurvePoint p;
            p.kind = k;
            p.timeMs = t;
            p.trainingMs = x;
            p.explored = x * 40;
            p.at = "2026-11-27T10:00:00Z";
            p.by = "Administrateur_Tres_Long";
            // 2.99: its splits at the three checkpoints (a gain in every segment).
            if (k != TasCurveKind::Publish) p.splits = {11000 - (86692 - t) / 4, 30000 - (86692 - t) / 2, 49000 - (86692 - t) * 3 / 4};
            p.source = k == TasCurveKind::Improvement ? "splice" : "";
            return p;
        };
        // Along the 11 days of training the server counts for it (987 654 321 ms).
        CHECK(c.addBest(hash, kSimulationVersion, pt(TasCurveKind::Start, 86692, 0)));
        CHECK(c.addBest(hash, kSimulationVersion, pt(TasCurveKind::Improvement, 86400, 100000000)));
        CHECK(c.addBest(hash, kSimulationVersion, pt(TasCurveKind::Improvement, 85912, 300000000)));
        CHECK(c.addBest(hash, kSimulationVersion, pt(TasCurveKind::Improvement, 85100, 900000000)));
        std::vector<TasCurvePoint> withPublish = c.find(hash, kSimulationVersion)->points;
        withPublish.push_back(pt(TasCurveKind::Publish, 85912, 350000000));
        withPublish.back().revision = 12;
        c.mergeServerPoints(hash, kSimulationVersion, withPublish);
        for (int i = 0; i < 60; ++i) {
            TasCurveWindow w;
            w.fromMs = int64_t(i) * 16000000;
            w.toMs = w.fromMs + 10000000;
            w.session = 1 + i / 30;
            w.local = true;
            w.by = "Administrateur_Tres_Long";
            w.stats.id = i + 1;
            w.stats.round = 1 + (i / 10) % 3;
            w.stats.section = 1 + i % 3;
            w.stats.sections = 8;  // more sections than « Arrêts par section » shows folded
            w.stats.attempts = 120000;
            w.stats.finished = 30;
            w.stats.bestMs = 85000 + i * 10;
            w.stats.medianMs = 88000;
            w.stats.worstMs = 95000;
            w.stats.routeMedian = 420;
            w.stats.routeMax = 980;
            w.stats.stopped = {30000, 20000, 15000, 15000, 10000, 10000, 10000, 9970};
            w.stats.broken = {2000, 500, 0, 0, 0, 0, 0, 0};
            // 2.98: the round's window (this computer's), the finished attempts as points (one
            // far slower than the others: above the scale), the times at three checkpoints.
            w.stats.focusFirst = i < 10 ? 0 : 1 + i % 2;
            w.stats.focusLast = i < 10 ? 0 : 2 + i % 2;
            for (int k = 0; k < 30; ++k) w.stats.finishes.push_back({int64_t(k) * 300000, k == 0 ? w.stats.bestMs : k == 29 ? 95000 : 86000 + (k * 37 + i * 11) % 3000});
            for (int cp = 0; cp < 3; ++cp) {
                TasAttemptWindow::Checkpoint ck;
                ck.reached = 50000 - cp * 10000;
                ck.bestMs = 11000 + cp * 19000 - i * 3;
                ck.medianMs = ck.bestMs + 400;
                ck.worstMs = ck.bestMs + 2000;
                for (int k = 0; k < 8; ++k) ck.sample.push_back({int64_t(k) * 1000000, k == 0 ? ck.bestMs : ck.bestMs + 100 + k * 150});
                w.stats.checkpoints.push_back(ck);
            }
            c.addWindow(hash, kSimulationVersion, w);
        }
        c.version(hash, kSimulationVersion).checkpoints = {{"o63", "Seuil des Anciens"}, {"o140", "Canopée brisée"}, {"o206", ""}};
        // 2.99: a player's run guides it (its splits: the references at each checkpoint, the sum of the best segments).
        c.version(hash, kSimulationVersion).references = {{"UnTresLongPseudo", 85000, {9500, 26000, 43000}},
                                                          {"NovaRider", 0, {9000, 25000, 41000}, true}};  // an older version: its route only
        CHECK(c.addBest(std::string(64, 'b'), kSimulationVersion, pt(TasCurveKind::Improvement, 80000, 1000)));
        page.select("level_0", RunCategory::AnyPercent);
        CHECK(page.selected("level_0", RunCategory::AnyPercent) && page.capturesBack());
    }

    // Every tab (« TOUT », CP1, CP3), in the default view and zoomed as deep as it goes.
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}})
        for (Language lang : {Language::French, Language::English})
            for (int pass = 0; pass < 7; ++pass) {
            const int tabNow = pass == 0 || pass == 3 || pass >= 5 ? 0 : pass == 1 || pass == 4 ? 1 : 3;
            const bool deep = pass == 3 || pass == 4;
            // Passes 5 and 6: « Arrêts par section » unfolded (every section, scrolled), then folded again.
            page.setStopsOpen(pass == 5);
            page.setCurveTab(tabNow);
            if (deep) {
                charts::ChartView& zv = page.curveView(tabNow);
                zv.reset();
                // Around the best point of the last window (« TOUT »: its best finished attempt; CPk: its best time there).
                const double t = tabNow == 0 ? 85000.0 + 59 * 10 : 11000.0 + (tabNow - 1) * 19000.0 - 59 * 3;
                for (int z = 0; z < 80; ++z) zv.zoomAt(59.0 * 16000000.0, t, 0.5, 0.5);
            } else {
                page.curveView(tabNow).reset();
            }
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
            // The curve's plot focused with the keyboard: the tooltip of the point nearest its reticle
            // (on « TOUT », its last best time).
            ui.setFocus("tas/curve/plot");
            ui.setUsingPointer(false);
            page.draw(inner);
            if (!page.tip().show) std::printf("  [%.0f onglet %d%s] pas de bulle au clavier\n", size.x, tabNow, deep ? " zoom" : "");
            CHECK(page.tip().show && !page.tip().title.empty() && page.tip().rows.size() >= (tabNow == 0 && !deep ? 5u : 3u));
            const Rect tipR = charts::tipBox(ui, page.tip());
            CHECK(tipR.x >= -0.5f && tipR.right() <= ui.screenW() + 0.5f && tipR.y >= -0.5f && tipR.bottom() <= ui.screenH() + 0.5f);
            page.drawTooltip();
            ui.end();
            const AdminTasLayout pointLayout = page.lastLayout();
            // Then the strip of the sections: the tooltip of a window of attempts.
            r.app.runFrames(1);
            ui.begin(r.app, 1.0f / 60.0f);
            ui.setFocus("tas/curve/strip");
            ui.setUsingPointer(false);
            page.draw(inner);
            CHECK(page.tip().show && page.tip().rows.size() >= 6);
            CHECK(page.curveTab() == tabNow);
            page.drawTooltip();
            ui.end();
            ui.setUsingPointer(true);
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
            if (L.rowsDrawn < 3 || L.detail.empty() || (pass == 0 && (L.pointsDrawn != 5 || L.windowsDrawn != 60 || L.roundsDrawn < 2 || L.referenceLines != 4)))
                std::printf("  [%s %.0f onglet %d%s] lignes %d, courbe %.0f x %.0f, points %d, fenêtres %d, références %d, tours %d, vide %d\n", where, size.x, tabNow,
                            deep ? " zoom" : "", L.rowsDrawn, L.detail.w, L.detail.h, L.pointsDrawn, L.windowsDrawn, L.referenceLines, L.roundsDrawn, L.emptyCurve ? 1 : 0);
            CHECK(L.rowsDrawn >= 3);  // the list keeps room for its rows, under it the curve
            CHECK(L.list.y < inner.bottom() - ui.s(150));
            // The curve: under the table, inside the page; its tabs, its plot, its strip, its counts beside.
            CHECK(!L.detail.empty() && L.detail.y >= L.list.bottom() && L.detail.bottom() <= inner.bottom() + 0.5f);
            CHECK(L.detail.containsRect(L.plot) && L.detail.containsRect(L.strip) && L.detail.containsRect(L.stats));
            CHECK(L.plot.w > ui.s(400) && L.plot.h > ui.s(80) && L.strip.y > L.plot.bottom() && L.stats.x > L.plot.right());
            CHECK(L.tabs.size() == 4);  // « TOUT », CP1, CP2, CP3
            for (size_t t = 0; t < L.tabs.size(); ++t) {
                CHECK(L.detail.containsRect(L.tabs[t]) && L.tabs[t].bottom() <= L.plot.y);
                for (size_t u = t + 1; u < L.tabs.size(); ++u) CHECK(!overlaps(L.tabs[t], L.tabs[u]));
                for (const Rect& b : L.buttons) CHECK(!overlaps(L.tabs[t], b));
            }
            if (L.xTicks < 1 || L.yTicks < 2 || (L.cloudDrawn <= 0 && L.pointsDrawn <= 0) || L.emptyCurve || L.windowsDrawn < 1)
                std::printf("  [%s %.0f onglet %d%s] graduations %d x %d, nuage %d, vide %d, fenêtres %d, vue %.0f..%.0f x %.1f..%.1f\n", where, size.x, tabNow,
                            deep ? " zoom" : "", L.xTicks, L.yTicks, L.cloudDrawn, L.emptyCurve ? 1 : 0, L.windowsDrawn, L.view.x0, L.view.x1, L.view.y0, L.view.y1);
            CHECK(!L.emptyCurve && (L.cloudDrawn > 0 || L.pointsDrawn > 0) && L.windowsDrawn >= 1 && L.xTicks >= 1 && L.yTicks >= 2);
            if (pass == 0) {
                // Shipped, online, the guide and the sum of the best segments.
                CHECK(L.pointsDrawn == 5 && L.windowsDrawn == 60 && L.referenceLines == 4 && L.roundsDrawn >= 2);
                CHECK(L.cloudKept == 60 * 30 && L.offView >= 1);  // every point kept; the slowest above the scale, said
            }
            if (pass >= 5) {
                // « Arrêts par section »: folded, « … N autres sections » unfolds it when they do not all fit;
                // unfolded, every section scrolls in its region, the arrival pinned under it, « Réduire » after.
                if (L.stopRows == 0) std::printf("  [%s %.0f] arrêts par section : pas de place dans les chiffres\n", where, size.x);
                CHECK(L.stopRows == 0 || (L.stopsOpen == (pass == 5) && !L.stopsArrival.empty()));
                if (pass == 5 && !L.stopsList.empty()) {
                    CHECK(L.stats.containsRect(L.stopsList) && L.stopsArrival.y >= L.stopsList.bottom() - 0.5f);
                    CHECK(!L.stopsToggle.empty() && L.stopsToggle.y >= L.stopsArrival.bottom() - 0.5f && L.stats.containsRect(L.stopsToggle));
                }
                if (pass == 6 && L.stopRows > 0 && L.stopRows < 8) CHECK(!L.stopsToggle.empty() && L.stopsToggle.bottom() <= L.stopsArrival.y + 0.5f);
                if (pass == 6 && L.stopRows < 8 && size.x < 1500) std::printf("  [%s %.0f] arrêts : %d sections sur 8 repliées\n", where, size.x, L.stopRows);
            }
            if (!deep && tabNow > 0) {
                // A CP tab: the gains marked where its segment changed, the guide's time there and the gap.
                CHECK(L.gainMarks >= 1 && L.referenceLines >= 2 && L.referenceGaps == 2);
            }
            if (deep) {
                // As deep as it goes: a few seconds, a few milliseconds; the overview and « Réinitialiser le zoom » shown.
                CHECK(L.view.zoomed && L.view.width() <= 3000.0 + 1e-6 && L.view.height() <= 4.0 + 1e-6);
                CHECK(!L.minimap.empty() && L.plot.containsRect(L.minimap) && !L.zoomReset.empty() && L.detail.containsRect(L.zoomReset));
            } else {
                CHECK(!L.view.zoomed && L.minimap.empty());
            }
            CHECK(L.sparklines >= 1);
            CHECK(pointLayout.texts.size() == L.texts.size());  // the same page whichever tooltip shows
            int checked = 0;
            for (size_t i = 0; i < L.texts.size(); ++i) {
                const auto& t = L.texts[i];
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
                CHECK(b.x >= inner.x - 0.5f && b.right() <= inner.right() + 0.5f && b.bottom() <= inner.bottom() + 0.5f);
                for (size_t j = i + 1; j < L.buttons.size(); ++j) CHECK(!overlaps(b, L.buttons[j]));
            }
            }
    page.setCurveTab(0);
    page.curveView(0).reset();
    // The row selected again closes the curve; a pair never trained says so.
    i18n::setLanguage(Language::French);
    page.select("level_0", RunCategory::AnyPercent);
    CHECK(!page.capturesBack());
    page.select("level_3", RunCategory::NoDeath);
    r.app.runFrames(1);
    g.ui.begin(r.app, 1.0f / 60.0f);
    page.draw({40, 40, 1200, 640});
    g.ui.end();
    CHECK(page.lastLayout().emptyCurve && !page.lastLayout().detail.empty());
    bool said = false;
    for (const auto& t : page.lastLayout().texts) said = said || t.text.find("jamais été entraînée") != std::string::npos;
    CHECK(said);
    page.select("level_3", RunCategory::NoDeath);
    i18n::setLanguage(Language::French);
    g.tasTraining.clearQueue();
}
