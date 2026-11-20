#pragma once

// 2.79 (docs/RAIDS.md, « Joueurs simulés : terrain et anticipation »): the moves of a simulated player, as
// small programs of inputs (run, jump and how long it is held, double and triple jump, dash on the ground
// or in the air, impulse, wall jump, a drop through a one-way platform, waiting for a moving platform),
// with a closed loop for the landing (it steers toward where it wants to land, braking in time).
//
// A program gives the frame of each step from the body as it is (padAt): the same function in the
// lookahead (BodySim, on a copy of the zone) and in the game, so the inputs played are those that were
// simulated. Nothing but the inputs of a player: never a position, a speed or a rule of its own.

#include <cstdint>
#include <functional>
#include <vector>

#include "game/raid/nav/BodySim.h"
#include "game/raid/nav/Terrain.h"

namespace cosmic::raid::nav {

/// What a move needs beyond running, jumping and the double jump (an edge of the graph keeps it: a
/// player without it never takes that edge).
enum Need : uint8_t { NeedNone = 0, NeedDash = 1, NeedImpulse = 2, NeedExtraJump = 4 };

/// The kind of move (the thought shown, the graph's statistics).
enum class Style : uint8_t { Walk, Drop, DropThrough, Jump, DoubleJump, TripleJump, Dash, LongJump, Impulse, WallJump, Stand };

struct Program {
    int8_t dir = 0;         // horizontal input of the move (-1, 0, +1)
    uint8_t wait = 0;       // steps standing still first (a moving platform, a beam)
    uint8_t run = 0;        // then steps running `dir` before the jump
    uint8_t stop = 0;       // on its feet, the step it lets go of `dir` (0: never): a dodge that stops
    uint8_t jump = 0;       // steps the jump is held from its press (0: no jump — a walk off, a drop)
    uint8_t late = 0;       // steps in the air before pressing `dir` (straight up first)
    uint8_t release = 0;    // steps in the air when `dir` is let go (0: never)
    uint8_t air1 = 0, hold1 = 0;  // the double jump: steps in the air before it (0: none), held
    uint8_t air2 = 0, hold2 = 0;  // the triple jump (steps in the air)
    uint8_t dash = 0;       // a dash in the air (steps in the air, 0: none)
    int8_t dashY = 0;       // its aim: -1 up (with `dir`: the diagonal), 0 level
    uint8_t groundDash = 0; // a dash on the ground at this step of the run (1-based; with a jump: the long jump)
    uint8_t impulse = 0;    // the impulse (steps in the air, 0: none)
    bool steer = true;      // after `late`, toward the target's x (braking to land on it)
    bool drop = false;      // Down + Jump first: through the one-way platform underneath
    uint8_t wall = 0;       // wall jumps, one each time it slides down a wall, from wall to wall (a chimney);
                            // the air jumps then count from the last one
    bool crouch = false;    // crouched (under a blow), no move

    uint8_t needs() const;
    Style style() const;
    bool operator==(const Program&) const = default;
};

/// The state a program keeps while it runs (the same in the lookahead and in the game).
struct Exec {
    int takeoff = -1;       // the step it left the ground (-1: not yet)
    int wallJumps = 0;
    int lastWall = 0;       // the side of the last wall it jumped off
    int lastWallStep = 0;   // and when
    uint32_t prevHeld = 0;  // the frame before (a press is a new hold)
};

/// The frame of step `t` of `p` for the body `b` (targetX: where it wants to land at that step).
Pad padAt(const Program& p, int t, const Body& b, float targetX, Exec& ex, const Kit& kit, const PlayerConfig& cfg);
/// The horizontal input that brings the feet to `x` and stops there (air or ground: its acceleration).
int steerTo(float x, const Body& b, const PlayerConfig& cfg);

/// Where a program is simulated: the copy of the zone's physics, what moves in it, what hurts.
class Arena {
public:
    virtual ~Arena() = default;
    virtual const PhysicsWorld& physics() const = 0;
    virtual void begin() = 0;
    /// The world before the body's step k (1-based): platforms, crumbling platforms, carry, surroundings.
    virtual void advance(Body& b, const BodySim& sim, int k) = 0;
    virtual void end() = 0;
    virtual Harm harm(const Rect& box, int k) const = 0;
};

/// The lookahead's arena: the Terrain (its copy of the zone, followed step by step).
class TerrainArena : public Arena {
public:
    explicit TerrainArena(Terrain& t) : t_(t) {}
    const PhysicsWorld& physics() const override { return t_.physics(); }
    void begin() override { t_.begin(run_); }
    void advance(Body& b, const BodySim& sim, int /*k*/) override { t_.advance(run_, b, sim); }
    void end() override { t_.end(run_); }
    Harm harm(const Rect& box, int k) const override { return t_.harm(box, k); }

private:
    Terrain& t_;
    Terrain::Run run_;
};

/// What a simulated program did.
struct Outcome {
    bool landed = false;    // on its feet again after leaving the ground (or never left it: a walk)
    bool accepted = false;  // where it was meant to land
    int steps = 0;          // steps simulated (its landing step when landed)
    Harm harm = Harm::None; // the worst it ran into
    int harmStep = -1;
    Vec2 feet;              // where it ended
    uint32_t ground = 0;    // the collider under it then
    int airJumps = 0, dashes = 0, impulses = 0, wallJumps = 0;
    bool overVoid = false;  // nothing under it for a while on the way (the thought: « au-dessus du vide »)
};

/// Simulates `p` from `start` (`prevHeld`: the frame before) for at most `maxSteps`, in `arena`.
/// `targetX(k)`: where it steers at step k; `accept(body, k)`: it landed where it wanted. `track`: the
/// feet at each step (optional).
Outcome simulate(const Program& p, const Body& start, uint32_t prevHeld, const BodySim& sim, Arena& arena, const std::function<float(int)>& targetX,
                 const std::function<bool(const Body&, int)>& accept, int maxSteps, std::vector<Vec2>* track = nullptr, int toleratedHarm = 0);

/// The moves worth trying to go `dx` across and `up` up (negative: down) with this kit, the simplest first.
/// `onOneWay`: it stands on a one-way platform; `wallSide`: a wall close on this side (-1, 0, +1).
void library(std::vector<Program>& out, float dx, float up, const Kit& kit, bool onOneWay, int wallSide);

}  // namespace cosmic::raid::nav
