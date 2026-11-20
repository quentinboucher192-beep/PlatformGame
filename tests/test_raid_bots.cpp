// 2.77 (docs/RAIDS.md §21): the simulated players of a raid test play the zone themselves (raid::TestBots
// on raid::NavGraph): alone they finish an ambush, a guardian, a generator, the armory's plate and a
// gigaboss; the same steps give the same digest (2 and 8 seats); the administrator AFK is played by the AI.
// « Simuler un raid complet » (raid::RaidSimulation): the whole raid in local up to the Cœur's fall, never
// a report. The rules of cooperation 3 (raids too): a soul on the last safe place, a revival held with
// « Interagir ». The armory: tablets up on their ledges, nothing taken walking on the floor, a name over each.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidHud.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

constexpr int kI = static_cast<int>(Action::Interact);
constexpr int kR = static_cast<int>(Action::MoveRight);

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

/// A test of `bots` simulated players without the administrator (their free place vacant), at `zone`.
std::unique_ptr<raid::TestSession> botsOnly(Svc& s, int zone, int bots, uint32_t state = 0, int startPhase = 1, bool invincible = false) {
    raid::RaidRun::Test t;
    t.on = true;
    t.invincible = true;
    t.players = 1 + bots;
    t.startPhase = startPhase;
    auto session = std::make_unique<raid::TestSession>(s.ws(), bras(), t);
    session->setAdminSeated(false);
    session->setBotsInvincible(invincible);
    session->setBranchState(state);
    CHECK(session->begin(zone));
    return session;
}

/// Steps until `done` (or `limit` steps); the steps it took (-1: not within the limit).
template <class F>
int until(raid::TestSession& s, int limit, F done) {
    for (int t = 0; t < limit; ++t) {
        s.step(InputFrame{});
        if (done()) return t + 1;
    }
    return -1;
}

int objectiveIndex(const char* id) { return bras().objectiveIndex(id); }

}  // namespace

TEST_CASE("raid bots (2.77): the zone's graph — places, jumps, ladders; a path from the start to each passage; the same graph twice") {
    Svc s;
    for (int zone : {1, 2, 3, 7}) {
        raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
        CHECK(run.begin(0, zone));
        raid::NavGraph a, b;
        a.build(run.world(), run.rules().magma.present ? run.rules().magma.zone : std::string());
        b.build(run.world(), run.rules().magma.present ? run.rules().magma.zone : std::string());
        CHECK(a.size() > 40);
        CHECK(a.edgeCount() > static_cast<size_t>(a.size()));
        CHECK(a.hash() == b.hash());
        const int from = a.locate(run.world().playerAt(0).feet());
        CHECK(from >= 0);
        int reached = 0, passages = 0;
        std::vector<int> path;
        for (const raid::Passage& p : run.rules().passages) {
            if (p.door.empty() == false) continue;  // a door that opens later (generators, the wrist)
            ++passages;
            const int to = a.nearest(p.rect.bottomCenter(), 96.0f);
            if (to >= 0 && a.path(from, to, path)) ++reached;
        }
        std::printf("  zone %d : %d places, %zu liaisons, %d / %d passages atteints, graphe en %.1f ms\n", zone, a.size(), a.edgeCount(), reached, passages,
                    a.buildMs());
        CHECK(reached >= 1);
    }
}

TEST_CASE("raid bots (2.77): alone (no administrator), they clear the Épaule's ambush, kill the Biceps' guardian, light the Veine's generator") {
    Svc s;
    struct Case {
        int zone;
        const char* objective;
        int limit;
    };
    for (const Case c : {Case{1, "epaule_arene", 150 * kSimHz}, Case{2, "biceps_gardien", 180 * kSimHz}, Case{3, "veine_generateur", 180 * kSimHz}}) {
        auto session = botsOnly(s, c.zone, 4);
        const int idx = objectiveIndex(c.objective);
        CHECK(idx >= 0);
        const auto t0 = std::chrono::steady_clock::now();
        const int took = until(*session, c.limit, [&] { return raid::Branch::done(session->run().localDone(), idx); });
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  zone %d (%s) : %s en %d pas (%.1f s de jeu, %.0f ms, %d sauvetages du test)\n", c.zone, c.objective, took > 0 ? "fini" : "PAS FINI", took,
                    took / static_cast<double>(kSimHz), ms, session->rescues());
        CHECK(took > 0);
        CHECK(session->run().takeReports().empty());
    }
}

TEST_CASE("raid bots (2.77): the armory — each takes its power by jumping onto its tablet, then the whole group on « Prêts » leaves") {
    Svc s;
    auto session = botsOnly(s, raid::RaidRun::kArmory, 4);
    CHECK(session->run().zone() == 0);
    // 2.78: each zone has its own simulated players: what each one wished is read in the armory, before they leave it.
    std::string wanted[5];
    const int took = until(*session, 120 * kSimHz, [&] {
        if (session->run().zone() == 0)
            for (int seat = 1; seat <= 4; ++seat) wanted[seat] = session->bots().wishOf(seat);
        return session->run().zone() != 0;
    });
    std::printf("  vestiaire : sortie en %d pas\n", took);
    CHECK(took > 0);
    CHECK(session->run().zone() == bras().startZone);
    const GameWorld& w = session->run().world();
    // 2.79: each one its role's power (the reviver: the decoy; the strikers: the charged shot; the others: the triple jump),
    // chosen through InputFrame.
    for (int seat = 1; seat <= 4; ++seat) CHECK(w.playerAt(seat).hasPower(wanted[seat]));
}

TEST_CASE("raid bots (2.77): they defeat Pyroclast by themselves from its phase 2 (its vents together, its weak points bare), within a limit of steps") {
    // « Joueurs simulés invincibles » (a test's setting): what is checked is that they understand the fight —
    // its vents closed together, its weak points and its body struck while it is bare —, not their survival.
    Svc s;
    // Its zone, the objectives before it given as done by other groups (the branch's state).
    uint32_t state = 0;
    for (const raid::ObjectiveInfo& o : bras().objectives)
        if (o.role != "gigaboss" && !o.optional) state |= 1u << o.index;
    auto session = botsOnly(s, 8, 7, state, 2, true);  // its carapace phase, then its last
    const int idx = objectiveIndex("main_pyroclast");
    const auto t0 = std::chrono::steady_clock::now();
    // 2.76.2: it fights by its parts — only its phalanges (× 2), its fist and its arm (× 0,5) count, its fingers never:
    // the simulated players strike it where a blow counts, from the platforms or when its fist comes down (slower).
    const int took = until(*session, 1200 * kSimHz, [&] { return raid::Branch::done(session->run().localDone(), idx) || session->run().wiped(); });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  Pyroclast : %s en %d pas (%.0f s de jeu, %.0f ms ; phase %d)\n", raid::Branch::done(session->run().localDone(), idx) ? "vaincu" : "DEBOUT",
                took, took / static_cast<double>(kSimHz), ms, session->run().gigaboss().phaseReached());
    CHECK(raid::Branch::done(session->run().localDone(), idx));
    CHECK(session->run().gigaboss().phaseReached() >= 3);
}

TEST_CASE("raid bots (2.77): the same steps, the same digest (2 and 8 seats); the administrator AFK is played by the AI") {
    Svc s;
    for (int players : {2, 8}) {
        uint64_t digest[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            raid::RaidRun::Test t;
            t.on = true;
            t.invincible = true;
            t.players = players;
            raid::TestSession session(s.ws(), bras(), t);
            CHECK(session.begin(1));
            for (int step = 0; step < 20 * kSimHz; ++step) session.step(InputFrame{});
            CHECK(session.autopilot());  // 15 s without a key: the AI plays the administrator's seat
            digest[k] = session.hash();
        }
        CHECK(digest[0] == digest[1]);
    }
}

TEST_CASE("raid simulation (2.77): the whole raid in local up to the Cœur's fall (reduced: from the lairs), never a report nor a network call") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 7;
    setup.bots = 7;
    setup.lairs = true;  // the real lairs, then the real heart
    setup.startPhase = 3;  // reduced: every gigaboss from its phase 3, the heart from its last (75 % of its shared health),
    setup.heartStartPhase = 4;
    setup.skill = raid::BotSkill::Expert;
    setup.botsInvincible = true;  // and the simulated players invincible (what is tested: the raid's course, not their survival)
    // The three Gigaboss the simulated players cannot beat yet (Titanus's restraints high on its legs, Skullmaw's
    // mirrors, Dreadfang's charges: docs/RAIDS.md §21, limits) are given as fallen by other groups.
    setup.given = {"crane", "bras_gauche", "jambe_droite"};
    auto run = [&](uint64_t& digest, uint32_t& steps, double& avg, double& worst) {
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        CHECK(sim.groups().size() == 7u);
        const uint32_t limit = 3600u * kSimHz;
        uint32_t opened = 0;
        while (!sim.heartFallen() && sim.tick() < limit) {
            sim.step(InputFrame{});
            if (sim.heartOpen() && opened == 0) opened = sim.tick();
            for (const auto& g : sim.groups()) CHECK(g->session->run().takeReports().empty());
        }
        digest = sim.hash();
        steps = sim.tick();
        avg = sim.avgStepMs();
        worst = sim.maxStepMs();
        std::printf("  raid complet : %s en %u pas (%.0f s de jeu), %d Gigaboss, %zu groupes, pas moyen %.2f ms (max %.1f ms), %llu pas de mondes\n",
                    sim.heartFallen() ? "Cœur tombé" : "PAS FINI", steps, steps / static_cast<double>(kSimHz), sim.gigabossesDown(), sim.groups().size(), avg, worst,
                    static_cast<unsigned long long>(sim.worldSteps()));
        for (const auto& e : sim.feed()) std::printf("    %5.0f s  %s\n", e.tick / static_cast<double>(kSimHz), e.text.c_str());
        CHECK(sim.heartFallen());
        CHECK(sim.gigabossesDown() == 7);
        CHECK(opened > 0);
        std::printf("  le Cœur : ouvert à %.0f s, tombé %.0f s plus tard (vie partagée ôtée : %d / 10000)\n", opened / static_cast<double>(kSimHz),
                    (steps - opened) / static_cast<double>(kSimHz), sim.heartTaken());
        CHECK(steps - opened > 20u * kSimHz);  // a real fight of every group, not a jump of phase
    };
    uint64_t a = 0;
    uint32_t sa = 0;
    double avg = 0, worst = 0;
    run(a, sa, avg, worst);
}

TEST_CASE("raid simulation (2.77): the same raid twice — the same digest at every step checked (7 groups, from the lairs)") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 7;
    setup.bots = 3;
    setup.lairs = true;
    std::vector<uint64_t> d[2];
    for (int k = 0; k < 2; ++k) {
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        for (int t = 0; t < 40 * kSimHz; ++t) {
            sim.step(InputFrame{});
            if (t % 600 == 599) d[k].push_back(sim.hash());
        }
    }
    CHECK(!d[0].empty());
    CHECK(d[0] == d[1]);
}

TEST_CASE("raid simulation (2.77): from the armories, 7 groups of 7 leave them for their branch; the cost of a step of the whole raid") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 7;
    setup.bots = 7;
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);  // nobody seated: every group plays alone
    const auto t0 = std::chrono::steady_clock::now();
    int left = 0;
    for (int t = 0; t < 150 * kSimHz && left < 7; ++t) {
        sim.step(InputFrame{});
        left = 0;
        for (const auto& g : sim.groups()) left += g->session->run().zone() != 0;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  7 groupes de 7 : %d / 7 sortis du vestiaire en %u pas (%.0f s de jeu) ; pas du raid : moyen %.2f ms, max %.1f ms ; %.0f ms au total\n", left,
                sim.tick(), sim.tick() / static_cast<double>(kSimHz), sim.avgStepMs(), sim.maxStepMs(), ms);
    CHECK(left == 7);
    for (int t = 0; t < 30 * kSimHz; ++t) sim.step(InputFrame{});  // the first zones (graphs built, fights)
    std::printf("  ensuite 30 s dans les premières zones : pas moyen %.2f ms, max %.1f ms\n", sim.avgStepMs(), sim.maxStepMs());
}

TEST_CASE("raid simulation (2.77): the administrator takes a group's free place, watches another; the groups go on without them") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 3;
    setup.bots = 2;
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    CHECK(sim.seated());
    CHECK(sim.viewed() == 0);
    CHECK(sim.group(0).session->adminSeated());
    CHECK(!sim.group(1).session->adminSeated());
    for (int t = 0; t < 120; ++t) sim.step(InputFrame{});
    CHECK(sim.join(2));
    CHECK(sim.viewed() == 2);
    CHECK(!sim.group(0).session->adminSeated());
    CHECK(sim.group(2).session->adminSeated());
    for (int t = 0; t < 120; ++t) sim.step(InputFrame{});
    CHECK(sim.group(0).session->run().world().member(0).presence == GameWorld::Presence::Vacant);  // its free place again
    CHECK(sim.group(2).session->run().world().member(0).presence == GameWorld::Presence::Playing);
    sim.watch(1);
    CHECK(!sim.seated());
    for (int t = 0; t < 120; ++t) sim.step(InputFrame{});
    for (int g = 0; g < 3; ++g) CHECK(!sim.group(g).session->adminSeated());
    CHECK(sim.feed().size() >= 1u);
}

// ------------------------------------------------------------------ the rules of cooperation 3 (2.77)

namespace {

struct Pair {
    std::vector<InputFrame> prev = std::vector<InputFrame>(raid::kMaxInstancePlayers);
    std::vector<InputFrame> frames(std::vector<InputFrame> held) {
        held.resize(raid::kMaxInstancePlayers);
        for (int k = 0; k < raid::kMaxInstancePlayers; ++k) {
            InputFrame& f = held[static_cast<size_t>(k)];
            if (k >= 2) {
                f = InputFrame{};
                f.flags = InputFrame::Vacant;
                continue;
            }
            f.pressed = f.held & ~prev[static_cast<size_t>(k)].held;
            f.released = prev[static_cast<size_t>(k)].held & ~f.held;
            prev[static_cast<size_t>(k)] = f;
        }
        return held;
    }
};

InputFrame interact() {
    InputFrame f;
    f.held = 1u << kI;
    return f;
}

}  // namespace

TEST_CASE("cooperation 3 (2.77): death in the rising magma, the soul on the last safe ground; revived only with « Interagir » held, let go it stops") {
    CHECK(kCoopSimulationVersion == 3);
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 3));  // the Veine de magma
    CHECK(run.rules().magma.present);
    Pair pair;
    auto step = [&](std::vector<InputFrame> held = {}) {
        auto f = pair.frames(std::move(held));
        run.step(f.data(), static_cast<int>(f.size()), 0);
    };
    for (int t = 0; t < 120; ++t) step();
    GameWorld& w = run.world();
    auto* magma = dynamic_cast<DamageZoneEntity*>(w.entity(run.rules().magma.zone));
    CHECK(magma != nullptr);
    if (!magma) return;
    const Rect zone = magma->object().rect;
    // Seat 1 on firm ground clear of the passages (its last safe place), then on the floor of the magma's basin
    // while it is drained (safe then too), then the magma rises.
    w.playerAt(1).teleportTo({zone.x - 30.0f, zone.bottom() - 32.0f});
    for (int t = 0; t < 60; ++t) step();
    const Vec2 safe = w.member(1).safeFeet;
    w.playerAt(1).teleportTo({zone.center().x, zone.bottom() - 1.0f});
    for (int t = 0; t < 40; ++t) step();
    magma->handleAction(w, "enable", Json::object());
    for (int t = 0; t < 900 && w.member(1).presence != GameWorld::Presence::Down; ++t) step();
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    const Vec2 soul = w.member(1).soul;
    // Out of the liquid (its highest level), on firm ground: where seat 1 last stood safely, or lifted above.
    const Rect body{soul.x - 5.0f, soul.y - 22.0f, 10.0f, 21.5f};
    CHECK(!magma->overlapBox().intersects(body));
    std::printf("  âme en (%.0f, %.0f) ; magma (%.0f, %.0f, %.0f, %.0f) ; dernier sol sûr (%.0f, %.0f)\n", soul.x, soul.y, zone.x, zone.y, zone.w, zone.h, safe.x, safe.y);
    const int serial = run.zoneSerial();  // seat 0 is put beside the soul (on its right: the passage is at the left)
    // Seat 0 beside the soul without « Interagir »: nothing.
    w.playerAt(0).teleportTo(soul + Vec2{14.0f, 0.0f});
    for (int t = 0; t < 360; ++t) step();
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    // Held 1 s, let go: the revival stops (back to zero).
    w.playerAt(0).teleportTo(soul + Vec2{14.0f, 0.0f});
    for (int t = 0; t < kSimHz; ++t) step({interact()});
    CHECK(w.member(1).revive > 0.5f);
    step();
    CHECK(w.member(1).revive == 0.0f);
    for (int t = 0; t < 300; ++t) step();
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    // Held 2 s: back on its feet.
    w.playerAt(0).teleportTo(soul + Vec2{14.0f, 0.0f});
    for (int t = 0; t < 2 * kSimHz + 60 && w.member(1).presence == GameWorld::Presence::Down; ++t) step({interact()});
    CHECK(w.member(1).presence == GameWorld::Presence::Playing);
    CHECK(run.zoneSerial() == serial);
    // The rules of cooperation 2 (an old team replay): standing near the soul was enough.
    {
        GameWorld::PlayerScope sc(w, 1);
        w.playerAt(1).kill(w);
    }
    for (int t = 0; t < 240; ++t) step();
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    w.setCoopVersion(2);
    w.playerAt(0).teleportTo(w.member(1).soul + Vec2{14.0f, 0.0f});
    for (int t = 0; t < 2 * kSimHz + 60 && w.member(1).presence == GameWorld::Presence::Down; ++t) step();
    CHECK(w.member(1).presence == GameWorld::Presence::Playing);
}

TEST_CASE("cooperation 3 (2.77): a soul never waits in a passage; a revival never takes the group to another zone") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 3));
    Pair pair;
    auto step = [&](std::vector<InputFrame> held = {}) {
        auto f = pair.frames(std::move(held));
        run.step(f.data(), static_cast<int>(f.size()), 0);
    };
    for (int t = 0; t < 120; ++t) step();
    GameWorld& w = run.world();
    const raid::Passage* open = nullptr;
    for (const raid::Passage& p : run.rules().passages)
        if (run.passageOpen(p)) open = &p;
    CHECK(open != nullptr);
    if (!open) return;
    const Rect passage = open->rect;
    auto inPassage = [&](const Rect& r) {
        for (const raid::Passage& p : run.rules().passages)
            if (p.rect.intersects(r)) return true;
        return false;
    };
    const int serial = run.zoneSerial();
    // Seat 1 down, its soul in the middle of an open passage (its last safe place there): at the next step, the
    // soul is on the nearest safe floor of the zone, out of every passage.
    GameWorld::MemberCarry down = w.carryOf(1);
    down.presence = GameWorld::Presence::Down;
    w.restoreMember(1, down, passage.center());
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    step();
    const Vec2 soul = w.member(1).soul;
    std::printf("  passage (%.0f, %.0f, %.0f, %.0f) ; âme déplacée en (%.0f, %.0f)\n", passage.x, passage.y, passage.w, passage.h, soul.x, soul.y);
    CHECK(!inPassage(Rect{soul.x - 5.0f, soul.y - 22.0f, 10.0f, 21.5f}.expanded(8.0f)));
    bool floor = false;
    for (const Collider& c : w.physics().colliders())
        floor = floor || (c.enabled && !c.dynamic && std::abs(c.box.y - soul.y) < 0.5f && soul.x >= c.box.x && soul.x <= c.box.right());
    CHECK(floor);
    CHECK(run.zoneSerial() == serial);
    // Revived there (« Interagir » held by seat 0 on the soul): on its feet in the same zone, out of the passage.
    w.playerAt(0).teleportTo(soul);
    for (int t = 0; t < 2 * kSimHz + 60 && w.member(1).presence == GameWorld::Presence::Down; ++t) step({interact()});
    CHECK(w.member(1).presence == GameWorld::Presence::Playing);
    for (int t = 0; t < 120; ++t) step();
    CHECK(run.zoneSerial() == serial);
    CHECK(!inPassage(w.playerAt(1).box));
    // Coming into play inside a passage (revived, back from an absence) never takes it: the player must step out
    // of it, then walk into it.
    {
        GameWorld::PlayerScope sc(w, 1);
        w.playerAt(1).kill(w);
    }
    for (int t = 0; t < 240; ++t) step();
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    GameWorld::MemberCarry back = w.carryOf(1);
    back.presence = GameWorld::Presence::Playing;
    w.restoreMember(1, back, passage.center());
    CHECK(w.playerAt(1).box.intersects(passage));
    for (int t = 0; t < 120; ++t) step();
    CHECK(run.zoneSerial() == serial);
    CHECK(w.member(1).presence == GameWorld::Presence::Playing);
    w.playerAt(1).teleportTo(w.playerAt(0).feet());
    for (int t = 0; t < 30; ++t) step();
    CHECK(run.zoneSerial() == serial);
    // Walked into it: it goes (2.78: alone, seat 0 stays where it is; the passage works as before).
    const int here = run.zoneOf(1);
    w.playerAt(1).teleportTo(passage.center());
    for (int t = 0; t < 60 && run.zoneOf(1) == here; ++t) step();
    CHECK(run.zoneOf(1) != here);
    CHECK(run.zoneOf(0) == here);
}

TEST_CASE("cooperation 3 (2.77): killed in mid-jump in a raid — the soul falls under it; over the void, at the last safe place; revived; the same digest (2 and 8 seats)") {
    Svc s;
    constexpr uint32_t kNotFirm = ColliderTag::Moving | ColliderTag::Crumble | ColliderTag::Teammate | ColliderTag::EnemyOnly | ColliderTag::Door |
                                  ColliderTag::Gate | ColliderTag::Breakable | ColliderTag::Bounce | ColliderTag::ShieldPass;
    for (int seats : {2, 8}) {
        uint64_t digest[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            raid::RaidRun run(s.ws(), bras(), seats);
            CHECK(run.begin(0, 3));
            std::vector<InputFrame> prev(static_cast<size_t>(seats));
            auto step = [&](bool interact) {
                std::vector<InputFrame> f(static_cast<size_t>(seats));
                if (interact) f[0].held = 1u << kI;
                for (size_t j = 0; j < f.size(); ++j) {
                    f[j].pressed = f[j].held & ~prev[j].held;
                    f[j].released = prev[j].held & ~f[j].held;
                    prev[j] = f[j];
                }
                run.step(f.data(), seats, 0);
            };
            for (int t = 0; t < 120; ++t) step(false);
            GameWorld& w = run.world();
            const int serial = run.zoneSerial();
            auto clear = [&](const Rect& r) {
                for (const Collider& c : w.physics().colliders())
                    if (c.enabled && !(c.tags & (ColliderTag::Teammate | ColliderTag::EnemyOnly)) && c.box.intersects(r)) return false;
                for (const raid::Passage& p : run.rules().passages)
                    if (p.rect.expanded(8.0f).intersects(r)) return false;
                return true;
            };
            // A firm ground beside seat 1, the air clear 70 px over it: where it is killed in mid-jump.
            const Vec2 f = w.playerAt(1).feet();
            Vec2 under{-1.0f, -1.0f};
            for (float dx : {64.0f, -64.0f, 48.0f, -48.0f, 96.0f, -96.0f, 32.0f, -32.0f, 128.0f, -128.0f}) {
                const float x = f.x + dx;
                bool firm = false;
                for (const Collider& c : w.physics().colliders())
                    firm = firm || (c.enabled && !c.dynamic && !(c.tags & kNotFirm) && std::abs(c.box.y - f.y) < 0.5f && x >= c.box.x && x <= c.box.right());
                if (firm && clear({x - 6.0f, f.y - 96.0f, 12.0f, 95.5f})) {
                    under = {x, f.y};
                    break;
                }
            }
            CHECK(under.x >= 0.0f);
            if (under.x < 0.0f) return;
            // A column over the void: nothing under it down to the bottom of the level.
            const Rect b = w.level().bounds;
            Vec2 over{-1.0f, -1.0f};
            for (float x = b.x + 48.0f; x < b.right() - 48.0f && over.x < 0.0f; x += 8.0f) {
                float lowest = b.y;
                for (const Collider& c : w.physics().colliders())
                    if (c.enabled && !(c.tags & (ColliderTag::Teammate | ColliderTag::EnemyOnly)) && c.box.x <= x + 8.0f && c.box.right() >= x - 8.0f)
                        lowest = std::max(lowest, c.box.bottom());
                const Vec2 at{x, lowest + 48.0f};
                if (at.y < b.bottom() - 16.0f && at.y < w.level().effectiveKillY() && clear({x - 6.0f, at.y - 26.0f, 12.0f, 26.0f})) over = at;
            }
            CHECK(over.x >= 0.0f);
            if (over.x < 0.0f) return;
            auto killAt = [&](Vec2 feet) {
                w.playerAt(1).teleportTo(feet);
                GameWorld::PlayerScope sc(w, 1);
                w.playerAt(1).kill(w);
            };
            auto revived = [&]() {
                w.playerAt(0).teleportTo(w.member(1).soul);
                for (int t = 0; t < 2 * kSimHz + 60 && w.member(1).presence == GameWorld::Presence::Down; ++t) step(true);
                return w.member(1).presence == GameWorld::Presence::Playing;
            };
            // Killed in mid-jump over the ground: the soul falls under the point of its death.
            const Vec2 safe = w.member(1).safeFeet;
            killAt(under + Vec2{0.0f, -70.0f});
            for (int t = 0; t < 150; ++t) step(false);
            CHECK(w.member(1).presence == GameWorld::Presence::Down);
            const Vec2 soul = w.member(1).soul;
            if (k == 0)
                std::printf("  %d places : mort en (%.0f, %.0f), âme en (%.0f, %.0f) (dernier endroit sûr (%.0f, %.0f))\n", seats, under.x, under.y - 70.0f,
                            soul.x, soul.y, safe.x, safe.y);
            CHECK(std::abs(soul.x - under.x) < 0.5f);
            CHECK(std::abs(soul.y - under.y) < 0.5f);
            CHECK(revived());
            for (int t = 0; t < 60; ++t) step(false);
            // Killed over the void: the soul at the last safe place (where it stood, on firm ground).
            const Vec2 safe2 = w.member(1).safeFeet;
            killAt(over);
            for (int t = 0; t < 150; ++t) step(false);
            CHECK(w.member(1).presence == GameWorld::Presence::Down);
            if (k == 0)
                std::printf("  %d places : mort au-dessus du vide en (%.0f, %.0f), âme en (%.0f, %.0f) (dernier endroit sûr (%.0f, %.0f))\n", seats, over.x,
                            over.y, w.member(1).soul.x, w.member(1).soul.y, safe2.x, safe2.y);
            // (A last safe place in a door or a passage — the zone's entrance — is left for the nearest safe floor.)
            auto inDoor = [&](Vec2 feet) {
                const Rect body{feet.x - 5.0f, feet.y - 22.0f, 10.0f, 21.5f};
                for (const auto& e : w.entities())
                    if (!e->removed && dynamic_cast<const DoorEntity*>(e.get()) && e->box.intersects(body)) return true;
                return !clear({feet.x - 5.0f, feet.y - 22.0f, 10.0f, 21.5f});
            };
            const Vec2 soul2 = w.member(1).soul;
            if (k == 0 && inDoor(safe2)) std::printf("  (dernier endroit sûr dans une porte ou un passage : l'âme va sur le sol sûr le plus proche)\n");
            if (!inDoor(safe2)) {
                CHECK(std::abs(soul2.x - safe2.x) < 0.5f);
                CHECK(std::abs(soul2.y - safe2.y) < 0.5f);
            } else {
                CHECK(!inDoor(soul2));
                CHECK(std::abs(soul2.x - safe2.x) < 64.0f);
            }
            CHECK(revived());
            for (int t = 0; t < 60; ++t) step(false);
            CHECK(run.zoneSerial() == serial);
            digest[k] = run.hash();
        }
        CHECK(digest[0] == digest[1]);
    }
}

// ------------------------------------------------------------------ the armory (2.77)

TEST_CASE("raid armory (2.77): walking on the floor to « Prêts » takes nothing; each tablet is up on its ledge, within a jump") {
    Svc s;
    for (const raid::Branch* b : raid::Branch::all()) {
        if (!b->hasArmory()) continue;
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        CHECK(run.begin(0, raid::RaidRun::kArmory));
        Pair pair;
        GameWorld& w = run.world();
        const float floorY = w.playerAt(0).feet().y;
        std::vector<std::string> before;
        for (const auto& p : w.playerAt(0).powers()) before.push_back(p->id());
        bool offered = false;
        int tablets = 0;
        for (const auto& e : w.entities())
            if (auto* t = dynamic_cast<const PowerUnlockEntity*>(e.get())) {
                ++tablets;
                CHECK(e->box.bottom() < floorY - 22.0f);         // over a walking player's head
                CHECK(e->box.bottom() > floorY - 22.0f - 58.0f - 48.0f);  // a jump from the floor or a lower ledge reaches it
                (void)t;
            }
        CHECK(tablets >= 19);
        InputFrame r;
        r.held = 1u << kR;
        r.moveX = 127;
        const Rect plate = run.rules().coops.front().rect;
        for (int t = 0; t < 30 * kSimHz && w.playerAt(0).feet().x < plate.x; ++t) {
            auto f = pair.frames({r});
            run.step(f.data(), static_cast<int>(f.size()), 0);
            offered = offered || w.powerOffer().has_value();
        }
        CHECK(w.playerAt(0).feet().x >= plate.x);
        CHECK(!offered);
        std::vector<std::string> after;
        for (const auto& p : w.playerAt(0).powers()) after.push_back(p->id());
        CHECK(before == after);
    }
}

TEST_CASE("raid armory (2.77): a name over every tablet, measured; two names never overlap, nor a tablet or a ledge") {
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
        WorldServices ws{app.resources(), app.audio(), g.content, nullptr, 0.0f, 1.0f};
        raid::RaidRun run(ws, bras(), raid::kMaxInstancePlayers);
        CHECK(run.begin(0, raid::RaidRun::kArmory));
        GameWorld& w = run.world();
        for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(app, 1.0f / 60.0f);
            // The raid's camera at three places of the room, and the whole room at once.
            std::vector<WorldView> views;
            for (float x : {300.0f, 900.0f, 1500.0f}) {
                WorldView v = w.camera().view(static_cast<int>(ui.screenW()), static_cast<int>(ui.screenH()), 1.0f);
                v.topLeft = {x - ui.screenW() * 0.5f / v.scale, w.level().bounds.bottom() - ui.screenH() / v.scale};
                views.push_back(v);
            }
            WorldView all;
            all.scale = ui.screenW() / w.level().bounds.w;
            all.screenW = static_cast<int>(ui.screenW());
            all.screenH = static_cast<int>(ui.screenH());
            all.topLeft = {w.level().bounds.x, w.level().bounds.bottom() - ui.screenH() / all.scale};
            views.push_back(all);
            for (const WorldView& v : views) {
                const auto labels = raidhud::armoryLabels(ui, w, v);
                CHECK(labels.size() >= 19u);  // every tablet has its name (in or out of the view)
                std::vector<Rect> solid;
                for (const auto& e : w.entities())
                    if (dynamic_cast<const PowerUnlockEntity*>(e.get())) solid.push_back(v.toScreen(e->box));
                for (const Collider& c : w.physics().colliders())
                    if (c.enabled && !c.dynamic && c.kind == ColliderKind::OneWay) solid.push_back(v.toScreen(c.box));
                for (size_t i = 0; i < labels.size(); ++i) {
                    CHECK(!labels[i].name.empty());
                    for (size_t j = i + 1; j < labels.size(); ++j) CHECK(!labels[i].box.intersects(labels[j].box));
                    for (const Rect& r : solid) CHECK(!labels[i].box.intersects(r));
                    const float nameW = ui.text().measure(labels[i].name, FontFace::Bold, ui.s(labels[i].size)).x;
                    CHECK(nameW <= labels[i].box.w + 0.5f);
                }
            }
            ui.end();
        }
    }
    app.shutdown();
}
