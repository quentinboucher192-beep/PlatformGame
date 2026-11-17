#include "engine/rendering/ShapeProbe.h"

#include <algorithm>
#include <cmath>

namespace cosmic {

namespace {
bool empty(const Rect& r) { return !(r.w > 0.0f) || !(r.h > 0.0f); }

/// Separating axis test of a convex polygon (up to 3 points) against a rectangle: they overlap
/// when no axis (the rectangle's two, the polygon's edge normals) separates them. Strict: shapes
/// that only meet along a line do not overlap.
bool polygonTouchesRect(const Vec2* p, int n, const Rect& r) {
    if (empty(r)) return false;
    const Vec2 corners[4] = {{r.x, r.y}, {r.right(), r.y}, {r.right(), r.bottom()}, {r.x, r.bottom()}};
    auto separated = [&](Vec2 axis) {
        float a0 = 1e30f, a1 = -1e30f, b0 = 1e30f, b1 = -1e30f;
        for (int i = 0; i < n; ++i) {
            const float d = p[i].x * axis.x + p[i].y * axis.y;
            a0 = std::min(a0, d);
            a1 = std::max(a1, d);
        }
        for (const Vec2& c : corners) {
            const float d = c.x * axis.x + c.y * axis.y;
            b0 = std::min(b0, d);
            b1 = std::max(b1, d);
        }
        return !(a1 > b0 && b1 > a0);
    };
    if (separated({1.0f, 0.0f}) || separated({0.0f, 1.0f})) return false;
    for (int i = 0; i < n; ++i) {
        const Vec2 e = p[(i + 1) % n] - p[i];
        if (std::abs(e.x) + std::abs(e.y) < 1e-6f) continue;  // a point: no edge
        if (separated({-e.y, e.x})) return false;
    }
    return true;
}
}  // namespace

namespace ShapeTest {
bool triangleTouchesRect(Vec2 a, Vec2 b, Vec2 c, const Rect& r) {
    const Vec2 p[3] = {a, b, c};
    return polygonTouchesRect(p, 3, r);
}

bool segmentTouchesRect(Vec2 a, Vec2 b, const Rect& r) {
    const Vec2 p[2] = {a, b};
    return polygonTouchesRect(p, 2, r);
}

bool circleTouchesRect(Vec2 center, float radius, const Rect& r) {
    if (empty(r) || !(radius > 0.0f)) return false;
    const float nx = std::clamp(center.x, r.x, r.right()), ny = std::clamp(center.y, r.y, r.bottom());
    const float dx = center.x - nx, dy = center.y - ny;
    return dx * dx + dy * dy < radius * radius;
}
}  // namespace ShapeTest

// ------------------------------------------------------------------ bounds

void ShapeBounds::add(float x0, float y0, float x1, float y1) {
    if (!(x1 > x0) || !(y1 > y0)) {
        // A line along an axis: as thick as a pixel.
        if (x1 <= x0) x1 = x0 + 1.0f;
        if (y1 <= y0) y1 = y0 + 1.0f;
    }
    if (!any_) {
        x0_ = x0;
        y0_ = y0;
        x1_ = x1;
        y1_ = y1;
        any_ = true;
        return;
    }
    x0_ = std::min(x0_, x0);
    y0_ = std::min(y0_, y0);
    x1_ = std::max(x1_, x1);
    y1_ = std::max(y1_, y1);
}

void ShapeBounds::triangle(Vec2 a, Vec2 b, Vec2 c) {
    add(std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}));
}

void ShapeBounds::segment(Vec2 a, Vec2 b) { add(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)); }

void ShapeBounds::rect(const Rect& r) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    add(r.x, r.y, r.right(), r.bottom());
}

void ShapeBounds::glow(Vec2 center, float radius) {
    const float core = radius * kGlowCore;
    if (core <= 0.0f) return;
    add(center.x - core, center.y - core, center.x + core, center.y + core);
}

// ------------------------------------------------------------------ coverage

void CoverageProbe::watch(const std::vector<Rect>& zones) {
    zones_ = zones;
    hit_.assign(zones_.size(), 0);
}

void CoverageProbe::triangle(Vec2 a, Vec2 b, Vec2 c) {
    for (size_t i = 0; i < zones_.size(); ++i)
        if (!hit_[i] && ShapeTest::triangleTouchesRect(a, b, c, zones_[i])) hit_[i] = 1;
}

void CoverageProbe::segment(Vec2 a, Vec2 b) {
    for (size_t i = 0; i < zones_.size(); ++i)
        if (!hit_[i] && ShapeTest::segmentTouchesRect(a, b, zones_[i])) hit_[i] = 1;
}

void CoverageProbe::rect(const Rect& r) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    for (size_t i = 0; i < zones_.size(); ++i)
        if (!hit_[i] && zones_[i].w > 0.0f && zones_[i].h > 0.0f && r.intersects(zones_[i])) hit_[i] = 1;
}

void CoverageProbe::glow(Vec2 center, float radius) {
    const float core = radius * kGlowCore;
    for (size_t i = 0; i < zones_.size(); ++i)
        if (!hit_[i] && ShapeTest::circleTouchesRect(center, core, zones_[i])) hit_[i] = 1;
}

}  // namespace cosmic
