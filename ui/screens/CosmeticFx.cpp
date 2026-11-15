#include "ui/screens/CosmeticFx.h"

#include <algorithm>
#include <cmath>

#include "engine/rendering/Renderer.h"

namespace cosmic {

namespace {

constexpr float kTrailLife = 0.45f;
constexpr float kFxPi = 3.14159265f;

/// Small deterministic noise for the sparkles' spread (display only).
float noise(uint32_t s) {
    s ^= s >> 16;
    s *= 0x7feb352dU;
    s ^= s >> 15;
    s *= 0x846ca68bU;
    s ^= s >> 16;
    return static_cast<float>(s & 0xFFFF) / 65535.0f;
}

Color mix(Color a, Color b, float k) {
    k = std::clamp(k, 0.0f, 1.0f);
    auto m = [k](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * k); };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

/// Draws in world space or in screen pixels, with one set of calls.
struct Canvas {
    Renderer& r;
    bool world;
    Vec2 origin;  // screen mode: where `at` / the points are relative to (already in pixels)
    float scale;

    Vec2 map(Vec2 p) const { return world ? p : origin + (p - origin) * 1.0f; }
    float len(float v) const { return world ? v : v * scale; }
    void glow(Vec2 p, float radius, Color c) const {
        if (world) r.drawWorldGlow(p, radius, c, BlendMode::Additive);
        else r.drawGlow(p, radius * scale, c, BlendMode::Additive);
    }
    void dot(Vec2 p, float radius, Color c, BlendMode b = BlendMode::Alpha) const {
        if (world) r.fillWorldCircle(p, radius, c, b);
        else r.fillCircle(p, radius * scale, c, b);
    }
    void ring(Vec2 p, float radius, float thickness, Color c) const {
        if (world) r.drawWorldCircle(p, radius, c, thickness);
        else r.drawRing(p, radius * scale, thickness * scale, c, BlendMode::Additive);
    }
    void line(Vec2 a, Vec2 b, Color c, float thickness) const {
        if (world) r.drawWorldLine(a, b, c, thickness, BlendMode::Additive);
        else r.drawLine(a, b, c, thickness * scale, BlendMode::Additive);
    }
    /// An offset of `d` world pixels from `p` (scaled on screen).
    Vec2 off(Vec2 p, Vec2 d) const { return world ? p + d : p + d * scale; }
};

void star(const Canvas& cv, Vec2 p, float size, Color c) {
    cv.glow(p, size * 3.0f, c.withAlpha(static_cast<uint8_t>(c.a * 0.5f)));
    cv.line(cv.off(p, {-size, 0}), cv.off(p, {size, 0}), c, 1.5f);
    cv.line(cv.off(p, {0, -size}), cv.off(p, {0, size}), c, 1.5f);
    cv.dot(p, size * 0.45f, Color{255, 255, 255, c.a}, BlendMode::Additive);
}

}  // namespace

void TrailFx::update(Vec2 center, float dt, bool emit) {
    for (auto& p : pts_) p.age += dt;
    while (!pts_.empty() && pts_.front().age > kTrailLife) pts_.pop_front();
    acc_ += dt;
    if (!emit) {
        have_ = false;
        return;
    }
    // One sample per 1/60 s, and only when Nova actually moved.
    if (acc_ < 1.0f / 60.0f) return;
    acc_ = 0.0f;
    if (have_ && (center - last_).lengthSq() < 2.0f) return;
    last_ = center;
    have_ = true;
    pts_.push_back({center, 0.0f, ++counter_});
    while (pts_.size() > 40) pts_.pop_front();
}

void TrailFx::draw(Renderer& r, const std::string& look, bool world, float scale) const {
    if (look.empty() || look == "none" || pts_.empty()) return;
    Canvas cv{r, world, {}, scale};
    size_t n = pts_.size();
    // The newest samples sit on Nova herself: the trail starts just behind her.
    for (size_t i = 0; i + 2 < n; ++i) {
        const Pt& pt = pts_[i];
        float k = 1.0f - pt.age / kTrailLife;  // 1 = fresh, 0 = gone
        if (k <= 0.0f) continue;
        Vec2 p = pt.p;
        if (look == "stardust") {
            Vec2 j{(noise(pt.seed) - 0.5f) * 10.0f, (noise(pt.seed * 7u + 3u) - 0.5f) * 12.0f};
            Vec2 q = cv.off(p, j);
            cv.glow(q, 5.0f * k, Color{57, 240, 216, static_cast<uint8_t>(150 * k)});
            if (pt.seed % 2 == 0) cv.dot(q, 1.3f * k + 0.4f, Color{143, 255, 240, static_cast<uint8_t>(230 * k)}, BlendMode::Additive);
        } else if (look == "comet") {
            Color c = mix(Color{110, 214, 255, 255}, Color{245, 250, 255, 255}, k);
            cv.glow(p, 9.0f * k + 2.0f, c.withAlpha(static_cast<uint8_t>(170 * k)));
            cv.dot(p, 2.6f * k, Color{255, 255, 255, static_cast<uint8_t>(200 * k)}, BlendMode::Additive);
        } else if (look == "aurora") {
            float wave = std::sin(pt.age * 14.0f + static_cast<float>(pt.seed) * 0.6f) * 5.0f;
            Vec2 q = cv.off(p, {0, wave});
            Color c = mix(Color{184, 140, 255, 255}, Color{120, 255, 156, 255}, k);
            cv.glow(q, 8.0f * k + 1.5f, c.withAlpha(static_cast<uint8_t>(160 * k)));
        } else if (look == "eclipse") {
            cv.dot(p, 6.0f * k + 1.0f, Color{18, 10, 4, static_cast<uint8_t>(170 * k)});
            cv.ring(p, 6.0f * k + 1.5f, 1.2f, Color{255, 214, 90, static_cast<uint8_t>(190 * k)});
        }
    }
}

void drawVictoryFx(Renderer& r, const std::string& look, Vec2 at, float t, bool world, float scale) {
    if (t < 0.0f || t > kVictoryFxTime || look.empty() || look == "classic") return;
    Canvas cv{r, world, at, scale};
    float fade = std::clamp((kVictoryFxTime - t) / 0.6f, 0.0f, 1.0f);
    if (look == "fireworks") {
        const Vec2 offs[3] = {{-46, -72}, {38, -96}, {4, -54}};
        const Color cols[3] = {{255, 118, 150, 255}, {255, 214, 90, 255}, {110, 214, 255, 255}};
        for (int b = 0; b < 3; ++b) {
            float tb = t - 0.35f * static_cast<float>(b);
            if (tb < 0.0f || tb > 1.6f) continue;
            Vec2 c = cv.off(at, offs[b]);
            float rad = 64.0f * (1.0f - std::exp(-3.2f * tb));
            float a = std::clamp(1.0f - tb / 1.6f, 0.0f, 1.0f) * fade;
            if (tb < 0.18f) cv.glow(c, 26.0f, cols[b].withAlpha(static_cast<uint8_t>(220 * (1.0f - tb / 0.18f))));
            for (int i = 0; i < 18; ++i) {
                float ang = static_cast<float>(i) / 18.0f * 2.0f * kFxPi + static_cast<float>(b);
                Vec2 d{std::cos(ang) * rad, std::sin(ang) * rad + 22.0f * tb * tb};
                Vec2 p = cv.off(c, d);
                cv.glow(p, 4.5f, cols[b].withAlpha(static_cast<uint8_t>(200 * a)));
                cv.dot(p, 1.4f, Color{255, 255, 255, static_cast<uint8_t>(230 * a)}, BlendMode::Additive);
            }
        }
    } else if (look == "starfall") {
        for (int i = 0; i < 16; ++i) {
            float delay = 0.11f * static_cast<float>(i);
            float ts = t - delay;
            if (ts < 0.0f || ts > 1.5f) continue;
            float x = (noise(static_cast<uint32_t>(i) * 977u + 11u) - 0.5f) * 190.0f;
            float y = -150.0f + 150.0f * std::min(1.0f, ts / 1.1f);
            float a = std::clamp(1.0f - (ts - 1.1f) / 0.4f, 0.0f, 1.0f) * fade;
            star(cv, cv.off(at, {x, y - 10.0f}), 4.0f, Color{255, 226, 122, static_cast<uint8_t>(240 * a)});
        }
    } else if (look == "supernova") {
        float k = std::clamp(t / 1.8f, 0.0f, 1.0f);
        if (t < 0.5f) cv.glow(cv.off(at, {0, -16}), 30.0f + 120.0f * (t / 0.5f), Color{255, 255, 255, static_cast<uint8_t>(220 * (1.0f - t / 0.5f))});
        float ringR = 26.0f + 210.0f * (1.0f - std::exp(-2.6f * t));
        Color ringC = mix(Color{180, 140, 255, 255}, Color{57, 240, 216, 255}, k);
        cv.ring(cv.off(at, {0, -16}), ringR, 5.0f, ringC.withAlpha(static_cast<uint8_t>(230 * (1.0f - k) * fade)));
        cv.ring(cv.off(at, {0, -16}), ringR * 0.7f, 2.5f, Color{255, 255, 255, static_cast<uint8_t>(160 * (1.0f - k) * fade)});
    }
}

}  // namespace cosmic
