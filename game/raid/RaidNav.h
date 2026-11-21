#pragma once

// 2.77: where a simulated player of a raid test can stand in a zone and how it goes from one place to
// another (docs/RAIDS.md §22): a graph built from the level's own collision world, never from the screen.
// 2.79 (« Joueurs simulés : terrain et anticipation »): its jumps are no longer guessed — every edge that
// leaves the ground is found by SIMULATING the player's own physics (game/raid/nav/BodySim) on a copy of
// the zone, and keeps the move that made it (raid::nav::Program).
//
// * nodes: the places a player (10 × 22 px) can stand: along the top of every solid and one-way
//   platform (one every 16 px, with room above, out of the lasting dangers: spikes, deadly liquids, a
//   damage zone that is not the zone's periodic liquid), up the ladders, and along the route of each
//   moving platform (every 32 px); the places joined by walking form a segment (one floor);
// * edges: walking along a surface, climbing a ladder, riding a moving platform; and the moves through
//   the air: from each segment to each other segment within the reach of a player (a drop, a jump, the
//   double and the triple jump, a dash in the air or on the ground, the impulse, a wall jump), the
//   simplest move that lands on it in the simulation, from the two best places to take off. An edge says
//   what it needs (the dash, the impulse, a third jump): a player without it never takes it;
// * built from a snapshot of the zone taken on the main thread (its physics copied, its dangers, its
//   ladders, the routes of its platforms), in the background (raid::NavGraph::request): the graph is a
//   pure function of that snapshot, used from a step fixed by the size of the zone (the same on every
//   computer and in every run, cached or not), waiting for it only then if it is late;
// * A* with fixed tie-breaks (the lowest node first): the same path on every run.
//
// Nothing here reads the clock or the screen.

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/raid/nav/Maneuver.h"

namespace cosmic {
class GameWorld;
}

namespace cosmic::raid {

class NavGraph {
public:
    enum class Move : uint8_t { Walk, Jump, Drop, Climb, Ride };
    struct Node {
        Vec2 feet;
        int mover = -1;      // a moving platform (index in movers()), -1: fixed ground
        bool ladder = false;
        uint32_t surface = 0;  // what it stands on (a collider id, a ladder or a platform's route)
        uint8_t out = 0;       // in a place to keep out of (a passage: walking into it changes zone), 1-based
    };
    struct Edge {
        int to = 0;
        float cost = 0.0f;
        Move move = Move::Walk;
        uint8_t needs = 0;     // nav::Need: what it needs beyond the double jump
        int32_t program = -1;  // the move found by the simulation (programs()), -1 for a walk, a climb, a ride
    };
    /// A moving platform: its collider (where it is now), the top-left of its box at both ends.
    struct Mover {
        uint32_t collider = 0;
        Vec2 a, b;
        float w = 0.0f, h = 0.0f;
        bool oneway = false;
    };
    /// What a build reads (copied from the world on the main thread; the build may run elsewhere).
    struct Snapshot;
    static std::shared_ptr<Snapshot> snapshot(const GameWorld& w, const std::string& periodic = {}, const std::vector<Rect>& keepOut = {});

    /// A graph asked for: built in the background, used from `delay` steps after the request.
    struct Ticket {
        std::shared_future<std::shared_ptr<const NavGraph>> future;
        uint64_t key = 0;
        int delay = 0;
        bool valid() const { return future.valid(); }
        /// The graph (waits for the build if it is not finished: only at the step it is due).
        std::shared_ptr<const NavGraph> get() const { return future.valid() ? future.get() : nullptr; }
        /// Finished (never used for a decision: only to measure the waits).
        bool finished() const;
    };
    /// The graph of a world's zone now (`periodic`: the id of its periodic liquid, never a lasting
    /// danger; `keepOut`: places a body must never touch on its way, the zone's passages — a floor is cut
    /// there, a move jumps over them), from the cache or built in the background. Cached by level, by the
    /// state of its doors and by those places.
    static Ticket request(const GameWorld& w, const std::string& periodic = {}, const std::vector<Rect>& keepOut = {});
    /// Compat: the graph now (waits for the build).
    static std::shared_ptr<const NavGraph> of(const GameWorld& w, const std::string& periodic = {}, const std::vector<Rect>& keepOut = {});
    /// What the graph depends on (the level, its doors and walls that open or break).
    static uint64_t signature(const GameWorld& w, const std::string& periodic = {}, const std::vector<Rect>& keepOut = {});
    /// 2.84: empties the graphs' cache (tests: a run as if in a fresh process).
    static void clearCache();
    /// The steps between a request and the use of its graph (a function of the zone only).
    static int delayFor(const GameWorld& w);

    void build(const Snapshot& s);
    void build(const GameWorld& w, const std::string& periodic, const std::vector<Rect>& keepOut = {});

    int size() const { return static_cast<int>(nodes_.size()); }
    const Node& node(int i) const { return nodes_[static_cast<size_t>(i)]; }
    const std::vector<Edge>& edges(int i) const { return edges_[static_cast<size_t>(i)]; }
    const std::vector<Mover>& movers() const { return movers_; }
    const std::vector<nav::Program>& programs() const { return programs_; }
    const std::vector<Rect>& keepOut() const { return keepOut_; }
    size_t edgeCount() const;
    /// The move from `a` to `b` (an edge of the graph), Walk when none.
    Move moveBetween(int a, int b) const;
    /// The cheapest edge from `a` to `b` a player meeting `can` (nav::Need bits) may take, null when none.
    const Edge* edgeBetween(int a, int b, uint8_t can = 0xFF) const;
    /// The segment of a node (the places joined by walking or climbing; a moving platform's place: its own).
    int segment(int i) const { return segment_[static_cast<size_t>(i)]; }
    /// The node under feet at `feet` (else the nearest within `reach`, a node below preferred), -1.
    int locate(Vec2 feet, float reach = 56.0f) const;
    /// The nearest node to `p` within `reach` accepted by `ok` (nullptr: any), -1.
    int nearest(Vec2 p, float reach, const std::function<bool(const Node&)>& ok = nullptr) const;
    /// A*: the nodes from `from` to `to` (both included); false when `to` cannot be reached within
    /// `budget` nodes expanded. `can`: the needs the player meets (nav::Need bits); `avoid(a, b)`: an
    /// edge it must not take (a move that failed it).
    bool path(int from, int to, std::vector<int>& out, int budget = 60000, uint8_t can = 0xFF,
              const std::function<bool(int, int)>& avoid = nullptr) const;
    /// The node reachable from `from` nearest to `goal` (within `maxCost` of moves), -1; its path in `out`.
    int closest(int from, Vec2 goal, float maxCost, std::vector<int>* out = nullptr, uint8_t can = 0xFF,
                const std::function<bool(int, int)>& avoid = nullptr) const;
    /// The cheapest node from `from` accepted by `ok`, within `maxCost` (Dijkstra), -1; its path in `out`.
    int search(int from, const std::function<bool(int)>& ok, float maxCost, std::vector<int>* out = nullptr, uint8_t can = 0xFF) const;
    /// Where the moving platform `m` is now (the feet of a player at its centre), from the world.
    Vec2 moverFeet(const GameWorld& w, int m) const;
    uint64_t hash() const;
    double buildMs() const { return buildMs_; }
    /// What the build did: simulations of moves, their steps; the edges found by the simulation by kind.
    struct Stats {
        int simulations = 0;
        int64_t bodySteps = 0;
        int pairs = 0;
        int byStyle[11] = {};
        size_t bytes = 0;
    };
    const Stats& stats() const { return stats_; }

    // Reach (data/player.json: jump 58 px, double jump 44 px, 150 px/s; the impulse 380 px/s).
    static constexpr float kStep = 16.0f;
    static constexpr float kReachUp = 210.0f;     // the highest a move is tried (jumps + impulse)
    static constexpr float kReachAcross = 300.0f;
    static constexpr float kReachDown = 460.0f;

private:
    void addEdge(int a, int b, Move m, float cost, uint8_t needs = 0, int32_t program = -1);
    std::vector<Node> nodes_;
    std::vector<std::vector<Edge>> edges_;
    std::vector<Mover> movers_;
    std::vector<nav::Program> programs_;
    std::vector<int> segment_;
    std::vector<Rect> keepOut_;
    std::vector<std::vector<int>> buckets_;  // nodes by column of 64 px
    float bucketX0_ = 0.0f;
    double buildMs_ = 0.0;
    Stats stats_;
};

}  // namespace cosmic::raid
