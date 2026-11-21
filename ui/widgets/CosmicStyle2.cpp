#include "ui/widgets/CosmicStyle2.h"

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
#include "game/levels/LevelData.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::cstyle {

namespace {

constexpr float kPi = 3.14159265f;
constexpr Color kLine{44, 58, 104, 255};
constexpr Color kInk{230, 236, 250, 255};
constexpr Color kMuted{140, 152, 186, 255};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float fsz(UIContext& ui, float refPx, float unit) { return refPx * unit / ui.scale(); }

/// The two halves of `s` split at the space nearest its middle (the second empty: no space).
std::pair<std::string, std::string> splitMiddle(const std::string& s) {
    size_t best = std::string::npos;
    const size_t mid = s.size() / 2;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == ' ' && (best == std::string::npos || (i > mid ? i - mid : mid - i) < (best > mid ? best - mid : mid - best))) best = i;
    if (best == std::string::npos) return {s, {}};
    return {s.substr(0, best), s.substr(best + 1)};
}

}  // namespace

// ------------------------------------------------------------------- plate

bool plateButtonEx(UIContext& ui, Anims& anims, const std::string& id, const Rect& r, const PlateEx& px, float u, float appear, float t,
                   float dt, const Motion& m) {
    const Plate& p = px.plate;
    const bool act = ui.hotspot(id, r);
    const float k = anims.step("plate/" + id, ui.focused(id), dt, 12.0f);
    const float ea = m.reduced ? 1.0f : applyEase(Ease::OutBack, clamp01(appear));
    Renderer& rd = ui.r();
    const float op = rd.opacity();
    rd.setOpacity(op * clamp01(appear * 1.5f) * (px.enabled ? 1.0f : 0.62f));
    const Rect rr = r.translated({k * 16.0f * u + (1.0f - ea) * -120.0f * u, 0});
    const float cut = std::min(16.0f * u, rr.h * 0.3f);
    const auto poly = chamfer(rr, cut);
    if (k > 0.01f) {
        fillPolyH(ui, chamfer(rr.expanded(7.0f * u), cut * 1.2f), p.accent.withAlphaF(0.10f * k), p.accent.withAlphaF(0.02f * k));
        fillPolyH(ui, chamfer(rr.expanded(3.5f * u), cut * 1.1f), p.accent.withAlphaF(0.16f * k), p.accent.withAlphaF(0.04f * k));
    }
    if (px.selected) fillPolyH(ui, poly, Color::lerp(Color{16, 28, 62, 235}, p.accent, 0.22f), Color{10, 18, 42, 200});
    else fillPolyH(ui, poly, Color{16, 28, 62, 230}, Color{10, 18, 42, 190});
    strokePoly(ui, poly, Color::lerp(px.selected ? p.accent.withAlphaF(0.6f) : kLine, p.accent, k), std::max(1.0f, 2.0f * u));
    rd.fillRect({rr.x, rr.y + cut, 4.0f * u, rr.h - 2 * cut}, p.accent.withAlphaF(px.selected ? 1.0f : 0.35f + 0.65f * k));
    if (k > 0.01f) rd.drawGlow({rr.x + 2.0f * u, rr.center().y}, 18.0f * u, p.accent.withAlphaF(0.35f * k));
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
    const float is = std::min(40.0f * u, rr.h * 0.66f) * (1.0f + 0.18f * k);
    menuIcon(ui, p.icon, Rect::fromCenter({rr.x + 40.0f * u, rr.center().y}, {is, is}), -6.0f * k);
    float right = rr.right() - 18.0f * u;
    if (px.chevron) {
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
    if (px.rightW > 0.0f) {
        const Rect room{right - px.rightW, rr.y, px.rightW, rr.h};
        if (px.drawRight) px.drawRight(room, k);
        right = room.x - 8.0f * u;
    }
    const float x0 = rr.x + plateLabelLeft() * u, room = std::max(10.0f * u, right - x0);
    const std::string label = upper(i18n::tr(p.label));
    const bool hasSub = !p.sub.empty();
    const Color ink = Color::lerp(px.selected ? Color::white() : kInk, Color::white(), k);
    const float full = fsz(ui, 21, u);
    if (px.wrap && !hasSub && ui.text().measure(label, FontFace::Bold, ui.s(full) * 0.8f).x > room) {
        auto [a, b] = splitMiddle(label);
        if (!b.empty()) {
            const float f = std::min(ui.fitFont(a, FontFace::Bold, fsz(ui, 18, u), room, 0.6f), ui.fitFont(b, FontFace::Bold, fsz(ui, 18, u), room, 0.6f));
            const float lh = ui.text().measure("Ag", FontFace::Bold, ui.s(f)).y * 0.98f;
            ui.label(i18n::keep(ui.elide(i18n::keep(a), FontFace::Bold, f, room)), {x0, rr.center().y - lh * 0.5f}, f, ink, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            ui.label(i18n::keep(ui.elide(i18n::keep(b), FontFace::Bold, f, room)), {x0, rr.center().y + lh * 0.5f}, f, ink, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            if (!p.feature.empty()) ui.newBadge(id, rr, p.feature, true, true);
            rd.setOpacity(op);
            return act;
        }
    }
    const float ly = hasSub ? rr.y + rr.h * 0.37f : rr.center().y;
    const float f = ui.fitFont(label, FontFace::Bold, full, room, 0.6f);
    ui.label(ui.elide(label, FontFace::Bold, f, room), {x0, ly}, f, ink, Align::Left, FontFace::Bold, VAlign::Middle);
    if (hasSub) {
        const float sf = ui.fitFont(p.sub, FontFace::Body, fsz(ui, 13, u), room, 0.85f);
        ui.label(ui.elide(p.sub, FontFace::Body, sf, room), {x0, rr.y + rr.h * 0.70f}, sf, kMuted, Align::Left, FontFace::Body, VAlign::Middle);
    }
    if (!p.feature.empty()) ui.newBadge(id, rr, p.feature, true, true);
    rd.setOpacity(op);
    return act;
}

// ------------------------------------------------------------ chrome, neon

namespace {
/// The text shown (translated once, kept as it is) and its Title size fitted to `maxW`.
std::pair<std::string, float> fitTitle(UIContext& ui, const std::string& text, float sizePx, float maxW) {
    std::string s = i18n::keep(i18n::tr(text));
    float S = sizePx;
    const float w = ui.text().measure(s, FontFace::Title, S).x;
    if (w > maxW && w > 0.0f) S *= std::max(0.3f, maxW / w * 0.995f);
    return {s, S};
}
Vec2 anchor(Vec2 pos, float w, Align a) { return {a == Align::Left ? pos.x : a == Align::Center ? pos.x - w * 0.5f : pos.x - w, pos.y}; }
}  // namespace

Vec2 ChromeText::measure(UIContext& ui, const std::string& text, float sizePx, float maxW) {
    auto [s, S] = fitTitle(ui, text, sizePx, maxW);
    return ui.text().measure(s, FontFace::Title, S);
}

Vec2 ChromeText::draw(UIContext& ui, const std::string& text, Vec2 pos, float sizePx, float maxW, Align a, float t, const Motion& m, float u) {
    auto [s, S] = fitTitle(ui, text, sizePx, maxW);
    Renderer& rd = ui.r();
    TextRenderer& tr = ui.text();
    const Vec2 sz = tr.measure(s, FontFace::Title, S);
    const float pad = 6.0f * u;
    const Vec2 left = anchor(pos, sz.x, a);
    const int bw = static_cast<int>(std::ceil(sz.x + pad * 2)), bh = static_cast<int>(std::ceil(sz.y + pad * 2));
    const Rect box{left.x - pad, pos.y - bh * 0.5f, static_cast<float>(bw), static_cast<float>(bh)};
    const float li = m.reduced ? clamp01((t - t0_) / 0.3f) : clamp01((t - t0_) / 0.5f);
    const float drop = m.reduced ? 0.0f : (1.0f - applyEase(Ease::OutCubic, li)) * -18.0f * u;
    TextStyle st;
    st.face = FontFace::Title;
    st.size = S;
    st.valign = VAlign::Middle;
    auto letters = [&](Vec2 o) {
        st.color = Color{8, 16, 40, 255};
        for (int d = 0; d < 8; ++d) {
            const float ang = d * kPi / 4;
            tr.draw(s, {o.x + std::cos(ang) * 3.0f * u, o.y + std::sin(ang) * 3.0f * u}, st);
        }
        st.color = Color::white();
        tr.draw(s, o, st);
    };
    if (bw > 0 && bh > 0 && (!target_ || target_->width() != bw || target_->height() != bh)) target_ = rd.createTarget(bw, bh, true);
    const float op = rd.opacity();
    if (target_) {
        SDL_Texture* prev = SDL_GetRenderTarget(rd.sdl());
        rd.setTarget(target_);
        SDL_SetRenderDrawColor(rd.sdl(), 0, 0, 0, 0);
        SDL_RenderClear(rd.sdl());
        rd.setOpacity(1.0f);
        const float midY = bh * 0.5f;
        letters({pad, midY});
        const float y0 = midY - S * 0.40f, y1 = midY + S * 0.40f, mid = y0 + (y1 - y0) * 0.48f;
        rd.fillGradientV({0, y0, box.w, mid - y0}, Color{255, 255, 255, 255}, Color{200, 239, 255, 255}, BlendMode::Multiply);
        rd.fillGradientV({0, mid, box.w, (y1 - mid) * 0.5f}, Color{44, 92, 168, 255}, Color{94, 200, 255, 255}, BlendMode::Multiply);
        rd.fillGradientV({0, mid + (y1 - mid) * 0.5f, box.w, (y1 - mid) * 0.5f + S * 0.2f}, Color{94, 200, 255, 255}, Color{232, 251, 255, 255},
                         BlendMode::Multiply);
        if (!m.reduced) {
            const float sx = (std::fmod(t * 0.45f, 1.6f) - 0.3f) * box.w;
            rd.fillGradientH({sx - 40.0f * u, 0, 40.0f * u, box.h}, Color{255, 255, 255, 0}, Color{255, 255, 255, 200}, BlendMode::Additive);
            rd.fillGradientH({sx, 0, 40.0f * u, box.h}, Color{255, 255, 255, 200}, Color{255, 255, 255, 0}, BlendMode::Additive);
        }
        SDL_SetRenderTarget(rd.sdl(), prev);
        rd.setOpacity(op * li);
        rd.drawTexture(*target_, nullptr, box.translated({0, drop}));
        rd.setOpacity(op);
    } else {
        rd.setOpacity(op * li);
        letters({left.x, pos.y + drop});
        rd.setOpacity(op);
    }
    return sz;
}

Vec2 neonText(UIContext& ui, const std::string& text, Vec2 pos, float sizePx, float maxW, Align a, float t, const Motion& m, float u, Color c) {
    auto [s, S] = fitTitle(ui, text, sizePx, maxW);
    TextRenderer& tr = ui.text();
    const Vec2 sz = tr.measure(s, FontFace::Title, S);
    const float dim = (!m.reduced && std::fmod(t, 4.3f) < 0.12f && std::sin(t * 60.0f) > 0.0f) ? 0.55f : 1.0f;
    TextStyle st;
    st.face = FontFace::Title;
    st.size = S;
    st.valign = VAlign::Middle;
    st.align = a;
    st.color = c.withAlphaF(dim * c.a / 255.0f);
    st.glow = 16.0f * u;
    st.glowColor = c.withAlphaF(0.45f * dim);
    tr.draw(s, pos, st);
    st.glow = 0.0f;
    st.color = Color{232, 253, 255, 255}.withAlphaF(0.75f * dim);
    tr.draw(s, pos, st);
    return sz;
}

// ----------------------------------------------------------- planet scenery

void PlanetScenery::start(const ContentDatabase& content) {
    planets_.clear();
    ids_.clear();
    std::vector<fs::path> files;
    for (const PlanetData* p : content.planets()) {
        const auto levels = content.levelsForPlanet(p->id);
        planets_.push_back({p->id, {}});
        ids_.push_back(p->id);
        files.push_back(levels.empty() ? fs::path() : levels.front()->path);
    }
    loading_ = std::async(std::launch::async, [files] {
        std::vector<Layers> out;
        for (const auto& f : files) {
            Layers layers;
            if (!f.empty())
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

void PlanetScenery::show(const std::string& planetId, bool instant) {
    useLevel_ = false;
    if (instant || shown_.empty()) {
        shown_ = wanted_ = planetId;
        fade_ = instant ? 1.0f : 0.0f;
        return;
    }
    wanted_ = planetId;
}

void PlanetScenery::useLevel(const LevelData& level) {
    level_.clear();
    for (const char* want : {"sky", "far", "mid", "near"})
        for (const ParallaxLayerDef& l : level.background)
            if (!l.foreground && fs::path(l.texture).stem().string() == want) {
                level_.push_back({l.texture, l.tint});
                break;
            }
    if (level_.empty())  // other names: its first background layers, in order
        for (const ParallaxLayerDef& l : level.background)
            if (!l.foreground && level_.size() < 4) level_.push_back({l.texture, l.tint});
    useLevel_ = true;
    shown_ = wanted_ = level.planet;
    fade_ = 1.0f;
}

const PlanetScenery::Layers* PlanetScenery::layersOf(const std::string& id) const {
    if (useLevel_) return &level_;
    for (const auto& [pid, layers] : planets_)
        if (pid == id) return &layers;
    return nullptr;
}

void PlanetScenery::update(UIContext& ui, float dt, const Motion& m) {
    clock_ += dt;
    if (moteQuality_ != m.particles) {
        moteQuality_ = m.particles;
        motes_.clear();
        const int n = m.particles <= 0 ? 14 : m.particles == 1 ? 32 : 56;
        uint32_t s = 7654321u;
        auto rnd = [&s] {
            s = s * 1664525u + 1013904223u;
            return static_cast<float>(s >> 8) / 16777216.0f;
        };
        for (int i = 0; i < n; ++i) motes_.push_back({rnd() * 1280.0f, rnd() * 720.0f, rnd() * 2.0f + 1.0f, rnd() * 6.0f, 4.0f + rnd() * 14.0f});
    }
    for (auto& mo : motes_)
        if ((mo.x -= mo.v * dt) < -10.0f) mo.x += 1300.0f;
    ResourceManager& res = ui.app().resources();
    if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto layers = loading_.get();
        for (size_t i = 0; i < planets_.size(); ++i) {
            if (i < layers.size()) planets_[i].second = std::move(layers[i]);
            if (planets_[i].second.empty())
                for (const char* l : {"sky", "far", "mid", "near"}) {
                    const std::string path = "backgrounds/" + planets_[i].first + "/" + l + ".png";
                    if (res.exists(path)) planets_[i].second.push_back({path, Color::white()});
                }
        }
    }
    // The fade: out of the shown planet, then into the wanted one.
    if (wanted_ != shown_) {
        if ((fade_ -= dt / 0.22f) <= 0.0f) fade_ = 0.0f, shown_ = wanted_;
    } else {
        fade_ = std::min(1.0f, fade_ + dt / 0.3f);
    }
    // One texture per frame: the wanted planet's first, then the others' (a choice shows at once).
    auto loadOne = [&](const Layers* ls) {
        if (!ls) return false;
        for (const auto& [path, tint] : *ls)
            if (std::find(ready_.begin(), ready_.end(), path) == ready_.end()) {
                if (res.exists(path)) res.texture(path, true);
                ready_.push_back(path);
                return true;
            }
        return false;
    };
    if (!loadOne(layersOf(wanted_)) && !useLevel_ && clock_ > 0.5f)
        for (const auto& id : ids_)
            if (loadOne(layersOf(id))) break;
}

void PlanetScenery::render(UIContext& ui, float veilRight, float u, const Motion& m, float alpha) {
    Renderer& rd = ui.r();
    const float W = ui.screenW(), H = ui.screenH();
    const float op = rd.opacity();
    rd.setOpacity(op * clamp01(alpha));
    rd.fillRect({0, 0, W, H}, Color{5, 8, 20, 255});
    if (const Layers* ls = layersOf(shown_)) {
        ResourceManager& res = ui.app().resources();
        static const float kSpeed[4] = {3, 10, 22, 40};
        const float speed = m.reduced ? 0.4f : 1.0f;
        rd.setOpacity(op * clamp01(alpha) * applyEase(Ease::OutCubic, fade_));
        for (size_t i = 0; i < ls->size() && i < 4; ++i) {
            const auto& [path, tint] = (*ls)[i];
            if (std::find(ready_.begin(), ready_.end(), path) == ready_.end()) continue;  // not loaded yet: never on this frame
            TexturePtr tex = res.texture(path, true);
            if (!tex || tex->height() <= 0) continue;
            const float h = H, w = H * tex->width() / tex->height();
            const float off = std::fmod(clock_ * kSpeed[i] * speed * u, w);
            for (float x = -off; x < W; x += w) rd.drawTexture(*tex, nullptr, {std::floor(x), 0, std::ceil(w) + 1.0f, h}, tint);
        }
        rd.setOpacity(op * clamp01(alpha));
    }
    rd.fillGradientV({0, 0, W, H * 0.3f}, Color{4, 6, 14, 150}, Color{4, 6, 14, 0});
    rd.fillGradientV({0, H * 0.65f, W, H * 0.35f}, Color{4, 6, 14, 0}, Color{4, 6, 14, 190});
    if (veilRight > 0.0f) rd.fillGradientH({0, 0, veilRight, H}, Color{4, 8, 20, 200}, Color{4, 8, 20, 0});
    for (const auto& mo : motes_) {
        const Color c = Color{200, 240, 255, 255}.withAlphaF(0.25f + 0.35f * std::sin(clock_ * 2.0f + mo.p));
        rd.fillRect({mo.x * W / 1280.0f, mo.y * H / 720.0f, mo.s * u, mo.s * u}, c);
    }
    rd.setOpacity(op);
}

// --------------------------------------------------------------- star, nova

void star(UIContext& ui, Vec2 c, float r, float fill, Color on, Color off, Color edge) {
    auto points = [&](float R) {
        std::vector<Vec2> pts;
        for (int i = 0; i < 10; ++i) {
            const float a = -kPi / 2 + i * kPi / 5, rr = (i % 2 == 0) ? R : R * 0.45f;
            pts.push_back({c.x + std::cos(a) * rr, c.y + std::sin(a) * rr});
        }
        return pts;
    };
    auto fillStar = [&](float R, Color col) {
        if (R <= 0.5f) return;
        const auto p = points(R);
        ui.r().fillConvexPolygon({p[1], p[3], p[5], p[7], p[9]}, col);
        for (int i = 0; i < 10; i += 2) ui.r().fillConvexPolygon({p[(i + 9) % 10], p[i], p[i + 1]}, col);
    };
    fillStar(r, off);
    if (fill > 0.0f) fillStar(r * clamp01(fill), on);
    if (edge.a > 0) strokePoly(ui, points(r), edge, std::max(1.0f, r * 0.08f));
}

void novaClip(UIContext& ui, const std::string& clip, float time, Vec2 c, float size, bool flip, Color tint) {
    AnimationSetPtr set = ui.app().resources().animation("animations/nova.json");
    if (!set) return;
    const AnimationClip* cl = set->clip(clip);
    if (!cl || !set->texture || cl->frameCount() == 0) return;
    const float total = std::max(0.001f, cl->totalDuration());
    float at = std::max(0.0f, time);
    at = cl->loop ? std::fmod(at, total) : std::min(at, total - 0.0001f);
    int f = 0;
    for (float acc = 0.0f; f < cl->frameCount() - 1 && (acc += cl->durations[static_cast<size_t>(f)]) <= at;) ++f;
    Rect src = set->frameRect(*cl, f);
    ui.r().drawTexture(*set->texture, &src, Rect::fromCenter(c, {size, size}), tint, BlendMode::Alpha, flip);
}

// ----------------------------------------------------------- hints, caption

float hintWidth(UIContext& ui, Action a, const std::string& text, float height) {
    return Glyphs::measureAction(ui, a, height) + height * 0.3f + ui.text().measure(text, FontFace::Body, height * 0.62f).x;
}

float hintBar(UIContext& ui, const std::vector<Hint>& hints, Vec2 pos, float height, float gap, float maxW) {
    float x = pos.x;
    for (size_t i = 0; i < hints.size(); ++i) {
        const Hint& h = hints[i];
        if (x + hintWidth(ui, h.action, h.text, height) > pos.x + maxW) break;
        x += Glyphs::drawHint(ui, h.action, h.text, {x, pos.y}, height);
        if (i + 1 < hints.size()) x += h.gapAfter >= 0.0f ? h.gapAfter : gap;
    }
    return x - pos.x;
}

void caption(UIContext& ui, const std::string& text, const Rect& r, float fontRef, float u, Color c, bool isNew) {
    if (text.empty()) return;
    const float pill = isNew ? ui.newPillWidth(true) + 10.0f * u : 0.0f;
    const std::string s = upper(i18n::tr(text));
    const float f = ui.fitFont(s, FontFace::Bold, fsz(ui, fontRef, u), r.w - pill, 0.7f);
    const std::string e = ui.elide(s, FontFace::Bold, f, r.w - pill);
    const Vec2 sz = ui.label(e, {r.x, r.center().y}, f, c, Align::Left, FontFace::Bold, VAlign::Middle);
    if (isNew) ui.drawNewPill(r.x + sz.x + pill, r.center().y, true);
}

float extremeBadgeWidth(UIContext& ui, float height) {
    if (height <= 0.0f) return 0.0f;
    TexturePtr tex;
    if (i18n::language() == Language::English) tex = ui.app().resources().texture("ui/badge_extreme_en.png");
    if (!tex) tex = ui.app().resources().texture("ui/badge_extreme.png");
    if (tex && tex->height() > 0) return height * static_cast<float>(tex->width()) / static_cast<float>(tex->height());
    return ui.text().measure("EXTRÊME", FontFace::Bold, height * 0.55f).x + height;
}

}  // namespace cosmic::cstyle
