#include "ui/widgets/CosmicStyle.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/core/Easing.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Json.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::cstyle {

namespace {

constexpr float kPi = 3.14159265f;
constexpr Color kLine{44, 58, 104, 255};
constexpr Color kInk{230, 236, 250, 255};
constexpr Color kMuted{140, 152, 186, 255};
constexpr Color kCyan{94, 224, 255, 255};
constexpr Color kGold{255, 205, 110, 255};
constexpr Color kRed{255, 106, 106, 255};
const char* const kNova = "animations/nova.json";

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
/// A font size for UIContext::label (unscaled units) from reference pixels.
float fsz(UIContext& ui, float refPx, float unit) { return refPx * unit / ui.scale(); }

/// One frame of a Nova clip (`tex`: another sheet with the same cells, a suit).
void drawNova(UIContext& ui, const std::string& clip, float fps, float t, Vec2 c, float size, bool flip, Color tint = Color::white(),
              const TexturePtr& tex = nullptr) {
    AnimationSetPtr set = ui.app().resources().animation(kNova);
    if (!set) return;
    const AnimationClip* cl = set->clip(clip);
    const Texture* sheet = tex ? tex.get() : set->texture.get();
    if (!cl || !sheet || cl->frameCount() == 0) return;
    const int f = static_cast<int>(std::floor(t * fps)) % cl->frameCount();
    Rect src = set->frameRect(*cl, std::max(0, f));
    ui.r().drawTexture(*sheet, &src, Rect::fromCenter(c, {size, size}), tint, BlendMode::Alpha, flip);
}

void fillEllipse(UIContext& ui, Vec2 c, float rx, float ry, Color col) {
    std::vector<Vec2> pts;
    for (int i = 0; i < 20; ++i) pts.push_back({c.x + std::cos(i * kPi / 10) * rx, c.y + std::sin(i * kPi / 10) * ry});
    ui.r().fillConvexPolygon(pts, col);
}

}  // namespace

// ------------------------------------------------------------------ helpers

float Anims::step(const std::string& key, bool on, float dt, float speed) {
    float& v = v_[key];
    v += ((on ? 1.0f : 0.0f) - v) * std::min(1.0f, dt * speed);
    return v;
}

CosmicLayout CosmicLayout::of(float screenW, float screenH) {
    CosmicLayout l;
    l.unit = std::max(0.1f, std::min(screenW / 1280.0f, screenH / 720.0f));
    l.origin = {(screenW - 1280.0f * l.unit) * 0.5f, (screenH - 720.0f * l.unit) * 0.5f};
    return l;
}

std::string upper(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 'a' && c <= 'z') {
            o += static_cast<char>(c - 32);
        } else if (c == 0xC3 && i + 1 < s.size()) {  // U+00E0..U+00FE (not ÷): minus 0x20
            unsigned char d = static_cast<unsigned char>(s[++i]);
            o += static_cast<char>(c);
            o += static_cast<char>(d >= 0xA0 && d <= 0xBE && d != 0xB7 ? d - 0x20 : d);
        } else if (c == 0xC5 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x93) {  // œ
            o += static_cast<char>(0xC5);
            o += static_cast<char>(0x92);
            ++i;
        } else {
            o += static_cast<char>(c);
        }
    }
    return o;
}

int menuIconIndex(const std::string& id) {
    // The order of tools/assetgen/environment/menu_icons.py (ICONS; tests/test_cosmic_menu.cpp
    // checks it against assets/ui/menu_icons.json).
    static const char* const kIds[] = {"continue", "campaign", "challenges", "coop", "online", "news",
                                       "achievements", "collection", "stats", "settings", "quit", "admin"};
    for (int i = 0; i < 12; ++i)
        if (id == kIds[i]) return i;
    return -1;
}

void menuIcon(UIContext& ui, const std::string& id, const Rect& dst, float angleDeg, Color tint) {
    const int i = menuIconIndex(id);
    if (i < 0) return;
    TexturePtr tex = ui.app().resources().texture("ui/menu_icons.png", true);
    if (!tex) return;
    Rect src{static_cast<float>(i * 24), 0, 24, 24};
    ui.r().drawTexture(*tex, &src, dst, tint, BlendMode::Alpha, false, false, angleDeg);
}

std::vector<Vec2> chamfer(const Rect& r, float cut) {
    return {{r.x + cut, r.y}, {r.right(), r.y}, {r.right(), r.bottom() - cut}, {r.right() - cut, r.bottom()}, {r.x, r.bottom()}, {r.x, r.y + cut}};
}

void fillPolyH(UIContext& ui, const std::vector<Vec2>& pts, Color left, Color right) {
    if (pts.size() < 3) return;
    float x0 = pts[0].x, x1 = pts[0].x;
    for (const Vec2& p : pts) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x);
    const float op = ui.r().opacity();
    std::vector<SDL_Vertex> v;
    for (const Vec2& p : pts) {
        Color c = Color::lerp(left, right, x1 > x0 ? (p.x - x0) / (x1 - x0) : 0.0f);
        v.push_back({{p.x, p.y}, {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * op}, {0, 0}});
    }
    std::vector<int> idx;
    for (int i = 1; i + 1 < static_cast<int>(pts.size()); ++i) idx.insert(idx.end(), {0, i, i + 1});
    ui.r().drawGeometry(nullptr, v, idx);
}

void strokePoly(UIContext& ui, const std::vector<Vec2>& pts, Color c, float thickness) {
    for (size_t i = 0; i < pts.size(); ++i) ui.r().drawLine(pts[i], pts[(i + 1) % pts.size()], c, thickness);
}

void badgePill(UIContext& ui, Vec2 c, int count, float h, Color fill) {
    if (count <= 0) return;
    const std::string t = count > 99 ? "99+" : std::to_string(count);
    const float px = h * 0.62f;
    const float w = std::max(h, ui.text().measure(t, FontFace::Bold, px).x + h * 0.6f);
    ui.r().fillRoundedRect({c.x - w * 0.5f, c.y - h * 0.5f, w, h}, h * 0.5f, fill);
    ui.label(t, c, px / ui.scale(), Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
}

// ------------------------------------------------------------------- plate

bool plateButton(UIContext& ui, Anims& anims, const std::string& id, const Rect& r, const Plate& p, float u, float appear, float t,
                 float dt, const Motion& m) {
    bool hovered = false;
    const bool act = ui.hotspot(id, r, &hovered);
    const float k = anims.step("plate/" + id, ui.focused(id), dt, 12.0f);
    const float ea = m.reduced ? 1.0f : applyEase(Ease::OutBack, clamp01(appear));
    Renderer& rd = ui.r();
    const float op = rd.opacity();
    rd.setOpacity(op * clamp01(appear * 1.5f));
    const Rect rr = r.translated({k * 16.0f * u + (1.0f - ea) * -120.0f * u, 0});
    const float cut = 16.0f * u;
    const auto poly = chamfer(rr, cut);
    if (k > 0.01f) {  // glow of the focused plate
        fillPolyH(ui, chamfer(rr.expanded(7.0f * u), cut * 1.2f), p.accent.withAlphaF(0.10f * k), p.accent.withAlphaF(0.02f * k));
        fillPolyH(ui, chamfer(rr.expanded(3.5f * u), cut * 1.1f), p.accent.withAlphaF(0.16f * k), p.accent.withAlphaF(0.04f * k));
    }
    fillPolyH(ui, poly, Color{16, 28, 62, 230}, Color{10, 18, 42, 190});
    strokePoly(ui, poly, Color::lerp(kLine, p.accent, k), 2.0f * u);
    // Accent cell at the left edge.
    rd.fillRect({rr.x, rr.y + cut, 4.0f * u, rr.h - 2 * cut}, p.accent.withAlphaF(0.35f + 0.65f * k));
    if (k > 0.01f) rd.drawGlow({rr.x + 2.0f * u, rr.center().y}, 18.0f * u, p.accent.withAlphaF(0.35f * k));
    // Sweep of light over the focused plate.
    if (k > 0.5f && !m.reduced) {
        const float ph = std::fmod(t, 1.6f) / 1.6f;
        if (ph < 0.6f) {
            const float cx = rr.x - rr.w * 0.3f + (ph / 0.6f) * rr.w * 1.6f, bw = rr.w * 0.15f, sk = rr.h * 0.35f;
            rd.pushClip(rr);
            const Color hi = Color::white().withAlphaF(0.16f * k), lo = Color::white().withAlpha(0);
            fillPolyH(ui, {{cx - bw + sk, rr.y}, {cx + sk, rr.y}, {cx - sk, rr.bottom()}, {cx - bw - sk, rr.bottom()}}, lo, hi);
            fillPolyH(ui, {{cx + sk, rr.y}, {cx + bw + sk, rr.y}, {cx + bw - sk, rr.bottom()}, {cx - sk, rr.bottom()}}, hi, lo);
            rd.popClip();
        }
    }
    // Icon (pops when focused).
    const float is = 40.0f * u * (1.0f + 0.18f * k);
    menuIcon(ui, p.icon, Rect::fromCenter({rr.x + 40.0f * u, rr.center().y}, {is, is}), -6.0f * k);
    // Right side: chevron or thumbnail, then the count pill.
    float right = rr.right() - 18.0f * u;
    if (p.big && p.thumb) {
        const Rect th{right - 130.0f * u, rr.center().y - 36.0f * u, 130.0f * u, 72.0f * u};
        const float tw = static_cast<float>(p.thumb->width()), thh = static_cast<float>(p.thumb->height());
        Rect src{0, 0, tw, thh};
        if (tw / thh > th.w / th.h) src.w = thh * th.w / th.h, src.x = (tw - src.w) * 0.5f;
        else src.h = tw * th.h / th.w, src.y = (thh - src.h) * 0.5f;
        rd.drawTexture(*p.thumb, &src, th);
        rd.drawRect(th, Color::white().withAlphaF(0.15f + 0.25f * k), std::max(1.0f, u));
        right = th.x - 12.0f * u;
    } else {
        ui.label("›", {right - (1.0f - k) * 8.0f * u, rr.center().y}, fsz(ui, 22, u), p.accent.withAlphaF(k), Align::Right, FontFace::Bold,
                 VAlign::Middle);
        right -= 22.0f * u;
    }
    if (p.badge > 0) {
        const float h = 22.0f * u;
        const std::string n = p.badge > 99 ? "99+" : std::to_string(p.badge);
        const float w = std::max(h, ui.text().measure(n, FontFace::Bold, h * 0.62f).x + h * 0.6f);
        badgePill(ui, {right - w * 0.5f, rr.center().y}, p.badge, h, Color{255, 77, 109, 255});
        right -= w + 10.0f * u;
    }
    // Label and subtitle, each fitted into the room left (never under the thumbnail or the pill).
    const float x0 = rr.x + 78.0f * u, room = std::max(10.0f * u, right - x0);
    const std::string label = upper(i18n::tr(p.label));
    const bool hasSub = !p.sub.empty();
    const float ly = p.big ? rr.y + 30.0f * u : hasSub ? rr.y + rr.h * 0.37f : rr.center().y;
    float f = ui.fitFont(label, FontFace::Bold, fsz(ui, 21, u), room, 0.6f);
    ui.label(ui.elide(label, FontFace::Bold, f, room), {x0, ly}, f, Color::lerp(kInk, Color::white(), k), Align::Left, FontFace::Bold, VAlign::Middle);
    if (hasSub) {
        const float sy = p.big ? rr.y + 55.0f * u : rr.y + rr.h * 0.70f;
        const float sf = fsz(ui, 13, u);
        ui.label(ui.elide(p.sub, FontFace::Body, sf, room), {x0, sy}, sf, kMuted, Align::Left, FontFace::Body, VAlign::Middle);
    }
    if (p.big && p.progress >= 0.0f) {
        const Rect bar{x0, rr.y + 72.0f * u, std::min(200.0f * u, room), 5.0f * u};
        rd.fillRoundedRect(bar, bar.h * 0.5f, Color{255, 255, 255, 30});
        if (p.progress > 0.0f) {
            Rect fill{bar.x, bar.y, std::max(bar.h, bar.w * clamp01(p.progress)), bar.h};
            rd.fillGradientH(fill, kGold, Color{255, 154, 64, 255});
        }
    }
    if (!p.feature.empty()) ui.newBadge(id, rr, p.feature, true, true);
    rd.setOpacity(op);
    (void)hovered;
    return act;
}

// ----------------------------------------------------------------- diamond

bool diamondButton(UIContext& ui, Anims& anims, const std::string& id, Vec2 c, float half, const Diamond& d, float appear, float t,
                   float dt, bool* isNew) {
    const Rect hit = Rect::fromCenter(c, {half * 2.0f, half * 2.0f});
    const bool act = ui.hotspot(id, hit);
    const float k = anims.step("diamond/" + id, ui.focused(id), dt, 14.0f);
    const float u = half / 28.0f;
    const float pop = applyEase(Ease::OutBack, clamp01(appear));
    if (pop <= 0.01f) return act;
    Renderer& rd = ui.r();
    const float hd = half * (0.86f + 0.14f * k) * pop;
    const Color acc = d.danger ? kRed : kGold;
    const std::vector<Vec2> pts{{c.x, c.y - hd}, {c.x + hd, c.y}, {c.x, c.y + hd}, {c.x - hd, c.y}};
    if (k > 0.01f) rd.drawGlow(c, hd * 1.9f, acc.withAlphaF(0.30f * k));
    const Color fill = Color::lerp(Color{12, 20, 46, 205}, d.danger ? Color{70, 20, 28, 230} : Color{60, 48, 20, 218}, k);
    rd.fillConvexPolygon(pts, fill);
    strokePoly(ui, pts, Color::lerp(kLine, acc, k), 2.0f * u);
    const float is = 34.0f * u * (1.0f + 0.1f * k) * pop;
    menuIcon(ui, d.icon, Rect::fromCenter(c, {is, is}));
    bool fresh = !d.feature.empty() && ui.newBadge(id, hit, d.feature, false);
    if (isNew) *isNew = fresh;
    if (fresh) {  // a small gold mark at the top-left edge
        const Vec2 m{c.x - half * 0.62f, c.y - half * 0.62f};
        const float s = 4.5f * u * (1.0f + 0.15f * std::sin(t * 4.0f));
        rd.fillConvexPolygon({{m.x, m.y - s}, {m.x + s, m.y}, {m.x, m.y + s}, {m.x - s, m.y}}, kGold);
    }
    if (d.badge > 0) badgePill(ui, {c.x + half * 0.72f, c.y - half * 0.72f}, d.badge, 20.0f * u, Color{255, 77, 109, 255});
    return act;
}

// ---------------------------------------------------------------- holo card

void HoloCard::begin(UIContext& ui, const Rect& r, float t, float u, const Motion& m) {
    r_ = r;
    reduced_ = m.reduced;
    Renderer& rd = ui.r();
    rd.drawRoundedRect(r.expanded(3.0f * u), 17.0f * u, Color{94, 224, 255, 26}, 6.0f * u);
    rd.fillRoundedRect(r, 14.0f * u, Color{10, 18, 42, 190});
    rd.drawGlow(r.center(), r.w * 0.45f, Color{94, 224, 255, 18});
    rd.drawRoundedRect(r, 14.0f * u, Color{94, 224, 255, 115}, std::max(1.0f, u));
    const float p = clamp01((t - t0_) / 0.45f);
    float sy = 1.0f;
    if (!m.reduced && p < 1.0f) sy = p < 0.4f ? 0.02f + (1.04f - 0.02f) * applyEase(Ease::OutCubic, p / 0.4f) : 1.04f - 0.04f * ((p - 0.4f) / 0.6f);
    const float h = std::min(r.h, r.h * sy);
    rd.pushClip({r.x, r.center().y - h * 0.5f, r.w, h});
}

void HoloCard::end(UIContext& ui, float t) {
    Renderer& rd = ui.r();
    const float step = std::max(3.0f, r_.h / 80.0f);
    for (float y = r_.y + 2.0f; y < r_.bottom() - 2.0f; y += step) rd.fillRect({r_.x + 2.0f, y, r_.w - 4.0f, 1.0f}, Color{255, 255, 255, 9});
    const float p = clamp01((t - t0_) / 0.45f);
    if (!reduced_ && p < 1.0f) rd.fillRect(r_, Color::white().withAlphaF((1.0f - p) * 0.35f), BlendMode::Additive);
    rd.popClip();
}

void HoloCard::text(UIContext& ui, const Rect& r, const Text& tx, float u) {
    TextRenderer& tr = ui.text();
    float y = r.y;
    auto line = [&](const std::string& s, FontFace face, float refPx, Color c, float minScale, bool verbatim = false) {
        if (s.empty()) return;
        i18n::Verbatim v(verbatim);
        float f = ui.fitFont(s, face, fsz(ui, refPx, u), r.w, minScale);
        const std::string e = ui.elide(s, face, f, r.w);
        ui.label(e, {r.x, y}, f, c, Align::Left, face, VAlign::Top);
        y += tr.measure(e, face, ui.s(f)).y + 6.0f * u;
    };
    line(tx.kicker, FontFace::Bold, 12, kCyan, 0.8f);
    line(tx.head, FontFace::Title, 22, kInk, 0.6f, tx.headVerbatim);
    line(tx.big, FontFace::Title, 30, tx.bigColor, 0.6f);
    float bottom = r.bottom();
    if (!tx.meta.empty()) {
        const float f = fsz(ui, 13, u);
        const std::string e = ui.elide(tx.meta, FontFace::Body, f, r.w);
        const float h = tr.measure(e, FontFace::Body, ui.s(f)).y;
        ui.label(e, {r.x, r.bottom() - h}, f, kMuted, Align::Left, FontFace::Body, VAlign::Top);
        bottom = r.bottom() - h - 8.0f * u;
    }
    if (tx.para.empty() || bottom <= y) return;
    for (float ref : {15.0f, 14.0f, 13.0f, 12.0f}) {
        const float f = fsz(ui, ref, u);
        if (tr.measureWrapped(tx.para, FontFace::Body, ui.s(f), r.w).y <= bottom - y) {
            ui.labelWrapped(tx.para, {r.x, y}, r.w, f, kInk);
            return;
        }
    }
    const float f = fsz(ui, 12, u);  // one line, shortened, when it fits
    if (tr.measure(tx.para, FontFace::Body, ui.s(f)).y <= bottom - y)
        ui.label(ui.elide(tx.para, FontFace::Body, f, r.w), {r.x, y}, f, kInk, Align::Left, FontFace::Body, VAlign::Top);
}

// --------------------------------------------------------------- title logo

namespace {
struct Orbit {
    float cx, cy, rx, ry, rot;
    Vec2 at(float a, bool* back = nullptr) const {
        const float x = std::cos(a) * rx, y = std::sin(a) * ry;
        if (back) *back = std::sin(a) < 0.0f;
        return {cx + x * std::cos(rot) - y * std::sin(rot), cy + x * std::sin(rot) + y * std::cos(rot)};
    }
};
Orbit orbitOf(float cx, float top, float u) { return {cx, top + 110.0f * u, 330.0f * u, 62.0f * u, -0.1f}; }
}  // namespace

void TitleLogo::drawOrbit(UIContext& ui, float cx, float top, float u, float t, const Motion& m, bool back) {
    Renderer& rd = ui.r();
    const Orbit o = orbitOf(cx, top, u);
    const int dots = m.particles <= 0 ? 60 : 120;
    for (int i = 0; i < dots; ++i) {
        const float a = i * 2.0f * kPi / dots;
        bool b = false;
        const Vec2 p = o.at(a, &b);
        if (b != back) continue;
        const float tw = 0.35f + 0.35f * std::sin(t * 3.0f + i * 0.7f);
        const float s = (back ? 2.0f : 3.0f) * u;
        rd.fillRect({p.x - s * 0.5f, p.y - s * 0.5f, s, s}, back ? Color{120, 170, 255, 255}.withAlphaF(tw * 0.5f) : Color{170, 235, 255, 255}.withAlphaF(tw));
    }
    const float speed = 0.55f;
    const int trail = m.particles <= 0 ? 0 : m.particles == 1 ? 16 : 34;
    for (int k = trail; k >= 1; --k) {
        const float tk = t - k * (1.1f / trail), fade = 1.0f - static_cast<float>(k) / trail;
        bool b = false;
        const Vec2 p = o.at(tk * speed, &b);
        if (b != back) continue;
        if (back) {
            rd.fillRect({p.x - u, p.y - u, 2.0f * u, 2.0f * u}, Color{180, 220, 255, 255}.withAlphaF(fade * 0.35f));
        } else {
            const float s = (3.0f * fade + 1.0f) * u;
            const Vec2 j{std::sin(tk * 40.0f) * 3.0f * u, std::cos(tk * 33.0f) * 3.0f * u};
            rd.fillRect({p.x + j.x - s * 0.5f, p.y + j.y - s * 0.5f, s, s}, Color{255, 230, 160, 255}.withAlphaF(fade * 0.9f));
        }
    }
    bool b = false;
    const float a = t * speed;
    const Vec2 p = o.at(a, &b);
    if (b != back) return;
    const float size = 32.0f * (back ? 1.6f : 2.4f) * u;
    const bool movingRight = -std::sin(a) > 0.0f;
    drawNova(ui, "glide", 8.0f, t, p, size, !movingRight, Color::white().withAlphaF(back ? 0.55f : 1.0f));
}

void TitleLogo::draw(UIContext& ui, float cx, float top, float u, float t, const Motion& m) {
    i18n::Verbatim name;  // the game's name, never translated (single letters either)
    Renderer& rd = ui.r();
    TextRenderer& tr = ui.text();
    drawOrbit(ui, cx, top, u, t, m, true);

    // « COSMIC »: chrome letters dropping in one by one, rendered apart (gradient, sweep).
    const int bw = static_cast<int>(std::ceil(820.0f * u)), bh = static_cast<int>(std::ceil(150.0f * u));
    const Rect box{cx - 410.0f * u, top, static_cast<float>(bw), static_cast<float>(bh)};
    if (!target_ || target_->width() != bw || target_->height() != bh) target_ = rd.createTarget(bw, bh, true);
    const std::string word = "COSMIC";
    const float S = 104.0f * u, gap = 6.0f * u;
    float widths[6], total = gap * 5;
    for (int i = 0; i < 6; ++i) total += widths[i] = tr.measure(word.substr(i, 1), FontFace::Title, S).x;
    const float midY = 78.0f * u;
    auto letters = [&](Vec2 o) {
        float x = o.x + 410.0f * u - total * 0.5f;
        for (int i = 0; i < 6; ++i) {
            const float li = m.reduced ? clamp01((t - t0_) / 0.6f) : clamp01((t - t0_ - i * 0.09f) / 0.5f);
            const float e = 1.0f - std::pow(1.0f - li, 3.0f);
            const float y = o.y + midY - (m.reduced ? 0.0f : (1.0f - e) * 90.0f * u) + std::sin(t * 1.6f + i * 0.7f) * 2.5f * u;
            TextStyle st;
            st.face = FontFace::Title;
            st.size = S;
            st.align = Align::Center;
            st.valign = VAlign::Middle;
            const Vec2 c{x + widths[i] * 0.5f, y};
            st.color = Color{8, 16, 40, 255}.withAlphaF(li);
            for (int d = 0; d < 8; ++d) {
                const float ang = d * kPi / 4;
                tr.draw(word.substr(i, 1), {c.x + std::cos(ang) * 4.0f * u, c.y + std::sin(ang) * 4.0f * u}, st);
            }
            st.color = Color::white().withAlphaF(li);
            tr.draw(word.substr(i, 1), c, st);
            x += widths[i] + gap;
        }
    };
    if (target_) {
        SDL_Texture* prev = SDL_GetRenderTarget(rd.sdl());
        rd.setTarget(target_);
        SDL_SetRenderDrawColor(rd.sdl(), 0, 0, 0, 0);
        SDL_RenderClear(rd.sdl());
        const float op = rd.opacity();
        rd.setOpacity(1.0f);
        letters({0, 0});
        // Chrome: white at the top, a dark blue band at the middle, light below (multiplied).
        const float y0 = midY - S * 0.40f, y1 = midY + S * 0.40f, mid = y0 + (y1 - y0) * 0.48f;
        rd.fillGradientV({0, y0, box.w, mid - y0}, Color{255, 255, 255, 255}, Color{200, 239, 255, 255}, BlendMode::Multiply);
        rd.fillGradientV({0, mid, box.w, (y1 - mid) * 0.5f}, Color{44, 92, 168, 255}, Color{94, 200, 255, 255}, BlendMode::Multiply);
        rd.fillGradientV({0, mid + (y1 - mid) * 0.5f, box.w, (y1 - mid) * 0.5f + S * 0.2f}, Color{94, 200, 255, 255}, Color{232, 251, 255, 255},
                         BlendMode::Multiply);
        if (!m.reduced) {  // a light sweeping the word
            const float sx = (std::fmod(t * 0.45f, 1.6f) - 0.3f) * box.w;
            rd.fillGradientH({sx - 60.0f * u, 0, 60.0f * u, box.h}, Color{255, 255, 255, 0}, Color{255, 255, 255, 215}, BlendMode::Additive);
            rd.fillGradientH({sx, 0, 60.0f * u, box.h}, Color{255, 255, 255, 215}, Color{255, 255, 255, 0}, BlendMode::Additive);
        }
        rd.setOpacity(op);
        SDL_SetRenderTarget(rd.sdl(), prev);
        // Now and then, a chromatic split and a displaced band.
        if (!m.reduced && std::fmod(t, 5.5f) < 0.22f) {
            rd.drawTexture(*target_, nullptr, box.translated({-6.0f * u, 0}), Color{255, 80, 80, 140}, BlendMode::Additive);
            rd.drawTexture(*target_, nullptr, box.translated({6.0f * u, 0}), Color{80, 220, 255, 140}, BlendMode::Additive);
            const uint32_t h = static_cast<uint32_t>(t * 30.0f) * 2654435761u;
            const float band = (20.0f + (h % 60u)) * u, dx = (static_cast<float>((h >> 8) % 25u) - 12.0f) * u;
            Rect src{0, band, box.w, 18.0f * u};
            rd.drawTexture(*target_, &src, {box.x + dx, box.y + band, box.w, 18.0f * u});
        }
        rd.drawTexture(*target_, nullptr, box);
    } else {
        letters(box.pos());
    }

    // « PLATFORMER »: spaced neon, a letter dimming now and then.
    const std::string w2 = "PLATFORMER";
    const float S2 = 44.0f * u, sp = 10.0f * u;
    float ws[10], t2 = sp * 9;
    for (int i = 0; i < 10; ++i) t2 += ws[i] = tr.measure(w2.substr(i, 1), FontFace::Title, S2).x;
    float x2 = cx - t2 * 0.5f;
    const int flick = static_cast<int>(t * 7.0f) % 23;
    const float pk = m.reduced ? clamp01((t - t0_) / 0.6f) : clamp01((t - t0_ - 0.6f) / 0.6f);
    for (int i = 0; i < 10; ++i) {
        const float dim = (!m.reduced && i == flick % 10 && std::sin(t * 40.0f) > 0.0f) ? 0.35f : 1.0f;
        TextStyle st;
        st.face = FontFace::Title;
        st.size = S2;
        st.align = Align::Center;
        st.valign = VAlign::Middle;
        st.color = Color{159, 243, 255, 255}.withAlphaF(pk * dim);
        st.glow = 18.0f * u;
        st.glowColor = Color{94, 224, 255, 110}.withAlphaF(pk * dim * 0.45f);
        const Vec2 c{x2 + ws[i] * 0.5f, top + 168.0f * u};
        tr.draw(w2.substr(i, 1), c, st);
        st.glow = 0.0f;
        st.color = Color{232, 253, 255, 255}.withAlphaF(pk * dim * 0.8f);
        tr.draw(w2.substr(i, 1), c, st);
        x2 += ws[i] + sp;
    }
    drawOrbit(ui, cx, top, u, t, m, false);
}

// ------------------------------------------------------------ planet backdrop

void PlanetBackdrop::start(const ContentDatabase& content) {
    planets_.clear();
    std::vector<fs::path> files;
    for (const PlanetData* p : content.planets()) {
        if (p->teaser) continue;
        const auto levels = content.levelsForPlanet(p->id);
        if (levels.empty()) continue;
        planets_.push_back({p->id, p->name, p->color, {}});
        files.push_back(levels.front()->path);
    }
    // Each planet's look is its first level's: the sky, far, mid and near layers with their tints.
    loading_ = std::async(std::launch::async, [files] {
        std::vector<std::vector<std::pair<std::string, Color>>> out;
        for (const auto& f : files) {
            std::vector<std::pair<std::string, Color>> layers;
            if (auto j = FileUtil::loadJson(f); j && j->is_object() && (*j)["background"].is_array()) {
                for (const char* want : {"sky", "far", "mid", "near"})
                    for (const auto& l : (*j)["background"]) {
                        const std::string tex = jstr(l, "texture");
                        if (fs::path(tex).stem().string() != want) continue;
                        layers.push_back({tex, l.contains("tint") ? colorFromJson(l["tint"], Color::white()) : Color::white()});
                        break;
                    }
            }
            out.push_back(std::move(layers));
        }
        return out;
    });
}

void PlanetBackdrop::warpNow() {
    if (warpT_ < 0.0f && planets_.size() > 1) warpT_ = clock_, swapped_ = false;
}

void PlanetBackdrop::ensureMotes(const Motion& m) {
    if (moteQuality_ == m.particles) return;
    moteQuality_ = m.particles;
    motes_.clear();
    const int n = m.particles <= 0 ? 18 : m.particles == 1 ? 40 : 70;
    uint32_t s = 1234567u;
    auto rnd = [&s] {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    };
    for (int i = 0; i < n; ++i) motes_.push_back({rnd() * 1280.0f, rnd() * 720.0f, rnd() * 2.0f + 1.0f, rnd() * 6.0f, 4.0f + rnd() * 14.0f});
}

void PlanetBackdrop::update(UIContext& ui, float dt, const Motion& m) {
    clock_ += dt;
    ensureMotes(m);
    if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto layers = loading_.get();
        ResourceManager& res = ui.app().resources();
        for (size_t i = 0; i < planets_.size(); ++i) {
            if (i < layers.size()) planets_[i].layers = std::move(layers[i]);
            if (planets_[i].layers.empty())  // no level file: the planet's own folder, when it has one
                for (const char* l : {"sky", "far", "mid", "near"}) {
                    const std::string path = "backgrounds/" + planets_[i].id + "/" + l + ".png";
                    if (res.exists(path)) planets_[i].layers.push_back({path, Color::white()});
                }
        }
    }
    speed_ = 1.0f;
    if (warpT_ < 0.0f && clock_ >= nextWarp_) warpNow();
    if (warpT_ >= 0.0f) {
        const float u = (clock_ - warpT_) / kWarp;
        speed_ = 1.0f + std::sin(std::min(1.0f, u) * kPi) * (m.reduced ? 2.0f : 14.0f);
        if (u >= 0.5f && !swapped_) {
            swapped_ = true;
            current_ = planets_.empty() ? 0 : (current_ + 1) % static_cast<int>(planets_.size());
            preload_ = 0;
        }
        if (u >= 1.0f) warpT_ = -1.0f, nextWarp_ = clock_ + kCycle;
    }
    static const float kSpeed[4] = {3, 10, 22, 40};
    for (int i = 0; i < 4; ++i) scroll_[i] += kSpeed[i] * speed_ * dt;
    for (auto& mo : motes_) {
        mo.x -= mo.v * speed_ * dt;
        if (mo.x < -10.0f) mo.x += 1300.0f;
    }
    // The next planet's layers, one per frame, before its jump (no pause at the warp).
    if (planets_.size() > 1 && clock_ > 2.0f) {
        const Planet& next = planets_[(current_ + 1) % planets_.size()];
        if (preload_ < next.layers.size()) ui.app().resources().texture(next.layers[preload_++].first, true);
    }
}

void PlanetBackdrop::render(UIContext& ui, Vec2 warpCenter, float veilRight, float u, const Motion& m) {
    Renderer& rd = ui.r();
    const float W = ui.screenW(), H = ui.screenH();
    rd.fillRect({0, 0, W, H}, Color{5, 8, 20, 255});
    if (!planets_.empty()) {
        const Planet& p = planets_[std::clamp(current_, 0, static_cast<int>(planets_.size()) - 1)];
        ResourceManager& res = ui.app().resources();
        for (size_t i = 0; i < p.layers.size() && i < 4; ++i) {
            TexturePtr tex = res.texture(p.layers[i].first, true);
            if (!tex || tex->height() <= 0) continue;
            const float h = H, w = H * tex->width() / tex->height();
            const float off = std::fmod(scroll_[i] * u, w);
            for (float x = -off; x < W; x += w) rd.drawTexture(*tex, nullptr, {std::floor(x), 0, std::ceil(w) + 1.0f, h}, p.layers[i].second);
        }
    }
    rd.fillGradientV({0, 0, W, H * 0.3f}, Color{4, 6, 14, 150}, Color{4, 6, 14, 0});
    rd.fillGradientV({0, H * 0.65f, W, H * 0.35f}, Color{4, 6, 14, 0}, Color{4, 6, 14, 190});
    rd.fillGradientH({0, 0, std::max(1.0f, veilRight), H}, Color{4, 8, 20, 200}, Color{4, 8, 20, 0});
    // Motes twinkling (stretched into streaks during the jump).
    for (const auto& mo : motes_) {
        const Color c = Color{200, 240, 255, 255}.withAlphaF(0.25f + 0.35f * std::sin(clock_ * 2.0f + mo.p));
        const float x = mo.x * W / 1280.0f, y = mo.y * H / 720.0f;
        if (speed_ > 2.0f && !m.reduced) rd.fillRect({x, y, mo.s * speed_ * 2.0f * u, 1.2f * u}, c);
        else rd.fillRect({x, y, mo.s * u, mo.s * u}, c);
    }
    if (warpT_ >= 0.0f) {
        const float wu = (clock_ - warpT_) / kWarp, a = std::sin(std::min(1.0f, wu) * kPi);
        const float fl = std::max(0.0f, 1.0f - std::abs(wu - 0.5f) * 6.0f);
        if (!m.reduced) {
            const int n = m.particles <= 0 ? 25 : m.particles == 1 ? 50 : 90;
            for (int i = 0; i < n; ++i) {
                const float ang = i * 2.399f, r0 = (60.0f + std::fmod(i * 53.0f + clock_ * 1400.0f, 700.0f)) * u, len = (40.0f + a * 260.0f) * u;
                const Vec2 d{std::cos(ang), std::sin(ang)};
                rd.drawLine(warpCenter + d * r0, warpCenter + d * (r0 + len), Color{190, 230, 255, 255}.withAlphaF(a * 0.6f), 1.5f * u,
                            BlendMode::Additive);
            }
            if (fl > 0.0f) rd.fillRect({0, 0, W, H}, Color::white().withAlphaF(fl * 0.85f));
        } else if (fl > 0.0f) {
            rd.fillRect({0, 0, W, H}, Color{4, 6, 14, 255}.withAlphaF(fl * 0.9f));  // a fade, no flash
        }
    }
}

void PlanetBackdrop::drawNames(UIContext& ui, Vec2 c, float maxW, float fontRef, float u) {
    if (planets_.empty()) return;
    TextRenderer& tr = ui.text();
    const std::string sep = "  ·  ";
    float px = fontRef * u;
    auto widthAt = [&](float size) {
        float w = tr.measure(sep, FontFace::Bold, size).x * (planets_.size() - 1);
        for (const auto& p : planets_) w += tr.measure(p.name, FontFace::Bold, size).x;
        return w;
    };
    float total = widthAt(px);
    for (int i = 0; i < 8 && total > maxW; ++i) total = widthAt(px *= std::max(0.5f, maxW / total * 0.98f));
    if (total > maxW) return;  // no room at all: nothing rather than over something else
    const float cycle = warpT_ >= 0.0f ? 1.0f : clamp01(1.0f - (nextWarp_ - clock_) / kCycle);
    float x = c.x - total * 0.5f;
    const float sepW = tr.measure(sep, FontFace::Bold, px).x;
    for (size_t i = 0; i < planets_.size(); ++i) {
        const auto& p = planets_[i];
        const float w = tr.measure(p.name, FontFace::Bold, px).x;
        const bool on = static_cast<int>(i) == current_;
        TextStyle st;
        st.face = FontFace::Bold;
        st.size = px;
        st.valign = VAlign::Middle;
        st.color = on ? p.color : Color{200, 210, 235, 190};
        if (on) st.glow = 12.0f * u, st.glowColor = p.color.withAlpha(90);
        tr.draw(p.name, {x, c.y}, st);
        if (on) ui.r().fillRect({x, c.y + px * 0.75f, w * cycle, 2.0f * u}, p.color);
        x += w;
        if (i + 1 < planets_.size()) {
            st.glow = 0.0f;
            st.color = Color{200, 210, 235, 128};
            tr.draw(sep, {x, c.y}, st);
            x += sepW;
        }
    }
}

// ------------------------------------------------------------------- runner

void Runner::draw(UIContext& ui, float x0, float x1, float novaX, float groundY, float u, float t, const Motion& m) {
    Renderer& rd = ui.r();
    const float w = x1 - x0;
    rd.fillGradientH({x0, groundY, w * 0.3f, 2.0f * u}, Color{94, 224, 255, 0}, Color{94, 224, 255, 178});
    rd.fillGradientH({x0 + w * 0.3f, groundY, w * 0.7f, 2.0f * u}, Color{94, 224, 255, 178}, Color{94, 224, 255, 50});
    if (!m.reduced)
        for (int i = 0; i < 14; ++i) {
            const float dx = std::fmod(std::fmod(i * w / 12.0f - t * 260.0f * u, w) + w, w);
            if (dx > 26.0f * u && dx < w - 26.0f * u) rd.fillRect({x0 + dx, groundY + 6.0f * u, 26.0f * u, 2.0f * u}, Color{94, 224, 255, 115});
        }
    float lift = 0.0f;
    std::string clip = "run";
    float fps = 14.0f;
    if (jumpT_ >= 0.0f) {
        const float p = (t - jumpT_) / 0.75f;
        if (p >= 1.0f) {
            jumpT_ = -1.0f;
            landT_ = t;
        } else {
            lift = std::sin(p * kPi) * 120.0f * u;
            clip = p < 0.4f ? "double_jump" : "fall";
            fps = 12.0f;
        }
    }
    fillEllipse(ui, {novaX, groundY}, 40.0f * u * (1.0f - lift / (240.0f * u)), 6.0f * u, Color{0, 0, 0, 90});
    drawNova(ui, clip, fps, t, {novaX, groundY - 44.0f * u - lift}, 96.0f * u, false);
    const float lp = (t - landT_) / 0.5f;
    if (lp >= 0.0f && lp < 1.0f && m.particles > 0)
        for (float d : {-1.0f, 1.0f})
            rd.fillCircle({novaX + d * (20.0f + lp * 40.0f) * u, groundY - 4.0f * u - lp * 10.0f * u}, (6.0f + lp * 8.0f) * u,
                          Color{200, 230, 255, 255}.withAlphaF((1.0f - lp) * 0.5f));
}

}  // namespace cosmic::cstyle
