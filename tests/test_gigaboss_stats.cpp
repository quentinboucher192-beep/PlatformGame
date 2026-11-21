// 2.77.3 (docs/RAIDS.md §24): the statistics of the seven gigaboss of Asterion and of its heart — on their raid
// sheets (CLANS › RAIDS › Fiches, tab « Combat ») and in STATISTIQUES › Boss (« Raid Asterion ») — compared with
// their data, number by number (their definition, their lair's rules, their parts, the titan's links); their
// layout measured at 1280 × 720 and 1920 × 1080, in French and in English; and « Simuler un raid complet » with
// « Joueurs par groupe : N » (every group N players: the administrator's N − 1 simulated players and them).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "engine/core/Scene.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/HitParts.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidSheets.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"
#include "tests/TestFramework.h"
#include "ui/screens/RaidSheetsScene.h"
#include "ui/widgets/GigabossStatsView.h"
#include "ui/widgets/HeartSheetView.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

struct Blank : Scene {
    void render(float) override {}
};

/// The rules its fight reads (its lair's metadata.raid.gigaboss).
const raid::GigabossRules* rulesOf(const raid::GigabossSheet& g) {
    const raid::Branch* b = raid::Branch::builtin(g.part);
    const raid::ZoneRules* z = b ? b->zoneRules(g.lairZone) : nullptr;
    return z && z->gigaboss.present ? &z->gigaboss : nullptr;
}

/// Its definition read again from its file (not through the content database).
Json enemyFile(const std::string& id) {
    auto j = FileUtil::loadJson(Paths::dataDir() / "enemies" / (id + ".json"));
    return j ? *j : Json::object();
}

}  // namespace

TEST_CASE("2.77.3 gigaboss statistics: every number of each sheet is its data's (health by the group, phases, blows, parts, speed, size)") {
    Svc s;
    AchievementCatalog achievements;
    achievements.loadDefault();
    CosmeticCatalog cosmetics;
    cosmetics.loadDefault();
    const raid::RaidSheets sheets = raid::loadRaidSheets(s.content, achievements, cosmetics);
    for (const std::string& p : sheets.problems) std::printf("  problème : %s\n", p.c_str());
    CHECK(sheets.problems.empty());
    CHECK(sheets.gigaboss.size() == 7);
    CHECK(sheets.heart.def != nullptr);
    std::vector<const raid::GigabossSheet*> all;
    for (const raid::GigabossSheet& g : sheets.gigaboss) all.push_back(&g);
    all.push_back(&sheets.heart);
    for (const raid::GigabossSheet* gp : all) {
        const raid::GigabossSheet& g = *gp;
        CHECK(g.def != nullptr);
        if (!g.def) continue;
        const raid::GigabossRules* r = rulesOf(g);
        CHECK(r != nullptr);
        if (!r) continue;
        const raid::SheetStats& st = g.stats;
        const Json file = enemyFile(g.enemy);
        const Json boss = file.value("boss", Json::object());
        std::printf("  %s : %s PV (1 joueur), %s à 8 joueurs, %zu phases, %zu coups, %zu parties\n", g.id.c_str(), gbstats::thousands(static_cast<int>(st.hp)).c_str(),
                    gbstats::thousands(st.health.empty() ? 0 : st.health.back()).c_str(), st.phases.size(), st.blows.size(), st.parts.size());
        // Its health: its file's « hp » × the lair's scaling (Gigaboss::healthFactor), 2 to 8 players.
        CHECK(st.hp == jget<float>(file, "hp", -1.0f) * r->hpScale);  // titan 13: its raid's base (the lair's « hpScale »)
        CHECK(st.hpPerPlayer == r->hpPerPlayer);
        CHECK(st.health.size() == 7);
        for (int n = 2; n <= 8; ++n)
            CHECK(st.health[static_cast<size_t>(n - 2)] == static_cast<int>(std::lround(g.def->hp * raid::Gigaboss::healthFactor(*r, n))));
        for (size_t i = 1; i < st.health.size(); ++i) CHECK(st.health[i] > st.health[i - 1]);
        // Its phases: their thresholds in its file, their health then.
        const Json phases = boss.value("phases", Json::array());
        CHECK(st.phases.size() == phases.size());
        for (size_t i = 0; i < st.phases.size() && i < phases.size(); ++i) {
            const raid::SheetStats::Phase& p = st.phases[i];
            CHECK(p.name == jstr(phases[i], "name"));
            if (i > 0) CHECK(std::abs(p.from - jget<float>(phases[i], "hpThreshold", -1.0f)) < 1e-6f);
            CHECK(p.health.size() == 7);
            for (size_t k = 0; k < p.health.size(); ++k) CHECK(p.health[k] == static_cast<int>(std::lround(static_cast<float>(st.health[k]) * p.from)));
            CHECK(!p.states.empty());  // its diagram's states (« Combat », « Blindé », « À nu »…)
        }
        // Its contact and each attack of its file, then each blow of its lair's rules.
        CHECK(st.contact == jget<float>(file, "contactDamage", -1.0f));
        std::set<std::string> names;
        for (const Json& a : file.value("attacks", Json::array())) {
            if (!names.insert(jstr(a, "name")).second) continue;
            bool found = false;
            for (const raid::SheetStats::Blow& b : st.blows)
                if (!b.mechanism && b.name == jstr(a, "name")) found = std::abs(b.damage - jget<float>(a, "damage", 1.0f)) < 1e-6f;
            if (!found) std::printf("  %s : attaque « %s » absente\n", g.id.c_str(), jstr(a, "name").c_str());
            CHECK(found);
        }
        int ruleBlows = 0;
        for (auto it = r->extra.begin(); it != r->extra.end(); ++it) {
            const Json& b = it.value();
            if (!b.is_object() || !b.contains("damage")) continue;
            if (it.key() == "fists" && r->fistPeriods.empty()) continue;
            if (it.key() == "jet" && r->vents.empty()) continue;
            ++ruleBlows;
            bool found = false;
            for (const raid::SheetStats::Blow& sb : st.blows) found = found || (sb.mechanism && std::abs(sb.damage - b["damage"].get<float>()) < 1e-6f);
            CHECK(found);
        }
        int mech = 0;
        for (const raid::SheetStats::Blow& b : st.blows) mech += b.mechanism ? 1 : 0;
        CHECK(mech == ruleBlows);
        CHECK(ruleBlows > 0);
        // Its parts: the roles and multipliers of its file.
        const Json roles = file.value("parts", Json::object()).value("roles", Json::object());
        CHECK(st.parts.size() == roles.size());
        for (const PartRule& pr : st.parts) {
            CHECK(roles.contains(pr.name));
            if (!roles.contains(pr.name)) continue;
            const std::string role = jstr(roles[pr.name], "role", "normal");
            CHECK(std::string(partRoleId(pr.role)) == role);
            if (role != "hurt") CHECK(std::abs(pr.damage - jget<float>(roles[pr.name], "damage", 1.0f)) < 1e-6f);
        }
        CHECK(st.byParts == (jget<int>(file, "partsFromVersion", 0) > 0));
        // Its rhythm, its bare windows, its speed, its size, its time limit.
        CHECK(st.exposed == r->exposed && st.exposedMult == r->exposedMult && st.weakShare == r->weakShare);
        CHECK(st.pressurePerPlayer == r->pressurePerPlayer && st.weakHpPerPlayer == r->weakHpPerPlayer);
        CHECK(st.speed == jget<float>(file, "speed", -1.0f));
        CHECK(st.chase == jget<float>(file, "chaseSpeedMultiplier", 1.6f));
        const Json size = file.value("size", Json::array());
        // As the engine reads it (EnemyDefinition: 2 to 400 px; Titanus's 420 is 400 in the game).
        CHECK(size.size() == 2 && st.size.x == std::clamp(size[0].get<float>(), 2.0f, 400.0f) && st.size.y == std::clamp(size[1].get<float>(), 2.0f, 400.0f));
        CHECK(st.scale == jget<float>(file, "scale", 1.0f));
        CHECK(st.enrage == jget<float>(boss, "enrageTime", 0.0f));
    }
    // The heart: its relics (each −relicShare of its health, at least half kept), its links « modifier ».
    const raid::SheetStats& h = sheets.heart.stats;
    const raid::GigabossRules* hr = rulesOf(sheets.heart);
    CHECK(hr != nullptr);
    if (hr) CHECK(std::abs(h.relicShare - hr->extra.value("heart", Json::object()).value("relicShare", -1.0f)) < 1e-6f);
    CHECK(h.relicShare > 0.0f);
    for (int n = 2; n <= 8; ++n)
        for (int relics = 0; relics <= 7; ++relics)
            CHECK(raid::sheetHealth(h, n, relics) ==
                  static_cast<int>(std::lround(h.hp * (1.0f + h.hpPerPlayer * static_cast<float>(n - 1)) * std::max(0.5f, 1.0f - h.relicShare * static_cast<float>(relics)))));
    raid::SheetStats many = h;
    many.relicShare = 0.1f;
    CHECK(raid::sheetHealth(many, 8, 7) == static_cast<int>(std::lround(many.hp * (1.0f + many.hpPerPlayer * 7.0f) * 0.5f)));  // its floor
    CHECK(sheets.heartModifiers.size() == 7);
    std::set<std::string> parts;
    const raid::Titan* titan = raid::Titan::builtin();
    CHECK(titan != nullptr);
    for (const raid::HeartModifier& m : sheets.heartModifiers) {
        parts.insert(m.part);
        CHECK(!m.partName.empty() && !m.boss.empty());
        bool found = false;
        if (titan)
            for (const Json& l : titan->links)
                if (jstr(l, "id") == m.id) found = jstr(l, "kind") == "modifier" && jstr(l, "label") == m.label && jstr(l, "to") == titan->heart.id;
        CHECK(found);
    }
    CHECK(parts.size() == 7);
    CHECK(sheets.find("coeur") == &sheets.heart);
    CHECK(sheets.ofEnemy("asterion_coeur") == &sheets.heart);
    CHECK(sheets.ofEnemy("titanus") && sheets.ofEnemy("titanus")->name == "Titanus");
    CHECK(sheets.asterion.heartSheet == "coeur");  // Asterion's sheet opens it
    // The real state of the clan, read on a titan's map of an event running.
    Json map = Json::object();
    map["event"] = Json{{"state", "en_cours"}};
    Json mods = Json::array();
    for (size_t k = 0; k < sheets.heartModifiers.size(); ++k) mods.push_back(Json{{"id", sheets.heartModifiers[k].id}, {"on", k % 2 == 0}});
    map["heart"] = Json{{"relics", 3}, {"modifiers", mods}};
    bool ok = false;
    const heartsheet::State real = heartsheet::realState(sheets, map, &ok);
    CHECK(ok && real.real && real.relicCount() == 3);
    for (int k = 0; k < 7; ++k) CHECK(real.down(k) == (k % 2 == 0));
    map["event"] = Json{{"state", "termine"}};
    heartsheet::realState(sheets, map, &ok);
    CHECK(!ok);  // after the event: a simulation
}

TEST_CASE("2.77.3 layout: the statistics and the heart's three columns measured, nothing over anything (1280 × 720, 1920 × 1080, FR / EN)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.pushScene(std::make_unique<Blank>());
        const raid::RaidSheets sheets = raid::loadRaidSheets(g.content, g.achievementCatalog, g.cosmetics);
        heartsheet::State sim;
        sim.fallen = 0b1011011;  // five parts fallen
        sim.relics = 0b1001001;  // three relics
        heartsheet::State real = sim;
        real.real = real.realAvailable = true;
        real.realRelics = 3;
        for (const Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const float W = ui.screenW(), H = ui.screenH();
                const Rect screen{0.0f, 0.0f, W, H};
                auto checkItems = [&](const gbstats::Layout& S, const Rect& col, const char* what) {
                    float prev = col.y;
                    for (const gbstats::Item& it : S.items) {
                        const bool inside = it.r.x >= col.x - 0.5f && it.r.right() <= col.right() + 0.5f && it.r.y >= prev - 0.5f;
                        if (!inside) std::printf("  %s : « %s » hors de sa colonne en %.0f x %.0f\n", what, it.label.c_str(), size.x, size.y);
                        CHECK(inside);
                        prev = it.r.bottom();
                        if (it.kind == gbstats::Item::Kind::Row || it.kind == gbstats::Item::Kind::Bar) {
                            CHECK(it.r.containsRect(it.valueR));
                            CHECK(!it.labelR.intersects(it.valueR));
                            // The value whole (fitted, never shortened); the label inside its room.
                            const bool whole = ui.text().measure(it.value, FontFace::Bold, ui.s(it.valueSize)).x <= it.valueR.w + 0.5f;
                            if (!whole) std::printf("  %s : « %s » coupé en %.0f x %.0f\n", what, it.value.c_str(), size.x, size.y);
                            CHECK(whole);
                            CHECK(ui.text().measure(ui.elide(it.label, FontFace::Body, it.labelSize, it.labelR.w), FontFace::Body, ui.s(it.labelSize)).x <=
                                  it.labelR.w + 0.5f);
                            if (it.kind == gbstats::Item::Kind::Bar) {
                                CHECK(it.r.containsRect(it.labelR));
                                if (it.barR.w > 0.0f) {
                                    CHECK(!it.barR.intersects(it.labelR) && !it.barR.intersects(it.valueR));
                                    CHECK(it.r.containsRect(it.barR));
                                }
                                if (it.extraR.w > 0.0f) {
                                    CHECK(it.r.containsRect(it.extraR) && !it.extraR.intersects(it.valueR));
                                    CHECK(ui.text().measure(it.extra, FontFace::Body, ui.s(it.size)).x <= it.extraR.w + 0.5f);
                                }
                            }
                            CHECK(!it.tip.empty());  // every number explained
                        }
                        if (it.kind == gbstats::Item::Kind::Note) {
                            i18n::Verbatim shown(true);
                            for (const std::string& l : it.lines) CHECK(ui.text().measure(l, FontFace::Body, ui.s(it.size)).x <= it.r.w + 0.5f);
                        }
                        if (it.kind == gbstats::Item::Kind::Chips)
                            for (size_t k = 0; k < it.chips.size(); ++k) {
                                CHECK(it.r.containsRect(it.chips[k]));
                                for (size_t j = k + 1; j < it.chips.size(); ++j) CHECK(!it.chips[k].intersects(it.chips[j]));
                            }
                    }
                };
                // Each gigaboss's tab « Combat »: its statistics in the sheet's content, as the scene lays them.
                for (const raid::GigabossSheet& gs : sheets.gigaboss) {
                    const RaidSheetsLayout L = layoutRaidSheets(ui, sheets, gs.id);
                    const float pad = ui.s(14), w = L.content.w - ui.s(16);
                    const Rect col{L.content.x + pad, L.content.y, w - 2.0f * pad, 1e6f};
                    const gbstats::Layout S = gbstats::layoutStats(ui, gs.stats, col.x, col.y, col.w);
                    CHECK(S.height > ui.s(300));
                    checkItems(S, col, gs.id.c_str());
                    // A tooltip stays inside the screen, wherever its number is.
                    for (const gbstats::Item& it : S.items) {
                        if (it.tip.empty()) continue;
                        gbstats::Tip tip{it.tipTitle, it.tip, {it.r.x, std::min(it.r.y, H - ui.s(30)), it.r.w, it.r.h}, true};
                        CHECK(screen.containsRect(gbstats::tipBox(ui, tip)));
                    }
                }
                // STATISTIQUES › Boss › « Fiche »: the left column (40 % of its detail).
                const gbstats::Layout narrow = gbstats::layoutStats(ui, sheets.gigaboss.front().stats, 0.0f, 0.0f, std::max(ui.s(300), W * 0.22f));
                checkItems(narrow, {0.0f, 0.0f, std::max(ui.s(300), W * 0.22f), 1e6f}, "statistiques (boss)");
                // The heart's « Combat »: its three columns, real state and simulation.
                const RaidSheetsLayout HL = layoutRaidSheets(ui, sheets, "coeur");
                CHECK(HL.tabLabels.size() == 3 && HL.tabLabels[1] == "Où frapper");
                for (const heartsheet::State* st : {&sim, &real}) {
                    const Rect area{HL.content.x, HL.content.y, HL.content.w - ui.s(16), 0.0f};
                    const heartsheet::Layout L = heartsheet::layoutHeart(ui, sheets, *st, area);
                    CHECK(!L.left.intersects(L.middle) && !L.middle.intersects(L.right) && !L.left.intersects(L.right));
                    for (const Rect& c : {L.left, L.middle, L.right}) CHECK(c.x >= area.x - 0.5f && c.right() <= area.right() + 0.5f);
                    CHECK(L.left.containsRect(L.leftTitle) && L.left.containsRect(L.modeR) && L.left.containsRect(L.figure) && L.left.containsRect(L.totals));
                    CHECK(L.left.containsRect(L.honestR));
                    CHECK(L.modeR.bottom() <= L.figure.y + 0.5f && L.figure.bottom() <= L.parts.front().row.y + 0.5f);
                    CHECK(L.parts.size() == 7);
                    for (size_t k = 0; k < L.parts.size(); ++k) {
                        const heartsheet::Part& p = L.parts[k];
                        CHECK(L.left.containsRect(p.row));
                        CHECK(!p.fallBtn.intersects(p.relicBtn) && !p.name_.intersects(p.fallBtn));
                        if (k + 1 < L.parts.size()) CHECK(p.row.bottom() <= L.parts[k + 1].row.y + 0.5f);
                    }
                    CHECK(L.parts.back().row.bottom() <= L.totals.y + 0.5f && L.totals.bottom() <= L.honestR.y + 0.5f);
                    CHECK(ui.text().measure(L.totalsText, FontFace::Bold, ui.s(16) * 0.75f).x <= L.totals.w + 0.5f);
                    if (st->realAvailable) CHECK(L.left.containsRect(L.resetBtn) && L.honestR.bottom() <= L.resetBtn.y + 0.5f);
                    checkItems(L.stats, {L.middle.x, L.middle.y, L.middle.w, L.middle.h}, "cœur");
                    CHECK(L.cards.size() == 7);
                    int weakened = 0;
                    for (size_t k = 0; k < L.cards.size(); ++k) {
                        const heartsheet::Card& c = L.cards[k];
                        weakened += c.weakened ? 1 : 0;
                        CHECK(L.right.containsRect(c.r) && c.r.containsRect(c.icon) && c.r.containsRect(c.tag));
                        CHECK(!c.lines.empty() && c.lines.size() == c.lineR.size());
                        i18n::Verbatim shown(true);
                        for (size_t i = 0; i < c.lines.size(); ++i) {
                            CHECK(!c.icon.intersects(c.lineR[i]));
                            CHECK(ui.text().measure(c.lines[i], FontFace::Body, ui.s(14)).x <= c.lineR[i].w + 0.5f);
                        }
                        if (k + 1 < L.cards.size()) CHECK(c.r.bottom() <= L.cards[k + 1].r.y + 0.5f);
                    }
                    CHECK(weakened == 5);
                    CHECK(L.right.containsRect(L.legendR));
                }
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}

TEST_CASE("2.77.3 « Simuler un raid complet », « Joueurs par groupe : 7 »: yours has you and 6 simulated players, every other group 7") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 3;
    setup.bots = 6;  // N − 1
    setup.fullGroups = true;
    setup.skill = raid::BotSkill::Expert;
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    CHECK(sim.groups().size() == 3);
    CHECK(sim.seated() && sim.viewed() == 0);
    auto count = [&](int g, bool admin, int bots) {
        const raid::TestSession& t = *sim.group(g).session;
        CHECK(t.players() == 7);
        CHECK(t.adminSeated() == admin);
        CHECK(t.simulatedPlayers() == bots);
        CHECK(t.simulatedPlayers() + (t.adminSeated() ? 1 : 0) == 7);
    };
    count(0, true, 6);
    count(1, false, 7);
    count(2, false, 7);
    for (int t = 0; t < 120; ++t) sim.step(InputFrame{});
    // The administrator's seat of a group they are not in: a simulated player in play, never a free place.
    for (int g = 1; g < 3; ++g) {
        const GameWorld& w = sim.group(g).session->run().world();
        CHECK(w.member(0).presence != GameWorld::Presence::Vacant);
        CHECK(!(sim.group(g).session->frames()[0].flags & InputFrame::Vacant));
    }
    // Joining another group: they take its simulated player's place; the groups keep their size.
    CHECK(sim.join(1));
    count(0, false, 7);
    count(1, true, 6);
    // The zone's test and the old setting: the administrator's place stays free (its own counting: 0 to 7 simulated players).
    raid::SimSetup old = setup;
    old.fullGroups = false;
    raid::RaidSimulation before(s.ws(), old);
    CHECK(before.begin());
    CHECK(before.group(1).session->simulatedPlayers() == 6);
    before.step(InputFrame{});
    CHECK(before.group(1).session->frames()[0].flags & InputFrame::Vacant);
}
