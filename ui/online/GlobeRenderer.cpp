#include "ui/online/GlobeRenderer.h"

#include <algorithm>
#include <cmath>

#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"

namespace cosmic {

namespace {
constexpr int kCols = 96;  // 3.75 degrees
constexpr int kRows = 48;

float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

SDL_FColor fcol(float r, float g, float b, float a) { return {r, g, b, a}; }
}  // namespace

void GlobeRenderer::init(ResourceManager& res) {
    day_ = res.texture("online/earth_day.png", false);
    night_ = res.texture("online/earth_night.png", false);
    if (day_ && day_->width() <= 64) day_.reset();  // the "missing texture" placeholder
    cols_ = kCols;
    rows_ = kRows;
    unit_.clear();
    for (int y = 0; y <= rows_; ++y)
        for (int x = 0; x <= cols_; ++x)
            unit_.push_back(geoToUnit({90.0 - 180.0 * y / rows_, -180.0 + 360.0 * x / cols_}));
}

void GlobeRenderer::renderBackdrop(Renderer& r, const GlobeView& view, const GlobeCamera& cam, float time) const {
    float R = view.radius(cam);
    // Soft halo of the planet in space.
    float breathe = 1.0f + 0.03f * std::sin(time * 0.8f);
    r.drawGlow(view.center, R * 1.65f * breathe, Color{40, 120, 255, 70});
    r.drawGlow(view.center, R * 1.25f, Color{57, 240, 216, 60});
}

void GlobeRenderer::renderEarth(Renderer& r, const GlobeView& view, const GlobeCamera& cam, GeoPoint sun) {
    float R = view.radius(cam);
    if (!day_) {
        // Without the textures (corrupt installation) a plain ocean sphere still works.
        r.fillCircle(view.center, R, Color{18, 50, 110, 255});
        return;
    }
    Vec3d sunDir = geoToUnit(sun);
    size_t n = unit_.size();
    verts_.resize(n);
    nightVerts_.resize(n);
    std::vector<double> depth(n);
    for (int y = 0; y <= rows_; ++y) {
        for (int x = 0; x <= cols_; ++x) {
            size_t i = static_cast<size_t>(y * (cols_ + 1) + x);
            const Vec3d& w = unit_[i];
            Vec3d v = cam.toView(w);
            depth[i] = v.z;
            float sx, sy;
            if (v.z >= 0.0) {
                sx = view.center.x + R * static_cast<float>(v.x);
                sy = view.center.y - R * static_cast<float>(v.y);
            } else {
                // Behind the globe: pinned on the limb so edge triangles end exactly on it.
                double l = std::sqrt(v.x * v.x + v.y * v.y);
                double k = l > 1e-6 ? 1.0 / l : 0.0;
                sx = view.center.x + R * static_cast<float>(v.x * k);
                sy = view.center.y - R * static_cast<float>(v.y * k);
            }
            float light = static_cast<float>(w.dot(sunDir));
            float day = smooth(-0.12f, 0.22f, light);
            // Limb darkening and a cool night tint.
            float limb = 0.72f + 0.28f * static_cast<float>(std::max(0.0, v.z));
            float rr = (0.16f + 0.84f * day) * limb;
            float gg = (0.20f + 0.80f * day) * limb;
            float bb = (0.34f + 0.66f * day) * limb;
            float u = static_cast<float>(x) / cols_;
            float t = static_cast<float>(y) / rows_;
            verts_[i] = {{sx, sy}, fcol(rr, gg, bb, 1.0f), {u, t}};
            float night = std::pow(1.0f - day, 1.4f) * (0.55f + 0.45f * limb);
            nightVerts_[i] = {{sx, sy}, fcol(night, night, night, 1.0f), {u, t}};
        }
    }
    idx_.clear();
    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            int a = y * (cols_ + 1) + x, b = a + 1, c = a + cols_ + 1, d = c + 1;
            // Keep quads with at least one vertex facing the viewer.
            if (depth[a] < 0 && depth[b] < 0 && depth[c] < 0 && depth[d] < 0) continue;
            idx_.insert(idx_.end(), {a, b, d, a, d, c});
        }
    }
    r.drawGeometry(day_.get(), verts_, idx_, BlendMode::Alpha);
    if (night_) r.drawGeometry(night_.get(), nightVerts_, idx_, BlendMode::Additive);
}

void GlobeRenderer::renderAtmosphere(Renderer& r, const GlobeView& view, const GlobeCamera& cam, GeoPoint sun, float) const {
    float R = view.radius(cam);
    // Rim of light: a ring mesh fading outwards, brighter on the sunlit side.
    Vec3d sv = cam.toView(geoToUnit(sun));
    Vec2 sun2{static_cast<float>(sv.x), static_cast<float>(-sv.y)};
    float sunLen = std::sqrt(sun2.x * sun2.x + sun2.y * sun2.y);
    const int seg = 128;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve(seg * 3);
    for (int i = 0; i < seg; ++i) {
        float a = static_cast<float>(i) / seg * 6.2831853f;
        Vec2 dir{std::cos(a), std::sin(a)};
        float lit = sunLen > 1e-4f ? (dir.x * sun2.x + dir.y * sun2.y) / sunLen : 0.0f;
        float boost = 0.55f + 0.45f * std::clamp(lit * 0.5f + 0.5f + static_cast<float>(sv.z) * 0.3f, 0.0f, 1.0f);
        Vec2 in = view.center + dir * (R * 0.94f);
        Vec2 mid = view.center + dir * (R * 1.005f);
        Vec2 out = view.center + dir * (R * 1.075f);
        v.push_back({{in.x, in.y}, fcol(0.25f, 0.75f, 1.0f, 0.0f), {0, 0}});
        v.push_back({{mid.x, mid.y}, fcol(0.35f, 0.85f, 1.0f, 0.55f * boost), {0, 0}});
        v.push_back({{out.x, out.y}, fcol(0.2f, 0.45f, 1.0f, 0.0f), {0, 0}});
    }
    for (int i = 0; i < seg; ++i) {
        int j = (i + 1) % seg;
        int a = i * 3, b = j * 3;
        idx.insert(idx.end(), {a, b, a + 1, b, b + 1, a + 1, a + 1, b + 1, a + 2, b + 1, b + 2, a + 2});
    }
    r.drawGeometry(nullptr, v, idx, BlendMode::Additive);
}

Color GlobeRenderer::tierColor(MarkerTier t) {
    switch (t) {
        case MarkerTier::Gold: return {255, 206, 84, 255};
        case MarkerTier::Silver: return {214, 226, 240, 255};
        case MarkerTier::Bronze: return {222, 140, 78, 255};
        case MarkerTier::Cyan: return {57, 240, 216, 255};
    }
    return {57, 240, 216, 255};
}

void GlobeRenderer::renderMarker(Renderer& r, Vec2 pos, MarkerTier tier, float scale, float pulse, bool selected, bool me, float depth) const {
    Color c = tierColor(tier);
    // Markers near the limb fade and shrink (they are seen at a grazing angle).
    float edge = std::clamp(depth * 3.0f, 0.25f, 1.0f);
    float s = scale * (0.6f + 0.4f * edge);
    float glow = (selected ? 26.0f : 15.0f) * s * (1.0f + 0.18f * pulse);
    r.drawGlow(pos, glow, c.withAlphaF((selected ? 0.75f : 0.45f) * edge));
    r.fillCircle(pos, 4.2f * s, c.withAlphaF(edge));
    r.fillCircle(pos, 2.0f * s, Color{255, 255, 255, static_cast<uint8_t>(230 * edge)});
    if (me) r.drawRing(pos, 8.0f * s, 1.8f * s, Color{176, 76, 255, static_cast<uint8_t>(230 * edge)});
    if (selected) {
        r.drawRing(pos, (10.0f + 4.0f * pulse) * s, 2.0f * s, Color{255, 255, 255, 220});
        r.drawLine(pos + Vec2{0, -14 * s}, pos + Vec2{0, -34 * s}, c.withAlphaF(0.8f), 2.0f);
    }
}

void GlobeRenderer::renderShockwave(Renderer& r, Vec2 pos, float t, Color c) {
    if (t < 0.0f || t > 1.0f) return;
    float rad = 12.0f + 90.0f * t;
    r.drawRing(pos, rad, 3.0f * (1.0f - t) + 1.0f, c.withAlphaF(1.0f - t));
    r.drawGlow(pos, rad * 0.8f, c.withAlphaF(0.5f * (1.0f - t)));
}

}  // namespace cosmic
