// cosmic_tas — the search of the machine's runs (docs/TAS.md). Offline: on our machines, or
// installed next to the game and launched by it for an administrator (ADMINISTRATION › TAS);
// never by itself on a player's computer, never on Cosmic Online.
//
//   cosmic_tas <levelId> <category> [--budget minutes] [--seed-replay file] [--threads n]
//              [--beam n] [--seed n] [--out folder] [--keep-seed-sections n] [--save-partial file]
//              [--until-stopped] [--round minutes] [--progress file] [--stop-file file]
//              [--stop-on-stdin] [--user-dir folder] [--server url]
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

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasProgress.h"
#include "game/tas/TasVerify.h"
#include "tools/tas/Course.h"
#include "tools/tas/Output.h"
#include "tools/tas/Search.h"

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
    if (std::string seed = value("--seed-replay", ""); !seed.empty()) {
        std::string why;
        if (!tas::readSeedReplay(seed, levelId, opt.seedRun, why)) return finish(TasPhase::Failed, "Course de départ refusée : " + why, 2);
    }
    opt.keepSeedSections = std::max(0, std::atoi(value("--keep-seed-sections", "0").c_str()));
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

    // The run to beat: the seed run, verified as a run of the machine (its exact time), when it is one.
    std::vector<InputFrame> best;
    int64_t bestMs = 0;
    if (!opt.seedRun.empty()) {
        training.edit([](TasProgress& p) { p.phase = TasPhase::Verifying; });
        const tas::Verified sv = tas::recordAndVerify(content, *level, *fp, category, opt.seedRun, 0);
        if (sv.ok) {
            best = opt.seedRun;
            bestMs = sv.record.timeMs;
            say("Course de départ vérifiée : " + formatRunTime(bestMs));
        } else {
            say("Course de départ non vérifiée (" + sv.why + ") : la recherche part d'elle sans la garder");
        }
        training.edit([&](TasProgress& p) {
            p.seedMs = bestMs;
            p.bestMs = bestMs;
            p.bestElapsedMs = training.elapsed();
            p.bestExplored = training.explored.load();
        });
    }

    // Round after round (a single one without --until-stopped), each from the best run so far.
    const fs::path file = tasFilePath(levelId, category, root);
    const int64_t seedMs = bestMs;
    int round = 0, improvements = 0;
    std::string lastWhy;
    // The window of the round (docs/TAS.md « Les tours »): the first round searches the whole
    // level; with a best run, the next ones a window of one or two sections (the rest follows the
    // best run), rotating over the level, sooner again where a round gained.
    int focusFirst = 0, focusLast = 0;
    int64_t roundFrom = 0;  // the best time when the round started
    do {
        ++round;
        if (round > 1) {
            planner.done(round - 1, focusFirst, focusLast, roundFrom > 0 && bestMs > 0 ? std::max<int64_t>(0, roundFrom - bestMs) : 0);
            opt.seed = baseSeed + static_cast<uint64_t>(round - 1) * 7919u;
        }
        roundFrom = bestMs;
        focusFirst = focusLast = 0;
        if (!best.empty()) {
            opt.seedRun = best;
            opt.keepSeedSections = 0;
            if (untilStopped && round > 1 && planner.sections() > 0) std::tie(focusFirst, focusLast) = planner.next(round);
        } else if (round > 1 && !partialRun.empty()) {
            // No run yet: from where the last round got to (its sections kept), with a longer round.
            opt.seedRun = partialRun;
            opt.keepSeedSections = partialSections;
            opt.budgetSeconds = std::min(opt.budgetSeconds * 2.0, 60.0 * 60.0);
            say("Pas encore de course : le tour " + std::to_string(round) + " repart de la section " + std::to_string(partialSections + 1));
        }
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
            if (focusFirst > 0)
                say("Tour " + std::to_string(round) + " : sections " + std::to_string(focusFirst) + " à " + std::to_string(focusLast) +
                    ", le reste suit la meilleure course");
            else say("Tour " + std::to_string(round) + " : tout le niveau");
        }
        const tas::SearchOutcome found = tas::search(content, *level, opt);
        if (!found.found) {
            lastWhy = found.why;
            say("Aucune course trouvée (" + found.why + ") après " + std::to_string(training.explored.load()) + " essais.");
            continue;
        }
        if (bestMs > 0 && clockMs(found.clock) >= bestMs) continue;  // nothing faster this round
        training.edit([](TasProgress& p) { p.phase = TasPhase::Verifying; });
        const tas::Verified v = tas::recordAndVerify(content, *level, *fp, category, found.inputs, training.explored.load());
        if (!v.ok) {
            lastWhy = v.why;
            say("Course refusée à la vérification : " + v.why + ". Rien n'est écrit.");
            continue;
        }
        if (bestMs > 0 && v.record.timeMs >= bestMs) continue;
        char line[300];
        std::snprintf(line, sizeof line, "Vérifiée par la lecture normale des replays : %s (%lld ms), morts %d, dégâts %d, collectibles %d/%d, %lld essais simulés.",
                      formatRunTime(v.record.timeMs).c_str(), static_cast<long long>(v.record.timeMs), v.record.deaths, v.record.damageTaken,
                      v.record.collectibles, v.record.totalCollectibles, static_cast<long long>(v.record.explored));
        say(line);
        best = found.inputs;
        bestMs = v.record.timeMs;
        if (seedMs > 0) ++improvements;  // faster than the run it started from
        // A faster run of the same level and simulation already in the file is kept.
        if (auto old = readTasFile(file); old && old->levelHash == fp->hash && old->simVersion == kSimulationVersion && old->timeMs <= v.record.timeMs) {
            say("Le fichier garde sa course, plus rapide ou égale (" + formatRunTime(old->timeMs) + ").");
        } else {
            TasRecord r = v.record;
            if (!FileUtil::writeBinary(tasReplayPath(r, root), v.bytes) || !writeTasFile(file, r))
                return finish(TasPhase::Failed, "Écriture impossible dans " + Paths::toUtf8(file.parent_path()), 1);
            say("Écrit : " + Paths::toUtf8(file));
        }
        training.edit([&](TasProgress& p) {
            p.bestMs = bestMs;
            p.improvements = improvements;
            p.bestElapsedMs = training.elapsed();
            p.bestExplored = training.explored.load();
        });
    } while (untilStopped && !training.stop.load());

    if (bestMs == 0)
        return finish(untilStopped ? TasPhase::Stopped : TasPhase::Failed, "Aucune course vérifiée" + (lastWhy.empty() ? std::string() : " (" + lastWhy + ")"),
                      untilStopped ? 0 : 1);
    return finish(TasPhase::Stopped, training.stop.load() ? "Entraînement arrêté : " + formatRunTime(bestMs) : std::string(), 0);
}
