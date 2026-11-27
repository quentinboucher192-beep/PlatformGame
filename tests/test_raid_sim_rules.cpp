// « Simuler un raid complet » (raid::RaidSimulation, docs/RAIDS.md §22, §40 and §41): the rules between the groups.
//
// * §41 (titan 16): ALWAYS seven groups, group k in branch k, never two in a branch; the raid's 14 to 56 players spread
//   over them (2 to 8 each, as even as possible).
// * A group whose branch's Gigaboss fell STAYS in its branch, « en attente »; it never goes to help a group that
//   is only slow or weakened. It goes to another branch ONLY to replace a group wiped there (it takes the zone back
//   where that group fell, the branch's validated state kept). Nobody waiting: the zone waits.
// * A pair of objectives (each branch's zone 4, the Torse's plates) is done by ONE group split in two, both halves
//   within 3 s (raid::RaidRun); the simulated players split for it.
// * The seven Gigaboss fallen: every group still in play goes to the Cœur, each in its own instance, and the Cœur has
//   ONE health for all of them (each instance's part summed, never a bar per instance).
// * The raid lasts 60 minutes: the Cœur standing then, it is lost; every group wiped: lost at once.
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

TEST_CASE("raid simulation bench (§41): the full raid from the armories, where each group is and what blocks it (COSMIC_RAID_SIM_BENCH)") {
    // A measure, not a check (an hour of play): only with COSMIC_RAID_SIM_BENCH set. §41: always seven groups;
    // COSMIC_RAID_SIM_PLAYERS (35: 5 per group), COSMIC_RAID_SIM_SKILL (1: Confirmé), COSMIC_RAID_SIM_MINUTES (90: the
    // measure goes on past the raid's 60 min), COSMIC_RAID_SIM_INVINCIBLE (the simulated players invincible),
    // COSMIC_RAID_SIM_EVERY (seconds between two lines of each group, 120).
    if (!std::getenv("COSMIC_RAID_SIM_BENCH")) {
        std::printf("  (mesure : COSMIC_RAID_SIM_BENCH=1)\n");
        return;
    }
    Svc s;
    raid::SimSetup setup;
    setup.players = envInt("COSMIC_RAID_SIM_PLAYERS", 35);
    setup.fullGroups = true;  // the menu's: every place played
    setup.minutes = 0;        // the measure goes on past the raid's 60 min (said at the end)
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
    std::printf("  == %s en %s de jeu (%s) · %d / 7 Gigaboss · Cœur %d %% ôtés · %zu groupes · %d morts · %d sauvetages · %.0f s de calcul\n",
                sim.heartFallen() ? "Cœur tombé" : "PAS FINI", clock(sim.tick()).c_str(),
                sim.heartFallen() && sim.tick() <= 3600u * kSimHz ? "victoire dans les 60 min" : "défaite à 60 min : temps écoulé", sim.gigabossesDown(),
                sim.heartTaken() / 100, sim.groups().size(), sim.botDeaths(), sim.rescues(), ms / 1000.0);
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

namespace {

/// No branch ever has two groups in play (§41: one group per branch; the heart apart, every group in its own instance).
bool oneGroupPerBranch(const raid::RaidSimulation& sim) {
    std::vector<int> per(sim.branches().size(), 0);
    for (const auto& g : sim.groups())
        if (!g->wiped && g->branch != sim.heartIndex()) ++per[static_cast<size_t>(g->branch)];
    for (int n : per)
        if (n > 1) return false;
    return true;
}

}  // namespace

TEST_CASE("raid simulation (§41): always seven groups, group k in branch k, 2 to 8 each as even as possible (14 and 56 players); never two in a branch") {
    Svc s;
    for (const int players : {14, 20, 56}) {
        raid::SimSetup setup;
        setup.players = players;
        setup.fullGroups = true;
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        sim.watch(0);
        CHECK(sim.groups().size() == 7u);
        int total = 0, lo = 99, hi = 0;
        for (int k = 0; k < 7; ++k) {
            const auto& g = sim.group(k);
            CHECK(g.branch == k);
            CHECK(g.seats == g.session->players());
            total += g.seats;
            lo = std::min(lo, g.seats);
            hi = std::max(hi, g.seats);
        }
        CHECK(total == players);
        CHECK(hi - lo <= 1);
        CHECK(lo >= 2);
        CHECK(hi <= 8);
        // Some seconds of play (every group out of its armory): still seven, one per branch, nobody moved.
        for (int t = 0; t < 20 * kSimHz; ++t) {
            sim.step(InputFrame{});
            if (t % kSimHz == 0) CHECK(oneGroupPerBranch(sim));
        }
        CHECK(sim.groups().size() == 7u);
        for (int k = 0; k < 7; ++k) CHECK(sim.group(k).moves == 0);
        std::printf("  %d joueurs : 7 groupes de %d à %d\n", players, lo, hi);
    }
    // Out of bounds: 14 to 56.
    raid::SimSetup few;
    few.players = 3;
    raid::RaidSimulation small(s.ws(), few);
    CHECK(small.setup().players == 14);
    raid::SimSetup many;
    many.players = 99;
    raid::RaidSimulation big(s.ws(), many);
    CHECK(big.setup().players == 56);
}

TEST_CASE("raid run (§41): a pair is done by ONE group, both halves within 3 s; a half alone closes again; the other half validated: it completes alone") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin("bras_droit");
    CHECK(b != nullptr);
    if (!b) return;
    const int west = b->objectiveIndex("forge_soufflet_ouest"), east = b->objectiveIndex("forge_soufflet_est");
    CHECK(west >= 0 && east >= 0);
    uint32_t before = 0;  // every objective before the Coude-forge (zone 4), as done by the group
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (!o.optional && o.zone < 4 && o.role != "gigaboss") before |= 1u << o.index;
    auto hit = [&](raid::TestSession& t, const char* id) {
        GameWorld* w = t.run().worldOf(4);
        CHECK(w != nullptr);
        if (!w) return;
        std::string source;
        for (const raid::ZoneObjective& zo : t.run().rulesOf(4)->objectives)
            if (zo.index == (std::string(id) == "west" ? west : east)) source = zo.source;
        LevelEntity* e = w->entity(source);
        CHECK(e != nullptr);
        if (e) e->onPowerHit(*w, "wave", e->box);
    };
    auto session = [&] {
        raid::RaidRun::Test test;
        test.on = true;
        test.invincible = true;
        test.players = 2;
        auto t = std::make_unique<raid::TestSession>(s.ws(), *b, test);
        t->setAdminSeated(false);
        t->setBranchState(before);
        CHECK(t->begin(4));
        return t;
    };
    {
        // One half alone: armed for 3 s, nothing counted; then it closes again (its receptor off: hit again, it arms again).
        auto t = session();
        for (int k = 0; k < 10; ++k) t->step(InputFrame{});
        hit(*t, "west");
        t->step(InputFrame{});
        CHECK(((t->run().pairArmed() >> west) & 1u) != 0);
        CHECK((t->run().localDone() & ((1u << west) | (1u << east))) == 0u);
        CHECK(t->run().pairStepsLeft(west) > 0);
        for (int k = 0; k < 3 * kSimHz + 2; ++k) t->step(InputFrame{});
        CHECK(t->run().pairArmed() == 0u);
        CHECK((t->run().localDone() & (1u << west)) == 0u);
        bool late = false;
        for (const raid::Alert& a : t->run().alerts()) late = late || a.text.find("Trop tard") != std::string::npos;
        CHECK(late);
        // Both now, 2,5 s apart: both done, by this group.
        hit(*t, "west");
        t->step(InputFrame{});
        CHECK(((t->run().pairArmed() >> west) & 1u) != 0);  // the receptor was off again: it armed again
        for (int k = 0; k < 300; ++k) t->step(InputFrame{});
        hit(*t, "east");
        t->step(InputFrame{});
        CHECK((t->run().localDone() & (1u << west)) != 0u);
        CHECK((t->run().localDone() & (1u << east)) != 0u);
        CHECK(t->run().pairArmed() == 0u);
    }
    {
        // The other half validated for the branch already (a raid of before): this one completes alone.
        auto t = session();
        t->setBranchState(before | (1u << west));
        for (int k = 0; k < 10; ++k) t->step(InputFrame{});
        hit(*t, "east");
        t->step(InputFrame{});
        CHECK((t->run().localDone() & (1u << east)) != 0u);
    }
}

TEST_CASE("raid bots (§41): the group splits on a pair — both bellows of the Coude-forge at the same time, by two simulated players") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin("bras_droit");
    CHECK(b != nullptr);
    if (!b) return;
    const int west = b->objectiveIndex("forge_soufflet_ouest"), east = b->objectiveIndex("forge_soufflet_est");
    uint32_t state = 0;
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (!o.optional && o.zone < 4 && o.role != "gigaboss") state |= 1u << o.index;
    for (const int players : {2, 4}) {
        raid::RaidRun::Test test;
        test.on = true;
        test.invincible = true;
        test.players = players;
        raid::TestSession t(s.ws(), *b, test);
        t.setAdminSeated(false);
        t.setFreeSeatPlayed(true);
        t.setBotsInvincible(true);
        t.setSkill(raid::BotSkill::Skilled);
        t.setBranchState(state);
        CHECK(t.begin(4));
        uint32_t doneAt = 0;
        for (uint32_t k = 0; k < 240u * kSimHz && !doneAt; ++k) {
            t.step(InputFrame{});
            if ((t.run().localDone() >> west & 1u) && (t.run().localDone() >> east & 1u)) doneAt = t.run().tick();
        }
        std::printf("  %d joueurs simulés : les deux soufflets ensemble en %.1f s\n", players, doneAt / static_cast<double>(kSimHz));
        CHECK(doneAt > 0);
    }
}

TEST_CASE("raid simulation (§41): a group whose Gigaboss fell waits in its branch; it leaves only to replace a wiped group, where it fell; nobody waiting: the zone waits") {
    Svc s;
    {
        // Nobody waits: the wiped group's zone stays to take, no new players appear, nobody moves.
        raid::SimSetup setup;
        setup.players = 14;
        setup.invincible = false;
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        sim.watch(0);
        for (int t = 0; t < kSimHz; ++t) sim.step(InputFrame{});
        wipeGroup(sim, 1);
        for (int t = 0; t < 3 * kSimHz; ++t) sim.step(InputFrame{});
        CHECK(sim.group(1).wiped);
        CHECK(sim.vacancies().size() == 1u);
        CHECK(sim.groups().size() == 7u);
        for (int k = 0; k < 7; ++k) CHECK(sim.group(k).moves == 0);
        CHECK(sim.outcome() == raid::RaidSimulation::Outcome::Running);
    }
    raid::SimSetup setup;
    setup.players = 21;
    setup.invincible = false;
    setup.given = {"crane"};  // Skullmaw given as fallen: the Crâne's group has nothing left to do from the start
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);
    const int crane = branchOf(sim, "crane"), brasDroit = branchOf(sim, "bras_droit");
    CHECK(sim.group(2).branch == brasDroit);
    // 30 s: the Crâne's group waits there (« en attente »), every other branch is slower: nobody moves (never a help).
    for (int t = 0; t < 30 * kSimHz; ++t) {
        sim.step(InputFrame{});
        if (t % kSimHz == 0) CHECK(oneGroupPerBranch(sim));
    }
    CHECK(sim.group(0).waiting);
    CHECK(sim.group(0).branch == crane);
    for (int k = 0; k < 7; ++k) CHECK(sim.group(k).moves == 0);
    bool told = false;
    for (const auto& e : sim.feed()) told = told || e.text.find("EN ATTENTE · Groupe 1 reste dans la branche") == 0;
    CHECK(told);
    // The Bras droit's group out of its armory, in its first zone, then wiped.
    int t = 0;
    for (; t < 90 * kSimHz; ++t) {
        sim.step(InputFrame{});
        const int z = sim.group(2).session->run().zone();
        if (z >= 1 && z != raid::kLairArmory) break;
    }
    CHECK(t < 90 * kSimHz);
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
    CHECK(g.seats == 3);  // it keeps its size
    CHECK(sim.vacancies().empty());
    CHECK(sim.groups().size() == 7u);  // no group from nowhere
    CHECK(oneGroupPerBranch(sim));
    for (int k = 1; k < 7; ++k)
        if (k != 2) CHECK(sim.group(k).moves == 0);
    told = false;
    for (const auto& e : sim.feed()) told = told || e.text.find("REMPLACEMENT · Groupe 1 quitte sa branche pour remplacer Groupe 3") == 0;
    CHECK(told);
}

TEST_CASE("raid simulation (§41): defeat — « temps écoulé » at the end of its minutes with the Cœur standing, « tous les groupes éliminés » at once") {
    Svc s;
    {
        raid::SimSetup setup;
        setup.players = 14;
        setup.minutes = 1;  // the standard raid lasts 60; one minute here
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        CHECK(sim.deadline() == 60u * kSimHz);
        sim.watch(0);
        for (int t = 0; t < 61 * kSimHz; ++t) sim.step(InputFrame{});
        CHECK(sim.outcome() == raid::RaidSimulation::Outcome::DefeatTime);
        CHECK(sim.endedAt() == 60u * kSimHz);
        const uint32_t frozen = sim.tick();
        sim.step(InputFrame{});
        CHECK(sim.tick() == frozen);  // the raid is over: nothing steps any more
        bool told = false;
        for (const auto& e : sim.feed()) told = told || e.text.find("DÉFAITE · Temps écoulé") == 0;
        CHECK(told);
        // The standard raid: 60 minutes.
        raid::SimSetup standard;
        CHECK(standard.minutes == 60);
    }
    {
        raid::SimSetup setup;
        setup.players = 14;
        setup.invincible = false;
        raid::RaidSimulation sim(s.ws(), setup);
        CHECK(sim.begin());
        sim.watch(0);
        for (int t = 0; t < kSimHz; ++t) sim.step(InputFrame{});
        for (int k = 0; k < 6; ++k) wipeGroup(sim, k);
        for (int t = 0; t < kSimHz; ++t) sim.step(InputFrame{});
        CHECK(sim.outcome() == raid::RaidSimulation::Outcome::Running);  // one group still plays
        wipeGroup(sim, 6);
        for (int t = 0; t < kSimHz; ++t) sim.step(InputFrame{});
        CHECK(sim.outcome() == raid::RaidSimulation::Outcome::DefeatWiped);
        bool told = false;
        for (const auto& e : sim.feed()) told = told || e.text.find("DÉFAITE · Tous les groupes sont éliminés") == 0;
        CHECK(told);
    }
}

TEST_CASE("raid simulation (§41): the seven fallen, every group alive goes to the Cœur, each in its own instance; ONE health: every instance's part summed, given to the others") {
    Svc s;
    raid::SimSetup setup;
    setup.players = 21;  // seven groups of 3
    setup.lairs = true;  // the heart's arena
    setup.botsInvincible = true;
    setup.given = {"crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"};
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);
    sim.step(InputFrame{});
    CHECK(sim.heartOpen());
    CHECK(sim.gigabossesDown() == 7);
    for (int g = 0; g < 7; ++g) {
        CHECK(sim.group(g).branch == sim.heartIndex());
        CHECK(!sim.group(g).waiting);
    }
    // Every instance in the fight (its heart awake).
    auto engaged = [&](int g) {
        const raid::RaidRun& r = sim.group(g).session->run();
        return r.heartRoutine() != nullptr && r.lairGigaboss().engaged();
    };
    auto all = [&] {
        for (int g = 0; g < 7; ++g)
            if (!engaged(g)) return false;
        return true;
    };
    int t = 0;
    for (; t < 120 * kSimHz && !all(); ++t) sim.step(InputFrame{});
    CHECK(t < 120 * kSimHz);
    if (!all()) return;
    // Two instances strike hard (12 % and 15 % of its health): the shared phase passes 25 % for everyone.
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
    for (int g = 0; g < 7; ++g) sum += sim.group(g).session->run().heartRoutine()->own();
    std::printf("  le Cœur à sept instances : ôté %d (somme des parts %d), phase partagée %d\n", sim.heartTaken(), sum, sim.heartPhase());
    CHECK(sim.heartTaken() == std::min(raid::kHeartUnits, sum));  // one health: the parts summed
    CHECK(sim.heartTaken() >= 2700);
    CHECK(sim.heartPhase() == 2);
    for (int g = 0; g < 7; ++g) {
        const raid::AsterionHeartRoutine& hr = *sim.group(g).session->run().heartRoutine();
        // What each instance shows is that same health (its part and the others'), within a step of blows.
        CHECK(std::abs(hr.remaining() - (raid::kHeartUnits - sim.heartTaken())) <= 60);
        CHECK(std::abs(hr.others() - (sim.heartTaken() - hr.own())) <= 60);
    }
    // An instance that struck nothing has its heart at the shared phase too (never above it).
    const raid::RaidRun& third = sim.group(2).session->run();
    const Enemy* b = third.lairGigaboss().boss(third.world());
    CHECK(b != nullptr);
    if (b) CHECK(b->phaseIndex() + 1 >= 2);
}
