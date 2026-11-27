#pragma once

// The planner of the machine's runs (docs/TAS.md « Le planificateur »): from the real state of a
// run, the fastest inputs it can find to the next waypoint, planned in a light copy of Nova's
// movement (game/raid/nav/BodySim: the jump and its held height, the buffered jump, the double
// jump, the wall jump, the dash and the long jump, the impulse, the slide, the drop through a
// one-way platform; its energy, regeneration and cooldowns) against a copy of the level's physics
// (game/raid/nav/Terrain: the moving platforms where they will be, the crumbling platforms, the
// ladders, water and updrafts). Nothing it plans is trusted: the search plays every plan in the
// real world and keeps it only on its real merit (no enemy, blow or attack is in the copy).
//
// * A time-synchronous beam over maneuvers of a few steps: every state of a generation has
//   played the same number of steps, so the first arrival is the fastest found. States are
//   ranked by an estimate of the time to go (the level's distance to the target, the momentum:
//   where the body will be a moment later, the energy and the jumps kept for later) and kept
//   diverse (one per place, speed and resources).
// * The maneuvers depend on what the body can do NOW: a dash only when its energy, cooldown and
//   air uses allow it (else the beam waits for the regeneration or does something else), the
//   long jump only on the ground, the jump pressed before a landing (buffered) or held for its
//   height, the next dash on its cooldown.
// * lowerBoundSeconds: a time the body can never beat to touch a box, from per-axis speed caps
//   of its kit and of the level (every power it may still be given counted as held), energy-aware
//   (the dash counts only from when the energy allows it). Admissible by construction (straight
//   lines, the fastest speeds), so loose; for branch-and-bound pruning and the planner's tests.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/nav/BodySim.h"

namespace cosmic {
class ContentDatabase;
class GameWorld;
class PhysicsWorld;
struct LevelData;
}  // namespace cosmic

namespace cosmic::raid::nav {
class Terrain;
}

namespace cosmic::tas {

class Course;

/// A point of a human's path (a ghost's trajectory, SearchOptions::routeHints): where Nova's feet
/// were, at what clock (s). Optional, and only a starting point (a human path is not the limit):
/// the planner always plans toward the waypoint, and also toward the point of that path about a
/// second ahead of where Nova is (an intermediate target) — less and less often once Nova is
/// there as early as the human or earlier (every 2nd, 4th, ... 16th plan).
struct RouteHint {
    Vec2 feet;
    float clock = 0.0f;
};

struct PlanBudget {
    int beam = 48;      // states kept per generation
    int horizon = 1440; // steps planned at most (12 s)
    int segment = 8;    // steps per maneuver
    int proposals = 3;  // plans returned
};

/// A plan: frames from the state it was planned from (pressed / released from the keys held
/// before), and the feet BodySim predicts after each of them.
struct Plan {
    std::vector<InputFrame> frames;
    std::vector<Vec2> feet;
    bool arrives = false;  // touches the goal in the copy (else: the best partial plan)
    int dashes = 0, impulses = 0, airJumps = 0, wallJumps = 0;
};

/// What the planner did (a worker's counts; summed in SearchOutcome).
struct PlanStats {
    int64_t calls = 0;        // plans asked
    int64_t proposals = 0;    // plans given to the search
    int64_t arriving = 0;     // ... that touch their goal in the copy
    int64_t hinted = 0;       // plans toward a route hint
    int64_t simSteps = 0;     // BodySim steps
    double ms = 0.0;          // time spent planning
    // Filled by the search (the real game):
    int64_t kept = 0;         // plans with a state the beam kept on its merit
    int64_t arrivals = 0;     // plans that reached the section's end in the real game
    int64_t wins = 0;         // sections whose winner was a plan's arrival
    // The prediction error (px, |real feet - predicted feet|) by steps played: <= 32, <= 128, more.
    double err[3] = {0.0, 0.0, 0.0};
    int64_t errN[3] = {0, 0, 0};
    void add(const PlanStats& o);
    void noteError(size_t steps, float px);
};

/// What the beam aims at: the box the body's box must touch, and a distance (px) to rank by (empty:
/// the straight line to the box).
struct PlanGoal {
    Rect box;
    std::function<float(Vec2 feet)> distance;
};

/// The search itself, on any physics (the unit tests call it on a world of their own). `terrain`
/// (may be null): the moving platforms, crumbling platforms and surroundings of the level's copy,
/// already synced and `plan`ned; `ph` is then its physics.
std::vector<Plan> planOn(const raid::nav::BodySim& sim, const raid::nav::Body& start, uint32_t prevHeld, const PhysicsWorld& ph, raid::nav::Terrain* terrain,
                         float killY, const PlanGoal& goal, const PlanBudget& budget, PlanStats* stats = nullptr);

/// Speed caps beyond the body's kit (the level's bounce pads, moving platforms, powers it may still
/// be given): what lowerBoundSeconds may never be faster than.
struct SpeedCaps {
    float up = 0.0f;        // the strongest launch up (bounce pads)
    float carry = 0.0f;     // the fastest moving platform
    bool dash = false, impulse = false, teleport = false, grapple = false, groundPound = false, recall = false;
    float dashSpeed = 0.0f, impulseVelocity = 0.0f, teleportDistance = 0.0f, teleportCooldown = 1e9f, grappleSpeed = 0.0f;
    static SpeedCaps of(const LevelData& level);  // its bounce pads and moving platforms
};

/// The admissible lower bound (s) of the time for `b` to touch `goal` (see above).
double lowerBoundSeconds(const raid::nav::Body& b, const raid::nav::Kit& kit, const PlayerConfig& cfg, const SpeedCaps& level, const Rect& goal);

class Planner {
public:
    /// `content`: the parameters of the powers the level gives (the lower bound; without it, the shipped ones).
    explicit Planner(const Course& course, const ContentDatabase* content = nullptr);
    ~Planner();
    Planner(const Planner&) = delete;
    Planner& operator=(const Planner&) = delete;

    /// Plans from `w` now (`prevHeld`: the keys held at the step before) to the waypoint
    /// `targetWaypoint`: the fastest plans found, or the furthest partial ones; none during a
    /// cinematic, a power offer, or when Nova is not in control.
    std::vector<Plan> propose(const GameWorld& w, int targetWaypoint, uint32_t prevHeld, const PlanBudget& budget);
    /// Never more than the real time Nova still needs to touch the waypoint (s).
    double lowerBoundSeconds(const GameWorld& w, int targetWaypoint);
    /// ... to touch the exit (straight to it: a run may leave waypoints out).
    double lowerBoundToFinish(const GameWorld& w);

    /// The human path to follow when there is one (empty: none).
    void setRouteHints(std::vector<RouteHint> hints) {
        hints_ = std::move(hints);
        matched_ = 0;
    }

    PlanStats stats;

private:
    bool goalBox(const GameWorld& w, int waypoint, Rect& out);
    const Course& course_;
    const ContentDatabase* content_ = nullptr;
    std::unique_ptr<raid::nav::Terrain> terrain_;
    SpeedCaps caps_;
    std::vector<RouteHint> hints_;
    int matched_ = 0;          // plans asked from a state as early as the human's path, or earlier
    int64_t hintCalls_ = 0;
    bool capsReady_ = false;
};

}  // namespace cosmic::tas
