#pragma once

// Cosmetic effects (2.19): Nova's trail and the victory effects. Display only: they are fed
// with drawn positions and the render clock, never read by the simulation.

#include <cstdint>
#include <deque>
#include <string>

#include "engine/core/Math.h"

namespace cosmic {

class Renderer;

/// Trail behind Nova (« stardust », « comet », « aurora », « eclipse »; « none »: nothing).
class TrailFx {
public:
    void reset() {
        pts_.clear();
        acc_ = 0.0f;
        have_ = false;
    }
    /// `center`: Nova's drawn centre; `emit`: she moves fast enough to leave a trail.
    void update(Vec2 center, float dt, bool emit);
    /// world: in world space (the renderer's camera), else in screen pixels scaled by `scale`.
    void draw(Renderer& r, const std::string& look, bool world, float scale = 1.0f) const;
    bool empty() const { return pts_.empty(); }

private:
    struct Pt {
        Vec2 p;
        float age = 0.0f;
        uint32_t seed = 0;
    };
    std::deque<Pt> pts_;
    float acc_ = 0.0f;
    Vec2 last_;
    bool have_ = false;
    uint32_t counter_ = 0;
};

/// Length of a victory effect (seconds).
constexpr float kVictoryFxTime = 2.6f;
/// Victory effect around `at` (Nova at the exit), `t` seconds after the finish
/// (« fireworks », « starfall », « supernova »; « classic »: nothing more than the game's own).
void drawVictoryFx(Renderer& r, const std::string& look, Vec2 at, float t, bool world, float scale = 1.0f);

}  // namespace cosmic
