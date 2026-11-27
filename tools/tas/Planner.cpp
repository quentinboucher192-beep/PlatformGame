#include "tools/tas/Planner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "engine/physics/PhysicsWorld.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelData.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "game/raid/nav/Terrain.h"
#include "tools/tas/Course.h"

namespace cosmic::tas {

using raid::nav::Body;
using raid::nav::BodySim;
using raid::nav::Harm;
using raid::nav::Kit;
using raid::nav::Pad;
using raid::nav::Terrain;

void PlanStats::add(const PlanStats& o) {
    calls += o.calls;
    proposals += o.proposals;
    arriving += o.arriving;
    hinted += o.hinted;
    simSteps += o.simSteps;
    ms += o.ms;
    kept += o.kept;
    arrivals += o.arrivals;
    wins += o.wins;
    for (int i = 0; i < 3; ++i) err[i] += o.err[i], errN[i] += o.errN[i];
}

void PlanStats::noteError(size_t steps, float px) {
    const int b = steps <= 32 ? 0 : (steps <= 128 ? 1 : 2);
    err[b] += static_cast<double>(px);
    ++errN[b];
}

namespace {

constexpr uint32_t bitA(Action a) { return 1u << static_cast<int>(a); }

/// A maneuver: the keys of each step of a segment (a direction, the jump held over steps
/// [jumpOn, jumpOff), a power's key pressed at one step, Down or Up).
struct Man {
    int8_t dir = 0;
    int8_t vert = 0;          // -1 up, +1 down
    int8_t jumpOn = -1, jumpOff = 127;
    int8_t key = -1, keyAt = 0;
};

InputFrame frameOf(const Man& m, int t) {
    InputFrame f;
    if (m.dir > 0) f.held |= bitA(Action::MoveRight), f.moveX = 127;
    if (m.dir < 0) f.held |= bitA(Action::MoveLeft), f.moveX = -127;
    if (m.vert != 0) {
        f.held |= m.vert > 0 ? bitA(Action::MoveDown) : bitA(Action::MoveUp);
        f.moveY = static_cast<int8_t>(m.vert > 0 ? 127 : -127);
    }
    if (m.jumpOn >= 0 && t >= m.jumpOn && t < m.jumpOff) f.held |= bitA(Action::Jump);
    if (m.key >= 0 && t == m.keyAt) f.held |= 1u << m.key;
    return f;
}

/// The dash can be pressed within the next `steps` (its energy, cooldown and air uses).
bool dashReady(const Body& b, const Kit& kit, const PlayerConfig& cfg, int steps) {
    if (!kit.hasDash() || b.state == PlayerState::Dash) return false;
    if (!b.grounded && b.dashAir <= 0 && !b.dashRefill) return false;
    const float dt = kSimDt * static_cast<float>(steps);
    if (b.dashCool > dt) return false;
    float e = b.energy;
    if (b.energyRegenTimer < dt) e += cfg.energyRegen * (dt - std::max(0.0f, b.energyRegenTimer));
    return e + 0.001f >= kit.dashCost;
}

bool impulseReady(const Body& b, const Kit& kit, int steps) {
    if (!kit.hasImpulse()) return false;
    if (b.impulseCool > kSimDt * static_cast<float>(steps)) return false;
    if (!b.grounded && b.impulseUses <= 0) return false;
    return b.energy + 0.001f >= kit.impulseCost;
}

/// The maneuvers worth trying from `b` (the keys held before: `held`).
void maneuvers(std::vector<Man>& out, const Body& b, uint32_t held, const Kit& kit, const PlayerConfig& cfg, int seg) {
    out.clear();
    const bool jumpHeld = (held & bitA(Action::Jump)) != 0;
    const auto S = static_cast<int8_t>(seg);
    for (int8_t dir : {int8_t{1}, int8_t{-1}, int8_t{0}}) {
        Man m;
        m.dir = dir;
        out.push_back(m);  // run (the jump let go)
        m.jumpOn = 0;
        out.push_back(m);  // the jump held (a new press, or its height kept)
        if (dir == 0) continue;
        // Let go, then pressed again: the double jump, the wall jump, a jump buffered before a landing.
        m.jumpOn = 1;
        out.push_back(m);
        m.jumpOn = static_cast<int8_t>(std::max(2, seg / 2));
        out.push_back(m);
        // Pressed (or kept) a few steps only: a lower jump.
        m.jumpOn = jumpHeld ? 0 : 1;
        m.jumpOff = static_cast<int8_t>(std::min<int>(S, m.jumpOn + 3));
        out.push_back(m);
    }
    // Down: the slide on the ground, the fast fall in the air; with a new jump on a one-way
    // platform: the drop through it.
    for (int8_t dir : {int8_t{1}, int8_t{-1}}) {
        Man m;
        m.dir = dir;
        m.vert = 1;
        out.push_back(m);
    }
    if (b.grounded) {
        Man m;
        m.vert = 1;
        m.jumpOn = jumpHeld ? 1 : 0;
        m.jumpOff = static_cast<int8_t>(m.jumpOn + 2);
        out.push_back(m);
    }
    // The dash (only when its energy, cooldown and air uses allow it within the segment): level,
    // diagonal up; on the ground, the long jump (a jump buffered during the dash).
    if (dashReady(b, kit, cfg, seg)) {
        const auto k = static_cast<int8_t>(kit.dashKey);
        const auto at = static_cast<int8_t>((held >> kit.dashKey) & 1u ? 1 : 0);
        for (int8_t dir : {int8_t{1}, int8_t{-1}}) {
            Man m;
            m.dir = dir;
            m.key = k;
            m.keyAt = at;
            out.push_back(m);
            m.vert = -1;
            out.push_back(m);
            m.vert = 0;
            if (b.grounded) {
                m.jumpOn = static_cast<int8_t>(std::max<int>(at + 2, jumpHeld ? 1 : 0));
                out.push_back(m);
            }
            m.jumpOn = -1;
            if (!b.grounded) {
                m.jumpOn = static_cast<int8_t>(at + 3);  // a dash ended by an air jump
                out.push_back(m);
            }
        }
    }
    if (impulseReady(b, kit, seg)) {
        const auto k = static_cast<int8_t>(kit.impulseKey);
        const auto at = static_cast<int8_t>((held >> kit.impulseKey) & 1u ? 1 : 0);
        for (int8_t dir : {int8_t{1}, int8_t{-1}}) {
            Man m;
            m.dir = dir;
            m.key = k;
            m.keyAt = at;
            out.push_back(m);
        }
    }
}

/// The straight-line gap (px) from a point to a box.
float gapTo(const Rect& r, Vec2 p) {
    const float dx = std::max({0.0f, r.x - p.x, p.x - r.right()});
    const float dy = std::max({0.0f, r.y - p.y, p.y - r.bottom()});
    return std::sqrt(dx * dx + dy * dy);
}

struct PNode {
    Body b;
    Terrain::Run run;
    uint32_t held = 0;
    int parent = -1;
    size_t from = 0, count = 0;  // its segment's frames in the pool
    float score = 0.0f;
    bool arrived = false;
    int arrivedAt = 0;  // steps of its segment played when it arrived
};

}  // namespace

std::vector<Plan> planOn(const BodySim& sim, const Body& start, uint32_t prevHeld, const PhysicsWorld& ph, Terrain* terrain, float killY, const PlanGoal& goal,
                         const PlanBudget& budget, PlanStats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    const Kit& kit = sim.kit();
    const PlayerConfig& cfg = sim.config();
    const int seg = std::clamp(budget.segment, 2, 32);
    const PhysicsWorld& phys = terrain ? terrain->physics() : ph;
    std::vector<PNode> tree;  // every state kept (their parents: the path)
    std::vector<InputFrame> pool;
    std::vector<Vec2> feetPool;
    auto distance = [&](Vec2 f) {
        if (goal.distance) {
            const float d = goal.distance(f);
            if (std::isfinite(d)) return d;
        }
        return gapTo(goal.box, f) + 64.0f;
    };
    // The estimate of the time to go (px of the distance at the best speed): where the body is,
    // where it will be a moment later (its momentum), and the energy and jumps it keeps.
    const float best = std::max(cfg.maxRunSpeed, kit.hasDash() ? kit.dashSpeed : 0.0f);
    auto score = [&](const Body& b) {
        const Vec2 f = b.feet();
        const float d0 = distance(f);
        const float d1 = distance(f + b.vel * 0.12f);
        float s = 0.5f * d0 + 0.5f * std::min(d1, d0 + 0.12f * best);
        if (kit.hasDash()) s -= 6.0f * std::min(3.0f, b.energy / std::max(1.0f, kit.dashCost)) + 4.0f * static_cast<float>(std::max(0, b.dashAir));
        s -= 3.0f * static_cast<float>(b.airJumpsLeft);
        return s;
    };
    PNode root;
    root.b = start;
    root.b.airJumpsUsed = root.b.dashes = root.b.impulses = root.b.wallJumps = 0;
    root.held = prevHeld;
    if (terrain) terrain->begin(root.run);
    root.score = score(root.b);
    tree.push_back(root);
    std::vector<int> beam{0};
    std::vector<PNode> cands;
    std::vector<InputFrame> cpool;
    std::vector<Vec2> cfeet;
    std::vector<Man> mans;
    std::vector<int> arrivals;  // tree indices
    int64_t simSteps = 0;
    int firstArrivalGen = -1;
    const int gens = std::max(1, budget.horizon / seg);
    for (int gen = 0; gen < gens && !beam.empty(); ++gen) {
        if (firstArrivalGen >= 0 && gen > firstArrivalGen) break;  // the first arrivals are the fastest
        cands.clear();
        cpool.clear();
        cfeet.clear();
        for (int bi : beam) {
            const PNode& n = tree[static_cast<size_t>(bi)];
            maneuvers(mans, n.b, n.held, kit, cfg, seg);
            for (const Man& m : mans) {
                PNode c;
                c.b = n.b;
                c.run = n.run;
                c.held = n.held;
                c.parent = bi;
                c.from = cpool.size();
                if (terrain) terrain->resume(c.run);
                bool dead = false;
                for (int t = 0; t < seg; ++t) {
                    InputFrame f = frameOf(m, t);
                    f.pressed = f.held & ~c.held;
                    f.released = c.held & ~f.held;
                    c.held = f.held;
                    Pad pad;
                    pad.held = f.held;
                    pad.pressed = f.pressed;
                    pad.mx = f.moveX;
                    pad.my = f.moveY;
                    if (terrain) terrain->advance(c.run, c.b, sim);
                    sim.step(c.b, phys, pad);
                    ++simSteps;
                    cpool.push_back(f);
                    cfeet.push_back(c.b.feet());
                    if (c.b.box.y > killY) {
                        dead = true;
                        break;
                    }
                    if (terrain) {
                        const Harm h = terrain->harm(c.b.box, std::min(c.run.k, terrain->horizon()));
                        if (h >= Harm::Hazard) {
                            dead = true;
                            break;
                        }
                    }
                    if (c.b.box.intersects(goal.box)) {
                        c.arrived = true;
                        c.arrivedAt = t + 1;
                        break;
                    }
                }
                c.count = cpool.size() - c.from;
                if (dead) {
                    cpool.resize(c.from);
                    cfeet.resize(c.from);
                    continue;
                }
                c.score = c.arrived ? -1e9f + static_cast<float>(c.arrivedAt) : score(c.b);
                cands.push_back(std::move(c));
            }
        }
        if (terrain) terrain->end(tree[0].run);
        if (cands.empty()) break;
        // Selection: the best of each place, speed and resources.
        std::vector<int> order(cands.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cands[static_cast<size_t>(a)].score < cands[static_cast<size_t>(b)].score; });
        std::unordered_map<uint64_t, int> cells;
        std::vector<int> next;
        for (int i : order) {
            PNode& c = cands[static_cast<size_t>(i)];
            if (!c.arrived) {
                const Vec2 f = c.b.feet();
                const auto q = [](float v, float s) { return static_cast<uint64_t>(static_cast<int64_t>(std::floor(v / s)) & 0xFFF); };
                const uint64_t key = q(f.x, 6.0f) | q(f.y, 6.0f) << 12 | q(c.b.vel.x, 45.0f) << 24 | q(c.b.vel.y, 60.0f) << 36 |
                                     static_cast<uint64_t>(c.b.grounded) << 48 | static_cast<uint64_t>(std::clamp(c.b.airJumpsLeft, 0, 3)) << 49 |
                                     static_cast<uint64_t>(std::clamp(c.b.dashAir, 0, 3)) << 51 | static_cast<uint64_t>(c.b.state == PlayerState::Dash) << 53 |
                                     static_cast<uint64_t>(std::clamp(static_cast<int>(c.b.energy / std::max(1.0f, kit.dashCost)), 0, 3)) << 54 |
                                     static_cast<uint64_t>(c.b.wallDir + 1) << 56 | static_cast<uint64_t>((c.held & bitA(Action::Jump)) != 0) << 58;
                if (!cells.emplace(key, i).second) continue;
                if (static_cast<int>(next.size()) >= budget.beam) continue;
            }
            next.push_back(i);
        }
        // Kept: into the tree, their frames into the pool.
        std::vector<int> kept;
        for (int i : next) {
            PNode c = std::move(cands[static_cast<size_t>(i)]);
            const size_t from = c.from;
            c.from = pool.size();
            pool.insert(pool.end(), cpool.begin() + static_cast<std::ptrdiff_t>(from), cpool.begin() + static_cast<std::ptrdiff_t>(from + c.count));
            feetPool.insert(feetPool.end(), cfeet.begin() + static_cast<std::ptrdiff_t>(from), cfeet.begin() + static_cast<std::ptrdiff_t>(from + c.count));
            tree.push_back(std::move(c));
            const int idx = static_cast<int>(tree.size()) - 1;
            if (tree.back().arrived) {
                arrivals.push_back(idx);
                if (firstArrivalGen < 0) firstArrivalGen = gen;
            } else {
                kept.push_back(idx);
            }
        }
        beam = std::move(kept);
    }
    // The plans: the arrivals (the earliest first), else the states nearest the goal.
    std::vector<int> picks = arrivals;
    std::stable_sort(picks.begin(), picks.end(), [&](int a, int b) { return tree[static_cast<size_t>(a)].score < tree[static_cast<size_t>(b)].score; });
    const bool arrived = !picks.empty();
    if (!arrived) {
        picks = beam;
        std::stable_sort(picks.begin(), picks.end(), [&](int a, int b) { return tree[static_cast<size_t>(a)].score < tree[static_cast<size_t>(b)].score; });
    }
    std::vector<Plan> out;
    for (int p : picks) {
        if (static_cast<int>(out.size()) >= budget.proposals) break;
        std::vector<int> chain;
        for (int k = p; k > 0; k = tree[static_cast<size_t>(k)].parent) chain.push_back(k);
        Plan plan;
        plan.arrives = arrived;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const PNode& n = tree[static_cast<size_t>(*it)];
            plan.frames.insert(plan.frames.end(), pool.begin() + static_cast<std::ptrdiff_t>(n.from), pool.begin() + static_cast<std::ptrdiff_t>(n.from + n.count));
            plan.feet.insert(plan.feet.end(), feetPool.begin() + static_cast<std::ptrdiff_t>(n.from), feetPool.begin() + static_cast<std::ptrdiff_t>(n.from + n.count));
        }
        if (plan.frames.empty()) continue;
        const Body& end = tree[static_cast<size_t>(p)].b;
        plan.dashes = end.dashes;
        plan.impulses = end.impulses;
        plan.airJumps = end.airJumpsUsed;
        plan.wallJumps = end.wallJumps;
        out.push_back(std::move(plan));
    }
    if (stats) {
        ++stats->calls;
        stats->proposals += static_cast<int64_t>(out.size());
        if (arrived) stats->arriving += static_cast<int64_t>(out.size());
        stats->simSteps += simSteps;
        stats->ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    return out;
}

// ------------------------------------------------------------------ the lower bound

SpeedCaps SpeedCaps::of(const LevelData& level) {
    SpeedCaps c;
    for (const LevelObject& o : level.objects) {
        if (o.type == "bounce_pad") c.up = std::max(c.up, o.prop<float>("power", 430.0f));
        if (o.type == "moving_platform") c.carry = std::max(c.carry, o.prop<float>("speed", 60.0f));
    }
    return c;
}

namespace {

/// The time (s) to cover `gap` px when the speed is at most `slow` until `tFast` (s), then at most `fast`.
double cover(double gap, double slow, double fast, double tFast) {
    if (gap <= 0.0) return 0.0;
    slow = std::max(1e-3, slow);
    fast = std::max(slow, fast);
    if (gap <= slow * tFast) return gap / slow;
    return tFast + (gap - slow * tFast) / fast;
}

}  // namespace

double lowerBoundSeconds(const Body& b, const Kit& kit, const PlayerConfig& cfg, const SpeedCaps& level, const Rect& goal) {
    if (level.recall) return 0.0;  // a return to an earlier place: no bound from here
    const Rect& r = b.box;
    double gx = std::max({0.0f, goal.x - r.right(), r.x - goal.right()});
    double gUp = std::max(0.0f, r.y - goal.bottom());
    double gDown = std::max(0.0f, goal.y - r.bottom());
    if (gx <= 0.0 && gUp <= 0.0 && gDown <= 0.0) return 0.0;
    // A teleport moves without speed: each one covers its distance at once (at most one per cooldown, and one now).
    const bool teleport = level.teleport;
    // The speeds without a power that costs energy (the run, the slide, the jumps, the wall jump, a
    // blow's knockback, a bounce pad, the platform under it), and what the body has now.
    const double carry = level.carry;
    double hSlow = std::max({cfg.maxRunSpeed, cfg.slideSpeed, std::abs(cfg.wallJumpVelocity.x), std::abs(cfg.knockback.x), cfg.swimSpeed, std::abs(b.vel.x)}) + carry;
    double upSlow = std::max({cfg.jumpVelocity(), cfg.doubleJumpVelocity(), std::abs(cfg.wallJumpVelocity.y), std::abs(cfg.knockback.y), cfg.swimSpeed, cfg.climbSpeed,
                              level.up, -b.vel.y}) +
                    carry;
    double down = std::max({cfg.fastFallSpeed, cfg.maxFallSpeed, b.vel.y, cfg.climbSpeed}) + carry;
    // The powers: held, or that the level may still give (counted as held, ready now).
    const bool dashHeld = kit.hasDash(), dashLevel = level.dash && !dashHeld;
    const double dashSpeed = std::max(dashHeld ? kit.dashSpeed : 0.0f, level.dash ? level.dashSpeed : 0.0f) * std::max(1.0f, b.speedMultiplier);
    const bool impulseHeld = kit.hasImpulse(), impulseLevel = level.impulse && !impulseHeld;
    const double impulse = std::max(impulseHeld ? kit.impulseVelocity : 0.0f, level.impulse ? level.impulseVelocity : 0.0f);
    double hFast = std::max(hSlow, dashSpeed + carry), upFast = std::max(upSlow, std::max(dashSpeed, impulse) + carry);
    if (impulse > 0.0) hFast = std::max(hFast, impulse + carry);  // an impulse aimed sideways
    if (level.grapple) {
        // The grapple pulls and lets go with a boost: no energy, ready at once.
        hSlow += level.grappleSpeed;
        upSlow += level.grappleSpeed;
        hFast += level.grappleSpeed;
        upFast += level.grappleSpeed;
    }
    if (level.groundPound || dashSpeed > 0.0) down = std::max(down, std::max(dashSpeed, 900.0) + carry);
    // When the dash (or the impulse) can first be used: its energy and cooldown.
    auto readyIn = [&](float cost, float cool) {
        double t = std::max(0.0f, cool);
        if (b.energy + 0.001f < cost) t = std::max(t, static_cast<double>(std::max(0.0f, b.energyRegenTimer)) + (cost - b.energy) / std::max(0.01f, cfg.energyRegen));
        return t;
    };
    double tFast = std::numeric_limits<double>::infinity();
    if (dashHeld) tFast = std::min(tFast, readyIn(kit.dashCost, b.dashCool));
    if (impulseHeld) tFast = std::min(tFast, readyIn(kit.impulseCost, b.impulseCool));
    if (dashLevel || impulseLevel) tFast = 0.0;
    if (b.state == PlayerState::Dash) tFast = 0.0;  // already as fast as a dash
    if (!std::isfinite(tFast) && hFast <= hSlow && upFast <= upSlow) tFast = 0.0;
    // A teleport now and one per cooldown: up to (1 + t / cooldown) × distance at once.
    double t = std::max({cover(gx, hSlow, hFast, tFast), cover(gUp, upSlow, upFast, tFast), cover(gDown, down, down, 0.0)});
    if (teleport) {
        const double d = level.teleportDistance, cd = std::max(0.05f, level.teleportCooldown);
        // Each axis: gap <= speed × t + d × (1 + t / cd)  →  t >= (gap - d) / (speed + d / cd).
        auto withTp = [&](double gap, double v) { return gap <= d ? 0.0 : (gap - d) / (v + d / cd); };
        t = std::max({withTp(gx, hFast), withTp(gUp, upFast), withTp(gDown, down)});
    }
    return std::max(0.0, t);
}

// ------------------------------------------------------------------ the planner of a search

Planner::Planner(const Course& course, const ContentDatabase* content) : course_(course), content_(content), terrain_(std::make_unique<Terrain>()) {}
Planner::~Planner() = default;

bool Planner::goalBox(const GameWorld& w, int waypoint, Rect& out) {
    const auto& wps = course_.waypoints();
    if (waypoint < 0 || waypoint >= static_cast<int>(wps.size())) return false;
    const Waypoint& wp = wps[static_cast<size_t>(waypoint)];
    for (const auto& e : w.entities()) {
        if (!e || e->removed) continue;
        const bool exit = wp.kind == Waypoint::Kind::Exit && dynamic_cast<const ExitEntity*>(e.get());
        if (exit || (!wp.objectId.empty() && e->object().id == wp.objectId)) {
            out = e->overlapBox();
            return true;
        }
    }
    // Not an entity (or gone): around its feet.
    out = Rect::fromBottomCenter(wp.feet, {24.0f, 32.0f});
    return true;
}

std::vector<Plan> Planner::propose(const GameWorld& w, int targetWaypoint, uint32_t prevHeld, const PlanBudget& budget) {
    std::vector<Plan> none;
    const Player& p = w.player();
    if (w.cinematic().active() || w.powerOffer() || w.state() != WorldState::Playing) return none;
    if (p.state() == PlayerState::Dead || p.state() == PlayerState::Hidden || p.state() == PlayerState::Respawn) return none;
    Rect box;
    if (!goalBox(w, targetWaypoint, box)) return none;
    terrain_->sync(w, nullptr);
    const Body start = Body::of(p);
    terrain_->plan(budget.horizon + budget.segment, start.grounded ? start.groundId : 0, start.feet());
    const BodySim sim(p.config(), Kit::of(p));
    PlanGoal goal;
    goal.box = box;
    goal.distance = [this, targetWaypoint](Vec2 f) { return course_.distanceTo(targetWaypoint, f); };
    std::vector<Plan> out = planOn(sim, start, prevHeld, terrain_->physics(), terrain_.get(), terrain_->killY(), goal, budget, &stats);
    // A human path (route hints): also toward its point about a second ahead of where Nova is on
    // it, while that point is still on the way to the waypoint.
    if (!hints_.empty()) {
        const Vec2 f = start.feet();
        size_t near = 0;
        for (size_t i = 1; i < hints_.size(); ++i)
            if ((hints_[i].feet - f).lengthSq() < (hints_[near].feet - f).lengthSq()) near = i;
        size_t ahead = near;
        while (ahead + 1 < hints_.size() && hints_[ahead].clock < hints_[near].clock + 1.0f) ++ahead;
        const float here = course_.distanceTo(targetWaypoint, f), there = course_.distanceTo(targetWaypoint, hints_[ahead].feet);
        // Its weight decays once the search matches the human there (as early or earlier).
        if (w.timer().elapsed() <= hints_[near].clock + 0.05f) matched_ = std::min(matched_ + 1, 4);
        const bool due = (hintCalls_++ % (int64_t{1} << matched_)) == 0;
        if (due && ahead != near && std::isfinite(there) && there < here) {
            PlanGoal hint;
            hint.box = Rect::fromBottomCenter(hints_[ahead].feet, {24.0f, 32.0f});
            PlanBudget b = budget;
            b.horizon = std::min(budget.horizon, 480);
            b.proposals = std::max(1, budget.proposals / 2);
            std::vector<Plan> toward = planOn(sim, start, prevHeld, terrain_->physics(), terrain_.get(), terrain_->killY(), hint, b, &stats);
            stats.hinted += static_cast<int64_t>(toward.size());
            for (Plan& p : toward) out.push_back(std::move(p));
        }
    }
    return out;
}

double Planner::lowerBoundSeconds(const GameWorld& w, int targetWaypoint) {
    if (!capsReady_) {
        caps_ = SpeedCaps::of(w.level());
        // Every power the level may give (its stations), as if held (their shipped parameters).
        std::vector<std::string> ids = w.level().startPowers;
        for (const LevelObject& o : w.level().objects)
            if (std::string id = o.propStr("power"); !id.empty()) ids.push_back(id);
        for (const std::string& id : ids) {
            const PowerDefinition* d = content_ ? content_->power(id) : nullptr;
            const std::string b = d ? d->behavior : id;
            auto param = [d](const char* k, float def) { return d ? d->param(k, def) : def; };
            if (b == "dash" || b == "air_dash") caps_.dash = true, caps_.dashSpeed = std::max(caps_.dashSpeed, param("speed", 430.0f));
            if (b == "impulse") caps_.impulse = true, caps_.impulseVelocity = std::max(caps_.impulseVelocity, param("velocity", 380.0f));
            if (b == "teleport")
                caps_.teleport = true, caps_.teleportDistance = std::max(caps_.teleportDistance, param("distance", 80.0f)),
                caps_.teleportCooldown = std::min(caps_.teleportCooldown, d ? d->cooldown : 1.1f);
            if (b == "grapple") caps_.grapple = true, caps_.grappleSpeed = std::max(caps_.grappleSpeed, param("pullSpeed", 430.0f) + param("releaseBoost", 230.0f));
            if (b == "ground_pound") caps_.groundPound = true;
            if (b == "recall") caps_.recall = true;
        }
        capsReady_ = true;
    }
    Rect box;
    if (!goalBox(w, targetWaypoint, box)) return 0.0;
    const Player& p = w.player();
    SpeedCaps c = caps_;
    // The powers held now, with their real parameters.
    for (const auto& pw : p.powers()) {
        const std::string& b = pw->def().behavior;
        if (b == "teleport") c.teleport = true, c.teleportDistance = std::max(c.teleportDistance, pw->def().param("distance", 80.0f)),
                             c.teleportCooldown = std::min(c.teleportCooldown, pw->def().cooldown);
        if (b == "grapple") c.grapple = true, c.grappleSpeed = std::max(c.grappleSpeed, pw->def().param("pullSpeed", 430.0f) + pw->def().param("releaseBoost", 230.0f));
        if (b == "ground_pound") c.groundPound = true;
        if (b == "recall") c.recall = true;
    }
    return tas::lowerBoundSeconds(Body::of(p), Kit::of(p), p.config(), c, box);
}

double Planner::lowerBoundToFinish(const GameWorld& w) {
    const auto& wps = course_.waypoints();
    return wps.empty() ? 0.0 : lowerBoundSeconds(w, static_cast<int>(wps.size()) - 1);
}

}  // namespace cosmic::tas
