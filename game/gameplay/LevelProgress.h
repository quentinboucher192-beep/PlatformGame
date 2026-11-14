#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class ContentDatabase;
struct LevelData;

/// Progress bar of a level: the intended route (start -> checkpoints -> exit) measured in
/// pixels, and the places along it worth showing (checkpoints, ambushes, bosses).
struct LevelProgressTrack {
    struct Marker {
        enum class Kind { Checkpoint, Ambush, Boss };
        Kind kind = Kind::Checkpoint;
        float at = 0.0f;       // 0..1 along the route
        std::string objectId;  // level object (state read from the world)
        std::string label;     // boss name, "3 vagues"...
        Color color{255, 70, 96, 255};  // ambush skull (editor "Aspect", default: the planet's colour)
        std::string enemyId;            // boss definition (badge)
    };
    std::vector<Vec2> route;
    std::vector<float> cumulative;  // px from the start, per route point
    float length = 0.0f;
    std::vector<Marker> markers;

    bool valid() const { return route.size() >= 2 && length > 1.0f; }
    /// Fraction of the route reached at `feet`. `hint` (route index of the previous call)
    /// keeps the search local, so a route passing above or below does not make it jump.
    float project(Vec2 feet, size_t& hint) const;
    float fractionAt(Vec2 p) const;
};

/// Computed off the main thread when a level starts (LevelAnalyzer::intendedRoute).
LevelProgressTrack buildProgressTrack(const LevelData& level, const ContentDatabase& content);

}  // namespace cosmic
