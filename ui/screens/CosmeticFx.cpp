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
    } else if (look == "scales") {
        // « Écailles d'obsidienne » (2.48, Pyrrhax): a ring of black glass plates whose seams glow
        // with a living fire, pulsing slowly; embers rise from the top.
        const float pulse = 0.75f + 0.25f * std::sin(time * 2.4f);
        r.drawGlow(center, radius + 18 * scale, Color{255, 110, 40, static_cast<uint8_t>(110 * pulse)});
        ring(main, 7, Color{30, 24, 36, 255});
        ring(main + 2.5f * scale, 1.5f, Color{120, 80, 140, 255});
        const int plates = 12;
        for (int i = 0; i < plates; ++i) {
            const float a = static_cast<float>(i) * 2.0f * kFxPi / plates + 0.08f;
            // A seam of fire between two plates, brighter on the lower half (the heat rises).
            const Vec2 d{std::cos(a), std::sin(a)};
            const float hot = 0.6f + 0.4f * std::max(0.0f, d.y);
            r.drawLine(center + d * (main - 3.2f * scale), center + d * (main + 3.2f * scale),
                       Color{255, static_cast<uint8_t>(120 + 80 * hot), 50, static_cast<uint8_t>(230 * pulse)}, 1.6f * scale, BlendMode::Additive);
            // The glassy light of each plate.
            r.drawArc(center, main + 1.0f * scale, 1.0f * scale, a + 0.12f, a + 0.28f, Color{200, 170, 230, 150});
        }
        for (int i = 0; i < 4; ++i) {
            float u = std::fmod(time * 0.5f + static_cast<float>(i) * 0.25f, 1.0f);
            float a = -kFxPi * 0.5f + (static_cast<float>(i) - 1.5f) * 0.35f;
            Vec2 p{center.x + std::cos(a) * (main + u * 14 * scale), center.y + std::sin(a) * (main + u * 14 * scale)};
            r.drawGlow(p, (4.0f - 2.0f * u) * scale, Color{255, 160, 60, static_cast<uint8_t>(200 * (1.0f - u))});
        }
    } else if (look == "runic") {
        // « Cercle runique » (2.49, Monolithe): a ring of speckled granite, eight runes lighting up
        // one after the other in amber.
        ring(main, 7, Color{96, 90, 86, 255});
        ring(main + 2.5f * scale, 1.2f, Color{150, 144, 138, 255});
        ring(main - 3.0f * scale, 1.0f, Color{50, 46, 44, 255});
        const int runes = 8;
        const float lit = std::fmod(time * 1.6f, static_cast<float>(runes));
        for (int i = 0; i < runes; ++i) {
            const float a = -kFxPi * 0.5f + static_cast<float>(i) * 2.0f * kFxPi / runes;
            const Vec2 d{std::cos(a), std::sin(a)}, t{-d.y, d.x};
            const Vec2 p = center + d * main;
            float glow = 0.25f;
            const float dist = std::fabs(lit - static_cast<float>(i));
            if (dist < 1.0f) glow = 1.0f - 0.75f * dist;
            const Color c{255, 178, 70, static_cast<uint8_t>(255 * glow)};
            if (glow > 0.5f) r.drawGlow(p, 8 * scale, Color{255, 170, 60, static_cast<uint8_t>(150 * glow)});
            // A rune: a stroke along the ring and two ticks.
            r.drawLine(p - t * (3.0f * scale), p + t * (3.0f * scale), c, 1.4f * scale, BlendMode::Additive);
            r.drawLine(p - t * (1.5f * scale), p - t * (1.5f * scale) + d * (2.4f * scale), c, 1.2f * scale, BlendMode::Additive);
            r.drawLine(p + t * (1.5f * scale), p + t * (1.5f * scale) - d * (2.4f * scale), c, 1.2f * scale, BlendMode::Additive);
        }
    } else if (look == "tentacles") {
        // « Étreinte du Kraken » (2.53): two tentacles coil around the avatar from below, deep
        // violet with a teal sheen, their suckers lighting up in cyan one after the other along
        // them. Within kAvatarFrameMargin: their outer edge stays 12 units beyond the avatar.
        r.drawGlow(center, radius + 16 * scale, Color{80, 200, 255, 70});
        ring(main, 2.5f, Color{24, 18, 52, 255});
        const int n = 26;
        const float wave = std::fmod(time * 0.7f, 1.0f);
        for (int side = -1; side <= 1; side += 2) {
            auto at = [&](int i, Vec2& d, float& th) {
                const float u = static_cast<float>(i) / static_cast<float>(n - 1);
                const float a = kFxPi * 0.5f + static_cast<float>(side) * (0.15f + u * 2.55f);
                d = Vec2{std::cos(a), std::sin(a)};
                th = (5.0f - 3.4f * u) * scale;
                const float wob = std::sin(time * 2.2f + u * 6.0f + static_cast<float>(side)) * 1.1f * u;
                return center + d * (main + (2.5f + wob) * scale);
            };
            Vec2 d;
            float th = 0.0f;
            for (int i = 0; i < n; ++i) {
                const Vec2 p = at(i, d, th);
                r.fillCircle(p, th + 1.2f * scale, Color{20, 12, 40, 255});
            }
            for (int i = 0; i < n; ++i) {
                const Vec2 p = at(i, d, th);
                r.fillCircle(p, th, Color{70, 64, 130, 255});
            }
            for (int i = 0; i < n; ++i) {
                const Vec2 p = at(i, d, th);
                r.fillCircle(p + d * (th * 0.4f), th * 0.38f, Color{95, 138, 168, 255});  // the sheen on its outer side
            }
            for (int i = 1; i < n - 2; i += 3) {
                const Vec2 p = at(i, d, th);
                const float u = static_cast<float>(i) / static_cast<float>(n - 1);
                const float lit = std::max(0.0f, 1.0f - std::fabs(u - wave) * 5.0f);
                const Vec2 s = p - d * (th * 0.5f);
                r.fillCircle(s, std::max(1.0f * scale, th * 0.3f), Color{157, 147, 194, 255});
                r.drawGlow(s, (3.0f + 5.0f * lit) * scale, Color{110, 235, 255, static_cast<uint8_t>(70 + 170 * lit)});
            }
        }
    } else if (look == "plumage") {
        // « Couronne de plumes » (2.56, Phénix): feathers of night blue fanned round the avatar,
        // their edges of gold, their tips lighting up one after the other like a dawn going round.
        // Within kAvatarFrameMargin: the tips stay 13 units beyond the avatar.
        r.drawGlow(center, radius + 16 * scale, Color{255, 190, 90, 70});
        ring(main, 2.5f, Color{30, 26, 64, 255});
        const int n = 18;
        const float dawn = std::fmod(time * 0.35f, 1.0f);
        for (int i = 0; i < n; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(n);
            const float a = u * 2.0f * kFxPi - kFxPi * 0.5f;
            const Vec2 d{std::cos(a), std::sin(a)};
            const Vec2 t{-d.y, d.x};
            const Vec2 base = center + d * (main - 1.0f * scale);
            const Vec2 tip = center + d * (main + 11.0f * scale) + t * (3.0f * scale);
            const Vec2 l = base + t * (3.2f * scale), rr = base - t * (3.2f * scale);
            r.fillTriangle(l, rr, tip, Color{52, 47, 103, 255});
            r.drawLine(l, tip, Color{255, 208, 74, 220}, 1.3f * scale, BlendMode::Additive);
            float lit = std::fabs(u - dawn);
            lit = 1.0f - std::min(lit, 1.0f - lit) * 6.0f;
            lit = std::max(0.0f, lit);
            r.drawGlow(tip, (2.5f + 5.0f * lit) * scale, Color{255, 230, 160, static_cast<uint8_t>(90 + 160 * lit)});
            r.fillCircle(tip, (0.8f + 0.6f * lit) * scale, Color{255, 250, 230, 255}, BlendMode::Additive);
        }
        ring(main - 1.5f * scale, 1.2f, Color{255, 208, 74, 200});
    } else if (look == "ossuary") {
        // « Ossuaire » (2.57, Liche): bones laid end to end round the avatar, six skulls among
        // them whose eyes of soul fire light up one after the other. Within kAvatarFrameMargin:
        // the skulls stay 10 units beyond the avatar.
        r.drawGlow(center, radius + 14 * scale, Color{150, 240, 110, 60});
        ring(main, 3.0f, Color{40, 34, 30, 255});
        const int bones = 18;
        for (int i = 0; i < bones; ++i) {
            const float a0 = (static_cast<float>(i) + 0.15f) / bones * 2.0f * kFxPi;
            const float a1 = (static_cast<float>(i) + 0.85f) / bones * 2.0f * kFxPi;
            const Vec2 p0 = center + Vec2{std::cos(a0), std::sin(a0)} * main;
            const Vec2 p1 = center + Vec2{std::cos(a1), std::sin(a1)} * main;
            r.drawLine(p0, p1, Color{226, 214, 184, 255}, 2.4f * scale);
            r.fillCircle(p0, 1.6f * scale, Color{236, 226, 198, 255});
            r.fillCircle(p1, 1.6f * scale, Color{236, 226, 198, 255});
        }
        const float turn = std::fmod(time * 0.5f, 1.0f);
        for (int k = 0; k < 6; ++k) {
            const float u = static_cast<float>(k) / 6.0f;
            const float a = u * 2.0f * kFxPi - kFxPi * 0.5f;
            const Vec2 c = center + Vec2{std::cos(a), std::sin(a)} * (main + 4.0f * scale);
            r.fillCircle(c, 5.6f * scale, Color{232, 222, 196, 255});
            r.fillRect({c.x - 3.0f * scale, c.y + 2.5f * scale, 6.0f * scale, 3.4f * scale}, Color{232, 222, 196, 255});
            float lit = std::fabs(u - turn);
            lit = std::max(0.0f, 1.0f - std::min(lit, 1.0f - lit) * 5.0f);
            const Color eye{static_cast<uint8_t>(40 + 140 * lit), static_cast<uint8_t>(40 + 205 * lit), static_cast<uint8_t>(30 + 72 * lit), 255};
            r.fillCircle(c + Vec2{-2.0f * scale, -0.4f * scale}, 1.3f * scale, eye);
            r.fillCircle(c + Vec2{2.0f * scale, -0.4f * scale}, 1.3f * scale, eye);
            if (lit > 0.0f) r.drawGlow(c, (3.0f + 6.0f * lit) * scale, Color{180, 245, 102, static_cast<uint8_t>(60 + 150 * lit)});
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
        // « Couronne de la semaine » (2.35): the gold ring, seven studs on its lower half (the
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
    } else if (look == "ashes") {
        // « Cendres de dragon » (2.48, Pyrrhax): grey flakes of ash drifting down, a few embers
        // still burning among them.
        const bool ember = seed % 4 == 0;
        Vec2 j{(noise(seed) - 0.5f) * 10.0f + std::sin(age * 6.0f + static_cast<float>(seed)) * 3.0f, age * 22.0f + noise(seed * 3u + 2u) * 4.0f};
        Vec2 q = cv.off(p, j);
        if (ember) {
            cv.glow(q, 6.0f * k + 1.5f, Color{255, 120, 40, alpha(0.7f * f)});
            cv.dot(q, 1.4f * k + 0.5f, Color{255, 220, 150, alpha(0.9f * f)}, BlendMode::Additive);
        } else {
            cv.dot(q, 1.8f * k + 0.7f, Color{150, 140, 150, alpha(0.75f * f)});
        }
    } else if (look == "debris") {
        // « Éclats de roche » (2.49, Monolithe): chips of granite bouncing behind Nova, a rune
        // glowing on one in three.
        const float up = std::fabs(std::sin(age * 7.0f + static_cast<float>(seed))) * 10.0f * (1.0f - std::min(1.0f, age));
        Vec2 q = cv.off(p, {(noise(seed) - 0.5f) * 12.0f, -up + age * 6.0f});
        const float sz = 1.4f + 1.4f * noise(seed * 11u + 4u);
        cv.dot(q, sz * k + 0.6f, Color{130, 124, 118, alpha(0.9f * f)});
        cv.dot(cv.off(q, {-0.5f, -0.5f}), (sz * k + 0.6f) * 0.5f, Color{180, 174, 166, alpha(0.8f * f)});
        if (seed % 3 == 0) cv.glow(q, 4.0f * k + 1.0f, Color{255, 170, 60, alpha(0.6f * f)});
    } else if (look == "bubbles") {
        // « Bulles des abysses » (2.53, Kraken): bubbles of light rising and wobbling behind
        // Nova, a few curls of ink among them.
        Vec2 j{(noise(seed) - 0.5f) * 10.0f + std::sin(age * 9.0f + static_cast<float>(seed)) * 3.0f,
               -age * 36.0f - noise(seed * 3u + 5u) * 5.0f};
        Vec2 q = cv.off(p, j);
        if (seed % 4 == 0) {
            cv.dot(q, 3.2f * k + 1.2f, Color{40, 30, 80, alpha(0.55f * f)});
            cv.dot(cv.off(q, {2.0f, -1.0f}), 2.2f * k + 0.8f, Color{62, 50, 112, alpha(0.45f * f)});
        } else {
            const float sz = 1.6f + 2.0f * noise(seed * 7u + 1u);
            cv.glow(q, sz * 2.4f * k + 1.5f, Color{110, 230, 255, alpha(0.35f * f)});
            cv.ring(q, sz * k + 0.8f, 1.0f, Color{170, 240, 255, alpha(0.85f * f)});
            cv.dot(cv.off(q, {-sz * 0.35f, -sz * 0.35f}), 0.6f, Color{255, 255, 255, alpha(0.9f * f)}, BlendMode::Additive);
        }
    } else if (look == "feathers") {
        // « Plumes de lumière » (2.56, Phénix): feathers of golden light spinning down behind
        // Nova, going out into embers as they fall.
        if (seed % 2 == 1) return;
        const float spin = age * 9.0f + static_cast<float>(seed) * 1.3f;
        Vec2 j{(noise(seed) - 0.5f) * 12.0f + std::sin(age * 5.0f + static_cast<float>(seed)) * 4.0f, age * 26.0f + noise(seed * 5u + 3u) * 5.0f};
        Vec2 q = cv.off(p, j);
        const float len = 5.5f * (0.5f + 0.5f * k) + 1.5f;
        Vec2 d{std::cos(spin), std::sin(spin) * 0.6f + 0.4f};
        const float dl = std::sqrt(d.x * d.x + d.y * d.y);
        d = d / std::max(0.001f, dl);
        Color c = mix(Color{255, 110, 60, 255}, Color{255, 214, 90, 255}, k);
        cv.glow(q, 6.0f * k + 1.5f, c.withAlpha(alpha(0.4f * f)));
        cv.line(cv.off(q, d * -len), cv.off(q, d * len), c.withAlpha(alpha(0.9f * f)), 2.4f * k + 0.8f);
        cv.dot(cv.off(q, d * len), 1.0f, Color{255, 250, 225, alpha(0.9f * f)}, BlendMode::Additive);
    } else if (look == "souls") {
        // « Âmes errantes » (2.57, Liche): pale souls slip away behind Nova and rise, trembling,
        // a soft tail under each, two hollow eyes.
        if (seed % 2 == 1) return;
        Vec2 j{(noise(seed) - 0.5f) * 10.0f + std::sin(age * 7.0f + static_cast<float>(seed)) * 3.0f, -age * 24.0f - noise(seed * 3u + 1u) * 4.0f};
        Vec2 q = cv.off(p, j);
        const float sz = 3.2f * (0.5f + 0.5f * k) + 1.0f;
        const Color soul = mix(Color{110, 200, 120, 255}, Color{200, 255, 170, 255}, k);
        cv.glow(q, 7.0f * k + 2.0f, soul.withAlpha(alpha(0.35f * f)));
        for (int t = 1; t <= 3; ++t)
            cv.dot(cv.off(q, {std::sin(age * 9.0f + static_cast<float>(t)) * 1.5f, sz * 0.8f * static_cast<float>(t)}), sz * (1.0f - 0.25f * static_cast<float>(t)),
                   soul.withAlpha(alpha(0.5f * f / static_cast<float>(t))));
        cv.dot(q, sz, Color{230, 255, 220, alpha(0.85f * f)});
        cv.dot(cv.off(q, {-sz * 0.35f, -sz * 0.1f}), sz * 0.22f, Color{20, 30, 24, alpha(0.9f * f)});
        cv.dot(cv.off(q, {sz * 0.35f, -sz * 0.1f}), sz * 0.22f, Color{20, 30, 24, alpha(0.9f * f)});
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
        // « Comète du mois » (2.35): a gold head fading into a long violet tail, a small star
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
    } else if (look == "dragon_breath") {
        // « Souffle du dragon » (2.48, Pyrrhax): a cone of fire sweeps over the portal from left
        // to right, then falls as embers.
        Vec2 o = cv.off(at, {-110, -90});
        const float sweep = std::clamp(t / 1.4f, 0.0f, 1.0f);
        const float ang = 0.15f + 0.55f * sweep;  // from almost horizontal toward the portal
        Vec2 d{std::cos(ang), std::sin(ang)};
        if (t < 1.6f)
            for (int i = 0; i < 14; ++i) {
                const float u = static_cast<float>(i) / 13.0f;
                const float wob = std::sin(t * 20.0f + static_cast<float>(i) * 1.3f) * 6.0f * u;
                Vec2 q = cv.off(o, d * (20.0f + 190.0f * u) + Vec2{-d.y, d.x} * wob);
                Color c = mix(Color{255, 240, 170, 255}, Color{230, 60, 20, 255}, u);
                cv.glow(q, 10.0f + 22.0f * u, c.withAlpha(alpha(0.75f * fade * (1.0f - sweep * 0.3f))));
            }
        for (int i = 0; i < 18; ++i) {
            const float te = t - 0.6f - 0.06f * static_cast<float>(i);
            if (te < 0.0f) continue;
            const float x = -60.0f + 130.0f * noise(static_cast<uint32_t>(i) * 53u + 9u);
            Vec2 q = cv.off(at, {x + std::sin(te * 3.0f + static_cast<float>(i)) * 6.0f, -110.0f + 70.0f * te});
            cv.glow(q, 5.0f, Color{255, 140, 50, alpha(0.8f * fade * std::clamp(1.0f - te / 1.6f, 0.0f, 1.0f))});
        }
    } else if (look == "quake") {
        // « Séisme runique » (2.49, Monolithe): the ground shakes, a ring of runes lights up around
        // the portal, then rocks burst out of the ground and fall back.
        const float shake = t < 0.8f ? std::sin(t * 60.0f) * 3.0f * (1.0f - t / 0.8f) : 0.0f;
        Vec2 o = cv.off(at, {shake, 0});
        const float grow = 1.0f - std::exp(-3.0f * t);
        for (int i = 0; i < 10; ++i) {
            const float a = static_cast<float>(i) * 2.0f * kFxPi / 10.0f;
            Vec2 p = cv.off(o, {std::cos(a) * 70.0f * grow, std::sin(a) * 22.0f * grow - 4.0f});
            const float on = std::clamp(t * 4.0f - static_cast<float>(i) * 0.3f, 0.0f, 1.0f);
            cv.glow(p, 7.0f, Color{255, 170, 60, alpha(0.8f * on * fade)});
            cv.dot(p, 1.6f, Color{255, 230, 170, alpha(on * fade)}, BlendMode::Additive);
        }
        for (int i = 0; i < 8; ++i) {
            const float tr = t - 0.5f - 0.08f * static_cast<float>(i);
            if (tr < 0.0f || tr > 1.4f) continue;
            const float x = (noise(static_cast<uint32_t>(i) * 41u + 3u) - 0.5f) * 150.0f;
            const float v = 150.0f + 60.0f * noise(static_cast<uint32_t>(i) * 17u + 1u);
            Vec2 p = cv.off(o, {x, -v * tr + 160.0f * tr * tr});
            cv.dot(p, 4.0f, Color{120, 114, 108, alpha(fade)});
            cv.dot(cv.off(p, {-1.0f, -1.0f}), 2.0f, Color{170, 164, 156, alpha(fade)});
        }
    } else if (look == "tidal_wave") {
        // « Raz-de-marée » (2.53, Kraken): a giant wave rises behind the portal, its crest leaning
        // over it, falls back in foam, and leaves bubbles of light rising.
        const float rise = std::clamp(t / 0.9f, 0.0f, 1.0f);
        const float fall = std::clamp((t - 0.9f) / 0.7f, 0.0f, 1.0f);
        const float hgt = 150.0f * (1.0f - (1.0f - rise) * (1.0f - rise)) * (1.0f - 0.9f * fall);
        for (int i = 0; i < 24; ++i) {
            const float u = static_cast<float>(i) / 23.0f;
            const float shape = std::sin(u * kFxPi) * (0.8f + 0.2f * std::sin(u * 7.0f + t * 4.0f));
            const float top = hgt * shape;
            const float lean = 26.0f * rise * (1.0f - fall) * shape;  // the crest leans to the right
            const float x = -120.0f + 240.0f * u;
            for (float y = 0.0f; y < top; y += 10.0f) {
                const float h = y / std::max(1.0f, top);
                cv.dot(cv.off(at, {x + lean * h * h, -y}), 7.0f, mix(Color{14, 34, 80, 255}, Color{30, 90, 150, 255}, h).withAlpha(alpha(0.7f * fade)));
            }
            if (top > 4.0f) {
                Vec2 crest = cv.off(at, {x + lean, -top});
                cv.glow(crest, 9.0f, Color{110, 230, 255, alpha(0.7f * fade)});
                cv.dot(crest, 2.2f, Color{235, 250, 255, alpha(0.9f * fade)}, BlendMode::Additive);
            }
        }
        // The foam thrown when it breaks.
        for (int i = 0; i < 16; ++i) {
            const float tf = t - 0.95f - 0.03f * static_cast<float>(i);
            if (tf < 0.0f || tf > 1.0f) continue;
            const float x = -100.0f + 200.0f * noise(static_cast<uint32_t>(i) * 29u + 7u);
            const float v = 90.0f + 70.0f * noise(static_cast<uint32_t>(i) * 13u + 2u);
            Vec2 q = cv.off(at, {x + 30.0f * tf, -40.0f - v * tf + 140.0f * tf * tf});
            cv.dot(q, 2.0f, Color{235, 250, 255, alpha(0.85f * fade * (1.0f - tf))}, BlendMode::Additive);
        }
        // Bubbles of light rising where it fell.
        for (int i = 0; i < 14; ++i) {
            const float tb = t - 1.2f - 0.05f * static_cast<float>(i);
            if (tb < 0.0f) continue;
            const float x = -90.0f + 180.0f * noise(static_cast<uint32_t>(i) * 61u + 3u) + std::sin(tb * 6.0f + static_cast<float>(i)) * 5.0f;
            Vec2 q = cv.off(at, {x, -10.0f - 70.0f * tb});
            const float a = fade * std::clamp(1.0f - tb / 1.4f, 0.0f, 1.0f);
            cv.glow(q, 6.0f, Color{110, 230, 255, alpha(0.5f * a)});
            cv.ring(q, 3.0f, 1.0f, Color{180, 240, 255, alpha(0.9f * a)});
        }
    } else if (look == "rebirth") {
        // « Renaissance » (2.56, Phénix): the portal flares up, falls to ash, then a phoenix of
        // light is born from it and flies up, wings wide, a rain of golden sparks behind it.
        const float burn = std::clamp(t / 0.7f, 0.0f, 1.0f);
        const float ashes = std::clamp((t - 0.7f) / 0.5f, 0.0f, 1.0f);
        const float rise = std::clamp((t - 1.1f) / 1.3f, 0.0f, 1.0f);
        // The flames round the portal, then a heap of glowing ash.
        for (int i = 0; i < 14; ++i) {
            const float u = static_cast<float>(i) / 13.0f;
            const float x = -46.0f + 92.0f * u;
            const float h = (30.0f + 40.0f * noise(static_cast<uint32_t>(i) * 17u + 5u)) * burn * (1.0f - ashes) *
                            (0.7f + 0.3f * std::sin(t * 18.0f + static_cast<float>(i)));
            if (h > 1.0f) {
                cv.line(cv.off(at, {x, 0.0f}), cv.off(at, {x + std::sin(t * 9.0f + x) * 4.0f, -h}), Color{255, 150, 60, alpha(0.8f * fade)}, 4.0f);
                cv.glow(cv.off(at, {x, -h * 0.6f}), 10.0f, Color{255, 120, 40, alpha(0.35f * fade)});
            }
            if (ashes > 0.0f && rise < 1.0f) cv.dot(cv.off(at, {x * 0.8f, -2.0f - 3.0f * std::sin(u * kFxPi)}), 3.0f, Color{140, 130, 124, alpha(ashes * (1.0f - rise) * fade)});
        }
        if (rise > 0.0f) {
            // The bird of light: a body, two wings beating, three tail plumes.
            const Vec2 o = cv.off(at, {10.0f * rise, -20.0f - 120.0f * rise});
            const float beat = std::sin(t * 12.0f) * 0.5f + 0.5f;
            const Color gold{255, 214, 90, alpha(0.95f * fade)};
            cv.glow(o, 30.0f, Color{255, 190, 90, alpha(0.5f * fade)});
            for (int side = -1; side <= 1; side += 2)
                for (int f = 0; f < 5; ++f) {
                    const float a = -kFxPi * 0.5f + static_cast<float>(side) * (0.5f + 0.22f * static_cast<float>(f) + 0.35f * beat);
                    const float L = 30.0f - 3.0f * static_cast<float>(f);
                    cv.line(o, cv.off(o, {std::cos(a) * L, std::sin(a) * L}), gold, 2.2f);
                }
            for (int k = -1; k <= 1; ++k)
                cv.line(o, cv.off(o, {static_cast<float>(k) * 7.0f, 34.0f + 6.0f * std::fabs(static_cast<float>(k))}), Color{255, 140, 70, alpha(0.8f * fade)}, 2.0f);
            cv.dot(o, 4.0f, Color{255, 250, 230, alpha(fade)}, BlendMode::Additive);
            // Golden sparks falling from its wings.
            for (int i = 0; i < 16; ++i) {
                const float ts = t - 1.2f - 0.06f * static_cast<float>(i);
                if (ts < 0.0f) continue;
                Vec2 q = cv.off(at, {-40.0f + 80.0f * noise(static_cast<uint32_t>(i) * 41u + 9u), -30.0f - 110.0f * noise(static_cast<uint32_t>(i) * 7u + 1u) + 50.0f * ts});
                star(cv, q, 2.0f, Color{255, 230, 150, alpha(0.8f * fade * std::clamp(1.0f - ts, 0.0f, 1.0f))});
            }
        }
    } else if (look == "candles") {
        // « Veillée des âmes » (2.57, Liche): candles of soul fire light up one after the other in
        // a ring round the portal, then their green flames rise toward the sky as souls set free.
        const int n = 11;
        for (int i = 0; i < n; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(n - 1);
            const float x = -60.0f + 120.0f * u;
            const float y = -6.0f * std::sin(u * kFxPi);
            const float lit = std::clamp((t - 0.12f * static_cast<float>(i)) / 0.25f, 0.0f, 1.0f);
            const float fly = std::clamp((t - 1.5f - 0.05f * static_cast<float>(i)) / 1.0f, 0.0f, 1.0f);
            const Vec2 base = cv.off(at, {x, y});
            cv.line(base, cv.off(base, {0.0f, -12.0f}), Color{226, 214, 184, alpha(fade)}, 3.0f);
            if (lit <= 0.0f) continue;
            const float flick = 0.8f + 0.2f * std::sin(t * 17.0f + static_cast<float>(i));
            if (fly < 1.0f) {
                const Vec2 fl = cv.off(base, {0.0f, -16.0f});
                cv.glow(fl, 8.0f * lit * flick, Color{150, 240, 110, alpha(0.5f * lit * fade * (1.0f - fly))});
                cv.dot(fl, 2.4f * lit * flick, Color{220, 255, 200, alpha(lit * fade * (1.0f - fly))}, BlendMode::Additive);
            }
            if (fly > 0.0f) {
                // The soul leaves its flame and rises, swaying.
                const Vec2 q = cv.off(base, {std::sin(fly * 6.0f + static_cast<float>(i)) * 6.0f, -16.0f - 110.0f * fly});
                cv.glow(q, 9.0f, Color{150, 240, 110, alpha(0.45f * fade * (1.0f - fly * 0.6f))});
                cv.dot(q, 3.0f, Color{230, 255, 220, alpha(fade * (1.0f - fly * 0.5f))}, BlendMode::Additive);
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
        // « Série de feu » (2.35): seven flames, one per day of the series, light up one after
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
