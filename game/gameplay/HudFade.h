#pragma once

#include "engine/core/Math.h"

namespace cosmic {

/// 2.60, « Effacer le HUD quand il cache l'action » (Paramètres › Jeu): a block of the HUD that
/// hides something important — the boss, Nova, an attack of the boss — fades, then comes back
/// once nothing is under it any more. Display only: the simulation never sees it.
namespace HudFade {

/// What is left of a block over the action, and how long the whole way takes (1 → kFaded, or back).
constexpr float kFaded = 0.28f;
constexpr float kSeconds = 0.15f;

/// Whether a block of the HUD (its rectangle on screen) hides a thing (they overlap by more than
/// an edge). An empty rectangle hides nothing and is hidden by nothing.
bool hides(const Rect& block, const Rect& thing);

/// Whether something important is under a block: Nova, the boss, or an attack of the boss
/// (`attack`: one of its shapes touches the block). The boss's own health bar (`bossBar`) only
/// gives way to Nova: the boss and its attacks are what it is about.
bool covered(const Rect& block, const Rect& nova, const Rect& boss, bool attack, bool bossBar);

/// One step of a block's opacity toward its target (kFaded when covered, 1 when not), at a
/// constant pace: the whole way in kSeconds.
float step(float opacity, bool covered, float dt);

/// The visible part of a sprite on screen: `dst` where its frame is drawn, `frame` the size of a
/// frame, `seen` the box of its visible pixels in the frame (frame coordinates), the sprite
/// mirrored with `flipX`. An empty `seen`: the whole frame.
Rect visiblePart(const Rect& dst, Vec2 frame, const Rect& seen, bool flipX);

}  // namespace HudFade

}  // namespace cosmic
