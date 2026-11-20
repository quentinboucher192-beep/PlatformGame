#include "game/raid/nav/Maneuver.h"

#include <algorithm>
#include <cmath>

#include "engine/physics/PhysicsWorld.h"
#include "game/gameplay/InputFrame.h"

namespace cosmic::raid::nav {

namespace {
constexpr uint32_t kJumpBit = 1u << static_cast<int>(Action::Jump);
constexpr int kRightBit = static_cast<int>(Action::MoveRight);
constexpr int kLeftBit = static_cast<int>(Action::MoveLeft);
constexpr int kUpBit = static_cast<int>(Action::MoveUp);
constexpr int kDownBit = static_cast<int>(Action::MoveDown);
}  // namespace

uint8_t Program::needs() const {
    uint8_t n = NeedNone;
    if (dash > 0 || groundDash > 0) n |= NeedDash;
    if (impulse > 0) n |= NeedImpulse;
    if (air2 > 0) n |= NeedExtraJump;
    return n;
}

Style Program::style() const {
    if (wall > 0) return Style::WallJump;
    if (impulse > 0) return Style::Impulse;
    if (groundDash > 0 && jump > 0) return Style::LongJump;
    if (dash > 0 || groundDash > 0) return Style::Dash;
    if (air2 > 0) return Style::TripleJump;
    if (air1 > 0) return Style::DoubleJump;
    if (drop) return Style::DropThrough;
    if (jump > 0) return Style::Jump;
    if (dir != 0 && !crouch) return Style::Drop;
    return Style::Stand;
}

int steerTo(float x, const Body& b, const PlayerConfig& cfg) {
    const float dx = x - b.feet().x, v = b.vel.x;
    if (b.grounded) {
        // Let go in time: the ground stops it (its deceleration) right there.
        if (std::fabs(dx) < 1.5f) return 0;
        const float stop = v * std::fabs(v) / (2.0f * cfg.groundDeceleration);
        if (dx > 0.0f) return stop >= dx - 1.0f ? 0 : 1;
        return stop <= dx + 1.0f ? 0 : -1;
    }
    if (std::fabs(dx) < 2.0f && std::fabs(v) < 30.0f) return 0;
    // In the air: push toward it, brake (the other way) when going on would carry it past.
    const float stop = v * std::fabs(v) / (2.0f * cfg.airAcceleration);
    if (dx > 0.0f) return stop >= dx - 1.0f ? -1 : 1;
    return stop <= dx + 1.0f ? 1 : -1;
}

Pad padAt(const Program& p, int t, const Body& b, float targetX, Exec& ex, const Kit& kit, const PlayerConfig& cfg) {
    uint32_t held = 0;
    int mx = 0, my = 0;
    const int jumpAt = p.wait + p.run;  // the jump's press (or the start of a walk off)
    if (ex.takeoff < 0 && t > p.wait && !b.grounded && b.state != PlayerState::Climb) ex.takeoff = t;
    if (t < p.wait) {
        if (p.crouch) my = 127;
    } else if (ex.takeoff < 0) {
        // On its feet: the run, the press of the jump, a drop through, a dash on the ground.
        if (p.drop) {
            if (t == p.wait) {
                held |= kJumpBit;
                my = 127;
            }
        } else if (p.crouch) {
            my = 127;
        } else {
            mx = p.dir;
            if (p.jump > 0 && t >= jumpAt) {
                if (t - jumpAt < p.jump) held |= kJumpBit;
                if (p.late > 0) mx = 0;
            }
            if (p.groundDash > 0 && t == p.wait + p.groundDash - 1 && kit.dashKey >= 0) {
                held |= 1u << kit.dashKey;
                mx = p.dir;
            }
            if (p.stop > 0 && t >= p.stop) mx = 0;
        }
    } else {
        // In the air: the jump still held, the steering, the air jumps, a dash, the impulse, a wall jump.
        const int s = t - ex.takeoff;
        if (p.jump > 0 && t >= jumpAt && t - jumpAt < p.jump) held |= kJumpBit;
        int d = p.dir;
        if (s < p.late)
            d = 0;
        else if (p.release > 0 && s >= p.release)
            d = 0;
        else if (p.steer && !(p.wall > 0 && ex.wallJumps < p.wall))
            d = steerTo(targetX, b, cfg);
        mx = d;
        // The air jumps: from its takeoff, or from its last wall jump (none before).
        const int sa = p.wall == 0 ? s : ex.wallJumps >= p.wall ? t - ex.lastWallStep : -1;
        if (p.air1 > 0 && sa >= p.air1 && sa < p.air1 + std::max<int>(1, p.hold1)) held |= kJumpBit;
        if (p.air2 > 0 && sa >= p.air2 && sa < p.air2 + std::max<int>(1, p.hold2)) held |= kJumpBit;
        if (p.dash > 0 && s == p.dash && kit.dashKey >= 0) {
            held |= 1u << kit.dashKey;
            mx = p.dir;
            my = p.dashY < 0 ? -127 : 0;
        }
        if (p.impulse > 0 && s == p.impulse && kit.impulseKey >= 0) held |= 1u << kit.impulseKey;
        // A wall jump held (let go at once, it would be cut to half its height).
        if (p.wall > 0 && ex.wallJumps > 0 && t - ex.lastWallStep < 18) held |= kJumpBit;
        if (p.wall > 0 && ex.wallJumps < p.wall) {
            if (ex.wallJumps > 0) mx = -ex.lastWall;  // on toward the other wall
            if (b.state == PlayerState::WallSlide) {
                // Off the wall: let go of the jump one step if it is held, then press it, away from the wall.
                if (ex.prevHeld & kJumpBit) {
                    held &= ~kJumpBit;
                    mx = b.wallDir;
                } else {
                    held |= kJumpBit;
                    mx = -b.wallDir;
                    ex.lastWall = b.wallDir;
                    ex.lastWallStep = t;
                    ++ex.wallJumps;
                }
            }
        }
    }
    if (mx > 0) held |= 1u << kRightBit;
    if (mx < 0) held |= 1u << kLeftBit;
    if (my > 0) held |= 1u << kDownBit;
    if (my < 0) held |= 1u << kUpBit;
    Pad pad;
    pad.held = held;
    pad.pressed = held & ~ex.prevHeld;
    pad.mx = static_cast<int8_t>(mx * 127);
    pad.my = static_cast<int8_t>(my);
    ex.prevHeld = held;
    return pad;
}

Outcome simulate(const Program& p, const Body& start, uint32_t prevHeld, const BodySim& sim, Arena& arena, const std::function<float(int)>& targetX,
                 const std::function<bool(const Body&, int)>& accept, int maxSteps, std::vector<Vec2>* track, int toleratedHarm) {
    Outcome o;
    Body b = start;
    b.airJumpsUsed = b.dashes = b.impulses = b.wallJumps = 0;
    Exec ex;
    ex.prevHeld = prevHeld;
    if (track) track->clear();
    arena.begin();
    bool left = !start.grounded;
    int bare = 0;
    for (int k = 1; k <= maxSteps; ++k) {
        const int t = k - 1;
        const Pad pad = padAt(p, t, b, targetX(k), ex, sim.kit(), sim.config());
        arena.advance(b, sim, k);
        sim.step(b, arena.physics(), pad);
        if (track) track->push_back(b.feet());
        o.steps = k;
        Harm h = arena.harm(b.box, k);
        // Invulnerable (a dash, the moments after a blow): the blows, shots and beams go through it.
        if (b.invuln > 0.0f && (h == Harm::Blow || h == Harm::Shot || h == Harm::Beam)) h = Harm::None;
        if (h > o.harm) {
            o.harm = h;
            o.harmStep = k;
        }
        if (h == Harm::Void || static_cast<int>(h) > toleratedHarm) break;
        const bool footed = b.grounded || b.state == PlayerState::Climb;
        if (!footed) {
            left = true;
            // Nothing under it within reach (« au-dessus du vide »), looked at every few steps.
            if (!o.overVoid && (k % 6) == 0) {
                const Vec2 f = b.feet();
                if (!arena.physics().raycast(f, {f.x, f.y + 220.0f}, true, ColliderTag::Teammate | ColliderTag::EnemyOnly)) {
                    if (++bare >= 2) o.overVoid = true;
                } else {
                    bare = 0;
                }
            }
        }
        if (left && footed && t >= p.wait + p.run) {
            o.landed = true;
            break;
        }
    }
    o.feet = b.feet();
    o.ground = b.grounded ? b.groundId : 0;
    o.airJumps = b.airJumpsUsed;
    o.dashes = b.dashes;
    o.impulses = b.impulses;
    o.wallJumps = b.wallJumps;
    o.accepted = (o.landed || !left) && static_cast<int>(o.harm) <= toleratedHarm && o.harm != Harm::Void && accept(b, o.steps);
    arena.end();
    return o;
}

void library(std::vector<Program>& out, float dx, float up, const Kit& kit, bool onOneWay, int wallSide) {
    out.clear();
    const int8_t dir = dx > 6.0f ? 1 : dx < -6.0f ? -1 : 0;
    auto add = [&](Program p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
    // A drop (lower, or across at its level): walk off, then the double jump to go further.
    if (up < -6.0f || (std::fabs(up) <= 6.0f && dir != 0)) {
        if (onOneWay && std::fabs(dx) < 40.0f && up < -6.0f) {
            Program p;
            p.dir = dir;
            p.drop = true;
            add(p);
        }
        if (dir != 0) {
            Program p;
            p.dir = dir;
            add(p);
            for (uint8_t a : {14, 28, 44}) {
                Program q = p;
                q.air1 = a;
                q.hold1 = 40;
                add(q);
            }
        } else if (up < -6.0f) {
            // Straight below: off one end or the other of its floor, then back under it.
            for (int8_t d : {int8_t{1}, int8_t{-1}}) {
                Program p;
                p.dir = d;
                add(p);
            }
        }
    }
    // Jumps: full, low, straight up then across.
    for (uint8_t hold : {40, 16, 8}) {
        Program p;
        p.dir = dir;
        p.jump = hold;
        add(p);
    }
    {
        Program p;
        p.dir = dir;
        p.jump = 40;
        p.late = 18;
        add(p);
    }
    // The double jump: at the top, earlier, later.
    for (uint8_t a : {30, 18, 44, 58}) {
        Program p;
        p.dir = dir;
        p.jump = std::min<uint8_t>(40, a);
        p.air1 = a;
        p.hold1 = 40;
        add(p);
    }
    {
        Program p;
        p.dir = dir;
        p.jump = 30;
        p.air1 = 30;
        p.hold1 = 40;
        p.late = 26;
        add(p);
    }
    // The triple jump.
    if (kit.airJumps >= 2)
        for (uint8_t a : {24, 36}) {
            Program p;
            p.dir = dir;
            p.jump = a;
            p.air1 = a;
            p.hold1 = 22;
            p.air2 = static_cast<uint8_t>(a + 24);
            p.hold2 = 40;
            add(p);
        }
    // The dash: in the air (level, after the double jump, upward), on the ground with a jump.
    if (kit.hasDash() && dir != 0) {
        for (uint8_t d : {26, 44}) {
            Program p;
            p.dir = dir;
            p.jump = 40;
            p.dash = d;
            add(p);
        }
        {
            Program p;
            p.dir = dir;
            p.jump = 30;
            p.air1 = 30;
            p.hold1 = 30;
            p.dash = 62;
            add(p);
        }
        {
            Program p;
            p.dir = dir;
            p.jump = 30;
            p.air1 = 30;
            p.hold1 = 30;
            p.dash = 50;
            p.dashY = -1;
            add(p);
        }
        {
            Program p;
            p.dir = dir;
            p.groundDash = 1;
            p.run = 4;
            p.jump = 40;
            p.air1 = 40;
            p.hold1 = 40;
            add(p);
        }
    }
    // The impulse (once per jump): high, then the double jump.
    if (kit.hasImpulse()) {
        for (uint8_t i : {22, 40}) {
            Program p;
            p.dir = dir;
            p.jump = 40;
            p.impulse = i;
            add(p);
        }
        {
            Program p;
            p.dir = dir;
            p.jump = 30;
            p.impulse = 24;
            p.air1 = 60;
            p.hold1 = 40;
            add(p);
        }
    }
    // Wall jumps: off the wall on its side, then from wall to wall (a chimney), the double jump after.
    if (wallSide != 0)
        for (uint8_t n : {1, 2, 3, 4, 5, 6}) {
            Program p;
            p.dir = static_cast<int8_t>(wallSide);
            p.jump = 40;
            p.wall = n;
            add(p);
            Program q = p;
            q.air1 = 14;
            q.hold1 = 40;
            add(q);
        }
}

}  // namespace cosmic::raid::nav
