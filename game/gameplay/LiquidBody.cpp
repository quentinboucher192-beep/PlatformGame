#include "game/gameplay/LiquidBody.h"

#include <algorithm>
#include <cmath>

#include "engine/core/DetMath.h"
#include "game/gameplay/Entity.h"

namespace cosmic {

LiquidLook liquidLook(std::string_view look, Color accent) {
    if (look == "acid")
        return {{74, 172, 40, 188}, {18, 60, 22, 232}, accent.withAlpha(230), {225, 255, 190, 200}, accent.withAlpha(255), true};
    if (look == "plasma")
        return {{206, 62, 176, 192}, {78, 10, 62, 234}, {255, 170, 235, 230}, {255, 230, 250, 210}, {255, 120, 220, 255}, true};
    if (look == "tide")
        return {{30, 66, 116, 205}, {6, 14, 40, 238}, accent.withAlpha(210), {235, 250, 255, 190}, accent.withAlpha(255), false};
    return {{236, 96, 32, 200}, {112, 22, 8, 236}, {255, 200, 90, 230}, {255, 240, 180, 210}, {255, 120, 50, 255}, true};
}

void drawLiquidBody(RenderContext& ctx, const Rect& body, float solidBottom, const LiquidLook& look, float t, float alpha) {
    if (body.w <= 0.5f || body.h <= 0.5f || alpha <= 0.0f) return;
    const Rect vis = ctx.r.view().visibleWorld().expanded(16.0f);
    const float x0 = std::max(body.x, vis.x), x1 = std::min(body.right(), vis.right());
    if (x1 <= x0 || body.y > vis.bottom() || body.bottom() < vis.y) return;
    auto fade = [&](Color c, float k = 1.0f) { return c.withAlpha(static_cast<uint8_t>(std::clamp(c.a * alpha * k, 0.0f, 255.0f))); };
    auto screen = [&](const Rect& r) {  // snapped like fillWorldRect: no seam between the two
        const Rect s = ctx.r.view().toScreen(r);
        const float sx0 = std::round(s.x), sy0 = std::round(s.y);
        return Rect{sx0, sy0, std::round(s.right()) - sx0, std::round(s.bottom()) - sy0};
    };
    constexpr float kLip = 4.0f;  // the swell's reach under the surface line
    const float top = body.y;
    const float solid = std::clamp(solidBottom, top + kLip, body.bottom());
    const float depth = solid - top - kLip;
    const float w = x1 - x0;

    // The body: lighter under its surface, darker and more opaque deeper; then it melts into the
    // ground below its floor.
    if (depth > 0.0f) ctx.r.fillGradientV(screen({x0, top + kLip, w, depth}), fade(look.shallow), fade(look.deep));
    if (body.bottom() > solid) ctx.r.fillGradientV(screen({x0, solid, w, body.bottom() - solid}), fade(look.deep), fade(look.deep, 0.0f));

    // A light just under the surface, slow caustics drifting down through it.
    if (depth > 2.0f) {
        ctx.r.fillGradientV(screen({x0, top + kLip, w, std::min(20.0f, depth)}), fade(look.crest, 0.28f), fade(look.crest, 0.0f), BlendMode::Additive);
        for (int k = 0; k < 2 && depth > 24.0f; ++k) {
            const float ph = std::fmod(t * 0.11f + static_cast<float>(k) * 0.5f, 1.0f);
            const float y = top + kLip + 8.0f + ph * (std::min(depth, 200.0f) - 16.0f);
            const float k01 = 1.0f - std::abs(2.0f * ph - 1.0f);
            for (float x = body.x + std::floor((x0 - body.x) / 36.0f) * 36.0f; x < x1; x += 36.0f) {
                const float s = 0.5f + 0.5f * dmath::sin(t * 1.3f + x * 0.05f + static_cast<float>(k) * 2.0f);
                const float sx = std::max(x, body.x), ex = std::min(x + 20.0f, body.right());
                if (ex <= sx) continue;
                ctx.r.fillWorldRect({sx, y + 2.0f * dmath::sin(x * 0.04f + t), ex - sx, 1.5f}, fade(look.crest, 0.2f * s * k01), BlendMode::Additive);
            }
        }
    }

    // Its surface: a swell (two waves), its bright crest, foam where it peaks.
    constexpr float kCol = 6.0f;
    for (float x = body.x + std::floor((x0 - body.x) / kCol) * kCol; x < x1; x += kCol) {
        const float sx = std::max(x, body.x), wd = std::min(x + kCol, body.right()) - sx;
        if (wd <= 0.0f) continue;
        const float swell = 2.6f * dmath::sin(t * 1.7f + x * 0.031f) + 1.3f * dmath::sin(t * 3.9f + x * 0.113f);
        const float y = std::min(top + swell, top + kLip - 0.5f);
        ctx.r.fillWorldRect({sx, y, wd, top + kLip - y}, fade(look.shallow));
        ctx.r.fillWorldRect({sx, y - 1.0f, wd, 2.0f}, fade(look.crest), BlendMode::Additive);
        if (swell < -2.9f) ctx.r.fillWorldRect({sx, y - 2.5f, wd, 2.0f}, fade(look.foam), BlendMode::Additive);
    }

    // A meniscus against each end (a wall, or the edge of its basin).
    const float edgeH = std::max(0.0f, solid - top);
    if (edgeH > 0.0f) {
        if (body.x >= x0 - 1.0f) ctx.r.fillGradientH(screen({body.x, top, 5.0f, edgeH}), fade(look.crest, 0.4f), fade(look.crest, 0.0f), BlendMode::Additive);
        if (body.right() <= x1 + 1.0f)
            ctx.r.fillGradientH(screen({body.right() - 5.0f, top, 5.0f, edgeH}), fade(look.crest, 0.0f), fade(look.crest, 0.4f), BlendMode::Additive);
    }

    // Bubbles rising inside it.
    const float rise = std::min(depth, 220.0f);
    const int bubbles = static_cast<int>(body.w / 44.0f);
    for (int k = 0; k < bubbles && rise > 12.0f; ++k) {
        const float ph = std::fmod(t * 0.45f + static_cast<float>(k) * 0.618f, 1.0f);
        const float x = body.x + std::fmod(static_cast<float>(k) * 97.3f, body.w) + 4.0f * dmath::sin(t * 3.0f + static_cast<float>(k));
        if (x < x0 || x > x1) continue;
        const float y = top + kLip + 4.0f + (1.0f - ph) * (rise - 8.0f);
        ctx.r.drawWorldCircle({x, y}, 1.5f + 1.5f * ph, fade(look.foam, 0.75f * ph), 1.0f);
    }

    // Its glow: along its surface, and from within when it shines (not under the dark water).
    const float lx0 = body.x + 48.0f + std::max(0.0f, std::floor((x0 - 120.0f - body.x - 48.0f) / 120.0f) * 120.0f);
    for (float x = lx0; x < std::min(body.right(), x1 + 120.0f); x += 120.0f) ctx.lights.add({x, top}, 110.0f, fade(look.light));
    if (look.glowsInside && depth > 24.0f) {
        const float r = std::clamp(depth * 0.7f, 60.0f, 150.0f);
        const float ly = top + kLip + std::min(depth * 0.5f, 120.0f);
        for (float x = lx0 + 60.0f; x < std::min(body.right(), x1 + 120.0f); x += 120.0f) ctx.lights.add({x, ly}, r, fade(look.light, 0.55f));
    }
}

}  // namespace cosmic
