// « Simuler un raid complet » (raid::RaidSimulation, docs/RAIDS.md §22 and §40): the rules between the groups.
//
// * A group whose branch's Gigaboss fell STAYS in its branch, « en attente »; it never goes to help a group that
//   is only slow or weakened.
// * A waiting group goes to another branch ONLY to replace a group wiped there (it takes the zone back where that
//   group fell, the branch's validated state kept), or to take a branch no group plays (fewer groups than branches),
//   or to be the second group a branch cannot reach its Gigaboss without (the Torse: its 4 plates are two pairs, and
//   one group never does both of a pair).
// * More groups than branches: the 8th doubles the Torse from the start (its two-group lock), the next ones the other
//   branches in the titan's order.
// * The seven Gigaboss fallen: every group still in play goes to the Cœur, each in its own instance, and the Cœur has
//   ONE health for all of them (each instance's part summed, never a bar per instance).
//
// And the benches (measures, not checks): the full raid (COSMIC_RAID_SIM_BENCH), where each group is and what blocks it,
// with numbers; one zone of a branch (COSMIC_RAID_ZONE_BENCH), its plates and each player.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"
#include "tests/TestFramework.h"

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

int envInt(const char* name, int fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::atoi(v) : fallback;
}

std::string clock(uint32_t tick) {
    const int s = static_cast<int>(tick / kSimHz);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

}  // namespace

TEST_CASE("raid simulation bench (§40): the full raid from the armories, where each group is and what blocks it (COSMIC_RAID_SIM_BENCH)") {
    // A measure, not a check (an hour of play): only with COSMIC_RAID_SIM_BENCH set. COSMIC_RAID_SIM_GROUPS (8),
    // COSMIC_RAID_SIM_BOTS (4: the menu's « Joueurs par groupe : 5 »), COSMIC_RAID_SIM_SKILL (1: Confirmé),
    // COSMIC_RAID_SIM_MINUTES (90), COSMIC_RAID_SIM_INVINCIBLE (the simulated players invincible), COSMIC_RAID_SIM_EVERY
    // (seconds between two lines of each group, 120).
    if (!std::getenv("COSMIC_RAID_SIM_BENCH")) {
        std::printf("  (mesure : COSMIC_RAID_SIM_BENCH=1)\n");
        return;
    }
    Svc s;
    raid::SimSetup setup;
    setup.groups = envInt("COSMIC_RAID_SIM_GROUPS", 8);
    setup.bots = envInt("COSMIC_RAID_SIM_BOTS", 4);
    setup.fullGroups = true;  // the menu's « Joueurs par groupe »
    setup.skill = static_cast<raid::BotSkill>(std::clamp(envInt("COSMIC_RAID_SIM_SKILL", 1), 0, 2));
    setup.botsInvincible = std::getenv("COSMIC_RAID_SIM_INVINCIBLE") != nullptr;
    setup.rescues = std::getenv("COSMIC_RAID_SIM_NORESCUE") == nullptr;
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);  // nobody seated: every group plays alone (its free seat played too)
    const uint32_t limit = static_cast<uint32_t>(envInt("COSMIC_RAID_SIM_MINUTES", 90)) * 60u * kSimHz;
    const uint32_t every = static_cast<uint32_t>(std::max(10, envInt("COSMIC_RAID_SIM_EVERY", 120))) * kSimHz;
    // Per group: the zone it is in (its lowest seat in play), since when, the longest stay in one zone, the zones reached.
    struct Track {
        int branch = -1, zone = -999;
        uint32_t since = 0, longest = 0;
        int longestZone = 0, longestBranch = 0;
        std::vector<std::string> path;
    };
    std::vector<Track> tracks;
    const auto t0 = std::chrono::steady_clock::now();
    std::set<std::pair<uint32_t, std::string>> printed;
    uint32_t fedTick = 0;
    while (!sim.heartFallen() && sim.tick() < limit) {
        sim.step(InputFrame{});
        // The feed keeps its last 60 lines: each new one printed once (its step and its text).
        for (const auto& e : sim.feed())
            if (e.tick >= fedTick && printed.insert({e.tick, e.text}).second) std::printf("    %7s  %s\n", clock(e.tick).c_str(), e.text.c_str());
        if (!sim.feed().empty()) fedTick = sim.feed().front().tick;
        tracks.resize(sim.groups().size());
        for (size_t i = 0; i < sim.groups().size(); ++i) {
            const auto& g = *sim.groups()[i];
            Track& t = tracks[i];
            const int zone = g.session->run().zone();
            if (g.branch != t.branch || zone != t.zone) {
                if (t.zone != -999 && sim.tick() - t.since > t.longest) {
                    t.longest = sim.tick() - t.since;
                    t.longestZone = t.zone;
                    t.longestBranch = t.branch;
                }
                const raid::Branch& br = *sim.branches()[static_cast<size_t>(g.branch)];
                t.path.push_back(clock(sim.tick()) + " " + br.part + ":" + std::to_string(zone));
                t.branch = g.branch;
                t.zone = zone;
                t.since = sim.tick();
            }
        }
        if (sim.tick() % every == 0) {
            std::printf("  ---- %s · %d / 7 Gigaboss · Cœur %s %d %%\n", clock(sim.tick()).c_str(), sim.gigabossesDown(),
                        sim.heartFallen() ? "tombé" : sim.heartOpen() ? "ouvert" : "fermé", sim.heartTaken() / 100);
            for (size_t i = 0; i < sim.groups().size(); ++i) {
                const auto& g = *sim.groups()[i];
                const raid::RaidRun& run = g.session->run();
                int inPlay = 0, seats = 0;
                for (int seat = 0; seat < g.session->players(); ++seat) {
                    const GameWorld* w = run.worldOf(run.zoneOf(seat));
                    if (!w || seat >= w->teamSize() || w->member(seat).presence == GameWorld::Presence::Vacant) continue;
                    ++seats;
                    inPlay += w->member(seat).presence == GameWorld::Presence::Playing;
                }
                std::string zones;
                for (int z : run.zonesLoaded()) zones += (zones.empty() ? "" : ",") + std::to_string(z);
                std::printf("    %-10s %-13s zone %-2d (zones %s) depuis %s · %d/%d en jeu · %s%s · %s\n", g.name.c_str(),
                            sim.branches()[static_cast<size_t>(g.branch)]->part.c_str(), run.zone(), zones.c_str(),
                            clock(sim.tick() - tracks[i].since).c_str(), inPlay, seats, g.wiped ? "ÉLIMINÉ" : "",
                            g.waiting ? "EN ATTENTE" : "", g.session->bots().plan().c_str());
                // COSMIC_RAID_SIM_DETAIL=1 (every group): its plates and each player (where, what it thinks).
                if (std::getenv("COSMIC_RAID_SIM_DETAIL") && !g.wiped && !g.waiting) {
                    for (size_t c = 0; c < run.rules().coops.size(); ++c) {
                        const auto v = run.coop(c);
                        if (v.done) continue;
                        std::printf("        plaque %s : %d / %d, embuscades %d / %d\n", run.rules().coops[c].id.c_str(), v.on, v.need, v.ambushLeft, v.ambushTotal);
                    }
                    for (int seat = 0; seat < g.session->players(); ++seat) {
                        const GameWorld* w = run.worldOf(run.zoneOf(seat));
                        if (!w || seat >= w->teamSize() || w->member(seat).presence == GameWorld::Presence::Vacant) continue;
                        const auto& th = g.session->bots().thought(seat);
                        std::printf("        place %d zone %d (%.0f, %.0f) %d : %s · %s\n", seat, run.zoneOf(seat), w->playerAt(seat).feet().x, w->playerAt(seat).feet().y,
                                    static_cast<int>(w->member(seat).presence), th.intent.c_str(), th.move.c_str());
                    }
                }
            }
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  == %s en %s de jeu · %d / 7 Gigaboss · Cœur %d %% ôtés · %zu groupes · %d morts · %d sauvetages · %.0f s de calcul\n",
                sim.heartFallen() ? "Cœur tombé" : "PAS FINI", clock(sim.tick()).c_str(), sim.gigabossesDown(), sim.heartTaken() / 100, sim.groups().size(),
                sim.botDeaths(), sim.rescues(), ms / 1000.0);
    for (size_t i = 0; i < sim.groups().size(); ++i) {
        const auto& g = *sim.groups()[i];
        Track& t = tracks[i];
        if (sim.tick() - t.since > t.longest) {
            t.longest = sim.tick() - t.since;
            t.longestZone = t.zone;
            t.longestBranch = t.branch;
        }
        std::string path;
        for (const std::string& p : t.path) path += (path.empty() ? "" : " → ") + p;
        std::printf("  %-10s %s · morts %d · le plus long : %s dans %s:%d\n      %s\n", g.name.c_str(), g.wiped ? "ÉLIMINÉ" : g.waiting ? "EN ATTENTE" : "en jeu",
                    g.session->botDeaths(), clock(t.longest).c_str(), sim.branches()[static_cast<size_t>(t.longestBranch)]->part.c_str(), t.longestZone, path.c_str());
    }
}

TEST_CASE("raid zone bench (§40): one zone of a branch, 5 simulated players (Confirmé, invincible), its plates and each player every 10 s (COSMIC_RAID_ZONE_BENCH)") {
    // A measure (minutes of play): COSMIC_RAID_ZONE_PART (bras_droit), _NUMBER (7), _FROM (the zone it starts in: -1 the armory, its powers
    // chosen there), _SECONDS (600), _SEED (the simulated players' seed). The objectives before the zone are given as done.
    if (!std::getenv("COSMIC_RAID_ZONE_BENCH")) {
        std::printf("  (mesure : COSMIC_RAID_ZONE_BENCH=1)\n");
        return;
    }
    Svc s;
    const raid::Branch* b = raid::Branch::builtin(std::getenv("COSMIC_RAID_ZONE_PART") ? std::getenv("COSMIC_RAID_ZONE_PART") : "bras_droit");
    CHECK(b != nullptr);
    if (!b) return;
    const int zone = envInt("COSMIC_RAID_ZONE_NUMBER", 7);
    raid::RaidRun::Test t;
    t.on = true;
    t.invincible = true;
    t.players = 5;
    raid::TestSession session(s.ws(), *b, t);
    session.setAdminSeated(false);
    session.setFreeSeatPlayed(true);
    session.setBotsInvincible(true);
    session.setSkill(raid::BotSkill::Skilled);
    session.setBotSeed(static_cast<uint32_t>(envInt("COSMIC_RAID_ZONE_SEED", 0)));
    uint32_t state = 0;
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (!o.optional && o.zone < zone && o.role != "gigaboss" && b->pairDone(o, state).empty()) state |= 1u << o.index;
    session.setBranchState(state);
    const int from = envInt("COSMIC_RAID_ZONE_FROM", zone);
    CHECK(session.begin(from));
    bool arrived = from == zone;
    const int secs = envInt("COSMIC_RAID_ZONE_SECONDS", 600);
    for (int k = 0; k < secs * kSimHz; ++k) {
        session.step(InputFrame{});
        state |= session.run().localDone();
        session.setBranchState(state);
        if (k % (10 * kSimHz) == 0) {
            raid::RaidRun& r = session.run();
            std::printf("t=%3d zone %d plan « %s »\n", k / kSimHz, r.zone(), session.bots().plan().c_str());
            for (size_t c = 0; c < r.rules().coops.size(); ++c) {
                const auto v = r.coop(c);
                std::printf("   coop %s on %d need %d done %d ambush %d (%d/%d) rect %.0f,%.0f\n", r.rules().coops[c].id.c_str(), v.on, v.need, v.done, v.ambush, v.ambushLeft,
                            v.ambushTotal, r.rules().coops[c].rect.x, r.rules().coops[c].rect.y);
            }
            const GameWorld& w = r.world();
            for (int seat = 0; seat < w.teamSize(); ++seat) {
                const auto& th = session.bots().thought(seat);
                const Player& pl = w.playerAt(seat);
                const auto& bb = session.bots().bots()[static_cast<size_t>(seat)];
                std::printf("   seat %d at %.0f,%.0f pres %d st %d gr %d lad %d v %.0f,%.0f task %d goal %.0f,%.0f gnode %d path %zu at %zu still %d noprog %d lost %d held %x : %s · %s\n",
                            seat, pl.feet().x, pl.feet().y, static_cast<int>(w.member(seat).presence), static_cast<int>(pl.state()), pl.grounded(), pl.onLadder, pl.vel.x,
                            pl.vel.y, static_cast<int>(bb.task), bb.goal.x, bb.goal.y, bb.goalNode, bb.path.size(), bb.at, bb.still, bb.noProgress, bb.lost,
                            session.frames()[static_cast<size_t>(seat)].held, th.intent.c_str(), th.move.c_str());
            }
        }
        if (!arrived && session.run().zone() == zone) {
            arrived = true;
            std::printf("arrived in zone %d at %d s\n", zone, k / kSimHz);
        }
        if (arrived && session.run().zone() != zone) {
            std::printf("left to zone %d at %d s\n", session.run().zone(), k / kSimHz);
            break;
        }
    }
}

namespace {

int branchOf(const raid::RaidSimulation& sim, const std::string& part) {
    for (size_t b = 0; b < sim.branches().size(); ++b)
        if (sim.branches()[b]->part == part) return static_cast<int>(b);
    return -1;
}

/// Puts every player of group `g` down at once (all its zones): the group is wiped at the next step.
void wipeGroup(raid::RaidSimulation& sim, int g) {
    raid::RaidRun& run = sim.group(g).session->run();
    for (int z : run.zonesLoaded()) {
        GameWorld* w = run.worldOf(z);
        if (!w) continue;
        for (int seat = 0; seat < w->teamSize(); ++seat) {
            if (w->member(seat).presence != GameWorld::Presence::Playing) continue;
            GameWorld::PlayerScope sc(*w, seat);
            w->playerAt(seat).kill(*w);
        }
    }
}

}  // namespace

TEST_CASE("raid simulation (§40): the 8th group doubles the Torse (two groups to reach Heartbreaker); a group whose Gigaboss fell waits in its branch, never pulled away to help") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 8;
    setup.bots = 2;
    setup.given = {"crane"};  // Skullmaw given as fallen: the Crâne's group has nothing left to do from the start
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);
    const int crane = branchOf(sim, "crane"), torse = branchOf(sim, "torse");
    CHECK(crane >= 0 && torse >= 0);
    if (crane < 0 || torse < 0) return;
    // Only the Torse: its lock wants its 4 plates, two pairs, and an instance never does both objectives of a pair.
    for (int b = 0; b < 7; ++b) CHECK(sim.needsTwoGroups(b) == (b == torse));
    for (int k = 0; k < 7; ++k) CHECK(sim.group(k).branch == k);
    CHECK(sim.group(7).branch == torse);
    // More groups: the Torse first, then the others in the titan's order (14: two in each branch).
    CHECK(sim.startBranch(7) == torse);
    CHECK(sim.startBranch(8) == 0);
    std::vector<int> per(7, 0);
    for (int k = 0; k < 14; ++k) ++per[static_cast<size_t>(sim.startBranch(k))];
    for (int n : per) CHECK(n == 2);
    // 30 s: the Crâne's group waits there (« en attente »); every other branch is slower and has a zone for two groups
    // (before §40 it went to help the Bras gauche after 5 s): nobody moves.
    for (int t = 0; t < 30 * kSimHz; ++t) sim.step(InputFrame{});
    CHECK(sim.group(0).waiting);
    CHECK(sim.group(0).branch == crane);
    CHECK(sim.group(0).moves == 0);
    for (int k = 1; k < 8; ++k) {
        CHECK(!sim.group(k).waiting);
        CHECK(sim.group(k).moves == 0);
    }
    CHECK(sim.groups().size() == 8u);
    bool told = false;
    for (const auto& e : sim.feed()) told = told || e.text.find("EN ATTENTE · Groupe 1 reste dans la branche") == 0;
    CHECK(told);
}

TEST_CASE("raid simulation (§40): seven groups — the Torse's lone group can never open its lock: the first waiting group becomes its second, nobody else moves") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 7;
    setup.bots = 2;
    setup.given = {"crane"};
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);
    const int torse = branchOf(sim, "torse");
    for (int t = 0; t < 2 * kSimHz; ++t) sim.step(InputFrame{});
    CHECK(sim.group(0).branch == torse);  // the Crâne's waiting group: the Torse's second group
    CHECK(!sim.group(0).waiting);
    CHECK(sim.group(0).moves == 1);
    for (int t = 0; t < 10 * kSimHz; ++t) sim.step(InputFrame{});
    for (int k = 1; k < 7; ++k) CHECK(sim.group(k).moves == 0);  // the others are only slow: never helped
    bool told = false;
    for (const auto& e : sim.feed()) told = told || e.text == "Groupe 1 quitte sa branche pour la branche TORSE : un groupe seul n'en atteint jamais le Gigaboss";
    CHECK(told);
}

TEST_CASE("raid simulation (§40): a waiting group leaves its branch only to replace a wiped group, where it fell; nobody waiting: the zone waits, no group from nowhere") {
    Svc s;
    {
        // Nobody waits: the wiped group's zone stays to take, no new players appear, nobody moves.
        raid::SimSetup setup;
        setup.groups = 3;
        setup.bots = 2;
        setup.invincible = false;
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        sim.watch(0);
        for (int t = 0; t < kSimHz; ++t) sim.step(InputFrame{});
        wipeGroup(sim, 1);
        for (int t = 0; t < 3 * kSimHz; ++t) sim.step(InputFrame{});
        CHECK(sim.group(1).wiped);
        CHECK(sim.vacancies().size() == 1u);
        CHECK(sim.groups().size() == 3u);
        CHECK(sim.group(0).moves == 0 && sim.group(2).moves == 0);
    }
    raid::SimSetup setup;
    setup.groups = 8;
    setup.bots = 2;
    setup.invincible = false;
    setup.given = {"crane"};
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);
    const int brasDroit = branchOf(sim, "bras_droit");
    CHECK(sim.group(2).branch == brasDroit);
    // The Bras droit's group out of its armory, in its first zone.
    int t = 0;
    for (; t < 90 * kSimHz; ++t) {
        sim.step(InputFrame{});
        const int z = sim.group(2).session->run().zone();
        if (z >= 1 && z != raid::kLairArmory && t > 2 * kSimHz) break;
    }
    CHECK(t < 90 * kSimHz);
    CHECK(sim.group(0).waiting);
    CHECK(sim.group(0).moves == 0);
    wipeGroup(sim, 2);
    int wipedZone = -1;
    for (int k = 0; k < 5 * kSimHz && wipedZone < 0; ++k) {
        sim.step(InputFrame{});
        if (sim.group(2).wiped) wipedZone = sim.group(2).session->run().wipedZone();
    }
    CHECK(wipedZone >= 1);
    for (int k = 0; k < kSimHz; ++k) sim.step(InputFrame{});  // the next dispatch (twice a second)
    // The waiting group replaced it, where it fell, with the branch's validated state; the others never moved.
    const raid::RaidSimulation::Group& g = sim.group(0);
    CHECK(g.branch == brasDroit);
    CHECK(!g.waiting);
    CHECK(g.relieves == 3);
    CHECK(g.moves == 1);
    CHECK(g.session->run().zone() == wipedZone);
    CHECK(g.session->branchState() == sim.mask(brasDroit));
    CHECK(sim.vacancies().empty());
    CHECK(sim.groups().size() == 8u);  // no group from nowhere
    for (int k = 1; k < 8; ++k)
        if (k != 2) CHECK(sim.group(k).moves == 0);
    bool told = false;
    for (const auto& e : sim.feed()) told = told || e.text.find("REMPLACEMENT · Groupe 1 quitte sa branche pour remplacer Groupe 3") == 0;
    CHECK(told);
}

TEST_CASE("raid simulation (§40): the seven fallen, every group alive goes to the Cœur, each in its own instance; ONE health: every instance's part summed, given to the others") {
    Svc s;
    raid::SimSetup setup;
    setup.groups = 3;
    setup.bots = 3;
    setup.lairs = true;  // the heart's arena
    setup.botsInvincible = true;
    setup.given = {"crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"};
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);
    sim.step(InputFrame{});
    CHECK(sim.heartOpen());
    CHECK(sim.gigabossesDown() == 7);
    for (int g = 0; g < 3; ++g) {
        CHECK(sim.group(g).branch == sim.heartIndex());
        CHECK(!sim.group(g).waiting);
    }
    // Every instance in the fight (its heart awake).
    auto engaged = [&](int g) {
        const raid::RaidRun& r = sim.group(g).session->run();
        return r.heartRoutine() != nullptr && r.lairGigaboss().engaged();
    };
    int t = 0;
    for (; t < 120 * kSimHz && !(engaged(0) && engaged(1) && engaged(2)); ++t) sim.step(InputFrame{});
    CHECK(t < 120 * kSimHz);
    if (!(engaged(0) && engaged(1) && engaged(2))) return;
    // Two instances strike hard (12 % and 15 % of its health): the shared phase passes 75 % for everyone.
    auto strike = [&](int g, float share) {
        raid::RaidRun& r = sim.group(g).session->run();
        GameWorld& w = r.world();
        const Enemy* heart = r.lairGigaboss().boss(w);
        CHECK(heart != nullptr);
        if (Enemy* b = heart ? w.findEnemy(heart->uid()) : nullptr) b->wound(w, share * b->maxHp());
    };
    strike(0, 0.12f);
    strike(1, 0.15f);
    for (int k = 0; k < 3; ++k) sim.step(InputFrame{});
    int sum = 0;
    for (int g = 0; g < 3; ++g) sum += sim.group(g).session->run().heartRoutine()->own();
    std::printf("  le Cœur à trois instances : ôté %d (parts %d + %d + %d), phase partagée %d\n", sim.heartTaken(), sim.group(0).session->run().heartRoutine()->own(),
                sim.group(1).session->run().heartRoutine()->own(), sim.group(2).session->run().heartRoutine()->own(), sim.heartPhase());
    CHECK(sim.heartTaken() == std::min(raid::kHeartUnits, sum));  // one health: the parts summed
    CHECK(sim.heartTaken() >= 2700);
    CHECK(sim.heartPhase() == 2);
    for (int g = 0; g < 3; ++g) {
        const raid::AsterionHeartRoutine& hr = *sim.group(g).session->run().heartRoutine();
        // What each instance shows is that same health (its part and the others'), within a step of blows.
        CHECK(std::abs(hr.remaining() - (raid::kHeartUnits - sim.heartTaken())) <= 60);
        CHECK(std::abs(hr.others() - (sim.heartTaken() - hr.own())) <= 60);
    }
    // The instance that struck nothing has its heart at the shared phase too (never above it).
    const raid::RaidRun& third = sim.group(2).session->run();
    const Enemy* b = third.lairGigaboss().boss(third.world());
    CHECK(b != nullptr);
    if (b) CHECK(b->phaseIndex() + 1 >= 2);
}
