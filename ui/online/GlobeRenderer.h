#pragma once

#include <vector>

#include <SDL3/SDL.h>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "game/online/OnlineGlobe.h"

namespace cosmic {

class InputManager;
class Renderer;
class ResourceManager;
class UIContext;

/// Draws the Earth of Cosmic Online with SDL_RenderGeometry: a latitude / longitude mesh
/// projected orthographically (back-facing vertices are pinned to the limb, giving a
/// perfectly round silhouette), day texture lit by the real position of the sun, city
/// lights on the night side, atmosphere and markers. No 3D engine involved.
class GlobeRenderer {
public:
    void init(ResourceManager& res);
    bool ready() const { return day_ != nullptr; }

    void renderBackdrop(Renderer& r, const GlobeView& view, const GlobeCamera& cam, float time) const;
    void renderEarth(Renderer& r, const GlobeView& view, const GlobeCamera& cam, GeoPoint sun);
    void renderAtmosphere(Renderer& r, const GlobeView& view, const GlobeCamera& cam, GeoPoint sun, float time) const;

    static Color tierColor(MarkerTier t);
    /// A marker (a record, a cluster, a country): soft halo, dark outline, coloured dot and
    /// white core, readable on the day side as on the city lights of the night side.
    /// `scale` comes from markerScale() (hover / selection may enlarge it), `fade` from
    /// markerFade(); `pulse` (0..1) animates the halo. Returns the radius of the dot.
    float renderMarker(Renderer& r, Vec2 pos, Color color, float scale, float pulse, bool selected, bool me, float fade) const;
    float renderMarker(Renderer& r, Vec2 pos, MarkerTier tier, float scale, float pulse, bool selected, bool me, float fade) const {
        return renderMarker(r, pos, tierColor(tier), scale, pulse, selected, me, fade);
    }
    /// The size of a group (records of a cluster, players of a country; nothing for 1) in
    /// a pill at the right of a dot of radius `dotRadius`, or at its left: measured, never
    /// over the dot, never out of `clip`, never over a rectangle of `taken` (dots, other
    /// pills, panels), to which it is added. Without free room nothing is drawn (the
    /// number stays in the tooltip). Returns its rectangle (empty when not drawn).
    static Rect renderCount(UIContext& ui, Vec2 pos, float dotRadius, int count, Color accent, float fade, const Rect& clip,
                            std::vector<Rect>* taken = nullptr);
    /// Expanding ring (new record glow).
    static void renderShockwave(Renderer& r, Vec2 pos, float t, Color c);

    /// Pointer and gamepad control shared by the globes (the keyboard uses the buttons
    /// drawn beside them): drag to turn, wheel / triggers to zoom, right stick to turn.
    struct Input {
        bool pressing = false;
        bool dragged = false;
        Vec2 pressPos;
        bool touched = false;  // the player moved the globe this frame
    };
    /// Returns true on a click (left button released without dragging) inside `zone`, at
    /// the mouse position. `blocked`: the pointer is over a panel or button of the zone.
    static bool handleInput(InputManager& in, Input& st, GlobeCamera& cam, const GlobeView& view, const Rect& zone, bool blocked, float dt,
                            float dragThreshold);

private:
    TexturePtr day_;
    TexturePtr night_;
    std::vector<SDL_Vertex> verts_;
    std::vector<SDL_Vertex> nightVerts_;
    std::vector<int> idx_;
    std::vector<Vec3d> unit_;  // cached grid directions
    int cols_ = 0, rows_ = 0;
};

}  // namespace cosmic
