#pragma once

// 2.77: where a simulated player of a raid test can stand in a zone and how it goes from one place to
// another (docs/RAIDS.md §21): a graph built from the level's own collision world, never from the screen.
//
// * nodes: the places a player (10 × 22 px) can stand: along the top of every solid and one-way
//   platform (one every 16 px, with room above, out of the lasting dangers: spikes, lava, a damage zone
//   that is not the zone's periodic liquid), up the ladders, and along the route of each moving platform;
// * edges: walking along a surface, jumping (jump then double jump: 88 px up at most, the width a run
//   covers), dropping off a ledge or through a one-way platform, climbing a ladder, riding a moving
//   platform. Each jump is checked against the walls and ceilings on its way (a curve of 12 bodies);
// * built from what does not move: the level's colliders (a closed door blocks, an open one does not;
//   a crumbling platform always counts as a floor), so the same level with the same doors gives the
//   same graph — kept in a small shared cache (several groups in the same zone use one graph);
// * A* with fixed tie-breaks (the lowest node first): the same path on every run.
//
// Nothing here reads the clock or the screen; the graph is a pure function of the world's static state.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"

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
    };
    struct Edge {
        int to = 0;
        float cost = 0.0f;
        Move move = Move::Walk;
    };
    /// A moving platform: its collider (where it is now), the top-left of its box at both ends.
    struct Mover {
        uint32_t collider = 0;
        Vec2 a, b;
        float w = 0.0f, h = 0.0f;
    };

    /// The graph of a world's zone now (`periodic`: the id of its periodic liquid, never a lasting
    /// danger). Cached by level and by the state of its doors: cheap when nothing changed.
    static std::shared_ptr<const NavGraph> of(const GameWorld& w, const std::string& periodic = {});
    /// What the graph depends on (the level, its doors and walls that open or break).
    static uint64_t signature(const GameWorld& w, const std::string& periodic = {});

    void build(const GameWorld& w, const std::string& periodic);

    int size() const { return static_cast<int>(nodes_.size()); }
    const Node& node(int i) const { return nodes_[static_cast<size_t>(i)]; }
    const std::vector<Edge>& edges(int i) const { return edges_[static_cast<size_t>(i)]; }
    const std::vector<Mover>& movers() const { return movers_; }
    size_t edgeCount() const;
    /// The move from `a` to `b` (an edge of the graph), Walk when none.
    Move moveBetween(int a, int b) const;
    /// The node under feet at `feet` (else the nearest within `reach`, a node below preferred), -1.
    int locate(Vec2 feet, float reach = 56.0f) const;
    /// The nearest node to `p` within `reach` accepted by `ok` (nullptr: any), -1.
    int nearest(Vec2 p, float reach, const std::function<bool(const Node&)>& ok = nullptr) const;
    /// A*: the nodes from `from` to `to` (both included); false when `to` cannot be reached within
    /// `budget` nodes expanded.
    bool path(int from, int to, std::vector<int>& out, int budget = 60000) const;
    /// The cheapest node from `from` accepted by `ok`, within `maxCost` (Dijkstra), -1; its path in `out`.
    int search(int from, const std::function<bool(int)>& ok, float maxCost, std::vector<int>* out = nullptr) const;
    /// Where the moving platform `m` is now (the feet of a player at its centre), from the world.
    Vec2 moverFeet(const GameWorld& w, int m) const;
    uint64_t hash() const;
    double buildMs() const { return buildMs_; }

    // Reach of a jump (data/player.json: jump 58 px, double jump 44 px, 150 px/s).
    static constexpr float kMaxUp = 88.0f;
    static constexpr float kStep = 16.0f;

private:
    void addEdge(int a, int b, Move m, float cost);
    std::vector<Node> nodes_;
    std::vector<std::vector<Edge>> edges_;
    std::vector<Mover> movers_;
    std::vector<std::vector<int>> buckets_;  // nodes by column of 64 px
    float bucketX0_ = 0.0f;
    double buildMs_ = 0.0;
};

}  // namespace cosmic::raid
