// The TAS in the screens (« meilleur temps trouvé par la machine », ui/screens/TasCatalog.h, TasUi.h):
//  * the rule of the player — a TAS is shown only for the level version and the simulation it was
//    computed for: after an update it is HIDDEN until computed again, and a level without TAS file
//    shows nothing of it (no box, no row, no placeholder);
//  * every screen that shows it, drawn with the fixtures of tests/data/tas (copied, their version set
//    to the installed level's) at 1280 x 720 and 1920 x 1080, in French and in English (nothing left
//    in French): « Course contre des fantômes », the « Fiche du TAS », Statistiques › Niveaux, the
//    rankings (NIVEAU), the replay viewer, the race against its ghost; the pieces each one drew of the
//    TAS never overlap and stay inside their frame.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/PersonalBests.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/records/PlayStats.h"
#include "game/tas/TasFile.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/TasCatalog.h"
#include "ui/screens/TasUi.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

constexpr const char* kLevel = "aethra_01";

/// The catalog back on the game's own TAS whatever happens (a failed check throws).
struct RootGuard {
    ~RootGuard() { tas::catalog().setRoot(tasRoot()); }
};

struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig(int w, int h, Language l) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = w;
        cfg.height = h;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;
        g->settings.language = i18n::code(l);
        g->applySettings();
    }
    ~Rig() {
        i18n::stopAudit();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
};

/// A coherent run of the level for the replays of the fixtures: its feet along the ground, its
/// checkpoints at the splits, finished; the inputs of every step (an exact replay).
ReplayData run(const std::string& levelHash, const Json& tas) {
    ReplayData d;
    d.levelId = kLevel;
    d.levelHash = levelHash;
    d.attemptId = makeUuidV4();
    d.seed = 7;
    d.simVersion = static_cast<uint16_t>(kSimulationVersion);
    const uint32_t ms = jget<uint32_t>(tas, "timeMs", 60000);
    d.durationMs = ms;
    d.inputs.assign(static_cast<size_t>(ms) * 120 / 1000, InputFrame{});
    const size_t n = static_cast<size_t>(ms) * d.sampleHz / 1000 + 1;
    for (size_t i = 0; i < n; ++i) {
        ReplayFrame f;
        f.x = 200.0f + 7200.0f * static_cast<float>(i) / static_cast<float>(n - 1);
        f.y = 608.0f;
        f.flags = ReplayFrame::Grounded;
        d.frames.push_back(f);
    }
    for (const Json& s : jfield(tas, "splits", Json::array()))
        d.events.push_back({static_cast<uint32_t>(jget<int64_t>(s, "timeMs", 0) * d.sampleHz / 1000), ReplayEventType::Checkpoint, 0, 0});
    d.events.push_back({static_cast<uint32_t>(n - 1), ReplayEventType::Finish, 0, 0});
    return d;
}

/// The fixtures of tests/data/tas copied under `root`, for the level version `hash` and the simulation
/// `sim`, each with its replay.
void install(const fs::path& root, const std::string& hash, int sim) {
    std::error_code ec;
    fs::remove_all(root, ec);
    for (const char* cat : {"ANY_PERCENT", "NO_DEATH", "HUNDRED_PERCENT"}) {
        auto j = FileUtil::loadJson(Paths::fromUtf8(std::string("tests/data/tas/") + kLevel + "/" + cat + ".json"));
        CHECK(j.has_value());
        Json t = *j;
        t["levelHash"] = hash;
        t["simVersion"] = sim;
        const fs::path dir = root / kLevel;
        CHECK(FileUtil::writeTextAtomic(dir / (std::string(cat) + ".json"), t.dump(1)));
        CHECK(FileUtil::writeBinary(dir / jstr(t, "replay"), run(hash, t).encode()));
    }
    tas::catalog().setRoot(root);
}

/// Two rectangles sharing more than a hairline.
bool overlaps(const Rect& a, const Rect& b) {
    const float w = std::min(a.right(), b.right()) - std::max(a.x, b.x), h = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
    return w > 0.75f && h > 0.75f;
}

/// What a screen drew of the TAS: inside its frame, nothing over anything else.
bool layoutOk = true;  // every failure printed, the test fails once at its end
void checkPieces(const std::string& screen, const char* where) {
    const tasui::Pieces& P = tasui::pieces(screen);
    CHECK(!P.items.empty());
    const Rect frame = P.frame.expanded(1.0f);
    for (size_t i = 0; i < P.items.size(); ++i) {
        const auto& [name, r] = P.items[i];
        const bool inside = frame.containsRect(r);
        if (!inside)
            std::fprintf(stderr, "  %s %s: « %s » outside (%g,%g %gx%g) of (%g,%g %gx%g)\n", where, screen.c_str(), name.c_str(), r.x, r.y, r.w, r.h, P.frame.x,
                         P.frame.y, P.frame.w, P.frame.h);
        layoutOk = layoutOk && inside;
        for (size_t k = i + 1; k < P.items.size(); ++k) {
            const bool over = overlaps(r, P.items[k].second);
            if (over) std::fprintf(stderr, "  %s %s: « %s » over « %s »\n", where, screen.c_str(), name.c_str(), P.items[k].first.c_str());
            layoutOk = layoutOk && !over;
        }
    }
}

std::string rankedHash(GameServices& g) {
    const LevelFingerprint* fp = g.online.fingerprints().get(g.content, kLevel);
    CHECK(fp != nullptr);
    return fp->hash;
}

/// My runs of this version: a record, its replay kept as my best ghost, my best segments, my statistics.
void myRuns(GameServices& g, const std::string& hash) {
    const std::string key = GhostStore::levelKey(kLevel, hash);
    const std::vector<std::string> route = {"o104", "o148", "o213", "o263"};
    g.bests.setRoute(key, route);
    BestAttempt a;
    a.passage = BestPassage::ofSplits({17.1f, 29.9f, 44.2f, 56.6f}, route, 68.4f);
    a.date = "2026-11-22 10:00";
    g.bests.add(key, a);
    Json t;
    t["timeMs"] = 68400;
    t["splits"] = Json::array({Json{{"checkpoint", "o104"}, {"timeMs", 17100}}, Json{{"checkpoint", "o263"}, {"timeMs", 56600}}});
    GhostMeta m;
    m.source = "pb";
    m.levelId = kLevel;
    m.levelHash = hash;
    m.timeMs = 68400;
    m.date = "2026-11-22";
    CHECK(g.ghosts.recordRun(run(hash, t), m, true));
    PlayStatsData d;
    const int today = g.playStats.today();
    LevelRuns& r = d.runs[kLevel];
    r.name = "Le Bosquet Englouti";
    r.version = hash;
    r.attempts = 12;
    r.finished = 6;
    r.pbRoute = route;
    r.pb = 68.4f;
    const char* cps[] = {"", "o104", "o148", "o213", "o263", ""};
    for (int i = 0; i < 5; ++i) {
        SegmentRec& s = r.segmentAt(cps[i], cps[i + 1]);
        for (int k = 0; k < 8; ++k) s.times.push_back(12.0f + static_cast<float>(i) + 0.1f * static_cast<float>(k));
        s.best = 12.0f + static_cast<float>(i);
    }
    for (int f = 0; f < 6; ++f) r.finishes.push_back({today - 6 + f, 75.0f - static_cast<float>(f)});
    r.history = {{today - 9, 72.0f, 66.0f}, {today - 2, 68.4f, 62.0f}};
    r.lastDay = today;
    g.playStats.setData(d);
}

}  // namespace

TEST_CASE("tas screens: shown only for the level version and the simulation it was computed for, hidden otherwise") {
    RootGuard guard;
    Rig rig(1280, 720, Language::French);
    GameServices& g = *rig.g;
    const std::string hash = rankedHash(g);
    const fs::path root = Paths::userDir() / "tas_screens_rule";
    // No file at all: nothing (and nothing of it on the screens).
    std::error_code ec;
    fs::remove_all(root, ec);
    tas::catalog().setRoot(root);
    CHECK(!tas::catalog().any(g, kLevel));
    // The fixtures as they are written: another version of the level, another simulation: hidden.
    tas::catalog().setRoot(Paths::fromUtf8("tests/data/tas"));
    for (RunCategory c : kAllCategories) CHECK(tas::catalog().tasFor(g, kLevel, c) == nullptr);
    // This version, this simulation: shown, every category with a file (SANS DÉGÂT has none).
    install(root, hash, kSimulationVersion);
    const tas::TasRun* any = tas::catalog().tasFor(g, kLevel, RunCategory::AnyPercent);
    CHECK(any != nullptr);
    CHECK(any->timeMs == 60000 && any->splits.size() == 4 && any->levelVersion == 1);
    CHECK(any->segment("", "o104") == 15000 && any->segment("o148", "o213") == 13000 && any->segment("o263", "") == 10000);
    CHECK(any->segment("o213", "o148") < 0);  // not in this order
    CHECK(tas::catalog().tasFor(g, kLevel, RunCategory::NoDamage) == nullptr);
    const tas::TasRun* noDeath = tas::catalog().tasFor(g, kLevel, RunCategory::NoDeath);
    CHECK(noDeath && noDeath->sameAsAny);  // « = ANY% »
    const tas::TasRun* full = tas::catalog().tasFor(g, kLevel, RunCategory::HundredPercent);
    CHECK(full && !full->sameAsAny);
    const GhostEntry ghost = any->ghost();
    CHECK(ghost.meta.source == "tas" && ghost.meta.category == "ANY_PERCENT" && ghost.meta.splits.size() == 4 && ghost.bytes > 0);
    // Another version of the level (an update): hidden until computed again.
    CHECK(tas::catalog().tasFor(kLevel, RunCategory::AnyPercent, std::string(64, 'f')) == nullptr);
    CHECK(tas::catalog().tasFor(kLevel, RunCategory::AnyPercent, hash) != nullptr);
    // Another simulation: hidden.
    install(root, hash, kSimulationVersion - 1);
    CHECK(!tas::catalog().any(g, kLevel));
    // Its replay gone: hidden (nothing to watch nor race).
    install(root, hash, kSimulationVersion);
    fs::remove(root / kLevel / "ANY_PERCENT.cprz", ec);
    tas::catalog().clear();
    CHECK(tas::catalog().tasFor(g, kLevel, RunCategory::AnyPercent) == nullptr);
    CHECK(tas::catalog().tasFor(g, kLevel, RunCategory::HundredPercent) != nullptr);
    // A hidden TAS shows nothing: the ghosts' setup has no « TAS » row and no « TAS de ce niveau ».
    install(root, std::string(64, 'f'), kSimulationVersion);
    rig.app.pushScene(makeMainMenu(g));
    rig.app.runFrames(2);
    rig.app.pushScene(makeGhostSetup(g, kLevel));
    rig.app.runFrames(4);
    for (const auto& [name, r] : tasui::pieces("setup").items) CHECK(name.rfind("block/", 0) != 0 && name.rfind("row/", 0) != 0);
    CHECK(!g.ui.navigable("gsetup/tassheet"));
    CHECK(!tasui::openSheet(g, kLevel, RunCategory::AnyPercent));
    tas::catalog().setRoot(tasRoot());
    fs::remove_all(root, ec);
}

TEST_CASE("tas screens: every screen of the TAS drawn at 1280 x 720 and 1920 x 1080, in French and English, nothing overlapping") {
    layoutOk = true;
    for (int size = 0; size < 2; ++size)
        for (Language lang : {Language::French, Language::English}) {
            const char* where = size ? (lang == Language::French ? "1920x1080 fr" : "1920x1080 en") : (lang == Language::French ? "1280x720 fr" : "1280x720 en");
            RootGuard guard;
            Rig rig(size ? 1920 : 1280, size ? 1080 : 720, lang);
            GameServices& g = *rig.g;
            const std::string hash = rankedHash(g);
            const fs::path root = Paths::userDir() / "tas_screens";
            install(root, hash, kSimulationVersion);
            myRuns(g, hash);
            const tas::TasRun* any = tas::catalog().tasFor(g, kLevel, RunCategory::AnyPercent);
            CHECK(any != nullptr);
            if (lang == Language::English) i18n::startAudit();
            rig.app.pushScene(makeMainMenu(g));
            rig.app.runFrames(2);
            // « Course contre des fantômes »: the TAS's row, « TAS de ce niveau » (then compact with the training).
            {
                GhostSetupRequest req;
                req.tasCategory = "ANY_PERCENT";
                rig.app.pushScene(makeGhostSetup(g, kLevel, req));
                rig.app.runFrames(6);
                checkPieces("setup", where);
                bool row = false, block = false;
                for (const auto& [name, r] : tasui::pieces("setup").items) {
                    row = row || name == "row/title";
                    block = block || name == "block/sheet";
                }
                CHECK(row && block);
                CHECK(g.ui.navigable("gsetup/taswatch") && g.ui.navigable("gsetup/tasrace") && g.ui.navigable("gsetup/tassheet"));
                rig.app.popScene();
                rig.app.runFrames(1);
            }
            // The « Fiche du TAS », each of its categories.
            for (RunCategory c : {RunCategory::AnyPercent, RunCategory::NoDeath, RunCategory::HundredPercent}) {
                CHECK(tasui::openSheet(g, kLevel, c));
                rig.app.runFrames(5);
                checkPieces("sheet", where);
                CHECK(g.ui.navigable("tassheet/watch") && g.ui.navigable("tassheet/race"));
                rig.app.popScene();
                rig.app.runFrames(1);
            }
            // Statistiques › Niveaux: the machine's strip, its column, its line on the chart.
            g.onlineOpen = "stats:runs=aethra_01,level=aethra_01";
            rig.app.pushScene(makeStatistics(g, 3));
            rig.app.runFrames(10);
            checkPieces("stats", where);
            rig.app.popScene();
            rig.app.runFrames(1);
            // The rankings, NIVEAU tab: its row above n°1 (it comes with the game: offline too).
            rig.app.pushScene(makeOnlineRanking(g, kLevel));
            rig.app.runFrames(6);
            bool rowShown = false;
            for (const auto& [name, r] : tasui::pieces("ranking").items) rowShown = rowShown || name == "tas/time";
            CHECK(rowShown);
            checkPieces("ranking", where);
            // The rankings and the account screen they ask for (no account here), at once: a frame
            // between them would ask for the account again.
            for (size_t k = rig.app.sceneCount(); k > 1; --k) rig.app.popScene();
            rig.app.runFrames(1);
            CHECK(rig.app.sceneCount() == 1);
            // The replay viewer on my record: « TAS » among the references, « Comparer avec le TAS ».
            {
                auto mine = GhostStore::loadReplay(g.ghosts.localGhosts(kLevel).front().file);
                CHECK(mine.has_value());
                g.onlineOpen = "rview:pause,at=20";
                rig.app.pushScene(makeLocalReplayViewer(g, std::move(*mine), "Mon meilleur temps"));
                for (int i = 0; i < 40 && !g.ui.navigable("gap/tas"); ++i) {
                    rig.app.runFrames(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                CHECK(g.ui.navigable("gap/tas"));
                CHECK(g.ui.navigable("replay/comparetas"));
                rig.app.popScene();
                rig.app.runFrames(1);
            }
            // The TAS's own replay in the viewer, then the race against its ghost.
            CHECK(tasui::watch(g, *any, 12.0));
            rig.app.runFrames(4);
            rig.app.popScene();
            rig.app.runFrames(1);
            {
                PlayOptions o;
                o.ghostsChosen = true;
                o.ghosts = {any->ghost()};
                rig.app.pushScene(makeGameScene(g, kLevel, o));
                for (int i = 0; i < 40; ++i) {
                    rig.app.runFrames(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                CHECK(rig.app.sceneCount() >= 1);
            }
            if (lang == Language::English) {
                std::vector<i18n::AuditFinding> wrong = i18n::auditFindings();
                for (size_t i = 0; i < wrong.size() && i < 30; ++i)
                    std::fprintf(stderr, "  [%s] %s -> %s\n", wrong[i].reason.c_str(), wrong[i].text.c_str(), wrong[i].shown.c_str());
                layoutOk = layoutOk && wrong.empty();
                i18n::stopAudit();
            }
            tas::catalog().setRoot(tasRoot());
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    CHECK(layoutOk);  // what failed is printed above (every size and language)
}
