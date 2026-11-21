#include "ui/widgets/CosmicFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"

namespace cosmic::cframe {

namespace {

constexpr Color kLine{44, 58, 104, 255};
constexpr Color kCyan{94, 224, 255, 255};
constexpr Color kGold{255, 205, 110, 255};

cstyle::Motion gMotion;
cstyle::Anims gAnims;
float gFrameT = -1.0f, gDt = 1.0f / 60.0f;

/// This frame's step (the UI clock), the same for every piece drawn in it.
float frameDt(const UIContext& ui) {
    if (ui.time() != gFrameT) {
        gDt = gFrameT < 0.0f ? 1.0f / 60.0f : std::clamp(ui.time() - gFrameT, 0.0f, 0.1f);
        gFrameT = ui.time();
    }
    return gDt;
}

std::string key(const UIContext& ui, const std::string& kind, const std::string& name) { return kind + std::to_string(ui.id(name)); }

/// A plate's body: glow when lit, gradient glass, edge, the accent cell, the light sweep.
void drawPlate(UIContext& ui, const Rect& r, float hot, Color accent, Color left, Color right, float edgeBase, bool sweep) {
    const float u = unit(ui);
    const float cut = std::min(14.0f * u, std::min(r.h, r.w) * 0.28f);
    Renderer& rd = ui.r();
    if (hot > 0.01f) {
        cstyle::fillPolyH(ui, cstyle::chamfer(r.expanded(3.0f * u), cut * 1.1f), accent.withAlphaF(0.16f * hot), accent.withAlphaF(0.04f * hot));
    }
    const auto poly = cstyle::chamfer(r, cut);
    cstyle::fillPolyH(ui, poly, left, right);
    cstyle::strokePoly(ui, poly, Color::lerp(Color::lerp(kLine, accent, edgeBase), accent, hot), std::max(1.0f, 1.6f * u));
    if (r.h > 2.0f * cut + 4.0f * u) rd.fillRect({r.x, r.y + cut, std::max(2.0f, 3.0f * u), r.h - 2.0f * cut}, accent.withAlphaF(0.3f + 0.7f * hot));
    if (hot > 0.01f) rd.drawGlow({r.x + 2.0f * u, r.center().y}, std::min(18.0f * u, r.h * 0.5f), accent.withAlphaF(0.3f * hot));
    if (sweep && hot > 0.5f && !gMotion.reduced) {
        const float ph = std::fmod(ui.time(), 1.6f) / 1.6f;
        if (ph < 0.6f) {
            const float cx = r.x - r.w * 0.3f + (ph / 0.6f) * r.w * 1.6f, bw = r.w * 0.12f, sk = r.h * 0.35f;
            rd.pushClip(r);
            const Color hi = Color::white().withAlphaF(0.14f * hot), lo = Color::white().withAlpha(0);
            cstyle::fillPolyH(ui, {{cx - bw + sk, r.y}, {cx + sk, r.y}, {cx - sk, r.bottom()}, {cx - bw - sk, r.bottom()}}, lo, hi);
            cstyle::fillPolyH(ui, {{cx + sk, r.y}, {cx + bw + sk, r.y}, {cx + bw - sk, r.bottom()}, {cx - sk, r.bottom()}}, hi, lo);
            rd.popClip();
        }
    }
}

}  // namespace

cstyle::Motion motion(const GameServices& g) { return cstyle::Motion::from(g.settings.graphics.particleQuality, g.settings.graphics.screenShake); }
const cstyle::Motion& currentMotion() { return gMotion; }
float unit(const UIContext& ui) { return cstyle::CosmicLayout::of(ui.screenW(), ui.screenH()).unit; }

// ------------------------------------------------------------------ backdrop

void Backdrop::init(GameServices& g) {
    g_ = &g;
    bg_.start(g.content);
}

void Backdrop::update(float dt) {
    if (!g_) return;
    gMotion = motion(*g_);
    bg_.update(g_->ui, dt, gMotion);
}

void Backdrop::render(GameServices& g, float dim) {
    if (!g_) init(g);
    UIContext& ui = g.ui;
    gMotion = motion(g);
    const float W = ui.screenW(), H = ui.screenH();
    bg_.render(ui, {W * 0.5f, H * 0.45f}, W * 0.3f, unit(ui), gMotion);
    if (dim > 0.0f) ui.r().fillRect({0, 0, W, H}, Color{4, 7, 18, 255}.withAlphaF(std::clamp(dim, 0.0f, 1.0f) * 0.82f));
}

// --------------------------------------------------------------------- title

Vec2 measureTitle(UIContext& ui, const std::string& text, float size, float maxW) {
    const float f = maxW > 0.0f ? ui.fitFont(text, FontFace::Title, size, maxW, 0.5f) : size;
    return ui.text().measure(maxW > 0.0f ? ui.elide(text, FontFace::Title, f, maxW) : text, FontFace::Title, ui.s(f));
}

Vec2 title(UIContext& ui, const std::string& text, Vec2 pos, float size, Align a, VAlign va, float maxW, bool neon) {
    const float f = maxW > 0.0f ? ui.fitFont(text, FontFace::Title, size, maxW, 0.5f) : size;
    const std::string shown = maxW > 0.0f ? ui.elide(text, FontFace::Title, f, maxW) : text;
    TextRenderer& tr = ui.text();
    Renderer& rd = ui.r();
    const float px = ui.s(f);
    const Vec2 sz = tr.measure(shown, FontFace::Title, px);
    Vec2 tl = pos;
    if (a == Align::Center) tl.x -= sz.x * 0.5f;
    else if (a == Align::Right) tl.x -= sz.x;
    if (va == VAlign::Middle) tl.y -= sz.y * 0.5f;
    else if (va == VAlign::Bottom) tl.y -= sz.y;
    TextStyle st;
    st.face = FontFace::Title;
    st.size = px;
    const float t = ui.time();
    if (neon) {
        st.color = Color{159, 243, 255, 255};
        st.glow = px * 0.4f;
        st.glowColor = Color{94, 224, 255, 50};
        tr.draw(shown, tl, st);
        st.glow = 0.0f;
        st.color = Color{232, 253, 255, 210};
        tr.draw(shown, tl, st);
        return sz;
    }
    // A dark outline, then the chrome in horizontal bands (white, a deep blue band under the
    // middle, light blue below), then a light sweeping the letters now and then.
    st.color = Color{8, 16, 40, 230};
    const float o = std::max(1.0f, px * 0.035f);
    for (int d = 0; d < 8; ++d) {
        const float ang = d * 0.7853982f;
        tr.draw(shown, {tl.x + std::cos(ang) * o, tl.y + std::sin(ang) * o}, st);
    }
    const float cy = tl.y + sz.y * 0.5f, cap = px * 0.40f;
    const float y0 = cy - cap, y1 = cy + cap, mid = y0 + (y1 - y0) * 0.52f;
    struct Band {
        float a, b;
        Color c;
    };
    const Band bands[] = {{tl.y - o, mid, {246, 252, 255, 255}},
                          {mid, mid + (y1 - mid) * 0.38f, {70, 128, 204, 255}},
                          {mid + (y1 - mid) * 0.38f, mid + (y1 - mid) * 0.72f, {120, 206, 255, 255}},
                          {mid + (y1 - mid) * 0.72f, tl.y + sz.y + o, {220, 246, 255, 255}}};
    for (const Band& b : bands) {
        rd.pushClip({tl.x - o, b.a, sz.x + 2.0f * o, std::max(0.0f, b.b - b.a)});
        st.color = b.c;
        tr.draw(shown, tl, st);
        rd.popClip();
    }
    if (!gMotion.reduced) {
        const float ph = std::fmod(t * 0.35f, 1.4f);
        if (ph < 1.0f) {
            const float sx = tl.x - sz.x * 0.1f + ph * sz.x * 1.2f, bw = std::max(8.0f, px * 0.45f);
            rd.pushClip({sx, tl.y - o, bw, sz.y + 2.0f * o});
            st.color = Color::white().withAlphaF(0.85f);
            tr.draw(shown, tl, st);
            rd.popClip();
        }
    }
    return sz;
}

// --------------------------------------------------------------------- panel

void panel(UIContext& ui, const Rect& r, bool highlight, float radius) {
    const float u = unit(ui);
    // A dark base under the glass: the content stays readable over the planets (and a dialog
    // hides what is under it).
    ui.r().fillRoundedRect(r, 14.0f * u, Color{6, 10, 24, 255}.withAlpha(highlight ? 235 : 120));
    cstyle::HoloCard card;
    card.begin(ui, r, ui.time(), u, gMotion);
    card.end(ui, ui.time());
    Renderer& rd = ui.r();
    if (highlight) rd.drawRoundedRect(r, std::min(ui.s(radius), 14.0f * u), kCyan.withAlpha(170), std::max(1.0f, 2.0f * u));
    // Corner brackets: the cockpit's frame.
    const float L = std::min(22.0f * u, std::min(r.w, r.h) * 0.2f), th = std::max(1.0f, 2.0f * u);
    const Color c = kCyan.withAlpha(highlight ? 210 : 150);
    rd.fillRect({r.x - th, r.y - th, L, th}, c);
    rd.fillRect({r.x - th, r.y - th, th, L}, c);
    rd.fillRect({r.right() - L + th, r.bottom(), L, th}, c);
    rd.fillRect({r.right(), r.bottom() - L + th, th, L}, c);
}

// ---------------------------------------------------------------------- tabs

bool tabs(UIContext& ui, const std::string& name, const Rect& rc, const std::vector<std::string>& labels, int& active, const std::vector<int>& badges) {
    if (labels.empty()) return false;
    bool changed = false;
    {  // the bar's rules, unseen
        DrawGroup hidden(ui.r(), 0.0f);
        changed = ui.tabs(name, rc, labels, active, badges);
    }
    const float dt = frameDt(ui), u = unit(ui);
    const int n = static_cast<int>(labels.size());
    const float w = rc.w / n;
    float common = 21.0f;
    for (const auto& l : labels) common = std::min(common, ui.fitFont(l, FontFace::Bold, 21.0f, w - ui.s(4) - ui.s(16), 0.55f));
    for (int i = 0; i < n; ++i) {
        const Rect tr{rc.x + i * w + ui.s(2), rc.y, w - ui.s(4), rc.h};
        const bool on = i == active;
        const float hot = gAnims.step(key(ui, "tab", name + "#" + std::to_string(i)), on || ui.mouseOver(tr), dt, 12.0f);
        const float lit = on ? 1.0f : hot * 0.45f;
        drawPlate(ui, tr, lit, kCyan, Color::lerp(Color{16, 28, 62, 225}, Color{22, 60, 92, 240}, lit), Color{10, 18, 42, 195}, 0.0f, on);
        int count = i < static_cast<int>(badges.size()) ? badges[static_cast<size_t>(i)] : 0;
        const bool urgent = count > 0;
        count = std::abs(count);
        Rect room{tr.x + ui.s(6), tr.y, tr.w - ui.s(8), tr.h};
        if (count > 0) {
            const std::string s = count > 99 ? "99+" : std::to_string(count);
            const float bh = std::min(ui.s(20), tr.h - ui.s(8));
            const float bw = std::max(bh, ui.text().measure(s, FontFace::Bold, ui.s(13)).x + ui.s(10));
            const Rect b{tr.right() - ui.s(7) - bw, tr.center().y - bh * 0.5f, bw, bh};
            ui.r().fillRoundedRect(b, bh * 0.5f, urgent ? Color{255, 77, 109, 255} : kCyan.withAlphaF(0.85f));
            ui.label(s, b.center(), 13, urgent ? Color::white() : Color{6, 14, 24, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
            room.w = b.x - ui.s(3) - room.x;
        }
        const float fs = std::min(common, ui.fitFont(labels[i], FontFace::Bold, 21.0f, room.w - ui.s(10), 0.55f));
        const std::string shown = ui.elide(labels[i], FontFace::Bold, fs, room.w - ui.s(8));
        ui.label(shown, room.center(), fs, on ? Color::white() : Color::lerp(ui.theme().textDim, ui.theme().text, hot), Align::Center,
                 FontFace::Bold, VAlign::Middle);
        if (on) ui.r().fillRect({tr.x + tr.w * 0.25f, tr.bottom() - std::max(2.0f, 3.0f * u), tr.w * 0.5f, std::max(2.0f, 3.0f * u)}, kCyan);
    }
    return changed;
}

// -------------------------------------------------------------------- button

bool button(UIContext& ui, const std::string& name, const Rect& rc, const std::string& t, const UIContext::ButtonStyle& st) {
    bool clicked = false;
    {  // the button's rules, unseen (focus, clicks, sound, read-only forms)
        DrawGroup hidden(ui.r(), 0.0f);
        clicked = ui.button(name, rc, t, st);
    }
    if (rc.w <= 0.0f || rc.h <= 0.0f) return clicked;
    const float dt = frameDt(ui);
    const bool enabled = st.enabled && !ui.readOnly;
    const float hot = gAnims.step(key(ui, "btn", name), enabled && (ui.focused(name) || ui.mouseOver(rc)), dt, 12.0f);
    const UITheme& th = ui.theme();
    Color accent = st.danger ? Color{255, 77, 109, 255} : st.primary ? kGold : kCyan;
    Color left = st.primary ? Color{30, 40, 66, 235} : Color{16, 28, 62, 228};
    Color right = Color{10, 18, 42, 200};
    if (st.danger) left = {74, 22, 38, 235}, right = {40, 12, 24, 210};
    if (st.selected) left = Color::lerp(left, kCyan.withAlpha(235), 0.3f);
    if (!enabled) left = right = Color{20, 22, 30, 180}, accent = Color{90, 96, 120, 255};
    drawPlate(ui, rc, hot, accent, left, right, st.primary || st.danger ? 0.55f : (st.selected ? 0.6f : 0.0f), st.primary);
    if (st.lit) ui.heldHighlight(rc);
    float fs = st.fontSize > 0 ? st.fontSize : (st.small ? 20.0f : 26.0f);
    float textX = st.align == Align::Center ? rc.center().x : rc.x + ui.s(20);
    if (!st.icon.empty() && st.iconIndex >= 0) {
        const float is = rc.h * 0.62f;
        const Rect ir{rc.x + ui.s(14), rc.center().y - is * 0.5f, is, is};
        ui.icon(st.icon, st.iconIndex, st.iconCell, ir, enabled ? Color::white() : Color{120, 120, 120, 255});
        if (st.align != Align::Center) textX = ir.right() + ui.s(12);
    }
    const float room = st.align == Align::Center ? rc.w - ui.s(20) : rc.right() - textX - ui.s(12);
    fs = ui.fitFont(t, FontFace::Bold, fs, room);
    Color tc = enabled ? Color::lerp(th.text, Color::white(), hot) : th.textDim;
    if (st.lit) tc = Color::white();
    const Vec2 lsz = ui.label(t, {textX, rc.center().y}, fs, tc, st.align, FontFace::Bold, VAlign::Middle);
    // The chevron of the main menu's plates, when there is room for it beside the label.
    if (st.primary && enabled && st.align == Align::Center && lsz.x + 2.0f * ui.s(34) <= rc.w)
        ui.label("›", {rc.right() - ui.s(12) - (1.0f - hot) * ui.s(5), rc.center().y}, std::min(fs, 24.0f), accent.withAlphaF(0.35f + 0.65f * hot),
                 Align::Right, FontFace::Bold, VAlign::Middle);
    return clicked;
}

// ---------------------------------------------------------------------- back

bool back(UIContext& ui, const std::string& name, const Rect& r, const std::string& text, const std::string& icon) {
    const bool act = diamond(ui, name, r, text, icon);
    if (act) ui.app().resources().playSound("sounds/ui_back.wav", 0.6f);
    return act;
}

bool diamond(UIContext& ui, const std::string& name, const Rect& r, const std::string& text, const std::string& icon, bool lit) {
    const float dt = frameDt(ui);
    const float half = r.h * 0.5f;
    const Vec2 c{r.x + half, r.center().y};
    cstyle::Diamond d;
    d.icon = icon;
    bool act = cstyle::diamondButton(ui, gAnims, name, c, half, d, 1.0f, ui.time(), dt);
    const Rect lab{r.x + r.h + ui.s(6), r.y, std::max(0.0f, r.w - r.h - ui.s(6)), r.h};
    bool over = false;
    if (lab.w > 0.0f) act = ui.mouseClick(name + "#label", lab, &over) || act;
    const bool on = lit || ui.focused(name) || over || ui.mouseOver(Rect::fromCenter(c, {half * 2.0f, half * 2.0f}));
    if (lab.w > ui.s(12)) {
        const std::string s = i18n::keep(cstyle::upper(i18n::tr(text)));
        const float f = ui.fitFont(s, FontFace::Bold, std::min(22.0f, r.h * 0.42f / ui.scale()), lab.w, 0.6f);
        ui.label(ui.elide(s, FontFace::Bold, f, lab.w), {lab.x, lab.center().y}, f, on ? kGold : Color{226, 236, 245, 255}, Align::Left,
                 FontFace::Bold, VAlign::Middle);
    }
    return act;
}

}  // namespace cosmic::cframe
