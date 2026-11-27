// 2.91 — the training screens by the keyboard: ENTRAÎNEMENT › « Segments et enchaînements » (its
// grid of ranges, the chosen range, « Startpos et mesures »), a section training where F5 places a
// StartPos and F8 the measure points (through the inputs), and Pause › « Startpos et mesures ».
// 2.92: the strips draw the level itself (what is drawn over them inside), and « Startpos et
// mesures » adds, moves by the pixel and deletes the StartPos and the measure points.

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/gameplay/StartPos.h"
#include "game/ghost/MeasurePoints.h"
#include "game/ghost/StartPosStore.h"
#include "game/levels/LevelData.h"
#include "game/player/PlayerConfig.h"
#include "tests/TestFramework.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/PauseScene.h"
#include "ui/screens/TrainingStrip.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

void key(SDL_Scancode sc, SDL_Keycode k) {
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = sc;
    down.key.key = k;
    down.key.down = true;
    SDL_PushEvent(&down);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    SDL_PushEvent(&up);
}

/// A headless game at `w` x `h` in `lang`.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig(int w, int h, Language lang) {
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
        g->settings.language = i18n::code(lang);
        g->applySettings();
    }
    ~Rig() {
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
    void press(SDL_Scancode sc, SDL_Keycode k, int frames = 3) {
        key(sc, k);
        app.runFrames(frames);
    }
};

/// Frames until `done` (the level's pictures are made off the frame's path); false after a minute.
template <class F>
bool waitFor(Rig& rig, F done) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) return false;
        rig.app.runFrames(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

bool inside(const Rect& o, const Rect& a) { return o.x >= a.x - 0.5f && o.y >= a.y - 0.5f && o.right() <= a.right() + 0.5f && o.bottom() <= a.bottom() + 0.5f; }

/// No text left in French (English, the audit on since startAudit()).
void checkAudit() {
    std::vector<i18n::AuditFinding> wrong = i18n::auditFindings();
    for (size_t i = 0; i < wrong.size() && i < 30; ++i)
        std::fprintf(stderr, "  [%s] %s -> %s\n", wrong[i].reason.c_str(), wrong[i].text.c_str(), wrong[i].shown.c_str());
    CHECK(wrong.empty());
    i18n::stopAudit();
}

/// The request the ghost setup builds for a level (its checkpoints left to right).
ChainTrainingRequest requestFor(GameServices& g, const std::string& levelId, const std::string& hash) {
    ChainTrainingRequest r;
    auto level = g.content.loadLevel(levelId);
    CHECK(level.has_value());
    r.levelId = levelId;
    r.levelName = level->name;
    r.levelHash = hash;
    r.levelBounds = level->bounds;
    for (const LevelObject& o : level->objects)
        if (o.type == "checkpoint")
            r.checkpoints.push_back({o.id, o.propStr("label"), o.rect.bottomCenter(), (o.rect.bottomCenter().x - level->bounds.x) / level->bounds.w});
    std::sort(r.checkpoints.begin(), r.checkpoints.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    return r;
}

}  // namespace

TEST_CASE("training screens: the grid of ranges and the chosen range, by the keyboard, in French and in English") {
    for (Language lang : {Language::French, Language::English}) {
        Rig rig(1280, 720, lang);
        GameServices& g = *rig.g;
        ChainTrainingRequest req = requestFor(g, "sylvanox_01", std::string(64, 'e'));
        CHECK(req.checkpoints.size() >= 2);
        const std::string key = GhostStore::levelKey("sylvanox_01", req.levelHash);
        rig.app.pushScene(makeChainTraining(g, req));
        rig.app.runFrames(5);
        CHECK(rig.app.sceneCount() == 1);
        // The level's route is its Sum of Best's.
        CHECK(g.bests.route(key) && g.bests.route(key)->size() == req.checkpoints.size());
        CHECK(g.ui.focused("chains/c0_1"));  // the first segment
        rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
        CHECK(g.ui.focused("chains/c0_2"));  // départ → its second point: an enchaînement
        rig.press(SDL_SCANCODE_DOWN, SDLK_DOWN);
        CHECK(g.ui.focused("chains/c1_2"));  // the second segment
        // The other tab, then back.
        rig.press(SDL_SCANCODE_E, SDLK_E, 5);
        rig.press(SDL_SCANCODE_Q, SDLK_Q, 5);
        rig.press(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
        CHECK(rig.app.sceneCount() == 0 || rig.app.sceneCount() == 1);  // gone (the root screen may come back)
    }
}

TEST_CASE("training screens: F5 places a StartPos and F8 the measure points through the inputs; the pause lists them") {
    Rig rig(1280, 720, Language::French);
    GameServices& g = *rig.g;
    auto level = g.content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    std::string firstCp;
    float firstX = 1e9f;
    Vec2 start;
    for (const LevelObject& obj : level->objects) {
        if (obj.type == "checkpoint" && obj.rect.x < firstX) {
            firstX = obj.rect.x;
            firstCp = obj.id;
        }
        if (obj.type == "player_start") start = obj.rect.bottomCenter();
    }
    PlayOptions o;
    o.training = true;
    o.spawn = start;          // in play at once (no intro)
    o.trainingEnd = firstCp;  // from the level's start to its first checkpoint
    std::unique_ptr<Scene> scene = makeGameScene(g, "sylvanox_01", o);
    GameScene* game = static_cast<GameScene*>(scene.get());
    rig.app.pushScene(std::move(scene));
    rig.app.runFrames(90);  // the intro skipped or played, Nova in play
    const std::string key = game->levelKey();
    CHECK(!key.empty());
    for (int i = 0; i < 120 && game->world() && game->world()->state() != WorldState::Playing; ++i) rig.app.runFrames(1);
    rig.press(SDL_SCANCODE_F5, SDLK_F5, 6);
    CHECK(g.startPositions.list(key).size() == 1);
    CHECK(g.startPositions.activeId(key) == g.startPositions.list(key).front().id);
    rig.press(SDL_SCANCODE_F8, SDLK_F8, 6);
    rig.press(SDL_SCANCODE_F8, SDLK_F8, 6);
    const auto pairs = g.measures.list(key);
    CHECK(pairs.size() == 1 && pairs.front().complete());
    // Pause › « Startpos et mesures », then back to the pause and the run.
    rig.app.pushScene(std::make_unique<PauseScene>(g, *game));
    rig.app.runFrames(10);
    rig.app.pushScene(makeTrainingMarks(g, *game));
    rig.app.runFrames(10);
    CHECK(g.ui.focused("marks/resume"));
    CHECK(rig.app.sceneCount() == 3);
    rig.press(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    CHECK(rig.app.sceneCount() == 2);
}

TEST_CASE("training screens: both strips draw the level itself, what is drawn over them inside, 1280 x 720 and 1920 x 1080, French and English") {
    const std::pair<int, int> sizes[] = {{1280, 720}, {1920, 1080}};
    for (const auto& [w, h] : sizes)
        for (Language lang : {Language::French, Language::English}) {
            Rig rig(w, h, lang);
            GameServices& g = *rig.g;
            ChainTrainingRequest req = requestFor(g, "sylvanox_01", std::string(64, 'b'));
            CHECK(req.checkpoints.size() >= 2);
            const std::string key = GhostStore::levelKey("sylvanox_01", req.levelHash);
            // A StartPos and a pair of gates in this level version.
            g.startPositions.clear(key);
            g.measures.clear(key);
            StartPosState s;
            s.health = 3;
            s.feet = req.checkpoints.front().feet + Vec2{96.0f, 0.0f};
            CHECK(g.startPositions.add(key, s, -1.0f, "2026-11-27 10:00") > 0);
            const int pair = g.measures.place(key, req.checkpoints.front().feet + Vec2{240.0f, 0.0f}, "2026-11-27 10:00");
            CHECK(pair > 0);
            CHECK(g.measures.place(key, req.checkpoints.front().feet + Vec2{720.0f, 0.0f}, "2026-11-27 10:00") == pair);
            if (lang == Language::English) i18n::startAudit();
            rig.app.pushScene(makeChainTraining(g, req));
            rig.app.runFrames(1);
            // Until the level's picture is made: the schematic bar and what goes over it, never an
            // empty box.
            const TrainingStrip::Report first = TrainingStrip::last("segments");
            CHECK(!first.picture);
            CHECK(first.strip.w > 0.0f && first.strip.h > 0.0f && !first.overlays.empty());
            // Then the level itself, its segments, names and range over it, inside the strip.
            CHECK(waitFor(rig, [] { return TrainingStrip::last("segments").picture; }));
            rig.app.runFrames(1);
            const TrainingStrip::Report seg = TrainingStrip::last("segments");
            CHECK(seg.picture && seg.levelId == "sylvanox_01");
            CHECK(seg.strip.w > static_cast<float>(w) * 0.8f);
            CHECK(seg.area.x >= 0.0f && seg.area.right() <= static_cast<float>(w) && seg.area.bottom() <= static_cast<float>(h));
            CHECK(seg.overlays.size() >= req.checkpoints.size() + 3);  // the range, the separators, the names
            for (const Rect& o : seg.overlays) CHECK(inside(o, seg.area));
            // « Startpos et mesures »: the level again, the pennant and the gates at their places.
            rig.press(SDL_SCANCODE_E, SDLK_E, 3);
            CHECK(waitFor(rig, [] { return TrainingStrip::last("marks").picture; }));
            rig.app.runFrames(2);
            const TrainingStrip::Report marks = TrainingStrip::last("marks");
            CHECK(marks.picture && marks.levelId == "sylvanox_01");
            CHECK(marks.overlays.size() >= req.checkpoints.size() + 6);  // ticks, a pennant, two gates, their names
            for (const Rect& o : marks.overlays) CHECK(inside(o, marks.area));
            CHECK(marks.area.right() <= static_cast<float>(w));
            if (lang == Language::English) checkAudit();
            rig.app.popScene();
            rig.app.runFrames(1);
            g.startPositions.clear(key);
            g.measures.clear(key);
            g.startPositions.save();
            g.measures.save();
        }
}

TEST_CASE("training screens: StartPos and measure points added, moved by the pixel and deleted from « Startpos et mesures », kept on disk") {
    Rig rig(1280, 720, Language::English);
    GameServices& g = *rig.g;
    ChainTrainingRequest req = requestFor(g, "sylvanox_01", std::string(64, 'c'));
    req.tab = 1;
    const std::string key = GhostStore::levelKey("sylvanox_01", req.levelHash);
    g.startPositions.clear(key);
    g.measures.clear(key);
    auto level = g.content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    const PlayerConfig cfg = PlayerConfig::fromJson(g.content.playerConfig());
    i18n::startAudit();
    rig.app.pushScene(makeChainTraining(g, req));
    rig.app.runFrames(1);  // its own strip drawn (the report of the last one replaced)
    CHECK(waitFor(rig, [] { return TrainingStrip::last("marks").picture; }));
    rig.app.runFrames(2);
    // « Ajouter une startpos », then its place on the strip: the cursor with the arrows, Entrée.
    g.ui.setFocus("chains/spadd");
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(g.ui.focused("chains/strip"));
    for (int i = 0; i < 4; ++i) rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    CHECK(g.ui.focused("chains/strip"));  // the arrows move its cursor, not the focus
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    auto list = g.startPositions.list(key);
    CHECK(list.size() == 1);
    if (list.empty()) return;
    const StartPosEntry placed = list.front();
    // Where the cursor was (half the strip and 4 steps), standing, as its segment's training.
    CHECK(placed.menu);
    CHECK(std::abs((placed.state.feet.x - level->bounds.x) / level->bounds.w - 0.52f) < 0.02f);
    CHECK(startPosProblem(*level, cfg.size, placed.state.feet) == StartPosProblem::None);
    CHECK(placed.state.health == cfg.maxHealth && placed.state.energy == cfg.maxEnergy);
    if (req.checkpoints.front().feet.x < placed.state.feet.x - 400.0f) CHECK(!placed.state.reached.empty());  // past the first checkpoint: lit
    // The preview has the focus: an arrow moves it by one pixel, with Maj by eight.
    CHECK(g.ui.focused("chains/preview"));
    auto feet = [&] { return g.startPositions.find(key, placed.id)->state.feet; };
    const Vec2 f0 = feet();
    rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    CHECK(feet().x == f0.x + 1.0f && feet().y == f0.y);
    rig.press(SDL_SCANCODE_UP, SDLK_UP);
    CHECK(feet().x == f0.x + 1.0f && feet().y == f0.y - 1.0f);
    SDL_SetModState(SDL_KMOD_LSHIFT);
    rig.press(SDL_SCANCODE_LEFT, SDLK_LEFT);
    SDL_SetModState(SDL_KMOD_NONE);
    CHECK(feet().x == f0.x + 1.0f - 8.0f);
    CHECK(g.ui.focused("chains/preview"));  // the arrows never took the focus away
    // Pushed into the terrain: refused (shown in red), it stays at its last valid place.
    Vec2 ground;
    bool found = false;
    for (const LevelObject& o : level->objects) {
        if (o.type != "solid" || o.rect.w < 96.0f || o.rect.y < level->bounds.y + 96.0f) continue;
        const Vec2 f{std::round(o.rect.center().x), o.rect.y};
        if (startPosProblem(*level, cfg.size, f) == StartPosProblem::None && startPosProblem(*level, cfg.size, f + Vec2{0.0f, 1.0f}) == StartPosProblem::InSolid) {
            ground = f;
            found = true;
            break;
        }
    }
    CHECK(found);
    StartPosState st = g.startPositions.find(key, placed.id)->state;
    st.feet = ground;
    g.startPositions.update(key, placed.id, st, -1.0f);
    rig.press(SDL_SCANCODE_DOWN, SDLK_DOWN);
    CHECK(feet().x == ground.x && feet().y == ground.y);
    rig.press(SDL_SCANCODE_DOWN, SDLK_DOWN);
    CHECK(feet().y == ground.y);
    rig.press(SDL_SCANCODE_UP, SDLK_UP);
    rig.press(SDL_SCANCODE_UP, SDLK_UP);  // out of the ground again: on it
    CHECK(feet().y == ground.y);
    rig.press(SDL_SCANCODE_UP, SDLK_UP);
    CHECK(feet().y == ground.y - 1.0f);
    const Vec2 kept = feet();
    // « Ajouter A → B »: A, then B on the strip; the preview then moves B by the pixel.
    g.ui.setFocus("chains/madd");
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(g.ui.focused("chains/strip"));
    for (int i = 0; i < 6; ++i) rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    auto pairs = g.measures.list(key);
    CHECK(pairs.size() == 1 && !pairs.front().complete());
    CHECK(g.ui.focused("chains/strip"));  // B next
    for (int i = 0; i < 6; ++i) rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    pairs = g.measures.list(key);
    CHECK(pairs.size() == 1 && pairs.front().complete());
    if (pairs.empty() || !pairs.front().complete()) return;
    CHECK(pairs.front().b->at.x > pairs.front().a.at.x);
    CHECK(g.ui.focused("chains/preview"));
    const int pairId = pairs.front().id;
    const float bx = pairs.front().b->at.x;
    rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    CHECK(g.measures.find(key, pairId)->b->at.x == bx + 1.0f);
    // Échap leaves the preview first (the screen stays), then the screen: what was moved is saved.
    rig.press(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    CHECK(rig.app.sceneCount() == 1);
    CHECK(!g.ui.focused("chains/preview"));
    checkAudit();
    rig.app.popScene();
    rig.app.runFrames(2);
    {
        StartPosStore disk(Paths::savesDir() / "startpos.json");
        disk.load();
        const StartPosEntry* e = disk.find(key, placed.id);
        CHECK(e && e->menu && e->state.feet.x == kept.x && e->state.feet.y == kept.y);
        MeasureStore m(Paths::savesDir() / "measures.json");
        m.load();
        const MeasurePair* p = m.find(key, pairId);
        CHECK(p && p->complete() && p->b->at.x == bx + 1.0f);
    }
    // Deleted, each after a confirmation.
    rig.app.pushScene(makeChainTraining(g, req));
    rig.app.runFrames(3);
    g.ui.setFocus("chains/spdel");
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(g.startPositions.list(key).size() == 1);  // asked first
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(g.startPositions.list(key).empty());
    g.ui.setFocus("chains/mdel");
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(g.measures.list(key).size() == 1);
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    CHECK(g.measures.list(key).empty());
    {
        StartPosStore disk(Paths::savesDir() / "startpos.json");
        disk.load();
        CHECK(disk.list(key).empty());
        MeasureStore m(Paths::savesDir() / "measures.json");
        m.load();
        CHECK(m.list(key).empty());
    }
    rig.app.popScene();
    rig.app.runFrames(1);
}
