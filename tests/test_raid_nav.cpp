// 2.79 (docs/RAIDS.md, « Joueurs simulés : terrain et anticipation »): the simulated players move like a
// very good player — the proof:
//
// * the light copy of the physics (raid::nav::BodySim) plays exactly as the game's Player: the same inputs,
//   the same positions, step after step (running, jumps held, double and triple jump, dash, impulse, wall
//   jump, crouch, a drop through a one-way platform, a moving platform carrying it);
// * the zone's graph: its moves through the air found by simulating that physics (simulations, steps,
//   time, memory, measured);
// * in EVERY zone of EVERY branch of Asterion (the armory included), an Expert simulated player goes from
//   the start to each objective, then to the way out, without the terrain killing it (the void, a deadly
//   liquid) nor touching a danger, within a time limit — the table of the times;
// * aimed tests: a moving platform reached when it is there, a gap crossed with the double jump (a jump
//   alone does not), the rising magma kept below, a shot dodged;
// * the same digest on every run (2 and 8 seats), the cost of the lookahead per step.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/Projectile.h"
#include "game/gameplay/entities/Entities.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidTest.h"
#include "game/raid/nav/BodySim.h"
#include "game/raid/nav/Intent.h"
#include "game/raid/nav/Maneuver.h"
#include "game/raid/nav/Pilot.h"
#include "game/raid/nav/Terrain.h"
#include "tests/TestFramework.h"

using namespace cosmic;
using namespace cosmic::raid;

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

constexpr int kSeat = 1;  // the simulated player of these tests (seat 0: the administrator's, vacant)

InputFrame frameOf(const nav::Pad& pad, uint32_t& prevHeld) {
    InputFrame f;
    f.held = pad.held;
    if (pad.mx > 0) f.held |= 1u << static_cast<int>(Action::MoveRight);
    if (pad.mx < 0) f.held |= 1u << static_cast<int>(Action::MoveLeft);
    f.pressed = f.held & ~prevHeld;
    f.released = prevHeld & ~f.held;
    f.moveX = pad.mx;
    f.moveY = pad.my;
    prevHeld = f.held;
    return f;
}

/// One step of a run with seat 1 played by `f`, the other seats vacant.
void stepRun(RaidRun& run, const InputFrame& f, uint32_t state) {
    InputFrame frames[kMaxInstancePlayers];
    for (int s = 0; s < kMaxInstancePlayers; ++s) frames[s].flags = InputFrame::Vacant;
    frames[kSeat] = f;
    if (const auto& offer = run.world().powerOffer(); offer && offer->player == kSeat) frames[kSeat].choice = InputFrame::kChoiceLeave;
    run.step(frames, kMaxInstancePlayers, state);
}

uint32_t allDone(const Branch& br) {
    uint32_t s = 0;
    for (const ObjectiveInfo& o : br.objectives) s |= 1u << o.index;
    for (const EffectInfo& e : br.effects)
        if (e.bit >= 0 && e.bit < 32) s |= 1u << e.bit;
    return s;
}

std::unique_ptr<RaidRun> zoneRun(Svc& s, const Branch& br, int zone, uint32_t state) {
    RaidRun::Test t;
    t.on = true;
    t.invincible = true;
    t.players = 2;
    auto run = std::make_unique<RaidRun>(s.ws(), br, kMaxInstancePlayers);
    run->setTest(t);
    run->setQuiet(true);
    if (!run->begin(state, zone)) return nullptr;
    return run;
}

/// Seat 1 driven by the navigation (raid::nav::Navigator) toward targets, as RaidBots drives it.
struct Driver {
    nav::Navigator navi;
    std::vector<int> path;
    size_t at = 0;
    int planned = -100000;
    float best = 1e30f;
    int noProgress = 0;
    uint32_t prevHeld = 0;
    uint32_t tick = 0;
    explicit Driver(int level = 2) { navi.setSkill(level); }
    bool mayLeave = false;
    InputFrame step(RaidRun& run, Vec2 goal, int goalNode, std::vector<Rect> blows = {}) {
        navi.update(run, tick, blows);
        if (navi.graphChanged()) path.clear();
        nav::SeatPlan plan;
        plan.goal = goal;
        plan.goalNode = goalNode;
        plan.stop = 4.0f;
        plan.path = &path;
        plan.at = &at;
        plan.planned = &planned;
        plan.best = &best;
        plan.noProgress = &noProgress;
        plan.mayLeave = mayLeave;
        const nav::Pad pad = navi.steer(run.world(), kSeat, plan, prevHeld);
        ++tick;
        return frameOf(pad, prevHeld);
    }
};

}  // namespace

// ============================================================================ the copy of the physics

TEST_CASE("raid nav (2.79): the light copy of the physics plays exactly as the game's player (jumps, double jump, dash, impulse, wall jump, drop, crouch)") {
    Svc s;
    const Branch* br = Branch::builtin("bras_droit");
    CHECK(br != nullptr);
    struct Script {
        const char* name;
        std::vector<nav::Pad> pads;
    };
    auto pad = [](int mx, int my, bool jump, int key = -1) {
        nav::Pad p;
        p.mx = static_cast<int8_t>(mx * 127);
        p.my = static_cast<int8_t>(my * 127);
        if (jump) p.held |= 1u << static_cast<int>(Action::Jump);
        if (key >= 0) p.held |= 1u << key;
        if (mx > 0) p.held |= 1u << static_cast<int>(Action::MoveRight);
        if (mx < 0) p.held |= 1u << static_cast<int>(Action::MoveLeft);
        if (my > 0) p.held |= 1u << static_cast<int>(Action::MoveDown);
        if (my < 0) p.held |= 1u << static_cast<int>(Action::MoveUp);
        return p;
    };
    double worst = 0.0;
    int compared = 0;
    for (int zone : {1, 2, 3, 6}) {
        auto run = zoneRun(s, *br, zone, 0);
        CHECK(run);
        // A few steps to settle (the zone's start).
        for (int i = 0; i < 30; ++i) stepRun(*run, {}, 0);
        const Player& me = run->world().playerAt(kSeat);
        const nav::Kit kit = nav::Kit::of(me);
        CHECK(kit.hasDash());
        CHECK(kit.hasImpulse());
        std::vector<Script> scripts;
        {
            Script sc{"course, saut tenu, double saut, ruée, impulsion", {}};
            for (int i = 0; i < 30; ++i) sc.pads.push_back(pad(1, 0, false));
            for (int i = 0; i < 24; ++i) sc.pads.push_back(pad(1, 0, true));
            for (int i = 0; i < 8; ++i) sc.pads.push_back(pad(1, 0, false));
            for (int i = 0; i < 20; ++i) sc.pads.push_back(pad(1, 0, true));  // the double jump
            sc.pads.push_back(pad(1, -1, false, kit.dashKey));                   // a dash, up and forward
            for (int i = 0; i < 30; ++i) sc.pads.push_back(pad(-1, 0, false));
            sc.pads.push_back(pad(0, 0, false, kit.impulseKey));  // the impulse
            for (int i = 0; i < 90; ++i) sc.pads.push_back(pad(i < 30 ? -1 : 1, 0, i % 50 < 10));
            for (int i = 0; i < 30; ++i) sc.pads.push_back(pad(0, 1, false));  // crouched
            for (int i = 0; i < 40; ++i) sc.pads.push_back(pad(1, i > 20 ? 1 : 0, false));  // a slide
            scripts.push_back(sc);
        }
        {
            Script sc{"saut court, ruée au sol puis saut (saut allongé), chute rapide", {}};
            for (int i = 0; i < 6; ++i) sc.pads.push_back(pad(1, 0, true));
            for (int i = 0; i < 50; ++i) sc.pads.push_back(pad(i < 25 ? 1 : -1, 0, false));
            sc.pads.push_back(pad(1, 0, false, kit.dashKey));
            sc.pads.push_back(pad(1, 0, false));
            sc.pads.push_back(pad(1, 0, true));
            for (int i = 0; i < 60; ++i) sc.pads.push_back(pad(1, i > 30 ? 1 : 0, i < 20));
            scripts.push_back(sc);
        }
        for (const Script& sc : scripts) {
            auto r = zoneRun(s, *br, zone, 0);
            for (int i = 0; i < 30; ++i) stepRun(*r, {}, 0);
            GameWorld& w = r->world();
            w.playerAt(kSeat).setInvulnerable(60.0f);  // nothing but the inputs moves it
            nav::Terrain terrain;
            terrain.sync(w, r.get());
            terrain.plan(static_cast<int>(sc.pads.size()) + 2, 0, w.playerAt(kSeat).feet());
            const nav::Kit k = nav::Kit::of(w.playerAt(kSeat));
            const nav::BodySim sim(w.playerAt(kSeat).config(), k);
            nav::Body body = nav::Body::of(w.playerAt(kSeat));
            nav::TerrainArena arena(terrain);
            arena.begin();
            uint32_t prev = 0;
            double err = 0.0;
            for (size_t i = 0; i < sc.pads.size(); ++i) {
                nav::Pad p = sc.pads[i];
                p.pressed = p.held & ~prev;
                InputFrame f;
                f.held = p.held;
                f.pressed = p.pressed;
                f.released = prev & ~p.held;
                f.moveX = p.mx;
                f.moveY = p.my;
                prev = p.held;
                arena.advance(body, sim, static_cast<int>(i) + 1);
                sim.step(body, arena.physics(), p);
                stepRun(*r, f, 0);
                const Player& real = w.playerAt(kSeat);
                err = std::max(err, static_cast<double>((real.feet() - body.feet()).length()) + std::fabs(real.vel.y - body.vel.y) * 0.01);
                ++compared;
            }
            arena.end();
            std::printf("  zone %d, %s : %zu pas, écart maximal %.5f px\n", zone, sc.name, sc.pads.size(), err);
            worst = std::max(worst, err);
        }
    }
    std::printf("  copie de la physique : %d pas comparés, écart maximal %.5f px\n", compared, worst);
    CHECK(worst < 0.01);
}

// ============================================================================ the graph

TEST_CASE("raid nav (2.79): the zone's graph — its moves found by simulating the physics (double jumps, dashes…), its cost and memory measured, the same twice") {
    Svc s;
    for (const char* part : {"bras_droit", "jambe_gauche", "crane"}) {
        const Branch* br = Branch::builtin(part);
        CHECK(br != nullptr);
        for (int zone : {1, 2, 3, 7}) {
            auto run = zoneRun(s, *br, zone, 0);
            CHECK(run);
            const std::string periodic = run->rules().magma.present ? run->rules().magma.zone : std::string();
            NavGraph a, b;
            a.build(run->world(), periodic);
            b.build(run->world(), periodic);
            CHECK(a.hash() == b.hash());
            CHECK(a.size() > 30);
            const auto& st = a.stats();
            std::printf("  %s zone %d : %d places, %zu liaisons, %d paires essayées, %d simulations (%lld pas), %.1f ms, %.0f Ko ; sauts %d, doubles %d, "
                        "triples %d, ruées %d, impulsions %d, muraux %d, chutes %d\n",
                        part, zone, a.size(), a.edgeCount(), st.pairs, st.simulations, static_cast<long long>(st.bodySteps), a.buildMs(),
                        static_cast<double>(st.bytes) / 1024.0, st.byStyle[static_cast<int>(nav::Style::Jump)],
                        st.byStyle[static_cast<int>(nav::Style::DoubleJump)], st.byStyle[static_cast<int>(nav::Style::TripleJump)],
                        st.byStyle[static_cast<int>(nav::Style::Dash)] + st.byStyle[static_cast<int>(nav::Style::LongJump)],
                        st.byStyle[static_cast<int>(nav::Style::Impulse)], st.byStyle[static_cast<int>(nav::Style::WallJump)],
                        st.byStyle[static_cast<int>(nav::Style::Drop)] + st.byStyle[static_cast<int>(nav::Style::DropThrough)]);
        }
    }
}

// ============================================================================ every zone of every branch

namespace {

struct ZoneResult {
    std::string branch, zone;
    int targets = 0, reached = 0, deaths = 0, touches = 0, steps = 0, rescues = 0;
    double ms = 0.0;
    std::string missed;
    nav::Pilot::Used used;
    bool ok() const { return reached == targets && deaths == 0 && touches == 0; }
};

/// An Expert simulated player alone (seat 1), the objectives of the branch done (its doors open, its
/// arenas cleared: nothing but the terrain stands in its way), invulnerable to the enemies' blows (what is
/// counted is the terrain): from the start to each objective (and each plate of a lock, held until it
/// opens), then into the way out. Counted: the targets reached, its deaths (the void, a deadly liquid),
/// its touches of a danger of the terrain (spikes, a damage zone, the rising liquid).
ZoneResult runZone(Svc& s, const Branch& br, int zone, int level = 2) {
    ZoneResult res;
    res.branch = br.part;
    const ZoneInfo* zi = br.zone(zone);
    res.zone = zone == 0 ? std::string("Vestiaire") : zi ? zi->name : std::to_string(zone);
    const uint32_t state = allDone(br);
    auto run = zoneRun(s, br, zone == 0 ? RaidRun::kArmory : zone, state);
    if (!run) {
        res.missed = "zone introuvable";
        res.targets = 1;
        return res;
    }
    const int startZone = run->zone();
    const ZoneRules& rules = run->rules();
    struct Target {
        std::string what;
        Rect area;
        int kind = 0;  // 0 a place, 1 a plate held until done, 2 the way out
        size_t coop = 0;
    };
    std::vector<Target> todo;
    auto objectRect = [&](const std::string& id, Rect& out) {
        for (const LevelObject& o : run->world().level().objects)
            if (o.id == id) {
                out = o.rect;
                return true;
            }
        return false;
    };
    for (const ZoneObjective& zo : rules.objectives) {
        Rect r;
        if (objectRect(zo.source, r)) todo.push_back({zo.id, r, 0, 0});
    }
    if (zone == 0)
        for (const auto& e : run->world().entities())
            if (auto* t = dynamic_cast<const PowerUnlockEntity*>(e.get()); t && !e->removed) todo.push_back({"tablette " + t->power(), e->box, 0, 0});
    for (size_t k = 0; k < rules.coops.size(); ++k) {
        // 2.78: the plate of a passage of the group (« Prêts ») is the way out itself (below), not a plate to hold first.
        bool groupPlate = false;
        for (const Passage& p : rules.passages) groupPlate = groupPlate || p.plate == rules.coops[k].id;
        if (!groupPlate) todo.push_back({rules.coops[k].label, rules.coops[k].rect, 1, k});
    }
    // The order of a player: always the nearest next (from the start).
    {
        Vec2 from = run->world().playerAt(kSeat).feet();
        std::vector<Target> ordered;
        while (!todo.empty()) {
            size_t best = 0;
            for (size_t i = 1; i < todo.size(); ++i)
                if (std::fabs(todo[i].area.center().x - from.x) < std::fabs(todo[best].area.center().x - from.x)) best = i;
            from = todo[best].area.bottomCenter();
            ordered.push_back(todo[best]);
            todo.erase(todo.begin() + static_cast<std::ptrdiff_t>(best));
        }
        todo = ordered;
    }
    // The way out: toward the deepest zone of the branch (the lair last), a secret zone only when alone.
    int exit = -1;
    for (size_t k = 0; k < rules.passages.size(); ++k) {
        const Passage& p = rules.passages[k];
        if (!run->passageOpen(p)) continue;
        const ZoneInfo* to = br.zone(p.to);
        const bool secret = to && to->secret;
        if (exit < 0) {
            exit = static_cast<int>(k);
            continue;
        }
        const Passage& cur = rules.passages[static_cast<size_t>(exit)];
        const ZoneInfo* curTo = br.zone(cur.to);
        const bool curSecret = curTo && curTo->secret;
        if ((curSecret && !secret) || (curSecret == secret && p.to > cur.to)) exit = static_cast<int>(k);
    }
    if (exit >= 0) {
        // 2.78: a passage of the group (the armory's « Prêts ») is taken from its plate, held (nobody walks into it).
        Rect out = rules.passages[static_cast<size_t>(exit)].rect;
        for (const CoopPlate& c : rules.coops)
            if (c.id == rules.passages[static_cast<size_t>(exit)].plate) out = c.rect;
        todo.push_back({"sortie", out, 2, static_cast<size_t>(exit)});
    }
    res.targets = static_cast<int>(todo.size());
    Driver d(level);
    const auto t0 = std::chrono::steady_clock::now();
    bool touching = false;
    const char* traceOf = std::getenv("COSMIC_NAV_TRACE");
    const bool trace = traceOf && *traceOf && res.zone.find(traceOf) != std::string::npos;
    for (const Target& t : todo) {
        const Vec2 from = run->world().playerAt(kSeat).feet();
        const int limit = (40 + static_cast<int>(std::fabs(t.area.center().x - from.x) / 50.0f)) * kSimHz;
        int goalNode = -1;
        const NavGraph* last = nullptr;
        bool done = false;
        for (int step = 0; step < limit && !done; ++step) {
            GameWorld& w = run->world();
            Player& me = w.playerAt(kSeat);
            if (w.member(kSeat).presence != GameWorld::Presence::Playing) break;
            me.setInvulnerable(0.5f);  // the enemies' blows: nothing but the terrain is counted
            me.health = me.maxHealth();
            if (d.navi.graph() != last) {
                last = d.navi.graph();
                goalNode = -1;
            }
            if (last && goalNode < 0) {
                const bool onTablet = t.what.rfind("tablette", 0) == 0;
                const Rect r = t.kind == 2 ? t.area : t.area.expanded(t.kind == 1 ? 0.0f : onTablet ? 2.0f : 24.0f);
                // A plate may be on a platform (a goods lift): its places count too.
                goalNode = last->nearest(t.kind == 1 ? Vec2{t.area.center().x, t.area.bottom()} : r.bottomCenter(), std::max(48.0f, r.w),
                                         [&](const NavGraph::Node& nd) {
                                             return (nd.mover < 0 || t.kind == 1) && Rect{nd.feet.x - 5.0f, nd.feet.y - 22.0f, 10.0f, 22.0f}.intersects(r);
                                         });
                if (goalNode < 0) goalNode = last->nearest(r.bottomCenter(), 200.0f, [](const NavGraph::Node& nd) { return nd.mover < 0; });
            }
            const Vec2 goal = goalNode >= 0 && last ? last->node(goalNode).feet : t.area.bottomCenter();
            d.mayLeave = t.kind == 2;
            const InputFrame f = d.step(*run, goal, goalNode);
            if (trace && step % 60 == 0) {
                const Player& tp = run->world().playerAt(kSeat);
                std::printf("    [%s] %s t=%d pieds (%.0f, %.0f) v (%.0f, %.0f) %s chemin %zu/%zu but %d (%.0f, %.0f) %s\n", res.zone.c_str(), t.what.c_str(), step,
                            tp.feet().x, tp.feet().y, tp.vel.x, tp.vel.y, tp.grounded() ? "au sol" : "en l'air", d.at, d.path.size(), goalNode, goal.x, goal.y,
                            nav::intentText(d.navi.pilot(kSeat).thought().intent));
                if (last && d.path.size() < 2 && step % 600 == 0) {
                    const int here = last->locate(tp.feet());
                    std::printf("      ici %d seg %d ; arêtes :", here, here >= 0 ? last->segment(here) : -1);
                    for (int i = 0; i < last->size(); ++i)
                        if (here >= 0 && last->segment(i) == last->segment(here))
                            for (const auto& e : last->edges(i))
                                if (last->segment(e.to) != last->segment(here))
                                    std::printf(" [(%.0f,%.0f)->(%.0f,%.0f) m%d n%d]", last->node(i).feet.x, last->node(i).feet.y, last->node(e.to).feet.x,
                                                last->node(e.to).feet.y, static_cast<int>(e.move), e.needs);
                    std::printf("\n");
                }
                if (last && d.at + 1 < d.path.size()) {
                    const NavGraph::Node& a = last->node(d.path[d.at]);
                    const NavGraph::Node& b = last->node(d.path[d.at + 1]);
                    const NavGraph::Edge* e = last->edgeBetween(d.path[d.at], d.path[d.at + 1]);
                    std::printf("      arête (%.0f, %.0f) -> (%.0f, %.0f) %d besoin %d prog %d\n", a.feet.x, a.feet.y, b.feet.x, b.feet.y, e ? static_cast<int>(e->move) : -1,
                                e ? e->needs : -1, e ? e->program : -1);
                }
            }
            stepRun(*run, f, state);
            ++res.steps;
            if (run->zone() != startZone) {
                done = t.kind == 2;
                if (!done) res.missed += "(sortie prise trop tot) ";
                break;
            }
            GameWorld& w2 = run->world();
            const Player& p2 = w2.playerAt(kSeat);
            if (w2.member(kSeat).presence != GameWorld::Presence::Playing || !p2.alive()) {
                ++res.deaths;
                res.missed += "(mort vers " + t.what + ") ";
                break;
            }
            d.navi.terrain().sync(w2, run.get());
            const nav::Harm h = d.navi.terrain().harm(p2.box, 0);
            const bool touch = h == nav::Harm::Hazard || h == nav::Harm::Liquid;  // the void: a death (counted above)
            if (touch && !touching) {
                ++res.touches;
                res.missed += "(danger touche vers " + t.what + ") ";
            }
            touching = touch;
            const bool footed = p2.grounded() || p2.onLadder;
            // A place: within reach of it (a tablet: touched, jumping onto its ledge), or on the best place to
            // stand by it (a heart floating over its arena: the highest floor under it).
            const bool tablet = t.what.rfind("tablette", 0) == 0;
            if (t.kind == 0)
                done = tablet ? p2.box.intersects(t.area)
                              : footed && (p2.box.intersects(t.area.expanded(40.0f)) ||
                                           (last && goalNode >= 0 && (last->node(goalNode).feet - p2.feet()).length() < 8.0f));
            else if (t.kind == 1) done = run->coop(t.coop).done;
        }
        if (done) ++res.reached;
        else res.missed += t.what + " ";
        if (run->zone() != startZone || run->world().member(kSeat).presence != GameWorld::Presence::Playing) break;
    }
    res.used = d.navi.pilot(kSeat).used();
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

}  // namespace

TEST_CASE("raid nav (2.79): in EVERY zone of EVERY branch of Asterion (armory included), an Expert goes to each objective then out, the terrain never killing it") {
    Svc s;
    std::vector<ZoneResult> all;
    const char* only = std::getenv("COSMIC_NAV_BRANCH");  // to look at one branch
    for (const Branch* br : Branch::all()) {
        if (only && *only && br->part != only) continue;
        std::vector<int> zones;
        if (br->hasArmory()) zones.push_back(0);
        for (const ZoneInfo& z : br->zones) zones.push_back(z.id);
        for (int z : zones) all.push_back(runZone(s, *br, z));
    }
    int ok = 0;
    std::printf("  | Branche | Zone | Objectifs atteints | Temps de jeu | Morts (terrain) | Dangers touchés | Doubles sauts | Ruées | Impulsions | Muraux | Calcul |\n");
    for (const ZoneResult& r : all) {
        std::printf("  | %s | %s | %d / %d | %.1f s | %d | %d | %d | %d | %d | %d | %.0f ms | %s\n", r.branch.c_str(), r.zone.c_str(), r.reached, r.targets,
                    r.steps / static_cast<double>(kSimHz), r.deaths, r.touches, r.used.airJumps, r.used.dashes, r.used.impulses, r.used.wallJumps, r.ms,
                    r.missed.c_str());
        ok += r.ok() ? 1 : 0;
    }
    std::printf("  zones réussies : %d / %zu\n", ok, all.size());
    CHECK(ok == static_cast<int>(all.size()));
}

TEST_CASE("raid nav (2.79): Débutant, Confirmé, Expert — the same moves and physics, a slower reaction, a shallower lookahead, a less precise timing") {
    Svc s;
    const Branch* jg = Branch::builtin("jambe_gauche");
    const Branch* bd = Branch::builtin("bras_droit");
    for (auto [br, zone] : {std::pair<const Branch*, int>{jg, 3}, std::pair<const Branch*, int>{bd, 7}}) {
        double seconds[3] = {0, 0, 0};
        for (int level : {0, 1, 2}) {
            const ZoneResult r = runZone(s, *br, zone, level);
            seconds[level] = r.steps / static_cast<double>(kSimHz);
            std::printf("  %s / %s, %s : %d / %d, %.1f s, %d mort(s), %d rattrapage(s) en l'air, %d double(s) saut(s), %d ruée(s), %d impulsion(s)\n",
                        r.branch.c_str(), r.zone.c_str(), botSkillName(static_cast<BotSkill>(level)), r.reached, r.targets, seconds[level], r.deaths,
                        r.used.recoveries, r.used.airJumps, r.used.dashes, r.used.impulses);
            if (level == 2) CHECK(r.ok());
        }
    }
}

// ============================================================================ aimed tests

namespace {

/// A level of its own (outside a raid): the solo player driven by the navigation.
struct SoloWorld {
    Svc s;
    std::unique_ptr<GameWorld> w;
    nav::Navigator navi;
    std::vector<int> path;
    size_t at = 0;
    int planned = -100000;
    float best = 1e30f;
    int noProgress = 0;
    uint32_t prevHeld = 0;
    uint32_t tick = 0;
    explicit SoloWorld(LevelData l) {
        l.intro.steps.clear();
        WorldServices ws{s.res, s.audio, s.content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(ws, WorldMode::Arena);
        w->load(l);
        navi.setSkill(2);
        for (int i = 0; i < 30; ++i) stepWorld(*w, InputFrame{}, kSimDt);
    }
    void step(Vec2 goal, int goalNode) {
        navi.update(*w, 1, tick, {}, {}, nullptr);
        if (navi.graphChanged()) path.clear();
        nav::SeatPlan plan;
        plan.goal = goal;
        plan.goalNode = goalNode;
        plan.stop = 4.0f;
        plan.path = &path;
        plan.at = &at;
        plan.planned = &planned;
        plan.best = &best;
        plan.noProgress = &noProgress;
        const nav::Pad pad = navi.steer(*w, 0, plan, prevHeld);
        stepWorld(*w, frameOf(pad, prevHeld), kSimDt);
        ++tick;
    }
    Player& me() { return w->player(); }
};

LevelData twoFloors(float gap) {
    LevelData l = LevelData::makeEmpty("test_nav_gap", "Test", "sylvanox");
    l.objects.clear();
    LevelObject a;
    a.id = "a";
    a.type = "solid";
    a.layer = "terrain";
    a.rect = {0, 800, 400, 160};
    l.objects.push_back(a);
    LevelObject b = a;
    b.id = "b";
    b.rect = {400 + gap, 800, 2000 - gap, 160};
    l.objects.push_back(b);
    LevelObject start;
    start.id = "start";
    start.type = "player_start";
    start.layer = "gameplay";
    start.rect = {160, 778, 10, 22};
    l.objects.push_back(start);
    l.startPowers.clear();  // no dash, no impulse: its jumps only
    return l;
}

}  // namespace

TEST_CASE("raid nav (2.79): a gap a single jump cannot cross is crossed with the double jump (the graph knows it, the player does it)") {
    const float gap = 170.0f;
    SoloWorld sw(twoFloors(gap));
    CHECK(sw.me().airJumpCount() == 1);
    // The graph of the level (no dash, no impulse): from the near floor to the far one, only a double jump.
    auto snap = NavGraph::snapshot(*sw.w);
    NavGraph g;
    g.build(*snap);
    int doubles = 0, singles = 0;
    for (int i = 0; i < g.size(); ++i)
        for (const auto& e : g.edges(i))
            if (g.node(i).feet.x < 400.0f && g.node(e.to).feet.x > 400.0f + gap && e.program >= 0) {
                const nav::Style st = g.programs()[static_cast<size_t>(e.program)].style();
                if (e.needs == 0 && st == nav::Style::DoubleJump) ++doubles;
                if (st == nav::Style::Jump) ++singles;
            }
    std::printf("  au-dessus d'un vide de %.0f px : %d liaisons en double saut, %d en saut simple\n", gap, doubles, singles);
    CHECK(doubles >= 1);
    CHECK(singles == 0);
    // A jump alone (held to the top, running from the edge) falls in: the copy of the physics says so.
    {
        nav::Body b = nav::Body::of(sw.me());
        b.box = Rect::fromBottomCenter({394.0f, 800.0f}, sw.me().config().size);
        nav::Terrain t;
        t.sync(*sw.w, nullptr);
        t.plan(240, 0, b.feet());
        nav::TerrainArena arena(t);
        nav::Program jump;
        jump.dir = 1;
        jump.jump = 40;
        jump.steer = false;
        const nav::BodySim sim(sw.me().config(), nav::Kit::of(sw.me()));
        const nav::Outcome o = nav::simulate(jump, b, 0, sim, arena, [](int) { return 2000.0f; }, [](const nav::Body&, int) { return true; }, 240);
        CHECK(!o.landed || o.feet.y > 800.5f);
    }
    // The player: from its start to the far floor, never in the void, with a double jump.
    const int goalNode = g.nearest({900.0f, 800.0f}, 64.0f);
    bool thoughtVoid = false, died = false;
    for (int step = 0; step < 20 * kSimHz; ++step) {
        sw.step(sw.navi.graph() ? sw.navi.graph()->node(sw.navi.graph()->nearest({900.0f, 800.0f}, 64.0f)).feet : Vec2{900.0f, 800.0f},
                sw.navi.graph() ? sw.navi.graph()->nearest({900.0f, 800.0f}, 64.0f) : -1);
        thoughtVoid = thoughtVoid || sw.navi.pilot(0).thought().intent == nav::Intent::DoubleJumpVoid;
        if (!sw.me().alive() || sw.me().box.y > sw.w->level().effectiveKillY()) died = true;
        if (sw.me().grounded() && sw.me().feet().x > 880.0f) break;
    }
    (void)goalNode;
    std::printf("  le joueur simulé : de l'autre côté à x = %.0f, %d double(s) saut(s), pensée « %s » vue : %s\n", sw.me().feet().x,
                sw.navi.pilot(0).used().airJumps, nav::intentText(nav::Intent::DoubleJumpVoid), thoughtVoid ? "oui" : "non");
    CHECK(!died);
    CHECK(sw.me().feet().x > 880.0f);
    CHECK(sw.navi.pilot(0).used().airJumps >= 1);
    CHECK(thoughtVoid);
}

TEST_CASE("raid nav (2.79): a moving platform over the void is reached where it WILL be (the Muscle suspendu, four islands that move)") {
    Svc s;
    const Branch* br = Branch::builtin("jambe_gauche");
    CHECK(br != nullptr);
    const uint32_t state = allDone(*br);
    auto run = zoneRun(s, *br, 3, state);
    CHECK(run);
    // Its guardian, beyond the four islands.
    Rect target;
    for (const ZoneObjective& zo : run->rules().objectives)
        for (const LevelObject& o : run->world().level().objects)
            if (o.id == zo.source) target = o.rect;
    CHECK(target.w > 0.0f);
    Driver d(2);
    int moverJumps = 0, aimedAhead = 0, exact = 0;
    bool died = false, reached = false;
    uint32_t aimed = 0;
    Rect boxAtTakeoff;
    Vec2 predicted;
    for (int step = 0; step < 90 * kSimHz && !reached; ++step) {
        GameWorld& w = run->world();
        w.playerAt(kSeat).setInvulnerable(0.5f);
        w.playerAt(kSeat).health = w.playerAt(kSeat).maxHealth();
        const NavGraph* g = d.navi.graph();
        const int goalNode = g ? g->nearest(target.bottomCenter(), 120.0f, [](const NavGraph::Node& nd) { return nd.mover < 0; }) : -1;
        const InputFrame f = d.step(*run, goalNode >= 0 ? g->node(goalNode).feet : target.bottomCenter(), goalNode);
        const nav::Pilot& pilot = d.navi.pilot(kSeat);
        if (pilot.active() && pilot.targetMover() != 0 && pilot.played() == 1 && !pilot.track().empty()) {
            aimed = pilot.targetMover();
            boxAtTakeoff = w.physics().get(aimed)->box;
            predicted = pilot.track().back();
        }
        stepRun(*run, f, state);
        const Player& me = run->world().playerAt(kSeat);
        if (run->world().member(kSeat).presence != GameWorld::Presence::Playing || !me.alive()) {
            died = true;
            break;
        }
        if (aimed != 0 && me.grounded() && me.groundCollider() == aimed) {
            ++moverJumps;
            const Rect now = run->world().physics().get(aimed)->box;
            if (std::fabs(now.x - boxAtTakeoff.x) >= 8.0f) ++aimedAhead;  // it moved during the flight: it was aimed ahead
            if ((me.feet() - predicted).length() < 1.5f) ++exact;        // where the simulation said it would land
            aimed = 0;
        }
        reached = me.grounded() && me.box.intersects(target.expanded(120.0f));
    }
    std::printf("  îlots mouvants : %d sauts sur une plateforme mobile, %d visant où elle sera (elle a bougé d'au moins 8 px en vol), %d posés au pixel "
                "près où la simulation l'annonçait ; gardien atteint : %s\n",
                moverJumps, aimedAhead, exact, reached ? "oui" : "non");
    CHECK(!died);
    CHECK(reached);
    CHECK(moverJumps >= 3);
    CHECK(aimedAhead >= 2);
    CHECK(exact >= 2);
}

TEST_CASE("raid nav (2.79): the rising magma is kept below — up on a ledge before it rises, down again once it is gone") {
    Svc s;
    const Branch* br = Branch::builtin("bras_droit");
    uint32_t state = 0;  // the objectives done, never a link of the titan (one stops the magma)
    for (const ObjectiveInfo& o : br->objectives) state |= 1u << o.index;
    auto run = zoneRun(s, *br, 3, state);  // la Veine de magma: up at 16 s, for 7 s, every 34 s
    CHECK(run);
    CHECK(run->rules().magma.present);
    auto* magma = dynamic_cast<DamageZoneEntity*>(run->world().entity(run->rules().magma.zone));
    CHECK(magma != nullptr);
    const Rect basin = magma->box;
    Driver d(2);
    bool fled = false, touched = false, died = false, upSeen = false;
    int hurt = 0;
    const Vec2 spot{basin.center().x, basin.bottom()};
    for (int step = 0; step < 34 * kSimHz; ++step) {
        const NavGraph* g = d.navi.graph();
        const int goalNode = g ? g->nearest(spot, 80.0f, [](const NavGraph::Node& nd) { return nd.mover < 0; }) : -1;
        const InputFrame f = d.step(*run, goalNode >= 0 ? g->node(goalNode).feet : spot, goalNode);
        const int before = run->world().playerAt(kSeat).health;
        stepRun(*run, f, state);
        const Player& me = run->world().playerAt(kSeat);
        if (run->world().member(kSeat).presence != GameWorld::Presence::Playing || !me.alive()) {
            died = true;
            break;
        }
        if (me.health < before) ++hurt;
        const Rect body = magma->overlapBox();
        if (magma->active && magma->magmaLevel() > 0.05f) upSeen = true;
        if (magma->active && body.h > 0.5f && body.intersects(me.box)) touched = true;
        fled = fled || d.navi.pilot(kSeat).thought().intent == nav::Intent::FleeLiquid;
    }
    const Player& me = run->world().playerAt(kSeat);
    std::printf("  magma : monté %s, touché %s, blessures %d, pensée « %s » : %s, de retour dans la cuvette : %s\n", upSeen ? "oui" : "non",
                touched ? "oui" : "non", hurt, nav::intentText(nav::Intent::FleeLiquid), fled ? "oui" : "non",
                me.feet().y >= basin.bottom() - 2.0f ? "oui" : "non");
    CHECK(upSeen);
    CHECK(!died);
    CHECK(!touched);
    CHECK(fled);
}

TEST_CASE("raid nav (2.79): shots and a beam are dodged (crouch, jump, wait) — seen after its reaction time, never by a rule of its own") {
    // Shots at head height and at the feet, from the right, at a player standing on its floor.
    {
        LevelData l = twoFloors(0.0f);
        SoloWorld sw(l);
        const Vec2 home = sw.me().feet();
        int hits = 0;
        bool dodgeSeen = false;
        for (int step = 0; step < 8 * kSimHz; ++step) {
            if (step % 90 == 30) {
                Projectile pr;
                const bool low = (step / 90) % 2 == 1;
                pr.pos = pr.prevPos = {home.x + 230.0f, home.y - (low ? 5.0f : 18.0f)};
                pr.vel = {-260.0f, 0.0f};
                pr.radius = 4.0f;
                pr.life = 3.0f;
                pr.fromPlayer = false;
                sw.w->spawnProjectile(pr);
            }
            const int before = sw.me().health;
            sw.step(home, sw.navi.graph() ? sw.navi.graph()->nearest(home, 32.0f) : -1);
            if (sw.me().health < before) ++hits;
            const nav::Intent i = sw.navi.pilot(0).thought().intent;
            dodgeSeen = dodgeSeen || i == nav::Intent::DodgeShot || i == nav::Intent::Duck;
            sw.me().setInvulnerable(0.0f);
        }
        std::printf("  tirs : %d touché(s) sur 8, esquive vue : %s, %d esquives\n", hits, dodgeSeen ? "oui" : "non", sw.navi.pilot(0).used().dodges);
        CHECK(hits == 0);
        CHECK(dodgeSeen);
    }
    // A beam across its way (on 1.6 s, off 1.6 s): it waits for it to go out, then crosses.
    {
        LevelData l = twoFloors(0.0f);
        LevelObject laser;
        laser.id = "rayon";
        laser.type = "laser";
        laser.layer = "gameplay";
        laser.rect = {620.0f, 704.0f, 160.0f, 16.0f};  // vertical, from 640 down to the floor at x = 700
        laser.rotation = 90.0f;
        laser.props = {{"onTime", 1.6}, {"offTime", 1.6}, {"warnTime", 0.6}, {"phase", 0.0}, {"active", true}};
        l.objects.push_back(laser);
        SoloWorld sw(l);
        const Vec2 goal{900.0f, 800.0f};
        int hits = 0;
        bool beamSeen = false, crossed = false;
        for (int step = 0; step < 15 * kSimHz && !crossed; ++step) {
            const int before = sw.me().health;
            sw.step(goal, sw.navi.graph() ? sw.navi.graph()->nearest(goal, 32.0f) : -1);
            if (sw.me().health < before) ++hits;
            sw.me().setInvulnerable(0.0f);
            beamSeen = beamSeen || sw.navi.pilot(0).thought().intent == nav::Intent::DodgeBeam;
            crossed = sw.me().feet().x > 880.0f;
        }
        std::printf("  rayon : traversé %s, touché %d fois, pensée « %s » : %s\n", crossed ? "oui" : "non", hits, nav::intentText(nav::Intent::DodgeBeam),
                    beamSeen ? "oui" : "non");
        CHECK(crossed);
        CHECK(hits == 0);
    }
}

// ============================================================================ wall jumps

namespace {

/// A chimney: two walls 48 px apart rising from the floor (the left one open below: the way in), a ledge on
/// top of the left one 200 px above the floor — out of reach of a jump and a double jump (102 px).
LevelData chimney() {
    LevelData l = twoFloors(0.0f);
    LevelObject left;
    left.id = "mur_gauche";
    left.type = "solid";
    left.layer = "terrain";
    left.rect = {240.0f, 600.0f, 32.0f, 144.0f};  // y 600..744: 56 px of room under it
    l.objects.push_back(left);
    LevelObject right = left;
    right.id = "mur_droit";
    right.rect = {320.0f, 480.0f, 32.0f, 320.0f};
    l.objects.push_back(right);
    return l;
}

}  // namespace

TEST_CASE("raid nav (2.79): the wall jump — the copy of the physics slides and jumps off walls as the player does, and climbs a chimney with it") {
    // The copy: into the chimney, a jump against the right wall, sliding, off it, against the left one…
    {
        SoloWorld sw(chimney());
        nav::Body body = nav::Body::of(sw.me());
        nav::Terrain t;
        t.sync(*sw.w, nullptr);
        std::vector<nav::Pad> pads;
        auto pad = [](int mx, bool jump) {
            nav::Pad p;
            p.mx = static_cast<int8_t>(mx * 127);
            if (jump) p.held |= 1u << static_cast<int>(Action::Jump);
            if (mx > 0) p.held |= 1u << static_cast<int>(Action::MoveRight);
            if (mx < 0) p.held |= 1u << static_cast<int>(Action::MoveLeft);
            return p;
        };
        for (int i = 0; i < 110; ++i) pads.push_back(pad(1, false));  // under the left wall, into the chimney
        for (int i = 0; i < 24; ++i) pads.push_back(pad(1, true));      // a jump against the right wall
        int wall = 1;
        for (int round = 0; round < 4; ++round) {
            for (int i = 0; i < 40; ++i) pads.push_back(pad(wall, false));    // pressed against it: it slides down
            for (int i = 0; i < 16; ++i) pads.push_back(pad(-wall, true));    // off it (a new press), toward the other
            wall = -wall;
        }
        t.plan(static_cast<int>(pads.size()) + 2, 0, body.feet());
        nav::TerrainArena arena(t);
        const nav::BodySim sim(sw.me().config(), nav::Kit::of(sw.me()));
        arena.begin();
        uint32_t prev = 0;
        double err = 0.0;
        int slides = 0;
        for (size_t i = 0; i < pads.size(); ++i) {
            nav::Pad p = pads[i];
            p.pressed = p.held & ~prev;
            InputFrame f;
            f.held = p.held;
            f.pressed = p.pressed;
            f.released = prev & ~p.held;
            f.moveX = p.mx;
            prev = p.held;
            arena.advance(body, sim, static_cast<int>(i) + 1);
            sim.step(body, arena.physics(), p);
            stepWorld(*sw.w, f, kSimDt);
            err = std::max(err, static_cast<double>((sw.me().feet() - body.feet()).length()));
            if (sw.me().state() == PlayerState::WallSlide) ++slides;
        }
        arena.end();
        std::printf("  saut mural : %zu pas, %d pas en glissade le long d'un mur, %d sauts muraux (copie), écart maximal %.5f px\n", pads.size(), slides,
                    body.wallJumps, err);
        CHECK(slides > 0);
        CHECK(body.wallJumps >= 2);
        CHECK(err < 0.01);
    }
    // The player: up the chimney to the ledge, from wall to wall.
    {
        SoloWorld sw(chimney());
        const Vec2 top{256.0f, 600.0f};
        bool reached = false, died = false, thought = false;
        for (int step = 0; step < 30 * kSimHz && !reached; ++step) {
            const NavGraph* g = sw.navi.graph();
            const int goalNode = g ? g->nearest(top, 24.0f) : -1;
            sw.step(goalNode >= 0 ? g->node(goalNode).feet : top, goalNode);
            thought = thought || sw.navi.pilot(0).thought().intent == nav::Intent::WallJump;
            if (!sw.me().alive()) died = true;
            reached = sw.me().grounded() && std::fabs(sw.me().feet().y - 600.0f) < 1.0f && sw.me().feet().x < 272.0f;
        }
        std::printf("  cheminée : sommet atteint %s, %d saut(s) mural(aux), pensée « %s » vue : %s\n", reached ? "oui" : "non", sw.navi.pilot(0).used().wallJumps,
                    nav::intentText(nav::Intent::WallJump), thought ? "oui" : "non");
        CHECK(!died);
        CHECK(reached);
        CHECK(sw.navi.pilot(0).used().wallJumps >= 1);
        CHECK(thought);
    }
}

// ============================================================================ the same run, its cost

TEST_CASE("raid nav (2.79): the same steps give the same digest with the lookahead (2 and 8 seats, moving platforms); its cost per step measured") {
    Svc s;
    const Branch* br = Branch::builtin("jambe_gauche");
    for (int players : {2, 8}) {
        uint64_t digest[2] = {0, 0};
        nav::Cost cost;
        for (int k = 0; k < 2; ++k) {
            RaidRun::Test t;
            t.on = true;
            t.invincible = true;
            t.players = players;
            TestSession session(s.ws(), *br, t);
            session.setAdminSeated(false);
            session.setSkill(BotSkill::Expert);
            session.setBotsInvincible(true);
            CHECK(session.begin(3));  // the Muscle suspendu: its islands that move
            for (int step = 0; step < 30 * kSimHz; ++step) session.step(InputFrame{});
            digest[k] = session.hash();
            cost = session.bots().navigation().cost();
        }
        std::printf("  %d places : empreinte %016llx deux fois ; %lld pas pilotés, %.4f ms en moyenne, %.2f ms au pire ; %lld choix de mouvement, "
                    "%lld pas simulés (au plus %lld en un pas de jeu), %lld choix remis au pas suivant, %lld graphes attendus\n",
                    players, static_cast<unsigned long long>(digest[0]), static_cast<long long>(cost.steps), cost.totalMs / std::max<int64_t>(1, cost.steps),
                    cost.maxStepMs, static_cast<long long>(cost.searches), static_cast<long long>(cost.bodySteps), static_cast<long long>(cost.maxStepBodySteps),
                    static_cast<long long>(cost.deferred), static_cast<long long>(cost.graphWaits));
        CHECK(digest[0] == digest[1]);
    }
}
