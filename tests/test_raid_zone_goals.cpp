// 2.75 (docs/RAIDS.md §19): what is left to do in a zone (game/raid/ZoneGoals), a zone finished told
// once to every player of the instance with its « Tableau des contributions » (raid::RaidRun, counted in
// the simulation: the same on every computer, 2 and 8 seats), the HUD's new panel measured into its
// place, and the gigaboss sheets' « Où frapper » diagram read from the fight's own rules
// (game/raid/BossHitboxes).

#include <algorithm>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/BossHitboxes.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidSheets.h"
#include "game/raid/RaidTest.h"
#include "game/raid/ZoneGoals.h"
#include "saves/RaidHudLayout.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidHud.h"
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

const raid::Branch& bras() {
    const raid::Branch* b = raid::Branch::builtin();
    CHECK(b != nullptr);
    return *b;
}

uint32_t bit(const std::string& id) {
    const int i = bras().objectiveIndex(id);
    CHECK(i >= 0);
    return i >= 0 ? 1u << i : 0u;
}

std::vector<InputFrame> soloFrames() {
    std::vector<InputFrame> f(raid::kMaxInstancePlayers);
    for (int k = 1; k < raid::kMaxInstancePlayers; ++k) f[static_cast<size_t>(k)].flags = InputFrame::Vacant;
    return f;
}

const raid::Requirement* findReq(const raid::ZoneGoals& g, const std::string& part) {
    for (const raid::Requirement& r : g.items)
        if (r.text.find(part) != std::string::npos) return &r;
    return nullptr;
}

InputFrame walker(uint32_t t, uint32_t& prev) {
    InputFrame f;
    const int dir = (t / 400) % 3 == 2 ? -1 : 1;
    f.held |= 1u << static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft);
    f.moveX = static_cast<int8_t>(dir * 127);
    if (t % 90 < 14) f.held |= 1u << static_cast<int>(Action::Jump);
    if (t % 20 == 0) f.held |= 1u << static_cast<int>(Action::Attack);
    f.pressed = f.held & ~prev;
    f.released = prev & ~f.held;
    prev = f.held;
    return f;
}

}  // namespace

TEST_CASE("raid zone goals (2.75): each zone's requirements, ticked from the validated state, pending when only this group did it") {
    const raid::Branch& b = bras();
    // Titan 9: the Biceps opens with the Épaule's guard (an ambush: the Épaule's ways on wait for it).
    raid::ZoneGoals z2 = raid::zoneGoals(b, 2, 0);
    CHECK(!z2.reachable);
    const raid::Requirement* gate = findReq(z2, "Ouvrir l'accès : Embuscades restantes : 1 sur 1");
    CHECK(gate != nullptr && gate->access);
    const raid::ZoneGoals z1 = raid::zoneGoals(b, 1, 0);
    CHECK(findReq(z1, "Embuscades restantes : 1 sur 1") != nullptr);
    CHECK(findReq(raid::zoneGoals(b, 1, bit("epaule_arene")), "Toutes les embuscades sont vaincues : les passages vers la suite sont ouverts") != nullptr);
    const uint32_t entry = bit("epaule_arene");
    // The Biceps: its guardian.
    z2 = raid::zoneGoals(b, 2, entry);
    CHECK(z2.reachable);
    CHECK(!z2.finished);
    CHECK(z2.items.size() == 1);
    CHECK(!z2.items.front().done);
    CHECK(!z2.missing().empty());
    z2 = raid::zoneGoals(b, 2, entry, bit("biceps_gardien"));  // this group only: done, waiting for the server
    CHECK(z2.finished);
    CHECK(z2.items.front().done);
    CHECK(z2.items.front().pending);
    z2 = raid::zoneGoals(b, 2, entry | bit("biceps_gardien"));
    CHECK(!z2.items.front().pending);
    CHECK(z2.missing().empty());
    // The Coude-forge: its pair (one per group), its optional anvil never required.
    raid::ZoneGoals z4 = raid::zoneGoals(b, 4, bit("forge_soufflet_ouest"));
    CHECK(!z4.finished);
    const raid::Requirement* east = findReq(z4, "est");
    CHECK(east != nullptr);
    if (east) CHECK(east->progress == "1 / 2");
    CHECK(z4.missing().find("Facultatif") == std::string::npos);
    CHECK(raid::zoneFinished(b, 4, bit("forge_soufflet_ouest") | bit("forge_soufflet_est")));
    // The Poignet-verrou: locked by the two generators (« 1 / 2 » once one is on), then its last ambush.
    raid::ZoneGoals z7 = raid::zoneGoals(b, 7, entry | bit("veine_generateur"));
    CHECK(!z7.reachable);
    CHECK(!z7.items.empty());
    CHECK(z7.items.front().access);
    CHECK(z7.items.front().progress == "1 / 2");
    CHECK(z7.missing().find("1 / 2") != std::string::npos);
    z7 = raid::zoneGoals(b, 7, entry | bit("veine_generateur") | bit("avantbras_generateur"));
    CHECK(z7.reachable);
    CHECK(!z7.items.front().access);
    // The generators say « k générateur(s) sur N ».
    const raid::ZoneGoals z3 = raid::zoneGoals(b, 3, bit("avantbras_generateur"));
    CHECK(findReq(z3, "1 générateur sur 2") != nullptr);
    // The secret zone: its relic only (optional), finished once taken.
    CHECK(raid::zoneGoals(b, 5, 0).optional);
    CHECK(!raid::zoneFinished(b, 5, 0));
    CHECK(raid::zoneFinished(b, 5, bit("grotte_relique")));
    // The branch: every zone, the lair last.
    const auto all = raid::branchGoals(b, 0);
    CHECK(all.size() == b.zones.size() + (b.hasLairArmory() ? 1 : 0));  // 2.78: and the lair's armory, a zone of passage
    CHECK(all.back().lair);
    CHECK(!b.hasLairArmory() || all[all.size() - 2].passage);
}

TEST_CASE("raid zone finished (2.75): told once at the step it happens, its table made and reported once, the others' zones told apart") {
    Svc s;
    auto frames = soloFrames();
    {
        // Finished by another group (the validated state at a step).
        raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
        CHECK(run.begin(0, 2));
        for (int k = 0; k < 10; ++k) run.step(frames.data(), static_cast<int>(frames.size()), 0);
        run.takeReports();
        CHECK(run.finishes().empty());
        int told = 0, tables = 0;
        for (int k = 0; k < 60; ++k) {
            run.step(frames.data(), static_cast<int>(frames.size()), bit("biceps_gardien"));
            told += run.zoneFinishedNow() == 2;
            for (const raid::Report& r : run.takeReports()) tables += r.kind == raid::Report::Kind::ZoneTable && r.zone == 2;
        }
        CHECK(told == 1);
        CHECK(tables == 1);
        CHECK(run.finishes().size() == 1);
        CHECK(!run.finishes().front().byUs);
        bool banner = false;
        for (const raid::Alert& a : run.alerts()) banner = banner || a.text.find("ZONE TERMINÉE") != std::string::npos;
        CHECK(banner);
        // Another zone of the branch finished elsewhere: an alert, no table here.
        run.step(frames.data(), static_cast<int>(frames.size()), bit("biceps_gardien") | bit("veine_generateur"));
        CHECK(run.finishes().size() == 1);
        bool elsewhere = false;
        for (const raid::Alert& a : run.alerts()) elsewhere = elsewhere || a.text.find("Zone terminée : Veine de magma") != std::string::npos;
        CHECK(elsewhere);
        CHECK((run.zonesFinished() >> 3) & 1u);
    }
    {
        // Already finished when the instance comes: no news.
        raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
        CHECK(run.begin(bit("biceps_gardien"), 2));
        for (int k = 0; k < 30; ++k) run.step(frames.data(), static_cast<int>(frames.size()), bit("biceps_gardien"));
        CHECK(run.finishes().empty());
    }
    {
        // Finished by this group: its guardian beaten.
        raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
        CHECK(run.begin(0, 2));
        run.step(frames.data(), static_cast<int>(frames.size()), 0);
        Enemy* guardian = nullptr;
        for (const auto& e : run.world().enemies())
            if (e->alive() && e->objectId() == "o27") guardian = e.get();
        CHECK(guardian != nullptr);
        if (guardian) guardian->kill(run.world());
        int told = 0;
        for (int k = 0; k < 120; ++k) {
            run.step(frames.data(), static_cast<int>(frames.size()), 0);
            told += run.zoneFinishedNow() == 2;
        }
        CHECK(told == 1);
        CHECK(run.finishes().size() == 1);
        if (!run.finishes().empty()) CHECK(run.finishes().front().byUs);
    }
}

TEST_CASE("raid zone tables (2.75): the same numbers on every computer (2 and 8 seats), boss damage within the damage") {
    Svc s;
    for (const int players : {2, 8}) {
        auto play = [&](std::string& canonical, int64_t& damage, int64_t& recorded, bool& bossWithin) {
            raid::RaidRun::Test setup;
            setup.on = true;
            setup.invincible = true;
            setup.players = players;
            raid::TestSession session(s.ws(), bras(), setup);
            CHECK(session.begin(1));
            uint32_t prev = 0;
            for (uint32_t t = 0; t < 1800 && session.run().zone() == 1; ++t) session.step(walker(t, prev));
            const auto rows = session.run().contributions().zoneTable(1);
            canonical = raid::Contributions::zoneCanonical(rows);
            damage = 0;
            bossWithin = true;
            for (const raid::ZoneContribution& z : rows) {
                damage += z.damage;
                bossWithin = bossWithin && z.bossDamage <= z.damage;
            }
            recorded = 0;
            for (const raid::Contribution& c : session.run().contributions().records()) recorded += c.damage;
        };
        std::string a, b;
        int64_t da = 0, db = 0, ra = 0, rb = 0;
        bool wa = false, wb = false;
        play(a, da, ra, wa);
        play(b, db, rb, wb);
        CHECK(a == b);  // what every computer of the instance reports
        CHECK(da == db);
        CHECK(da == ra);  // one zone played: its table holds all the damage the records count
        CHECK(wa && wb);
    }
}

TEST_CASE("raid HUD (2.75): « Ce qu'il reste à faire » has its own place, inside the screen, over no other element; the zone table's headers fit") {
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
        CHECK(std::string(RaidHudLayout::id(RaidHudElement::Remaining)) == "remaining");
        CHECK(RaidHudLayout{}[RaidHudElement::Remaining].visible);
        for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{1366, 768}, Vec2{2560, 1440}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(app, 1.0f / 60.0f);
            const RaidHudLayout l;
            const raidhud::HudAreas hud{};
            const Rect rem = raidhud::area(ui, l, RaidHudElement::Remaining, hud);
            CHECK(rem.w > ui.s(100));
            CHECK(rem.h >= ui.s(120));
            CHECK(rem.x >= 0.0f);
            CHECK(rem.y >= 0.0f);
            CHECK(rem.right() <= ui.screenW() + 0.5f);
            CHECK(rem.bottom() <= ui.screenH() + 0.5f);
            for (int i = 0; i < kRaidHudElements; ++i) {
                const auto e = static_cast<RaidHudElement>(i);
                if (e == RaidHudElement::Remaining || RaidHudLayout::hudElement(e) != HudElement::Count) continue;
                const Rect o = raidhud::area(ui, l, e, hud);
                if (o.w <= 0.0f) continue;
                const bool clear = !rem.intersects(o);
                if (!clear) std::printf("  « Ce qu'il reste à faire » touche « %s » en %.0f x %.0f\n", RaidHudLayout::label(e), size.x, size.y);
                CHECK(clear);
            }
            // The zone table's columns: within its width, each header fits its column (at 75 % at worst).
            raidhud::ZoneTable t;
            t.rows.push_back({"Joueur", Color::white(), false, 12, 40, 1, 2, 1});
            const float width = std::min(ui.s(760), ui.screenW() - ui.s(40)) - ui.s(48);
            const raidhud::ZoneColumns c = raidhud::zoneColumns(t, width);
            CHECK(c.x[5] + c.w[5] <= width + 0.5f);
            for (int i = 0; i < 6; ++i) {
                const float w = ui.text().measure(c.header[i], FontFace::Bold, ui.s(12) * 0.75f).x;
                if (w > c.w[i] - ui.s(6)) std::printf("  en-tête « %s » trop large (%.0f > %.0f)\n", c.header[i].c_str(), w, c.w[i] - ui.s(6));
                CHECK(w <= c.w[i] - ui.s(6) + 0.5f);
            }
            ui.end();
        }
    }
    app.shutdown();
}

TEST_CASE("raid sheets (2.75): every gigaboss has its « Où frapper » diagram per phase, its rectangles those of its fight's rules") {
    ContentDatabase content;
    content.reload();
    AchievementCatalog achievements;
    achievements.loadDefault();
    CosmeticCatalog cosmetics;
    cosmetics.loadDefault();
    const raid::RaidSheets sheets = raid::loadRaidSheets(content, achievements, cosmetics);
    CHECK(sheets.gigaboss.size() >= 7);
    for (const raid::GigabossSheet& g : sheets.gigaboss) {
        if (!g.def) continue;
        const raid::BossHitboxes& hb = g.hitboxes;
        CHECK(!hb.views.empty());
        CHECK(hb.body == Rect::fromBottomCenter({0.0f, 0.0f}, g.def->size));  // as Enemy places its box
        const int phases = std::max<int>(1, static_cast<int>(g.def->phases.size()));
        for (int p = 1; p <= phases; ++p) {
            bool seen = false;
            for (const raid::HitboxView& v : hb.views) seen = seen || v.phase == p;
            if (!seen) std::printf("  %s : pas de schéma pour la phase %d\n", g.name.c_str(), p);
            CHECK(seen);
        }
        const raid::Branch* b = nullptr;
        for (const raid::Branch* x : raid::Branch::all())
            if (x->part == g.part) b = x;
        if (!b) b = raid::Branch::builtin(g.part);
        const raid::ZoneRules* zr = b ? b->zoneRules(g.lairZone) : nullptr;
        CHECK(zr != nullptr);
        if (!zr) continue;
        const raid::GigabossRules& rules = zr->gigaboss;
        bool armour = false, exposed = false;
        for (const raid::HitboxView& v : hb.views) {
            CHECK(!v.hitWhen.empty());
            CHECK(!v.parts.empty());
            if (v.state == "armour") {
                armour = true;
                CHECK(v.phase >= rules.carapaceFrom - 1);  // Skullmaw's veil: its own range
                // 2.75.2: Heartbreaker's first phase: its plates on its body, no rib to hold in its arena.
                if (!rules.vents.empty() && v.stateName != "Blindé (plaques)") {
                    CHECK(v.plates.size() == rules.vents.size());
                    for (size_t k = 0; k < v.plates.size() && k < rules.vents.size(); ++k) CHECK(v.plates[k].rect == rules.vents[k]);
                }
                for (const raid::HitPart& p : v.parts)
                    if (p.label.rfind("Corps", 0) == 0 && p.kind != raid::HitPart::Kind::Hurt) CHECK(p.kind == raid::HitPart::Kind::Armour);
            }
            if (v.state == "exposed") {
                exposed = true;
                // Its weak points where Gigaboss sticks them (spotFeet), each its enemy's size.
                std::vector<Rect> weak;
                for (const raid::HitPart& p : v.parts)
                    if (p.label.rfind("Point faible", 0) == 0) weak.push_back(p.rect);
                CHECK(weak.size() == rules.weakSpots.size());
                for (size_t k = 0; k < weak.size() && k < rules.weakSpots.size(); ++k) {
                    const EnemyDefinition* d = content.enemy(rules.weakEnemyAt(k));
                    CHECK(d != nullptr);
                    if (d) CHECK(weak[k] == Rect::fromBottomCenter(raid::spotFeet(hb.body, 1.0f, rules.weakSpots[k]), d->size));
                }
            }
        }
        CHECK(armour == exposed);  // a mechanism: its armour and its bare state
        CHECK(armour);             // every gigaboss of Asterion has one
    }
}
