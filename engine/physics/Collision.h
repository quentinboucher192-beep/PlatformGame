#pragma once

#include <optional>

#include "engine/core/Math.h"

namespace cosmic {

/// Oriented box (rotated rectangle) used for rotated hazards and decor hit areas.
struct OrientedBox {
    Vec2 center;
    Vec2 halfSize;
    float angle = 0.0f;  // radians
};

namespace Collision {

/// Minimum translation to push `a` out of `b` (zero if not overlapping).
Vec2 penetration(const Rect& a, const Rect& b);

/// Separating-axis test between an axis-aligned rect and an oriented box.
bool overlaps(const Rect& a, const OrientedBox& b);

bool segmentIntersectsRect(Vec2 p0, Vec2 p1, const Rect& r, float* tHit = nullptr);

bool circleIntersectsRect(Vec2 c, float radius, const Rect& r);

}  // namespace Collision

}  // namespace cosmic
