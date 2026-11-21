// 2.84: the raid's pause in the Cosmic kit, without the « Menu du raid » entry (the user asked for it gone: its key,
// Tab or LS, opens the raid's menu, as the help line of the pause says); no screen draws that label any more.

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
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

}  // namespace

TEST_CASE("raid (2.84): the pause has no « Menu du raid » entry, Tab still opens the raid's menu") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    app.init(cfg);
    app.headlessDt = 1.0f / 30.0f;
    {
        auto g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [&g](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->applySettings();
        UIContext& ui = g->ui;
        RaidTestSetup t;
        t.branch = "bras_droit";
        t.zone = 3;
        t.players = 2;
        t.capture = true;
        app.pushScene(makeRaidTest(*g, t));
        app.runFrames(40);
        key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);  // the pause
        app.runFrames(4);
        CHECK(ui.navigable("raidgame/pause/resume"));
        CHECK(ui.navigable("raidgame/pause/quit"));
        CHECK(!ui.navigable("raidgame/pause/menu"));  // « Menu du raid · Tab »: gone
        // Down from « Reprendre » reaches « Quitter » (one entry fewer, nothing in between in a plain test).
        ui.setFocus("raidgame/pause/resume");
        app.runFrames(1);
        key(SDL_SCANCODE_DOWN, SDLK_DOWN);
        app.runFrames(2);
        CHECK(ui.focused("raidgame/pause/quit"));
        key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);  // closed again
        app.runFrames(4);
        CHECK(!ui.navigable("raidgame/pause/resume"));
        key(SDL_SCANCODE_TAB, SDLK_TAB);  // the raid's menu, by its key
        app.runFrames(4);
        CHECK(ui.navigable("raidgame/raidmenu/close"));
        key(SDL_SCANCODE_TAB, SDLK_TAB);
        app.runFrames(2);
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
    }
    // No screen writes « Menu du raid » on a button any more (the HUD's button shows the key only).
    namespace fs = std::filesystem;
    const fs::path root = Paths::installDir();  // the source tree (assets/ and data/ beside ui/ and game/)
    int files = 0;
    for (const char* dir : {"ui", "game"}) {
        std::error_code ec;
        if (!fs::exists(root / dir, ec)) continue;
        for (const auto& f : fs::recursive_directory_iterator(root / dir, ec)) {
            if (f.path().extension() != ".cpp" && f.path().extension() != ".h") continue;
            std::ifstream in(f.path());
            std::stringstream ss;
            ss << in.rdbuf();
            ++files;
            if (ss.str().find("\"Menu du raid") != std::string::npos) std::fprintf(stderr, "« Menu du raid » in %s\n", f.path().string().c_str());
            CHECK(ss.str().find("\"Menu du raid") == std::string::npos);
        }
    }
    if (files == 0) std::fprintf(stderr, "raid pause: sources not found from %s (label scan skipped)\n", root.string().c_str());
}
