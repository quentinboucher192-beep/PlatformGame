#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ContentDatabase;

/// Shortcut detector for levels.
///
/// The level is rasterised at 8 px; a cell is free when the player's box fits there. The
/// player's abilities in this level (start powers, powers picked up) give how high above a
/// support it can rise (jumps, impulse, upward teleport) and how far it can travel in the air
/// (dash, glide). Supports are floors and platforms, walls (climbable with wall jumps),
/// ladders, updrafts, bounce pads and grapple points. Deadly liquids and the bottom of the
/// level are forbidden. The model is deliberately generous: it over-estimates what a very
/// good player can do, so a route it does not find does not exist.
///
/// The shortest possible route from the start to the exit is then compared with the intended
/// route, which goes through every checkpoint in order. A checkpoint far from the shortest
/// route, or a shortest route much shorter than the intended one, is reported.
struct LevelAnalysis {
    struct Checkpoint {
        std::string label;
        Vec2 pos;
        float distanceToRoute = 0.0f;  // px between the checkpoint and the shortest route
        bool skipped = false;
    };

    bool reachable = false;          // the exit can be reached at all
    float shortestRoute = 0.0f;      // px, start -> exit, any route
    float intendedRoute = 0.0f;      // px, start -> checkpoints in order -> exit
    float ratio = 1.0f;              // shortest / intended
    float maxRise = 0.0f;            // px a player can rise above a support (abilities)
    float airReach = 0.0f;           // px of horizontal travel in the air
    bool reachesTop = false;         // a reachable cell touches the top of the level
    std::vector<Checkpoint> checkpoints;
    std::vector<Vec2> route;         // shortest route (cell centres, feet)
    std::vector<Vec2> intendedPath;  // route through every checkpoint (feet)
    struct Bypass {
        Rect door;
        std::vector<Vec2> route;  // way around the closed door
    };
    std::vector<Bypass> doorBypasses;
    std::vector<std::string> problems;

    bool ok() const { return problems.empty(); }
};

struct LevelAnalyzerOptions {
    /// Shortest / intended below this ratio: shortcut.
    float minRatio = 0.85f;
    /// A checkpoint farther than this from the shortest route is skipped.
    float skipDistance = 192.0f;
    /// A closed door that can be walked around within this distance is useless.
    float doorDetour = 4000.0f;
    /// Powers available besides the level's start powers and pickups (campaign context).
    std::vector<std::string> extraPowers;
};

LevelAnalysis analyzeLevel(const LevelData& level, const ContentDatabase& content, const LevelAnalyzerOptions& opt = {});

/// Solid columns ("gates") hanging from the top of the level above every skipped
/// checkpoint, stopping `clearance` px above the intended route (never across it): the
/// route then has to pass by the checkpoint. Snapped to the 16 px tile grid.
std::vector<Rect> suggestGates(const LevelData& level, const LevelAnalysis& a, float clearance = 112.0f, float width = 48.0f,
                               bool deviationGates = false);

/// Writes a PPM picture of the analysis (terrain, reachable space, routes, checkpoints).
bool writeAnalysisImage(const LevelData& level, const ContentDatabase& content, const LevelAnalysis& a, const std::string& path,
                        const LevelAnalyzerOptions& opt = {});

}  // namespace cosmic
