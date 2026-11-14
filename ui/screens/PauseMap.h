#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameWorld;
class UIContext;

/// What the player has seen of the level (cells of the camera's view since the level began).
/// Display only: it never takes part in the simulation (replays stay exact).
struct ExploredMap {
    Rect bounds;
    float cell = 64.0f;
    int cols = 0, rows = 0;
    std::vector<uint8_t> seen;

    void reset(const Rect& levelBounds);
    void mark(const Rect& area);
    bool at(int c, int r) const { return c >= 0 && r >= 0 && c < cols && r < rows && seen[static_cast<size_t>(r * cols + c)] != 0; }
    bool seenPoint(Vec2 p) const;
    bool empty() const;
    /// The smallest rectangle holding every seen cell.
    Rect seenBox() const;
};

/// The map of the pause menu in `area`: only what was explored, with the checkpoints, the
/// exit, the power tablets holding a power the player does not carry (a power left there in
/// exchange, for instance) and the objects still on the ground.
/// Where the map was drawn, and how far it can be scrolled (wider than its frame).
struct PauseMapView {
    Rect dst;
    float scale = 1.0f;          // screen pixels per world pixel
    float minPan = 0.0f, maxPan = 0.0f;  // world units, relative to the window centred on the player
};

/// `drawLevel(dst, win)` draws the real level: the part `win` of the world in `dst`. The map
/// fills `area`, centred on the player and moved by `pan` (world units) when wider than it.
PauseMapView drawPauseMap(UIContext& ui, GameWorld& world, const ExploredMap& seen, const Rect& area,
                          const std::function<void(const Rect& dst, const Rect& win)>& drawLevel, float pan);

}  // namespace cosmic
