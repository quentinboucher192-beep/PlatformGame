#pragma once

#include <vector>

#include <SDL3/SDL.h>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "game/online/OnlineGlobe.h"

namespace cosmic {

class Renderer;
class ResourceManager;

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
    /// A marker or a cluster. `count` > 1 draws the group size; `pulse` animates it.
    void renderMarker(Renderer& r, Vec2 pos, MarkerTier tier, float scale, float pulse, bool selected, bool me, float depth) const;
    /// Expanding ring (new record glow).
    static void renderShockwave(Renderer& r, Vec2 pos, float t, Color c);

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
