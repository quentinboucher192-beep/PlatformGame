#include "ui/screens/MenuBackground.h"

#include <cmath>

#include "game/GameServices.h"

namespace cosmic {

void MenuBackground::init(GameServices& g) {
    std::vector<ParallaxLayerDef> layers;
    auto add = [&](const char* tex, float fx, float fy, bool fill = false, bool fg = false) {
        ParallaxLayerDef d;
        d.texture = tex;
        d.factor = {fx, fy};
        d.fill = fill;
        d.foreground = fg;
        layers.push_back(d);
    };
    add("backgrounds/sylvanox/sky.png", 0.0f, 0.0f, true);
    add("backgrounds/sylvanox/far.png", 0.12f, 0.0f);
    add("backgrounds/sylvanox/mid.png", 0.3f, 0.0f);
    add("backgrounds/sylvanox/abyss.png", 0.45f, 0.0f);
    add("backgrounds/sylvanox/near.png", 0.6f, 0.0f);
    add("backgrounds/sylvanox/fg_vines.png", 1.1f, 0.0f, false, true);
    parallax_.load(g.app.resources(), layers, 0.0f);
    motes_.clear();
    for (int i = 0; i < 90; ++i) {
        Mote m;
        m.pos = {rng_.range(0.0f, 1.0f), rng_.range(0.0f, 1.0f)};
        m.vel = {rng_.range(-0.004f, 0.006f), rng_.range(-0.012f, -0.003f)};
        m.size = rng_.range(2.0f, 7.0f);
        m.phase = rng_.range(0.0f, 6.28f);
        m.color = rng_.chance(0.75f) ? Color{57, 240, 216, 255} : (rng_.chance(0.5f) ? Color{176, 76, 255, 255} : Color{255, 179, 71, 255});
        motes_.push_back(m);
    }
    ready_ = true;
}

void MenuBackground::update(float dt) {
    time_ += dt;
    for (auto& m : motes_) {
        m.pos += m.vel * dt;
        m.pos.x += std::sin(time_ * 0.7f + m.phase) * 0.004f * dt;
        if (m.pos.y < -0.05f) m.pos.y = 1.05f, m.pos.x = rng_.range(0.0f, 1.0f);
        if (m.pos.x < -0.05f) m.pos.x = 1.05f;
        if (m.pos.x > 1.05f) m.pos.x = -0.05f;
    }
}

void MenuBackground::render(GameServices& g, float dim) {
    if (!ready_) init(g);
    Renderer& r = g.app.renderer();
    float w = static_cast<float>(r.width()), h = static_cast<float>(r.height());
    WorldView v;
    v.screenW = r.width();
    v.screenH = r.height();
    v.scale = h / 300.0f;
    float camX = time_ * 14.0f;
    float camY = std::sin(time_ * 0.07f) * 10.0f;
    v.topLeft = {camX - w / v.scale * 0.5f, camY - h / v.scale * 0.5f};
    r.fillRect({0, 0, w, h}, {11, 15, 30, 255});
    parallax_.render(r, v, false);
    for (const auto& m : motes_) {
        float tw = 0.6f + 0.4f * std::sin(time_ * 2.2f + m.phase);
        Vec2 p{m.pos.x * w, m.pos.y * h};
        r.drawGlow(p, m.size * 4.0f * (h / 1080.0f), m.color.withAlphaF(0.35f * tw));
        r.fillCircle(p, m.size * 0.45f * (h / 1080.0f) + 1.0f, m.color.withAlphaF(0.9f * tw), BlendMode::Additive);
    }
    parallax_.render(r, v, true);
    // Vignette
    r.fillGradientV({0, 0, w, h * 0.35f}, {4, 6, 14, 170}, {4, 6, 14, 0});
    r.fillGradientV({0, h * 0.6f, w, h * 0.4f}, {4, 6, 14, 0}, {4, 6, 14, 200});
    if (dim > 0.0f) r.fillRect({0, 0, w, h}, Color{4, 6, 14, 255}.withAlphaF(dim));
}

void drawGameTitle(GameServices& g, Vec2 center, float size, float alpha) {
    UIContext& ui = g.ui;
    TextStyle st;
    st.face = FontFace::Title;
    st.size = ui.s(size);
    st.color = Color::white().withAlphaF(alpha);
    st.align = Align::Center;
    st.valign = VAlign::Middle;
    st.glow = ui.s(size * 0.6f);
    st.glowColor = Color{57, 240, 216, 70}.withAlphaF(alpha);
    st.shadow = true;
    st.shadowOffset = ui.s(4);
    ui.text().draw("COSMIC", {center.x, center.y - ui.s(size * 0.55f)}, st);
    st.size = ui.s(size * 0.62f);
    st.color = Color{57, 240, 216, 255}.withAlphaF(alpha);
    st.glow = ui.s(size * 0.4f);
    ui.text().draw("PLATFORMER", {center.x, center.y + ui.s(size * 0.45f)}, st);
}

}  // namespace cosmic
