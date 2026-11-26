#pragma once

// 2.79 (docs/RAIDS.md, « Joueurs simulés : terrain et anticipation »): what a simulated player knows of
// its zone to play a few steps ahead — a light copy of the zone's state, never the game's world:
//
// * a private copy of the zone's collision world (PhysicsWorld), followed step by step (doors, crumbling
//   platforms, the walls of a boss); during a lookahead the moving platforms are put in it where they
//   WILL be (MovingPlatformEntity::predict: their very arithmetic), the body they carry is carried, the
//   crumbling platforms give way under it and come back;
// * the body's surroundings at each step (a ladder, water, an updraft), as the world sets them;
// * what hurts, with its time: the void (the kill line), deadly liquids, spikes, damage zones, the zone's
//   periodic liquid (RaidRun's schedule: when it rises, how high), lasers (their cycle), the blows
//   announced (fists, jets, a gigaboss's marks, the bosses' hazards: given by the caller) and the
//   enemies' shots (their path, gravity included).
//
// It reads the world and writes only its own copy, restored after each candidate of a lookahead.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/physics/PhysicsWorld.h"
#include "game/raid/nav/BodySim.h"

namespace cosmic {
class GameWorld;
class MovingPlatformEntity;
class LaserEntity;
class DamageZoneEntity;
class CrumblePlatformEntity;
class UpdraftEntity;
}  // namespace cosmic

namespace cosmic::raid {
class RaidRun;
}

namespace cosmic::raid::nav {

/// What a body runs into at a step of a lookahead (the most serious last).
enum class Harm : uint8_t { None = 0, Blow, Shot, Beam, Hazard, Liquid, Void };

/// 2.90 (docs/RAIDS.md §36): a blow announced, with its time — the steps of the lookahead it hurts in (`from` to `to`, 0:
/// now): where a body may be before it lands or once it is over. Without its time (a blow read as a box only), always.
struct Blow {
    Rect r;
    int from = 0;
    int to = 1 << 30;
    bool dodgeOnly = false;  // seen only by a dodge's choice (the boss's body: a dodge never ends in it; the moves pass near it)
    bool body = false;       // the boss's body (a dodge out of it has the time to get out: Terrain::dodgeView's grace)
};

class Terrain {
public:
    /// Follows the world now (each step, cheap: the doors, the platforms; a full copy of its physics when
    /// its colliders changed). `run`: the raid's rules (the zone's periodic liquid), may be null.
    void sync(const GameWorld& w, const RaidRun* run);
    bool ready() const { return ready_; }
    /// The copy of the zone's physics (the moving platforms where the lookahead put them).
    const PhysicsWorld& physics() const { return phys_; }
    float killY() const { return killY_; }
    const Rect& bounds() const { return bounds_; }
    /// The blows announced this step (a gigaboss's fists, jets and marks, the bosses' hazards).
    void setBlows(const std::vector<Rect>& blows) {
        blows_.clear();
        for (const Rect& r : blows) blows_.push_back({r});
    }
    /// 2.90: the same with their time (Blow).
    void setBlows(const std::vector<Blow>& blows) { blows_ = blows; }
    /// 2.90: the blows only a dodge's choice sees (Blow::dodgeOnly) are seen from now on (until set off again).
    void dodgeView(bool on, int grace = 0) {
        dodgeView_ = on;
        dodgeGrace_ = grace;
    }
    /// 2.90: the last step of the blows on `box` at step k (the one a dodge leaves: until when it must stay out of it), -1 none.
    int blowEnd(const Rect& box, int k) const;
    /// 2.90: whether `box` touches the boss's body (Blow::body), now.
    bool inBody(const Rect& box) const;
    /// 2.90: a blow that still hurts at step `k` or later and touches `box` (a place too close to one to end a dodge at).
    bool blowNear(const Rect& box, int k) const;
    /// Places a body must not touch (the zone's passages: another zone) — as the void for the lookahead —,
    /// but the one allowed (1-based: where it goes, or where it stands; 0 none).
    void setKeepOut(const std::vector<Rect>& rects) { keepOut_ = rects; }
    void allowOut(int index) { allowOut_ = index; }

    // ---------------------------------------------------------------- the lookahead
    /// Prepares `horizon` steps ahead around `focus` (± 720 px: where each moving platform will be, the
    /// shots' paths; the periodic liquid); `rider`: the collider the simulated body stands on now (a
    /// platform waiting for a rider leaves). Call before the candidates of a lookahead; what was already
    /// predicted this step is kept.
    void plan(int horizon, uint32_t rider, Vec2 focus);
    int horizon() const { return horizon_; }
    /// One candidate's run in the copy.
    struct Run {
        int k = 0;  // the step reached (0: now)
        struct Crumble {
            int phase = 0;
            float timer = 0.0f;
        };
        std::vector<Crumble> crumbles;
    };
    void begin(Run& r);
    /// Step r.k + 1: the platforms where they will be, the body carried by its own, the crumbling platforms
    /// under it, its surroundings (ladder, water, updraft), as GameWorld does before the player's step.
    void advance(Run& r, Body& b, const BodySim& sim);
    /// The copy as the world is now (after each candidate).
    void end(Run& r);
    /// What hurts a body's box at step k of the lookahead (0: now).
    Harm harm(const Rect& box, int k) const;
    /// Where the moving platform of collider `id` will be at step k (its box), and whether it is one.
    bool moverBox(uint32_t id, int k, Rect& out) const;
    /// The first step the zone's periodic liquid is up within the horizon (-1: none).
    int liquidFrom() const { return liquidFrom_; }
    /// The periodic liquid's hurting box at step k (empty: down).
    Rect liquidAt(int k) const;
    /// The periodic liquid is a rising magma (the thought « Remonte au-dessus du magma »).
    bool liquidIsMagma() const;
    /// Steps before the periodic liquid is up (0: up now, -1: none, or not scheduled), from its schedule.
    int liquidIn() const;
    /// The box the periodic liquid fills at its highest (empty: none).
    Rect liquidBox() const;
    /// The deadly liquids, the spikes and the damage zones that last (the graph keeps out of them).
    const std::vector<Rect>& lasting() const { return lasting_; }

    struct Mover {
        uint32_t collider = 0;
        const MovingPlatformEntity* e = nullptr;
        bool oneway = false;
        Rect box;                 // its box now
        Rect span;                // the whole of its route
        std::vector<Vec2> route;  // its top-left at steps 1..horizon (predicted this step when `near`)
        bool near = false;        // around the lookahead's focus: moved in the copy
        uint32_t step = 0;        // the sync its route was predicted at
        bool rider = false;
    };
    const std::vector<Mover>& movers() const { return movers_; }

private:
    struct Crumble {
        const CrumblePlatformEntity* e = nullptr;
        uint32_t collider = 0;
        Rect box;
        int phase = 0;
        float timer = 0.0f, delay = 0.45f, respawn = 3.0f;
        bool onEvent = false;
    };
    struct Laser {
        const LaserEntity* e = nullptr;
        Vec2 from, to;
    };
    struct Shot {
        std::vector<Vec2> at;  // its centre at steps 1..
        float radius = 4.0f;
    };
    struct Env {
        Rect box;
        int kind = 0;  // 0 ladder, 1 water, 2 updraft
        float strength = 0.0f;
        const UpdraftEntity* updraft = nullptr;
        bool on = true;
    };
    void scan(const GameWorld& w);
    Rect moverCollider(const Mover& m, Vec2 topLeft) const;
    Vec2 moverAt(const Mover& m, int k) const;

    PhysicsWorld phys_;
    bool ready_ = false;
    std::string level_;
    uint64_t entitySig_ = 0;
    const DamageZoneEntity* liquidSeen_ = nullptr;
    float killY_ = 1e9f;
    Rect bounds_;
    std::vector<Mover> movers_;
    std::vector<Crumble> crumbles_;
    std::vector<Laser> lasers_;
    std::vector<Env> env_;
    std::vector<Rect> lasting_;   // deadly liquids, spikes, damage zones that are not the periodic one
    std::vector<uint8_t> deadly_;  // per lasting_: a deadly liquid
    std::vector<Blow> blows_;
    bool dodgeView_ = false;
    int dodgeGrace_ = 0;  // 2.90: the boss's body seen only from this step of a dodge's lookahead (the time to get out of it)
    std::vector<Rect> keepOut_;
    int allowOut_ = 0;
    std::vector<Shot> shots_;
    bool shotsDone_ = false;
    // The zone's periodic liquid.
    const DamageZoneEntity* liquid_ = nullptr;
    std::vector<Rect> liquidAt_;  // its hurting box at steps 0..horizon
    int liquidFrom_ = -1;
    int liquidHorizon_ = -1;
    // Its schedule (RaidRun::updateMagma): whole steps.
    bool scheduled_ = false;
    uint32_t zoneTicks_ = 0, first_ = 0, period_ = 1, duration_ = 0;
    int horizon_ = 0;
    uint32_t step_ = 0;  // counts the syncs (a prediction is valid for its own)
    const GameWorld* world_ = nullptr;
};

}  // namespace cosmic::raid::nav
