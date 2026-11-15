#include "ui/online/GlobeRenderer.h"

#include <algorithm>
#include <cmath>

#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "ui/widgets/UIContext.h"

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

float GlobeRenderer::renderMarker(Renderer& r, Vec2 pos, Color c, float scale, float pulse, bool selected, bool me, float fade) const {
    if (fade <= 0.0f) return 0.0f;
    // Towards the limb a marker is seen at a grazing angle: a little smaller, then gone.
    float s = scale * (0.75f + 0.25f * fade);
    float dot = 4.6f * s;
    auto a8 = [&](float a) { return static_cast<uint8_t>(std::clamp(a * fade, 0.0f, 1.0f) * 255.0f); };
    // Wide soft halo, then a tight bright one: the dot catches the eye on the textured Earth.
    float halo = (selected ? 30.0f : 21.0f) * s * (1.0f + 0.16f * pulse);
    r.drawGlow(pos, halo, c.withAlphaF((selected ? 0.85f : 0.6f) * fade));
    r.drawGlow(pos, dot * 2.3f, c.withAlphaF(0.75f * fade));
    // Dark outline: the colour stays readable on deserts, ice and city lights alike.
    r.fillCircle(pos, dot + 1.7f * s, Color{4, 8, 20, a8(0.82f)});
    r.fillCircle(pos, dot, c.withAlphaF(fade));
    r.fillCircle(pos, dot * 0.45f, Color{255, 255, 255, a8(0.95f)});
    if (me) r.drawRing(pos, dot + 4.0f * s, 1.8f * s, Color{176, 76, 255, a8(0.95f)});
    if (selected) {
        r.drawRing(pos, dot + (6.0f + 4.0f * pulse) * s, 2.0f * s, Color{255, 255, 255, a8(0.9f)});
        r.drawLine(pos + Vec2{0, -dot - 8 * s}, pos + Vec2{0, -dot - 26 * s}, c.withAlphaF(0.8f * fade), 2.0f);
    }
    return dot;
}

Rect GlobeRenderer::renderCount(UIContext& ui, Vec2 pos, float dotRadius, int count, Color accent, float fade, const Rect& clip,
                                std::vector<Rect>* taken) {
    if (count <= 1 || fade <= 0.05f) return {};
    std::string t = count > 999 ? "999+" : std::to_string(count);
    // Readable down to 1280 x 720 (about 11 px of text in a 16 px pill).
    const float fs = 16.0f;
    float h = ui.s(24);
    float w = std::max(h, ui.text().measure(t, FontFace::Bold, ui.s(fs)).x + ui.s(12));
    float gap = ui.s(4);
    // At the right of the dot, or at its left near the edge of the zone (never over it).
    Rect right{pos.x + dotRadius + gap, pos.y - h * 0.5f, w, h};
    Rect left{pos.x - dotRadius - gap - w, pos.y - h * 0.5f, w, h};
    auto fits = [&](const Rect& b) {
        if (b.x < clip.x || b.y < clip.y || b.right() > clip.right() || b.bottom() > clip.bottom()) return false;
        if (taken)
            for (const Rect& o : *taken)
                if (b.intersects(o)) return false;
        return true;
    };
    Rect b = fits(right) ? right : fits(left) ? left : Rect{};
    if (b.w <= 0.0f) return {};  // no free room: the count stays in the tooltip / info card
    if (taken) taken->push_back(b);
    ui.r().fillRoundedRect(b, h * 0.5f, Color{8, 12, 26, static_cast<uint8_t>(230 * fade)});
    ui.r().drawRoundedRect(b, h * 0.5f, accent.withAlphaF(0.9f * fade), ui.s(1.3f));
    ui.label(t, b.center(), fs, Color{255, 255, 255, static_cast<uint8_t>(245 * fade)}, Align::Center, FontFace::Bold, VAlign::Middle);
    return b;
}

void GlobeRenderer::renderShockwave(Renderer& r, Vec2 pos, float t, Color c) {
    if (t < 0.0f || t > 1.0f) return;
    float rad = 12.0f + 90.0f * t;
    r.drawRing(pos, rad, 3.0f * (1.0f - t) + 1.0f, c.withAlphaF(1.0f - t));
    r.drawGlow(pos, rad * 0.8f, c.withAlphaF(0.5f * (1.0f - t)));
}

bool GlobeRenderer::handleInput(InputManager& in, Input& st, GlobeCamera& cam, const GlobeView& view, const Rect& zone, bool blocked, float dt,
                                float dragThreshold) {
    st.touched = false;
    Vec2 mp = in.mousePos();
    bool inside = zone.contains(mp) && !blocked;
    if (inside && in.wheel() != 0.0f) {
        cam.cancelFlight();
        cam.zoomBy(std::pow(1.15, in.wheel()));
        st.touched = true;
    }
    if (in.mousePressed(SDL_BUTTON_LEFT) && inside) {
        st.pressing = true;
        st.dragged = false;
        st.pressPos = mp;
    }
    if (st.pressing && in.mouseDown(SDL_BUTTON_LEFT)) {
        Vec2 d = in.mouseDelta();
        if ((mp - st.pressPos).length() > dragThreshold) st.dragged = true;
        if (st.dragged && (d.x != 0.0f || d.y != 0.0f)) {
            float R = view.radius(cam);
            cam.rotate(d.x / R * 57.3, d.y / R * 57.3);
            st.touched = true;
        }
    }
    bool click = false;
    if (st.pressing && in.mouseReleased(SDL_BUTTON_LEFT)) {
        st.pressing = false;
        click = !st.dragged && zone.contains(mp);
    }
    // Gamepad: right stick turns the globe, triggers zoom.
    Vec2 rs = in.rightStick();
    if (std::abs(rs.x) > 0.05f || std::abs(rs.y) > 0.05f) {
        cam.rotate(rs.x * dt * 90.0 / cam.zoom, -rs.y * dt * 70.0 / cam.zoom);
        st.touched = true;
    }
    float zt = in.rightTrigger() - in.leftTrigger();
    if (std::abs(zt) > 0.1f) {
        cam.cancelFlight();
        cam.zoomBy(std::pow(1.9, zt * dt));
        st.touched = true;
    }
    return click;
}

}  // namespace cosmic
