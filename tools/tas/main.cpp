// cosmic_tas — the search of the machine's runs (docs/TAS.md). Offline, on our machines only:
// never installed, never run on a player's computer.
//
//   cosmic_tas <levelId> <category> [--budget minutes] [--seed-replay file] [--threads n]
//              [--beam n] [--seed n] [--out folder] [--keep-seed-sections n] [--save-partial file]
//
// <category>: ANY_PERCENT, HUNDRED_PERCENT, NO_DEATH, NO_DAMAGE (or ANY%, 100%...). The run found
// is verified by the normal replay playback, then written next to the levels:
// data/tas/<levelId>/<CATEGORY>.json and .cprz (--out: another folder). When the file already
// holds a faster run of the same level and simulation, it is kept.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/tas/TasFile.h"
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
                 "Usage : cosmic_tas <niveau> <catégorie> [--budget minutes] [--seed-replay fichier] [--threads n] [--beam n] [--seed n] [--out dossier]\n"
                 "  catégorie : ANY_PERCENT, HUNDRED_PERCENT, NO_DEATH, NO_DAMAGE\n");
    return 2;
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
    const std::string levelId = args[0];
    RunCategory category;
    if (!parseCategory(args[1], category)) return usage();

    Paths::init(value("--user-dir", ""));
    Log::setMinLevel(LogLevel::Error);
    ContentDatabase content;
    content.reload();
    const LevelEntry* entry = content.level(levelId);
    if (!entry || entry->source != ContentSource::Builtin) {
        std::fprintf(stderr, "Niveau inconnu ou pas un niveau du jeu : %s\n", levelId.c_str());
        return 2;
    }
    std::string err;
    auto level = content.loadLevel(levelId, &err);
    if (!level) {
        std::fprintf(stderr, "Niveau illisible : %s\n", err.c_str());
        return 2;
    }
    LevelFingerprints fingerprints;
    const LevelFingerprint* fp = fingerprints.get(content, levelId);
    if (!fp) {
        std::fprintf(stderr, "Pas d'empreinte de classement pour %s\n", levelId.c_str());
        return 2;
    }

    tas::SearchOptions opt;
    opt.category = category;
    opt.budgetSeconds = std::max(0.1, std::atof(value("--budget", "30").c_str())) * 60.0;
    opt.threads = std::clamp(std::atoi(value("--threads", "2").c_str()), 1, 64);
    opt.beam = std::clamp(std::atoi(value("--beam", "24").c_str()), 4, 4096);
    opt.seed = static_cast<uint64_t>(std::strtoull(value("--seed", "1").c_str(), nullptr, 10));
    opt.log = [](const std::string& s) {
        std::printf("%s\n", s.c_str());
        std::fflush(stdout);
    };
    if (std::string seed = value("--seed-replay", ""); !seed.empty()) {
        std::string why;
        if (!tas::readSeedReplay(seed, levelId, opt.seedRun, why)) {
            std::fprintf(stderr, "Course de départ refusée : %s\n", why.c_str());
            return 2;
        }
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
    const fs::path root = value("--out", "").empty() ? tasRoot() : Paths::fromUtf8(value("--out", ""));
    std::printf("%s : %s, %s, budget %.1f min, %d fils\n", tas::toolVersion().c_str(), levelId.c_str(), categoryLabel(category), opt.budgetSeconds / 60.0,
                opt.threads);

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
            return 0;
        }
    const tas::SearchOutcome found = tas::search(content, *level, opt);
    if (!found.found) {
        std::printf("Aucune course trouvée (%s) après %lld essais.\n", found.why.c_str(), static_cast<long long>(found.explored));
        SDL_Quit();
        return 1;
    }
    const tas::Verified v = tas::recordAndVerify(content, *level, *fp, category, found.inputs, found.explored);
    if (!v.ok) {
        std::printf("Course refusée à la vérification : %s. Rien n'est écrit.\n", v.why.c_str());
        SDL_Quit();
        return 1;
    }
    std::printf("Vérifiée par la lecture normale des replays : %s (%lld ms), morts %d, dégâts %d, collectibles %d/%d, %lld essais simulés.\n",
                formatRunTime(v.record.timeMs).c_str(), static_cast<long long>(v.record.timeMs), v.record.deaths, v.record.damageTaken,
                v.record.collectibles, v.record.totalCollectibles, static_cast<long long>(v.record.explored));
    // A faster run of the same level and simulation already there is kept.
    const fs::path file = tasFilePath(levelId, category, root);
    if (auto old = readTasFile(file); old && old->levelHash == fp->hash && old->simVersion == kSimulationVersion && old->timeMs <= v.record.timeMs) {
        std::printf("Le fichier garde sa course, plus rapide ou égale (%s).\n", formatRunTime(old->timeMs).c_str());
        SDL_Quit();
        return 0;
    }
    TasRecord r = v.record;
    if (!FileUtil::writeBinary(tasReplayPath(r, root), v.bytes) || !writeTasFile(file, r)) {
        std::fprintf(stderr, "Écriture impossible dans %s\n", Paths::toUtf8(file.parent_path()).c_str());
        SDL_Quit();
        return 1;
    }
    std::printf("Écrit : %s\n", Paths::toUtf8(file).c_str());
    SDL_Quit();
    return 0;
}
