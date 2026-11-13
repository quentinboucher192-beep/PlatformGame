// Cosmic Platformer - entry point.
//
// Command line (all optional):
//   --level <id>            start directly in a level
//   --spawn <x>,<y>         with --level: start at this position (pixels), skipping the intro
//   --editor [<id>]         start in the level editor
//   --enemy-editor          start in the enemy editor
//   --windowed / --fullscreen
//   --headless              hidden window, fixed 60 Hz frames (automated checks)
//   --frames <n>            quit after n frames
//   --shot <frame>:<file>   save a screenshot at a given frame (repeatable)
//   --input <script>        scripted gameplay input, e.g. "0:right;90:right,jump;120:"
//   --user-dir <path>       use another folder for saves and user content

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "ui/screens/Navigation.h"

using namespace cosmic;

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto has = [&](const char* f) {
        for (const auto& a : args)
            if (a == f) return true;
        return false;
    };
    auto value = [&](const char* f) -> std::string {
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == f) return args[i + 1];
        return {};
    };

    Paths::init(value("--user-dir"));
    Log::init(Paths::toUtf8(Paths::logFile()));
    Log::info("Cosmic Platformer {} — data: {} — user: {}", COSMIC_VERSION_STRING, Paths::toUtf8(Paths::installDir()),
              Paths::toUtf8(Paths::userDir()));

    Settings initial = SettingsStore::load();
    AppConfig cfg;
    cfg.headless = has("--headless");
    cfg.width = cfg.headless ? 1600 : initial.graphics.windowWidth;
    cfg.height = cfg.headless ? 900 : initial.graphics.windowHeight;
    cfg.fullscreen = !has("--windowed") && (initial.graphics.fullscreen || has("--fullscreen"));
    cfg.vsync = initial.graphics.vsync;

    Application app;
    if (!app.init(cfg)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cosmic Platformer",
                                 "Impossible d'initialiser le jeu. Consultez cosmic.log dans le dossier utilisateur.", nullptr);
        return 1;
    }
    GameServices game(app);
    game.loadAll();
    game.inputScript = value("--input");

    if (std::string f = value("--frames"); !f.empty()) app.maxFrames = std::atoi(f.c_str());
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--shot") continue;
        const std::string& s = args[i + 1];
        size_t c = s.find(':');
        if (c != std::string::npos) app.scheduledScreenshots.push_back({std::atoi(s.substr(0, c).c_str()), s.substr(c + 1)});
    }

    std::string level = value("--level");
    if (!level.empty()) {
        PlayOptions opt;
        if (std::string sp = value("--spawn"); !sp.empty()) {
            size_t c = sp.find(',');
            if (c != std::string::npos)
                opt.spawn = Vec2{static_cast<float>(std::atof(sp.substr(0, c).c_str())),
                                 static_cast<float>(std::atof(sp.substr(c + 1).c_str()))};
        }
        app.pushScene(makeGameScene(game, level, opt));
    } else if (has("--editor")) {
        app.pushScene(makeLevelEditor(game, value("--editor")));
    } else if (has("--enemy-editor")) {
        app.pushScene(makeEnemyEditor(game));
    } else {
        app.pushScene(makeSplashScene(game));
    }

    app.run();
    game.progression.save();
    game.records.save();
    app.shutdown();
    Log::info("Bye");
    Log::shutdown();
    return 0;
}
