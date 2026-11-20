#include "ui/screens/GiftArt.h"

#include <algorithm>
#include <cmath>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/ProfileBadgesUi.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::giftart {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = 57.2957795f;

Rect cellOf(int wrap, bool lid) {
    return {lid ? static_cast<float>(gifts::kWrapCell) : 0.0f, static_cast<float>(wrap * gifts::kWrapCell), static_cast<float>(gifts::kWrapCell),
            static_cast<float>(gifts::kWrapCell)};
}

/// A frame of `clip` of Nova's sheet in `look` (a suit of the catalogue), as the Collection draws it.
void drawSuit(GameServices& g, const std::string& look, const std::string& clip, float t, const Rect& dst) {
    AnimationSetPtr set = g.app.resources().animation("animations/nova.json");
    if (!set || !set->texture) return;
    const AnimationClip* c = set->clip(clip);
    if (!c) c = set->clip("idle");
    if (!c || c->frameCount() == 0) return;
    const float total = std::max(0.01f, c->totalDuration());
    float local = std::fmod(t, total), acc = 0.0f;
    int frame = 0;
    for (int i = 0; i < c->frameCount(); ++i) {
        acc += c->durations[static_cast<size_t>(i)];
        if (local < acc) {
            frame = i;
            break;
        }
    }
    TexturePtr tex = set->texture;
    if (TexturePtr s = g.suitSheet(look)) tex = s;
    const Rect src = set->frameRect(*c, frame);
    g.app.renderer().drawTexture(*tex, &src, dst);
}

/// A title as a gold plaque (the mockup's): dark gold, a gold edge, the text in pale gold, a star turning at each side.
void drawPlaque(GameServices& g, const std::string& text, Vec2 c, float size, float t) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    const float sc = std::max(0.01f, ui.scale());
    const float fsPx = std::max(9.0f, size * 0.17f);
    float fs = fsPx / sc;
    fs = ui.fitFont(text, FontFace::Bold, fs, size * 2.6f, 0.6f);
    const std::string shown = ui.elide(text, FontFace::Bold, fs, size * 2.6f);
    const float tw = ui.text().measure(shown, FontFace::Bold, ui.s(fs)).x;
    const float pw = tw + fsPx * 2.2f, ph = fsPx * 2.1f;
    const Rect plate{c.x - pw * 0.5f, c.y - ph * 0.5f, pw, ph};
    r.fillRoundedRectGradient(plate, ph * 0.3f, Color{58, 42, 16, 255}, Color{28, 20, 8, 255});
    r.drawRoundedRect(plate, ph * 0.3f, Color{255, 205, 110, 255}, std::max(1.5f, size * 0.03f));
    ui.label(shown, {c.x, c.y + 1.0f}, fs, Color{255, 230, 168, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
    star(r, {plate.x - fsPx * 0.6f, c.y}, fsPx * 0.45f, t, Color{255, 205, 110, 255});
    star(r, {plate.right() + fsPx * 0.6f, c.y}, fsPx * 0.45f, -t, Color{255, 205, 110, 255});
}

}  // namespace

void star(Renderer& r, Vec2 c, float radius, float rot, Color col, BlendMode blend) {
    Vec2 pts[8];
    for (int i = 0; i < 8; ++i) {
        const float rr = i % 2 ? radius * 0.38f : radius;
        const float a = rot + static_cast<float>(i) * kPi / 4.0f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 8; ++i) r.fillTriangle(c, pts[i], pts[(i + 1) % 8], col, blend);
}

void drawBox(GameServices& g, const std::string& wrap, Vec2 base, float px, const BoxPose& p) {
    TexturePtr tex = g.app.resources().texture(gifts::kWrapsTexture);
    if (!tex) return;
    const int w = std::max(0, gifts::wrapIndex(wrap));
    const float cw = gifts::kWrapCell * px * p.sx, ch = gifts::kWrapCell * px * p.sy;
    Renderer& r = g.app.renderer();
    // Turned around the middle of the bottom edge, like the body under it.
    const Rect src = cellOf(w, false);
    const Rect body{base.x - cw * 0.5f, base.y - ch, cw, ch};
    const Vec2 pivot{cw * 0.5f, ch};
    r.drawTexture(*tex, &src, body, p.tint, BlendMode::Alpha, false, false, p.rot * kDeg, &pivot);
    if (p.open) return;
    const float lift = (-gifts::kLidDy + -p.lidDy) * px * p.sy;  // above the body's cell
    const Rect lsrc = cellOf(w, true);
    const Rect lid{body.x, body.y - lift, cw, ch};
    const Vec2 lpivot{cw * 0.5f, ch + lift};
    r.drawTexture(*tex, &lsrc, lid, p.tint, BlendMode::Alpha, false, false, p.rot * kDeg, &lpivot);
}

void drawLid(GameServices& g, const std::string& wrap, Vec2 base, float px, Vec2 offset, float rot, float alpha) {
    TexturePtr tex = g.app.resources().texture(gifts::kWrapsTexture);
    if (!tex || alpha <= 0.0f) return;
    const int w = std::max(0, gifts::wrapIndex(wrap));
    const float cw = gifts::kWrapCell * px;
    const Rect src = cellOf(w, true);
    // The lid's cell where it sits on the closed box, then moved and turned around the middle of the box's top.
    const Rect dst{base.x - cw * 0.5f + offset.x, base.y - cw + gifts::kLidDy * px + offset.y, cw, cw};
    const Vec2 pivot{cw * 0.5f, cw * 0.72f};
    g.app.renderer().drawTexture(*tex, &src, dst, Color::white().withAlphaF(alpha), BlendMode::Alpha, false, false, rot * kDeg, &pivot);
}

std::string rewardName(GameServices& g, const gifts::Reward& r) {
    if (r.kind == "badge") {
        if (const AchievementDef* a = g.achievementCatalog.find(r.id)) return a->name;
    } else if (const CosmeticItem* it = g.cosmetics.find(r.id); it && it->category == r.kind) {
        return it->name;
    }
    return r.name.empty() ? r.id : r.name;
}

void drawReward(GameServices& g, OnlineUi* online, const gifts::Reward& rw, Vec2 c, float size, float t, bool reveal) {
    Renderer& r = g.app.renderer();
    if (rw.kind == "badge") {
        if (const AchievementDef* a = g.achievementCatalog.find(rw.id)) drawBadgeEmblem(g, *a, c, size * 0.36f);
        else star(r, c, size * 0.3f, t, Color{255, 214, 90, 255});
        return;
    }
    const CosmeticItem* it = g.cosmetics.find(rw.id);
    const std::string look = it ? it->look : std::string();
    if (rw.kind == "suit") {
        drawSuit(g, look.empty() ? "standard" : look, reveal ? "victory" : "idle", t, Rect::fromCenter(c, {size, size}));
    } else if (rw.kind == "title") {
        drawPlaque(g, look.empty() ? rewardName(g, rw) : look, c, size, t);
    } else if (rw.kind == "frame") {
        const float rad = size * 0.32f;
        if (online) {
            const std::string avatar = g.online.loggedIn() ? jstr(g.online.profile(), "avatar", "avatar_01") : std::string("avatar_01");
            online->drawAvatar(avatar, g.online.loggedIn() ? g.online.playerId() : std::string(), Rect::fromCenter(c, {rad * 2, rad * 2}));
        } else {
            r.fillCircle(c, rad, Color{12, 20, 50, 255});
        }
        drawAvatarFrame(r, c, rad, look.empty() ? "simple" : look, rad / 40.0f, t);
    } else if (rw.kind == "trail") {
        const Rect box = Rect::fromCenter(c, {size, size * 0.42f});
        r.pushClip(box.expanded(size * 0.1f));
        drawTrailSample(r, look.empty() ? "stardust" : look, box, t);
        r.popClip();
    } else if (rw.kind == "victory") {
        const Rect box = Rect::fromCenter(c, {size, size});
        if (look == "classic" || look.empty()) r.drawGlow(c, size * 0.4f, Color{57, 240, 216, 110});
        else drawVictoryFx(r, look, {c.x, box.bottom() - size * 0.08f}, std::fmod(t, 2.4f) + 0.15f, false, size / 220.0f);
    }
}

void drawGiftIcon(GameServices& g, OnlineUi* online, const gifts::Gift& gift, const Rect& box, float t) {
    if (gift.opened && gift.reward) {
        drawReward(g, online, *gift.reward, box.center(), box.w * 0.8f, t, false);
        return;
    }
    Renderer& r = g.app.renderer();
    const Color light = gifts::wrapLight(gift.wrap);
    const float phase = static_cast<float>(gift.id % 97);
    r.drawGlow({box.center().x, box.y + box.h * 0.6f}, box.w * 0.5f, light.withAlphaF(0.45f));
    const float px = box.w / 48.0f;
    BoxPose pose;
    pose.rot = std::sin(t * 5.0f + phase) * 0.03f;
    drawBox(g, gift.wrap, {box.center().x, box.y + box.h * 0.92f + std::sin(t * 2.4f + phase) * box.h * 0.035f}, px, pose);
}

void drawWrapIcon(GameServices& g, const std::string& wrap, const Rect& box, float t, float phase) {
    Renderer& r = g.app.renderer();
    r.drawGlow({box.center().x, box.y + box.h * 0.6f}, box.w * 0.5f, gifts::wrapLight(wrap).withAlphaF(0.4f));
    BoxPose pose;
    pose.rot = std::sin(t * 5.0f + phase) * 0.03f;
    drawBox(g, wrap, {box.center().x, box.y + box.h * 0.94f + std::sin(t * 2.4f + phase) * box.h * 0.03f}, box.w / 46.0f, pose);
}

}  // namespace cosmic::giftart
