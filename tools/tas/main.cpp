// cosmic_tas — the search of the machine's runs (docs/TAS.md). Offline: on our machines, or
// installed next to the game and launched by it for an administrator (ADMINISTRATION › TAS);
// never by itself on a player's computer, never on Cosmic Online.
//
//   cosmic_tas <levelId> <category> [--budget minutes] [--seed-replay file] [--threads n]
//              [--beam n] [--seed n] [--out folder] [--keep-seed-sections n] [--save-partial file]
//              [--until-stopped] [--round minutes] [--progress file] [--stop-file file]
//              [--stop-on-stdin] [--user-dir folder] [--server url] [--guide replay [--guide-name player]]...
//              [--portfolio] [--arms beam,explore,timing,refine] [--slice seconds] [--no-bnb] [--bound-slack s]
//              [--phase] [--phase-source replay]... [--phase-budget minutes] [--phase-wait s] [--phase-keep n]
//              [--realign replay [--realign-through n] [--realign-source replay]... [--realign-out file]]
//              [--branch replay [--branch-through n] [--branch-out folder] [--branch-share f]]
//
// <category>: ANY_PERCENT, HUNDRED_PERCENT, NO_DEATH, NO_DAMAGE (or ANY%, 100%...). The run found
// is verified by the normal replay playback, then written next to the levels:
// data/tas/<levelId>/<CATEGORY>.json and .cprz (--out: another folder). When the file already
// holds a faster run of the same level and simulation, it is kept.
//
// A training (docs/TAS.md « Entraînement depuis ADMINISTRATION »: the game launches it as a child
// process on an administrator's computer): --until-stopped searches in rounds of --round minutes,
// each one from the best run so far, until it is stopped; --progress rewrites a small JSON file
// (game/tas/TasProgress.h) about every half second; it stops cleanly when --stop-file appears or,
// with --stop-on-stdin, when its standard input reads « stop » or closes (the game closed, even
// by a crash): the search ends with the best run it has, verified and written before it exits.
//
// Every faster whole run the search holds is verified AT ONCE, on a thread of its own, while the
// search goes on (docs/TAS.md « Vérification au fil de l'eau »): written as soon as it passes,
// never before. --guide (several): players' runs of this very version of the level that guide
// the search (docs/TAS.md « Les guides »); the run written says which (guidedBy).
//
// The phase realignment (docs/TAS.md « Le réalignement de phase »): --realign continues a replay's
// early arrival with the other runs' next sections after the waits that put the moving elements back
// in phase; --phase does it at every round (the best run, the guides, the --phase-source files read
// again, the round's early arrivals). --branch commits to a replay's first sections and trains the
// rest as a line of its own (its best kept in its own file), given back to the best run's rounds.

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasProgress.h"
#include "game/tas/TasVerify.h"
#include "tools/tas/Branch.h"
#include "tools/tas/Course.h"
#include "tools/tas/Output.h"
#include "tools/tas/Phase.h"
#include "tools/tas/Portfolio.h"
#include "tools/tas/Search.h"
#include "tools/tas/Sim.h"

using namespace cosmic;

namespace {

bool parseCategory(std::string s, RunCategory& out) {
    if (categoryFromId(s, out)) return true;
    for (RunCategory c : kAllCategories)
        if (s == categoryLabel(c)) {
            out = c;
            return true;
        }
    if (s == "ANY" || s == "any") return out = RunCategory::AnyPercent, true;
    if (s == "100" || s == "100%") return out = RunCategory::HundredPercent, true;
    return false;
}

int usage() {
    std::fprintf(stderr,
                 "Usage : cosmic_tas <niveau> <catégorie> [--budget minutes] [--seed-replay fichier] [--threads n] [--beam n] [--seed n] [--out dossier] [--no-planner]\n"
                 "        [--until-stopped] [--round minutes] [--progress fichier] [--stop-file fichier] [--stop-on-stdin] [--user-dir dossier] [--server url]\n"
                 "        [--guide replay.cprz [--guide-name joueur]]...\n"
                 "        [--phase] [--phase-source replay]... [--phase-budget minutes] [--realign replay [--realign-through n] [--realign-source replay]... [--realign-out fichier]]\n"
                 "        [--branch replay [--branch-through n] [--branch-out dossier] [--branch-share f]]\n"
                 "  catégorie : ANY_PERCENT, HUNDRED_PERCENT, NO_DEATH, NO_DAMAGE\n");
    return 2;
}

int64_t clockMs(float seconds) { return static_cast<int64_t>(std::llround(static_cast<double>(seconds) * 1000.0)); }

/// The progress of a training (--progress), its stop (--stop-file, --stop-on-stdin): a thread
/// writes the file about every half second; the search reads stop.
struct Training {
    std::atomic<bool> stop{false};
    std::atomic<int64_t> explored{0};
    std::atomic<int64_t> candidateMs{0};  // the search's best clock, not verified yet
    std::mutex m, writeMutex;
    std::condition_variable cv;
    TasProgress p;  // under m (explored, candidateMs and elapsedMs filled at each write)
    fs::path progressFile, stopFile;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    bool done = false;
    std::thread writer;
    // The attempts counted in windows (TasProgress::windows): the counter under its own lock (the
    // search's threads add to it), the window's start and number under m.
    std::mutex statsMutex;
    TasAttemptStats stats;
    int64_t windowStart = 0, windowId = 0;

    int64_t elapsed() const { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count(); }
    void attempts(const tas::AttemptNote* notes, size_t n, const tas::Crossing* crossings, size_t nc) {
        const int64_t at = elapsed();
        std::lock_guard<std::mutex> l(statsMutex);
        for (size_t i = 0; i < n; ++i) {
            const tas::AttemptNote& a = notes[i];
            if (a.finished) stats.addFinished(clockMs(a.clock), at);
            else stats.addStopped(a.section, static_cast<int>(std::lround(a.route * 1000.0f)), a.broke);
        }
        for (size_t i = 0; i < nc; ++i) stats.addCrossing(crossings[i].checkpoint, clockMs(crossings[i].clock), at);
    }
    /// The window under way closed into the progress file's list (when it counted anything).
    void closeWindow() {
        TasAttemptWindow w;
        bool any = false;
        int64_t from = 0, now = 0;
        {
            std::lock_guard<std::mutex> l(m);
            now = elapsed();
            from = std::min(windowStart, now);
        }
        {
            std::lock_guard<std::mutex> s(statsMutex);
            any = !stats.empty();
            if (any) w = stats.take(from, now);
        }
        std::lock_guard<std::mutex> l(m);
        if (any && w.attempts > 0) {
            w.id = ++windowId;
            w.startMs = from;
            w.endMs = now;
            w.round = p.round;
            w.section = std::min(p.section, w.sections);
            w.focusFirst = p.focusFirst;
            w.focusLast = p.focusLast;
            p.windows.push_back(std::move(w));
            if (p.windows.size() > TasProgress::kMaxWindows) p.windows.erase(p.windows.begin());
        }
        windowStart = now;
    }
    /// A new stage of the search (a section, or the refinement): its attempts in a window of their own.
    void stage(int section, int sections) {
        closeWindow();
        {
            std::lock_guard<std::mutex> s(statsMutex);
            stats.setSections(sections);
        }
        std::lock_guard<std::mutex> l(m);
        p.sections = std::clamp(sections, 0, TasAttemptWindow::kMaxSections);
        p.section = std::clamp(section, 0, p.sections);
    }

    void write() {
        if (progressFile.empty()) return;
        TasProgress copy;
        {
            std::lock_guard<std::mutex> l(m);
            p.elapsedMs = elapsed();
            p.explored = explored.load();
            const int64_t c = candidateMs.load();
            p.candidateMs = c > 0 && (p.bestMs == 0 || c < p.bestMs) ? c : 0;
            ++p.sequence;
            copy = p;
        }
        // One writer at a time (the thread and the search's log share the file's temporary copy).
        std::lock_guard<std::mutex> w(writeMutex);
        FileUtil::writeTextAtomic(progressFile, copy.toJson().dump() + "\n");
    }
    template <class F>
    void edit(F f) {
        {
            std::lock_guard<std::mutex> l(m);
            f(p);
        }
        write();
    }
    void begin(bool stopOnStdin) {
        if (stopOnStdin) {
            // « stop », or the end of the input (the game closed, crashed, or stopped us): stop.
            std::thread([this] {
                std::string line;
                while (std::getline(std::cin, line))
                    if (line.rfind("stop", 0) == 0) break;
                stop = true;
            }).detach();
        }
        writer = std::thread([this] {
            std::unique_lock<std::mutex> l(m);
            while (!done) {
                l.unlock();
                std::error_code ec;
                if (!stopFile.empty() && fs::exists(stopFile, ec)) stop = true;
                bool due = false;
                {
                    std::lock_guard<std::mutex> w(m);
                    due = elapsed() - windowStart >= TasProgress::kWindowMs;
                }
                if (due) closeWindow();
                write();
                l.lock();
                cv.wait_for(l, std::chrono::milliseconds(500), [this] { return done; });
            }
        });
    }
    void end() {
        {
            std::lock_guard<std::mutex> l(m);
            done = true;
        }
        cv.notify_all();
        if (writer.joinable()) writer.join();
        closeWindow();
        write();
    }
};

/// The runs to verify (docs/TAS.md « Vérification au fil de l'eau »): the search hands each faster
/// whole run over (SearchOptions::onRun) and goes on; a thread verifies the fastest one waiting
/// (a slower one waiting is dropped) with `check`, which writes it when it passes.
struct Verifier {
    struct Job {
        std::vector<InputFrame> run;
        int64_t clockMs = 0;
        std::string source;
    };
    std::function<void(const Job&)> check;
    std::mutex m;
    std::condition_variable cv;
    std::optional<Job> pending;
    bool busy = false, quit = false;
    std::thread thread;

    void submit(const std::vector<InputFrame>& run, int64_t clockMs, const std::string& source) {
        {
            std::lock_guard<std::mutex> l(m);
            if (pending && pending->clockMs <= clockMs) return;
            pending = Job{run, clockMs, source};
        }
        cv.notify_all();
    }
    /// Until nothing waits and nothing is being verified.
    void drain() {
        std::unique_lock<std::mutex> l(m);
        cv.wait(l, [this] { return !pending && !busy; });
    }
    void start() {
        thread = std::thread([this] {
            std::unique_lock<std::mutex> l(m);
            while (true) {
                cv.wait(l, [this] { return quit || pending; });
                if (!pending) break;
                Job j = std::move(*pending);
                pending.reset();
                busy = true;
                l.unlock();
                check(j);
                l.lock();
                busy = false;
                cv.notify_all();
            }
        });
    }
    void stop() {
        drain();
        {
            std::lock_guard<std::mutex> l(m);
            quit = true;
        }
        cv.notify_all();
        if (thread.joinable()) thread.join();
    }
};

/// The splits of a verified run at the level's checkpoints (CP1 first: `cps`, their object ids),
/// 0 where it did not reach one (the first time it reached each).
std::vector<int64_t> splitsAt(const std::vector<TasSplit>& splits, const std::vector<std::string>& cps) {
    std::vector<int64_t> out(cps.size(), 0);
    for (const TasSplit& s : splits)
        for (size_t k = 0; k < cps.size(); ++k)
            if (cps[k] == s.checkpoint && out[k] == 0) out[k] = s.timeMs;
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2) return usage();
    auto value = [&](const char* f, const char* def) -> std::string {
        for (size_t i = 2; i + 1 < args.size(); ++i)
            if (args[i] == f) return args[i + 1];
        return def;
    };
    auto flag = [&](const char* f) { return std::find(args.begin() + 2, args.end(), std::string(f)) != args.end(); };
    const std::string levelId = args[0];
    RunCategory category;
    if (!parseCategory(args[1], category)) return usage();

    Paths::init(value("--user-dir", ""));
    Log::setMinLevel(LogLevel::Error);

    // A training (--progress, --stop-file, --stop-on-stdin, --until-stopped): its file says
    // where it is from the start, a failure included.
    Training training;
    training.progressFile = value("--progress", "").empty() ? fs::path() : Paths::fromUtf8(value("--progress", ""));
    training.stopFile = value("--stop-file", "").empty() ? fs::path() : Paths::fromUtf8(value("--stop-file", ""));
    const bool untilStopped = flag("--until-stopped");
    training.p.levelId = levelId;
    training.p.category = category;
    training.begin(flag("--stop-on-stdin"));
    auto say = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        training.edit([&](TasProgress& p) { p.message = line; });
    };
    auto finish = [&](TasPhase phase, const std::string& line, int code) {
        if (!line.empty()) std::printf("%s\n", line.c_str());
        training.edit([&](TasProgress& p) {
            p.phase = phase;
            if (!line.empty()) p.message = line;
        });
        training.end();
        SDL_Quit();
        return code;
    };

    ContentDatabase content;
    content.reload();
    // --server: the level as the game plays it on this computer (the online revisions it downloaded).
    applyOnlineRevisions(content, Paths::userDir(), value("--server", ""));
    const LevelEntry* entry = content.level(levelId);
    if (!entry || entry->source != ContentSource::Builtin) return finish(TasPhase::Failed, "Niveau inconnu ou pas un niveau du jeu : " + levelId, 2);
    std::string err;
    auto level = content.loadLevel(levelId, &err);
    if (!level) return finish(TasPhase::Failed, "Niveau illisible : " + err, 2);
    LevelFingerprints fingerprints;
    const LevelFingerprint* fp = fingerprints.get(content, levelId);
    if (!fp) return finish(TasPhase::Failed, "Pas d'empreinte de classement pour " + levelId, 2);

    tas::SearchOptions opt;
    opt.category = category;
    const double roundSeconds = std::max(0.1, std::atof(value("--round", "10").c_str())) * 60.0;
    opt.budgetSeconds = untilStopped ? roundSeconds : std::max(0.1, std::atof(value("--budget", "30").c_str())) * 60.0;
    opt.threads = std::clamp(std::atoi(value("--threads", "2").c_str()), 1, 64);
    opt.beam = std::clamp(std::atoi(value("--beam", "24").c_str()), 4, 4096);
    opt.planner = !flag("--no-planner");
    opt.phaseLeads = !flag("--no-phase-leads");
    const uint64_t baseSeed = static_cast<uint64_t>(std::strtoull(value("--seed", "1").c_str(), nullptr, 10));
    opt.seed = baseSeed;
    opt.log = [&](const std::string& s) { say(s); };
    opt.stopRequested = &training.stop;
    opt.exploredTotal = &training.explored;
    opt.onBest = [&](float clock) {
        const int64_t c = clockMs(clock);
        int64_t old = training.candidateMs.load();
        while ((old == 0 || c < old) && !training.candidateMs.compare_exchange_weak(old, c)) {
        }
    };
    // The attempts counted for the training's windows (only when someone reads the progress), with
    // the level's checkpoints they reach (CP1 first: the course's order, the level's labels).
    if (!training.progressFile.empty()) {
        opt.onAttempts = [&](const tas::AttemptNote* notes, size_t n, const tas::Crossing* crossings, size_t nc) { training.attempts(notes, n, crossings, nc); };
        opt.onStage = [&](int section, int sections) { training.stage(section, sections); };
    }
    training.p.threads = opt.threads;
    // The portfolio of strategies (docs/TAS.md « Le portefeuille »): --portfolio (all the arms) or
    // --arms a,b (only those), --slice <s> (a slice's length), --no-bnb, --bound-slack <s>.
    std::unique_ptr<tas::Portfolio> portfolio;
    if (flag("--portfolio") || !value("--arms", "").empty()) {
        portfolio = std::make_unique<tas::Portfolio>(content, *level, category, opt.threads, baseSeed);
        portfolio->sliceSeconds = std::clamp(std::atof(value("--slice", "45").c_str()), 5.0, 600.0);
        portfolio->branchAndBound = !flag("--no-bnb");
        portfolio->boundSlack = static_cast<float>(std::max(0.0, std::atof(value("--bound-slack", "0").c_str())));
        std::vector<std::string> arms;
        for (std::string rest = value("--arms", ""); !rest.empty();) {
            const size_t c = rest.find(',');
            arms.push_back(rest.substr(0, c));
            rest = c == std::string::npos ? std::string() : rest.substr(c + 1);
        }
        portfolio->only(arms);
        opt.portfolio = portfolio.get();
    }
    // The splits of a run at each checkpoint (the log: where a faster run gains).
    auto splitsLine = [&](const std::vector<InputFrame>& run) {
        tas::Course course(*level, content, category);
        tas::Sim sim(content, *level, course, category);
        const tas::RunStudy g = sim.study(run);
        std::string line = "Temps aux points de passage :";
        for (size_t s = 0; s < g.ends.size(); ++s) {
            char b[48];
            if (s + 1 < course.sectionEnds().size()) std::snprintf(b, sizeof b, " CP%zu %.3f", s + 1, static_cast<double>(g.splitAt(s)));
            else std::snprintf(b, sizeof b, " fin %.3f", static_cast<double>(g.splitAt(s)));
            line += b;
        }
        return line;
    };
    // What each arm of the portfolio was given and found (the progress file, the curve's panel).
    auto strategiesNow = [&] {
        std::vector<TasStrategy> out;
        if (!portfolio) return out;
        const auto gains = portfolio->gains();
        for (const auto& a : portfolio->bandit().arms()) {
            if (a.cpu <= 0.0 && std::none_of(gains.begin(), gains.end(), [&](const tas::FoundGain& g) { return g.arm == a.id; })) continue;
            if (out.size() >= TasStrategy::kMax) break;
            TasStrategy t;
            t.id = a.id;
            t.timeMs = static_cast<int64_t>(a.cpu * 1000.0);
            for (const tas::FoundGain& g : gains)
                if (g.arm == a.id) ++t.wins, t.gainMs += std::max<int64_t>(0, clockMs(g.from) - clockMs(g.to));
            out.push_back(std::move(t));
        }
        return out;
    };
    if (std::string seed = value("--seed-replay", ""); !seed.empty()) {
        std::string why;
        if (!tas::readSeedReplay(seed, levelId, opt.seedRun, why)) return finish(TasPhase::Failed, "Course de départ refusée : " + why, 2);
    }
    opt.keepSeedSections = std::max(0, std::atoi(value("--keep-seed-sections", "0").c_str()));
    // --guide <replay> [--guide-name <player>], several: players' runs that guide the search, each an
    // exact replay of this very version of the level and of the simulation (refused otherwise).
    struct GuideArg {
        std::string file, player;
    };
    std::vector<GuideArg> guideArgs;
    for (size_t i = 2; i + 1 < args.size(); ++i) {
        if (args[i] == "--guide") guideArgs.push_back({args[i + 1], ""});
        else if (args[i] == "--guide-name" && !guideArgs.empty()) guideArgs.back().player = args[i + 1];
    }
    if (guideArgs.size() > TasGuide::kMax) return finish(TasPhase::Failed, "Trop de guides (" + std::to_string(TasGuide::kMax) + " au plus)", 2);
    std::vector<std::vector<InputFrame>> guideRuns;
    std::vector<std::string> guideIds;
    std::vector<GuideArg> exactArgs;
    std::vector<std::vector<int64_t>> exactSplits;  // their checkpoint times as recorded (when they no longer replay to the end)
    // A run of an older version of the level (its inputs no longer play the same): its ROUTE only,
    // never its inputs (docs/TAS.md « Le tracé d'une ancienne version »).
    struct RouteArg {
        std::string player, id;
        std::vector<int64_t> splits;
    };
    std::vector<RouteArg> routeArgs;
    for (const GuideArg& g : guideArgs) {
        std::vector<InputFrame> in;
        std::string id, why;
        if (!tas::readGuideReplay(g.file, levelId, fp->hash, in, id, why)) {
            std::vector<tas::RouteHint> route;
            std::vector<int64_t> splits;
            std::string whyRoute;
            if (!tas::readRouteReplay(g.file, levelId, route, splits, whyRoute)) return finish(TasPhase::Failed, "Guide refusé (" + g.file + ") : " + why, 2);
            say("Guide " + (g.player.empty() ? g.file : g.player) + " : " + why + " ; son tracé seulement guide la recherche (" + std::to_string(route.size()) +
                " positions)");
            opt.routeHints.insert(opt.routeHints.end(), route.begin(), route.end());
            routeArgs.push_back({g.player.empty() ? std::string("?") : g.player, Paths::toUtf8(Paths::fromUtf8(g.file).stem()), std::move(splits)});
            continue;
        }
        // Its recorded route too: where its inputs stop playing the same (a run recorded before a
        // change of the simulation's details that the fingerprint does not see), it still says the way.
        {
            std::vector<tas::RouteHint> route;
            std::vector<int64_t> splits;
            std::string whyRoute;
            if (tas::readRouteReplay(g.file, levelId, route, splits, whyRoute)) opt.routeHints.insert(opt.routeHints.end(), route.begin(), route.end());
            exactSplits.push_back(std::move(splits));
        }
        guideRuns.push_back(std::move(in));
        guideIds.push_back(id.empty() ? Paths::toUtf8(Paths::fromUtf8(g.file).stem()) : id);
        exactArgs.push_back(g);
    }
    // --save-partial <file>: the run so far after each section, as a replay of its inputs (to start
    // a later search from it with --seed-replay; never a run of the machine by itself).
    if (std::string partial = value("--save-partial", ""); !partial.empty()) {
        const std::string hash = fp->hash;
        opt.onSection = [partial, hash, levelId](const std::vector<InputFrame>& run) {
            ReplayData d;
            d.levelId = levelId;
            d.levelHash = hash;
            d.attemptId = makeUuidV4();
            d.seed = levelSeed(levelId);
            d.keysPerSlot = true;
            d.inputs = run;
            d.frames.push_back(ReplayFrame{});
            FileUtil::writeBinary(Paths::fromUtf8(partial), d.encode());
        };
    }
    // A training: the run so far after each section of a round (a level whose first run takes
    // longer than a round goes on from it in the next round, instead of starting again).
    std::vector<InputFrame> partialRun;
    int partialSections = 0;
    {
        auto save = opt.onSection;
        opt.onSection = [save, &partialRun, &partialSections](const std::vector<InputFrame>& run) {
            partialRun = run;
            ++partialSections;
            if (save) save(run);
        };
    }
    const fs::path root = value("--out", "").empty() ? tasRoot() : Paths::fromUtf8(value("--out", ""));
    {
        char line[240];
        if (untilStopped)
            std::snprintf(line, sizeof line, "%s : %s, %s, jusqu'à l'arrêt (tours de %.1f min), %d fils", tas::toolVersion().c_str(), levelId.c_str(),
                          categoryLabel(category), roundSeconds / 60.0, opt.threads);
        else
            std::snprintf(line, sizeof line, "%s : %s, %s, budget %.1f min, %d fils", tas::toolVersion().c_str(), levelId.c_str(), categoryLabel(category),
                          opt.budgetSeconds / 60.0, opt.threads);
        say(line);
    }

    // --study <replay> [<from s> <to s>]: where Nova is in a run every 0.25 s of the clock (and its
    // energy, health), its section ends (measurements: where a run waits), then nothing else.
    for (size_t i = 2; i + 1 < args.size(); ++i)
        if (args[i] == "--study") {
            std::vector<InputFrame> r;
            std::string why;
            if (!tas::readSeedReplay(args[i + 1], levelId, r, why)) return finish(TasPhase::Failed, "Course illisible : " + why, 2);
            const double from = i + 2 < args.size() ? std::atof(args[i + 2].c_str()) : 0.0;
            const double to = i + 3 < args.size() ? std::atof(args[i + 3].c_str()) : 1e9;
            tas::Course course(*level, content, category);
            tas::Sim sim(content, *level, course, category);
            const tas::RunStudy g = sim.study(r);
            for (size_t s = 0; s < g.ends.size(); ++s) std::printf("fin de la section %zu : image %zu, %.3f s\n", s + 1, g.ends[s], static_cast<double>(g.splitAt(s)));
            sim.arena().restore(sim.origin());
            double next = from;
            for (size_t k = 0; k < r.size() && k <= g.exitAt; ++k) {
                const double c = sim.world().timer().elapsed();
                if (c >= next && c <= to) {
                    const Player& p = sim.world().player();
                    std::string powers;
                    for (const auto& pw : p.powers()) powers += (powers.empty() ? "" : ",") + pw->id();
                    std::printf("%8.3f s  image %6zu  pieds (%6.0f, %5.0f)  vitesse (%5.0f, %5.0f)  énergie %5.1f  santé %d  pouvoirs %s\n", c, k,
                                static_cast<double>(p.feet().x), static_cast<double>(p.feet().y), static_cast<double>(p.vel.x), static_cast<double>(p.vel.y),
                                static_cast<double>(p.energy), p.health, powers.c_str());
                    next = c + 0.25;
                }
                sim.arena().step(r[k]);
            }
            return finish(TasPhase::Stopped, "", 0);
        }
    // --probe <waypoint> <y>: the distances to a waypoint along a line of the level (a check of the
    // route the search follows), then nothing else.
    for (size_t i = 2; i + 2 < args.size(); ++i)
        if (args[i] == "--probe") {
            tas::Course course(*level, content, category);
            const int wp = std::atoi(args[i + 1].c_str());
            const float y = static_cast<float>(std::atof(args[i + 2].c_str()));
            const auto& w = course.waypoints()[static_cast<size_t>(std::clamp(wp, 0, static_cast<int>(course.waypoints().size()) - 1))];
            std::printf("repère %d (%s) en (%.0f, %.0f), %.0f px depuis le précédent, cumul %.0f\n", wp, w.objectId.c_str(), static_cast<double>(w.feet.x),
                        static_cast<double>(w.feet.y), static_cast<double>(w.fromPrev), static_cast<double>(w.cumulative));
            for (float x = level->bounds.x; x < level->bounds.right(); x += 64.0f)
                std::printf("  x %5.0f : %.0f\n", static_cast<double>(x), static_cast<double>(course.distanceTo(wp, {x, y})));
            return finish(TasPhase::Stopped, "", 0);
        }

    // The level's sections (the windows of a training's rounds) and checkpoints (the attempts that
    // reach each one: the clouds of the training curve), CP1 first, named as the level names them.
    tas::WindowPlanner planner;
    std::vector<std::string> cpIds;  // the level's checkpoints (CP1 first), for the splits
    {
        tas::Course course(*level, content, category);
        for (const tas::Waypoint& w : course.waypoints())
            if (w.kind == tas::Waypoint::Kind::Checkpoint && cpIds.size() < static_cast<size_t>(TasAttemptWindow::kMaxSections)) cpIds.push_back(w.objectId);
    }
    if (untilStopped || !training.progressFile.empty()) {
        tas::Course course(*level, content, category);
        planner = tas::WindowPlanner(static_cast<int>(course.sectionEnds().size()));
        std::vector<TasCheckpoint> cps;
        for (const tas::Waypoint& w : course.waypoints()) {
            if (w.kind != tas::Waypoint::Kind::Checkpoint || cps.size() >= static_cast<size_t>(TasAttemptWindow::kMaxSections)) continue;
            TasCheckpoint c;
            c.id = w.objectId;
            for (const LevelObject& o : level->objects)
                if (o.id == w.objectId) c.label = o.propStr("label");
            cps.push_back(std::move(c));
        }
        {
            std::lock_guard<std::mutex> s(training.statsMutex);
            training.stats.setCheckpoints(static_cast<int>(cps.size()));
        }
        training.edit([&](TasProgress& p) { p.checkpoints = std::move(cps); });
    }

    // The guides measured: their time when they finish the category (verified as a run would be)
    // and their splits — the references of the curve's checkpoints — and said on the run written.
    std::vector<TasGuide> guidedBy;
    {
        std::vector<TasProgress::Reference> refs;
        for (size_t k = 0; k < guideRuns.size(); ++k) {
            const tas::Verified gv = tas::recordAndVerify(content, *level, *fp, category, guideRuns[k], 0);
            TasProgress::Reference r;
            r.player = exactArgs[k].player.empty() ? std::string("?") : exactArgs[k].player;
            if (gv.ok) {
                r.timeMs = gv.record.timeMs;
                r.splits = splitsAt(gv.record.splits, cpIds);
                say("Guide " + std::to_string(k + 1) + " (" + r.player + ") : " + formatRunTime(r.timeMs));
            } else {
                // It no longer plays to the end today: its times as recorded, said as another version's.
                say("Guide " + std::to_string(k + 1) + " (" + r.player + ") : ne termine pas la catégorie aujourd'hui (" + gv.why +
                    "), ses entrées utilisées jusqu'où elles vont, son tracé au-delà");
                r.oldVersion = true;
                r.splits.assign(cpIds.size(), 0);
                for (size_t c = 0; c < exactSplits[k].size() && c < cpIds.size(); ++c) r.splits[c] = exactSplits[k][c];
                for (size_t c = 1; c < r.splits.size(); ++c)
                    if (r.splits[c] > 0 && r.splits[c] < r.splits[c - 1]) r.splits[c] = 0;
            }
            guidedBy.push_back({r.player, r.timeMs, guideIds[k]});
            refs.push_back(std::move(r));
        }
        // The routes of older versions: their checkpoint times as they were (said: another version).
        for (RouteArg& a : routeArgs) {
            TasProgress::Reference r;
            r.player = a.player;
            r.oldVersion = true;
            r.splits.assign(cpIds.size(), 0);
            for (size_t k = 0; k < a.splits.size() && k < cpIds.size(); ++k) r.splits[k] = a.splits[k];
            for (size_t k = 1; k < r.splits.size(); ++k)
                if (r.splits[k] > 0 && r.splits[k] < r.splits[k - 1]) r.splits[k] = 0;
            TasGuide g{a.player, 0, a.id};
            g.routeOnly = true;
            guidedBy.push_back(g);
            refs.push_back(std::move(r));
        }
        training.edit([&](TasProgress& p) { p.guides = std::move(refs); });
    }
    opt.guides = guideRuns;

    // The run to beat: the seed run, verified as a run of the machine (its exact time), when it is one.
    std::vector<InputFrame> best;
    int64_t bestMs = 0;
    std::mutex keepMutex;  // best, bestMs, improvements: the verifier's thread writes them
    if (!opt.seedRun.empty()) {
        training.edit([](TasProgress& p) { p.phase = TasPhase::Verifying; });
        const tas::Verified sv = tas::recordAndVerify(content, *level, *fp, category, opt.seedRun, 0);
        std::vector<int64_t> splits;
        if (sv.ok) {
            best = opt.seedRun;
            bestMs = sv.record.timeMs;
            splits = splitsAt(sv.record.splits, cpIds);
            say("Course de départ vérifiée : " + formatRunTime(bestMs));
            if (portfolio) say(splitsLine(best));
        } else {
            say("Course de départ non vérifiée (" + sv.why + ") : la recherche part d'elle sans la garder");
        }
        training.edit([&](TasProgress& p) {
            p.seedMs = bestMs;
            p.bestMs = bestMs;
            p.bestSplits = splits;
            p.bestElapsedMs = training.elapsed();
            p.bestExplored = training.explored.load();
        });
    }

    // Round after round (a single one without --until-stopped), each from the best run so far.
    const fs::path file = tasFilePath(levelId, category, root);
    const int64_t seedMs = bestMs;
    int round = 0, improvements = 0;
    std::string lastWhy;
    std::atomic<int64_t> bestAtomic{bestMs};  // read by the search's threads (onRun)
    bool writeFailed = false;
    // A run verified (by the normal replay playback) and, when faster than the best, written —
    // never before its verification. On the verifier's thread (or after it has drained).
    auto keep = [&](const Verifier::Job& job) {
        training.edit([](TasProgress& p) { p.verifying = true; });
        const tas::Verified v = tas::recordAndVerify(content, *level, *fp, category, job.run, training.explored.load());
        std::lock_guard<std::mutex> l(keepMutex);
        auto done = [&] {
            // The candidate dealt with: shown no more unless a faster one waits.
            int64_t c = job.clockMs;
            training.candidateMs.compare_exchange_strong(c, 0);
            training.edit([](TasProgress& p) { p.verifying = false; });
        };
        if (!v.ok) {
            lastWhy = v.why;
            say("Course refusée à la vérification : " + v.why + ". Rien n'est écrit.");
            return done();
        }
        if (bestMs > 0 && v.record.timeMs >= bestMs) return done();
        char line[300];
        std::snprintf(line, sizeof line, "Vérifiée par la lecture normale des replays : %s (%lld ms), morts %d, dégâts %d, collectibles %d/%d, %lld essais simulés.",
                      formatRunTime(v.record.timeMs).c_str(), static_cast<long long>(v.record.timeMs), v.record.deaths, v.record.damageTaken,
                      v.record.collectibles, v.record.totalCollectibles, static_cast<long long>(v.record.explored));
        say(line);
        best = job.run;
        bestMs = v.record.timeMs;
        bestAtomic = bestMs;
        if (seedMs > 0) ++improvements;  // faster than the run it started from
        // A faster run of the same level and simulation already in the file is kept.
        if (auto old = readTasFile(file); old && old->levelHash == fp->hash && old->simVersion == kSimulationVersion && old->timeMs <= v.record.timeMs) {
            say("Le fichier garde sa course, plus rapide ou égale (" + formatRunTime(old->timeMs) + ").");
        } else {
            TasRecord r = v.record;
            r.guidedBy = guidedBy;
            if (!FileUtil::writeBinary(tasReplayPath(r, root), v.bytes) || !writeTasFile(file, r)) {
                writeFailed = true;
                training.stop = true;
                say("Écriture impossible dans " + Paths::toUtf8(file.parent_path()));
                return done();
            }
            say("Écrit : " + Paths::toUtf8(file));
        }
        const std::vector<int64_t> splits = splitsAt(v.record.splits, cpIds);
        training.edit([&](TasProgress& p) {
            p.bestMs = bestMs;
            p.bestSplits = splits;
            p.bestSource = job.source;
            p.improvements = improvements;
            p.bestElapsedMs = training.elapsed();
            p.bestExplored = training.explored.load();
        });
        done();
    };
    Verifier verifier;
    verifier.check = keep;
    verifier.start();
    opt.onRun = [&](const std::vector<InputFrame>& run, float clock, const std::string& source) {
        const int64_t c = clockMs(clock), b = bestAtomic.load();
        if (b > 0 && c >= b) return;
        verifier.submit(run, c, source);
    };
    // A run's inputs as a replay of this level (--realign-out).
    auto writeInputs = [&](const std::string& path, const std::vector<InputFrame>& run) {
        ReplayData d;
        d.levelId = levelId;
        d.levelHash = fp->hash;
        d.attemptId = makeUuidV4();
        d.seed = levelSeed(levelId);
        d.keysPerSlot = true;
        d.inputs = run;
        d.frames.push_back(ReplayFrame{});
        return FileUtil::writeBinary(Paths::fromUtf8(path), d.encode());
    };
    // The phase realignment (docs/TAS.md « Le réalignement de phase »): runs whose arrival at a
    // section's end is EARLIER than another's are continued with the other runs' next sections,
    // after the waits that put the world back in phase; a faster run is verified and written like
    // any other. The sources: the best run, the guides of this version, every --phase-source
    // replay (read again at each round: another training's best file, a branch's...).
    tas::PhaseOptions po;
    po.maxWait = static_cast<float>(std::clamp(std::atof(value("--phase-wait", "8").c_str()), 0.1, 30.0));
    po.keep = std::clamp(std::atoi(value("--phase-keep", "3").c_str()), 1, 16);
    po.minWait = static_cast<float>(std::clamp(std::atof(value("--phase-min-wait", "0.5").c_str()), 0.0, 30.0));
    if (flag("--phase-trace")) po.trace = [&](const std::string& line) { say(line); };
    const auto elements = tas::periodicElements(*level);
    std::vector<std::string> phaseFiles;
    for (size_t i = 2; i + 1 < args.size(); ++i)
        if (args[i] == "--phase-source") phaseFiles.push_back(args[i + 1]);
    struct PhaseSrc {
        std::vector<InputFrame> run;
        std::string name;
    };
    // `runs` realigned: the leads given (none: every lead among them), until `budget` seconds.
    auto realignRuns = [&](const std::vector<PhaseSrc>& runs, std::vector<tas::PhaseLead> leads, double budget) {
        tas::Course course(*level, content, category);
        tas::Sim sim(content, *level, course, category);
        std::vector<tas::RunStudy> studies;
        for (const PhaseSrc& r : runs) studies.push_back(sim.study(r.run));
        std::vector<const tas::RunStudy*> sources;
        for (const tas::RunStudy& g : studies) sources.push_back(&g);
        if (leads.empty()) leads = tas::phaseLeads(sources, course.sectionEnds().size(), 0.1f);
        for (tas::PhaseLead& l : leads)
            if (l.lead <= 0.0f && l.through > 0)  // (a lead given: its advance on the sources that finish, for the log)
                for (const tas::RunStudy* g : sources)
                    if (g->finishes() && l.through <= g->ends.size() && l.through <= studies[l.source].ends.size())
                        l.lead = std::max(l.lead, g->splitAt(l.through - 1) - studies[l.source].splitAt(l.through - 1));
        if (leads.empty()) {
            say("Réalignement de phase : aucune arrivée en avance à garder");
            return tas::PhaseSplice{};
        }
        const auto t0 = std::chrono::steady_clock::now();
        const auto until = t0 + std::chrono::milliseconds(static_cast<int64_t>(budget * 1000.0));
        auto over = [&] { return training.stop.load() || std::chrono::steady_clock::now() >= until; };
        const tas::PhaseSplice ps = tas::phaseSplice(sim, sources, elements, leads, po, over, [&](const tas::PhaseLead& l, const tas::PhaseRun& r) {
            training.explored += r.tried;
            char line[300];
            std::snprintf(line, sizeof line, "Réalignement après la section %zu (%s, %.3f s au chrono, %.3f s d'avance) : %s, %lld essais", l.through,
                          runs[l.source].name.c_str(), static_cast<double>(studies[l.source].splitAt(l.through - 1)), static_cast<double>(l.lead),
                          r.found ? formatRunTime(clockMs(r.clock)).c_str() : "aucune course", static_cast<long long>(r.tried));
            say(line);
            for (size_t k = 0; k < r.sections.size(); ++k) {
                const tas::PhaseSection& p = r.sections[k];
                std::snprintf(line, sizeof line, "  section %zu : %s, attente %.3f s (%d endroit(s)), arrivée %.3f s (la source : %.3f s), période rencontrée %.2f s",
                              l.through + k + 1, runs[static_cast<size_t>(p.source)].name.c_str(), p.wait / 120.0, p.waits, static_cast<double>(p.arrival),
                              static_cast<double>(p.sourceArrival), static_cast<double>(p.periodMet));
                say(line);
            }
            if (r.found) opt.onRun(r.run, r.clock, "phase");
        });
        char line[200];
        std::snprintf(line, sizeof line, "Réalignement de phase : %d avance(s) réalignée(s) en %.0f s, la plus rapide %s", ps.leads,
                      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                      ps.run.found ? formatRunTime(clockMs(ps.run.clock)).c_str() : "aucune");
        say(line);
        verifier.drain();
        return ps;
    };
    // The sources of a round's phase splice: the best run, the exact guides, the --phase-source files now.
    auto phaseSources = [&](const std::vector<PhaseSrc>& more) {
        std::vector<PhaseSrc> runs;
        if (!best.empty()) runs.push_back({best, "la meilleure course"});
        for (size_t k = 0; k < guideRuns.size(); ++k) runs.push_back({guideRuns[k], "le guide " + std::to_string(k + 1)});
        for (const std::string& f : phaseFiles) {
            std::vector<InputFrame> r;
            std::string why;
            if (tas::readSeedReplay(f, levelId, r, why)) runs.push_back({std::move(r), Paths::toUtf8(Paths::fromUtf8(f).filename())});
        }
        for (const PhaseSrc& m : more) runs.push_back(m);
        return runs;
    };
    // --realign <replay>: that replay's first sections (through each of its sections in turn, or
    // --realign-through <n>) kept, the rest realigned on the sources; --realign-out <file>: the
    // fastest run found, faster than the best or not (a branch can start from it).
    if (std::string ra = value("--realign", ""); !ra.empty()) {
        std::vector<InputFrame> lead;
        std::string why;
        if (!tas::readSeedReplay(ra, levelId, lead, why)) return finish(TasPhase::Failed, "Course à réaligner refusée : " + why, 2);
        std::vector<PhaseSrc> runs = phaseSources({});
        runs.push_back({lead, "la course réalignée"});
        for (size_t i = 2; i + 1 < args.size(); ++i)
            if (args[i] == "--realign-source") {
                std::vector<InputFrame> r;
                if (!tas::readSeedReplay(args[i + 1], levelId, r, why)) return finish(TasPhase::Failed, "Source refusée (" + args[i + 1] + ") : " + why, 2);
                runs.push_back({std::move(r), Paths::toUtf8(Paths::fromUtf8(args[i + 1]).filename())});
            }
        size_t reached = 0;
        {
            tas::Course course(*level, content, category);
            tas::Sim sim(content, *level, course, category);
            tas::throughSections(sim, lead, reached);
        }
        const size_t sections = tas::Course(*level, content, category).sectionEnds().size();
        std::vector<tas::PhaseLead> leads;
        const size_t leadIndex = runs.size() - 1 - static_cast<size_t>(std::count(args.begin(), args.end(), std::string("--realign-source")));
        if (const int n = std::atoi(value("--realign-through", "0").c_str()); n > 0) leads.push_back({leadIndex, static_cast<size_t>(n), 0.0f});
        else
            for (size_t n = 1; n <= reached && n < sections; ++n) leads.push_back({leadIndex, n, 0.0f});
        const tas::PhaseSplice ps = realignRuns(runs, leads, 1.0e9);
        if (const std::string outFile = value("--realign-out", ""); !outFile.empty()) {
            if (ps.run.found) {
                writeInputs(outFile, ps.run.run);
            } else if (ps.furthest.partialSections > 0) {
                // Nothing finished: the way realigned the furthest (a branch can start from it).
                writeInputs(outFile, ps.furthest.partial);
                char line[200];
                std::snprintf(line, sizeof line, "Réalignement : aucune course entière ; la plus avancée, jusqu'à la fin de la section %zu (%.3f s au chrono), écrite dans %s",
                              ps.furthest.partialSections, static_cast<double>(ps.furthest.partialClock), outFile.c_str());
                say(line);
            }
        }
        if (!untilStopped) {
            verifier.stop();
            if (writeFailed) return finish(TasPhase::Failed, "Écriture impossible dans " + Paths::toUtf8(file.parent_path()), 1);
            return finish(TasPhase::Stopped, ps.run.found ? "Réalignement : " + formatRunTime(clockMs(ps.run.clock)) : std::string("Réalignement : aucune course"), 0);
        }
    }
    // --phase (or --phase-source): a phase splice at the start of every round whose sources changed,
    // at most --phase-budget minutes (3).
    const bool phaseRounds = flag("--phase") || !phaseFiles.empty();
    const double phaseBudget = std::clamp(std::atof(value("--phase-budget", "3").c_str()), 0.1, 600.0) * 60.0;
    std::string phaseKey, branchPhaseKey;  // the sources of the last phase splice (the branch's)
    // --branch <replay> [--branch-through <n>] (docs/TAS.md « La branche »): a training that COMMITS to
    // the replay's first n sections (2 by default: an early lead), kept exactly, and trains the
    // rest round after round, windows and all, from the branch's own best run — kept (verified)
    // in its own file (--branch-out, by default <--out>/branch) even when slower than the best
    // run; its later sections are given back to the best run's rounds (a guide, the phase splice),
    // and a branch run faster than the best run is the best run. --branch-share: the rounds it gets (0.5).
    tas::BranchPlan branch;  // (tools/tas/Branch.h) under branchMutex: the branch's verifier offers it its runs
    std::atomic<int64_t> branchAtomic{0};
    std::mutex branchMutex;
    fs::path branchRoot;
    if (std::string br = value("--branch", ""); !br.empty()) {
        if (!value("--branch-out", "").empty()) branchRoot = Paths::fromUtf8(value("--branch-out", ""));
        else if (!value("--out", "").empty()) branchRoot = root / "branch";
        else return finish(TasPhase::Failed, "La branche demande --out ou --branch-out (jamais data/tas)", 2);
        std::vector<InputFrame> r;
        std::string why;
        if (!tas::readSeedReplay(br, levelId, r, why)) return finish(TasPhase::Failed, "Branche refusée : " + why, 2);
        const size_t want = static_cast<size_t>(std::clamp(std::atoi(value("--branch-through", "2").c_str()), 1, 64));
        tas::Course course(*level, content, category);
        tas::Sim sim(content, *level, course, category);
        size_t through = 0;
        std::vector<InputFrame> prefix = tas::throughSections(sim, r, through, want);
        const size_t sections = course.sectionEnds().size();
        if (through < want || through >= sections)
            return finish(TasPhase::Failed, "Branche : la course doit finir la section " + std::to_string(want) + " et en laisser au moins une après", 2);
        const float prefixClock = sim.study(prefix).clockAt.back();
        branch = tas::BranchPlan(std::move(prefix), through, static_cast<int>(sections), std::atof(value("--branch-share", "0.5").c_str()));
        // Its best: the branch's file when its run starts with the prefix (a restart), or the run
        // given when it finishes; verified either way.
        std::vector<std::vector<InputFrame>> candidates;
        if (auto old = readTasFile(tasFilePath(levelId, category, branchRoot)); old && old->levelHash == fp->hash && old->simVersion == kSimulationVersion) {
            std::vector<InputFrame> b;
            if (tas::readSeedReplay(Paths::toUtf8(tasReplayPath(*old, branchRoot)), levelId, b, why)) candidates.push_back(std::move(b));
        }
        candidates.push_back(r);
        for (const auto& c : candidates) {
            if (!branch.owns(c)) continue;
            const tas::Verified v = tas::recordAndVerify(content, *level, *fp, category, c, 0);
            if (v.ok) branch.offer(c, v.record.timeMs);
        }
        branchAtomic = branch.bestMs();
        char line[240];
        std::snprintf(line, sizeof line, "Branche : les sections 1 à %zu de %s gardées (%.3f s au chrono), le reste entraîné ; sa meilleure course %s", branch.through(),
                      Paths::toUtf8(Paths::fromUtf8(br).filename()).c_str(), static_cast<double>(prefixClock),
                      branch.bestMs() > 0 ? formatRunTime(branch.bestMs()).c_str() : "aucune encore");
        say(line);
    }
    const bool branching = branch.active();
    const fs::path branchFile = branching ? tasFilePath(levelId, category, branchRoot) : fs::path();
    auto onBranch = [&](const std::vector<InputFrame>& run) { return branch.owns(run); };  // (the prefix never changes)
    // A branch run verified and, when faster than the branch's best, written in its own file (and
    // given to the best run when faster than it too).
    auto keepBranch = [&](const Verifier::Job& job) {
        const tas::Verified v = tas::recordAndVerify(content, *level, *fp, category, job.run, training.explored.load());
        if (!v.ok) {
            say("Branche : course refusée à la vérification : " + v.why + ". Rien n'est écrit.");
            return;
        }
        {
            std::lock_guard<std::mutex> l(branchMutex);
            if (!branch.offer(job.run, v.record.timeMs)) return;
            branchAtomic = branch.bestMs();
        }
        TasRecord r = v.record;
        r.guidedBy = guidedBy;
        if (!FileUtil::writeBinary(tasReplayPath(r, branchRoot), v.bytes) || !writeTasFile(branchFile, r)) say("Branche : écriture impossible dans " + Paths::toUtf8(branchRoot));
        else say("Branche : " + formatRunTime(v.record.timeMs) + " vérifiée, écrite : " + Paths::toUtf8(branchFile));
        if (bestAtomic.load() == 0 || v.record.timeMs < bestAtomic.load()) verifier.submit(job.run, v.record.timeMs, job.source);
    };
    Verifier branchVerifier;
    branchVerifier.check = keepBranch;
    if (branching) branchVerifier.start();
    const auto mainOnRun = opt.onRun;
    auto branchOnRun = [&](const std::vector<InputFrame>& run, float clock, const std::string& source) {
        if (!onBranch(run)) return mainOnRun(run, clock, source);  // (it left the prefix: the best run's line only)
        const int64_t c = clockMs(clock), b = branchAtomic.load();
        if (b > 0 && c >= b) return;
        branchVerifier.submit(run, c, source);
    };
    auto drainAll = [&] {
        if (branching) branchVerifier.drain();
        verifier.drain();
    };
    std::string splicedFrom;  // the best times the last splice of the guides started from
    // The window of the round (docs/TAS.md « Les tours »): the first round searches the whole
    // level; with a best run, the next ones a window of one or two sections (the rest follows the
    // best run), rotating over the level, sooner again where a round gained.
    int focusFirst = 0, focusLast = 0;
    int64_t roundFrom = 0;  // the best time when the round started (the branch's in its rounds)
    bool lastBranch = false;
    do {
        ++round;
        if (round > 1) {
            const int64_t now = lastBranch ? branch.bestMs() : bestMs;
            const int64_t gain = roundFrom > 0 && now > 0 ? std::max<int64_t>(0, roundFrom - now) : 0;
            if (lastBranch) branch.done(round - 1, focusFirst, focusLast, gain);
            else planner.done(round - 1, focusFirst, focusLast, gain);
            opt.seed = baseSeed + static_cast<uint64_t>(round - 1) * 7919u;
        }
        drainAll();  // the last round's runs verified: the round starts from the best one
        const bool branchRound = branch.roundIsBranch(round);
        lastBranch = branchRound;
        opt.onRun = branchRound ? std::function<void(const std::vector<InputFrame>&, float, const std::string&)>(branchOnRun) : mainOnRun;
        std::vector<PhaseSrc> branchSrc;
        if (branching && !branch.best().empty()) branchSrc.push_back({branch.best(), "la branche"});
        // (the sources of a realignment, to do it again only when they changed)
        auto keyOf = [](const std::vector<PhaseSrc>& runs) {
            std::string key;
            for (const PhaseSrc& r : runs) {
                uint64_t h = 1469598103934665603ull;  // FNV-1a over the keys held of every frame
                for (const InputFrame& f : r.run) h = (h ^ (f.held + 0x9E37u * f.choice + 0x7Fu * f.flags)) * 1099511628211ull;
                key += std::to_string(r.run.size()) + ":" + std::to_string(h) + ";";
            }
            return key;
        };
        if (branchRound && !training.stop.load()) {
            // The branch's prefix realigned on every source (its own best, the best run, the guides...),
            // when they changed.
            std::vector<PhaseSrc> runs = phaseSources(branchSrc);
            runs.push_back({branch.prefix(), "le début de la branche"});
            if (const std::string key = keyOf(runs); runs.size() >= 2 && key != branchPhaseKey) {
                branchPhaseKey = key;
                realignRuns(runs, {tas::PhaseLead{runs.size() - 1, branch.through(), 0.0f}}, phaseBudget);
                drainAll();
            }
        } else if (phaseRounds && !training.stop.load()) {
            // The phase splice, when its sources changed (the best run, a --phase-source file, the branch).
            const std::vector<PhaseSrc> runs = phaseSources(branchSrc);
            const std::string key = keyOf(runs);
            if (runs.size() >= 2 && key != phaseKey) {
                phaseKey = key;
                training.edit([&](TasProgress& p) {
                    p.phase = TasPhase::Searching;
                    p.round = round;
                });
                realignRuns(runs, {}, phaseBudget);
                drainAll();
            }
        }
        std::unique_lock<std::mutex> kl(keepMutex);
        std::unique_lock<std::mutex> bl(branchMutex);
        focusFirst = focusLast = 0;
        opt.startRun.clear();
        opt.guides = guideRuns;
        // The --phase-source runs are guides of the round too (read again: another training's best
        // run as it is now): their frames offered to every section, realigned in phase, re-joined.
        std::string phaseGuideKey;
        for (const std::string& f : phaseFiles) {
            std::vector<InputFrame> r;
            std::string why;
            if (!tas::readSeedReplay(f, levelId, r, why) || r == best || (branch.active() && r == branch.best())) continue;
            phaseGuideKey += std::to_string(r.size()) + ",";
            opt.guides.push_back(std::move(r));
        }
        if (branchRound) {
            // The branch: from its best (its prefix kept, the windows after it), or from its prefix.
            roundFrom = branch.bestMs();
            opt.splice = false;  // (an assemblage from the load would leave the prefix)
            if (!best.empty() && best != branch.best()) opt.guides.push_back(best);
            if (!branch.best().empty()) {
                opt.seedRun = branch.best();
                opt.keepSeedSections = 0;
                if (untilStopped) std::tie(focusFirst, focusLast) = branch.window(round);
            } else {
                opt.seedRun.clear();
                opt.startRun = branch.prefix();
            }
        } else {
            roundFrom = bestMs;
            if (!branch.best().empty() && branch.best() != best) opt.guides.push_back(branch.best());
            // The guides spliced again when the best run (or the branch's) changed (the splice is deterministic).
            const std::string key = std::to_string(bestMs) + "/" + std::to_string(branch.bestMs()) + "/" + phaseGuideKey;
            opt.splice = !opt.guides.empty() && splicedFrom != key;
            splicedFrom = key;
            if (!best.empty()) {
                opt.seedRun = best;
                opt.keepSeedSections = 0;
                // (with the portfolio, from the first round: its strategies need a best run and a window)
                if (untilStopped && (round > 1 || portfolio) && planner.sections() > 0) std::tie(focusFirst, focusLast) = planner.next(round);
            } else if (round > 1 && !partialRun.empty()) {
                // No run yet: from where the last round got to (its sections kept), with a longer round.
                opt.seedRun = partialRun;
                opt.keepSeedSections = partialSections;
                opt.budgetSeconds = std::min(opt.budgetSeconds * 2.0, 60.0 * 60.0);
                say("Pas encore de course : le tour " + std::to_string(round) + " repart de la section " + std::to_string(partialSections + 1));
            }
        }
        bl.unlock();
        kl.unlock();
        partialRun.clear();
        partialSections = 0;
        training.candidateMs = 0;
        training.closeWindow();  // the last round's attempts stay in their own window
        opt.windowFirst = focusFirst;
        opt.windowLast = focusLast;
        training.edit([&](TasProgress& p) {
            p.phase = TasPhase::Searching;
            p.round = round;
            p.focusFirst = focusFirst;
            p.focusLast = focusLast;
        });
        if (untilStopped) {
            const std::string who = branchRound ? "Tour " + std::to_string(round) + " (branche)" : "Tour " + std::to_string(round);
            if (focusFirst > 0)
                say(who + " : sections " + std::to_string(focusFirst) + " à " + std::to_string(focusLast) + ", le reste suit " +
                    (branchRound ? "la meilleure course de la branche" : "la meilleure course"));
            else if (branchRound) say(who + " : depuis la fin de la section " + std::to_string(branch.through()) + " de la branche");
            else say(who + " : tout le niveau");
        }
        const tas::SearchOutcome found = tas::search(content, *level, opt);
        if (phaseRounds && !found.early.empty() && !training.stop.load()) {
            // The round's arrivals earlier than the best run that did not re-join it: realigned on
            // every source (docs/TAS.md « Le réalignement de phase »), the largest leads first.
            drainAll();
            std::vector<PhaseSrc> runs = phaseSources(branchSrc);
            std::vector<tas::PhaseLead> leads;
            for (const auto& e : found.early) {
                runs.push_back({e.run, "une arrivée en avance (section " + std::to_string(e.sections) + ")"});
                leads.push_back({runs.size() - 1, e.sections, e.lead});
            }
            std::stable_sort(leads.begin(), leads.end(), [](const tas::PhaseLead& a, const tas::PhaseLead& b) { return a.lead > b.lead; });
            realignRuns(runs, leads, phaseBudget);
            drainAll();
        }
        if (portfolio) {
            training.edit([&](TasProgress& p) { p.strategies = strategiesNow(); });
            say("Portefeuille, gains trouvés : " + [&] {
                std::string s;
                for (const tas::FoundGain& g : portfolio->gains()) {
                    char b[96];
                    std::snprintf(b, sizeof b, "%s%s %.3f -> %.3f à %.0f s", s.empty() ? "" : " ; ", tas::strategyLabel(g.arm).c_str(), static_cast<double>(g.from),
                                  static_cast<double>(g.to), static_cast<double>(g.atMs) / 1000.0);
                    s += b;
                }
                return s.empty() ? std::string("aucun") : s;
            }());
        }
        if (!found.found) {
            lastWhy = found.why;
            say("Aucune course trouvée (" + found.why + ") après " + std::to_string(training.explored.load()) + " essais.");
            continue;
        }
        // The run kept at the round's end (verified like the others; usually already done).
        if (branchRound && onBranch(found.inputs)) {
            if (branchAtomic.load() == 0 || clockMs(found.clock) < branchAtomic.load()) branchVerifier.submit(found.inputs, clockMs(found.clock), found.bestSource);
        } else if (bestAtomic.load() == 0 || clockMs(found.clock) < bestAtomic.load()) {
            training.edit([](TasProgress& p) { p.phase = TasPhase::Verifying; });
            verifier.submit(found.inputs, clockMs(found.clock), found.bestSource);
        }
        drainAll();
        if (writeFailed) break;
    } while (untilStopped && !training.stop.load());
    if (branching) branchVerifier.stop();
    verifier.stop();
    if (writeFailed) return finish(TasPhase::Failed, "Écriture impossible dans " + Paths::toUtf8(file.parent_path()), 1);

    if (bestMs == 0)
        return finish(untilStopped ? TasPhase::Stopped : TasPhase::Failed, "Aucune course vérifiée" + (lastWhy.empty() ? std::string() : " (" + lastWhy + ")"),
                      untilStopped ? 0 : 1);
    return finish(TasPhase::Stopped, training.stop.load() ? "Entraînement arrêté : " + formatRunTime(bestMs) : std::string(), 0);
}
