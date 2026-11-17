// 2.63: the game's trailer (data/trailer/trailer.json, tools/assetgen/environment/trailer.py), played
// by the stories' scene, and the main menu's attract mode: after a minute without any input the
// trailer plays, starts again at its end for as long as nobody touches anything, and any input
// goes back to the menu without acting on it.

#include <SDL3/SDL.h>

#include <cmath>
#include <memory>
#include <set>
#include <string>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/Navigation.h"

using namespace cosmic;

TEST_CASE("trailer: about forty seconds of quick cuts, real files and clips, titles in French and English") {
    std::string err;
    auto t = story::loadTrailer(&err);
    CHECK(t.has_value());
    if (!t) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(t->isTrailer());
    CHECK(t->version == 2);
    CHECK(!t->music.empty() && FileUtil::exists(Paths::assetsDir() / t->music));  // the game's own music
    const float total = story::duration(*t);
    CHECK(total >= 35.0f && total <= 45.0f);
    CHECK(t->shots.size() >= 12);
    std::set<std::string> sheets;
    for (const StoryShot& shot : t->shots) {
        CHECK(shot.duration > 0.0f && shot.duration <= 4.0f);  // quick cuts
        CHECK(story::shotLength(shot) == shot.duration);
        CHECK(shot.stage.has_value());
        if (!shot.stage) continue;
        const StoryStage& st = *shot.stage;
        CHECK(st.lastTime() <= shot.duration + 1e-3f);
        const auto [c0, z0] = st.cameraAt(0.0f);
        const auto [c1, z1] = st.cameraAt(shot.duration);
        CHECK(c0.x != c1.x || c0.y != c1.y || z0 != z1);  // the camera moves in every shot
        for (const StoryElement& e : st.elements) {
            if (e.kind == StoryElementKind::Layer) {
                CHECK(FileUtil::exists(Paths::assetsDir() / e.source));
            } else if (e.kind == StoryElementKind::Sprite) {
                sheets.insert(e.source);
                auto j = FileUtil::loadJson(Paths::assetsDir() / e.source);
                CHECK(j.has_value());
                if (!j) continue;
                auto set = AnimationSet::fromJson(*j, e.source);
                for (const StoryKey& k : e.keys)
                    if (!k.clip.empty()) {
                        CHECK(set->clip(k.clip) != nullptr);
                        if (!set->clip(k.clip)) std::printf("  %s: no clip %s\n", e.source.c_str(), k.clip.c_str());
                    }
            }
        }
        // Its title and subtitle are translated (the English of data/i18n/en.json).
        i18n::setLanguage(Language::English);
        CHECK(i18n::tr(shot.text) != shot.text || shot.text == "PYRRHAX" || shot.text == "SINGULARIS");
        // (names stay names: the planets, Cosmic Online)
        if (!shot.subtitle.empty() && shot.subtitle != "Cosmic Online" && shot.subtitle.find("Sylvanox") == std::string::npos)
            CHECK(i18n::tr(shot.subtitle) != shot.subtitle);
        i18n::setLanguage(Language::French);
    }
    // Nothing made up: Nova, the seven giga bosses on the scenery of their stories, real enemies.
    CHECK(sheets.count("animations/nova.json") == 1);
    // Its sounds (2.63): the game's own, each within its shot; every giga boss roars on its cut.
    int roars = 0;
    for (const StoryShot& shot : t->shots)
        if (shot.stage)
            for (const StorySfx& fx : shot.stage->sounds) {
                CHECK(FileUtil::exists(Paths::assetsDir() / fx.sound));
                CHECK(fx.t >= 0.0f && fx.t <= shot.duration);
                roars += shot.scene.rfind("boss_", 0) == 0 ? 1 : 0;
            }
    CHECK(roars == 7);
    for (const char* b : {"monolithe", "kraken", "phenix", "liche", "titan", "pyrrhax", "singularis"}) {
        CHECK(sheets.count(std::string("animations/") + b + ".json") == 1);
        bool scenery = false;
        for (const auto& s : sheets) scenery |= s.rfind(std::string("animations/stories/") + b + "_", 0) == 0;
        CHECK(scenery);
    }
    // A story file without the new fields reads as before (their reading time, no subtitle).
    StoryShot plain;
    plain.text = "court";
    CHECK(story::shotLength(plain) == story::shotDuration("court"));
}

TEST_CASE("trailer: attract mode on the main menu (a minute idle, loops, any input goes back, never when excluded)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 960;
    cfg.height = 540;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    app.headlessDt = 1.0f / 20.0f;  // 20 frames per simulated second: minutes in a few seconds
    const auto frames = [&](float seconds) { app.runFrames(static_cast<int>(std::lround(seconds * 20.0f))); };
    {
        GameServices g(app);
        g.loadAll();
        app.onSceneRender = [&g](bool covered) { g.ui.sceneInput = !covered; };  // as the game does (main.cpp)
        CHECK(g.trailer.has_value());
        g.progression.tutorial = "finished";
        g.settings.gameplay.attractTrailer = true;
        app.pushScene(makeMainMenu(g));
        frames(1.0f);
        Scene* menu = app.topScene();  // (a scene pushed is there from the next frame)
        CHECK(menu != nullptr);
        CHECK(attractAllowed(g));
        // Nothing before a minute without any input…
        frames(kAttractDelay - 3.0f);
        CHECK(app.sceneCount() == 1);
        CHECK(app.topScene() == menu);
        // …then the trailer, over the menu, in attract mode.
        frames(4.0f);
        CHECK(app.sceneCount() == 2);
        CHECK(trailerPasses(app.topScene()) == 0);
        // It loops for as long as nobody touches anything: two passes and more, still there.
        const float pass = story::duration(*g.trailer);
        frames(2.0f * pass + 2.0f);
        CHECK(app.sceneCount() == 2);
        CHECK(trailerPasses(app.topScene()) >= 2);
        // Any input (Enter here) goes back to the menu at once, without acting on it (Enter on
        // the menu would have opened the focused entry).
        SDL_Event down{};
        down.type = SDL_EVENT_KEY_DOWN;
        down.key.scancode = SDL_SCANCODE_RETURN;
        down.key.key = SDLK_RETURN;
        down.key.down = true;
        SDL_PushEvent(&down);
        frames(0.1f);
        SDL_Event up = down;
        up.type = SDL_EVENT_KEY_UP;
        up.key.down = false;
        SDL_PushEvent(&up);
        frames(0.5f);
        CHECK(app.sceneCount() == 1);
        CHECK(app.topScene() == menu);
        frames(2.0f);
        CHECK(app.topScene() == menu);  // no screen opened by that Enter
        // An input within the minute starts it again: the mouse moving after 50 s, then 20 s more.
        frames(50.0f);
        SDL_Event move{};
        move.type = SDL_EVENT_MOUSE_MOTION;
        move.motion.x = 300.0f;
        move.motion.y = 200.0f;
        move.motion.xrel = 4.0f;
        SDL_PushEvent(&move);
        frames(20.0f);
        CHECK(app.sceneCount() == 1);
        // Never over another window (the settings are open: the menu below waits).
        app.pushScene(makeSettingsScene(g, false));
        frames(0.5f);
        Scene* settings = app.topScene();
        CHECK(settings != menu);
        frames(kAttractDelay + 5.0f);
        CHECK(app.topScene() == settings);
        CHECK(trailerPasses(app.topScene()) == -1);
        app.popScene();
        frames(0.5f);
        CHECK(app.topScene() == menu);
        // Never while something is typed, before the tutorial, or with its setting off.
        app.input().startTextInput(app.window());
        CHECK(!attractAllowed(g));
        app.input().stopTextInput(app.window());
        g.progression.tutorial = "";
        CHECK(!attractAllowed(g));
        g.progression.tutorial = "finished";
        g.settings.gameplay.attractTrailer = false;
        CHECK(!attractAllowed(g));
        frames(kAttractDelay + 5.0f);
        CHECK(app.sceneCount() == 1);
        g.settings.gameplay.attractTrailer = true;
        CHECK(attractAllowed(g));
        app.shutdown();  // the scenes first (they use the services)
        app.onSceneRender = nullptr;
    }
    // Paramètres › Jeu: on by default, kept with the settings.
    Settings s;
    CHECK(s.gameplay.attractTrailer);
    s.gameplay.attractTrailer = false;
    CHECK(!Settings::fromJson(s.toJson()).gameplay.attractTrailer);
}
