#include "ui/widgets/AsterionFigure.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "ui/online/RaidWidgets.h"

namespace cosmic {

namespace {

using namespace raidui;

struct Data {
    asterion::Sprite sprite;
    std::vector<asterion::Part> parts;
};

/// The sprite's manifest and the parts, read once.
const Data& data(GameServices& g) {
    static Data d;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (auto j = g.app.resources().json("ui/raids/asterion.json")) d.sprite = asterion::parseSprite(*j);
        if (auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion.json")) d.parts = asterion::parseParts(*j);
    }
    return d;
}

const asterion::Part* partOf(const Data& d, const std::string& id) {
    for (const asterion::Part& p : d.parts)
        if (p.id == id) return &p;
    return nullptr;
}

void pillColors(asterion::State s, Color& fill, Color& text) {
    switch (s) {
        case asterion::State::Intact: fill = {31, 94, 58, 255}, text = {184, 245, 204, 255}; break;
        case asterion::State::Attacked: fill = {122, 74, 26, 255}, text = {255, 214, 160, 255}; break;
        case asterion::State::Weakened: fill = {122, 26, 26, 255}, text = kText; break;
        case asterion::State::Destroyed: fill = {52, 42, 74, 255}, text = kDim; break;
        case asterion::State::Unknown: fill = {42, 58, 122, 255}, text = kText; break;
    }
}

}  // namespace

std::string AsterionFigure::draw(GameServices& g, const std::string& id, const Rect& r, const std::map<std::string, asterion::State>& states) {
    tip_.clear();
    const Data& d = data(g);
    UIContext& ui = g.ui;
    if (!d.sprite.valid || r.w <= 0.0f || r.h <= 0.0f) return {};
    TexturePtr tex = g.app.resources().texture(d.sprite.sheet);
    if (!tex) return {};
    const auto& layers = d.sprite.layers;
    const float cw = static_cast<float>(d.sprite.canvasW), ch = static_cast<float>(d.sprite.canvasH);
    float k = std::min(r.w / cw, r.h / ch);
    if (k >= 1.0f) k = std::floor(k);  // pixel-perfect once enlarged
    if (k <= 0.0f) return {};
    const float ox = std::round(r.x + (r.w - cw * k) * 0.5f), oy = std::round(r.y + (r.h - ch * k) * 0.5f);
    const float t = ui.time();
    std::vector<int> lifts(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) lifts[i] = asterion::breathLift(layers[i], t);

    // The part under the mouse (its pixels), the part focused (its stop), the part activated.
    int hover = -1, focused = -1;
    std::string activated;
    const Rect fig{ox, oy, cw * k, ch * k};
    if (ui.acceptsInput() && ui.mouseOver(fig)) {
        const Vec2 m = ui.input().mousePos();
        hover = asterion::hitTest(d.sprite, static_cast<int>(std::floor((m.x - ox) / k)), static_cast<int>(std::floor((m.y - oy) / k)), lifts);
        if (hover >= 0 && !partOf(d, layers[static_cast<size_t>(hover)].id)) hover = -1;
    }
    const float stop = std::max(ui.s(10), 3.0f * k);
    for (size_t i = 0; i < layers.size(); ++i) {
        const asterion::Layer& l = layers[i];
        if (!l.hasMask() || !partOf(d, l.id)) continue;
        const Vec2 c{ox + (static_cast<float>(l.anchorX) + 0.5f) * k, oy + (static_cast<float>(l.anchorY - lifts[i]) + 0.5f) * k};
        const std::string name = id + "/" + l.id;
        if (ui.hotspot(name, Rect::fromCenter(c, {stop, stop}))) activated = l.id;
        if (ui.focused(name)) focused = static_cast<int>(i);
    }
    bool overFigure = false;
    if (ui.mouseClick(id + "/clic", fig, &overFigure) && hover >= 0) activated = layers[static_cast<size_t>(hover)].id;
    const int shown = ui.usingPointer() ? hover : focused;

    // The layers, bottom to top.
    Renderer& rr = ui.r();
    const float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
    for (size_t i = 0; i < layers.size(); ++i) {
        const asterion::Layer& l = layers[i];
        const bool isPart = l.hasMask() && partOf(d, l.id);
        const asterion::State st = isPart ? asterion::stateOf(states, l.id) : asterion::State::Intact;
        const asterion::Visual v = asterion::visualOf(st);
        const asterion::Cell* cell = l.cellAt(v.look, t);
        if (!cell) continue;
        const Rect dst{ox + static_cast<float>(l.x) * k, oy + static_cast<float>(l.y - lifts[i]) * k, static_cast<float>(l.w) * k,
                       static_cast<float>(l.h) * k};
        const Rect src{static_cast<float>(cell->x), static_cast<float>(cell->y), static_cast<float>(l.w), static_cast<float>(l.h)};
        if (l.id == "coeur" && v.glow > 0.0f) {  // the heart's light on the chest, beating with it
            const float beat = 0.5f + 0.5f * std::sin(t * l.fps * 3.14159265f * 0.5f);
            rr.drawGlow({ox + (static_cast<float>(l.anchorX) + 0.5f) * k, oy + (static_cast<float>(l.anchorY - lifts[i]) + 0.5f) * k}, 11.0f * k,
                        Color{255, 140, 60, static_cast<uint8_t>((40.0f + 50.0f * beat) * v.glow)});
        }
        rr.drawTexture(*tex, &src, dst);
        const asterion::Cell* rim = isPart ? l.cellAt("contour", 0.0f) : nullptr;
        const Rect rimSrc = rim ? Rect{static_cast<float>(rim->x), static_cast<float>(rim->y), src.w, src.h} : Rect{};
        if (rim && v.attackRim) rr.drawTexture(*tex, &rimSrc, dst, kOrange.withAlphaF(0.35f + 0.65f * pulse));
        if (rim && static_cast<int>(i) == shown) {
            rr.drawTexture(*tex, &src, dst, Color{255, 255, 255, 70}, BlendMode::Additive);
            rr.drawTexture(*tex, &rimSrc, dst, kGold);
        }
    }
    if (shown >= 0) {
        const asterion::Layer& l = layers[static_cast<size_t>(shown)];
        tip_ = l.id;
        tipState_ = asterion::stateOf(states, l.id);
        tipFigure_ = fig;
        tipAnchor_ = {ox + (static_cast<float>(l.anchorX) + 0.5f) * k, oy + (static_cast<float>(l.anchorY) + 0.5f) * k};
    }
    return activated;
}

void AsterionFigure::drawTooltip(GameServices& g) {
    const std::string part = tip_;
    tip_.clear();
    const asterion::Part* p = part.empty() ? nullptr : partOf(data(g), part);
    if (!p) return;
    UIContext& ui = g.ui;
    Renderer& rr = ui.r();
    const asterion::Tooltip tt = asterion::tooltipOf(*p, tipState_);
    const float W = ui.screenW(), H = ui.screenH(), margin = ui.s(8);
    const float w = std::min(ui.s(360), W - 2 * margin);
    const float pad = ui.s(14), inner = w - 2 * pad;
    // Measured first: the box is as high as its texts.
    const auto effect = wrap(ui, tt.effect, FontFace::Body, 15, inner, 4);
    const float titleH = ui.s(30), bossH = tt.boss.empty() ? 0.0f : ui.s(24), stateH = ui.s(34), lineH = ui.s(21);
    const float effectH = effect.empty() ? 0.0f : ui.s(4) + lineH * static_cast<float>(effect.size());
    const float h = pad + titleH + bossH + stateH + effectH + pad;
    // Beside the figure (left, else right, else over it), level with the part; always on screen.
    float x = tipFigure_.x - ui.s(12) - w;
    if (x < margin) x = tipFigure_.right() + ui.s(12);
    if (x + w > W - margin) x = std::clamp(tipAnchor_.x + ui.s(16), margin, std::max(margin, W - margin - w));
    const float y = std::clamp(tipAnchor_.y - h * 0.5f, margin, std::max(margin, H - margin - h));
    const Rect box{x, y, w, h};
    Color fill, text;
    pillColors(tipState_, fill, text);
    const Color border = tipState_ == asterion::State::Attacked ? kOrange : tipState_ == asterion::State::Intact ? kGold : Color{120, 110, 170, 255};
    rr.fillRoundedRect(box.expanded(ui.s(1)), ui.s(13), Color{2, 4, 10, 200});
    rr.fillRoundedRect(box, ui.s(12), Color{12, 16, 38, 250});
    rr.drawRoundedRect(box, ui.s(12), border, std::max(1.0f, ui.s(1.6f)));
    const float tx = box.x + pad;
    float ty = box.y + pad;
    fit(ui, tt.title, {tx, ty + titleH * 0.5f}, 20, inner, kText, FontFace::Title);
    ty += titleH;
    if (!tt.boss.empty()) {
        fit(ui, "Gigaboss : " + tt.boss, {tx, ty + bossH * 0.5f}, 15, inner, kPeach, FontFace::Bold);
        ty += bossH;
    }
    if (pill(ui, tx, ty + stateH * 0.5f, tt.state, fill, text, tx + inner, 13) == 0.0f) fit(ui, tt.state, {tx, ty + stateH * 0.5f}, 13, inner, text, FontFace::Bold);
    ty += stateH;
    if (!effect.empty()) drawLines(ui, effect, {tx, ty + ui.s(4)}, 15, lineH, kDim);
}

}  // namespace cosmic
