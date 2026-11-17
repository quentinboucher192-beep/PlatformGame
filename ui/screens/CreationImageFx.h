#pragma once

// 2.61 Studio de Créations: the creations drawn from their image (game/cosmetics/CreationImages.h)
// — a trail or a victory effect throws its animated particle (its frames played over each
// particle's life, tinted from the creation's first colour to its second) as its emitter says; an
// avatar frame draws its ring around the avatar, looped. Used by the game (CosmeticFx.cpp, when
// the image of a "srv<kind>:<n>" reference is ready) and by the Studio's live preview. Display
// only: fed with drawn positions and the render clock, never read by the simulation.

#include <cstdint>
#include <vector>

#include "engine/core/Math.h"
#include "game/cosmetics/CreationImages.h"
#include "game/cosmetics/SuitRecolor.h"

namespace cosmic {

class Renderer;
class Texture;

/// An image cut in frames side by side.
struct ImageSprite {
    const Texture* texture = nullptr;
    int frameW = 16, frameH = 16, frames = 1, fps = 12;
};
ImageSprite spriteOf(const CreationImageDraw& d);

/// A sample of a trail (TrailFx): Nova's drawn centre, how old it is, its number.
struct TrailSample {
    Vec2 p;
    float age = 0.0f;
    uint32_t seed = 0;
};

/// The particles of a trail thrown from its samples (oldest first): in world space, or in
/// screen pixels scaled by `scale`; `fade` dims it (a ghost's trail).
void drawImageTrail(Renderer& r, const ImageSprite& s, const ImageEmitter& e, Rgb8 a, Rgb8 b, const std::vector<TrailSample>& samples, bool world,
                    float scale = 1.0f, float fade = 1.0f);
/// A victory effect `t` seconds after the finish (kVictoryFxTime long), around `at` (Nova's feet).
void drawImageVictory(Renderer& r, const ImageSprite& s, const ImageEmitter& e, Rgb8 a, Rgb8 b, Vec2 at, float t, bool world, float scale = 1.0f);
/// An avatar frame around a round avatar of `radius` centred on `center` (screen pixels).
void drawImageFrame(Renderer& r, const ImageSprite& s, Vec2 center, float radius, float scale, float time);

}  // namespace cosmic
