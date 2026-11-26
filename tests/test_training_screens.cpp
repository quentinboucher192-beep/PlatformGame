// 2.91 — the training screens by the keyboard: ENTRAÎNEMENT › « Segments et enchaînements » (its
// grid of ranges, the chosen range, « Startpos et mesures »), a section training where F5 places a
// StartPos and F8 the measure points (through the inputs), and Pause › « Startpos et mesures ».

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "tests/TestFramework.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/PauseScene.h"
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
