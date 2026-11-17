#include "game/gameplay/HudFade.h"

#include <algorithm>

namespace cosmic::HudFade {

namespace {
bool filled(const Rect& r) { return r.w > 0.0f && r.h > 0.0f; }
}  // namespace

bool hides(const Rect& block, const Rect& thing) { return filled(block) && filled(thing) && block.intersects(thing); }

bool covered(const Rect& block, const Rect& nova, const Rect& boss, bool attack, bool bossBar) {
    if (!filled(block)) return false;
    if (hides(block, nova)) return true;
    if (bossBar) return false;
    return attack || hides(block, boss);
}

float step(float opacity, bool covered, float dt) {
    const float target = covered ? kFaded : 1.0f;
    const float pace = (1.0f - kFaded) / kSeconds * std::max(0.0f, dt);
    if (opacity > target) return std::max(target, opacity - pace);
    return std::min(target, opacity + pace);
}

Rect visiblePart(const Rect& dst, Vec2 frame, const Rect& seen, bool flipX) {
    if (!filled(seen) || frame.x <= 0.0f || frame.y <= 0.0f) return dst;
    const float kx = dst.w / frame.x, ky = dst.h / frame.y;
    const float x = flipX ? frame.x - seen.right() : seen.x;
    return {dst.x + x * kx, dst.y + seen.y * ky, seen.w * kx, seen.h * ky};
}

}  // namespace cosmic::HudFade
