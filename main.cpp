// Cosmic Platformer - entry point.
//
// Command line (all optional):
//   --level <id>            start directly in a level
//   --spawn <x>,<y>         with --level: start at this position (pixels), skipping the intro
//   --pause                 with --level: open the pause menu once playing (screenshots)
//   --boss-attack <id>      with --level: the active boss uses this attack whenever it can (screenshots)
//   --editor [<id>]         start in the level editor (--editor-help: with its help open)
//   --tutorial              start the tutorial of the first launch at once (--tutorial-welcome: its welcome)
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
//   --realtime              headless: frames paced at real time (checks against a server)
//   --click <frame>:<x>,<y> left click at a screen position at a given frame (repeatable;
//                           automated checks of the menus)
//   --hover <frame>:<x>,<y> the mouse moved there without a click (tooltips in screenshots)
//   --drag <frame>:<x1>,<y1>:<x2>,<y2>[:<frames>]
//                           press, move and release the left button (scroll bars, sliders)
//   --input <script>        scripted gameplay input, e.g. "0:right;90:right,jump;120:"
//   --user-dir <path>       use another folder for saves and user content
//   --lang <fr|en>          language of the texts (as the setting « Langue / Language »)
//   --import <file.cplanet> open the import screen with this package (also: file as argument)
//   --export <level> <file.cplanet> [--author <name>]
//                           export a level and all its dependencies without opening a window
//   --admin [players[:<search>[:confirm]]|calendar[:<YYYY-MM>[:<challenge id>[:objectif]]]|log|
//            campaign[:<level id>]|creations[:new|:<kind>[:new|:<id>]]]
//                           open ADMINISTRATION (Joueurs, Calendrier des défis, Journal, Campagne)
//                           with an admin account (calendar: that month, that period in the
//                           editor, its tab « Objectif et paliers »; campaign: that level's history;
//                           creations: a kind's list (suit, trail, frame, victory), a new one, one
//                           creation by its id; 2.61 creations:studio[:new:<kind>|:import[:<file>]|
//                           :<draft>]: the Studio, a new creation, the import of a PNG, a draft)
//   --stats [<section>]     open the player's statistics (0 overview ... 5 data and saves)
//   --stats-day <YYYY-MM-DD>
//                           count the runs of this session on that day (automated checks of the
//                           statistics: real runs spread over several days)
//   --story <boss>[:<shot>|:titre|@<seconds>]
//                           tell the story of a giga boss over the main menu (from that shot,
//                           1 = the first, its line typed; « titre »: the title card revealed;
//                           @12.5: from that moment, as it plays then)
//   --asterion-film [<seconds>]
//                           play Asterion's film (2.68) over the main menu as its first viewing
//                           (nothing skips it), from that moment of it (screenshots)
//   --boss-stats [<boss>[:<clip>][:fiche][:attaques][:attaque=<attack>|:tout]]
//                           open the boss statistics (that boss, that clip of its animation;
//                           « fiche »: its sheet; « attaques »: its list of attacks; that attack
//                           unfolded with its example, or every attack unfolded)
//   --boss-training <boss> [--boss-training-go] [--boss-training-spawn <where>[:pick]]
//                           open the settings of a boss training (and enter its arena);
//                           <where>: usual | suggested | <x>,<y> (« Lieu d'apparition »),
//                           :pick (or « pick » alone) opens the map of the arena
//   --challenges [day|week|month|calendar[:<YYYY-MM>|:<id>]]
//                           open the challenges (Jour, Semaine, Mois, or the Calendrier on a
//                           month or on a challenge's card)
//   --challenge-start <id>[:practice]
//                           open the screen before a challenge run (cached in saves/challenges)
//   --challenge-play <id>[:practice]
//                           start a challenge run at once (with --input / --save-replay: checks)
//   --online [<level>]      start on the ONLINE RANKING screen
//   --online-focus <name>   with --online: select the globe marker of this player
//   --online-open <what>    with --online: open register | login | settings |
//                           profile:<name> | replay:<name> (automated screenshots)
//   --online-server <url>   Cosmic Online server to use (else data/online/config.json)
//   --clans [<clan id>]     open ONLINE › CLANS (on that clan's page)
//   --race <kind:level[:version]>
//                           the « course à la vérification » of an EXTRÊME level, over
//                           ONLINE RANKING (official) or ONLINE LEVELS (community)
//   --replay <file.cprz>    play a replay file (exact replays are re-simulated)
//   --compare <a.cprz>,<b.cprz>
//                           compare two replay files (A played, B as a ghost)
//   --compare-setup <level> open « Comparer deux replays » for a level
//   --replay-view <options> with --replay / --compare (screenshots): at=<s>,offset=<ms>,
//                           align=<row>,cam=<0..2>,pause
//   --analyze-levels <dir>  shortcut detector on the campaign levels (report + PPM maps)
//   --seal-levels <dir>     add gates against the shortcuts found (tools/levelgen/seals)
//   --plan-power-keys <dir> the keys of the powers of every campaign level, for
//                           tools/levelgen (tools/levelgen/power_keys)
//   --save-replay <file.cprz>
//                           with --level: save the replay of the run when the level is completed
//   --online-manifest <file.json>
//                           write the ranking manifest of the built-in levels (server seed)
//   --keep-records <id,id…> before tools/levelgen changes these levels without a major change:
//                           their current fingerprints go to data/levels/kept_records.json
//                           (their records stay valid on the next version, 2.50)

#include <algorithm>
#include <cstdio>
#include <map>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/levels/DifficultyRating.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/online/Replay.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/online/LevelFingerprint.h"
#include "game/powers/PowerKeys.h"
#include "packaging/PackageExporter.h"
#include "ui/online/OnlineToasts.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/BossStoryScene.h"

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
/// `--keep-records <id,id…>`: the current fingerprints (gameplay and ranked) of these campaign
/// levels, added to data/levels/kept_records.json: their records stay valid on the version that
/// tools/levelgen writes next (to run BEFORE regenerating them; the server links the same
/// rankings with `node scripts/levels-migration.mjs manifest.json --keep <id,id…>`).
int keepRecordsFromCommandLine(const std::string& ids) {
    ContentDatabase content;
    content.reload();
    const fs::path file = Paths::dataDir() / "levels" / "kept_records.json";
    Json j = FileUtil::loadJson(file).value_or(Json::object());
    if (!j.is_object()) j = Json::object();
    j["format"] = "cosmic.kept_records";
    j["comment"] = "Earlier versions of campaign levels whose records count on the shipped version (CosmicPlatformer --keep-records; game/records/KeptRecords.h).";
    if (!j.contains("levels") || !j["levels"].is_object()) j["levels"] = Json::object();
    LevelFingerprints fingerprints;  // keeps the fingerprints it returns
    size_t start = 0;
    int added = 0;
    while (start <= ids.size()) {
        size_t comma = ids.find(',', start);
        std::string id = ids.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? ids.size() + 1 : comma + 1;
        if (id.empty()) continue;
        auto level = content.loadLevel(id);
        const LevelEntry* e = content.level(id);
        if (!level || !e || e->source != ContentSource::Builtin) {
            Log::error("Niveau de la campagne inconnu : {}", id);
            return 2;
        }
        const LevelFingerprint* fp = fingerprints.get(content, id);
        const std::string gameplay = fingerprintGameplay(content, *level), ranked = fp ? fp->hash : std::string();
        Json& list = j["levels"][id];
        if (!list.is_array()) list = Json::array();
        bool known = false;
        for (const Json& v : list) known = known || (v.value("gameplay", std::string()) == gameplay && v.value("ranked", std::string()) == ranked);
        if (known) continue;
        list.push_back({{"gameplay", gameplay}, {"ranked", ranked}, {"version", fp ? fp->version : 0}, {"until", COSMIC_VERSION_STRING}});
        ++added;
        Log::info("Records gardés : {} révision {} ({} / {})", id, fp ? fp->version : 0, gameplay.substr(0, 12), ranked.substr(0, 12));
    }
    if (!FileUtil::saveJson(file, j)) {
        Log::error("Écriture impossible : {}", Paths::toUtf8(file));
        return 3;
    }
    Log::info("{} version(s) ajoutée(s) à {}", added, Paths::toUtf8(file));
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

/// `--plan-power-keys <dir>`: the keys of the powers of every campaign level (2.31), written by
/// tools/levelgen/levelkit.py into the level (`powerKeys`): each power's suggestion, conflicts
/// resolved as for a level without its own choice, so that a power has the same key in every
/// level whenever possible (the report says when it does not).
int planPowerKeysFromCommandLine(const std::string& folder) {
    ContentDatabase content;
    content.reload();
    std::error_code ec;
    fs::create_directories(Paths::fromUtf8(folder), ec);
    std::map<std::string, std::map<std::string, int>> used;  // power -> its keys ("key / pad") -> levels
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        if (!level) continue;
        Json keys = planLevelPowerKeys(*level, content);
        std::printf("%-14s", e->id.c_str());
        for (const auto& [power, k] : keys.items()) {
            std::string both = jstr(k, "key") + " / " + (k.contains("pad") ? k["pad"].dump() : std::string("-"));
            std::printf("  %s=%s", power.c_str(), both.c_str());
            ++used[power][both];
        }
        std::printf("\n");
        FileUtil::saveJson(Paths::fromUtf8(folder) / (e->id + ".json"), Json{{"level", e->id}, {"powerKeys", keys}});
    }
    int split = 0;
    for (const auto& [power, keys] : used)
        if (keys.size() > 1) {
            ++split;
            std::printf("!! %s n'a pas la même touche partout :", power.c_str());
            for (const auto& [k, n] : keys) std::printf("  %s (%d)", k.c_str(), n);
            std::printf("\n");
        }
    std::printf("%zu pouvoirs, %d sans touche commune\n", used.size(), split);
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

    if (std::string dir = value("--plan-power-keys"); !dir.empty()) {
        int rc = planPowerKeysFromCommandLine(dir);
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

    if (std::string k = value("--keep-records"); !k.empty()) {
        int rc = keepRecordsFromCommandLine(k);
        Log::shutdown();
        return rc;
    }

    if (std::string m = value("--online-manifest"); !m.empty()) {
        int rc = manifestFromCommandLine(m);
        Log::shutdown();
        return rc;
    }

    Settings initial = SettingsStore::load();
    applyInstallerLanguage(initial, Paths::installDir() / "language.ini");  // saved by loadAll
    i18n::setLanguage(settingsLanguage(initial));
    const std::optional<Language> langArg = i18n::parse(value("--lang"));
    if (langArg) i18n::setLanguage(*langArg);
    AppConfig cfg;
    cfg.headless = has("--headless");
    // Screenshots without a screen: 1600×900 unless --width / --height say otherwise.
    auto sizeArg = [&](const char* name, int def) {
        std::string v = value(name);
        int n = v.empty() ? def : std::atoi(v.c_str());
        return std::clamp(n, 640, 3840);
    };
    cfg.width = cfg.headless ? sizeArg("--width", 1600) : initial.graphics.windowWidth;
    cfg.height = cfg.headless ? sizeArg("--height", 900) : initial.graphics.windowHeight;
    cfg.fullscreen = !has("--windowed") && (initial.graphics.fullscreen || has("--fullscreen"));
    cfg.vsync = initial.graphics.vsync;

    Application app;
    if (!app.init(cfg)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cosmic Platformer",
                                 i18n::tr("Impossible d'initialiser le jeu. Consultez cosmic.log dans le dossier utilisateur.").c_str(),
                                 nullptr);
        return 1;
    }
    GameServices game(app);
    game.loadAll();
    // 2.72 (phase 6): the generated sheets some cosmetics draw (« Supernova d'Asterion », « Cœur d'Asterion »).
    setCosmeticTextures([&app](const std::string& path) { return app.resources().texture(path); });
    if (!cfg.headless) {
        // The window as it really is (--fullscreen / --windowed, or the saved setting): the
        // settings follow it, and a toggle by F11 / Alt+Entrée is saved too.
        if (app.fullscreen() != cfg.fullscreen) app.setFullscreen(cfg.fullscreen);
        game.settings.graphics.fullscreen = cfg.fullscreen;
        app.onFullscreenToggled = [&game](bool on) {
            game.settings.graphics.fullscreen = on;
            game.saveSettings();
        };
    }
    if (langArg) {
        game.settings.language = i18n::code(*langArg);
        game.applySettings();
    }
    // 2.54, automated checks: the day the statistics count this session's runs on.
    if (std::string sd = value("--stats-day"); sd.size() == 10) {
        int y = std::atoi(sd.substr(0, 4).c_str()), mo = std::atoi(sd.substr(5, 2).c_str()), d = std::atoi(sd.substr(8, 2).c_str());
        if (y > 1970 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) game.playStats.dayOverride = daysFromCivil(y, mo, d);
    }
    game.inputScript = value("--input");
    game.saveReplayPath = value("--save-replay");
    game.onlineFocus = value("--online-focus");
    game.onlineOpen = value("--online-open");
    game.online.init(game.settings.online, value("--online-server"));
    // 2.47: local and online. What is played logged out (local mode) is summed up at the next
    // login, and copies of the progression go to the account.
    game.online.onSessionStarted = [&game] { game.noteSessionStarted(); };
    game.online.onLoggedOut = [&game] { game.noteLoggedOut(); };
    game.noteStartedLocal();
    // 2.45: the campaign revisions published online (kept ones at once, offline too; the
    // manifest read when Cosmic Online is on and the window is real).
    game.startCampaignRevisions(!has("--headless") || has("--realtime"));
    // The suits created on Cosmic Online by an administrator (their colours: the list kept on
    // this computer at once, read again from the server like the campaign revisions).
    game.startServerSuits(!has("--headless") || has("--realtime"));
    // Daily challenges: the cached ones at once (offline), the current one asked for when
    // Cosmic Online is on (public, nothing personal sent).
    game.challenges.init(game.online, Paths::savesDir() / "challenges");
    game.challengeRewardsChanged(false);  // 2.34: the rewards of the challenges kept on this computer are worn too
    game.updates.init(game.online, game.news, COSMIC_VERSION_STRING);
    if (!has("--headless") || has("--realtime")) game.challenges.refresh();
    OnlineToasts toasts;
    game.checkForUpdate();
    // Scripted mouse clicks (automated checks): moved, pressed, released two frames later.
    struct Click {
        int frame;
        float x, y;
    };
    std::vector<Click> clicks, hovers;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--click" && args[i] != "--hover") continue;
        const std::string& s = args[i + 1];
        size_t c = s.find(':'), k = s.find(',');
        if (c != std::string::npos && k != std::string::npos && k > c)
            (args[i] == "--click" ? clicks : hovers)
                .push_back({std::atoi(s.substr(0, c).c_str()), std::strtof(s.substr(c + 1, k - c - 1).c_str(), nullptr),
                            std::strtof(s.substr(k + 1).c_str(), nullptr)});
    }
    // --drag <frame>:<x1>,<y1>:<x2>,<y2>[:<frames>] (screenshots): the left button pressed at
    // (x1, y1) on that frame, the pointer moved to (x2, y2) over <frames> frames (default 30),
    // then released.
    struct Drag {
        int frame = 0, frames = 30;
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    };
    std::vector<Drag> drags;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--drag") continue;
        Drag d;
        if (std::sscanf(args[i + 1].c_str(), "%d:%f,%f:%f,%f:%d", &d.frame, &d.x1, &d.y1, &d.x2, &d.y2, &d.frames) >= 5) drags.push_back(d);
    }
    // Presence for friends: a real window, or a headless run against a server (--realtime).
    const bool sendPresence = !has("--headless") || has("--realtime");
    // Live spectating (2.36): the same (a headless check streams only against a server).
    game.spectate.setAllowed(sendPresence);
    app.onUpdate = [&game, &toasts, &app, clicks, hovers, drags, sendPresence](float dt) {
        int f = static_cast<int>(app.frameIndex());
        for (const auto& d : drags) {
            int n = std::max(1, d.frames);
            if (f < d.frame || f > d.frame + n + 1) continue;
            float t = std::clamp(static_cast<float>(f - d.frame) / static_cast<float>(n), 0.0f, 1.0f);
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = d.x1 + (d.x2 - d.x1) * t;
            e.motion.y = d.y1 + (d.y2 - d.y1) * t;
            e.motion.state = SDL_BUTTON_LMASK;
            SDL_PushEvent(&e);
            if (f == d.frame || f == d.frame + n + 1) {
                SDL_Event b{};
                b.type = f == d.frame ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                b.button.down = f == d.frame;
                b.button.button = SDL_BUTTON_LEFT;
                b.button.x = e.motion.x;
                b.button.y = e.motion.y;
                SDL_PushEvent(&b);
            }
        }
        for (const auto& h : hovers) {
            if (f != h.frame) continue;
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = h.x;
            e.motion.y = h.y;
            SDL_PushEvent(&e);
        }
        for (const auto& c : clicks) {
            if (f != c.frame && f != c.frame + 2) continue;
            SDL_Event e{};
            if (f == c.frame) {
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.x = c.x;
                e.motion.y = c.y;
                SDL_PushEvent(&e);
                e = SDL_Event{};
                e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                e.button.down = true;
            } else {
                e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            }
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = c.x;
            e.button.y = c.y;
            SDL_PushEvent(&e);
        }
        game.ratings.poll();
        game.news.poll(game.content, game.records, COSMIC_VERSION_STRING);
        game.online.update(dt);
        game.updateProgressCopy(dt);  // 2.47: copies of the progression
        game.replayDownloads.update();
        game.challenges.update(dt);
        game.updateCampaignRevisions(dt);
        game.updateServerSuits(dt);
        game.updates.update();
        if (sendPresence) game.updatePresence(dt);
        game.spectate.update(dt);
        toasts.update(game, dt);
    };
    app.onOverlay = [&game, &toasts] { toasts.render(game); };
    // A scene under another one (a profile over a leaderboard...) never reacts to input.
    app.onSceneRender = [&game](bool covered) { game.ui.sceneInput = !covered; };

    if (std::string f = value("--frames"); !f.empty()) app.maxFrames = std::atoi(f.c_str());
    app.headlessRealtime = has("--realtime");  // headless at 60 frames per second (server checks)
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

    // A project invitation link (cosmic://rejoindre/<clé>, registered by the installer) opens
    // PROJETS with the key ready: joining stays the player's choice.
    std::string joinLink;
    for (const auto& a : args)
        if (a.size() > 9 && (a.rfind("cosmic://", 0) == 0 || a.rfind("COSMIC://", 0) == 0)) joinLink = a.substr(0, 200);

    std::string level = value("--level");
    if (!importFile.empty() || has("--workshop")) {
        app.pushScene(makeContentManager(game, importFile));
    } else if (!joinLink.empty()) {
        game.onlineOpen = "join-key:" + joinLink;
        app.pushScene(makeProjects(game));
    } else if (!level.empty()) {
        PlayOptions opt;
        if (std::string sp = value("--spawn"); !sp.empty()) {
            size_t c = sp.find(',');
            if (c != std::string::npos)
                opt.spawn = Vec2{static_cast<float>(std::atof(sp.substr(0, c).c_str())),
                                 static_cast<float>(std::atof(sp.substr(c + 1).c_str()))};
        }
        opt.openPause = has("--pause") || has("--pause-after");
        opt.pauseAfter = static_cast<float>(std::atof(value("--pause-after").c_str()));
        opt.forceBossAttack = value("--boss-attack");
        opt.forceBossPhase = std::atoi(value("--boss-phase").c_str());
        // --training-from <checkpoint>: a section training from that checkpoint (automated checks;
        // "start" = from the level's start to its first checkpoint).
        if (std::string tf = value("--training-from"); !tf.empty()) {
            opt.training = true;
            if (tf != "start") opt.trainingCheckpoint = tf;
        }
        app.pushScene(makeGameScene(game, level, opt));
    } else if (has("--tutorial") || has("--tutorial-welcome")) {
        // 2.64: the tutorial of the first launch at once, or its welcome (checks, captures).
        std::unique_ptr<Scene> run = has("--tutorial") ? makeTutorialRun(game) : makeTutorialWelcome(game);
        app.pushScene(run ? std::move(run) : makeMainMenu(game));
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
    } else if (has("--projects")) {
        app.pushScene(makeProjects(game, value("--projects")));
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
    } else if (std::string gs = value("--ghost-setup"); !gs.empty()) {
        app.pushScene(makeMainMenu(game));
        app.pushScene(makeGhostSetup(game, gs));
    } else if (has("--achievements")) {
        if (std::string a = value("--achievements"); !a.empty())
            game.onlineOpen = a == "list"          ? "ach-list"
                              : a == "suggestions" ? "ach-suggestions"  // 2.52: the Suggestions tab
                              : a.rfind("zoom:", 0) == 0 ? "ach-" + a
                                                         : "ach:" + a;
        app.pushScene(makeAchievements(game));
    } else if (has("--collection")) {
        // --collection [0-4][:detail] (tabs: Vestiaire, Route, Musiques, Galerie, Replays; details such as
        // col-cat:<0-5> (5: Badges du profil) or col-badge:<achievement id>)
        std::string c = value("--collection");
        int tab = c.empty() ? 0 : std::atoi(c.c_str());
        if (auto colon = c.find(':'); colon != std::string::npos) game.onlineOpen = c.substr(colon + 1);
        app.pushScene(makeMainMenu(game));
        app.pushScene(makeCollection(game, tab));
    } else if (has("--new-rewards")) {
        app.pushScene(makeMainMenu(game));
        app.pushScene(makeNewRewards(game));
    } else if (has("--admin")) {
        // --admin [players[:<search>[:confirm]] | calendar[:<YYYY-MM>[:<id>[:objectif]]] | log]:
        // ADMINISTRATION (an admin account's session).
        app.pushScene(makeMainMenu(game));
        app.pushScene(makeAdmin(game, value("--admin")));
    } else if (has("--stats")) {
        // --stats [<section>[:<what>]]: the player's statistics on a section (screenshots).
        std::string st = value("--stats");
        if (auto colon = st.find(':'); colon != std::string::npos) game.onlineOpen = "stats:" + st.substr(colon + 1);
        app.pushScene(makeMainMenu(game));
        app.pushScene(makeStatistics(game, st.empty() ? 0 : std::atoi(st.c_str())));
    } else if (std::string st = value("--story"); !st.empty()) {
        // --story <boss>[:<shot>|:titre] (screenshots): the story over the main menu.
        std::string boss = st.substr(0, st.find_first_of(":@"));
        int at = -1;
        float seconds = -1.0f;
        if (size_t c = st.find('@'); c != std::string::npos) seconds = std::max(0.0f, static_cast<float>(std::atof(st.c_str() + c + 1)));
        else if (size_t c = st.find(':'); c != std::string::npos) {
            std::string w = st.substr(c + 1);
            at = w == "titre" || w == "title" ? 1000 : std::max(0, std::atoi(w.c_str()) - 1);
            if (const BossStory* s = game.stories.find(boss); s && at > static_cast<int>(s->shots.size())) at = static_cast<int>(s->shots.size());
        }
        app.pushScene(makeMainMenu(game));
        if (auto story = makeBossStory(game, boss, at, seconds)) app.pushScene(std::move(story));
        else Log::error("Pas d'histoire pour le boss '{}'", boss);
    } else if (has("--asterion-film")) {
        // --asterion-film [<seconds>] (screenshots): Asterion's film over the main menu, locked.
        const std::string at = value("--asterion-film");
        app.pushScene(makeMainMenu(game));
        if (auto film = asterionfilm::make(game, true, at.empty() ? -1.0f : std::max(0.0f, static_cast<float>(std::atof(at.c_str())))))
            app.pushScene(std::move(film));
        else Log::error("Film d'Asterion illisible");
    } else if (has("--boss-stats")) {
        // --boss-stats [<boss id>[:<clip>]]: that boss selected (and that clip of its animation).
        if (std::string b = value("--boss-stats"); !b.empty()) game.onlineOpen = "boss:" + b;
        app.pushScene(makeBossStats(game));
    } else if (has("--challenges")) {
        // --challenges [day | week | month | calendar[:<YYYY-MM> | :<challenge id>]] (2.32's
        // « today » and « archives[:<id>] » still work: the day, the calendar).
        std::string c = value("--challenges");
        auto colon = c.find(':');
        std::string what = c.substr(0, colon), arg = colon == std::string::npos ? std::string{} : c.substr(colon + 1);
        int tab = what == "week" ? 1 : what == "month" ? 2 : (what == "calendar" || what == "archives") ? 3 : 0;
        app.pushScene(makeChallenges(game, tab, tab == 3 ? arg : std::string{}));
    } else if (std::string cs = value("--challenge-start"); !cs.empty()) {
        // --challenge-start <challenge id>[:practice]: the screen before a challenge run (over
        // its kind's tab, or the calendar on its card once it is over).
        auto colon = cs.find(':');
        bool practice = colon != std::string::npos && cs.substr(colon + 1) == "practice";
        app.pushScene(makeChallengesOn(game, cs.substr(0, colon)));
        app.pushScene(makeChallengeStart(game, cs.substr(0, colon), practice));
    } else if (std::string cp = value("--challenge-play"); !cp.empty()) {
        // --challenge-play <challenge id>[:practice]: a challenge run straight away (with --input).
        auto colon = cp.find(':');
        auto run = makeChallengeRun(game, cp.substr(0, colon), colon != std::string::npos && cp.substr(colon + 1) == "practice");
        if (run) app.pushScene(std::move(run));
        else app.pushScene(makeChallenges(game));
    } else if (std::string bt = value("--boss-training"); !bt.empty()) {
        // The settings of a boss training; --boss-training-go: enters its arena once it is found;
        // --boss-training-spawn: the « Lieu d'apparition » chosen (and its map opened).
        if (has("--boss-training-go")) game.onlineOpen = "train-go";
        if (std::string sp = value("--boss-training-spawn"); !sp.empty()) {
            if (game.onlineOpen.empty()) game.onlineOpen = "train";
            game.onlineOpen += ";spawn=" + sp;
        }
        app.pushScene(makeBossTraining(game, bt));
    } else if (std::string race = value("--race"); !race.empty()) {
        auto c1 = race.find(':');
        std::string kind = race.substr(0, c1), rest = c1 == std::string::npos ? std::string{} : race.substr(c1 + 1);
        auto c2 = rest.find(':');
        std::string lid = rest.substr(0, c2);
        int ver = c2 == std::string::npos ? 0 : std::atoi(rest.c_str() + c2 + 1);
        if (kind == "community") app.pushScene(makeOnlineLevels(game, lid));
        else app.pushScene(makeOnlineRanking(game, lid));
        app.pushScene(makeRaceBoard(game, kind, lid, ver));
    } else if (std::string pid = value("--profile"); !pid.empty()) {
        app.pushScene(makePlayerProfile(game, pid));
    } else if (has("--social")) {
        app.pushScene(makeSocial(game, value("--social")));
    } else if (has("--clans")) {
        app.pushScene(makeClans(game, value("--clans")));
    } else if (has("--online")) {
        app.pushScene(makeOnlineRanking(game, value("--online")));
    } else if (std::string cf = value("--compare"); !cf.empty()) {
        // Two replay files side by side (automated screenshots and checks).
        if (std::string v = value("--replay-view"); !v.empty()) game.onlineOpen = "rview:" + v;
        ReplayChoice ab[2];
        size_t comma = cf.find(',');
        std::string files[2] = {cf.substr(0, comma), comma == std::string::npos ? std::string{} : cf.substr(comma + 1)};
        for (int i = 0; i < 2; ++i) {
            std::string err;
            if (auto bytes = FileUtil::readBinary(Paths::fromUtf8(files[i]))) {
                if (auto rep = ReplayData::decode(*bytes, &err)) {
                    ab[i].meta.source = "file";
                    ab[i].meta.label = Paths::toUtf8(Paths::fromUtf8(files[i]).stem());
                    ab[i].meta.levelId = rep->levelId;
                    ab[i].meta.levelHash = rep->levelHash;
                    ab[i].meta.timeMs = rep->durationMs;
                    ab[i].replay = std::make_shared<const ReplayData>(std::move(*rep));
                }
            }
            if (!ab[i].valid()) Log::error("Replay {} : {}", files[i], err.empty() ? "lecture impossible" : err);
        }
        if (ab[0].valid() && ab[1].valid()) app.pushScene(makeReplayCompare(game, ab[0].replay->levelId, ab[0], ab[1]));
        else app.pushScene(makeSplashScene(game));
    } else if (std::string cs = value("--compare-setup"); !cs.empty()) {
        app.pushScene(makeMainMenu(game));
        ReplayCompareRequest req;
        req.levelId = cs;
        app.pushScene(makeReplayCompareSetup(game, std::move(req)));
    } else if (std::string rf = value("--replay"); !rf.empty()) {
        if (std::string v = value("--replay-view"); !v.empty()) game.onlineOpen = "rview:" + v;
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

    // « Retour » from a screen that replaced the main menu never closes the game.
    app.setRootScene([&game] { return makeMainMenu(game); });
    app.run();
    game.playStats.flush();  // 2.54: the statistics written by their worker, before the game closes
    game.progression.save();
    game.records.save();
    game.presenceOffline();  // friends see « hors ligne » at once (else after 150 s)
    game.online.shutdown();
    app.shutdown();
    Log::info("Bye");
    Log::shutdown();
    return 0;
}
