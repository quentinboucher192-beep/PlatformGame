// Decoration, lights and particle emitters.

#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

void DecorEntity::init(GameWorld& w) {
    auto& atlas = DecorAtlas::shared(w.resources());
    sprite_ = atlas.find(obj_.propStr("sprite", "mushroom_tall"));
    tex_ = sprite_ ? sprite_->texture : atlas.texture();
    tint_ = Color::fromHex(obj_.propStr("tint", "#ffffff"));
    alpha_ = std::clamp(obj_.prop<float>("alpha", 1.0f), 0.0f, 1.0f);
    animSpeed_ = obj_.prop<float>("animSpeed", 1.0f);
    sway_ = obj_.prop<float>("sway", 0.0f);
    glow_ = obj_.prop<bool>("glow", true);
    seed_ = std::fmod(box.x * 0.013f + box.y * 0.007f, 6.28f);
}

void DecorEntity::render(RenderContext& ctx) {
    if (!tex_ || !sprite_) {
        if (ctx.editor) ctx.r.drawWorldRect(box, {255, 0, 255, 200});
        return;
    }
    Rect src = sprite_->frameRect(ctx.time + seed_, animSpeed_);
    float angle = obj_.rotation;
    if (sway_ > 0.0f) angle += std::sin(ctx.time * 1.3f + seed_) * sway_;
    Color c = tint_;
    c.a = static_cast<uint8_t>(255 * alpha_ * fadeOverride);
    if (c.a == 0) return;
    // Hanging decor sways around its top, standing decor around its base.
    Vec2 pivot{box.w * 0.5f, sprite_->pivot.y < sprite_->rect.h * 0.5f ? 0.0f : box.h};
    ctx.r.drawWorldTexture(*tex_, &src, box, c, BlendMode::Alpha, obj_.flipX, angle, &pivot);
    if (glow_ && sprite_->glowRadius > 0.0f && sprite_->glow.a > 0) {
        float sx = box.w / std::max(1.0f, sprite_->rect.w), sy = box.h / std::max(1.0f, sprite_->rect.h);
        Vec2 anchor{box.x + sprite_->pivot.x * sx, box.y + sprite_->pivot.y * sy};
        Vec2 gp = anchor + Vec2{sprite_->glowOffset.x * sx * (obj_.flipX ? -1.0f : 1.0f), sprite_->glowOffset.y * sy};
        float pulse = 0.85f + 0.15f * std::sin(ctx.time * 1.7f + seed_);
        float rad = sprite_->glowRadius * std::max(sx, sy) * pulse;
        ctx.lights.add(gp, rad * 1.6f, sprite_->glow);
        ctx.r.drawWorldGlow(gp, rad * 0.35f, sprite_->glow.withAlphaF(0.22f * alpha_ * fadeOverride));
    }
}

void LightEntity::render(RenderContext& ctx) {
    Color c = Color::fromHex(obj_.propStr("color", "#39f0d8"));
    float radius = obj_.prop<float>("radius", 80.0f);
    float flicker = obj_.prop<float>("flicker", 0.1f);
    float n = std::sin(ctx.time * 7.3f + box.x) * 0.5f + std::sin(ctx.time * 13.1f + box.y) * 0.5f;
    float r = radius * (1.0f - flicker * 0.5f * (n + 1.0f) * 0.5f);
    ctx.lights.add(box.center(), r, c);
    if (obj_.prop<bool>("bloom", true)) ctx.r.drawWorldGlow(box.center(), r * 0.18f, c.withAlpha(70));
    if (ctx.editor) {
        ctx.r.drawWorldCircle(box.center(), radius, c.withAlpha(90), 1.0f);
        ctx.r.fillWorldCircle(box.center(), 3.0f, c);
    }
}

void EmitterEntity::update(GameWorld& w, float dt) {
    float rate = std::max(0.05f, obj_.prop<float>("rate", 2.0f));
    timer_ -= dt;
    // Only emit near the camera (performance).
    Rect vis = w.camera().visibleArea(1920, 1080).expanded(120);
    if (!vis.intersects(box)) return;
    int guard = 0;
    while (timer_ <= 0.0f && guard++ < 8) {
        timer_ += 1.0f / rate;
        Vec2 p{w.rng().range(box.x, box.right()), w.rng().range(box.y, box.bottom())};
        w.particles().emit(obj_.propStr("effect", "fireflies"), p, 1.0f);
    }
    if (timer_ < -1.0f) timer_ = 0.0f;
}

void EmitterEntity::render(RenderContext& ctx) {
    if (ctx.editor) {
        ctx.r.drawWorldRect(box, {200, 255, 255, 120});
    }
}

}  // namespace cosmic
