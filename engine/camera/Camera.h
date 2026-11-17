#pragma once

#include <optional>

#include "engine/core/Easing.h"
#include "engine/core/Math.h"
#include "engine/core/Random.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

/// A rectangular area forcing a camera behaviour while the target is inside.
struct CameraZone {
    Rect area;
    bool lockCenter = false;  // fixed camera on `center`
    Vec2 center;
    bool clampToArea = true;  // keep the view inside `area`
    float zoom = 0.0f;        // 0 = keep current
    bool fitArea = false;     // zoom out until the whole area is in view (boss arenas)
};

/// Professional 2D follow camera: smoothed follow with look-ahead, vertical dead zone,
/// level bounds, camera zones, zoom, trauma shake and scripted (cinematic) moves.
class Camera {
public:
    // Base view size in world pixels at zoom 1 (the art is designed for 480x270).
    static constexpr float kViewWidth = 480.0f;
    static constexpr float kViewHeight = 270.0f;
    /// Least zoom used to fit an area (beyond it, the camera follows inside the area).
    static constexpr float kMinFitZoom = 0.42f;

    void reset(Vec2 center);
    void setBounds(const Rect& b) { bounds_ = b; hasBounds_ = b.w > 0 && b.h > 0; }
    void clearBounds() { hasBounds_ = false; }

    /// Follow target: position of the player's centre, its velocity and facing.
    void setTarget(Vec2 pos, Vec2 velocity, float facing, bool grounded);
    /// Offset of the framing from the target (2.33: in a giant boss arena, toward the boss).
    void setLean(Vec2 lean) { lean_ = lean; }
    void setZone(const CameraZone* zone);

    // Scripted control (intro/outro cinematics, event pans).
    void moveTo(Vec2 center, float duration, Ease ease = Ease::InOutCubic);
    void zoomTo(float zoom, float duration, Ease ease = Ease::InOutCubic);
    void releaseScript() { scripted_ = false; }
    bool scripted() const { return scripted_; }
    bool scriptBusy() const;

    void addTrauma(float amount);
    void setShakeEnabled(float scale) { shakeScale_ = scale; }
    void setZoom(float z) { zoom_ = baseZoom_ = z; }
    float zoom() const { return zoom_; }

    void fixedUpdate(float dt);
    void update(float dt);

    Vec2 center() const { return pos_; }
    /// Interpolated view for rendering.
    WorldView view(int screenW, int screenH, float alpha) const;
    Rect visibleArea(int screenW, int screenH) const;
    /// Zoom showing the whole of `area` on the current screen shape.
    float fitZoom(const Rect& area) const;

    /// 2.60, display only: the screen pixels the HUD covers at the top and at the bottom. In a
    /// boss arena that fits the view (CameraZone::fitArea), its spare height goes there: the
    /// arena is centred in the band between them, as near as it can without leaving the view
    /// (never cut, never zoomed out for it, never beyond the level). An arena taller than the view
    /// (a giga boss's, followed at the widest) is framed as before. Eased in fixedUpdate().
    void setScreenMargins(float top, float bottom) {
        marginTop_ = std::max(0.0f, top);
        marginBottom_ = std::max(0.0f, bottom);
    }
    /// The view's centre (y) showing `area` (world; no taller than the view) in the band between
    /// `top` and `bottom` screen pixels of margin: its middle in the band's middle, or as near as
    /// the whole area stays in view. `halfH`: half the view's height (world), `scale`: screen
    /// pixels per world pixel.
    static float bandCenterY(const Rect& area, float halfH, float scale, float top, float bottom);

private:
    Vec2 clampCenter(Vec2 c, float zoom, int screenW, int screenH) const;
    Vec2 desiredCenter() const;

    Vec2 pos_, prevPos_;
    Vec2 target_, targetVel_;
    float facing_ = 1.0f;
    bool grounded_ = true;
    Vec2 lean_;
    float lookAhead_ = 0.0f;
    float focusY_ = 0.0f;
    Rect bounds_;
    bool hasBounds_ = false;
    std::optional<CameraZone> zone_;

    bool scripted_ = false;
    Vec2 scriptFrom_, scriptTo_;
    float scriptT_ = 0.0f, scriptDur_ = 0.0f;
    Ease scriptEase_ = Ease::InOutCubic;
    float zoom_ = 1.0f, baseZoom_ = 1.0f;
    float zoomFrom_ = 1.0f, zoomTo_ = 1.0f, zoomT_ = 0.0f, zoomDur_ = 0.0f;
    Ease zoomEase_ = Ease::InOutCubic;

    float trauma_ = 0.0f;
    float shakeScale_ = 1.0f;
    float shakeTime_ = 0.0f;
    Vec2 shakeOffset_;
    mutable int lastScreenW_ = 1920, lastScreenH_ = 1080;
    float marginTop_ = 0.0f, marginBottom_ = 0.0f;  // asked (screen pixels)
    float shownTop_ = 0.0f, shownBottom_ = 0.0f;    // eased toward them
};

}  // namespace cosmic
