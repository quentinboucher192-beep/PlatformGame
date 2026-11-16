#include "ui/screens/ProfileBadgesUi.h"

#include <algorithm>
#include <cmath>

#include "game/GameServices.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGold{255, 214, 90, 255};
constexpr float kBadgePi = 3.14159265f;

/// A filled five-pointed star.
void star(Renderer& r, Vec2 c, float radius, Color col) {
    Vec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -kBadgePi / 2.0f + static_cast<float>(i) * kBadgePi / 5.0f;
        float rr = i % 2 == 0 ? radius : radius * 0.45f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) r.fillTriangle(c, pts[i], pts[(i + 1) % 10], col);
}

Color scaled(Color c, float f, uint8_t a) {
    auto ch = [f](uint8_t v) { return static_cast<uint8_t>(std::clamp(static_cast<float>(v) * f, 0.0f, 255.0f)); };
    return Color{ch(c.r), ch(c.g), ch(c.b), a};
}

uint8_t alphaOf(float alpha, float base = 255.0f) { return static_cast<uint8_t>(std::clamp(base * alpha, 0.0f, 255.0f)); }

std::string starsWord(int n) { return std::to_string(n) + (n > 1 ? " étoiles" : " étoile"); }

}  // namespace

void drawBadgeEmblem(GameServices& g, const AchievementDef& a, Vec2 c, float radius, float alpha) {
    Renderer& r = g.ui.r();
    const AchievementCategory* k = g.achievementCatalog.category(a.category);
    const Color col = k ? k->color : g.ui.theme().accent;
    // A medal: a dark edge, the category's colour as its ring, a deep disc at its heart (the
    // colour darkened), a gleam on the ring, and the achievement's stars in gold.
    r.drawGlow(c, radius * 1.6f, col.withAlpha(alphaOf(alpha, 60.0f)), BlendMode::Additive);
    r.fillCircle(c, radius, Color{6, 10, 22, alphaOf(alpha)});
    r.fillCircle(c, radius * 0.9f, col.withAlpha(alphaOf(alpha)));
    r.fillCircle(c, radius * 0.68f, scaled(col, 0.22f, alphaOf(alpha)));
    r.drawCircle(c, radius * 0.68f, scaled(col, 0.6f, alphaOf(alpha)), std::max(1.0f, radius * 0.06f));
    r.drawArc(c, radius * 0.79f, std::max(1.0f, radius * 0.08f), kBadgePi * 1.08f, kBadgePi * 1.42f, Color{255, 255, 255, alphaOf(alpha, 120.0f)});
    const int n = std::clamp(a.stars, 1, 5);
    const Color gold = kGold.withAlpha(alphaOf(alpha));
    if (n == 1) {
        star(r, c, radius * 0.4f, gold);
        return;
    }
    // Two side by side; three and more on a small circle, the first at the top.
    const float sr = radius * (n == 2 ? 0.24f : n == 3 ? 0.23f : n == 4 ? 0.21f : 0.19f);
    const float ring = radius * (n == 2 ? 0.25f : 0.31f);
    for (int i = 0; i < n; ++i) {
        float ang = n == 2 ? (i == 0 ? kBadgePi : 0.0f) : -kBadgePi / 2.0f + 2.0f * kBadgePi * static_cast<float>(i) / static_cast<float>(n);
        if (n == 4) ang += kBadgePi / 4.0f;
        star(r, {c.x + std::cos(ang) * ring, c.y + std::sin(ang) * ring}, sr, gold);
    }
}

void drawBadgeSlot(UIContext& ui, Vec2 c, float radius, bool focused) {
    Renderer& r = ui.r();
    r.fillCircle(c, radius, Color{10, 16, 34, 230});
    // A ring of dashes: a place left free.
    const int dashes = 14;
    const Color col = focused ? ui.theme().accent : Color{120, 140, 180, 150};
    for (int i = 0; i < dashes; ++i) {
        float a0 = 2.0f * kBadgePi * static_cast<float>(i) / dashes, a1 = a0 + kBadgePi / dashes;
        r.drawArc(c, radius * 0.92f, std::max(1.0f, ui.s(1.6f)), a0, a1, col);
    }
    const float arm = radius * 0.32f, th = std::max(1.5f, radius * 0.1f);
    r.drawLine({c.x - arm, c.y}, {c.x + arm, c.y}, col, th);
    r.drawLine({c.x, c.y - arm}, {c.x, c.y + arm}, col, th);
}

float BadgeRow::width(size_t n, float size, float gap) {
    return n == 0 ? 0.0f : static_cast<float>(n) * size + static_cast<float>(n - 1) * gap;
}

int BadgeRow::draw(GameServices& g, const std::string& prefix, const std::vector<const AchievementDef*>& defs, float x, float cy, float size,
                   float gap) {
    UIContext& ui = g.ui;
    tip_ = nullptr;
    focused_ = -1;
    int activated = -1;
    for (size_t i = 0; i < defs.size(); ++i) {
        if (!defs[i]) continue;
        Rect box{x + static_cast<float>(i) * (size + gap), cy - size * 0.5f, size, size};
        const std::string name = prefix + std::to_string(i);
        bool hovered = false;
        if (ui.hotspot(name, box, &hovered)) activated = static_cast<int>(i);
        const bool focused = ui.focused(name);
        if (focused) focused_ = static_cast<int>(i);
        drawBadgeEmblem(g, *defs[i], box.center(), size * 0.5f);
        if ((focused && !ui.usingPointer()) || hovered)
            ui.r().drawCircle(box.center(), size * 0.5f + ui.s(3), ui.theme().accent, std::max(1.0f, ui.s(2.0f)));
        if ((ui.usingPointer() && hovered) || (!ui.usingPointer() && focused)) {
            tip_ = defs[i];
            tipRect_ = box;
        }
    }
    return activated;
}

void BadgeRow::drawTooltip(GameServices& g, const std::string& hint) {
    if (!tip_) return;
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    const AchievementDef& a = *tip_;
    const AchievementCategory* k = g.achievementCatalog.category(a.category);
    const Color col = k ? k->color : ui.theme().accent;
    const float W = ui.screenW(), H = ui.screenH();
    const float w = std::min(ui.s(400), W - ui.s(24));
    const float pad = ui.s(14), inner = w - 2 * pad;
    // The texts, measured first: the box is as high as they need.
    const float fsName = 19, fsSub = fz(ui, 14), fsDesc = fz(ui, 15), fsHint = fz(ui, 13.5f);
    const float nameH = ui.text().measureWrapped(a.name, FontFace::Bold, ui.s(fsName), inner).y;
    const float descH = ui.text().measureWrapped(a.description, FontFace::Body, ui.s(fsDesc), inner).y;
    const float subH = ui.s(22), hintH = hint.empty() ? 0.0f : ui.s(12) + ui.s(26);
    const float h = pad + nameH + ui.s(4) + subH + ui.s(8) + descH + hintH + pad;
    // Under the emblem when there is room, else above it; always on screen.
    Rect box{tipRect_.x, tipRect_.bottom() + ui.s(10), w, h};
    if (box.bottom() > H - ui.s(8)) box.y = tipRect_.y - ui.s(10) - h;
    box.y = std::clamp(box.y, ui.s(8), std::max(ui.s(8), H - h - ui.s(8)));
    box.x = std::clamp(box.x, ui.s(8), std::max(ui.s(8), W - w - ui.s(8)));
    r.fillRoundedRect(box.expanded(ui.s(1)), ui.s(13), Color{2, 4, 10, 200});
    r.fillRoundedRect(box, ui.s(12), Color{10, 15, 32, 250});
    r.drawRoundedRect(box, ui.s(12), col.withAlphaF(0.9f), std::max(1.0f, ui.s(1.6f)));
    float x = box.x + pad, y = box.y + pad;
    ui.labelWrapped(a.name, {x, y}, inner, fsName, ui.theme().text);
    y += nameH + ui.s(4);
    // The category in its colour, then the stars (as many as the achievement's).
    Vec2 cs = ui.label(ui.elide(k ? k->name : a.category, FontFace::Bold, fsSub, inner * 0.6f), {x, y + subH * 0.5f}, fsSub, col, Align::Left,
                       FontFace::Bold, VAlign::Middle);
    float sx = x + cs.x + ui.s(12);
    const float sr = ui.s(6.5f);
    const int n = std::clamp(a.stars, 1, 5);
    for (int i = 0; i < n; ++i) star(r, {sx + sr + static_cast<float>(i) * sr * 2.3f, y + subH * 0.5f}, sr, kGold);
    // « 3 étoiles » after them when it fits (the stars alone say it otherwise).
    const std::string sw = starsWord(n);
    const float wx = sx + static_cast<float>(n) * sr * 2.3f + ui.s(6);
    if (ui.text().measure(sw, FontFace::Body, ui.s(fsSub)).x <= x + inner - wx)
        ui.label(sw, {wx, y + subH * 0.5f}, fsSub, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += subH + ui.s(8);
    ui.labelWrapped(a.description, {x, y}, inner, fsDesc, Color{185, 200, 218, 255});
    y += descH;
    if (!hint.empty()) {
        y += ui.s(12);
        r.fillRect({x, y - ui.s(6), inner, std::max(1.0f, ui.s(1))}, Color{60, 76, 110, 160});
        const float gh = ui.s(22);
        float gw = Glyphs::drawAction(ui, Action::MenuConfirm, {x, y + ui.s(13)}, gh);
        ui.label(ui.elide(hint, FontFace::Body, fsHint, inner - gw - ui.s(8)), {x + gw + ui.s(8), y + ui.s(13)}, fsHint, ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
    }
}

}  // namespace cosmic
