#include "game/raid/ArenaCamera.h"

#include <algorithm>
#include <cmath>

namespace cosmic::raid {

void ArenaCamera::reset() {
    active_ = seen_ = false;
    introLeft_ = blendLeft_ = 0.0f;
}

float ArenaCamera::heightFor(const Rect& r, int screenW, int screenH) {
    const float aspect = static_cast<float>(std::max(1, screenW)) / static_cast<float>(std::max(1, screenH));
    return std::max(r.h, r.w / aspect);
}

Rect ArenaCamera::focus(const Frame& f, const Tuning& t) {
    Rect r{f.me.x - t.margin, f.me.y - t.margin, t.margin * 2.0f, t.margin * 2.0f};
    auto add = [&](Vec2 p, float pad) {
        const float x0 = std::min(r.x, p.x - pad), y0 = std::min(r.y, p.y - pad);
        const float x1 = std::max(r.right(), p.x + pad), y1 = std::max(r.bottom(), p.y + pad);
        r = {x0, y0, x1 - x0, y1 - y0};
    };
    for (const Vec2& th : f.threats)
        if (distance(th, f.me) <= t.threatReach) add(th, 48.0f);
    for (const Vec2& a : f.allies)
        if (distance(a, f.me) <= t.allyReach) add(a, 40.0f);
    if (f.boss.w > 0.0f) {
        // The boss when it is near: the part of it toward me (it is bigger than any view).
        const Vec2 c = f.boss.center();
        if (distance(c, f.me) <= t.bossReach) {
            const Vec2 nearPart{std::clamp(f.me.x, f.boss.x, f.boss.right()), std::clamp(f.me.y, f.boss.y, f.boss.bottom())};
            add(nearPart, 90.0f);
        }
    }
    return r;
}

Vec2 ArenaCamera::clamp(Vec2 c, float h, const Rect& level, int screenW, int screenH) const {
    if (level.w <= 0.0f || level.h <= 0.0f) return c;
    const float w = h * static_cast<float>(std::max(1, screenW)) / static_cast<float>(std::max(1, screenH));
    if (w < level.w) c.x = std::clamp(c.x, level.x + w * 0.5f, level.right() - w * 0.5f);
    else c.x = level.center().x;
    if (h < level.h) c.y = std::clamp(c.y, level.y + h * 0.5f, level.bottom() - h * 0.5f);
    else c.y = level.center().y;
    return c;
}

void ArenaCamera::update(const Frame& f, float dt, int screenW, int screenH) {
    if (f.arena.w <= 0.0f || !f.meInArena) {
        active_ = false;
        return;
    }
    if (!seen_) {
        // The first time in the arena: the whole of it, the boss in it.
        seen_ = true;
        introLeft_ = tune_.intro;
        blendLeft_ = tune_.blend;
        center_ = f.arena.center();
        height_ = heightFor(f.arena, screenW, screenH);
    }
    active_ = true;
    Vec2 wantC;
    float wantH;
    if (introLeft_ > 0.0f) {
        introLeft_ = std::max(0.0f, introLeft_ - dt);
        wantC = f.arena.center();
        wantH = heightFor(f.arena, screenW, screenH);
        center_ = wantC;
        height_ = wantH;
        return;
    }
    const Rect r = focus(f, tune_);
    wantH = std::clamp(heightFor(r, screenW, screenH), tune_.minView, tune_.maxView);
    wantC = r.center();
    // Me always inside (the focus may be wider than the view when clamped): toward me.
    const float halfW = wantH * static_cast<float>(std::max(1, screenW)) / static_cast<float>(std::max(1, screenH)) * 0.5f, halfH = wantH * 0.5f;
    const float keep = 0.7f;
    wantC.x = std::clamp(wantC.x, f.me.x - halfW * keep, f.me.x + halfW * keep);
    wantC.y = std::clamp(wantC.y, f.me.y - halfH * keep, f.me.y + halfH * keep);
    // Eased (faster for the position than for the zoom); the blend after the intro is slower.
    float k = blendLeft_ > 0.0f ? 1.6f : 5.0f, kz = blendLeft_ > 0.0f ? 1.4f : 2.5f;
    blendLeft_ = std::max(0.0f, blendLeft_ - dt);
    const float a = 1.0f - std::exp(-k * dt), az = 1.0f - std::exp(-kz * dt);
    center_ = center_ + (wantC - center_) * a;
    height_ = height_ + (wantH - height_) * az;
    center_ = clamp(center_, height_, f.level, screenW, screenH);
}

Rect ArenaCamera::shown(int screenW, int screenH) const {
    const float w = height_ * static_cast<float>(std::max(1, screenW)) / static_cast<float>(std::max(1, screenH));
    return {center_.x - w * 0.5f, center_.y - height_ * 0.5f, w, height_};
}

WorldView ArenaCamera::view(int screenW, int screenH) const {
    WorldView v;
    const Rect s = shown(screenW, screenH);
    v.scale = static_cast<float>(std::max(1, screenH)) / std::max(1.0f, height_);
    v.topLeft = {s.x, s.y};
    v.screenW = screenW;
    v.screenH = screenH;
    return v;
}

}  // namespace cosmic::raid
