#pragma once

// 2.79 (docs/RAIDS.md, « Joueurs simulés : terrain et anticipation »): how a simulated player moves — like
// a very good human player, with nothing but a player's inputs:
//
// * it follows its path on the zone's graph (raid::NavGraph, its jumps found by simulating the physics),
//   walking where it walks, climbing the ladders;
// * before each move through the air it tries several programs of inputs on a light copy of the zone
//   (game/raid/nav/Terrain: the moving platforms where they WILL be, the crumbling platforms, the shots,
//   the beams, the blows announced, the rising liquid) with the player's own physics (BodySim), a few
//   dozen steps ahead, and keeps the safest and quickest: the move stored on the graph's edge first, then
//   the others (a jump held longer or shorter, the double or triple jump earlier or later, a dash, the
//   impulse, a wall jump), waiting for a moving platform until it will be where it lands;
// * it plays the program chosen and checks each step that the game follows the simulation; when it
//   does not (a blow, a teammate, an enemy), it plans again — in the air, it catches itself with what is
//   left (an air jump, a dash, the impulse) on the nearest safe floor;
// * a few steps ahead it watches what will hurt it on its way: a shot, a beam, a blow announced, the
//   rising liquid, the void — and dodges (stops, crouches, jumps, dashes through: invulnerable) once it
//   has seen it (its level's reaction time);
// * its level (Débutant, Confirmé, Expert) is its reaction time, the depth of its lookahead and the
//   precision of its timing — never anything more than a player can do.
//
// The cost is bounded: each group has a budget of simulated steps per game step (the lookahead of the
// players waits its turn beyond it: a step later), and nothing runs on another thread but the graph's
// build. It never writes the game's world (only its own copy, restored after each try).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/raid/RaidNav.h"
#include "game/raid/nav/Intent.h"
#include "game/raid/nav/Maneuver.h"
#include "game/raid/nav/Terrain.h"

namespace cosmic {
class GameWorld;
}

namespace cosmic::raid {
class RaidRun;
}

namespace cosmic::raid::nav {

/// The level of a simulated player, for its moves: reaction, depth of lookahead, precision.
struct Skill {
    int reaction = 6;       // steps between seeing a danger and acting
    int budget = 6000;      // simulated steps for one choice of a move
    int horizon = 240;      // steps a move is simulated at most
    int look = 48;          // steps it looks ahead on its way for a danger
    int jitter = 0;         // steps its timing may be off by (imprecision)
    bool powers = true;     // tries the dash, the impulse and the wall jump for its moves (not only in a fall)
    static Skill of(int level);  // 0 Débutant, 1 Confirmé, 2 Expert
};

/// What the caller gives for a seat's step: its goal and its path (kept by the caller, RaidBots::Bot).
struct SeatPlan {
    Vec2 goal;
    int goalNode = -1;
    float stop = 6.0f;
    bool flee = false;  // up above the rising liquid
    bool hold = false;  // holds a mechanism or revives: it dodges only what would kill it
    bool mayLeave = false;  // its goal is a passage it takes (the group leaves the zone): else never in one
    std::vector<int>* path = nullptr;
    size_t* at = nullptr;
    int* planned = nullptr;
    float* best = nullptr;
    int* noProgress = nullptr;
};

/// The measured cost (tests/test_raid_nav.cpp, docs/RAIDS.md).
struct Cost {
    int64_t searches = 0, simulations = 0, bodySteps = 0;
    int64_t maxStepBodySteps = 0;   // the most simulated in one game step (one group)
    double maxStepMs = 0.0, totalMs = 0.0;
    int64_t steps = 0;              // game steps driven
    int64_t deferred = 0;           // searches put off to a later step (the budget of the step spent)
    int64_t graphWaits = 0;         // graphs not finished at their step (the main thread waited)
    double graphWaitMs = 0.0;
};

class Navigator;

/// One seat's pilot.
class Pilot {
public:
    void reset();
    const Thought& thought() const { return thought_; }
    /// A move through the air under way: its direction must not be changed by anything else.
    bool committed() const { return active_ && airborne_; }
    bool active() const { return active_; }
    /// The program under way (tests), and how many air jumps / dashes / impulses / wall jumps it played.
    const Program& program() const { return prog_; }
    /// The feet the simulation expects after each step of the move under way, the steps played of it,
    /// the moving platform it aims at (its collider, 0 none).
    const std::vector<Vec2>& track() const { return track_; }
    int played() const { return t_; }
    uint32_t targetMover() const { return targetMover_; }
    struct Used {
        int jumps = 0, airJumps = 0, dashes = 0, impulses = 0, wallJumps = 0, recoveries = 0, dodges = 0, moverJumps = 0, searches = 0, failed = 0;
    };
    const Used& used() const { return used_; }

private:
    friend class Navigator;
    void think(Intent i, uint32_t tick);
    bool active_ = false;
    bool airborne_ = false;
    bool dodge_ = false;
    Program prog_;
    Exec ex_;
    int t_ = 0;
    int target_ = -1;          // the node it goes to
    int targetSeg_ = -1;
    uint32_t targetMover_ = 0;
    int pathIndex_ = -1;       // the index of target_ in the path (-1: not in it)
    std::vector<Vec2> track_;  // the feet the simulation expects after each step
    Thought thought_;
    Intent moveIntent_ = Intent::None;
    int threat_ = 0;           // steps a danger ahead has been seen
    int shelter_ = -1;         // where it waits above the rising liquid
    int blocked_ = 0;          // steps pushing against something without moving
    float lastX_ = 0.0f;
    int cooldown_ = 0;         // steps before another search after a failure
    struct Failed {
        int a, b;
        uint32_t until;
    };
    std::vector<Failed> failed_;
    Used used_;
};

/// A group's navigation: its graph (asked for in the background, used from a fixed step), its copy of
/// the zone, a pilot per seat, the budget of the step.
class Navigator {
public:
    void reset();
    void setSkill(int level) { skill_ = Skill::of(level); level_ = level; }
    /// Each step, before the seats: follows the zone (a new world, a door opened: a new graph asked for).
    /// Its paths and moves keep out of the zone's passages (walking into one takes the whole group away)
    /// unless its goal is in one: a floor cut by a passage is crossed by jumping over it.
    void update(const RaidRun& run, uint32_t tick, const std::vector<Rect>& blows);
    /// The same for any world (tests, a level outside a raid): `zoneSerial` changes with the world,
    /// `keepOut` the places never to touch on the way, `run` the raid's rules when there are some.
    void update(const GameWorld& w, int zoneSerial, uint32_t tick, const std::vector<Rect>& blows, const std::vector<Rect>& keepOut,
                const RaidRun* run = nullptr);
    /// The graph in use (null while the zone's is not due yet).
    const NavGraph* graph() const { return graph_.get(); }
    std::shared_ptr<const NavGraph> graphShared() const { return graph_; }
    /// True once per new graph (the caller forgets its paths and goal nodes).
    bool graphChanged() const { return changed_; }
    /// The movement part of a seat's frame this step (held bits, mx, my).
    Pad steer(const GameWorld& w, int seat, SeatPlan& plan, uint32_t prevHeld);
    Pilot& pilot(int seat) { return pilots_[static_cast<size_t>(seat)]; }
    const Pilot& pilot(int seat) const { return pilots_[static_cast<size_t>(seat)]; }
    const Cost& cost() const { return cost_; }
    Terrain& terrain() { return terrain_; }
    uint64_t hash() const;

private:
    bool search(const GameWorld& w, int seat, Pilot& p, const Body& body, const BodySim& sim, int src, int dst, uint32_t prevHeld, bool recover);
    bool dodge(const GameWorld& w, int seat, Pilot& p, const Body& body, const BodySim& sim, uint32_t prevHeld, Harm seen, float towardX);
    void start(Pilot& p, const Program& prog, const std::vector<Vec2>& track, Intent intent);
    int spend(int steps);  // counts simulated steps; the budget left this step
    Skill skill_ = Skill::of(1);
    int level_ = 1;
    std::shared_ptr<const NavGraph> graph_;
    NavGraph::Ticket pending_;
    uint32_t due_ = 0;          // the step the pending graph is used
    uint64_t pendingKey_ = 0;
    uint64_t graphKey_ = 0;     // the signature of the graph in use
    int zoneSerial_ = -1;
    bool changed_ = false;
    uint32_t tick_ = 0;
    std::string periodic_;
    bool magmaStyle_ = false;
    Terrain terrain_;
    bool planned_ = false;      // the terrain's lookahead prepared this step
    std::vector<Pilot> pilots_ = std::vector<Pilot>(8);
    int stepSteps_ = 0;         // simulated steps this game step
    Cost cost_;
    std::vector<Rect> keepOut_;  // the zone's passages (walking into one takes the group away)
    uint8_t keepOutAt(int node) const;
};

}  // namespace cosmic::raid::nav
