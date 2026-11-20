#include "ui/screens/DownedView.h"

#include <cmath>

#include "engine/core/I18n.h"
#include "game/gameplay/GameWorld.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::downed {

namespace {

using raidui::kDim;
using raidui::kGold;
using raidui::kText;

const Color kDown{255, 196, 92, 255};    // à terre: gold, as the souls' rings
const Color kWiped{255, 74, 74, 255};    // groupe éliminé: the raid's danger
const Color kFill{10, 13, 32, 228};

constexpr float kTitleSize = 20.0f, kLineSize = 14.0f, kReviveSize = 14.0f, kButtonSize = 14.0f;
constexpr float kTitleH = 28.0f, kLineH = 20.0f, kReviveH = 22.0f, kGaugeH = 8.0f, kButtonH = 36.0f, kKeyH = 24.0f;

float smooth(float k) {
    k = std::clamp(k, 0.0f, 1.0f);
    return k * k * (3.0f - 2.0f * k);
}

}  // namespace

// ------------------------------------------------------------------------------------- the view

Vec2 viewCenter(const WorldView& v) {
    const float s = std::max(1e-4f, v.scale);
    return v.topLeft + Vec2{static_cast<float>(v.screenW), static_cast<float>(v.screenH)} * (0.5f / s);
}

WorldView ViewBlend::apply(const WorldView& want, float dt) {
    if (!on_ || want.scale <= 0.0f) return want;
    t_ = std::min(kSeconds, t_ + std::max(0.0f, dt));
    if (t_ >= kSeconds) {
        on_ = false;
        return want;
    }
    const float k = smooth(t_ / kSeconds);
    // The centre and the zoom (in its logarithm: a zoom out and a zoom in at the same pace), from the
    // view shown when it changed toward the view wanted now, on this screen's shape.
    const Vec2 c = lerpv(viewCenter(from_), viewCenter(want), k);
    const float scale = std::exp(lerpf(std::log(std::max(1e-4f, from_.scale)), std::log(want.scale), k));
    WorldView v = want;
    v.scale = scale;
    v.topLeft = c - Vec2{static_cast<float>(v.screenW), static_cast<float>(v.screenH)} * (0.5f / scale);
    return v;
}

// ------------------------------------------------------------------------------------ the state

State stateOf(const GameWorld& w, int local, bool wiped) {
    if (wiped) return State::Wiped;
    if (local >= 0 && local < w.teamSize() && w.member(local).presence == GameWorld::Presence::Down) return State::Down;
    return State::None;
}

Banner downBanner(const DownInfo& d) {
    Banner b;
    b.state = State::Down;
    b.title = "À TERRE — en attente de réanimation";
    b.color = kDown;
    if (!d.canRevive)
        b.lines.push_back(d.revivesLeft == 0 ? "L'équipe n'a plus de réanimation : vous restez à terre jusqu'à la fin de la tentative"
                          : d.elsewhere > 0 ? "Aucun allié dans cette zone : un allié d'une autre zone peut revenir vous relever"  // 2.78
                                            : "Aucun allié en jeu pour vous relever pour l'instant");
    else if (d.who.empty())
        b.lines.push_back("Un allié doit maintenir Interagir 2 s près de votre âme");
    else
        b.lines.push_back("Un allié doit maintenir Interagir 2 s près de l'âme de " + d.who);
    if (d.revivesLeft >= 0)
        b.lines.push_back("Réanimations restantes de l'équipe : " + std::to_string(d.revivesLeft) + " sur " + std::to_string(d.revivesMax));
    if (d.canRevive && d.revive > 0.0f) {
        const float k = std::clamp(d.revive / GameWorld::kReviveSeconds, 0.0f, 1.0f);
        const int pct = std::clamp(static_cast<int>(k * 100.0f), 0, 99);
        b.revive = k;
        b.reviveText = (d.reviver.empty() ? std::string("Un allié") : d.reviver) + " vous réanime… " + std::to_string(pct) + " %";
    }
    if (!d.watching.empty())
        b.lines.push_back("Vous regardez " + d.watching +
                          (d.soulInView ? " · votre âme est dans la vue" : " · votre âme est hors de la vue : suivez la flèche à votre couleur"));
    else
        b.lines.push_back(d.soulInView ? "Votre âme : dans la vue, sa marque au sol montre où vous serez relevé"
                                       : "Votre âme : hors de la vue, la flèche à votre couleur la montre au bord de l'écran");
    if (d.viewKeys) {
        b.commands.push_back({"Joueur précédent", Action::MenuTabPrev, d.others});
        b.commands.push_back({"Revenir sur mon âme", Action::MenuConfirm, !d.watching.empty()});
        b.commands.push_back({"Joueur suivant", Action::MenuTabNext, d.others});
    } else {
        b.lines.push_back("La caméra suit l'équipe en jeu");
    }
    return b;
}

Banner wipedBanner(const std::string& sub, bool freeCamera, bool targets) {
    Banner b;
    b.state = State::Wiped;
    b.title = "GROUPE ÉLIMINÉ — mode spectateur jusqu'à la fin de l'événement";
    b.color = kWiped;
    b.lines.push_back(sub);
    b.lines.push_back("Tout le groupe était à terre en même temps : plus de réanimation, la zone attend un groupe de renfort");
    b.commands.push_back({"Joueur précédent", Action::MenuTabPrev, targets});
    b.commands.push_back({"Caméra libre", Action::MenuConfirm, !freeCamera});
    b.commands.push_back({"Joueur suivant", Action::MenuTabNext, targets});
    return b;
}

// ----------------------------------------------------------------------------------- the layout

float width(UIContext& ui) { return std::min(ui.s(720), ui.screenW() - ui.s(32)); }

Layout layout(UIContext& ui, const Banner& b, Vec2 tl, float w) {
    Layout l;
    const float pad = ui.s(12), iw = w - pad * 2.0f, x = tl.x + pad;
    float y = tl.y + ui.s(10);
    l.title = {x, y, iw, ui.s(kTitleH)};
    y = l.title.bottom() + ui.s(2);
    for (const std::string& t : b.lines) {
        std::vector<std::string> lines = raidui::wrap(ui, t, FontFace::Body, kLineSize, iw, 2);
        if (lines.empty()) continue;
        const Rect r{x, y, iw, static_cast<float>(lines.size()) * ui.s(kLineH)};
        l.lineBoxes.push_back(r);
        l.lines.push_back(std::move(lines));
        y = r.bottom() + ui.s(2);
    }
    if (b.revive >= 0.0f) {
        y += ui.s(4);
        l.reviveLabel = {x, y, iw, ui.s(kReviveH)};
        y = l.reviveLabel.bottom() + ui.s(2);
        l.gauge = {x, y, iw, ui.s(kGaugeH)};
        y = l.gauge.bottom() + ui.s(4);
    }
    if (!b.commands.empty()) {
        y += ui.s(6);
        const float gap = ui.s(8), n = static_cast<float>(b.commands.size());
        const float each = (iw - gap * (n - 1.0f)) / n;
        for (size_t k = 0; k < b.commands.size(); ++k) l.buttons.push_back({x + static_cast<float>(k) * (each + gap), y, each, ui.s(kButtonH)});
        y += ui.s(kButtonH);
    }
    y += ui.s(10);
    l.panel = {tl.x, tl.y, w, y - tl.y};
    return l;
}

Rect place(UIContext& ui, float w, float h, const std::vector<Rect>& taken) {
    const float W = ui.screenW(), H = ui.screenH(), gap = ui.s(10);
    Rect r{(W - w) * 0.5f, ui.s(14), w, h};
    // Under whatever it would cover, one after the other (the minimap, the heart's bar, a block moved
    // there by the player), as many times as needed.
    for (int guard = 0; guard < 32; ++guard) {
        bool moved = false;
        for (const Rect& t : taken)
            if (t.w > 0.0f && t.h > 0.0f && t.intersects(r)) {
                r.y = t.bottom() + gap;
                moved = true;
            }
        if (!moved) break;
    }
    r.y = std::clamp(r.y, 0.0f, std::max(0.0f, H - h));
    return r;
}

Layout placed(UIContext& ui, const Banner& b, const std::vector<Rect>& taken) {
    const float w = width(ui);
    const Layout probe = layout(ui, b, {0.0f, 0.0f}, w);
    const Rect at = place(ui, w, probe.panel.h, taken);
    return layout(ui, b, {at.x, at.y}, w);
}

Rect below(const Rect& area, const Rect& banner, float gap, const std::vector<Rect>& taken) {
    if (banner.empty() || area.empty() || !area.intersects(banner)) return area;
    Rect r = area;
    r.y = banner.bottom() + gap;
    float bottom = r.y + area.h;
    for (const Rect& t : taken)
        if (!t.empty() && t.x < r.right() && t.right() > r.x && t.bottom() > r.y) bottom = std::min(bottom, t.y - gap);
    r.h = std::max(0.0f, bottom - r.y);
    return r;
}

// ---------------------------------------------------------------------------------- the drawing

int draw(UIContext& ui, const Banner& b, const Layout& l, const std::string& id) {
    Renderer& r = ui.r();
    r.fillRoundedRect(l.panel, ui.s(12), kFill);
    r.drawRoundedRect(l.panel, ui.s(12), b.color.withAlpha(200), ui.s(1.6f));
    raidui::fit(ui, b.title, l.title.center(), kTitleSize, l.title.w, b.color, FontFace::Bold, Align::Center);
    for (size_t i = 0; i < l.lines.size(); ++i) {
        const Rect& box = l.lineBoxes[i];
        // Each line centred in its box (wrapped already, measured).
        i18n::Verbatim shown(true);
        float y = box.y;
        for (const std::string& t : l.lines[i]) {
            ui.label(t, {box.center().x, y + ui.s(kLineH) * 0.5f}, kLineSize, i == 0 ? kText : kDim, Align::Center, FontFace::Body, VAlign::Middle);
            y += ui.s(kLineH);
        }
    }
    if (l.gauge.w > 0.0f) {
        raidui::fit(ui, b.reviveText, l.reviveLabel.center(), kReviveSize, l.reviveLabel.w, kGold, FontFace::Bold, Align::Center);
        r.fillRoundedRect(l.gauge, l.gauge.h * 0.5f, Color{40, 34, 58, 255});
        const float k = std::clamp(b.revive, 0.0f, 1.0f);
        if (k > 0.0f) r.fillRoundedRect({l.gauge.x, l.gauge.y, std::max(l.gauge.h, l.gauge.w * k), l.gauge.h}, l.gauge.h * 0.5f, kGold);
    }
    int clicked = -1;
    for (size_t k = 0; k < l.buttons.size() && k < b.commands.size(); ++k) {
        const Command& c = b.commands[k];
        const Rect& br = l.buttons[k];
        bool hovered = false;
        if (ui.mouseClick(id + std::to_string(k), br, &hovered) && c.enabled) clicked = static_cast<int>(k);
        const bool hot = hovered && c.enabled;
        r.fillRoundedRect(br, ui.s(8), !c.enabled ? Color{18, 22, 42, 200} : hot ? Color{48, 60, 116, 245} : Color{26, 33, 72, 240});
        r.drawRoundedRect(br, ui.s(8), c.enabled ? b.color.withAlpha(hot ? 230 : 140) : Color{70, 76, 110, 140}, ui.s(1.2f));
        // Its key (the device in use) then its label, centred together; the label fitted, then shortened.
        const float pad = ui.s(8), gap = ui.s(6), keyH = ui.s(kKeyH);
        const bool key = c.key != Action::Count;
        const float keyW = key ? Glyphs::measureAction(ui, c.key, keyH) : 0.0f;
        const float room = br.w - pad * 2.0f - (key ? keyW + gap : 0.0f);
        if (room <= ui.s(12)) continue;
        const float size = ui.fitFont(c.label, FontFace::Bold, kButtonSize, room, 0.75f);
        const std::string label = ui.elide(c.label, FontFace::Bold, size, room);
        const float labelW = std::min(room, ui.text().measure(label, FontFace::Bold, ui.s(size)).x);
        float x = br.center().x - ((key ? keyW + gap : 0.0f) + labelW) * 0.5f;
        if (key) {
            x += Glyphs::drawAction(ui, c.key, {x, br.center().y}, keyH) + gap;
        }
        ui.label(label, {x, br.center().y}, size, c.enabled ? kText : kDim, Align::Left, FontFace::Bold, VAlign::Middle);
    }
    return clicked;
}

void drawFilter(Renderer& r, float w, float h) {
    // A soft vignette from the borders (a fifth of the smaller side), the colours a little washed: the
    // world stays as readable, the HUD is drawn over it.
    const float d = std::min(w, h) * 0.2f;
    const Color edge{6, 8, 20, 110}, clear{6, 8, 20, 0};
    r.fillGradientV({0.0f, 0.0f, w, d}, edge, clear);
    r.fillGradientV({0.0f, h - d, w, d}, clear, edge);
    r.fillGradientH({0.0f, 0.0f, d, h}, edge, clear);
    r.fillGradientH({w - d, 0.0f, d, h}, clear, edge);
    r.fillRect({0.0f, 0.0f, w, h}, Color{74, 80, 100, 40});
}

}  // namespace cosmic::downed
