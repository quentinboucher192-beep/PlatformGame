// 2.72: the clan raids as the administrators test them and as the groups play them (docs/RAIDS.md
// §15): the off-screen arrows of the solo levels in a raid world, the zones that keep what was done
// in them (in a test, its branch's state kept as RAID_STATE gives it; in a real instance too), the
// simulated players of a test (deterministic, 2 and 8 seats, never a report), the rising magma
// (drawn over the actors, rising, hurting the players and burning the enemies caught in it, more
// often), its generated sheets (the same bytes), the raids' HUD layout and the key of the large map.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/input/InputTypes.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/EnemyArrows.h"
#include "game/gameplay/EnemyArrowsDraw.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/PowerKeys.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidTest.h"
#include "saves/RaidHudLayout.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

#ifdef _WIN32
#define COSMIC_RV_NULL "NUL"
#else
#define COSMIC_RV_NULL "/dev/null"
#endif

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

std::vector<InputFrame> soloFrames() {
    std::vector<InputFrame> f(raid::kMaxInstancePlayers);
    for (int k = 1; k < raid::kMaxInstancePlayers; ++k) f[static_cast<size_t>(k)].flags = InputFrame::Vacant;
    return f;
}

const raid::Passage* passageTo(const raid::RaidRun& run, int zone) {
    for (const auto& p : run.rules().passages)
        if (p.to == zone) return &p;
    return nullptr;
}

/// The administrator's input of a test at step t: runs right, jumps now and then, strikes.
InputFrame adminInput(uint32_t t, uint32_t& prev) {
    InputFrame f;
    const int dir = (t / 400) % 3 == 2 ? -1 : 1;
    f.held |= 1u << static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft);
    f.moveX = static_cast<int8_t>(dir * 127);
    if (t % 90 < 14) f.held |= 1u << static_cast<int>(Action::Jump);
    if (t % 50 == 0) f.held |= 1u << static_cast<int>(Action::Attack);
    f.pressed = f.held & ~prev;
    f.released = prev & ~f.held;
    prev = f.held;
    return f;
}

}  // namespace

TEST_CASE("raid arrows (2.72): in a raid world, an enemy met then out of view has its arrow; the ambush's have theirs") {
    Svc s;
    auto frames = soloFrames();
    // The Biceps de basalte: its ember mites are awake from the start.
    raid::RaidRun biceps(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(biceps.begin(0, 2));
    for (int t = 0; t < 3; ++t) biceps.step(frames.data(), static_cast<int>(frames.size()), 0);
    GameWorld& b = biceps.world();
    const Enemy* target = nullptr;
    for (const auto& e : b.enemies())
        if (e->alive() && !e->dormant() && !e->def().isBoss) target = e.get();
    CHECK(target != nullptr);
    if (!target) return;
    const Rect screen{0.0f, 0.0f, 1920.0f, 1080.0f};
    WorldView on{target->center() - Vec2{960.0f, 540.0f}, 1.0f, 1920, 1080, {}};
    WorldView away{target->center() + Vec2{2600.0f, -540.0f}, 1.0f, 1920, 1080, {}};
    EnemyArrows::Tracker met;
    // Never seen: no arrow, even out of view; seen once on screen, then out of it: its arrow.
    CHECK(EnemyArrows::targets(b, away, screen, met).usual.empty());
    CHECK(EnemyArrows::targets(b, on, screen, met).usual.empty());
    const EnemyArrows::Targets t = EnemyArrows::targets(b, away, screen, met);
    CHECK(!t.usual.empty());
    EnemyArrows::Layout l;
    l.screen = screen;
    CHECK(!EnemyArrows::layout(l, t.usual).empty());
    // The Épaule's arena under way: its enemies point at it in violet, met or not.
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    for (int k = 0; k < 3; ++k) run.step(frames.data(), static_cast<int>(frames.size()), 0);
    GameWorld& w = run.world();
    CombatZoneEntity* arena = nullptr;
    for (const auto& e : w.entities())
        if (auto* cz = dynamic_cast<CombatZoneEntity*>(e.get()); cz && cz->ambush()) arena = cz;
    CHECK(arena != nullptr);
    if (!arena) return;
    arena->handleAction(w, "activate", Json::object());
    for (int k = 0; k < 2; ++k) run.step(frames.data(), static_cast<int>(frames.size()), 0);
    CHECK(w.activeAmbush() != nullptr);
    WorldView far{arena->box.center() + Vec2{9000.0f, 0.0f}, 1.0f, 1920, 1080, {}};
    EnemyArrows::Tracker fresh;
    CHECK(!EnemyArrows::targets(w, far, screen, fresh).ambush.empty());
    // The raids' layout shows them unless the player turns them off.
    CHECK(RaidHudLayout{}.arrows);
}

TEST_CASE("raid zones (2.72): a zone keeps what was done in it when the group comes back, in a test and in a real instance") {
    Svc s;
    for (const bool test : {true, false}) {
        raid::RaidRun::Test setup;
        setup.on = true;
        setup.invincible = true;
        setup.players = 1;
        raid::TestSession session(s.ws(), bras(), setup);
        raid::RaidRun real(s.ws(), bras(), raid::kMaxInstancePlayers);
        raid::RaidRun& run = test ? session.run() : real;
        CHECK(test ? session.begin(3) : real.begin(0, 3));
        auto frames = soloFrames();
        auto step = [&] {
            if (test) session.step(InputFrame{});
            else real.step(frames.data(), static_cast<int>(frames.size()), 0);
        };
        step();
        GameWorld& w = run.world();
        // The generator hit (its objective), a lava stalker beaten, the health orb taken.
        LevelEntity* gen = w.entity("o28");
        CHECK(gen != nullptr);
        if (!gen) continue;
        CHECK(gen->onPowerHit(w, "wave", gen->box));
        Enemy* beaten = nullptr;
        for (const auto& e : w.enemies())
            if (e->alive() && e->objectId() == "o25") beaten = e.get();
        CHECK(beaten != nullptr);
        if (beaten) beaten->kill(w);
        for (int k = 0; k < 120; ++k) step();
        const int genIndex = bras().objectiveIndex("veine_generateur");
        CHECK(raid::Branch::done(run.mask(), genIndex));
        if (test) CHECK(raid::Branch::done(session.branchState(), genIndex));  // RAID_STATE's structure, kept
        CHECK(run.takeReports().empty() == test);                             // a test never reports anything
        // Away to the Épaule, and back.
        const raid::Passage* out = passageTo(run, 1);
        CHECK(out != nullptr);
        if (!out) continue;
        run.world().playerAt(0).teleportTo({out->rect.center().x, out->rect.bottom()});
        step();
        CHECK(run.zone() == 1);
        CHECK(run.memory().count(3) == 1);
        for (int k = 0; k < 30; ++k) step();
        // Titan 9: the Épaule's ways on wait for its guard (an ambush): the group beats it, wave after wave.
        GameWorld& e = run.world();
        CHECK(e.ambushes().size() == 1u);
        for (CombatZoneEntity* a : e.ambushes()) {
            a->handleAction(e, "start", Json::object());
            for (int k = 0; k < 4000 && !a->cleared(); ++k) {
                for (const auto& x : e.entities())
                    if (auto* es = dynamic_cast<EnemySpawnEntity*>(x.get()); es && a->lists(es->id()))
                        if (Enemy* en = es->enemy(e); en && en->alive() && !en->dormant()) en->kill(e);
                step();
            }
            CHECK(a->cleared());
        }
        for (int k = 0; k < 4; ++k) step();
        const raid::Passage* back = passageTo(run, 3);
        CHECK(back != nullptr);
        if (!back) continue;
        run.world().playerAt(0).teleportTo({back->rect.center().x, back->rect.bottom()});
        step();
        CHECK(run.zone() == 3);
        step();
        GameWorld& v = run.world();
        // The generator is on, the stalker beaten is not there again, the objective still done.
        LevelEntity* again = v.entity("o28");
        CHECK(again != nullptr);
        if (again) CHECK(!again->keepState(v).is_null());
        bool stalker = false;
        for (const auto& e : v.enemies())
            if (e->alive() && e->objectId() == "o25") stalker = true;
        CHECK(!stalker);
        CHECK(raid::Branch::done(run.mask(), genIndex));
    }
}

TEST_CASE("raid zones (2.72): what another group validated is done in the zone when it is loaded") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    const uint32_t golem = 1u << bras().objectiveIndex("biceps_gardien");
    CHECK(run.begin(golem, 2));
    auto frames = soloFrames();
    for (int t = 0; t < 3; ++t) run.step(frames.data(), static_cast<int>(frames.size()), golem);
    std::string source;
    for (const auto& zo : run.rules().objectives)
        if (zo.id == "biceps_gardien") source = zo.source;
    CHECK(!source.empty());
    bool alive = false;
    for (const auto& e : run.world().enemies())
        if (e->alive() && e->objectId() == source) alive = true;
    CHECK(!alive);  // the guardian killed by another group is not there
}

TEST_CASE("raid test (2.72): the simulated players play on their seats, the same digest twice (2 and 8 seats), never a report") {
    Svc s;
    for (const int players : {2, 8}) {
        auto play = [&](std::vector<uint64_t>& digests, int& moved, bool& reported, int& inPlay) {
            raid::RaidRun::Test setup;
            setup.on = true;
            setup.invincible = true;
            setup.players = players;
            raid::TestSession session(s.ws(), bras(), setup);
            CHECK(session.begin(1));
            std::vector<float> start;
            for (int i = 0; i < raid::kMaxInstancePlayers; ++i) start.push_back(session.run().world().playerAt(i).feet().x);
            uint32_t prev = 0;
            for (uint32_t t = 0; t < 1800; ++t) {
                session.step(adminInput(t, prev));
                reported = reported || !session.run().takeReports().empty();
                if (t % 100 == 99) digests.push_back(session.hash());
            }
            const GameWorld& w = session.run().world();
            moved = 0;
            inPlay = 0;
            for (int i = 1; i < raid::kMaxInstancePlayers; ++i) {
                const bool seat = i < players;
                const auto presence = w.member(i).presence;
                CHECK((presence == GameWorld::Presence::Vacant) == !seat);  // the seats beyond them stay free
                if (seat && presence != GameWorld::Presence::Vacant) ++inPlay;
                if (seat && std::fabs(w.playerAt(i).feet().x - start[static_cast<size_t>(i)]) > 48.0f) ++moved;
            }
        };
        std::vector<uint64_t> a, b;
        int movedA = 0, movedB = 0, inA = 0, inB = 0;
        bool repA = false, repB = false;
        play(a, movedA, repA, inA);
        play(b, movedB, repB, inB);
        CHECK(!a.empty());
        CHECK(a == b);
        CHECK(!repA && !repB);
        CHECK(inA == players - 1);
        CHECK(movedA == players - 1);  // they follow the administrator
        CHECK(raid::TestSession::botName(1) == "Joueur simulé 2");
    }
}

TEST_CASE("raid test (2.72): the simulated players stand on a co-op plate with the administrator, and it works") {
    Svc s;
    raid::RaidRun::Test setup;
    setup.on = true;
    setup.invincible = true;
    setup.players = 4;
    raid::TestSession session(s.ws(), bras(), setup);
    CHECK(session.begin(7));
    CHECK(!session.run().rules().coops.empty());
    if (session.run().rules().coops.empty()) return;
    session.step(InputFrame{});
    // The administrator on the first plate: the simulated players needed come by themselves (2.79: the test
    // never puts them there any more) — across the zone, its ambush and its doors on the way.
    const raid::CoopPlate& plate = session.run().rules().coops.front();
    bool done = false;
    int t = 0;
    for (; t < 300 * kSimHz && !done; ++t) {
        GameWorld& w = session.run().world();
        if (!plate.rect.intersects(w.playerAt(0).box)) w.playerAt(0).teleportTo({plate.rect.center().x, plate.rect.bottom()});
        session.step(InputFrame{});
        done = session.run().coop(0).done;
    }
    std::printf("  plaque « %s » tenue en %.1f s (jamais placés : %d blocages comptés)\n", plate.label.c_str(), t / static_cast<double>(kSimHz),
                session.rescues());
    CHECK(done);
}

TEST_CASE("raid magma (2.72): drawn over the actors, it rises, hurts and burns the enemies caught in it; it comes more often") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 3));
    auto frames = soloFrames();
    run.step(frames.data(), static_cast<int>(frames.size()), 0);
    GameWorld& w = run.world();
    CHECK(run.rules().magma.present);
    auto* dz = dynamic_cast<DamageZoneEntity*>(w.entity(run.rules().magma.zone));
    CHECK(dz != nullptr);
    if (!dz) return;
    CHECK(dz->magma());
    CHECK(dz->drawnOverActors());  // GameRenderer draws it after the players and the enemies: it occludes them
    CHECK(dz->magmaLevel() == 0.0f);
    CHECK(dz->overlapBox().h <= 0.0f);  // drained: it touches nothing
    // A walker on the low floor of the vein, then the magma comes (announced first).
    const Rect zone = dz->object().rect;
    Enemy* e = w.spawnEnemy("lava_stalker", {zone.x + 48.0f, zone.bottom()}, false);
    CHECK(e != nullptr);
    if (!e) return;
    const float hp = e->hp();
    dz->handleAction(w, "warn", Json{{"seconds", 3.0f}});
    CHECK(dz->warning());
    dz->handleAction(w, "enable", Json::object());
    run.step(frames.data(), static_cast<int>(frames.size()), 0);
    CHECK(dz->magmaLevel() > 0.0f);
    CHECK(dz->magmaLevel() < 0.1f);  // it rises, it does not appear all at once
    for (int t = 0; t < 4 * kSimHz; ++t) run.step(frames.data(), static_cast<int>(frames.size()), 0);
    CHECK(dz->magmaLevel() == 1.0f);
    CHECK(dz->overlapBox().h > 0.0f);
    CHECK(!e->alive() || e->hp() < hp);  // caught in it: burnt like a player would be
    dz->handleAction(w, "disable", Json::object());
    for (int t = 0; t < 3 * kSimHz; ++t) run.step(frames.data(), static_cast<int>(frames.size()), 0);
    CHECK(dz->magmaLevel() == 0.0f);
    CHECK(!dz->active);
    // More often (tools/levelgen/raid_bras_droit.py): the Veine, the Coude-forge, and the Avant-bras too.
    for (int zid : {3, 4, 6}) {
        const raid::ZoneRules* r = bras().zoneRules(zid);
        CHECK(r != nullptr);
        if (!r) continue;
        CHECK(r->magma.present);
        CHECK(r->magma.period <= 42.0f);
        CHECK(r->magma.warn >= 4.0f);
    }
}

TEST_CASE("raid visuals (2.72): the magma's and the minimap's sheets are the generator's output") {
    // Needs Python with numpy and Pillow (the asset tools); skipped without them.
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_RV_NULL " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification du générateur des visuels de raid sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assetgen/environment/raid_visuals.py --check > " COSMIC_RV_NULL " 2>&1") == 0);
}

TEST_CASE("raid HUD (2.72): its layout is saved apart from the solo one; the large map has its own key") {
    GameplaySettings g;
    g.raidHud[RaidHudElement::Minimap].custom = true;
    g.raidHud[RaidHudElement::Minimap].x = 0.25f;
    g.raidHud[RaidHudElement::Minimap].y = 0.5f;
    g.raidHud[RaidHudElement::Minimap].scale = 1.5f;
    g.raidHud[RaidHudElement::Group].visible = false;
    g.raidHud[RaidHudElement::Danger].visible = false;  // never hidden: what the group must see
    g.raidHud.arrows = false;
    g.raidHud[RaidHudElement::Timer].visible = false;  // the raid's chrono can be hidden
    const RaidHudLayout back = RaidHudLayout::fromJson(g.raidHud.toJson());
    CHECK(!back[RaidHudElement::Timer].visible);
    CHECK(RaidHudLayout{}[RaidHudElement::Timer].visible);  // shown by default
    CHECK(std::string(RaidHudLayout::id(RaidHudElement::Timer)) == "timer");
    CHECK(back[RaidHudElement::Minimap].custom);
    CHECK(std::fabs(back[RaidHudElement::Minimap].x - 0.25f) < 1e-4f);
    CHECK(std::fabs(back[RaidHudElement::Minimap].scale - 1.5f) < 1e-4f);
    CHECK(!back[RaidHudElement::Group].visible);
    CHECK(back[RaidHudElement::Danger].visible);
    CHECK(!back.arrows);
    CHECK(!g.hud[HudElement::Vitals].custom);  // the solo layout is untouched
    // M on the keyboard, View / Back on a gamepad, rebindable (a mouse button too), never recorded.
    const InputBindings b = InputBindings::defaults();
    CHECK(b.keys[static_cast<int>(Action::RaidMap)][0] == SDL_SCANCODE_M);
    CHECK(b.pad[static_cast<int>(Action::RaidMap)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_BACK));
    CHECK(actionIsRebindable(Action::RaidMap));
    CHECK(static_cast<int>(Action::RaidMap) >= InputFrame::kActions);
    CHECK(actionFromId("raid_map") == Action::RaidMap);
}
