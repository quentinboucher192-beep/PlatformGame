#pragma once

// 2.75: the spectator mode of a raid (docs/RAIDS.md §20). A group wiped (every player down at once) is
// out of its zone for good: its players only watch — a free camera over the zone, or the view of a
// player still playing in their branch (next / previous). Display only: nothing here reaches the
// simulation (a spectator's socket never holds a seat, NetCoopLink::kWatcherSeat).

#include <algorithm>
#include <string>
#include <vector>

#include "engine/camera/Camera.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"

namespace cosmic::raidspec {

/// A player still playing in the branch (GET /raids/instances/{id}/watch).
struct Target {
    std::string instance, name, clan;
    int seat = 0, group = 1, zone = 0, startZone = 0;
};

struct Spectator {
    bool on = false;
    bool free = true;   // the free camera, else the view of targets[index]
    int index = -1;     // -1: the free camera
    Vec2 center;
    float zoom = 0.8f;  // of the free camera (the wheel and the triggers change it)
    std::vector<Target> targets;
    float fetchedAt = -100.0f;

    void start(Vec2 at) {
        on = true;
        free = true;
        index = -1;
        center = at;
    }

    /// The free camera moved by the held directions (keyboard, stick), zoomed by the wheel; kept over `bounds`.
    void move(const InputManager& in, float dt, const Rect& bounds) {
        Vec2 d;
        if (in.held(Action::MoveLeft)) d.x -= 1.0f;
        if (in.held(Action::MoveRight)) d.x += 1.0f;
        if (in.held(Action::MoveUp)) d.y -= 1.0f;
        if (in.held(Action::MoveDown)) d.y += 1.0f;
        const float speed = 520.0f / zoom;
        center = center + d * (speed * dt);
        if (in.wheel() != 0.0f) zoom = std::clamp(zoom * (in.wheel() > 0.0f ? 1.1f : 1.0f / 1.1f), 0.42f, 1.6f);
        if (bounds.w > 0.0f && bounds.h > 0.0f) {
            center.x = std::clamp(center.x, bounds.x, bounds.right());
            center.y = std::clamp(center.y, bounds.y, bounds.bottom());
        }
    }

    /// The view of the free camera on a screen of `w` × `h` pixels.
    WorldView view(int w, int h) const {
        WorldView v;
        v.screenW = std::max(1, w);
        v.screenH = std::max(1, h);
        v.scale = static_cast<float>(v.screenH) / Camera::kViewHeight * zoom;
        v.topLeft = center - Vec2{static_cast<float>(v.screenW), static_cast<float>(v.screenH)} * (0.5f / v.scale);
        return v;
    }

    /// The next (+1) or previous (−1) view: the free camera, then each target in turn (wraps).
    int step(int dir) {
        const int n = static_cast<int>(targets.size());
        if (n == 0) {
            index = -1;
            free = true;
            return -1;
        }
        index = ((index + 1 + dir) % (n + 1) + (n + 1)) % (n + 1) - 1;
        free = index < 0;
        return index;
    }
};

}  // namespace cosmic::raidspec
