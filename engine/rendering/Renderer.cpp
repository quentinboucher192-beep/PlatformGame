#include "engine/rendering/Renderer.h"

#include <cmath>

#include "engine/core/Log.h"

namespace cosmic {

namespace {
SDL_FColor fcolor(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}; }
SDL_FRect frect(const Rect& r) { return {r.x, r.y, r.w, r.h}; }
SDL_Vertex vert(Vec2 p, Color c, Vec2 uv = {}) { return {{p.x, p.y}, fcolor(c), {uv.x, uv.y}}; }
}  // namespace

SDL_BlendMode Renderer::toSdl(BlendMode b) {
    switch (b) {
        case BlendMode::Alpha: return SDL_BLENDMODE_BLEND;
        case BlendMode::Additive: return SDL_BLENDMODE_ADD;
        case BlendMode::Multiply: return SDL_BLENDMODE_MOD;
        case BlendMode::None: return SDL_BLENDMODE_NONE;
    }
    return SDL_BLENDMODE_BLEND;
}

bool Renderer::init(SDL_Window* window, bool vsync) {
    renderer_ = SDL_CreateRenderer(window, nullptr);
    if (!renderer_) {
        Log::error("SDL_CreateRenderer failed: {}", SDL_GetError());
        return false;
    }
    Log::info("Renderer: {}", SDL_GetRendererName(renderer_));
    setVSync(vsync);
    refreshOutputSize();

    // Procedural smooth radial glow used for lights and bloom-like effects.
    const int gs = 128;
    SDL_Surface* s = SDL_CreateSurface(gs, gs, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        auto* px = static_cast<uint8_t*>(s->pixels);
        for (int y = 0; y < gs; ++y) {
            for (int x = 0; x < gs; ++x) {
                float dx = (x + 0.5f) / gs * 2.0f - 1.0f;
                float dy = (y + 0.5f) / gs * 2.0f - 1.0f;
                float d = std::sqrt(dx * dx + dy * dy);
                float a = std::clamp(1.0f - d, 0.0f, 1.0f);
                a = a * a * (3.0f - 2.0f * a);  // smoothstep falloff
                uint8_t* p = px + y * s->pitch + x * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<uint8_t>(a * 255.0f);
            }
        }
        glow_ = textureFromSurface(s, "<glow>", false);
        SDL_DestroySurface(s);
    }
    return true;
}

void Renderer::shutdown() {
    glow_.reset();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
}

void Renderer::refreshOutputSize() {
    int w = 1, h = 1;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    width_ = std::max(1, w);
    height_ = std::max(1, h);
}

void Renderer::setVSync(bool on) {
    if (!SDL_SetRenderVSync(renderer_, on ? 1 : 0)) Log::warn("VSync change failed: {}", SDL_GetError());
}

void Renderer::beginFrame(Color clear) {
    refreshOutputSize();
    SDL_SetRenderDrawColor(renderer_, clear.r, clear.g, clear.b, 255);
    SDL_RenderClear(renderer_);
    drawCalls_ = 0;
}

void Renderer::present() {
    lastDrawCalls_ = drawCalls_;
    SDL_RenderPresent(renderer_);
}

TexturePtr Renderer::createTarget(int w, int h, bool linearFilter) {
    SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) {
        Log::error("Render target {}x{} creation failed: {}", w, h, SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(t, linearFilter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_PIXELART);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return std::make_shared<Texture>(t, w, h, "<target>");
}

TexturePtr Renderer::textureFromSurface(SDL_Surface* surface, const std::string& source, bool pixelArt) {
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, surface);
    if (!t) {
        Log::error("Texture creation failed for {}: {}", source, SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(t, pixelArt ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return std::make_shared<Texture>(t, surface->w, surface->h, source);
}

void Renderer::setTarget(const TexturePtr& target) {
    SDL_SetRenderTarget(renderer_, target ? target->sdl() : nullptr);
}

Rect Renderer::snap(const Rect& r) const {
    float x0 = std::round(r.x), y0 = std::round(r.y);
    float x1 = std::round(r.x + r.w), y1 = std::round(r.y + r.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

void Renderer::drawTexture(const Texture& tex, const Rect* src, const Rect& dst, Color tint, BlendMode blend,
                           bool flipX, bool flipY, float angleDeg, const Vec2* pivot) {
    SDL_Texture* t = tex.sdl();
    if (!t) return;
    SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(t, tint.a);
    SDL_SetTextureBlendMode(t, toSdl(blend));
    SDL_FRect s;
    if (src) s = frect(*src);
    SDL_FRect d = frect(dst);
    int flip = SDL_FLIP_NONE;
    if (flipX) flip |= SDL_FLIP_HORIZONTAL;
    if (flipY) flip |= SDL_FLIP_VERTICAL;
    if (angleDeg != 0.0f || flip != SDL_FLIP_NONE) {
        SDL_FPoint c{dst.w * 0.5f, dst.h * 0.5f};
        if (pivot) c = {pivot->x, pivot->y};
        SDL_RenderTextureRotated(renderer_, t, src ? &s : nullptr, &d, angleDeg, &c, static_cast<SDL_FlipMode>(flip));
    } else {
        SDL_RenderTexture(renderer_, t, src ? &s : nullptr, &d);
    }
    ++drawCalls_;
}

void Renderer::drawNineSlice(const Texture& tex, const Rect& src, float border, const Rect& dst, float borderScale,
                             Color tint) {
    SDL_Texture* t = tex.sdl();
    if (!t) return;
    SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(t, tint.a);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_FRect s = frect(src);
    SDL_FRect d = frect(dst);
    SDL_RenderTexture9Grid(renderer_, t, &s, border, border, border, border, borderScale, &d);
    ++drawCalls_;
}

void Renderer::fillRect(const Rect& r, Color c, BlendMode blend) {
    SDL_SetRenderDrawBlendMode(renderer_, toSdl(blend));
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_FRect f = frect(r);
    SDL_RenderFillRect(renderer_, &f);
    ++drawCalls_;
}

void Renderer::drawRect(const Rect& r, Color c, float t) {
    if (t <= 1.0f) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_FRect f = frect(r);
        SDL_RenderRect(renderer_, &f);
        ++drawCalls_;
        return;
    }
    fillRect({r.x, r.y, r.w, t}, c);
    fillRect({r.x, r.bottom() - t, r.w, t}, c);
    fillRect({r.x, r.y + t, t, r.h - 2 * t}, c);
    fillRect({r.right() - t, r.y + t, t, r.h - 2 * t}, c);
}

void Renderer::geometry(const std::vector<SDL_Vertex>& verts, const std::vector<int>& idx, BlendMode blend,
                        SDL_Texture* tex) {
    if (verts.empty()) return;
    SDL_SetRenderDrawBlendMode(renderer_, toSdl(blend));
    if (tex) SDL_SetTextureBlendMode(tex, toSdl(blend));
    SDL_RenderGeometry(renderer_, tex, verts.data(), static_cast<int>(verts.size()),
                       idx.empty() ? nullptr : idx.data(), static_cast<int>(idx.size()));
    ++drawCalls_;
}

void Renderer::drawLine(Vec2 a, Vec2 b, Color c, float thickness, BlendMode blend) {
    if (thickness <= 1.0f) {
        SDL_SetRenderDrawBlendMode(renderer_, toSdl(blend));
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        ++drawCalls_;
        return;
    }
    Vec2 d = (b - a).normalized();
    Vec2 n{-d.y * thickness * 0.5f, d.x * thickness * 0.5f};
    scratchVerts_ = {vert(a + n, c), vert(b + n, c), vert(b - n, c), vert(a - n, c)};
    scratchIdx_ = {0, 1, 2, 0, 2, 3};
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::fillCircle(Vec2 center, float radius, Color c, BlendMode blend) {
    int seg = std::clamp(static_cast<int>(radius * 0.8f), 12, 64);
    scratchVerts_.clear();
    scratchIdx_.clear();
    scratchVerts_.push_back(vert(center, c));
    for (int i = 0; i <= seg; ++i) {
        float a = 2.0f * kPi * i / seg;
        scratchVerts_.push_back(vert(center + Vec2{std::cos(a), std::sin(a)} * radius, c));
        if (i > 0) {
            scratchIdx_.push_back(0);
            scratchIdx_.push_back(i);
            scratchIdx_.push_back(i + 1);
        }
    }
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::drawRing(Vec2 center, float radius, float thickness, Color c, BlendMode blend) {
    int seg = std::clamp(static_cast<int>(radius * 0.8f), 16, 96);
    scratchVerts_.clear();
    scratchIdx_.clear();
    float r0 = std::max(0.0f, radius - thickness * 0.5f), r1 = radius + thickness * 0.5f;
    for (int i = 0; i <= seg; ++i) {
        float a = 2.0f * kPi * i / seg;
        Vec2 d{std::cos(a), std::sin(a)};
        scratchVerts_.push_back(vert(center + d * r0, c));
        scratchVerts_.push_back(vert(center + d * r1, c));
        if (i > 0) {
            int b = (i - 1) * 2;
            scratchIdx_.insert(scratchIdx_.end(), {b, b + 1, b + 3, b, b + 3, b + 2});
        }
    }
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::drawArc(Vec2 center, float radius, float thickness, float startRad, float endRad, Color c) {
    int seg = std::clamp(static_cast<int>(radius * std::abs(endRad - startRad) * 0.3f), 6, 96);
    scratchVerts_.clear();
    scratchIdx_.clear();
    float r0 = std::max(0.0f, radius - thickness * 0.5f), r1 = radius + thickness * 0.5f;
    for (int i = 0; i <= seg; ++i) {
        float a = startRad + (endRad - startRad) * i / seg;
        Vec2 d{std::cos(a), std::sin(a)};
        scratchVerts_.push_back(vert(center + d * r0, c));
        scratchVerts_.push_back(vert(center + d * r1, c));
        if (i > 0) {
            int b = (i - 1) * 2;
            scratchIdx_.insert(scratchIdx_.end(), {b, b + 1, b + 3, b, b + 3, b + 2});
        }
    }
    geometry(scratchVerts_, scratchIdx_, BlendMode::Alpha);
}

void Renderer::drawCircle(Vec2 center, float radius, Color c, float thickness) {
    drawRing(center, radius, std::max(1.0f, thickness), c);
}

void Renderer::fillGradientV(const Rect& r, Color top, Color bottom, BlendMode blend) {
    scratchVerts_ = {vert({r.x, r.y}, top), vert({r.right(), r.y}, top), vert({r.right(), r.bottom()}, bottom),
                     vert({r.x, r.bottom()}, bottom)};
    scratchIdx_ = {0, 1, 2, 0, 2, 3};
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::fillGradientH(const Rect& r, Color left, Color right, BlendMode blend) {
    scratchVerts_ = {vert({r.x, r.y}, left), vert({r.right(), r.y}, right), vert({r.right(), r.bottom()}, right),
                     vert({r.x, r.bottom()}, left)};
    scratchIdx_ = {0, 1, 2, 0, 2, 3};
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::fillRoundedRectGradient(const Rect& r, float radius, Color top, Color bottom) {
    radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    if (radius <= 0.5f) {
        fillGradientV(r, top, bottom);
        return;
    }
    // Build the outline (corner arcs) as a convex fan with per-vertex colour.
    scratchVerts_.clear();
    scratchIdx_.clear();
    auto colAt = [&](float y) { return Color::lerp(top, bottom, std::clamp((y - r.y) / r.h, 0.0f, 1.0f)); };
    Vec2 c = r.center();
    scratchVerts_.push_back(vert(c, colAt(c.y)));
    const int seg = 6;
    const Vec2 centers[4] = {{r.right() - radius, r.y + radius},
                             {r.right() - radius, r.bottom() - radius},
                             {r.x + radius, r.bottom() - radius},
                             {r.x + radius, r.y + radius}};
    const float starts[4] = {-kPi / 2, 0, kPi / 2, kPi};
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i <= seg; ++i) {
            float a = starts[k] + (kPi / 2) * i / seg;
            Vec2 p = centers[k] + Vec2{std::cos(a), std::sin(a)} * radius;
            scratchVerts_.push_back(vert(p, colAt(p.y)));
        }
    }
    int n = static_cast<int>(scratchVerts_.size()) - 1;
    for (int i = 1; i <= n; ++i) {
        scratchIdx_.push_back(0);
        scratchIdx_.push_back(i);
        scratchIdx_.push_back(i == n ? 1 : i + 1);
    }
    geometry(scratchVerts_, scratchIdx_, BlendMode::Alpha);
}

void Renderer::fillRoundedRect(const Rect& r, float radius, Color c) { fillRoundedRectGradient(r, radius, c, c); }

void Renderer::drawRoundedRect(const Rect& r, float radius, Color c, float thickness) {
    radius = std::max(thickness, std::min({radius, r.w * 0.5f, r.h * 0.5f}));
    const int seg = 6;
    const Vec2 centers[4] = {{r.right() - radius, r.y + radius},
                             {r.right() - radius, r.bottom() - radius},
                             {r.x + radius, r.bottom() - radius},
                             {r.x + radius, r.y + radius}};
    const float starts[4] = {-kPi / 2, 0, kPi / 2, kPi};
    scratchVerts_.clear();
    scratchIdx_.clear();
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= seg; ++i) {
            float a = starts[k] + (kPi / 2) * i / seg;
            Vec2 dir{std::cos(a), std::sin(a)};
            scratchVerts_.push_back(vert(centers[k] + dir * radius, c));
            scratchVerts_.push_back(vert(centers[k] + dir * (radius - thickness), c));
        }
    int n = static_cast<int>(scratchVerts_.size() / 2);
    for (int i = 0; i < n; ++i) {
        int a = i * 2, b = ((i + 1) % n) * 2;
        scratchIdx_.insert(scratchIdx_.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
    geometry(scratchVerts_, scratchIdx_, BlendMode::Alpha);
}

void Renderer::fillConvexPolygon(const std::vector<Vec2>& pts, Color c, BlendMode blend) {
    if (pts.size() < 3) return;
    scratchVerts_.clear();
    scratchIdx_.clear();
    for (auto p : pts) scratchVerts_.push_back(vert(p, c));
    for (int i = 1; i + 1 < static_cast<int>(pts.size()); ++i) scratchIdx_.insert(scratchIdx_.end(), {0, i, i + 1});
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color col, BlendMode blend) {
    scratchVerts_ = {vert(a, col), vert(b, col), vert(c, col)};
    scratchIdx_.clear();
    geometry(scratchVerts_, scratchIdx_, blend);
}

void Renderer::drawGlow(Vec2 center, float radius, Color c, BlendMode blend) {
    if (!glow_ || radius <= 0.5f) return;
    drawTexture(*glow_, nullptr, {center.x - radius, center.y - radius, radius * 2, radius * 2}, c, blend);
}

void Renderer::pushClip(const Rect& r) {
    Rect clip = r;
    if (!clipStack_.empty()) clip = clipStack_.back().intersection(r);
    clipStack_.push_back(clip);
    SDL_Rect ir{static_cast<int>(std::floor(clip.x)), static_cast<int>(std::floor(clip.y)),
                std::max(0, static_cast<int>(std::ceil(clip.w))), std::max(0, static_cast<int>(std::ceil(clip.h)))};
    SDL_SetRenderClipRect(renderer_, &ir);
}

void Renderer::popClip() {
    if (!clipStack_.empty()) clipStack_.pop_back();
    if (clipStack_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        const Rect& c = clipStack_.back();
        SDL_Rect ir{static_cast<int>(c.x), static_cast<int>(c.y), static_cast<int>(c.w), static_cast<int>(c.h)};
        SDL_SetRenderClipRect(renderer_, &ir);
    }
}

// ------------------------------------------------------------------ world space

void Renderer::drawWorldTexture(const Texture& tex, const Rect* src, const Rect& worldDst, Color tint,
                                BlendMode blend, bool flipX, float angleDeg, const Vec2* worldPivotOffset,
                                bool flipY) {
    Rect d = snap(view_.toScreen(worldDst));
    if (d.right() < 0 || d.bottom() < 0 || d.x > width_ || d.y > height_) {
        if (angleDeg == 0.0f) return;
    }
    if (worldPivotOffset) {
        Vec2 p = *worldPivotOffset * view_.scale;
        drawTexture(tex, src, d, tint, blend, flipX, flipY, angleDeg, &p);
    } else {
        drawTexture(tex, src, d, tint, blend, flipX, flipY, angleDeg, nullptr);
    }
}

void Renderer::fillWorldRect(const Rect& r, Color c, BlendMode blend) { fillRect(snap(view_.toScreen(r)), c, blend); }

void Renderer::drawWorldRect(const Rect& r, Color c, float thicknessPx) {
    drawRect(snap(view_.toScreen(r)), c, thicknessPx);
}

void Renderer::drawWorldLine(Vec2 a, Vec2 b, Color c, float thicknessPx, BlendMode blend) {
    drawLine(view_.toScreen(a), view_.toScreen(b), c, thicknessPx, blend);
}

void Renderer::drawWorldGlow(Vec2 center, float radius, Color c, BlendMode blend) {
    drawGlow(view_.toScreen(center), radius * view_.scale, c, blend);
}

void Renderer::drawWorldCircle(Vec2 center, float radius, Color c, float thicknessPx) {
    drawRing(view_.toScreen(center), radius * view_.scale, thicknessPx, c);
}

void Renderer::fillWorldCircle(Vec2 center, float radius, Color c, BlendMode blend) {
    fillCircle(view_.toScreen(center), radius * view_.scale, c, blend);
}

}  // namespace cosmic
