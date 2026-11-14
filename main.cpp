// Cosmic Platformer - entry point.
//
// Command line (all optional):
//   --level <id>            start directly in a level
//   --spawn <x>,<y>         with --level: start at this position (pixels), skipping the intro
//   --pause                 with --level: open the pause menu once playing (screenshots)
//   --boss-attack <id>      with --level: the active boss uses this attack whenever it can (screenshots)
//   --editor [<id>]         start in the level editor (--editor-help: with its help open)
//   --enemy-editor          start in the enemy editor
//   --planets               start on the planet selection map
//   --level-select <planet> start on the levels of a planet (the last level played selected)
//   --hud-editor            open the HUD editor (place and size of the HUD elements)
//   --settings-tab <n>      open the settings on a tab (0 graphics ... 4 game)
//   --power-guide [--guide-page <n>]
//                           open the power guide of the help (animated demo of every power)
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
//   --online-open <what>    with --online: open register | login | settings |
//                           profile:<name> | replay:<name> (automated screenshots)
//   --online-server <url>   Cosmic Online server to use (else data/online/config.json)
//   --replay <file.cprz>    play a replay file (exact replays are re-simulated)
//   --analyze-levels <dir>  shortcut detector on the campaign levels (report + PPM maps)
//   --seal-levels <dir>     add gates against the shortcuts found (tools/levelgen/seals)
//   --save-replay <file.cprz>
//                           with --level: save the replay of the run when the level is completed
//   --online-manifest <file.json>
//                           write the ranking manifest of the built-in levels (server seed)

#include <cstdio>
#include <map>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "game/levels/DifficultyRating.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/online/Replay.h"
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
/// `--analyze-levels <folder>`: shortcut detector on the campaign levels (report + maps).
int analyzeFromCommandLine(const std::string& folder) {
    ContentDatabase content;
    content.reload();
    std::error_code ec;
    fs::create_directories(Paths::fromUtf8(folder), ec);
    int failures = 0;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        if (!level) continue;
        LevelAnalysis a = analyzeLevel(*level, content);
        std::printf("%-14s route %6.0f px / prévue %6.0f px (%3.0f %%)  montée %3.0f px  air %4.0f px  %s\n", e->id.c_str(), a.shortestRoute,
                    a.intendedRoute, a.ratio * 100.0f, a.maxRise, a.airReach, a.ok() ? "OK" : "PROBLÈMES");
        for (const auto& p : a.problems) std::printf("    - %s\n", p.c_str());
        writeAnalysisImage(*level, content, a, Paths::toUtf8(Paths::fromUtf8(folder) / (e->id + ".ppm")));
        failures += a.ok() ? 0 : 1;
    }
    return failures == 0 ? 0 : 1;
}
/// `--power-needs`: for every campaign level, which powers are indispensable (the exit or
/// a checkpoint can no longer be reached without them), to plan the power slots.
int powerNeedsFromCommandLine() {
    ContentDatabase content;
    content.reload();
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        if (!level) continue;
        std::set<std::string> all(level->startPowers.begin(), level->startPowers.end());
        for (const auto& o : level->objects)
            if (o.type == "power_unlock") all.insert(o.propStr("power"));
        std::printf("%-14s", e->id.c_str());
        for (const auto& p : all) {
            LevelData l = *level;
            l.startPowers.clear();
            for (const auto& q : all)
                if (q != p) l.startPowers.push_back(q);
            l.objects.erase(std::remove_if(l.objects.begin(), l.objects.end(),
                                           [&](const LevelObject& o) { return o.type == "power_unlock" && o.propStr("power") == p; }),
                            l.objects.end());
            LevelAnalysis a = analyzeLevel(l, content);
            std::string first;
            for (const auto& prob : a.problems)
                if (first.empty() && prob.find("inaccessible") != std::string::npos) first = prob;
            bool needed = !a.reachable || !first.empty();
            std::printf("  %s%s", p.c_str(), needed ? "*" : "");
            if (needed && !first.empty()) std::printf("(%s)", first.substr(0, 60).c_str());
        }
        std::printf("\n");
    }
    return 0;
}

/// `--plan-stations <dir>`: power slots plan of every campaign level (PowerSection list as
/// JSON, read by tools/levelgen to place the power stations).
int planStationsFromCommandLine(const std::string& folder) {
    ContentDatabase content;
    content.reload();
    std::error_code ec;
    fs::create_directories(Paths::fromUtf8(folder), ec);
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        if (!level) continue;
        std::vector<std::string> preferred = level->startPowers;
        if (level->metadata.contains("corePowers"))
            for (const auto& p : level->metadata["corePowers"]) preferred.insert(preferred.begin(), p.get<std::string>());
        auto sections = powerSections(*level, content, preferred);
        Json j = {{"level", e->id}, {"sections", Json::array()}};
        std::printf("%s\n", e->id.c_str());
        for (const auto& s : sections) {
            Json needs = Json::array();
            std::string txt;
            for (const auto& alt : s.needs) {
                needs.push_back(alt);
                txt += " " + alt.front() + (alt.size() > 1 ? "|" + alt[1] : "");
            }
            std::string mv;
            for (const auto& p : s.movement) mv += " +" + p;
            std::printf("   %-26s needs:%s%s%s\n", s.label.empty() ? "(départ)" : s.label.c_str(), txt.c_str(), mv.c_str(),
                        s.reachable ? "" : "  !! INATTEIGNABLE");
            j["sections"].push_back({{"label", s.label}, {"pos", {s.pos.x, s.pos.y}}, {"needs", needs}, {"movement", s.movement},
                                     {"reachable", s.reachable}});
        }
        Json st = Json::array();
        for (const auto& ps : planPowerStations(*level, content)) {
            Json tabs = Json::array();
            std::string txt;
            for (const auto& [p, c] : ps.tablets) {
                tabs.push_back({{"power", p}, {"center", {c.x, c.y}}});
                txt += " " + p;
            }
            st.push_back({{"section", ps.section}, {"mid", ps.midSection}, {"sign", {ps.sign.x, ps.sign.y}}, {"tablets", tabs},
                          {"upcoming", ps.upcoming}});
            std::printf("   STATION %s%s :%s\n", ps.section.empty() ? "(départ)" : ps.section.c_str(), ps.midSection ? " (milieu)" : "", txt.c_str());
        }
        j["stations"] = st;
        j["slots"] = level->powerSlots();
        FileUtil::saveJson(Paths::fromUtf8(folder) / (e->id + ".json"), j);
    }
    return 0;
}

/// `--rate-levels [file]`: computes the difficulty of every campaign level (breakdown on the
/// console); with a file, writes them as data/levels/ratings.json does.
int rateFromCommandLine(const std::string& file) {
    ContentDatabase content;
    content.reload();
    Json out = {{"format", "cosmic.level_ratings"}, {"version", DifficultyRating::kRatingVersion}, {"levels", Json::object()}};
    std::printf("%-14s %5s %6s | %5s %5s %5s %5s %5s %5s %5s %5s | sauts(vide) pièges ennemis cp-max soin/100\n", "niveau", "note", "brut",
                "saut", "piège", "enn.", "boss", "cp", "soin", "temps", "long");
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        if (!level) continue;
        DifficultyRating r = rateDifficulty(*level, content);
        std::printf("%-14s %5.1f %6.2f | %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f | %3d(%2d) %4d %4d %6.0f %5.1f\n", e->id.c_str(), r.score,
                    r.raw, r.jumps, r.hazards, r.enemies, r.boss, r.checkpoints, r.healing, r.timer, r.length, r.preciseJumps, r.deadlyJumps,
                    r.hazardCount, r.enemyCount, r.longestCheckpointGap, r.healPer100);
        out["levels"][e->id] = r.toJson();
    }
    if (!file.empty() && file != "1") {
        std::string text = out.dump(2) + "\n";
        if (!FileUtil::writeTextAtomic(Paths::fromUtf8(file), text)) return 1;
    }
    return 0;
}
/// `--seal-levels <folder>`: for each campaign level whose checkpoints can be bypassed, adds
/// suggested gates to <folder>/<level>.json (read by tools/levelgen/levelkit.py).
int sealFromCommandLine(const std::string& folder) {
    ContentDatabase content;
    content.reload();
    std::error_code ec;
    fs::create_directories(Paths::fromUtf8(folder), ec);
    int added = 0;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        if (!level) continue;
        LevelAnalysis a = analyzeLevel(*level, content);
        auto gates = suggestGates(*level, a);
        if (gates.empty()) continue;
        // Material of the level's terrain (most common one).
        std::map<std::string, int> count;
        for (const auto& o : level->objects)
            if (o.type == "solid") ++count[o.propStr("material", "ruin_stone")];
        std::string material = "ruin_stone";
        int best = 0;
        for (const auto& [m, n] : count)
            if (n > best) {
                best = n;
                material = m;
            }
        fs::path file = Paths::fromUtf8(folder) / (e->id + ".json");
        Json list = Json::array();
        if (auto j = FileUtil::loadJson(file); j && j->is_array()) list = *j;
        for (const auto& g : gates) {
            bool known = false;
            for (const auto& e : list)
                if (e.contains("rect") && e["rect"][0].get<float>() == g.x && e["rect"][3].get<float>() == g.h) known = true;
            if (known) continue;
            list.push_back({{"rect", {g.x, g.y, g.w, g.h}}, {"material", material}});
            std::printf("%-14s porte x=%.0f  hauteur %.0f px\n", e->id.c_str(), g.x, g.h);
            ++added;
        }
        FileUtil::saveJson(file, list);
    }
    std::printf("%d porte(s) ajoutée(s)\n", added);
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

    if (std::string dir = value("--analyze-levels"); !dir.empty()) {
        int rc = analyzeFromCommandLine(dir);
        Log::shutdown();
        return rc;
    }

    if (std::string dir = value("--plan-stations"); !dir.empty()) {
        int rc = planStationsFromCommandLine(dir);
        Log::shutdown();
        return rc;
    }

    if (has("--power-needs")) {
        int rc = powerNeedsFromCommandLine();
        Log::shutdown();
        return rc;
    }

    if (has("--rate-levels")) {
        int rc = rateFromCommandLine(value("--rate-levels"));
        Log::shutdown();
        return rc;
    }

    if (std::string dir = value("--seal-levels"); !dir.empty()) {
        int rc = sealFromCommandLine(dir);
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
    game.saveReplayPath = value("--save-replay");
    game.onlineFocus = value("--online-focus");
    game.onlineOpen = value("--online-open");
    game.online.init(game.settings.online, value("--online-server"));
    OnlineToasts toasts;
    game.checkForUpdate();
    app.onUpdate = [&game, &toasts](float dt) {
        game.ratings.poll();
        game.news.poll(game.content, game.records, COSMIC_VERSION_STRING);
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
        opt.openPause = has("--pause");
        opt.forceBossAttack = value("--boss-attack");
        app.pushScene(makeGameScene(game, level, opt));
    } else if (has("--editor")) {
        if (has("--editor-help")) app.pushScene(makeLevelEditorHelp(game, value("--editor")));
        else app.pushScene(makeLevelEditor(game, value("--editor")));
    } else if (has("--enemy-editor")) {
        if (std::string at = value("--enemy-attack"); !at.empty()) game.onlineOpen = "attack:" + at;
        app.pushScene(makeEnemyEditor(game, value("--enemy-editor"), std::atoi(value("--enemy-tab").c_str())));
    } else if (std::string ls = value("--level-select"); !ls.empty()) {
        app.pushScene(makeLevelSelect(game, ls));
    } else if (has("--planets")) {
        app.pushScene(makePlanetMap(game));
    } else if (std::string et = value("--editor-test"); !et.empty()) {
        // Automated checks: the editor's test run of a player level (completion replay).
        PlayOptions opt;
        opt.level = game.content.loadLevel(et);
        opt.editorTest = true;
        if (opt.level) app.pushScene(makeGameScene(game, et, opt));
        else app.pushScene(makeSplashScene(game));
    } else if (std::string pub = value("--publish"); !pub.empty()) {
        app.pushScene(makeCommunityPublish(game, pub));
    } else if (has("--online-levels")) {
        app.pushScene(makeOnlineLevels(game, value("--online-levels")));
    } else if (std::string st = value("--settings-tab"); !st.empty()) {
        game.onlineOpen = "settings-tab:" + st;
        app.pushScene(makeSettingsScene(game, false));
    } else if (has("--hud-editor")) {
        app.pushScene(makeHudEditor(game, game.settings.gameplay));
    } else if (has("--power-guide")) {
        std::string page = value("--guide-page");
        app.pushScene(makePowerGuide(game, page.empty() ? 0 : std::atoi(page.c_str()) - 1));
    } else if (has("--notifications")) {
        app.pushScene(makeNotifications(game, value("--notifications") == "online" ? 1 : 0));
    } else if (std::string pid = value("--profile"); !pid.empty()) {
        app.pushScene(makePlayerProfile(game, pid));
    } else if (has("--social")) {
        app.pushScene(makeSocial(game, value("--social")));
    } else if (has("--online")) {
        app.pushScene(makeOnlineRanking(game, value("--online")));
    } else if (std::string rf = value("--replay"); !rf.empty()) {
        std::string err;
        std::optional<ReplayData> rep;
        if (auto bytes = FileUtil::readBinary(Paths::fromUtf8(rf))) rep = ReplayData::decode(*bytes, &err);
        else err = "lecture impossible";
        if (!rep) {
            Log::error("Replay {} : {}", rf, err);
            app.pushScene(makeSplashScene(game));
        } else {
            app.pushScene(makeLocalReplayViewer(game, std::move(*rep), Paths::toUtf8(Paths::fromUtf8(rf).filename())));
        }
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
