#pragma once

// Cosmetic effects (2.19): Nova's trail and the victory effects. Display only: they are fed
// with drawn positions and the render clock, never read by the simulation.

#include <cstdint>
#include <deque>
#include <string>

#include "engine/core/Math.h"
#include "game/cosmetics/CreationLooks.h"

namespace cosmic {

class Renderer;

/// Trail behind Nova (« stardust », « comet », « aurora », « eclipse », and since 2.24
/// « flame » (embers rising), « prism » (a rainbow ribbon), « runes » (turning runes of
/// Aethra), since 2.35 « month_comet » (a gold head, a violet tail with stars), the giga
/// bosses' « ashes », « debris », (2.53) « bubbles » (bubbles of light, curls of ink),
/// (2.56) « feathers » (golden feathers spinning down into embers), (2.57) « souls » (pale
/// souls rising, trembling) and (2.58) « gears » (little brass gears spinning away, puffs of
/// steam); « none »: nothing). 2.59: a trail created on Cosmic Online ("srvtrail:<n>", or the
/// `custom` look given): one of the shapes « comet », « stardust », « prism », « flame »,
/// « aurora », « bubbles » in its two colours, its width, its length in time
/// (CreationLook::lifeMs) and its sparkles.
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
    /// `fade` dims it (a ghost's trail); `custom`: the look of a trail created on Cosmic Online
    /// (else a reference "srvtrail:<n>" in `look` is found through resolveCreationLook()).
    void draw(Renderer& r, const std::string& look, bool world, float scale = 1.0f, float fade = 1.0f, const CreationLook* custom = nullptr) const;
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
/// `time` waves it gently; `custom`: as TrailFx::draw.
void drawTrailSample(Renderer& r, const std::string& look, const Rect& box, float time, float fade = 1.0f, const CreationLook* custom = nullptr);

/// Length of a victory effect (seconds).
constexpr float kVictoryFxTime = 2.6f;
/// Victory effect around `at` (Nova at the exit), `t` seconds after the finish
/// (« fireworks », « starfall », « supernova », and since 2.24 « vortex » (turning arms of
/// sparks), « borealis » (curtains of light), « corona » (an eclipse and its rays), since 2.35
/// « fire_streak » (seven flames lit one after the other), the giga bosses' « dragon_breath »,
/// « quake », (2.53) « tidal_wave » (a wave rises, breaks, leaves bubbles of light), (2.56)
/// « rebirth » (the portal burns to ash, a phoenix of light is born from it and flies up),
/// (2.57) « candles » (candles of soul fire light up round the portal, their souls rise) and
/// (2.58) « foundry » (gears turn round the portal, molten steel bursts up in sparks);
/// « classic »: nothing more than the game's own). 2.59: an effect created on Cosmic Online
/// ("srvvictory:<n>", or the `custom` look given): « fireworks », « starfall », « supernova »,
/// « vortex », « borealis » or « corona » in its two colours.
void drawVictoryFx(Renderer& r, const std::string& look, Vec2 at, float t, bool world, float scale = 1.0f, const CreationLook* custom = nullptr);

/// How far an avatar frame reaches beyond the avatar's radius, in units of `scale` (glows
/// aside): layouts keep this much room around a framed avatar.
constexpr float kAvatarFrameMargin = 15.5f;
/// Avatar frame (2.23) around a round avatar of `radius` centred on `center`, in screen
/// pixels: « simple », « bronze », « silver », « gold » (with a halo), « platinum »
/// (iridescent), « constellation » (small stars around), and since 2.24 « emerald » (cut
/// gems), « orbit » (two moons going round), « nebula » (turning clouds of colour),
/// « laurels » (a crown of leaves), since 2.35 « crown » (a gold crown on top, seven studs),
/// the giga bosses' « scales », « runic », (2.53) « tentacles » (two tentacles coiled
/// around, their suckers lighting up), (2.56) « plumage » (feathers fanned around, their
/// tips lit one after the other), (2.57) « ossuary » (bones end to end, six skulls whose
/// eyes light up in turn) and (2.58) « cogs » (brass gears turning round it, rivets between);
/// anything else draws « simple ». 2.59: a frame created on Cosmic Online ("srvframe:<n>", or
/// the `custom` look given): « metal », « emerald », « orbit », « nebula », « constellation » or
/// « runic » in its border and glow colours.
/// `scale` sizes the rings, `time` (seconds) moves the sheens, stars and moons.
/// Shared by the Collection, the leaderboards and the profiles.
void drawAvatarFrame(Renderer& r, Vec2 center, float radius, const std::string& look, float scale, float time = 0.0f, const CreationLook* custom = nullptr);

}  // namespace cosmic
