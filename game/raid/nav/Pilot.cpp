#include "game/raid/nav/Pilot.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidRun.h"

namespace cosmic::raid::nav {

namespace {

constexpr uint32_t kJumpBit = 1u << static_cast<int>(Action::Jump);
constexpr int kMaxWait = 360;    // the longest it waits for a moving platform (3 s)
constexpr int kStepBudget = 8000;  // simulated steps a group starts choices with in one game step (then: next step)

uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

/// A deterministic number in [0, 1) for a seat at a step (its imprecision; no random generator).
float noise(int seat, uint32_t tick, uint32_t salt) {
    uint32_t x = static_cast<uint32_t>(seat + 1) * 0x9E3779B9u ^ tick * 0x85EBCA6Bu ^ salt * 0xC2B2AE35u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}

int jitterOf(int amount, int seat, uint32_t tick, uint32_t salt) {
    if (amount <= 0) return 0;
    return static_cast<int>(std::floor(noise(seat, tick, salt) * static_cast<float>(2 * amount + 1))) - amount;
}

uint8_t shifted(uint8_t v, int by) {
    if (v == 0) return 0;
    return static_cast<uint8_t>(std::clamp(static_cast<int>(v) + by, 1, 250));
}

Intent intentOf(const Program& p, const Outcome& o, bool mover, bool recover) {
    if (recover) return Intent::Recover;
    if (mover) return Intent::JumpMover;
    switch (p.style()) {
        case Style::DoubleJump: return o.overVoid ? Intent::DoubleJumpVoid : Intent::DoubleJump;
        case Style::TripleJump: return Intent::TripleJump;
        case Style::Dash: return o.overVoid ? Intent::DashVoid : Intent::Dash;
        case Style::LongJump: return Intent::LongJump;
        case Style::Impulse: return Intent::Impulse;
        case Style::WallJump: return Intent::WallJump;
        case Style::DropThrough: return Intent::DropThrough;
        case Style::Drop: return Intent::Drop;
        case Style::Jump: return Intent::Jump;
        default: return Intent::Walk;
    }
}

Intent dodgeIntent(Harm h, bool magma) {
    switch (h) {
        case Harm::Shot: return Intent::DodgeShot;
        case Harm::Beam: return Intent::DodgeBeam;
        case Harm::Blow: return Intent::DodgeBlow;
        case Harm::Liquid: return magma ? Intent::FleeLiquid : Intent::Shelter;
        default: return Intent::AvoidHazard;
    }
}

}  // namespace

Skill Skill::of(int level) {
    Skill s;
    switch (level) {
        case 0:  // Débutant: slow to react, a shallow lookahead, its timing a little off
            s.reaction = 36;
            s.budget = 2000;
            s.horizon = 200;
            s.look = 24;
            s.jitter = 2;
            s.powers = true;  // every move: its depth (the budget of a choice) keeps it to the simplest ones
            break;
        case 2:  // Expert
            s.reaction = 6;
            s.budget = 6000;
            s.horizon = 240;
            s.look = 48;
            s.jitter = 0;
            s.powers = true;
            break;
        default:  // Confirmé
            s.reaction = 18;
            s.budget = 4000;
            s.horizon = 220;
            s.look = 32;
            s.jitter = 1;
            s.powers = true;
            break;
    }
    return s;
}

void Pilot::reset() { *this = Pilot{}; }

void Pilot::think(Intent i, uint32_t tick) {
    if (thought_.intent == i) return;
    thought_.intent = i;
    thought_.since = tick;
}

// ----------------------------------------------------------------------------- the group

void Navigator::reset() {
    graph_.reset();
    pending_ = {};
    due_ = 0;
    pendingKey_ = 0;
    graphKey_ = 0;
    zoneSerial_ = -1;
    changed_ = false;
    tick_ = 0;
    for (Pilot& p : pilots_) p.reset();
}

void Navigator::update(const RaidRun& run, uint32_t tick, const std::vector<Rect>& blows) {
    std::vector<Rect> passages;
    for (const Passage& pa : run.rules().passages) {
        passages.push_back(pa.rect);
        // 2.78: a passage of the group is taken by standing on its plate (the armory's « Prêts »): that plate is
        // kept out of too, but by the players sent to hold it (SeatPlan::mayLeave).
        if (!pa.plate.empty())
            for (const CoopPlate& c : run.rules().coops)
                if (c.id == pa.plate) passages.push_back(c.rect);
    }
    update(run.world(), run.zoneSerial(), tick, blows, passages, &run);
}

void Navigator::update(const GameWorld& w, int zoneSerial, uint32_t tick, const std::vector<Rect>& blows, const std::vector<Rect>& keepOut,
                       const RaidRun* run) {
    tick_ = tick;
    changed_ = false;
    stepSteps_ = 0;
    periodic_ = run && run->rules().magma.present ? run->rules().magma.zone : std::string();
    keepOut_ = keepOut;
    terrain_.sync(w, run);
    terrain_.setKeepOut(keepOut_);
    terrain_.setBlows(blows);
    // The graph: asked for when a world is loaded or a door changed (checked every half second), used
    // `delay` steps later — a step fixed by the zone, the same in every run, cached or not.
    const bool newZone = zoneSerial_ != zoneSerial;
    if (newZone) {
        zoneSerial_ = zoneSerial;
        graph_.reset();
        graphKey_ = 0;
        pending_ = {};
        for (Pilot& p : pilots_) {
            p.active_ = false;
            p.airborne_ = false;
            p.failed_.clear();
        }
        changed_ = true;
    }
    if (newZone || tick % 60 == 0) {
        const uint64_t sig = NavGraph::signature(w, periodic_, keepOut_);
        const uint64_t have = pending_.valid() ? pending_.key : graphKey_;
        if (sig != have) {
            pending_ = NavGraph::request(w, periodic_, keepOut_);
            due_ = tick + static_cast<uint32_t>(pending_.delay);
        }
    }
    if (pending_.valid() && tick >= due_) {
        const auto t0 = std::chrono::steady_clock::now();
        const bool late = !pending_.finished();
        graph_ = pending_.get();
        graphKey_ = pending_.key;
        pending_ = {};
        if (late) {
            ++cost_.graphWaits;
            cost_.graphWaitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        for (Pilot& p : pilots_) {
            p.active_ = false;
            p.airborne_ = false;
            p.failed_.clear();
        }
        changed_ = true;
    }
}

uint8_t Navigator::keepOutAt(int node) const {
    const NavGraph* g = graph_.get();
    if (!g || node < 0 || node >= g->size()) return 0;
    return g->node(node).out;
}

int Navigator::spend(int steps) {
    stepSteps_ += steps;
    cost_.bodySteps += steps;
    cost_.maxStepBodySteps = std::max<int64_t>(cost_.maxStepBodySteps, stepSteps_);
    return kStepBudget - stepSteps_;
}

uint64_t Navigator::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (const Pilot& p : pilots_) {
        h = mix(h, static_cast<uint64_t>(p.active_) | (static_cast<uint64_t>(p.t_) << 8) | (static_cast<uint64_t>(static_cast<uint32_t>(p.target_)) << 32));
        h = mix(h, static_cast<uint64_t>(p.thought_.intent) | (static_cast<uint64_t>(p.prog_.jump) << 8) | (static_cast<uint64_t>(p.prog_.air1) << 16) |
                       (static_cast<uint64_t>(p.prog_.wait) << 24) | (static_cast<uint64_t>(p.prog_.dash) << 32));
    }
    return mix(h, graphKey_);
}

void Navigator::start(Pilot& p, const Program& prog, const std::vector<Vec2>& track, Intent intent) {
    p.prog_ = prog;
    p.ex_ = Exec{};
    p.t_ = 0;
    p.track_ = track;
    p.active_ = true;
    p.airborne_ = false;
    p.moveIntent_ = intent;
    // 2.90 (the escape): a blow seen on its way is not forgotten because a move begins (hopping from node to node, it never
    // reacted: each new move set it back to nothing).
    if (!escapeMode_ || p.dodge_) p.threat_ = 0;
}

// ----------------------------------------------------------------------------- a move chosen ahead

bool Navigator::search(const GameWorld& w, int seat, Pilot& p, const Body& body, const BodySim& sim, int src, int dst, uint32_t prevHeld, bool recover) {
    const NavGraph& nav = *graph_;
    ++cost_.searches;
    ++p.used_.searches;
    const PlayerConfig& cfg = sim.config();
    const Vec2 feet = body.feet();
    terrain_.plan(skill_.horizon + kMaxWait, body.grounded ? body.groundId : 0, feet);
    TerrainArena arena(terrain_);
    // The kit it plays with (a level without its powers would keep the dash and the impulse to catch itself).
    Kit kit = sim.kit();
    if (!skill_.powers && !recover) {
        kit.dashKey = -1;
        kit.impulseKey = -1;
    }
    const BodySim play(cfg, kit);
    struct Target {
        int node, seg;
        uint32_t mover;
        int pathIndex;
    };
    std::vector<Target> targets;
    auto targetOf = [&](int n, int idx) {
        const NavGraph::Node& nd = nav.node(n);
        Target t{n, nav.segment(n), nd.mover >= 0 ? nav.movers()[static_cast<size_t>(nd.mover)].collider : 0u, idx};
        for (const Target& o : targets)
            if (o.node == n || (o.seg == t.seg && t.mover == 0)) return;
        targets.push_back(t);
    };
    if (!recover) {
        targetOf(dst, -1);
    } else {
        // Caught in the air: where it was going if it can, else the nearest floors around (below first).
        if (dst >= 0) targetOf(dst, -1);
        for (int pass = 0; pass < 2 && targets.size() < 4; ++pass)
            for (float r : {90.0f, 180.0f, 280.0f}) {
                const int n = nav.nearest(feet + Vec2{0.0f, pass == 0 ? 60.0f : -30.0f}, r, [&](const NavGraph::Node& nd) {
                    if (nd.ladder || nd.out != 0) return false;  // never into a passage to catch itself
                    for (const Target& o : targets)
                        if (o.seg == nav.segment(static_cast<int>(&nd - &nav.node(0)))) return false;
                    return pass == 0 ? nd.feet.y >= feet.y - 8.0f : true;
                });
                if (n >= 0) targetOf(n, -1);
                if (targets.size() >= 4) break;
            }
    }
    if (targets.empty()) return false;
    const uint8_t can = static_cast<uint8_t>((kit.hasDash() ? NeedDash : 0) | (kit.hasImpulse() ? NeedImpulse : 0) | (kit.airJumps >= 2 ? NeedExtraJump : 0));
    const NavGraph::Edge* edge = (!recover && src >= 0) ? nav.edgeBetween(src, dst, can) : nullptr;
    // A passage only when its target is in it (or it stands in one).
    {
        const NavGraph::Node& goalNode = nav.node(targets.front().node);
        int here = 0;
        for (size_t k = 0; k < keepOut_.size(); ++k)
            if (keepOut_[k].intersects(body.box)) here = static_cast<int>(k) + 1;
        terrain_.allowOut(goalNode.out != 0 ? goalNode.out : here);
    }
    const uint32_t ridden = body.grounded ? body.groundId : 0;
    bool onMover = false;
    for (const auto& m : nav.movers())
        if (m.collider == ridden && ridden != 0) onMover = true;
    const Collider* under = body.grounded ? terrain_.physics().get(body.groundId) : nullptr;
    const bool onOneWay = under && under->kind == ColliderKind::OneWay;
    int budget = skill_.budget;
    Program best;
    Outcome bestO;
    float bestScore = 1e30f;
    const Target* bestT = nullptr;
    std::vector<Program> lib, cands;
    for (const Target& T : targets) {
        const NavGraph::Node& TN = nav.node(T.node);
        auto tx = [&](int k) {
            if (T.mover) {
                Rect r;
                if (terrain_.moverBox(T.mover, k, r)) return r.center().x;
            }
            return TN.feet.x;
        };
        auto accept = [&](const Body& b, int) {
            if (!b.grounded) return false;
            if (T.mover) return b.groundId == T.mover;
            const Vec2 f = b.feet();
            const int n = nav.nearest(f, 12.0f, [&](const NavGraph::Node& nd) { return nd.mover < 0 && !nd.ladder && std::fabs(nd.feet.y - f.y) < 1.5f; });
            return n >= 0 && nav.segment(n) == T.seg;
        };
        const float dx = TN.feet.x - feet.x, up = feet.y - TN.feet.y;
        cands.clear();
        if (!recover) {
            if (edge && edge->program >= 0 && edge->program < static_cast<int>(nav.programs().size())) cands.push_back(nav.programs()[static_cast<size_t>(edge->program)]);
            int wallSide = 0;
            for (int side : {-1, 1}) {
                const Rect probe{feet.x + static_cast<float>(side) * 30.0f - 8.0f, feet.y - 60.0f, 16.0f, 50.0f};
                MoveOptions opt;
                opt.ignoreTags = ColliderTag::Teammate | ColliderTag::Moving;
                if (terrain_.physics().overlapsSolid(probe, opt) && static_cast<float>(side) * dx <= 0.0f && up > 40.0f) wallSide = side;
            }
            library(lib, dx, up, kit, onOneWay, wallSide);
            for (const Program& q : lib)
                if (std::find(cands.begin(), cands.end(), q) == cands.end()) cands.push_back(q);
        } else {
            // What is left in the air: steer, an air jump now or a little later, a dash, the impulse.
            const int8_t dir = dx > 4.0f ? 1 : dx < -4.0f ? -1 : 0;
            Program base;
            base.dir = dir;
            cands.push_back(base);
            if (body.airJumpsLeft > 0)
                for (uint8_t a : {1, 8, 18, 30}) {
                    Program q = base;
                    q.air1 = a;
                    q.hold1 = 40;
                    cands.push_back(q);
                }
            if (body.airJumpsLeft > 1) {
                Program q = base;
                q.air1 = 1;
                q.hold1 = 20;
                q.air2 = 28;
                q.hold2 = 40;
                cands.push_back(q);
            }
            if (kit.hasDash() && body.dashAir > 0 && dir != 0)
                for (uint8_t d : {1, 10})
                    for (int8_t dy : {int8_t{0}, int8_t{-1}}) {
                        Program q = base;
                        q.dash = d;
                        q.dashY = dy;
                        cands.push_back(q);
                        if (body.airJumpsLeft > 0) {
                            q.air1 = static_cast<uint8_t>(d + 24);
                            q.hold1 = 40;
                            cands.push_back(q);
                        }
                    }
            if (kit.hasImpulse() && body.impulseUses > 0)
                for (uint8_t i : {1, 12}) {
                    Program q = base;
                    q.impulse = i;
                    cands.push_back(q);
                    if (body.airJumpsLeft > 0) {
                        q.air1 = static_cast<uint8_t>(i + 34);
                        q.hold1 = 40;
                        cands.push_back(q);
                    }
                }
        }
        // The jump is let go one step first when it is held now (a press is a new hold).
        if (prevHeld & kJumpBit)
            for (Program& q : cands)
                if ((q.jump > 0 || q.drop) && q.wait == 0) q.wait = 1;
        auto evaluate = [&](const Program& q, int maxSteps) {
            const Outcome o = simulate(q, body, prevHeld, play, arena, tx, accept, maxSteps);
            ++cost_.simulations;
            budget -= o.steps;
            spend(o.steps);
            if (!o.accepted) return false;
            const float landX = std::fabs(o.feet.x - TN.feet.x);
            float score = static_cast<float>(o.steps) + 5.0f * static_cast<float>(o.airJumps) + (recover ? 9.0f : dashCost_) * static_cast<float>(o.dashes) +
                          (recover ? 12.0f : dashCost_ + 3.0f) * static_cast<float>(o.impulses) + 6.0f * static_cast<float>(o.wallJumps) + landX * 0.4f;
            if (recover) score += static_cast<float>(&T - targets.data()) * 40.0f;  // where it was going first
            if (score < bestScore) {
                bestScore = score;
                best = q;
                bestO = o;
                bestT = &T;
            }
            return true;
        };
        bool any = false;
        for (const Program& q : cands) {
            if (budget <= 0) break;
            any = evaluate(q, skill_.horizon) || any;
        }
        // A moving platform at either end: when it will be there — waiting (standing, or carried) until a
        // move lands, the earliest first.
        if (!any && !recover && (T.mover != 0 || onMover)) {
            const size_t shapes = std::min<size_t>(cands.size(), 4);
            for (int wait = 4; wait <= kMaxWait && budget > 0 && !any; wait += 4)
                for (size_t c = 0; c < shapes && budget > 0; ++c) {
                    Program q = cands[c];
                    q.wait = static_cast<uint8_t>(std::min(250, static_cast<int>(q.wait) + wait));
                    if (wait > 240) {
                        // Beyond what one program counts: the rest is waited before choosing again.
                        q.wait = 240;
                    }
                    if (evaluate(q, skill_.horizon + q.wait)) {
                        any = true;
                        break;
                    }
                }
        }
        if (!recover && any) break;  // its target: no other
    }
    if (!bestT) return false;
    // Its track (the feet the game should follow), then its timing as its level plays it.
    std::vector<Vec2> track;
    {
        const Target& T = *bestT;
        const NavGraph::Node& TN = nav.node(T.node);
        auto tx = [&](int k) {
            if (T.mover) {
                Rect r;
                if (terrain_.moverBox(T.mover, k, r)) return r.center().x;
            }
            return TN.feet.x;
        };
        const Outcome o = simulate(best, body, prevHeld, play, arena, tx, [](const Body&, int) { return true; }, bestO.steps, &track);
        spend(o.steps);
    }
    Program played = best;
    if (skill_.jitter > 0) {
        const int j1 = jitterOf(skill_.jitter, seat, tick_, 1), j2 = jitterOf(skill_.jitter, seat, tick_, 2);
        played.air1 = shifted(played.air1, j1);
        played.dash = shifted(played.dash, j2);
        played.impulse = shifted(played.impulse, j2);
        if (played.wait > 0) played.wait = shifted(played.wait, j1);
    }
    const bool towardMover = bestT->mover != 0;
    Intent intent = intentOf(best, bestO, towardMover, recover);
    start(p, played, track, intent);
    p.target_ = bestT->node;
    p.targetSeg_ = bestT->seg;
    p.targetMover_ = bestT->mover;
    p.pathIndex_ = bestT->pathIndex;
    p.dodge_ = false;
    p.safe_ = false;
    if (recover) ++p.used_.recoveries;
    if (best.jump > 0) ++p.used_.jumps;
    p.used_.airJumps += bestO.airJumps;
    p.used_.dashes += bestO.dashes;
    p.used_.impulses += bestO.impulses;
    p.used_.wallJumps += bestO.wallJumps;
    if (towardMover) ++p.used_.moverJumps;
    (void)w;
    return true;
}

bool Navigator::dodge(const GameWorld& w, int seat, Pilot& p, const Body& body, const BodySim& sim, uint32_t prevHeld, Harm seen, float towardX) {
    (void)w;
    (void)seat;
    ++cost_.searches;
    terrain_.plan(skill_.horizon + kMaxWait, body.grounded ? body.groundId : 0, body.feet());
    {
        int here = 0;
        for (size_t k = 0; k < keepOut_.size(); ++k)
            if (keepOut_[k].intersects(body.box)) here = static_cast<int>(k) + 1;
        terrain_.allowOut(here);
    }
    TerrainArena arena(terrain_);
    const Kit& kit = sim.kit();
    const int8_t ahead = towardX > body.feet().x + 2.0f ? 1 : towardX < body.feet().x - 2.0f ? -1 : static_cast<int8_t>(body.facing >= 0.0f ? 1 : -1);
    std::vector<Program> cands;
    auto add = [&](Program q) {
        if (prevHeld & kJumpBit)
            if ((q.jump > 0 || q.drop) && q.wait == 0) q.wait = 1;
        cands.push_back(q);
    };
    {
        Program q;  // stay where it is
        q.wait = 110;
        add(q);
    }
    {
        Program q;  // crouched (a smaller body: what passes over it)
        q.crouch = true;
        q.wait = 110;
        add(q);
    }
    for (int8_t d : {ahead, static_cast<int8_t>(-ahead)}) {
        Program walk;  // a few steps aside, then still (its stop is part of the dodge)
        walk.dir = d;
        walk.steer = false;
        walk.stop = 50;
        add(walk);
        Program jump;
        jump.dir = d;
        jump.jump = 40;
        jump.steer = false;
        add(jump);
        Program dbl = jump;
        dbl.air1 = 30;
        dbl.hold1 = 40;
        add(dbl);
        if (kit.hasDash()) {
            Program dash;  // invulnerable while it dashes
            dash.dir = d;
            dash.groundDash = 1;
            dash.run = 6;
            dash.steer = false;
            dash.stop = 24;
            add(dash);
        }
    }
    {
        Program up;
        up.jump = 40;
        up.steer = false;
        add(up);
        up.air1 = 30;
        up.hold1 = 40;
        add(up);
    }
    const int horizon = 110;  // long enough to see a shot that would catch it up
    Program best;
    float bestScore = 1e30f;
    bool found = false;
    for (const Program& q : cands) {
        const Outcome o = simulate(q, body, prevHeld, sim, arena, [&](int) { return towardX; }, [](const Body&, int) { return true; }, horizon);
        ++cost_.simulations;
        spend(o.steps);
        if (o.harm != Harm::None) continue;  // nothing hurts it on the whole horizon (in the air or not)
        // Still in the air at the end: it must be over a floor it can reach (the next steps check it).
        float score = std::fabs(o.feet.x - towardX) + 25.0f * static_cast<float>(o.airJumps) + (dashCost_ > 9.0f ? 70.0f : 25.0f) * static_cast<float>(o.dashes) + (q.crouch ? 6.0f : 0.0f);
        if (!o.landed && q.jump > 0) score += 40.0f;
        if (score < bestScore) {
            bestScore = score;
            best = q;
            found = true;
        }
    }
    if (!found) return false;
    std::vector<Vec2> track;
    const Outcome o = simulate(best, body, prevHeld, sim, arena, [&](int) { return towardX; }, [](const Body&, int) { return true; }, horizon, &track);
    spend(o.steps);
    start(p, best, track, best.crouch ? Intent::Duck : dodgeIntent(seen, terrain_.liquidIsMagma()));
    p.dodge_ = true;
    p.target_ = -1;
    p.targetSeg_ = -1;
    p.targetMover_ = 0;
    p.pathIndex_ = -1;
    ++p.used_.dodges;
    p.used_.dashes += o.dashes;
    p.used_.airJumps += o.airJumps;
    return true;
}

bool Navigator::escape(const Body& body, Pilot& p, const BodySim& sim, uint32_t prevHeld, Harm seen, float towardX, int harmAt, int harmEnd, float dashWeight) {
    // 2.90 (docs/RAIDS.md §36): a blow will land where it stands (or where it is going) in `harmAt` steps. Each way out a
    // player has is played on the copy of the zone against every blow with its time (where they are when they land, not
    // their boxes all at once): standing still, a few steps more or less far on either side (off its platform too: the fall
    // is simulated), a jump, a double jump, a dash at once or timed so that its invulnerability (its duration + 0,05 s)
    // covers the blow, a few steps then a dash. The one clear of every blow for the whole horizon, nearest to where it
    // was going, a dash weighed (its energy), never ending against a blow (12 px); it is then played to its end — the
    // tactic does not change its direction meanwhile, the check of its track replans it only for a blow that came since.
    ++cost_.searches;
    constexpr int kHorizon = 150;
    terrain_.plan(kHorizon + 2, body.grounded ? body.groundId : 0, body.feet());
    {
        int here = 0;
        for (size_t k = 0; k < keepOut_.size(); ++k)
            if (keepOut_[k].intersects(body.box)) here = static_cast<int>(k) + 1;
        terrain_.allowOut(here);
    }
    // In the boss's body now (it walked into it, a knock): out of it within a third of a second, then never back.
    terrain_.dodgeView(true, terrain_.inBody(body.box) ? kSimHz / 3 : 0);
    TerrainArena arena(terrain_);
    const Kit& kit = sim.kit();
    const int8_t ahead = towardX > body.feet().x + 2.0f ? 1 : towardX < body.feet().x - 2.0f ? -1 : static_cast<int8_t>(body.facing >= 0.0f ? 1 : -1);
    const int hit = std::clamp(harmAt, 1, kHorizon);
    std::vector<Program> cands;
    auto add = [&](Program q) {
        if (prevHeld & kJumpBit)
            if ((q.jump > 0 || q.drop) && q.wait == 0) q.wait = 1;
        cands.push_back(q);
    };
    {
        Program q;  // stays where it is (the blow lands on its way: it waits for it)
        q.wait = kHorizon;
        add(q);
    }
    for (int8_t d : {ahead, static_cast<int8_t>(-ahead)}) {
        for (uint8_t stop : {8, 14, 22, 32, 44, 58, 76, 98, 126}) {
            Program walk;  // a few steps aside (off its platform: the fall), then still
            walk.dir = d;
            walk.steer = false;
            walk.stop = stop;
            add(walk);
        }
        Program jump;
        jump.dir = d;
        jump.jump = 40;
        jump.steer = false;
        add(jump);
        Program dbl = jump;
        dbl.air1 = 30;
        dbl.hold1 = 40;
        add(dbl);
        if (kit.hasDash()) {
            Program dash;  // a dash at once (invulnerable while it dashes), then still
            dash.dir = d;
            dash.groundDash = 1;
            dash.steer = false;
            dash.stop = 21;
            add(dash);
            if (hit > 10) {
                Program late = dash;  // standing until the blow, then the dash: its invulnerability covers the blow
                late.wait = static_cast<uint8_t>(std::min(hit - 5, 200));
                late.stop = static_cast<uint8_t>(std::min(late.wait + 21, 250));
                add(late);
                Program run = dash;  // running toward its way out, the dash as the blow lands
                run.groundDash = static_cast<uint8_t>(std::min(hit - 4, 200));
                run.stop = static_cast<uint8_t>(std::min(run.groundDash + 20, 250));
                add(run);
            }
        }
    }
    {
        Program up;  // straight up (a double jump: onto the platform over it, through its underside)
        up.jump = 40;
        up.steer = false;
        add(up);
        up.air1 = 30;
        up.hold1 = 40;
        add(up);
    }
    // Through the platform it stands on (down and jump), straight down or to either side.
    if (const Collider* under = body.grounded ? terrain_.physics().get(body.groundId) : nullptr; under && under->kind == ColliderKind::OneWay)
        for (int8_t d : {static_cast<int8_t>(0), ahead, static_cast<int8_t>(-ahead)}) {
            Program drop;
            drop.drop = true;
            drop.dir = d;
            drop.steer = false;
            drop.release = 14;
            add(drop);
        }
    Program best;
    float bestScore = 1e30f;
    bool found = false;
    Program least;  // none clear of every blow: the one hurt the latest (what comes after may be seen in time)
    int leastAt = hit + 15;
    bool any = false;
    const auto movedTill = [](const std::vector<Vec2>& tr) {
        for (size_t i = 2; i < tr.size(); ++i)
            if ((tr[i] - tr[i - 1]).lengthSq() < 0.0025f && (tr[i - 1] - tr[i - 2]).lengthSq() < 0.0025f) return static_cast<int>(i);
        return static_cast<int>(tr.size());
    };
    // A move through the air ends as it lands (the simulation stops there): where it lands is then watched standing still
    // to the horizon (the first step a blow is on it there, 1 << 20 none).
    const auto standsClear = [&](const Outcome& o) {
        if (!o.landed) return 1 << 20;
        const Rect box = Rect::fromBottomCenter(o.feet, body.box.size());
        for (int k = o.steps + 1; k <= kHorizon; ++k) {
            const Harm h = terrain_.harm(box, k);
            if (h == Harm::Blow || h == Harm::Shot || h == Harm::Beam || h == Harm::Liquid || h == Harm::Hazard) return k;
        }
        return 1 << 20;
    };
    // Clear of the blow it leaves (until it has landed and passed: a fist strikes a quarter of a second, a crack bursts three
    // tenths) is what counts; a blow that lands later where it ends only weighs (it will be left in its turn, its time known).
    const int clearTill = std::clamp(harmEnd >= hit ? harmEnd + 2 : hit + 8, hit + 4, kHorizon);
    std::vector<Vec2> tr;
    for (const Program& q : cands) {
        const Outcome o = simulate(q, body, prevHeld, sim, arena, [&](int) { return towardX; }, [](const Body&, int) { return true; }, kHorizon, &tr,
                                   static_cast<int>(Harm::Liquid));
        ++cost_.simulations;
        spend(o.steps);
        int first = o.harm != Harm::None ? o.harmStep : 1 << 20;
        first = std::min(first, standsClear(o));
        if (o.harm >= Harm::Hazard) continue;  // spikes, a liquid, the void: never
        if (first <= clearTill) {
            if (first > leastAt) {
                leastAt = first;
                least = q;
                any = true;
            }
            continue;
        }
        if (!o.landed && o.ground == 0) continue;  // still in the air at its end: not a place yet
        float score = std::fabs(o.feet.x - towardX) + 0.5f * std::fabs(o.feet.y - body.feet().y) + 30.0f * static_cast<float>(o.airJumps) +
                      dashWeight * static_cast<float>(o.dashes);
        if (first < (1 << 20))
            score += 80.0f * (1.0f - static_cast<float>(first - clearTill) / static_cast<float>(std::max(1, kHorizon - clearTill)));
        // Never ending against a blow still to come (a step of a teammate, a knock, and it is in it).
        const int rest = movedTill(tr);
        if (terrain_.blowNear(Rect::fromBottomCenter(o.feet, body.box.size()).expanded(12.0f), rest)) score += 40.0f;
        if (score < bestScore) {
            bestScore = score;
            best = q;
            found = true;
        }
    }
    bool safe = true;
    if (!found) {
        if (!any) {
            terrain_.dodgeView(false);
            ++p.used_.escFails;
            return false;
        }
        best = least;
        safe = false;
    }
    std::vector<Vec2> track;
    const Outcome o = simulate(best, body, prevHeld, sim, arena, [&](int) { return towardX; }, [](const Body&, int) { return true; }, kHorizon, &track,
                               static_cast<int>(Harm::Liquid));
    spend(o.steps);
    terrain_.dodgeView(false);
    // Played until it is out of it and the blow has landed (then its way again, watched as before).
    const int end = std::clamp(std::max(movedTill(track), hit + 12), 1, static_cast<int>(track.size()));
    track.resize(static_cast<size_t>(end));
    start(p, best, track, dodgeIntent(seen, terrain_.liquidIsMagma()));
    p.dodge_ = true;
    p.safe_ = safe;
    p.target_ = -1;
    p.targetSeg_ = -1;
    p.targetMover_ = 0;
    p.pathIndex_ = -1;
    ++p.used_.dodges;
    p.used_.dashes += o.dashes;
    p.used_.airJumps += o.airJumps;
    if (!safe) ++p.used_.escFails;
    else if (o.dashes > 0) ++p.used_.escDashes;
    else if (best.jump > 0 || std::fabs(o.feet.y - body.feet().y) > 8.0f) ++p.used_.escJumps;
    else if (best.dir == 0) ++p.used_.escStays;
    else ++p.used_.escWalks;
    return true;
}

// ----------------------------------------------------------------------------- one seat's step

bool Navigator::reaches(const GameWorld& w, int seat, int goal) const {
    const NavGraph* nav = graph_.get();
    if (!nav || goal < 0 || goal >= nav->size() || seat < 0 || seat >= w.teamSize()) return true;
    const Player& me = w.playerAt(seat);
    const int from = nav->locate(me.feet());
    if (from < 0) return true;
    const Kit kit = Kit::of(me);
    const uint8_t can = static_cast<uint8_t>((kit.hasDash() && skill_.powers ? NeedDash : 0) | (kit.hasImpulse() && skill_.powers ? NeedImpulse : 0) |
                                             (kit.airJumps >= 2 ? NeedExtraJump : 0));
    std::vector<int> way;
    return nav->path(from, goal, way, 20000, can);
}

Pad Navigator::steer(const GameWorld& w, int seat, SeatPlan& plan, uint32_t prevHeld) {
    const auto clock0 = std::chrono::steady_clock::now();
    Pilot& p = pilots_[static_cast<size_t>(seat)];
    ++cost_.steps;
    auto finish = [&](Pad pad) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clock0).count();
        cost_.totalMs += ms;
        cost_.maxStepMs = std::max(cost_.maxStepMs, ms);
        return pad;
    };
    const NavGraph* nav = graph_.get();
    if (!nav) {
        p.think(Intent::Observe, tick_);
        p.active_ = false;
        return finish({});
    }
    if (p.cooldown_ > 0) --p.cooldown_;
    p.failed_.erase(std::remove_if(p.failed_.begin(), p.failed_.end(), [&](const Pilot::Failed& f) { return f.until <= tick_; }), p.failed_.end());
    const Player& me = w.playerAt(seat);
    Body body = Body::of(me);
    // 2.90 (the escape): under its shield or the guard after a dash, as long as they last (the simulation lets the blows, shots
    // and beams through an invulnerable body): a blow that lands after is still one to leave.
    if (plan.escape) body.invuln = std::max(body.invuln, plan.guardFor);
    escapeMode_ = plan.escape;
    const Kit kit = Kit::of(me);
    // 2.84: its energy spared (a fight: its shots and its shield), a dash or an impulse only where nothing else goes.
    dashCost_ = plan.spare ? 150.0f : 9.0f;
    const BodySim sim(me.config(), kit);
    const PlayerConfig& cfg = me.config();
    const uint8_t can = static_cast<uint8_t>((kit.hasDash() && skill_.powers ? NeedDash : 0) | (kit.hasImpulse() && skill_.powers ? NeedImpulse : 0) |
                                             (kit.airJumps >= 2 ? NeedExtraJump : 0));
    const Vec2 feet = body.feet();
    const bool footed = body.grounded || body.state == PlayerState::Climb;
    std::vector<int>& path = *plan.path;
    size_t& at = *plan.at;
    const int now = static_cast<int>(tick_);
    // Where it stands on the graph: a place of the moving platform it rides, else the nearest fixed one.
    auto locateBody = [&]() {
        if (body.grounded)
            for (size_t m = 0; m < nav->movers().size(); ++m)
                if (nav->movers()[m].collider == body.groundId) {
                    const int on = nav->nearest(feet, 400.0f, [&](const NavGraph::Node& nd) { return nd.mover == static_cast<int>(m); });
                    if (on >= 0) return on;
                }
        return nav->locate(feet);
    };
    // A goal in a passage that it must not take (it is not leaving the zone): the nearest place out of it.
    if (!plan.mayLeave && plan.goalNode >= 0 && plan.goalNode < nav->size() && keepOutAt(plan.goalNode) != 0) {
        const int outside = nav->nearest(nav->node(plan.goalNode).feet, 400.0f,
                                         [](const NavGraph::Node& nd) { return nd.out == 0 && nd.mover < 0 && !nd.ladder; });
        if (outside >= 0) {
            plan.goalNode = outside;
            plan.goal = nav->node(outside).feet;
        }
    }
    const uint8_t goalOut = plan.mayLeave ? keepOutAt(plan.goalNode) : 0;
    {
        int hereOut = 0;
        for (size_t k = 0; k < keepOut_.size(); ++k)
            if (keepOut_[k].intersects(body.box)) hereOut = static_cast<int>(k) + 1;
        terrain_.allowOut(goalOut != 0 ? goalOut : hereOut);
    }
    auto avoid = [&](int a, int b) {
        for (const Pilot::Failed& f : p.failed_)
            if (f.a == a && f.b == b) return true;
        const uint8_t out = keepOutAt(b);
        return out != 0 && out != goalOut && keepOutAt(a) != out;  // into a passage that is not its goal
    };
    auto targetX = [&](int k) {
        if (p.targetMover_) {
            Rect r;
            terrain_.plan(skill_.horizon, body.grounded ? body.groundId : 0, feet);
            if (terrain_.moverBox(p.targetMover_, k, r)) return r.center().x;
        }
        if (p.target_ >= 0) return nav->node(p.target_).feet.x;
        return plan.goal.x;
    };
    auto play = [&]() {
        const int c = p.t_;
        Pad pad = padAt(p.prog_, c, body, targetX(1), p.ex_, sim.kit(), cfg);
        ++p.t_;
        p.airborne_ = !footed || p.ex_.takeoff >= 0;
        const bool waiting = c < p.prog_.wait && p.ex_.takeoff < 0;
        p.think(waiting && p.targetMover_ ? Intent::WaitMover : waiting && !p.dodge_ ? Intent::Wait : p.moveIntent_, tick_);
        return pad;
    };
    auto landedOnTarget = [&]() {
        if (p.targetMover_) return body.grounded && body.groundId == p.targetMover_;
        if (p.targetSeg_ < 0) return false;
        const int n = nav->nearest(feet, 12.0f, [&](const NavGraph::Node& nd) { return nd.mover < 0 && !nd.ladder && std::fabs(nd.feet.y - feet.y) < 1.5f; });
        return n >= 0 && nav->segment(n) == p.targetSeg_;
    };

    // 2.90 (the escape): the boss's body on it — its way out now (off its platform, a jump, a dash through it), unless a dodge
    // under way already leaves it; none found: the tactic's step aside.
    if (plan.escape && plan.fleeBody && footed && !(p.active_ && p.dodge_) && p.cooldown_ <= 0 && !plan.hold) {
        const float toward = plan.goal.x;
        if (escape(body, p, sim, prevHeld, Harm::Blow, toward, 1, kSimHz / 5, plan.escapeDash)) {
            if (p.safe_) return finish(play());
            p.active_ = false;  // nothing clear of it: the tactic's own way
        } else {
            p.cooldown_ = 6;
        }
    }
    // 1. A move under way: the game must follow the simulation, else it is planned again.
    if (p.active_) {
        const int c = p.t_;
        const bool off = c >= 1 && (c - 1 >= static_cast<int>(p.track_.size()) || (feet - p.track_[static_cast<size_t>(c - 1)]).length() > 2.5f);
        const bool left = p.ex_.takeoff >= 0;
        if (left && footed) {
            p.active_ = false;
            p.airborne_ = false;
            if (!p.dodge_ && landedOnTarget()) {
                // Where it meant to land: on along its path from that place.
                bool found = false;
                for (size_t k = at; k < path.size() && !found; ++k)
                    if (path[k] == p.target_ || (p.targetSeg_ >= 0 && nav->segment(path[k]) == p.targetSeg_ && k > at)) {
                        at = k;
                        found = true;
                    }
                if (!found) path.clear();
                if (plan.best) *plan.best = 1e30f;
                if (plan.noProgress) *plan.noProgress = 0;
            } else if (!p.dodge_ || plan.escape) {
                path.clear();  // elsewhere: a new path from here (2.90: after a jump or a drop of the escape too)
            }
        } else if (!left && c >= static_cast<int>(p.track_.size())) {
            p.active_ = false;
        } else if (off) {
            p.active_ = false;
            p.airborne_ = false;
            if (footed) path.clear();
        } else {
            // A danger that came up on the way of the move (a shot fired since, a beam): seen for its reaction
            // time, the move is chosen again (a dodge on its feet; in the air, what lands safe). A move with a
            // dash goes through (invulnerable).
            if (c % 3 == 0 && p.prog_.dash == 0 && p.prog_.groundDash == 0 && (body.invuln <= 0.0f || plan.escape)) {
                terrain_.plan(skill_.horizon, body.grounded ? body.groundId : 0, feet);
                Harm seen = Harm::None;
                int seenAt = 0, seenEnd = -1;
                // 2.90 (the escape): a dodge's whole track (to its end), its blows with their time, the boss's body too; any other
                // move to its end (a blow seen beyond it is left once it has landed: not forgotten meanwhile).
                const bool whole = plan.escape && p.dodge_;
                const int end = plan.escape ? static_cast<int>(p.track_.size()) : std::min<int>(static_cast<int>(p.track_.size()), c + skill_.look);
                terrain_.dodgeView(whole);
                for (int i = c; i < end && seen == Harm::None; ++i) {
                    Harm h = terrain_.harm(Rect::fromBottomCenter(p.track_[static_cast<size_t>(i)], body.box.size()), i - c + 1);
                    // 2.90: its guard (the shield, a dash's) lets the blows, shots and beams through as long as it lasts.
                    if ((h == Harm::Shot || h == Harm::Beam || h == Harm::Blow) && static_cast<float>(i - c + 1) <= body.invuln * static_cast<float>(kSimHz)) h = Harm::None;
                    if (h == Harm::Shot || h == Harm::Beam || h == Harm::Blow || h == Harm::Liquid || h == Harm::Hazard) {
                        seen = h;
                        seenAt = i - c + 1;
                        seenEnd = terrain_.blowEnd(Rect::fromBottomCenter(p.track_[static_cast<size_t>(i)], body.box.size()), seenAt);
                    }
                }
                terrain_.dodgeView(false);
                if (seen != Harm::None && !(plan.hold && seen != Harm::Liquid)) {
                    p.threat_ += 3;
                    if (p.threat_ >= skill_.reaction) {
                        const int dst = path.size() >= 2 && at + 1 < path.size() ? path[at + 1] : -1;
                        const float toward = p.target_ >= 0 ? nav->node(p.target_).feet.x : plan.goal.x;
                        const bool again = !footed ? search(w, seat, p, body, sim, -1, dst, prevHeld, true)
                                           : plan.escape ? escape(body, p, sim, prevHeld, seen, toward, seenAt, seenEnd, plan.escapeDash)
                                                         : dodge(w, seat, p, body, sim, prevHeld, seen, toward);
                        if (again) return finish(play());
                        // 2.90 (the escape): a dodge whose way is struck now, and none other clear: given up (its shield, the
                        // tactic's way), never played on into the blow.
                        if (plan.escape && p.dodge_ && footed) {
                            p.active_ = false;
                            p.airborne_ = false;
                            return finish({});
                        }
                    }
                } else if (!plan.escape) {
                    p.threat_ = 0;
                }
            }
            return finish(play());
        }
    }

    // 2. In the air without a move (knocked, a missed landing): it catches itself with what is left.
    if (!footed) {
        if (body.state == PlayerState::Dash || body.state == PlayerState::Hurt) return finish({});
        const int dst = path.size() >= 2 && at + 1 < path.size() ? path[at + 1] : -1;
        if (search(w, seat, p, body, sim, -1, dst, prevHeld, true)) return finish(play());
        p.think(Intent::Recover, tick_);
        Pad pad;
        const int d = steerTo(dst >= 0 ? nav->node(dst).feet.x : feet.x, body, cfg);
        pad.mx = static_cast<int8_t>(d * 127);
        return finish(pad);
    }

    // The zone's periodic liquid soon up (6 s) under it or over its goal: up above it first, where it waits
    // (unless its caller already sends it to shelter).
    if (!plan.flee) {
        const int in = terrain_.liquidIn();
        const Rect L = terrain_.liquidBox();
        auto below = [&](Vec2 f) { return L.w > 0.0f && f.x > L.x - 8.0f && f.x < L.right() + 8.0f && f.y > L.y - 2.0f; };
        if (in >= 0 && in <= 6 * kSimHz && (below(feet) || below(plan.goal))) {
            auto safe = [&](int i) {
                const NavGraph::Node& nd = nav->node(i);
                return nd.mover < 0 && !nd.ladder && !below(nd.feet);
            };
            if (p.shelter_ < 0 || p.shelter_ >= nav->size() || !safe(p.shelter_)) {
                const int here = locateBody();
                p.shelter_ = here >= 0 ? nav->search(here, safe, 6000.0f, nullptr, can) : -1;
            }
            if (p.shelter_ >= 0) {
                plan.goal = nav->node(p.shelter_).feet;
                plan.goalNode = p.shelter_;
                plan.flee = true;
            }
        } else {
            p.shelter_ = -1;
        }
    }
    // 3. On its feet: a path to its goal (again when the goal changed, every 2 s, or when it stalls).
    if (plan.goalNode >= 0) {
        const bool stale = path.empty() || path.back() != plan.goalNode || now - *plan.planned > 240 || *plan.noProgress > 360;
        if (stale && (now - *plan.planned > 30 || path.empty())) {
            const int here = locateBody();
            *plan.planned = now;
            *plan.best = 1e30f;
            *plan.noProgress = 0;
            at = 0;
            if (here < 0 || !nav->path(here, plan.goalNode, path, 20000, can, avoid)) {
                // Out of reach: as near as it can get (never a blind jump).
                if (here < 0 || nav->closest(here, plan.goal, 6000.0f, &path, can, avoid) < 0) path.clear();
            }
        }
    }
    Pad pad;
    int dir = 0;
    const Intent flee = terrain_.liquidIsMagma() ? Intent::FleeLiquid : Intent::Shelter;
    // A few steps ahead on its way (toward `wx`), what will hurt it: a shot, a beam, a blow announced, the
    // rising liquid, a danger of the terrain. Seen for its reaction time, it dodges (true: a dodge starts).
    auto watch = [&](float wx, float towardX) {
        // 2.90 (the escape): far enough ahead to walk out of a blow before it lands (its time known), the walk stopped where
        // it goes (it does not run on past it into what is beyond), the first blow met.
        const int look = plan.escape ? 150 : skill_.look;
        terrain_.plan(std::max(skill_.horizon, look), body.grounded ? body.groundId : 0, feet);
        TerrainArena arena(terrain_);
        Program walk;
        const float wdx = wx - feet.x;
        walk.dir = static_cast<int8_t>(std::fabs(wdx) > 2.0f ? (wdx > 0.0f ? 1 : -1) : 0);
        walk.steer = false;
        if (plan.escape && walk.dir != 0) walk.stop = static_cast<uint8_t>(std::clamp(std::fabs(wdx) * 0.8f + 4.0f, 1.0f, 250.0f));
        const Outcome o = simulate(walk, body, prevHeld, sim, arena, [&](int) { return feet.x; }, [](const Body&, int) { return true; }, look, nullptr,
                                   plan.escape ? 0 : 6);
        spend(o.steps);
        bool danger = o.harm == Harm::Shot || o.harm == Harm::Beam || o.harm == Harm::Blow || o.harm == Harm::Liquid || o.harm == Harm::Hazard;
        if (plan.hold && o.harm != Harm::Liquid) danger = false;  // its post first (a plate, a revival)
        if (!danger) {
            // 2.90 (the escape): a step without it (its goal changed, a step aside) does not make it forget what it saw.
            p.threat_ = plan.escape ? std::max(0, p.threat_ - 2) : 0;
            return false;
        }
        ++p.threat_;
        if (p.threat_ < skill_.reaction || p.cooldown_ > 0) return false;
        const int end = plan.escape ? terrain_.blowEnd(Rect::fromBottomCenter(o.feet, body.box.size()), o.harmStep) : -1;
        if (plan.escape ? escape(body, p, sim, prevHeld, o.harm, towardX, o.harmStep, end, plan.escapeDash) : dodge(w, seat, p, body, sim, prevHeld, o.harm, towardX))
            return true;
        p.cooldown_ = 6;
        return false;
    };
    auto reached = [&](int i) {
        const NavGraph::Node& nd = nav->node(i);
        if (nd.mover >= 0) {
            const NavGraph::Mover& m = nav->movers()[static_cast<size_t>(nd.mover)];
            return body.grounded && body.groundId == m.collider && (nav->moverFeet(w, nd.mover) - nd.feet).length() < 24.0f;
        }
        return std::fabs(feet.x - nd.feet.x) < 7.0f && std::fabs(feet.y - nd.feet.y) < 10.0f;
    };
    if (path.size() < 2) {
        if (watch(plan.goal.x, plan.goal.x)) return finish(play());
        const float dx = plan.goal.x - feet.x;
        dir = std::fabs(dx) > plan.stop && std::fabs(dx) < 40.0f ? (dx > 0.0f ? 1 : -1) : 0;
        p.think(plan.flee ? flee : plan.goalNode >= 0 && path.empty() && std::fabs(dx) > 40.0f ? Intent::Stuck : plan.goalNode >= 0 ? Intent::Wait : Intent::None, tick_);
        pad.mx = static_cast<int8_t>(dir * 127);
        return finish(pad);
    }
    for (size_t k = std::min(path.size() - 1, at + 6); k > at; --k)
        if (reached(path[k])) {
            at = k;
            *plan.best = 1e30f;
            *plan.noProgress = 0;
            break;
        }
    if (at + 1 >= path.size()) {
        // At its goal node: the last pixels toward the goal itself.
        if (watch(plan.goal.x, plan.goal.x)) return finish(play());
        const float dx = plan.goal.x - feet.x;
        dir = std::fabs(dx) > plan.stop && std::fabs(dx) < 40.0f ? (dx > 0.0f ? 1 : -1) : 0;
        p.think(plan.flee ? flee : dir != 0 ? Intent::Walk : Intent::Wait, tick_);
        pad.mx = static_cast<int8_t>(dir * 127);
        return finish(pad);
    }
    int src = path[at], dst = path[at + 1];
    const NavGraph::Edge* edge = nav->edgeBetween(src, dst, can);
    NavGraph::Move move = edge ? edge->move : NavGraph::Move::Walk;
    const NavGraph::Node& S = nav->node(src);
    const NavGraph::Node& T = nav->node(dst);
    {
        const float dist = std::fabs(T.feet.x - feet.x) + std::fabs(T.feet.y - feet.y);
        if (dist < *plan.best - 2.0f) {
            *plan.best = dist;
            *plan.noProgress = 0;
        } else {
            ++*plan.noProgress;
        }
    }
    // Carried by a moving platform: the next move off it (the rides skipped), when it will work.
    const NavGraph::Mover* riding = nullptr;
    for (const auto& m : nav->movers())
        if (body.grounded && body.groundId == m.collider) riding = &m;
    if (riding && (move == NavGraph::Move::Ride || S.mover >= 0)) {
        size_t k = at;
        while (k + 1 < path.size()) {
            const NavGraph::Edge* e = nav->edgeBetween(path[k], path[k + 1], can);
            if (!e || e->move != NavGraph::Move::Ride) break;
            ++k;
        }
        if (k + 1 < path.size()) {
            src = path[k];
            dst = path[k + 1];
            edge = nav->edgeBetween(src, dst, can);
            move = edge ? edge->move : NavGraph::Move::Walk;
            if (p.cooldown_ <= 0) {
                if (spend(0) <= 0) {
                    ++cost_.deferred;
                } else if (search(w, seat, p, body, sim, src, dst, prevHeld, false)) {
                    p.pathIndex_ = static_cast<int>(k + 1);
                    return finish(play());
                } else {
                    p.cooldown_ = 20;  // the platform moves on: tried again a little later
                }
            }
        }
        // Stays near its middle while it waits.
        const Rect* box = nullptr;
        (void)box;
        Rect mb;
        if (terrain_.moverBox(riding->collider, 0, mb)) {
            const float dx = mb.center().x - feet.x;
            dir = std::fabs(dx) > 6.0f ? (dx > 0.0f ? 1 : -1) : 0;
        }
        p.think(Intent::Ride, tick_);
        pad.mx = static_cast<int8_t>(dir * 127);
        return finish(pad);
    }

    // What will hurt it on its way (or where it stands), seen after its reaction time: it dodges.
    {
        const float wx = move == NavGraph::Move::Walk ? T.feet.x : S.feet.x;
        if (watch(wx, T.feet.x)) return finish(play());
    }

    switch (move) {
        case NavGraph::Move::Walk: {
            // On along its floor; it slows down only where it must stop (a move to take off for, its goal).
            const NavGraph::Edge* next = at + 2 < path.size() ? nav->edgeBetween(dst, path[at + 2], can) : nullptr;
            const float dx = T.feet.x - feet.x;
            if (next && next->move == NavGraph::Move::Walk)
                dir = std::fabs(dx) > 1.0f ? (dx > 0.0f ? 1 : -1) : 0;
            else
                dir = steerTo(T.feet.x, body, cfg);
            p.think(plan.flee ? flee : Intent::Walk, tick_);
            break;
        }
        case NavGraph::Move::Climb: {
            const float dx = T.feet.x - feet.x, up = feet.y - T.feet.y;
            dir = std::fabs(dx) > 3.0f ? (dx > 0.0f ? 1 : -1) : 0;
            if (std::fabs(up) > 4.0f && (body.onLadder || S.ladder || T.ladder)) {
                pad.held |= 1u << static_cast<int>(up > 0.0f ? Action::MoveUp : Action::MoveDown);
                pad.my = static_cast<int8_t>(up > 0.0f ? -127 : 127);
                // §40: down a ladder that goes through the one-way platform it stands on (the Poignet-verrou's grates): Down
                // alone never takes a ladder while standing (Player::stepMovement), so it stayed there pressing Down for ever.
                // As a player does: Down + Jump drops through the platform (a fresh press), and the ladder catches it below.
                if (up < 0.0f && body.grounded && body.state != PlayerState::Climb && !(prevHeld & kJumpBit)) {
                    const Player& pl = w.playerAt(seat);
                    const Collider* ground = w.physics().groundBelow(pl.box, pl.moveOptions());
                    if (ground && ground->kind == ColliderKind::OneWay) pad.held |= kJumpBit;
                }
            }
            p.think(Intent::Climb, tick_);
            break;
        }
        case NavGraph::Move::Ride:
            // A platform it is not on yet (it left it): a new path.
            path.clear();
            p.think(Intent::Stuck, tick_);
            break;
        case NavGraph::Move::Jump:
        case NavGraph::Move::Drop: {
            const float dx = S.feet.x - feet.x;
            if (std::fabs(dx) > 6.0f || std::fabs(body.vel.x) > 60.0f) {
                // To its takeoff first (it stops there: the move was found from a standstill).
                dir = steerTo(S.feet.x, body, cfg);
                if (std::fabs(dx) <= 6.0f) dir = 0;
                p.think(plan.flee ? flee : Intent::Walk, tick_);
                break;
            }
            if (p.cooldown_ > 0) {
                p.think(T.mover >= 0 ? Intent::WaitMover : Intent::Wait, tick_);
                break;
            }
            if (spend(0) <= 0) {
                ++cost_.deferred;  // the step's budget is spent: its turn comes next step
                p.think(T.mover >= 0 ? Intent::WaitMover : Intent::Jump, tick_);
                break;
            }
            if (search(w, seat, p, body, sim, src, dst, prevHeld, false)) {
                p.pathIndex_ = static_cast<int>(at + 1);
                return finish(play());
            }
            // No move lands there from here now: that edge is left for a while, another way is looked for
            // (a moving platform: tried again a little later).
            ++p.used_.failed;
            if (T.mover >= 0) {
                p.cooldown_ = 24;
                p.think(Intent::WaitMover, tick_);
            } else {
                p.failed_.push_back({src, dst, tick_ + 6u * kSimHz});
                path.clear();
                p.cooldown_ = 12;
                p.think(Intent::Stuck, tick_);
            }
            break;
        }
    }
    // Pushing against something on its floor (a platform at rest on it, a closed door): that way is left
    // for a while, another is looked for (a hop onto the platform…).
    if (dir != 0 && body.grounded && std::fabs(feet.x - p.lastX_) < 0.05f) {
        if (++p.blocked_ > 24) {
            p.blocked_ = 0;
            p.failed_.push_back({src, dst, tick_ + 3u * kSimHz});
            path.clear();
        }
    } else {
        p.blocked_ = 0;
    }
    p.lastX_ = feet.x;
    if (dir > 0) pad.held |= 1u << static_cast<int>(Action::MoveRight);
    if (dir < 0) pad.held |= 1u << static_cast<int>(Action::MoveLeft);
    pad.mx = static_cast<int8_t>(dir * 127);
    pad.pressed = pad.held & ~prevHeld;
    return finish(pad);
}

}  // namespace cosmic::raid::nav
