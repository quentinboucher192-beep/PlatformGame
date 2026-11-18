#pragma once

// 2.67: the camera of a gigaboss's arena (docs/RAIDS.md §9), for every gigaboss of the raids
// (Pyroclast first, the six others later). Display only: it never touches the simulation, the
// world's own camera goes on as before and is used again outside the arena.
//
// * Entering the arena: an intro shot showing the WHOLE arena and the boss (a few seconds), then
//   a blend to
// * the fight's view: it follows this computer's player with a zoom between `minView` and
//   `maxView` (world pixels shown in height): wide enough to show what threatens the player (the
//   marks of the attacks aimed at them, the boss when it is near), the allies near them and the
//   platforms around; never wider (8 players spread over a giant arena: each sees enough to play
//   well, not the whole arena in tiny). The player always stays inside the view.
// * Whatever it does not show can still be found: the screen draws arrows on its border toward
//   the boss (its weak point when bare) and the allies out of view (arrowTargets).

#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"

namespace cosmic::raid {

class ArenaCamera {
public:
    /// What the camera is shown each frame (world pixels).
    struct Frame {
        Rect arena;              // the whole arena (w 0: no arena in this zone)
        Rect level;              // the level's bounds (never beyond)
        Vec2 me;                 // this computer's player (its centre)
        bool meInArena = false;  // in play, inside the arena
        Rect boss;               // the boss's box (w 0: none)
        std::vector<Vec2> threats;  // marks of the attacks aimed near me
        std::vector<Vec2> allies;   // the other players in play
    };
    struct Tuning {
        float intro = 4.0f;       // s of the whole arena
        float blend = 1.2f;       // s from the global shot to the fight's view
        float minView = 300.0f;   // world px shown in height, at the closest
        float maxView = 620.0f;   // ... and at the widest
        float margin = 110.0f;    // around me
        float threatReach = 380.0f;
        float allyReach = 420.0f;
        float bossReach = 760.0f;
    };

    ArenaCamera() = default;
    explicit ArenaCamera(const Tuning& t) : tune_(t) {}
    /// A new zone (or the zone again): the intro plays once more when entering its arena.
    void reset();
    /// One frame; `screenW`/`screenH` the screen's size (its shape).
    void update(const Frame& f, float dt, int screenW, int screenH);
    /// It drives the view now (inside the arena).
    bool active() const { return active_; }
    /// The intro shot (the whole arena) is playing, and its progress (0..1).
    bool intro() const { return active_ && introLeft_ > 0.0f; }
    float introProgress() const { return tune_.intro > 0.0f ? 1.0f - introLeft_ / tune_.intro : 1.0f; }
    /// Its view now (when active).
    WorldView view(int screenW, int screenH) const;
    /// The world rectangle shown now.
    Rect shown(int screenW, int screenH) const;
    Vec2 center() const { return center_; }
    float height() const { return height_; }
    const Tuning& tuning() const { return tune_; }

    /// The rectangle (world) the fight's view would frame now: me and what matters around.
    static Rect focus(const Frame& f, const Tuning& t);
    /// Height (world px) showing `r` whole on a screen of that shape.
    static float heightFor(const Rect& r, int screenW, int screenH);

private:
    Vec2 clamp(Vec2 c, float h, const Rect& level, int screenW, int screenH) const;

    Tuning tune_;
    bool active_ = false, seen_ = false;
    float introLeft_ = 0.0f, blendLeft_ = 0.0f;
    Vec2 center_;
    float height_ = 300.0f;
};

}  // namespace cosmic::raid
