#include "engine/physics/Collision.h"
#include "engine/core/DetMath.h"

#include <cmath>
#include <limits>

namespace cosmic::Collision {

Vec2 penetration(const Rect& a, const Rect& b) {
    if (!a.intersects(b)) return {};
    float left = b.right() - a.x;
    float right = a.right() - b.x;
    float up = b.bottom() - a.y;
    float down = a.bottom() - b.y;
    float mx = left < right ? left : -right;
    float my = up < down ? up : -down;
    if (std::abs(mx) < std::abs(my)) return {mx, 0.0f};
    return {0.0f, my};
}

namespace {
void project(const Vec2* pts, int n, Vec2 axis, float& lo, float& hi) {
    lo = std::numeric_limits<float>::max();
    hi = -lo;
    for (int i = 0; i < n; ++i) {
        float d = pts[i].dot(axis);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
}
}  // namespace

bool overlaps(const Rect& a, const OrientedBox& b) {
    Vec2 ra[4] = {{a.x, a.y}, {a.right(), a.y}, {a.right(), a.bottom()}, {a.x, a.bottom()}};
    float c = dmath::cos(b.angle), s = dmath::sin(b.angle);
    Vec2 ux{c, s}, uy{-s, c};
    Vec2 rb[4];
    const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) rb[i] = b.center + ux * (b.halfSize.x * sx[i]) + uy * (b.halfSize.y * sy[i]);
    const Vec2 axes[4] = {{1, 0}, {0, 1}, ux, uy};
    for (const Vec2& ax : axes) {
        float a0, a1, b0, b1;
        project(ra, 4, ax, a0, a1);
        project(rb, 4, ax, b0, b1);
        if (a1 <= b0 || b1 <= a0) return false;
    }
    return true;
}

bool segmentIntersectsRect(Vec2 p0, Vec2 p1, const Rect& r, float* tHit) {
    Vec2 d = p1 - p0;
    float tmin = 0.0f, tmax = 1.0f;
    const float o[2] = {p0.x, p0.y}, dd[2] = {d.x, d.y};
    const float lo[2] = {r.x, r.y}, hi[2] = {r.right(), r.bottom()};
    for (int i = 0; i < 2; ++i) {
        if (std::abs(dd[i]) < 1e-8f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
        } else {
            float t1 = (lo[i] - o[i]) / dd[i];
            float t2 = (hi[i] - o[i]) / dd[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    if (tHit) *tHit = tmin;
    return true;
}

bool circleIntersectsRect(Vec2 c, float radius, const Rect& r) {
    float cx = std::clamp(c.x, r.x, r.right());
    float cy = std::clamp(c.y, r.y, r.bottom());
    float dx = c.x - cx, dy = c.y - cy;
    return dx * dx + dy * dy < radius * radius;
}

}  // namespace cosmic::Collision
