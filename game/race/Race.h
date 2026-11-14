#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class ContentDatabase;
class GameWorld;
struct LevelData;

/// The « course à la vérification » of an EXTRÊME level (one life): every attempt is
/// ranked by how far it went, even without reaching the end. The progress score (0..100)
/// counts the route (40 points) and the bosses and ambushes (60 points: 2 shares per
/// boss, 1 per ambush, by the part of the boss's health removed and of the waves beaten).
/// The server computes the same score from the replay (server/src/race.ts).
struct RaceLayout {
    struct Boss {
        std::string id, name;
    };
    struct Ambush {
        std::string id;
        int waves = 0;
    };
    std::vector<Vec2> route;  // the intended route (start -> checkpoints -> exit)
    std::vector<Boss> bosses;
    std::vector<Ambush> ambushes;
    Json toJson() const;
};

/// Built from the level and its content (bosses: enemies whose definition is a boss or
/// that are marked as one; ambushes: combat arenas).
RaceLayout buildRaceLayout(const LevelData& level, const ContentDatabase& content, bool withRoute = true);
/// The route of a layout, thinned like buildRaceLayout does (at most about 400 points).
std::vector<Vec2> raceRoute(std::vector<Vec2> route);

struct RaceProgress {
    float route = 0.0f;                 // 0..1, the furthest point reached along the route
    std::vector<float> bossRemoved;     // per boss of the layout: part of its health removed
    std::vector<int> wavesDone;         // per ambush of the layout
    bool finished = false;
    int bossesBeaten() const;
};

/// 0..100 (one decimal). A finished run is 100.
float raceScore(const RaceLayout& layout, const RaceProgress& p);

/// Follows a run of an extreme level: the furthest point on the route, the bosses' health
/// and the ambushes' waves (display and online only: the simulation is not touched).
class RaceTracker {
public:
    void begin(const RaceLayout& layout);
    /// The route arrives later (computed off the main thread when the level starts).
    void setRoute(const std::vector<Vec2>& route);
    void update(GameWorld& world);
    const RaceProgress& progress() const { return progress_; }
    bool active() const { return begun_; }
    const RaceLayout& layout() const { return layout_; }

private:
    void followRoute(Vec2 feet);
    void trackFights(GameWorld& world);
    RaceLayout layout_;
    RaceProgress progress_;
    std::vector<float> cumulative_;
    float length_ = 0.0f;
    size_t hint_ = 0;
    bool begun_ = false;
};

}  // namespace cosmic
