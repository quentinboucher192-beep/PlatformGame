#pragma once

// Cosmetic effects (2.19): Nova's trail and the victory effects. Display only: they are fed
// with drawn positions and the render clock, never read by the simulation.

#include <cstdint>
#include <deque>
#include <string>

#include "engine/core/Math.h"

namespace cosmic {

class Renderer;

/// Trail behind Nova (« stardust », « comet », « aurora », « eclipse », and since 2.24
/// « flame » (embers rising), « prism » (a rainbow ribbon), « runes » (turning runes of
/// Aethra), since 2.35 « month_comet » (a gold head, a violet tail with stars), the giga
/// bosses' « ashes », « debris » and (2.52) « bubbles » (bubbles of light, curls of ink);
/// « none »: nothing).
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

/// A still sample of a trail across `box` (screen pixels), drawn as the game draws it: the
/// thumbnails of the Collection and of the rewards card. `fade` dims it (locked item),
/// `time` waves it gently.
void drawTrailSample(Renderer& r, const std::string& look, const Rect& box, float time, float fade = 1.0f);

/// Length of a victory effect (seconds).
constexpr float kVictoryFxTime = 2.6f;
/// Victory effect around `at` (Nova at the exit), `t` seconds after the finish
/// (« fireworks », « starfall », « supernova », and since 2.24 « vortex » (turning arms of
/// sparks), « borealis » (curtains of light), « corona » (an eclipse and its rays), since 2.35
/// « fire_streak » (seven flames lit one after the other), the giga bosses' « dragon_breath »,
/// « quake » and (2.52) « tidal_wave » (a wave rises, breaks, leaves bubbles of light);
/// « classic »: nothing more than the game's own).
void drawVictoryFx(Renderer& r, const std::string& look, Vec2 at, float t, bool world, float scale = 1.0f);

/// How far an avatar frame reaches beyond the avatar's radius, in units of `scale` (glows
/// aside): layouts keep this much room around a framed avatar.
constexpr float kAvatarFrameMargin = 15.5f;
/// Avatar frame (2.23) around a round avatar of `radius` centred on `center`, in screen
/// pixels: « simple », « bronze », « silver », « gold » (with a halo), « platinum »
/// (iridescent), « constellation » (small stars around), and since 2.24 « emerald » (cut
/// gems), « orbit » (two moons going round), « nebula » (turning clouds of colour),
/// « laurels » (a crown of leaves), since 2.35 « crown » (a gold crown on top, seven studs),
/// the giga bosses' « scales », « runic » and (2.52) « tentacles » (two tentacles coiled
/// around, their suckers lighting up); anything else draws « simple ».
/// `scale` sizes the rings, `time` (seconds) moves the sheens, stars and moons.
/// Shared by the Collection, the leaderboards and the profiles.
void drawAvatarFrame(Renderer& r, Vec2 center, float radius, const std::string& look, float scale, float time = 0.0f);

}  // namespace cosmic
