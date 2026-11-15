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

/// 0..255 from a 0..1 opacity (clamped).
uint8_t alpha(float f) { return static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f); }

/// A bright colour of the rainbow (`h` in turns, 0..1).
Color rainbow(float h) {
    h = h - std::floor(h);
    float x = h * 6.0f;
    auto ch = [&](float off) { return static_cast<uint8_t>(110 + 145 * std::clamp(std::abs(std::fmod(x + off, 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f)); };
    return Color{ch(0.0f), ch(4.0f), ch(2.0f), 255};
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

/// A filled five-pointed star (screen pixels).
void fillStar(Renderer& r, Vec2 c, float radius, Color col) {
    Vec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -kFxPi * 0.5f + static_cast<float>(i) * kFxPi / 5.0f;
        float rr = i % 2 == 0 ? radius : radius * 0.45f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) r.fillTriangle(c, pts[i], pts[(i + 1) % 10], col);
}

}  // namespace

void drawAvatarFrame(Renderer& r, Vec2 center, float radius, const std::string& look, float scale, float time) {
    auto ring = [&](float rad, float th, Color c) { r.drawRing(center, rad, th * scale, c); };
    // A polished edge: a lighter arc on the upper left of the main ring.
    auto shine = [&](float rad, Color c) { r.drawArc(center, rad, 1.6f * scale, kFxPi * 1.05f, kFxPi * 1.45f, c); };
    const float main = radius + 2 * scale;
    if (look == "bronze") {
        ring(main, 5, Color{205, 127, 50, 255});
        shine(main, Color{240, 186, 130, 220});
        ring(radius + 6 * scale, 2, Color{59, 36, 16, 255});
    } else if (look == "silver") {
        r.drawGlow(center, radius + 14 * scale, Color{200, 210, 225, 90});
        ring(main, 5, Color{200, 210, 225, 255});
        shine(main, Color{255, 255, 255, 230});
        ring(radius + 6 * scale, 2, Color{43, 50, 64, 255});
    } else if (look == "gold") {
        // The halo of the gold frame.
        r.drawGlow(center, radius + 22 * scale, Color{255, 214, 90, 140});
        ring(main, 5, Color{255, 214, 90, 255});
        shine(main, Color{255, 246, 200, 235});
        ring(radius + 6 * scale, 2, Color{74, 58, 16, 255});
    } else if (look == "platinum") {
        r.drawGlow(center, radius + 24 * scale, Color{170, 240, 255, 140});
        ring(main, 5, Color{170, 240, 255, 255});
        // Iridescence: arcs of pale colours turning slowly over the ring.
        const Color sheen[4] = {{255, 190, 240, 200}, {170, 240, 255, 200}, {200, 185, 255, 200}, {190, 255, 220, 200}};
        const int arcs = 12;
        float step = 2.0f * kFxPi / arcs, turn = time * 0.6f;
        for (int i = 0; i < arcs; ++i)
            r.drawArc(center, main, 2.4f * scale, turn + i * step, turn + (i + 1) * step, sheen[i % 4]);
        ring(radius + 6 * scale, 2, Color{27, 59, 70, 255});
    } else if (look == "constellation") {
        r.drawGlow(center, radius + 30 * scale, Color{180, 140, 255, 150});
        ring(main, 5, Color{255, 255, 255, 255});
        ring(radius + 7 * scale, 3, Color{57, 240, 216, 255});
        ring(radius + 11 * scale, 3, Color{180, 140, 255, 255});
        // Small stars around, slowly turning and twinkling.
        const int stars = 6;
        for (int i = 0; i < stars; ++i) {
            float a = -kFxPi * 0.5f + time * 0.15f + i * 2.0f * kFxPi / stars;
            Vec2 p{center.x + std::cos(a) * (radius + 10 * scale), center.y + std::sin(a) * (radius + 10 * scale)};
            float tw = 0.85f + 0.15f * std::sin(time * 3.0f + i * 1.7f);
            r.drawGlow(p, 10 * scale, Color{255, 255, 255, static_cast<uint8_t>(170 * tw)});
            fillStar(r, p, 5.2f * scale, Color{20, 16, 48, 255});  // a dark edge: visible on the rings
            fillStar(r, p, 4.4f * scale * tw, i % 2 == 0 ? Color{255, 255, 255, 255} : Color{255, 214, 90, 255});
        }
    } else if (look == "emerald") {
        r.drawGlow(center, radius + 16 * scale, Color{80, 230, 140, 110});
        ring(main, 5, Color{46, 190, 110, 255});
        shine(main, Color{170, 255, 200, 230});
        ring(radius + 6 * scale, 2, Color{12, 52, 30, 255});
        // Eight cut gems set in the ring, a light and a dark face each.
        for (int i = 0; i < 8; ++i) {
            float a = kFxPi / 8.0f + static_cast<float>(i) * kFxPi / 4.0f;
            Vec2 d{std::cos(a), std::sin(a)}, t{-d.y, d.x};
            Vec2 p = center + d * main;
            Vec2 out = p + d * (4.2f * scale), in = p - d * (4.2f * scale), l = p + t * (2.6f * scale), rr = p - t * (2.6f * scale);
            r.fillTriangle(out, l, rr, Color{170, 255, 205, 255});
            r.fillTriangle(in, l, rr, Color{30, 150, 85, 255});
        }
    } else if (look == "orbit") {
        r.drawGlow(center, radius + 16 * scale, Color{110, 214, 255, 100});
        ring(main, 4, Color{190, 230, 255, 255});
        shine(main, Color{255, 255, 255, 230});
        // The orbit, and two moons going round it.
        const float orbit = radius + 11 * scale;
        ring(orbit, 1.2f, Color{110, 214, 255, 170});
        float a = -kFxPi * 0.5f + time * 0.9f;
        Vec2 moon{center.x + std::cos(a) * orbit, center.y + std::sin(a) * orbit};
        Vec2 moon2{center.x - std::cos(a) * orbit, center.y - std::sin(a) * orbit};
        r.drawGlow(moon, 9 * scale, Color{180, 230, 255, 190});
        r.fillCircle(moon, 3.4f * scale, Color{235, 248, 255, 255});
        r.drawGlow(moon2, 7 * scale, Color{255, 214, 90, 160});
        r.fillCircle(moon2, 2.4f * scale, Color{255, 214, 90, 255});
    } else if (look == "nebula") {
        r.drawGlow(center, radius + 26 * scale, Color{255, 110, 210, 110});
        r.drawGlow(center, radius + 18 * scale, Color{140, 110, 255, 120});
        ring(main, 5, Color{200, 130, 255, 255});
        ring(radius + 6 * scale, 1.5f, Color{40, 16, 60, 255});
        // Three clouds of colour turning around, and sparkles turning the other way.
        const Color clouds[3] = {{255, 140, 220, 235}, {120, 220, 255, 235}, {180, 140, 255, 235}};
        for (int i = 0; i < 3; ++i) {
            float a = time * 0.8f + static_cast<float>(i) * 2.0f * kFxPi / 3.0f;
            r.drawArc(center, radius + 9 * scale, 3 * scale, a, a + 1.3f, clouds[i]);
        }
        for (int i = 0; i < 5; ++i) {
            float a = -time * 0.5f + static_cast<float>(i) * 2.0f * kFxPi / 5.0f;
            Vec2 p{center.x + std::cos(a) * (radius + 13 * scale), center.y + std::sin(a) * (radius + 13 * scale)};
            float tw = 0.6f + 0.4f * std::sin(time * 4.0f + static_cast<float>(i) * 2.1f);
            r.drawGlow(p, 5 * scale, Color{255, 255, 255, alpha(0.6f * tw)});
            r.fillCircle(p, 1.2f * scale, Color{255, 255, 255, 255});
        }
    } else if (look == "laurels") {
        r.drawGlow(center, radius + 18 * scale, Color{255, 214, 90, 110});
        ring(main, 4, Color{255, 214, 90, 255});
        shine(main, Color{255, 246, 200, 235});
        // Two branches climbing each side from the bottom, open at the top: a stem, and leaves
        // alternately outside and inside it, pointing up the branch.
        const float stem = radius + 8.5f * scale;
        for (int side = -1; side <= 1; side += 2) {
            float a0 = kFxPi * 0.5f + static_cast<float>(side) * 0.22f, a1 = kFxPi * 0.5f + static_cast<float>(side) * 2.55f;
            r.drawArc(center, stem, 1.4f * scale, std::min(a0, a1), std::max(a0, a1), Color{110, 150, 50, 255});
            for (int i = 0; i < 9; ++i) {
                float a = kFxPi * 0.5f + static_cast<float>(side) * (0.34f + static_cast<float>(i) * 0.26f);
                Vec2 d{std::cos(a), std::sin(a)};
                Vec2 up = Vec2{-d.y, d.x} * static_cast<float>(side);  // along the branch, towards the top
                float out = i % 2 == 0 ? 1.0f : -1.0f;                // outside, then inside the stem
                Vec2 axis = (up + d * (0.55f * out)).normalized();
                Vec2 perp{-axis.y, axis.x};
                Vec2 base = center + d * (stem + out * 1.2f * scale);
                Vec2 tip = base + axis * (5.8f * scale), back = base - axis * (1.6f * scale);
                Vec2 l = base + perp * (2.4f * scale), rr = base - perp * (2.4f * scale);
                r.fillTriangle(tip, l, rr, Color{176, 220, 96, 255});
                r.fillTriangle(back, l, rr, Color{118, 168, 58, 255});
            }
        }
        r.fillCircle({center.x, center.y + stem}, 2.4f * scale, Color{255, 214, 90, 255});
    } else if (look == "crown") {
        // « Couronne de la semaine » (2.34): the gold ring, seven studs on its lower half (the
        // days of the week) and a crown on top: a band, five points, three gems, pearls.
        const Color gold{255, 204, 84, 255}, dark{70, 46, 8, 255};
        r.drawGlow(center, radius + 18 * scale, Color{255, 204, 84, 120});
        ring(main, 5, gold);
        shine(main, Color{255, 246, 200, 235});
        ring(radius + 6 * scale, 2, Color{74, 52, 12, 255});
        for (int i = 0; i < 7; ++i) {
            float a = kFxPi * (0.5f + (static_cast<float>(i) - 3.0f) * 0.17f);
            Vec2 p{center.x + std::cos(a) * main, center.y + std::sin(a) * main};
            r.fillCircle(p, 2.2f * scale, dark);
            r.fillCircle(p, 1.5f * scale, Color{255, 240, 190, 255});
        }
        const float top = center.y - main;  // the top of the ring
        auto pt = [&](float x, float y) { return Vec2{center.x + x * scale, top + y * scale}; };
        // Points (x of the tip, its height over the band), the middle one the tallest.
        // Within kAvatarFrameMargin: the highest pearl ends 14.3 units above the avatar.
        const float px[5] = {-10.5f, -5.25f, 0.0f, 5.25f, 10.5f}, ph[5] = {4.0f, 5.5f, 6.5f, 5.5f, 4.0f};
        const float bandTop = -4.5f, bandBottom = 1.0f;
        for (int pass = 0; pass < 2; ++pass) {
            // A dark edge first (visible on any background), then the gold.
            const float e = pass == 0 ? 1.0f : 0.0f;
            const Color c = pass == 0 ? dark : gold;
            for (int i = 0; i < 5; ++i)
                r.fillTriangle(pt(px[i] - 3.2f - e, bandTop + 0.5f), pt(px[i] + 3.2f + e, bandTop + 0.5f), pt(px[i], bandTop - ph[i] - e), c);
            r.fillRect({center.x - (12.5f + e) * scale, top + (bandTop - e) * scale, (25.0f + 2 * e) * scale, (bandBottom - bandTop + 2 * e) * scale}, c);
        }
        r.fillRect({center.x - 12.5f * scale, top + bandTop * scale, 25.0f * scale, 1.2f * scale}, Color{255, 246, 200, 255});
        const Color gems[3] = {{230, 60, 90, 255}, {90, 175, 255, 255}, {80, 220, 140, 255}};
        for (int i = 0; i < 3; ++i) r.fillCircle(pt(-7.0f + 7.0f * static_cast<float>(i), (bandTop + bandBottom) * 0.5f), 1.7f * scale, gems[i]);
        for (int i = 0; i < 5; ++i) r.fillCircle(pt(px[i], bandTop - ph[i]), 1.3f * scale, Color{255, 252, 240, 255});
    } else {
        ring(radius + 1.5f * scale, 3, Color{57, 240, 216, 255});
    }
}

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

namespace {

/// One sample of a trail: `k` 1 = fresh, 0 = gone; `prev`: the older sample before it (the
/// ribbons join them); `fade` dims everything (a locked item's thumbnail).
void drawTrailPoint(const Canvas& cv, const std::string& look, Vec2 p, float k, float age, uint32_t seed, const Vec2* prev, float fade) {
    const float f = k * fade;
    if (look == "stardust") {
        Vec2 j{(noise(seed) - 0.5f) * 10.0f, (noise(seed * 7u + 3u) - 0.5f) * 12.0f};
        Vec2 q = cv.off(p, j);
        cv.glow(q, 5.0f * k, Color{57, 240, 216, alpha(0.59f * f)});
        if (seed % 2 == 0) cv.dot(q, 1.3f * k + 0.4f, Color{143, 255, 240, alpha(0.9f * f)}, BlendMode::Additive);
    } else if (look == "comet") {
        Color c = mix(Color{110, 214, 255, 255}, Color{245, 250, 255, 255}, k);
        cv.glow(p, 9.0f * k + 2.0f, c.withAlpha(alpha(0.67f * f)));
        cv.dot(p, 2.6f * k, Color{255, 255, 255, alpha(0.78f * f)}, BlendMode::Additive);
    } else if (look == "aurora") {
        float wave = std::sin(age * 14.0f + static_cast<float>(seed) * 0.6f) * 5.0f;
        Vec2 q = cv.off(p, {0, wave});
        Color c = mix(Color{184, 140, 255, 255}, Color{120, 255, 156, 255}, k);
        cv.glow(q, 8.0f * k + 1.5f, c.withAlpha(alpha(0.63f * f)));
    } else if (look == "eclipse") {
        cv.dot(p, 6.0f * k + 1.0f, Color{18, 10, 4, alpha(0.67f * f)});
        cv.ring(p, 6.0f * k + 1.5f, 1.2f, Color{255, 214, 90, alpha(0.75f * f)});
    } else if (look == "flame") {
        // Embers rising from the trail: yellow when fresh, then red, then gone.
        Vec2 j{(noise(seed) - 0.5f) * 8.0f + std::sin(age * 18.0f + static_cast<float>(seed)) * 2.5f,
               -age * 50.0f - noise(seed * 5u + 1u) * 4.0f};
        Vec2 q = cv.off(p, j);
        Color c = mix(Color{235, 70, 30, 255}, Color{255, 226, 120, 255}, k);
        cv.glow(q, 8.0f * k + 2.0f, c.withAlpha(alpha(0.8f * f)));
        cv.dot(q, 2.2f * k + 0.6f, Color{255, 245, 200, alpha(0.9f * f)}, BlendMode::Additive);
    } else if (look == "prism") {
        // A ribbon whose colour runs through the rainbow along the trail, and keeps turning.
        Color c = rainbow(static_cast<float>(seed) * 0.06f + age * 0.8f);
        if (prev) cv.line(*prev, p, c.withAlpha(alpha(0.9f * f)), 4.5f * k + 1.2f);
        cv.glow(p, 5.0f * k + 1.5f, c.withAlpha(alpha(0.45f * f)));
    } else if (look == "runes") {
        // One turning rune of Aethra every three samples: a violet diamond, a gold heart.
        if (seed % 3 != 0) return;
        float turn = age * 5.0f + static_cast<float>(seed) * 0.9f;
        float size = 5.5f * (0.6f + 0.4f * k);
        Vec2 q = cv.off(p, {0, -3.0f * (1.0f - k)});
        Vec2 v[4];
        for (int i = 0; i < 4; ++i) {
            float a = turn + static_cast<float>(i) * kFxPi * 0.5f;
            v[i] = cv.off(q, {std::cos(a) * size, std::sin(a) * size});
        }
        cv.glow(q, 7.0f * k + 1.0f, Color{180, 140, 255, alpha(0.32f * f)});
        for (int i = 0; i < 4; ++i) cv.line(v[i], v[(i + 1) % 4], Color{205, 175, 255, alpha(0.9f * f)}, 1.3f);
        cv.dot(q, 1.2f, Color{255, 214, 90, alpha(0.95f * f)}, BlendMode::Additive);
    } else if (look == "month_comet") {
        // « Comète du mois » (2.34): a gold head fading into a long violet tail, a small star
        // now and then along it.
        Color c = mix(Color{150, 70, 255, 255}, Color{255, 214, 90, 255}, k * k);
        cv.glow(p, 10.0f * k + 2.5f, c.withAlpha(alpha(0.72f * f)));
        cv.dot(p, 2.8f * k + 0.5f, mix(Color{215, 165, 255, 255}, Color{255, 250, 225, 255}, k).withAlpha(alpha(0.85f * f)), BlendMode::Additive);
        if (seed % 4 == 0) {
            Vec2 j{(noise(seed) - 0.5f) * 14.0f, (noise(seed * 3u + 7u) - 0.5f) * 14.0f};
            star(cv, cv.off(p, j), 2.4f + 1.4f * k, Color{255, 236, 170, alpha(0.9f * f)});
        }
    }
}

}  // namespace

void TrailFx::draw(Renderer& r, const std::string& look, bool world, float scale) const {
    if (look.empty() || look == "none" || pts_.empty()) return;
    Canvas cv{r, world, {}, scale};
    size_t n = pts_.size();
    // The newest samples sit on Nova herself: the trail starts just behind her.
    for (size_t i = 0; i + 2 < n; ++i) {
        const Pt& pt = pts_[i];
        float k = 1.0f - pt.age / kTrailLife;  // 1 = fresh, 0 = gone
        if (k <= 0.0f) continue;
        // The sample before it, unless Nova jumped away in between (teleport, respawn).
        const Vec2* prev = i > 0 && (pt.p - pts_[i - 1].p).lengthSq() < 48.0f * 48.0f ? &pts_[i - 1].p : nullptr;
        drawTrailPoint(cv, look, pt.p, k, pt.age, pt.seed, prev, 1.0f);
    }
}

void drawTrailSample(Renderer& r, const std::string& look, const Rect& box, float time, float fade) {
    if (look.empty() || look == "none") return;
    // Samples from the oldest (left) to the newest (right), on a gentle wave, sized to the box.
    const int n = 16;
    const float scale = box.h / 60.0f;
    Canvas cv{r, false, {}, scale};
    Vec2 prev;
    for (int i = 0; i < n; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(n - 1);
        float age = kTrailLife * (1.0f - u) * 0.95f;
        Vec2 p{box.x + box.w * (0.12f + 0.72f * u), box.center().y + box.h * 0.12f + std::sin(u * 5.0f + time * 1.5f) * box.h * 0.08f};
        drawTrailPoint(cv, look, p, 1.0f - age / kTrailLife, age, static_cast<uint32_t>(i) + 1u, i > 0 ? &prev : nullptr, fade);
        prev = p;
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
    } else if (look == "vortex") {
        // Three arms of sparks turning around Nova, opening wide, then fading.
        Vec2 o = cv.off(at, {0, -24});
        float open = 1.0f - std::exp(-2.2f * t);
        float in = std::clamp(t / 0.2f, 0.0f, 1.0f);
        if (t < 0.4f) cv.glow(o, 14.0f + 40.0f * (t / 0.4f), Color{184, 140, 255, alpha(0.8f * (1.0f - t / 0.4f))});
        for (int arm = 0; arm < 3; ++arm)
            for (int i = 0; i < 14; ++i) {
                float f = static_cast<float>(i) / 13.0f;
                float ang = static_cast<float>(arm) * 2.0f * kFxPi / 3.0f + f * 3.2f + t * 5.0f;
                float rad = (10.0f + f * 80.0f) * open;
                Vec2 q = cv.off(o, {std::cos(ang) * rad, std::sin(ang) * rad * 0.8f});
                float a = fade * in * (1.0f - f * 0.5f);
                cv.glow(q, 5.0f * (1.0f - f * 0.4f), mix(Color{57, 240, 216, 255}, Color{184, 140, 255, 255}, f).withAlpha(alpha(0.75f * a)));
                if (i % 2 == 0) cv.dot(q, 1.3f, Color{255, 255, 255, alpha(0.86f * a)}, BlendMode::Additive);
            }
    } else if (look == "borealis") {
        // Curtains of light rising above the portal and waving, green below, violet above.
        float in = std::clamp(t / 0.6f, 0.0f, 1.0f);
        for (int c = 0; c < 5; ++c) {
            float x0 = -100.0f + 50.0f * static_cast<float>(c);
            float h = 120.0f + 30.0f * noise(static_cast<uint32_t>(c) * 31u + 7u);
            for (int i = 0; i < 12; ++i) {
                float f = static_cast<float>(i) / 11.0f;
                float x = x0 + std::sin(f * 4.0f + t * 2.6f + static_cast<float>(c) * 1.3f) * 12.0f;
                float y = -20.0f - f * h * in;
                float a = fade * in * (1.0f - f * 0.6f);
                cv.glow(cv.off(at, {x, y}), 9.0f - f * 3.0f, mix(Color{120, 255, 156, 255}, Color{184, 140, 255, 255}, f).withAlpha(alpha(0.5f * a)));
            }
        }
    } else if (look == "corona") {
        // An eclipse above the portal: a dark disc ringed with light, its rays turning.
        Vec2 o = cv.off(at, {0, -70});
        float grow = 1.0f - std::exp(-3.0f * t);
        cv.glow(o, 60.0f * grow + 4.0f, Color{255, 190, 80, alpha(0.6f * fade)});
        for (int i = 0; i < 16; ++i) {
            float ang = static_cast<float>(i) * 2.0f * kFxPi / 16.0f + t * 0.6f;
            float len = (34.0f + 22.0f * noise(static_cast<uint32_t>(i) * 13u + 5u)) * grow;
            Vec2 d{std::cos(ang), std::sin(ang)};
            cv.line(cv.off(o, d * (21.0f * grow)), cv.off(o, d * (21.0f * grow + len)), Color{255, 200, 90, alpha(0.8f * fade)}, 2.0f);
        }
        cv.dot(o, 18.0f * grow, Color{20, 10, 24, alpha(0.95f * fade)});
        cv.ring(o, 19.0f * grow, 2.0f, Color{255, 240, 200, alpha(0.9f * fade)});
    } else if (look == "fire_streak") {
        // « Série de feu » (2.34): seven flames, one per day of the series, light up one after
        // the other on an arc above Nova, then flare together.
        Vec2 o = cv.off(at, {0, -34});
        float flare = t > 1.0f ? 1.0f + 0.3f * std::sin(std::min(1.0f, (t - 1.0f) / 0.6f) * kFxPi) : 1.0f;
        for (int i = 0; i < 7; ++i) {
            float on = t - 0.12f * static_cast<float>(i);
            if (on < 0.0f) continue;
            float grow = std::min(1.0f, on / 0.25f) * flare;
            float a = kFxPi * (1.12f + 0.76f * static_cast<float>(i) / 6.0f);  // from the left, over the top, to the right
            Vec2 base = cv.off(o, {std::cos(a) * 46.0f, std::sin(a) * 30.0f});
            float h = 24.0f * grow;
            float flick = std::sin(t * 17.0f + static_cast<float>(i) * 1.7f) * 2.0f;
            cv.glow(base, 16.0f * grow, Color{255, 110, 40, alpha(0.6f * fade)});
            // The flame: a red base, an orange body, a yellow tip, swaying; a white heart.
            for (int s = 0; s < 4; ++s) {
                float u = static_cast<float>(s) / 3.0f;
                Color c = mix(Color{255, 70, 30, 255}, Color{255, 232, 120, 255}, u);
                cv.glow(cv.off(base, {flick * u, -h * u}), (9.0f - 5.0f * u) * grow, c.withAlpha(alpha(0.9f * fade)));
            }
            cv.dot(cv.off(base, {flick * 0.1f, -h * 0.12f}), 5.0f * grow, Color{240, 85, 30, alpha(0.95f * fade)});
            cv.dot(cv.off(base, {flick * 0.35f, -h * 0.36f}), 3.8f * grow, Color{255, 160, 50, alpha(0.95f * fade)});
            cv.dot(cv.off(base, {flick * 0.65f, -h * 0.62f}), 2.6f * grow, Color{255, 220, 100, alpha(0.95f * fade)});
            cv.dot(cv.off(base, {flick, -h * 0.86f}), 1.4f * grow, Color{255, 245, 170, alpha(0.95f * fade)});
            cv.dot(cv.off(base, {0, -h * 0.2f}), 2.0f * grow, Color{255, 250, 225, alpha(0.95f * fade)}, BlendMode::Additive);
        }
    }
}

}  // namespace cosmic
