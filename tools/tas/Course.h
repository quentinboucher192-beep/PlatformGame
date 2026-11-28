#pragma once

// What a run of a category has to do in a level, and how far a state of the world is on the way
// (docs/TAS.md « La recherche »): the same for every level — nothing here knows a level.
//
// * The waypoints: the checkpoints of the level in the order of its route (LevelAnalyzer), then
//   the exit; for 100 %, every shard and relic too, each in the section of the route it is
//   nearest to, the order within a section searched (nearest neighbour, then 2-opt on the
//   distances of the level's own geometry).
// * The potential of a state: the distance along the waypoints already passed, minus the
//   distance (LevelNavigator, the level's geometry) from Nova's feet to the next one.
// * What the world did on the way, read generically: doors and levers opened, receptors lit,
//   arenas cleared, enemies beaten, power gates broken, powers carried, bosses wounded.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {
class ContentDatabase;
class GameWorld;
class LevelEntity;
struct LevelData;
}  // namespace cosmic

namespace cosmic::tas {

struct Waypoint {
    enum class Kind { Checkpoint, Collectible, Exit };
    Kind kind = Kind::Checkpoint;
    std::string objectId;
    Vec2 feet;
    LevelNavigator::Field field;
    float fromPrev = 0.0f;    // geodesic distance from the previous waypoint (the start for the first)
    float cumulative = 0.0f;  // sum of fromPrev up to this one
};

/// How far a state is (computed after every move of the search).
struct Progress {
    int waypoint = 0;         // the next waypoint to reach
    float potential = 0.0f;   // px along the waypoints
    int milestones = 0;       // passages opened, arenas cleared, enemies beaten, gates broken...
    int powers = 0;
    uint64_t powerSet = 0;    // which powers, in which slots (the answers to an offer differ)
    float wounds = 0.0f;      // the health taken from the enemies of the arenas fighting (not the bosses)
    float bosses = 0.0f;      // bosses' phases and wounds
    int kills = 0;
    int health = 0;
    int deaths = 0;
    int damage = 0;
    int collectibles = 0;
    bool finished = false;    // the exit reached (the clock stopped)
    bool rejected = false;    // the category is broken (a death, a blow)
    bool offer = false;       // a power offer waits for its answer
    bool fighting = false;    // in an arena being fought (its doors closed until it is cleared)
    int enemiesLeft = 0;      // the enemies still to beat in the arenas being fought
    float clock = 0.0f;       // the level's clock (s)
    Vec2 feet;                // where Nova is (the log)
    int state = 0;            // PlayerState (the log)
    float score = 0.0f;
    uint64_t cell = 0;        // where it is, for the diversity of the beam
};

class Course {
public:
    Course(const LevelData& level, const ContentDatabase& content, RunCategory category);

    RunCategory category() const { return category_; }
    const std::vector<Waypoint>& waypoints() const { return waypoints_; }
    /// Index of the waypoint that ends the section `s` (its checkpoint, the exit for the last).
    const std::vector<int>& sectionEnds() const { return sectionEnds_; }
    int totalCollectibles() const { return totalCollectibles_; }
    float length() const { return waypoints_.empty() ? 0.0f : waypoints_.back().cumulative; }
    /// The distance of the level's geometry from `feet` to the waypoint `wp` (checks, the log).
    float distanceTo(int wp, Vec2 feet) const { return nav_->distance(waypoints_[static_cast<size_t>(wp)].field, feet); }

    /// The objects of one world the progress reads (resolved once per world: entities keep their
    /// address in a world, and so in all its savestates).
    struct Bound {
        std::vector<const LevelEntity*> waypointEntity;  // per waypoint (nullptr for the exit)
        std::vector<LevelEntity*> kept;                   // doors, levers, receptors, triggers, arenas, enemy spawns
    };
    Bound bind(GameWorld& w) const;
    /// The progress of `w` now, from the waypoint reached before (`fromWaypoint`).
    Progress measure(GameWorld& w, const Bound& b, int fromWaypoint) const;

private:
    RunCategory category_;
    std::unique_ptr<LevelNavigator> nav_;
    std::vector<Waypoint> waypoints_;
    std::vector<int> sectionEnds_;
    int totalCollectibles_ = 0;
};

}  // namespace cosmic::tas
