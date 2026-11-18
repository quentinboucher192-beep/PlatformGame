// 2.73: the sheets of Asterion and of its seven gigaboss (game/raid/RaidSheets, ui/screens/
// RaidSheetsScene): every gigaboss has its sheet, built from the game's real data (its definition,
// its lair's rules — the numbers its routine reads —, its branch, its part of the titan, its badge),
// with phases, attacks and mechanisms; the values of the sheets' words resolve in those rules; the
// sheets open for a player who never met them, from the raid's screens too; the fixed panels hold
// their texts at 1280 × 720 and 1920 × 1080, in French and in English.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossAttackDemo.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/AsterionBody.h"
#include "game/raid/GigabossTunables.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidSheets.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/RaidSheetsScene.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}
bool inside(const Rect& in, const Rect& out) {
    return in.x >= out.x - 0.5f && in.y >= out.y - 0.5f && in.right() <= out.right() + 0.5f && in.bottom() <= out.bottom() + 0.5f;
}

struct Catalogs {
    ContentDatabase content;
    AchievementCatalog achievements;
    CosmeticCatalog cosmetics;
    Catalogs() {
        content.reload();
        achievements.loadDefault();
        cosmetics.loadDefault();
    }
    raid::RaidSheets sheets() const { return raid::loadRaidSheets(content, achievements, cosmetics); }
};

/// The clips of an effects sheet (assets/animations/…json).
std::set<std::string> clipsOf(const std::string& fx) {
    std::set<std::string> out;
    if (auto j = FileUtil::loadJson(Paths::assetsDir() / fx)) {
        const Json clips = j->value("clips", Json());
        if (clips.is_object())
            for (auto it = clips.begin(); it != clips.end(); ++it) out.insert(it.key());
        else if (clips.is_array())
            for (const Json& c : clips) out.insert(jstr(c, "name"));
    }
    return out;
}

bool unresolved(const std::string& t) { return t.find('{') != std::string::npos || t.find('?') != std::string::npos; }

/// A headless game (no window shown), the sheets' screen pushed over its menu.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig(int w = 1280, int h = 720) {
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
        g->progression.asterionFilmSeen = true;  // its first viewing is its own test (test_asterion_film)
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~Rig() {
        app.shutdown();
        app.onSceneRender = nullptr;
    }
};

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

TEST_CASE("raid sheets: the values of their words, read in the rules the routines read") {
    const Json rules = Json::parse(R"({
        "gaze": {"track": 1.5, "periods": [8.0, 6.5, 5.0]},
        "fists": {"periods": [9.0, 7.0, 5.5]},
        "mirrors": [{}, {}, {}, {}],
        "sectors": {"edges": [576, 1376, 2176, 2976, 3776]},
        "weakShare": 0.04, "hpPerPlayer": 0.55,
        "swallow": {"periods": [0.0, 20.0, 15.0]},
        "carapace": [2, 3], "veil": [2, 4], "pin": {"from": 2}
    })");
    bool ok = false;
    CHECK(raid::sheetValue("gaze.track", rules, &ok) == "1,5" && ok);
    CHECK(raid::sheetValue("fists.periods", rules, &ok) == "9 / 7 / 5,5" && ok);
    CHECK(raid::sheetValue("count:mirrors", rules, &ok) == "4" && ok);
    CHECK(raid::sheetValue("gaps:sectors.edges", rules, &ok) == "4" && ok);
    CHECK(raid::sheetValue("pct:weakShare", rules, &ok) == "4" && ok);
    CHECK(raid::sheetValue("at2:swallow.periods", rules, &ok) == "20" && ok);
    CHECK(raid::sheetValue("at3:gaze.periods", rules, &ok) == "5" && ok);
    CHECK(raid::sheetValue("hpmul:8", rules, &ok) == "4,85" && ok);
    // The routines' own numbers (GigabossTunables.h), the same functions they call.
    CHECK(raid::sheetValue("slack:1", rules, &ok) == "2" && ok);
    CHECK(raid::sheetValue("slack:2", rules, &ok) == "1,35" && ok);
    CHECK(raid::sheetValue("plateslack:3", rules, &ok) == "1,2" && ok);
    CHECK(raid::sheetValue("code:pinTwoFrom", rules, &ok) == std::to_string(raid::tunables::kPinTwoFrom) && ok);
    CHECK(raid::sheetValue("code:gulpTwoFrom", rules, &ok) == std::to_string(raid::tunables::kGulpTwoFrom) && ok);
    // What does not resolve is never a number made up.
    CHECK(raid::sheetValue("gaze.nothing", rules, &ok) == "?" && !ok);
    CHECK(raid::sheetValue("code:nothing", rules, &ok) == "?" && !ok);
    std::vector<std::string> problems;
    const std::string t = raid::sheetText(Json::parse(R"({"text": "Il suit {0} s, puis {1} s.", "values": ["gaze.track", "gaze.lost"]})"), rules, &problems);
    CHECK(t == "Il suit 1,5 s, puis ? s.");
    CHECK(problems.size() == 1);
    // The phases of a mechanism, as the routines count them.
    auto phases = [&](const char* spec) { return raid::sheetPhases(Json::parse(spec), rules, 3, &ok); };
    CHECK(phases(R"("all")") == std::vector<int>({1, 2, 3}) && ok);
    CHECK(phases(R"("range:carapace")") == std::vector<int>({2}) && ok);
    CHECK(phases(R"("range:veil")") == std::vector<int>({2, 3}) && ok);
    CHECK(phases(R"("from:pin.from")") == std::vector<int>({2, 3}) && ok);
    CHECK(phases(R"("nonzero:swallow.periods")") == std::vector<int>({2, 3}) && ok);
    CHECK(phases("[1]") == std::vector<int>({1}) && ok);
    CHECK(phases(R"("range:nothing")").empty() && !ok);
    CHECK(raid::phasesLabel({1, 2, 3}, 3) == "Toutes les phases");
    CHECK(raid::phasesLabel({2, 3}, 3) == "Phases 2 et 3");
    CHECK(raid::phasesLabel({1}, 3) == "Phase 1");
    // The routines read the same numbers (no behaviour changed by naming them).
    CHECK(raid::tunables::slack(1) == 2.0f && raid::tunables::slack(2) == 1.35f && raid::tunables::slack(5) == 1.0f);
    CHECK(raid::tunables::plateSlack(2) == 1.5f && raid::tunables::plateSlack(4) == 1.2f && raid::tunables::plateSlack(8) == 1.0f);
}

TEST_CASE("raid sheets: every gigaboss has its sheet, from real data, with phases, attacks and mechanisms") {
    const Catalogs cat;
    const raid::RaidSheets s = cat.sheets();
    for (const std::string& p : s.problems) std::printf("  problème : %s\n", p.c_str());
    CHECK(s.problems.empty());
    // One sheet per gigaboss of the titan's body (data/raids/asterion.json), in its order.
    std::vector<asterion::Part> parts;
    if (auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion.json")) parts = asterion::parseParts(*j);
    int bosses = 0;
    for (const asterion::Part& p : parts) {
        if (p.boss.empty()) continue;
        ++bosses;
        const raid::GigabossSheet* g = s.ofPart(p.id);
        CHECK(g != nullptr);
        if (g) CHECK(g->name == p.boss);
    }
    CHECK(bosses == 7);
    CHECK(s.gigaboss.size() == 7);
    for (size_t i = 0; i + 1 < s.gigaboss.size(); ++i) {
        size_t a = 0, b = 0;
        for (size_t k = 0; k < parts.size(); ++k) {
            if (parts[k].id == s.gigaboss[i].part) a = k;
            if (parts[k].id == s.gigaboss[i + 1].part) b = k;
        }
        CHECK(a < b);
    }
    for (const raid::GigabossSheet& g : s.gigaboss) {
        std::printf("  %s : %zu phases, %zu mécanismes, difficulté %d / 10, antre « %s »\n", g.id.c_str(), g.phases.size(), g.mechanics.size(), g.stars,
                    g.lairName.c_str());
        CHECK(g.def != nullptr);
        if (!g.def) continue;
        CHECK(!g.def->raid.empty());  // a gigaboss of the raids (never a campaign boss)
        CHECK(g.stars >= 1 && g.stars <= 10);
        CHECK(g.phases.size() == g.def->phases.size() && g.phases.size() >= 3);
        for (const raid::SheetPhase& p : g.phases) {
            CHECK(!p.name.empty() && !p.attacks.empty() && !p.mechanics.empty());
            CHECK(!p.from.empty());
        }
        CHECK(!g.def->attacks.empty() && !g.lastStand.empty());
        // Every attack type it uses says how to avoid it.
        for (const AttackDef& a : g.def->attacks) {
            const std::string how = s.avoidOf(attackTypeId(a.type));
            if (how.empty()) std::printf("    sans « comment l'éviter » : %s (%s)\n", a.id.c_str(), attackTypeId(a.type));
            CHECK(!how.empty());
        }
        // Its mechanisms: named, explained, how to avoid them, in phases it has; one at least the
        // group must do together; every number filled from its rules.
        int collective = 0;
        const std::set<std::string> clips = clipsOf(g.fx);
        CHECK(!clips.empty());
        for (const raid::SheetMechanic& m : g.mechanics) {
            CHECK(!m.name.empty() && !m.text.empty() && !m.hint.empty() && !m.summary.empty());
            CHECK(!unresolved(m.text) && !unresolved(m.hint) && !unresolved(m.summary) && !unresolved(m.players));
            for (const raid::SheetFact& f : m.facts) CHECK(!unresolved(f.value) && !f.label.empty());
            CHECK(!m.phases.empty());
            for (int p : m.phases) CHECK(p >= 1 && p <= static_cast<int>(g.phases.size()));
            collective += m.kind == "collective" ? 1 : 0;
            // Its animated example, with the clips of its own effects sheet.
            CHECK(!m.sketch.kind.empty());
            for (const std::string& c : {m.sketch.clip, m.sketch.clipHeld, m.sketch.clipDone})
                if (!c.empty()) {
                    if (!clips.count(c)) std::printf("    clip absent de %s : %s\n", g.fx.c_str(), c.c_str());
                    CHECK(clips.count(c) == 1);
                }
        }
        CHECK(collective >= 1);
        // Its weak points (enemies of the game), where it lives, what its fall does, its badge.
        CHECK(!g.weak.empty());
        for (const raid::SheetWeakPoint& w : g.weak) CHECK(w.count >= 1 && !w.name.empty() && cat.content.enemy(w.enemy) != nullptr);
        CHECK(g.exposed > 0.0f && g.exposedMult >= 1.0f);
        CHECK(g.lairZone == 8 && !g.lairName.empty() && !g.expedition.empty() && g.expeditionMinutes > 0.0f && g.zones == 8);
        CHECK(!g.objective.empty() && !g.effect.empty() && !g.fallText.empty());
        CHECK(!g.rewards.empty() && g.rewards.front().kind == "badge");
        CHECK(g.scaling.size() == 3 && g.contributions.size() >= 6);
        for (const std::string& l : g.scaling) CHECK(!unresolved(l));
    }
    // The values are those of the lairs: a few read back from the rules themselves.
    const raid::GigabossSheet* skull = s.find("skullmaw");
    CHECK(skull != nullptr);
    if (skull) {
        const raid::ZoneRules* zr = raid::Branch::builtin("crane")->zoneRules(8);
        const Json& r = zr->gigaboss.extra;
        const std::string track = sheetNumber(r["gaze"]["track"].get<float>(), 2) + " s";
        bool found = false, need = false;
        for (const raid::SheetMechanic& m : skull->mechanics) {
            if (m.id == "regards") found = m.text.find(track) != std::string::npos && m.phases == std::vector<int>({1, 2, 3});
            if (m.id == "voile") need = m.byPlayers.size() >= 2 && m.phases == std::vector<int>({2, 3});
            if (m.id == "tenebres") CHECK(m.phases == std::vector<int>({skull->def->darkPhase}));
        }
        CHECK(found && need);
    }
    // Asterion: its seven parts and their gigaboss's sheets, its heart, the raid, its rewards.
    const raid::AsterionSheet& a = s.asterion;
    CHECK(a.parts.size() == 7);
    for (const raid::AsterionPart& p : a.parts) CHECK(!p.sheet.empty() && !p.effect.empty() && s.find(p.sheet) != nullptr);
    CHECK(a.heart.id == "coeur" && !a.heart.effect.empty() && !a.heartText.empty());
    // The heart's sheet comes by data once its fight exists: then it is a sheet like the others.
    if (!a.heartSheet.empty()) CHECK(s.find(a.heartSheet) && s.find(a.heartSheet)->def);
    CHECK(a.branches == 7 && a.zones == 56 && a.lairs == 7 && a.players == 8);
    CHECK(a.minMinutes > 0.0f && a.maxMinutes >= a.minMinutes);
    int badges = 0, titles = 0;
    for (const raid::SheetReward& r : a.rewards) {
        badges += r.kind == "badge" ? 1 : 0;
        titles += r.kind == "title" ? 1 : 0;
    }
    CHECK(badges == 7 && titles >= 3);
    CHECK(!a.lore.empty() && !a.contributions.empty());
}

TEST_CASE("raid sheets: open for a player who never met them, from the raid's screens too") {
    Rig r;
    GameServices& g = *r.g;
    const raid::RaidSheets s = raid::loadRaidSheets(g.content, g.achievementCatalog, g.cosmetics);
    for (const raid::GigabossSheet& b : s.gigaboss) {
        g.collection.met.erase(b.enemy);
        CHECK(g.bossStats.find(b.enemy) == nullptr || g.bossStats.find(b.enemy)->fights == 0);
    }
    std::vector<std::string> ids = {"asterion"};
    for (const raid::GigabossSheet& b : s.gigaboss) ids.push_back(b.id);
    for (const std::string& id : ids)
        for (int tab = 0; tab < 3; ++tab) {
            const size_t before = r.app.sceneCount();
            r.app.pushScene(makeRaidSheets(g, id, tab));
            r.app.runFrames(4);
            const RaidSheetsView v = raidSheetsView(r.app.topScene());
            CHECK(v.valid && v.sheet == id && v.tab == tab && !v.hidden);
            CHECK(v.blocks > 0);
            if (id != "asterion" && tab == 0) {
                // Every mechanism of its routine and every attack of its definition is listed (each unfolds).
                const raid::GigabossSheet* b = s.find(id);
                CHECK(v.attackRows == static_cast<int>(b->mechanics.size() + b->def->attacks.size()));
            }
            r.app.popScene();
            r.app.runFrames(2);
            CHECK(r.app.sceneCount() == before);
        }
    // From the sheet of an event (offline here: the sheets are the game's own data), « Fiches ».
    // (The game offline: its requests fail at once, never a real server in a test.)
    g.settings.online.enabled = false;
    g.online.setSettings(g.settings.online);
    r.app.pushScene(makeRaidEvent(g, "evenement_absent"));
    r.app.runFrames(4);
    g.ui.setFocus("raid/sheets");
    key(SDL_SCANCODE_RETURN, SDLK_RETURN);
    r.app.runFrames(4);
    const RaidSheetsView v = raidSheetsView(r.app.topScene());
    CHECK(v.valid && v.sheet == "asterion");
}

TEST_CASE("raid sheets: the fixed panels hold their texts at 1280 x 720 and 1920 x 1080, in French and in English") {
    Rig r;
    GameServices& g = *r.g;
    const raid::RaidSheets s = raid::loadRaidSheets(g.content, g.achievementCatalog, g.cosmetics);
    std::vector<std::string> ids = {"asterion"};
    for (const raid::GigabossSheet& b : s.gigaboss) ids.push_back(b.id);
    auto fits = [&](const RaidSheetText& t, const char* what, const std::string& id) {
        const float w = g.ui.text().measure(t.text, t.face, t.px).x;
        if (w > t.r.w + 0.5f) std::printf("  %s de %s : %.1f > %.1f (« %s »)\n", what, id.c_str(), w, t.r.w, t.text.c_str());
        CHECK(w <= t.r.w + 0.5f);
        CHECK(t.px >= 11.4f);
        CHECK(t.px <= t.r.h + 0.5f);
    };
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            r.app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            r.app.renderer().refreshOutputSize();
            i18n::setLanguage(lang);
            r.app.runFrames(1);
            g.ui.begin(r.app, 1.0f / 60.0f);
            for (const std::string& id : ids) {
                const RaidSheetsLayout L = layoutRaidSheets(g.ui, s, id);
                CHECK(inside(L.back, L.screen) && inside(L.title.r, L.screen) && inside(L.list, L.screen) && inside(L.panel, L.screen));
                CHECK(!overlaps(L.back, L.title.r) && !overlaps(L.list, L.panel) && !overlaps(L.title.r, L.panel));
                fits(L.title, "titre", id);
                CHECK(L.rows.size() == ids.size());
                for (const auto& row : L.rows) {
                    CHECK(inside(row.portrait, row.r) && inside(row.name.r, row.r) && inside(row.sub.r, row.r));
                    CHECK(!overlaps(row.name.r, row.sub.r) && !overlaps(row.portrait, row.name.r) && !overlaps(row.portrait, row.sub.r));
                    fits(row.name, "nom", id);
                    fits(row.sub, "ligne", id);
                }
                // The sheet's top: nothing overlaps, every text in its rect.
                std::vector<Rect> parts = {L.box, L.name.r, L.subtitle.r, L.tabs, L.content};
                if (!L.titan) parts.push_back(L.difficulty);
                else parts.push_back(L.caption.r);
                for (const RaidSheetText& c : L.chips) {
                    parts.push_back(c.r);
                    CHECK(g.ui.text().measure(c.text, c.face, c.px).x + g.ui.s(24) <= c.r.w + 0.5f);
                }
                for (size_t i = 0; i < parts.size(); ++i) {
                    CHECK(inside(parts[i], L.panel));
                    for (size_t j = i + 1; j < parts.size(); ++j) {
                        if (overlaps(parts[i], parts[j]))
                            std::printf("  %s (%gx%g) : éléments %zu et %zu se chevauchent\n", id.c_str(), size.x, size.y, i, j);
                        CHECK(!overlaps(parts[i], parts[j]));
                    }
                }
                if (!L.titan) CHECK(inside(L.caption.r, L.box));
                CHECK(L.content.h > g.ui.s(120));
                fits(L.name, "nom", id);
                if (!L.subtitle.text.empty()) fits(L.subtitle, "titre", id);
                if (L.titan) fits(L.caption, "légende", id);
                // The tabs' labels at the size the bar gives them all.
                const float tw = L.tabs.w / static_cast<float>(L.tabLabels.size());
                float common = 21.0f;
                for (const std::string& t : L.tabLabels) common = std::min(common, g.ui.fitFont(t, FontFace::Bold, 21.0f, tw - g.ui.s(14), 0.55f));
                for (const std::string& t : L.tabLabels) CHECK(g.ui.text().measure(t, FontFace::Bold, g.ui.s(common)).x <= tw - g.ui.s(14) + 0.5f);
            }
            // The screen itself at this size: every sheet and tab drawn.
            for (const std::string& id : ids) {
                r.app.pushScene(makeRaidSheets(g, id, 0));
                r.app.runFrames(2);
                CHECK(raidSheetsView(r.app.topScene()).blocks > 0);
                r.app.popScene();
                r.app.runFrames(1);
            }
        }
    i18n::setLanguage(Language::French);
    r.app.setWindowSize(1280, 720);
    r.app.renderer().refreshOutputSize();
}

TEST_CASE("raid sheets: a gigaboss's attack examples, played by the engine with its whole body in view") {
    // Its sheet plays each attack of its definition (a neutral arena: the raids' lairs are not
    // levels of the campaign), grown for a giant (2.73): the attack of its 2nd phase begins, the
    // gigaboss above the floor and under the arena's top.
    auto content = std::make_shared<ContentDatabase>();
    content->reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    AchievementCatalog achievements;
    achievements.loadDefault();
    CosmeticCatalog cosmetics;
    cosmetics.loadDefault();
    const raid::RaidSheets s = raid::loadRaidSheets(*content, achievements, cosmetics);
    int played = 0;
    for (const raid::GigabossSheet& g : s.gigaboss) {
        if (!g.def || g.def->phases.size() < 2 || g.def->phases[1].attacks.empty()) continue;
        const std::string attack = g.def->phases[1].attacks.front();
        BossAttackDemo demo;
        std::string error;
        const bool ok = demo.setup(content, res, *g.def, attack, std::nullopt, "", &error);
        if (!ok) std::printf("  %s / %s : %s\n", g.id.c_str(), attack.c_str(), error.c_str());
        CHECK(ok);
        if (!ok) continue;
        while (!demo.ready() && !demo.failed()) demo.warmUp(120);
        CHECK(demo.ready() && demo.shown() >= 1);
        CHECK(demo.world() && demo.world()->training() && demo.world()->invincible());
        if (const Enemy* b = demo.boss(); b && demo.world()) {
            const Rect arena = demo.world()->level().bounds;
            CHECK(arena.h >= g.def->size.y + 200.0f);
            CHECK(b->box.y >= arena.y && b->box.bottom() <= arena.bottom());
        }
        ++played;
    }
    CHECK(played == 7);
}
