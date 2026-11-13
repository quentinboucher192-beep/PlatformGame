// Cosmic Platformer - entry point.
//
// Command line (all optional):
//   --level <id>            start directly in a level
//   --spawn <x>,<y>         with --level: start at this position (pixels), skipping the intro
//   --editor [<id>]         start in the level editor
//   --enemy-editor          start in the enemy editor
//   --planets               start on the planet selection map
//   --windowed / --fullscreen
//   --headless              hidden window, fixed 60 Hz frames (automated checks)
//   --frames <n>            quit after n frames
//   --shot <frame>:<file>   save a screenshot at a given frame (repeatable)
//   --input <script>        scripted gameplay input, e.g. "0:right;90:right,jump;120:"
//   --user-dir <path>       use another folder for saves and user content
//   --import <file.cplanet> open the import screen with this package (also: file as argument)
//   --export <level> <file.cplanet> [--author <name>]
//                           export a level and all its dependencies without opening a window
//   --online [<level>]      start on the ONLINE RANKING screen
//   --online-focus <name>   with --online: select the globe marker of this player
//   --online-server <url>   Cosmic Online server to use (else data/online/config.json)
//   --online-manifest <file.json>
//                           write the ranking manifest of the built-in levels (server seed)

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/PackageExporter.h"
#include "ui/online/OnlineToasts.h"
#include "ui/screens/Navigation.h"

using namespace cosmic;

namespace {
/// `--export <level> <file>`: same exporter as the content manager, without a window.
int exportFromCommandLine(const std::string& levelId, const std::string& file, const std::string& author) {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    res.init(nullptr, nullptr);
    std::string err;
    auto level = content.loadLevel(levelId, &err);
    if (!level) {
        Log::error("Export: niveau '{}' introuvable ({})", levelId, err);
        return 2;
    }
    PackageExporter exporter(content, res);
    ExportOptions opt;
    opt.author = author.empty() ? level->author : author;
    opt.destination = Paths::fromUtf8(file);
    ExportPlan plan = exporter.plan(*level, opt);
    for (const auto& w : plan.warnings) Log::warn("Export: {}", w);
    if (!plan.ok() || !exporter.write(*level, plan, opt, &err)) {
        for (const auto& m : plan.missing) Log::error("Export: dépendance manquante: {}", m);
        Log::error("Export impossible: {}", err.empty() ? "dépendances manquantes" : err);
        return 3;
    }
    Log::info("Export: {} -> {} ({} fichiers, {} octets)", levelId, file, plan.items.size(), plan.totalSize);
    return 0;
}

/// `--online-manifest <file>`: fingerprints and measures of the built-in levels, used by
/// server/scripts/seed-levels.mjs to register the level revisions on the server.
int manifestFromCommandLine(const std::string& file) {
    ContentDatabase content;
    content.reload();
    std::string err;
    Json manifest = buildOnlineManifest(content, &err);
    if (manifest.is_null()) {
        Log::error("Manifeste en ligne impossible : {}", err);
        return 2;
    }
    if (!FileUtil::saveJson(Paths::fromUtf8(file), manifest)) {
        Log::error("Écriture impossible : {}", file);
        return 3;
    }
    Log::info("Manifeste en ligne : {} niveaux -> {}", manifest["levels"].size(), file);
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto has = [&](const char* f) {
        for (const auto& a : args)
            if (a == f) return true;
        return false;
    };
    auto value = [&](const char* f) -> std::string {
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == f && args[i + 1].rfind("--", 0) != 0) return args[i + 1];
        return {};
    };

    Paths::init(value("--user-dir"));
    Log::init(Paths::toUtf8(Paths::logFile()));
    Log::info("Cosmic Platformer {} — data: {} — user: {}", COSMIC_VERSION_STRING, Paths::toUtf8(Paths::installDir()),
              Paths::toUtf8(Paths::userDir()));

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--export") continue;
        if (i + 2 >= args.size()) {
            Log::error("Usage: --export <niveau> <fichier.cplanet> [--author <nom>]");
            return 2;
        }
        int rc = exportFromCommandLine(args[i + 1], args[i + 2], value("--author"));
        Log::shutdown();
        return rc;
    }

    if (std::string m = value("--online-manifest"); !m.empty()) {
        int rc = manifestFromCommandLine(m);
        Log::shutdown();
        return rc;
    }

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
    game.onlineFocus = value("--online-focus");
    game.online.init(game.settings.online, value("--online-server"));
    OnlineToasts toasts;
    app.onUpdate = [&game, &toasts](float dt) {
        game.online.update(dt);
        toasts.update(game, dt);
    };
    app.onOverlay = [&game, &toasts] { toasts.render(game); };

    if (std::string f = value("--frames"); !f.empty()) app.maxFrames = std::atoi(f.c_str());
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--shot") continue;
        const std::string& s = args[i + 1];
        size_t c = s.find(':');
        if (c != std::string::npos) app.scheduledScreenshots.push_back({std::atoi(s.substr(0, c).c_str()), s.substr(c + 1)});
    }

    // A .cplanet given on the command line (file association) opens the import screen.
    std::string importFile = value("--import");
    for (const auto& a : args)
        if (a.size() > 8 && a.substr(a.size() - 8) == ".cplanet") importFile = a;

    std::string level = value("--level");
    if (!importFile.empty()) {
        app.pushScene(makeContentManager(game, importFile));
    } else if (!level.empty()) {
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
    } else if (has("--planets")) {
        app.pushScene(makePlanetMap(game));
    } else if (has("--online")) {
        app.pushScene(makeOnlineRanking(game, value("--online")));
    } else {
        app.pushScene(makeSplashScene(game));
    }

    app.run();
    game.progression.save();
    game.records.save();
    game.online.shutdown();
    app.shutdown();
    Log::info("Bye");
    Log::shutdown();
    return 0;
}
