#include "game/powers/PowerFx.h"

#include <algorithm>
#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/rendering/Renderer.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

void renderFrozenEnemy(RenderContext& ctx, const Enemy& e) {
    if (!e.alive() || e.dormant()) return;
    Rect b{lerpf(e.prevBox.x, e.box.x, ctx.alpha), lerpf(e.prevBox.y, e.box.y, ctx.alpha), e.box.w, e.box.h};
    const Color ice{150, 220, 255, 255};
    if (!e.frozen()) {
        if (e.slowed()) {  // a boss: slowed, not frozen
            float pulse = 0.75f + 0.25f * dmath::sin(ctx.time * 6.0f);
            ctx.r.drawWorldGlow(b.center(), std::max(b.w, b.h) * 0.9f, ice.withAlphaF(0.35f * pulse));
            ctx.lights.add(b.center(), std::max(b.w, b.h) * 1.4f, ice);
        }
        return;
    }
    // A block of ice around the enemy: tinted body, bright top (where Nova can stand),
    // crisp outline and two glints.
    Rect block{b.x - 2.0f, b.y - 3.0f, b.w + 4.0f, b.h + 3.0f};
    ctx.r.drawWorldGlow(block.center(), std::max(block.w, block.h) * 0.85f, ice.withAlphaF(0.35f));
    ctx.r.fillWorldRect(block, Color{140, 215, 255, 120});
    ctx.r.fillWorldRect(block, Color{60, 140, 220, 40}, BlendMode::Additive);
    ctx.r.fillWorldRect({block.x, block.y, block.w, 2.0f}, Color{235, 250, 255, 230});
    ctx.r.drawWorldRect(block, Color{205, 242, 255, 230}, 1.0f);
    float gx = block.x + std::min(4.0f, block.w * 0.25f);
    ctx.r.drawWorldLine({gx, block.bottom() - 3.0f}, {gx + 4.0f, block.y + 5.0f}, Color{255, 255, 255, 170}, 1.0f);
    ctx.r.drawWorldLine({gx + 3.0f, block.bottom() - 2.0f}, {gx + 5.0f, block.bottom() - 7.0f}, Color{255, 255, 255, 120}, 1.0f);
    ctx.lights.add(block.center(), 40.0f, ice);
}

void renderDecoy(RenderContext& ctx, const GameWorld& w) {
    if (!w.decoyActive()) return;
    const Player& p = w.player();
    float left = w.decoyRemaining(), total = std::max(0.01f, w.decoyDuration());
    float appear = std::clamp((total - left) / 0.18f, 0.0f, 1.0f);
    float fade = std::clamp(left / 0.25f, 0.0f, 1.0f);
    float blink = left < 0.8f && static_cast<int>(left * 14.0f) % 2 == 0 ? 0.5f : 1.0f;
    float flicker = 0.88f + 0.12f * dmath::sin(ctx.time * 41.0f);
    float a = appear * fade * blink * flicker;
    const Color holo{120, 255, 240, 255};
    Rect body = w.decoyBox();
    Vec2 feet = body.bottomCenter();
    // Projector pad under its feet.
    ctx.r.drawWorldGlow(feet + Vec2{0.0f, -1.0f}, 13.0f, holo.withAlphaF(0.35f * a));
    ctx.r.fillWorldRect({feet.x - 8.0f, feet.y - 1.0f, 16.0f, 1.0f}, holo.withAlphaF(0.8f * a), BlendMode::Additive);
    const auto& set = p.anim.set();
    const AnimationClip* clip = set && set->texture ? set->clip("idle") : nullptr;
    if (clip && clip->frameCount() > 0) {
        Rect src = set->frameRect(*clip, static_cast<int>(ctx.time * 6.0f) % clip->frameCount());
        bool flip = w.decoyFacing() < 0.0f;
        float px = flip ? (set->frameWidth - set->pivot.x) : set->pivot.x;
        // Rises from the pad as it appears.
        float h = set->frameHeight * (0.35f + 0.65f * appear);
        Rect dst{feet.x - px, feet.y - set->pivot.y * (h / set->frameHeight), static_cast<float>(set->frameWidth), h};
        ctx.r.drawWorldTexture(*set->texture, &src, dst, Color{150, 255, 245, static_cast<uint8_t>(150 * a)}, BlendMode::Alpha, flip);
        ctx.r.drawWorldTexture(*set->texture, &src, dst, Color{57, 240, 216, static_cast<uint8_t>(130 * a)}, BlendMode::Additive, flip);
    } else {
        ctx.r.fillWorldRect(body, holo.withAlphaF(0.4f * a), BlendMode::Additive);
    }
    // Scan lines running up the body.
    for (int i = 0; i < 3; ++i) {
        float y = body.bottom() - std::fmod(ctx.time * 24.0f + i * body.h / 3.0f, body.h);
        ctx.r.fillWorldRect({body.x - 3.0f, y, body.w + 6.0f, 1.0f}, holo.withAlphaF(0.4f * a), BlendMode::Additive);
    }
    // What is left of its time, above the head.
    float k = std::clamp(left / total, 0.0f, 1.0f);
    Rect bar{body.center().x - 8.0f, body.y - 6.0f, 16.0f, 1.5f};
    ctx.r.fillWorldRect(bar, Color{10, 30, 40, static_cast<uint8_t>(140 * appear * fade)});
    ctx.r.fillWorldRect({bar.x, bar.y, bar.w * k, bar.h}, holo.withAlphaF(0.9f * appear * fade));
    ctx.lights.add(body.center(), 56.0f, holo);
}

}  // namespace cosmic
