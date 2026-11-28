// The planner of the machine's runs (tools/tas/Planner.cpp, docs/TAS.md « Le planificateur »): on
// small worlds of its own, it crosses a gap that needs the dash, never dashes without the energy
// (waits for the regeneration or takes another way), presses a jump before a landing when that is
// faster (the buffered jump), and does as well as a brute force over its maneuvers; its lower bound
// never exceeds a time really achieved (the brute force, its own plans, the run shipped for
// sylvanox_01); its plans are the same for the same state; the search with it never gives a slower
// run and its run is verified by the replays.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Paths.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/raid/nav/BodySim.h"
#include "game/tas/ArenaWorld.h"
#include "game/tas/TasFile.h"
#include "tests/TestFramework.h"
#include "tools/tas/Course.h"
#include "tools/tas/Output.h"
#include "tools/tas/Planner.h"
#include "tools/tas/Search.h"

using namespace cosmic;
using raid::nav::Body;
using raid::nav::BodySim;
using raid::nav::Kit;
using raid::nav::Pad;

namespace {

const PlayerConfig& shippedConfig() {
    static const PlayerConfig cfg = [] {
        ContentDatabase c;
        c.reload();
        return PlayerConfig::fromJson(c.playerConfig());
    }();
    return cfg;
}

Kit dashKit(float cost = 5.0f) {
    Kit k;
    k.airJumps = 1;
    k.dashKey = static_cast<int>(Action::PowerSlot1);
    k.dashCost = cost;
    return k;
}

/// A body standing (or put) with its feet at `feet`, settled a few steps.
Body bodyAt(const BodySim& sim, const PhysicsWorld& ph, Vec2 feet, float energy) {
    Body b;
    b.box = Rect::fromBottomCenter(feet, sim.config().size);
    b.airJumpsLeft = sim.kit().airJumps;
    b.dashAir = sim.kit().dashAirUses;
    b.energy = energy;
    for (int i = 0; i < 4; ++i) sim.step(b, ph, Pad{});
    return b;
}

/// Two floors with a gap between them (a pit), the goal on the far one.
struct Gap {
    PhysicsWorld ph;
    Rect goal;
    explicit Gap(float gap) {
        ph.add(Rect{-400.0f, 400.0f, 700.0f, 40.0f}, ColliderKind::Solid);
        ph.add(Rect{300.0f + gap, 400.0f, 900.0f, 40.0f}, ColliderKind::Solid);
        goal = Rect{300.0f + gap + 60.0f, 360.0f, 20.0f, 40.0f};
    }
};

/// The goal, and a distance to it that knows the pit (as the level's distance field does): below
/// the floors is far.
tas::PlanGoal goalOf(const Rect& r, bool pitAware = false) {
    tas::PlanGoal g;
    g.box = r;
    if (pitAware)
        g.distance = [r](Vec2 f) {
            const float dx = std::max({0.0f, r.x - f.x, f.x - r.right()});
            const float dy = std::max({0.0f, r.y - f.y, f.y - r.bottom()});
            return std::sqrt(dx * dx + dy * dy) + 10.0f * std::max(0.0f, f.y - 401.0f);
        };
    return g;
}

/// The plan played again in BodySim: the step it touches the goal (-1 never); every dash started with the energy it costs.
int replay(const BodySim& sim, const PhysicsWorld& ph, Body b, uint32_t held, const tas::Plan& p, const Rect& goal, bool* dashedWithEnergy = nullptr) {
    if (dashedWithEnergy) *dashedWithEnergy = true;
    for (size_t i = 0; i < p.frames.size(); ++i) {
        const InputFrame& f = p.frames[i];
        Pad pad;
        pad.held = f.held;
        pad.pressed = f.held & ~held;
        pad.mx = f.moveX;
        pad.my = f.moveY;
        held = f.held;
        const float before = b.energy;
        const uint16_t dashes = b.dashes;
        sim.step(b, ph, pad);
        if (b.dashes != dashes && dashedWithEnergy && before + 0.001f < sim.kit().dashCost) *dashedWithEnergy = false;
        if (b.box.intersects(goal)) return static_cast<int>(i) + 1;
    }
    return -1;
}

}  // namespace

TEST_CASE("tas planner: a gap that needs the dash is crossed with it, never without it") {
    const PlayerConfig& cfg = shippedConfig();
    Gap w(300.0f);
    tas::PlanBudget budget;
    budget.horizon = 600;
    // Without the dash: no way over.
    {
        const BodySim sim(cfg, Kit{});
        const Body b = bodyAt(sim, w.ph, {250.0f, 400.0f}, cfg.maxEnergy);
        const auto plans = tas::planOn(sim, b, 0, w.ph, nullptr, 700.0f, goalOf(w.goal), budget);
        CHECK(plans.empty() || !plans.front().arrives);
    }
    // With it: over, with a dash, and the plan really arrives when played again.
    const BodySim sim(cfg, dashKit());
    const Body b = bodyAt(sim, w.ph, {250.0f, 400.0f}, cfg.maxEnergy);
    tas::PlanStats st;
    const auto plans = tas::planOn(sim, b, 0, w.ph, nullptr, 700.0f, goalOf(w.goal), budget, &st);
    CHECK(!plans.empty() && plans.front().arrives);
    if (plans.empty() || !plans.front().arrives) return;
    const tas::Plan& p = plans.front();
    std::printf("  saut au-dessus de 300 px : %zu pas, %d dash, %d saut(s) en l'air, %lld pas simulés en %.1f ms\n", p.frames.size(), p.dashes, p.airJumps,
                static_cast<long long>(st.simSteps), st.ms);
    CHECK(p.dashes >= 1);
    CHECK(replay(sim, w.ph, b, 0, p, w.goal) == static_cast<int>(p.frames.size()));
    // Its lower bound never more than the time it took.
    CHECK(tas::lowerBoundSeconds(b, sim.kit(), cfg, tas::SpeedCaps{}, w.goal) <= static_cast<double>(p.frames.size()) / kSimHz + 1e-9);
}

TEST_CASE("tas planner: no dash without its energy — it waits for the regeneration, or takes another way") {
    const PlayerConfig& cfg = shippedConfig();
    const float cost = 10.0f;
    const BodySim sim(cfg, dashKit(cost));
    const float readyIn = cfg.energyRegenDelay + cost / cfg.energyRegen;  // from no energy, the regeneration's delay first
    {
        Gap w(300.0f);
        // A gap that needs the dash (without it: no way over).
        {
            const BodySim walker(cfg, Kit{});
            const auto none = tas::planOn(walker, bodyAt(walker, w.ph, {250.0f, 400.0f}, 0.0f), 0, w.ph, nullptr, 700.0f, goalOf(w.goal), tas::PlanBudget{});
            CHECK(none.empty() || !none.front().arrives);
        }
        Body b = bodyAt(sim, w.ph, {250.0f, 400.0f}, 0.0f);
        b.energy = 0.0f;
        b.energyRegenTimer = cfg.energyRegenDelay;
        // Too short a horizon for the energy to come back: no way over.
        tas::PlanBudget shortB;
        shortB.horizon = static_cast<int>((readyIn - 0.3f) * kSimHz);
        const auto none = tas::planOn(sim, b, 0, w.ph, nullptr, 700.0f, goalOf(w.goal), shortB);
        CHECK(none.empty() || !none.front().arrives);
        // Long enough: it waits, then dashes, and every dash had its energy.
        tas::PlanBudget longB;
        longB.horizon = 900;
        const auto plans = tas::planOn(sim, b, 0, w.ph, nullptr, 700.0f, goalOf(w.goal, true), longB);
        CHECK(!plans.empty() && plans.front().arrives);
        if (!plans.empty() && plans.front().arrives) {
            bool withEnergy = false;
            const int at = replay(sim, w.ph, b, 0, plans.front(), w.goal, &withEnergy);
            std::printf("  sans énergie : arrivé en %d pas (l'énergie revient en %.0f pas), %d dash\n", at, static_cast<double>(readyIn) * kSimHz, plans.front().dashes);
            CHECK(withEnergy);
            CHECK(plans.front().dashes >= 1);
            CHECK(at >= static_cast<int>(readyIn * kSimHz));
        }
    }
    {
        // A gap a jump crosses: no waiting (the dash dearer: ready only after the jump has crossed), no dash.
        const BodySim dear(cfg, dashKit(30.0f));
        const float dearIn = cfg.energyRegenDelay + 30.0f / cfg.energyRegen;
        Gap w(100.0f);
        Body b = bodyAt(dear, w.ph, {250.0f, 400.0f}, 0.0f);
        b.energy = 0.0f;
        b.energyRegenTimer = cfg.energyRegenDelay;
        tas::PlanBudget budget;
        budget.horizon = 600;
        const auto plans = tas::planOn(dear, b, 0, w.ph, nullptr, 700.0f, goalOf(w.goal, true), budget);
        CHECK(!plans.empty() && plans.front().arrives);
        if (!plans.empty() && plans.front().arrives) {
            std::printf("  saut sans dash : %zu pas (le dash prêt en %.0f pas), %d dash\n", plans.front().frames.size(), static_cast<double>(dearIn) * kSimHz, plans.front().dashes);
            CHECK(plans.front().dashes == 0);
            CHECK(static_cast<float>(plans.front().frames.size()) < dearIn * kSimHz);
        }
    }
}

TEST_CASE("tas planner: the jump is pressed before the landing when that is faster (buffered)") {
    const PlayerConfig& cfg = shippedConfig();
    const BodySim sim(cfg, Kit{});
    // Nova falls (no air jump left) toward a floor with a step just ahead: the sooner she jumps
    // after landing, the sooner she is over it. A start height where the landing comes two steps
    // after a press the planner can make and well before the next one (its maneuvers: presses at
    // steps 0, 1 and 4 of each 8).
    for (int dy = 0; dy < 60; ++dy) {
        PhysicsWorld ph;
        ph.add(Rect{-400.0f, 400.0f, 2000.0f, 40.0f}, ColliderKind::Solid);
        ph.add(Rect{262.0f, 362.0f, 1000.0f, 40.0f}, ColliderKind::Solid);
        Body b;
        b.box = Rect::fromBottomCenter({200.0f, 300.0f - static_cast<float>(dy)}, cfg.size);
        b.vel = {150.0f, 0.0f};
        b.state = PlayerState::Fall;
        b.airJumpsLeft = 0;
        Body f = b;
        int landing = -1;
        Pad right;
        right.held = 1u << static_cast<int>(Action::MoveRight);
        right.mx = 127;
        for (int k = 1; k < 200 && landing < 0; ++k) {
            sim.step(f, ph, right);
            if (f.grounded) landing = k;
        }
        if (landing < 0 || landing % 8 != 6) continue;
        const Rect goal{330.0f, 300.0f, 20.0f, 62.0f};
        tas::PlanBudget budget;
        budget.horizon = 360;
        const auto plans = tas::planOn(sim, b, 1u << static_cast<int>(Action::MoveRight), ph, nullptr, 700.0f, goalOf(goal), budget);
        CHECK(!plans.empty() && plans.front().arrives);
        if (plans.empty()) return;
        // The first press of the jump: before the landing (the body still in the air).
        Body r = b;
        uint32_t held = 1u << static_cast<int>(Action::MoveRight);
        int pressAt = -1;
        bool airborne = false;
        for (size_t i = 0; i < plans.front().frames.size(); ++i) {
            const InputFrame& fr = plans.front().frames[i];
            if ((fr.held & ~held) & (1u << static_cast<int>(Action::Jump))) {
                pressAt = static_cast<int>(i);
                airborne = !r.grounded;
                break;
            }
            Pad pad;
            pad.held = fr.held;
            pad.pressed = fr.held & ~held;
            pad.mx = fr.moveX;
            held = fr.held;
            sim.step(r, ph, pad);
        }
        std::printf("  atterrissage au pas %d, saut appuyé au pas %d (%s), arrivé en %zu pas\n", landing, pressAt, airborne ? "en l'air : tamponné" : "au sol",
                    plans.front().frames.size());
        CHECK(pressAt >= 0 && pressAt < landing && airborne);
        return;
    }
    CHECK(false);  // no start height found (the world above changed)
}

TEST_CASE("tas planner: as fast as a brute force over its maneuvers; the lower bound below both") {
    const PlayerConfig& cfg = shippedConfig();
    const BodySim sim(cfg, dashKit());
    Gap w(0.0f);  // a floor: 105 px to run, or to dash
    const Body start = bodyAt(sim, w.ph, {250.0f, 400.0f}, cfg.maxEnergy);
    // The brute force: every sequence of 6 maneuvers of 16 steps among 6 (right, the jump held,
    // pressed at step 8, a dash, a long jump, nothing), the earliest touch of the goal.
    const uint32_t R = 1u << static_cast<int>(Action::MoveRight), J = 1u << static_cast<int>(Action::Jump), D = 1u << sim.kit().dashKey;
    auto held = [&](int m, int t) -> uint32_t {
        switch (m) {
            case 0: return R;
            case 1: return R | J;
            case 2: return t >= 8 ? R | J : R;
            case 3: return t == 0 ? R | D : R;
            case 4: return (t == 0 ? D : 0u) | (t >= 2 ? J : 0u) | R;
            default: return 0u;
        }
    };
    int bestSteps = 1 << 30;
    std::vector<int> seq(6, 0);
    for (int code = 0; code < 6 * 6 * 6 * 6 * 6 * 6; ++code) {
        for (int i = 0, c = code; i < 6; ++i, c /= 6) seq[static_cast<size_t>(i)] = c % 6;
        Body b = start;
        uint32_t prev = 0;
        for (int k = 0; k < 96 && k < bestSteps; ++k) {
            Pad pad;
            pad.held = held(seq[static_cast<size_t>(k / 16)], k % 16);
            pad.pressed = pad.held & ~prev;
            pad.mx = (pad.held & R) ? 127 : 0;
            prev = pad.held;
            sim.step(b, w.ph, pad);
            if (b.box.y > 700.0f) break;
            if (b.box.intersects(w.goal)) {
                bestSteps = std::min(bestSteps, k + 1);
                break;
            }
        }
    }
    CHECK(bestSteps < (1 << 30));
    tas::PlanBudget budget;
    budget.horizon = 600;
    const auto plans = tas::planOn(sim, start, 0, w.ph, nullptr, 700.0f, goalOf(w.goal), budget);
    CHECK(!plans.empty() && plans.front().arrives);
    if (plans.empty() || !plans.front().arrives) return;
    const int planned = static_cast<int>(plans.front().frames.size());
    const double lb = tas::lowerBoundSeconds(start, sim.kit(), cfg, tas::SpeedCaps{}, w.goal);
    std::printf("  force brute %d pas, planificateur %d pas, borne inférieure %.0f pas\n", bestSteps, planned, lb * kSimHz);
    CHECK(planned <= bestSteps);
    CHECK(lb * kSimHz <= static_cast<double>(std::min(planned, bestSteps)) + 1e-6);
}

TEST_CASE("tas planner: on the run shipped for sylvanox_01, the bound never exceeds the time it took; the same state gives the same plans") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("sylvanox_01");
    if (!level) return;
    std::vector<InputFrame> run;
    std::string why;
    if (!tas::readSeedReplay(Paths::toUtf8(tasRoot() / "sylvanox_01" / "ANY_PERCENT.cprz"), "sylvanox_01", run, why)) {
        std::printf("  AVERTISSEMENT : pas de course livrée lisible (%s) : non testé\n", why.c_str());
        return;
    }
    tas::Course course(*level, content, RunCategory::AnyPercent);
    const auto& wps = course.waypoints();
    // Pass 1: the step each waypoint is touched (its checkpoint lit, the exit reached).
    std::vector<size_t> touched(wps.size(), static_cast<size_t>(-1));
    std::vector<tas::RouteHint> hints;  // the run's own path, as a ghost would give it
    {
        tas::ArenaWorld aw(content, *level);
        const tas::Course::Bound bound = course.bind(const_cast<GameWorld&>(aw.world()));
        for (size_t i = 0; i < run.size(); ++i) {
            aw.step(run[i]);
            if (i % 30 == 0) hints.push_back({aw.world().player().feet(), aw.world().timer().elapsed()});
            for (size_t k = 0; k < wps.size(); ++k) {
                if (touched[k] != static_cast<size_t>(-1)) continue;
                bool hit = false;
                if (wps[k].kind == tas::Waypoint::Kind::Exit) hit = aw.world().state() == WorldState::Outro || aw.world().state() == WorldState::Completed;
                else if (const auto* cp = dynamic_cast<const CheckpointEntity*>(bound.waypointEntity[k])) hit = cp->activated();
                if (hit) touched[k] = i + 1;
            }
            if (touched.back() != static_cast<size_t>(-1)) break;
        }
    }
    CHECK(touched.back() != static_cast<size_t>(-1));
    // Pass 2: every 30 steps, the bound to each waypoint still ahead.
    tas::ArenaWorld aw(content, *level);
    tas::Planner planner(course, &content);
    int samples = 0, violations = 0;
    double sumRatio = 0.0;
    std::optional<std::vector<tas::Plan>> first;
    for (size_t i = 0; i < run.size() && i < touched.back(); ++i) {
        if (i % 30 == 0 && aw.world().state() == WorldState::Playing) {
            for (size_t k = 0; k < wps.size(); ++k) {
                if (touched[k] == static_cast<size_t>(-1) || touched[k] <= i) continue;
                const double lb = planner.lowerBoundSeconds(aw.world(), static_cast<int>(k));
                const double real = static_cast<double>(touched[k] - i) / kSimHz;
                ++samples;
                if (lb > real + 1e-6) {
                    ++violations;
                    std::printf("  borne dépassée : pas %zu, repère %zu, %.3f s > %.3f s\n", i, k, lb, real);
                }
                sumRatio += real > 0.0 ? lb / real : 1.0;
            }
            CHECK(planner.lowerBoundToFinish(aw.world()) <= static_cast<double>(touched.back() - i) / kSimHz + 1e-6);
        }
        // The same state planned twice (and the plans the same): 5 s in.
        if (i == 600) {
            const uint32_t held = i > 0 ? run[i - 1].held : 0;
            tas::PlanBudget budget;
            budget.horizon = 480;
            first = planner.propose(aw.world(), 0, held, budget);
            const auto again = planner.propose(aw.world(), 0, held, budget);
            CHECK(!first->empty());
            CHECK(first->size() == again.size());
            for (size_t p = 0; p < std::min(first->size(), again.size()); ++p) CHECK((*first)[p].frames == again[p].frames);
            // With a human path: plans toward its point a second ahead, too.
            tas::Planner guided(course, &content);
            guided.setRouteHints(hints);
            const auto withHints = guided.propose(aw.world(), 0, held, budget);
            CHECK(guided.stats.hinted > 0 && withHints.size() > first->size());
        }
        aw.step(run[i]);
    }
    std::printf("  %d bornes sur la course livrée, %d dépassement(s), borne / temps réel en moyenne %.2f\n", samples, violations,
                samples > 0 ? sumRatio / samples : 0.0);
    CHECK(samples > 50);
    CHECK(violations == 0);
}

TEST_CASE("tas planner: the search with the planner never gives a slower run, and its run is verified") {
    ContentDatabase content;
    content.reload();
    LevelFingerprints fps;
    auto level = content.loadLevel("sylvanox_01");
    if (!level) return;
    const LevelFingerprint* fp = fps.get(content, "sylvanox_01");
    std::vector<InputFrame> best;
    std::string why;
    if (!fp || !tas::readSeedReplay(Paths::toUtf8(tasRoot() / "sylvanox_01" / "ANY_PERCENT.cprz"), "sylvanox_01", best, why)) return;
    const tas::Verified v = tas::recordAndVerify(content, *level, *fp, RunCategory::AnyPercent, best, 0);
    if (!v.ok) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 25.0;
    o.seed = 7;
    o.seedRun = best;
    o.windowFirst = o.windowLast = 1;
    const tas::SearchOutcome out = tas::search(content, *level, o);
    std::printf("  fenêtre 1 avec le planificateur : %lld plans, %lld gardés, %lld arrivés, %.3f s (livrée %.3f s)\n", static_cast<long long>(out.planner.proposals),
                static_cast<long long>(out.planner.kept), static_cast<long long>(out.planner.arrivals), static_cast<double>(out.clock),
                static_cast<double>(v.record.timeMs) / 1000.0);
    CHECK(out.found);
    CHECK(out.planner.calls > 0 && out.planner.proposals > 0);
    CHECK(std::llround(static_cast<double>(out.clock) * 1000.0) <= v.record.timeMs);
    const tas::Verified again = tas::recordAndVerify(content, *level, *fp, RunCategory::AnyPercent, out.inputs, 0);
    CHECK(again.ok && again.record.timeMs == std::llround(static_cast<double>(out.clock) * 1000.0));
}
