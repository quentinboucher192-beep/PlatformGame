#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "engine/core/DetMath.h"

namespace cosmic {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float px, float py) : x(px), y(py) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;

    float length() const { return std::sqrt(x * x + y * y); }
    constexpr float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const {
        float l = length();
        return l > 1e-6f ? Vec2{x / l, y / l} : Vec2{};
    }
    constexpr float dot(Vec2 o) const { return x * o.x + y * o.y; }
    static Vec2 fromAngle(float radians) { return {dmath::cos(radians), dmath::sin(radians)}; }
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float px, float py, float pw, float ph) : x(px), y(py), w(pw), h(ph) {}
    static constexpr Rect fromCenter(Vec2 c, Vec2 size) {
        return {c.x - size.x * 0.5f, c.y - size.y * 0.5f, size.x, size.y};
    }
    static constexpr Rect fromBottomCenter(Vec2 p, Vec2 size) {
        return {p.x - size.x * 0.5f, p.y - size.y, size.x, size.y};
    }

    constexpr float left() const { return x; }
    constexpr float right() const { return x + w; }
    constexpr float top() const { return y; }
    constexpr float bottom() const { return y + h; }
    constexpr Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    constexpr Vec2 bottomCenter() const { return {x + w * 0.5f, y + h}; }
    constexpr Vec2 size() const { return {w, h}; }
    constexpr Vec2 pos() const { return {x, y}; }

    constexpr bool intersects(const Rect& o) const {
        return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
    }
    constexpr bool contains(Vec2 p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
    constexpr bool containsRect(const Rect& o) const {
        return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom();
    }
    constexpr Rect expanded(float m) const { return {x - m, y - m, w + 2 * m, h + 2 * m}; }
    constexpr Rect translated(Vec2 d) const { return {x + d.x, y + d.y, w, h}; }
    Rect intersection(const Rect& o) const {
        float nx = std::max(x, o.x), ny = std::max(y, o.y);
        float nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
        if (nr <= nx || nb <= ny) return {};
        return {nx, ny, nr - nx, nb - ny};
    }
    Rect united(const Rect& o) const {
        float nx = std::min(x, o.x), ny = std::min(y, o.y);
        return {nx, ny, std::max(right(), o.right()) - nx, std::max(bottom(), o.bottom()) - ny};
    }
    constexpr bool empty() const { return w <= 0.0f || h <= 0.0f; }
    constexpr bool operator==(const Rect&) const = default;
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    constexpr Color() = default;
    constexpr Color(uint8_t pr, uint8_t pg, uint8_t pb, uint8_t pa = 255) : r(pr), g(pg), b(pb), a(pa) {}

    static constexpr Color white() { return {255, 255, 255, 255}; }
    static constexpr Color black() { return {0, 0, 0, 255}; }
    static constexpr Color transparent() { return {0, 0, 0, 0}; }

    constexpr Color withAlpha(uint8_t na) const { return {r, g, b, na}; }
    Color withAlphaF(float f) const {
        return {r, g, b, static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f)};
    }
    Color scaled(float f) const {
        auto s = [f](uint8_t c) { return static_cast<uint8_t>(std::clamp(c * f, 0.0f, 255.0f)); };
        return {s(r), s(g), s(b), a};
    }
    static Color lerp(Color a0, Color b0, float t) {
        auto l = [t](uint8_t p, uint8_t q) {
            return static_cast<uint8_t>(std::clamp(p + (q - p) * t, 0.0f, 255.0f));
        };
        return {l(a0.r, b0.r), l(a0.g, b0.g), l(a0.b, b0.b), l(a0.a, b0.a)};
    }
    /// Parses "#rrggbb" or "#rrggbbaa". Returns fallback on malformed input.
    static Color fromHex(const std::string& hex, Color fallback = white());
    std::string toHex() const;
    constexpr bool operator==(const Color&) const = default;
};

inline float clampf(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 lerpv(Vec2 a, Vec2 b, float t) { return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t)}; }
inline float signf(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
inline float degToRad(float d) { return d * kPi / 180.0f; }
inline float radToDeg(float r) { return r * 180.0f / kPi; }

/// Moves `current` towards `target` by at most `maxDelta`.
inline float approach(float current, float target, float maxDelta) {
    if (current < target) return std::min(current + maxDelta, target);
    return std::max(current - maxDelta, target);
}

/// Frame-rate independent exponential smoothing factor (1 - e^(-rate*dt)).
inline float damp(float rate, float dt) { return 1.0f - dmath::exp(-rate * dt); }

inline float distance(Vec2 a, Vec2 b) { return (a - b).length(); }

}  // namespace cosmic
