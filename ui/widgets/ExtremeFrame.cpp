#include "ui/widgets/ExtremeFrame.h"

#include <algorithm>
#include <cmath>

#include "engine/rendering/Renderer.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

constexpr Color kBlood{150, 14, 22, 255};
constexpr Color kEmber{255, 74, 28, 255};
constexpr Color kFlameOuter{168, 18, 12, 255};
constexpr Color kFlameMid{255, 96, 26, 255};
constexpr Color kFlameCore{255, 214, 120, 255};

/// A cheap smooth noise in [0, 1] (no state: the frame is pure decoration of the UI time).
float wave(float x, float t) {
    float v = std::sin(x * 0.071f + t * 5.3f) * 0.5f + std::sin(x * 0.173f - t * 3.1f) * 0.3f + std::sin(x * 0.031f + t * 1.7f) * 0.2f;
    return 0.5f + 0.5f * v;
}

/// The heartbeat: two quick beats then a rest (lub-dub), in [0, 1].
float heartbeat(float t) {
    float p = std::fmod(t, 1.35f);
    auto beat = [](float x) { return x < 0.0f ? 0.0f : std::exp(-x * 14.0f); };
    return std::clamp(beat(p) + 0.7f * beat(p - 0.24f), 0.0f, 1.0f);
}

float hash(float x) {
    float v = std::sin(x * 12.9898f) * 43758.5453f;
    return v - std::floor(v);
}

/// One tongue of fire: a dark outer flame, a hot middle, a white-yellow core (additive).
void tongue(Renderer& r, float x, float base, float half, float h, float sway) {
    r.fillTriangle({x - half, base}, {x + half, base}, {x + sway, base - h}, kFlameOuter.withAlphaF(0.85f));
    r.fillTriangle({x - half * 0.6f, base}, {x + half * 0.6f, base}, {x + sway * 0.85f, base - h * 0.7f}, kFlameMid.withAlphaF(0.8f), BlendMode::Additive);
    r.fillTriangle({x - half * 0.26f, base}, {x + half * 0.26f, base}, {x + sway * 0.6f, base - h * 0.4f}, kFlameCore.withAlphaF(0.75f),
                   BlendMode::Additive);
}

/// Irregular flames licking the bottom edge: big tongues of uneven width and height, small
/// ones between them, each flickering on its own.
void flames(UIContext& ui, const Rect& box, float t, float seed, float maxH, float step) {
    Renderer& r = ui.r();
    float base = box.bottom() + ui.s(2);
    // Big tongues.
    int i = 0;
    for (float x = box.x - step; x < box.right() + step; ++i) {
        float hi = hash(static_cast<float>(i) + seed * 7.0f);
        float w = step * (1.4f + 1.3f * hi);
        float k = wave(x + seed * 97.0f, t);
        float h = maxH * (0.45f + 0.55f * hi) * (0.55f + 0.45f * k);
        float sway = std::sin(t * (3.0f + 2.0f * hi) + static_cast<float>(i)) * w * 0.35f;
        tongue(r, x + w * 0.5f, base, w * 0.55f, h, sway);
        x += w * 0.8f;
    }
    // Small flames between them.
    for (float x = box.x + step * 0.3f; x < box.right(); x += step * 0.9f) {
        float k = wave(x * 1.7f + 31.0f + seed, t * 1.3f);
        tongue(r, x, base, step * 0.38f, maxH * 0.42f * (0.4f + 0.6f * k), std::sin(t * 6.0f + x) * step * 0.2f);
    }
}

/// Blood running down from the top edge: drops that grow, fall and come back.
void drips(UIContext& ui, const Rect& box, float t, float seed) {
    Renderer& r = ui.r();
    const Color dark{92, 6, 12, 235}, wet{170, 18, 26, 235};
    int n = std::max(3, static_cast<int>(box.w / ui.s(70)));
    for (int i = 0; i < n; ++i) {
        float hi = hash(static_cast<float>(i) * 3.1f + seed);
        float x = box.x + box.w * (static_cast<float>(i) + 0.25f + 0.5f * hi) / static_cast<float>(n);
        float cycle = 5.0f + 4.0f * hi;
        float p = std::fmod(t + hi * cycle, cycle) / cycle;  // 0..1
        float maxLen = ui.s(18.0f + 46.0f * hash(hi * 9.0f));
        float len = maxLen * std::min(1.0f, p * 1.6f);
        float w = ui.s(4.0f + 4.0f * hash(hi * 5.0f));
        float top = box.y + ui.s(3);
        r.fillRect({x - w * 0.5f, top, w, len}, dark);
        r.fillRect({x - w * 0.15f, top, std::max(1.0f, w * 0.25f), len * 0.8f}, wet);
        r.fillCircle({x, top + len}, w * 0.85f, dark);
        if (p > 0.62f) {
            // The drop falls.
            float f = (p - 0.62f) / 0.38f;
            r.fillCircle({x, top + len + f * f * box.h * 0.5f}, w * 0.7f, wet.withAlphaF(1.0f - f));
        }
    }
}

void embers(UIContext& ui, const Rect& box, float t, float seed, int count) {
    Renderer& r = ui.r();
    for (int i = 0; i < count; ++i) {
        float fi = static_cast<float>(i) + seed * 13.0f;
        float speed = 0.18f + 0.07f * std::fmod(fi * 0.37f, 1.0f);
        float life = std::fmod(t * speed + fi * 0.618f, 1.0f);  // 0 at the bottom, 1 at the top
        float x = box.x + box.w * std::fmod(fi * 0.381966f + 0.07f * std::sin(t + fi), 1.0f);
        float y = box.bottom() - life * box.h * 0.75f;
        float a = (1.0f - life) * 0.85f;
        r.fillCircle({x, y}, ui.s(1.6f + 1.2f * (1.0f - life)), kEmber.withAlphaF(a), BlendMode::Additive);
    }
}

/// Jagged cracks running from a corner along both edges.
void cracks(UIContext& ui, Vec2 corner, Vec2 alongX, Vec2 alongY, Color c) {
    Renderer& r = ui.r();
    static const float kA[] = {0.0f, 9.0f, 5.0f, 16.0f, 11.0f, 24.0f, 19.0f, 31.0f};
    static const float kB[] = {0.0f, 2.0f, 6.0f, 4.0f, 9.0f, 7.0f, 12.0f, 10.0f};
    for (int side = 0; side < 2; ++side) {
        Vec2 a = side == 0 ? alongX : alongY, b = side == 0 ? alongY : alongX;
        Vec2 prev = corner + b * ui.s(3.0f);
        for (int i = 1; i < 8; ++i) {
            Vec2 p = corner + a * ui.s(kA[i] * 3.0f) + b * ui.s(3.0f + kB[i] * 1.4f);
            r.drawLine(prev, p, c, std::max(1.0f, ui.s(2.4f)));
            r.drawGlow(p, ui.s(7.0f), c.withAlphaF(0.35f));
            prev = p;
        }
    }
}

/// Thorns pointing out of a corner.
void thorns(UIContext& ui, Vec2 corner, Vec2 out, Vec2 alongX, Vec2 alongY, Color dark, Color tip) {
    Renderer& r = ui.r();
    const float L[] = {48.0f, 30.0f, 30.0f};
    const Vec2 dirs[] = {out, (out + alongX * 0.9f).normalized(), (out + alongY * 0.9f).normalized()};
    const Vec2 offs[] = {Vec2{0, 0}, alongX * ui.s(22.0f), alongY * ui.s(22.0f)};
    for (int i = 0; i < 3; ++i) {
        Vec2 b = corner + offs[i];
        Vec2 n{-dirs[i].y, dirs[i].x};
        float w = ui.s(i == 0 ? 9.0f : 6.5f);
        Vec2 p = b + dirs[i] * ui.s(L[i]);
        r.fillTriangle(b + n * w, b - n * w, p, dark);
        r.fillTriangle(b + dirs[i] * ui.s(L[i] * 0.55f) + n * w * 0.4f, b + dirs[i] * ui.s(L[i] * 0.55f) - n * w * 0.4f, p, tip);
    }
}

}  // namespace

void drawExtremeFrame(UIContext& ui, const Rect& box, float radius, float seed, float flameHeight) {
    Renderer& r = ui.r();
    const float t = ui.time() + seed;
    const float beat = heartbeat(t);
    const float rad = ui.s(radius);
    // Backdrop: almost black, blood at the bottom, lit by the flames.
    r.fillRoundedRectGradient(box, rad, Color{20, 4, 8, 246}, Color{52, 6, 10, 250});
    r.drawGlow({box.center().x, box.bottom()}, std::max(box.w, box.h) * 0.55f, kEmber.withAlphaF(0.10f + 0.06f * beat));
    r.pushClip(box);
    // Darkness creeping in from the sides and the top.
    float vw = box.w * 0.10f, vh = box.h * 0.20f;
    r.fillGradientH({box.x, box.y, vw, box.h}, Color{0, 0, 0, 150}, Color{0, 0, 0, 0});
    r.fillGradientH({box.right() - vw, box.y, vw, box.h}, Color{0, 0, 0, 0}, Color{0, 0, 0, 150});
    r.fillGradientV({box.x, box.y, box.w, vh}, Color{0, 0, 0, 140}, Color{0, 0, 0, 0});
    flames(ui, box, t, seed, std::min(ui.s(flameHeight), box.h * 0.30f), ui.s(18));
    embers(ui, box, t, seed, std::clamp(static_cast<int>(box.w / ui.s(30)), 8, 50));
    drips(ui, box, t, seed);
    r.popClip();
    // The border: a dark thick rim, a thin vein that beats, a halo outside.
    r.drawRoundedRect(box.expanded(ui.s(4)), rad + ui.s(4), kBlood.withAlphaF(0.20f + 0.35f * beat), ui.s(4));
    r.drawRoundedRect(box, rad, Color{58, 6, 8, 255}, ui.s(5));
    Color vein = Color::lerp(kBlood, kEmber, 0.35f + 0.65f * beat);
    r.drawRoundedRect(box.expanded(-ui.s(3)), std::max(0.0f, rad - ui.s(3)), vein, std::max(1.0f, ui.s(2)));
    // Cracks and thorns at the four corners.
    Color crack = kEmber.withAlphaF(0.55f + 0.45f * beat);
    Color dark{40, 4, 6, 255}, tip = Color::lerp(kBlood, kEmber, beat);
    struct Corner {
        Vec2 p, out, ax, ay;
    };
    const Corner corners[] = {
        {{box.x, box.y}, Vec2{-0.7071f, -0.7071f}, {1, 0}, {0, 1}},
        {{box.right(), box.y}, Vec2{0.7071f, -0.7071f}, {-1, 0}, {0, 1}},
        {{box.x, box.bottom()}, Vec2{-0.7071f, 0.7071f}, {1, 0}, {0, -1}},
        {{box.right(), box.bottom()}, Vec2{0.7071f, 0.7071f}, {-1, 0}, {0, -1}},
    };
    for (const Corner& c : corners) {
        Vec2 inner = c.p + (c.ax + c.ay) * ui.s(6.0f);
        cracks(ui, inner, c.ax, c.ay, crack);
        thorns(ui, c.p + (c.ax + c.ay) * ui.s(2.0f), c.out, c.ax * -1.0f, c.ay * -1.0f, dark, tip);
    }
}

void drawExtremeRowAccent(UIContext& ui, const Rect& row, float seed) {
    Renderer& r = ui.r();
    const float t = ui.time() + seed;
    const float beat = heartbeat(t);
    Rect end{row.right() - row.w * 0.22f, row.y, row.w * 0.22f, row.h};
    r.pushClip(row);
    r.fillGradientH(end, Color{90, 8, 12, 0}, Color{110, 10, 14, 120}, BlendMode::Alpha);
    embers(ui, end, t, seed, 6);
    r.popClip();
    r.drawRoundedRect(row, ui.s(8), Color::lerp(kBlood, kEmber, 0.3f + 0.7f * beat), std::max(1.0f, ui.s(2)));
}

}  // namespace cosmic
