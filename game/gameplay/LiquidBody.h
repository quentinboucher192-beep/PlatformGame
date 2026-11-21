#pragma once

#include <string_view>

#include "engine/core/Math.h"

namespace cosmic {

struct RenderContext;

/// The colours of a body of liquid (display only): the bosses' floods and the raids' acid.
struct LiquidLook {
    Color shallow;                // the body just under its surface (its alpha: a little see-through)
    Color deep;                   // the body at its depth: darker, more opaque
    Color crest;                  // the bright line of its surface (additive)
    Color foam;                   // on the peaks of its swell (additive)
    Color light;                  // its glow (light map)
    bool glowsInside = false;     // acid, lava, plasma: lit from within too (not the dark water)
};

/// The look of a flood (`BossHazards::Flood::look`): "" / "lava", "plasma", "tide" (the dark water
/// of the abyss), "acid". `accent`: the boss's light (the tide's crests, the acid's glow).
LiquidLook liquidLook(std::string_view look, Color accent);

/// Draws a body of liquid filling `body` from its surface (body.y, an animated swell with its
/// crest and foam) down to body.bottom(), wall to wall (body.x .. body.right()): a gradient darker
/// deeper, a light under the surface, slow caustics, bubbles, a meniscus against each end. Solid
/// down to `solidBottom`, from there it fades out to body.bottom() (it melts into the ground
/// under its floor). `alpha` scales it (fading in). Display only (time-driven).
void drawLiquidBody(RenderContext& ctx, const Rect& body, float solidBottom, const LiquidLook& look, float t, float alpha = 1.0f);

}  // namespace cosmic
