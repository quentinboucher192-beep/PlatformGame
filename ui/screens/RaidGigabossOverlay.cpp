#include "ui/screens/RaidGigabossOverlay.h"

#include <algorithm>
#include <cmath>

#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/DreadfangFight.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/HeartbreakerFight.h"
#include "game/raid/SkullmawFight.h"

namespace cosmic::raidui {

namespace {

const Color kGoldC{255, 210, 122, 255};
const Color kGreenC{124, 240, 168, 255};
const Color kPinkC{255, 106, 213, 255};
const Color kVioletC{201, 168, 255, 255};
const Color kIceC{127, 246, 255, 255};
const Color kRedC{255, 90, 70, 255};
const Color kHeartC{255, 93, 143, 255};

uint8_t a8(float a) { return static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f); }

/// One frame of `clip` of `set`, drawn in the world rect `world` (its bottom on the ground).
void sprite(Renderer& r, const AnimationSetPtr& set, const char* clip, float clock, float fps, const Rect& world, const WorldView& view,
            bool flip = false, Color tint = Color::white(), BlendMode blend = BlendMode::Alpha, int frame = -1) {
    if (!set || !set->texture) return;
    const AnimationClip* c = set->clip(clip);
    if (!c) return;
    const int n = std::max(1, c->frameCount());
    const int f = frame >= 0 ? std::min(frame, n - 1) : static_cast<int>(clock * fps) % n;
    const Rect src = set->frameRect(*c, f);
    r.drawTexture(*set->texture, &src, view.toScreen(world), tint, blend, flip);
}

/// A ring with the share `k` of it drawn (a hold, a countdown).
void progressRing(Renderer& r, Vec2 c, float rad, float thick, float k, Color on) {
    r.drawRing(c, rad, thick, Color{30, 24, 36, 200});
    if (k > 0.0f) r.drawArc(c, rad, thick, -1.5708f, -1.5708f + 6.2832f * std::clamp(k, 0.0f, 1.0f), on);
}

/// A mark on the ground (a blow coming down on it in `k` 0..1 of its warning).
void groundMark(Renderer& r, Vec2 c, float rad, float scale, float k, Color col) {
    r.drawRing(c, rad, 2.0f * scale, col.withAlpha(static_cast<uint8_t>(120 + 120 * k)));
    r.drawArc(c, rad * 0.7f, 3.0f * scale, -1.5708f, -1.5708f + 6.2832f * k, Color{255, 236, 220, 230});
}

}  // namespace

void BossOverlay::draw(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock) {
    const raid::BossMechanic* m = gb.mechanic();
    if (!m || gb.stage() == raid::Gigaboss::Stage::Absent || gb.stage() == raid::Gigaboss::Stage::Fallen) return;
    if (dynamic_cast<const raid::SkullmawFight*>(m)) skullmaw(r, res, w, gb, view, clock);
    else if (dynamic_cast<const raid::DreadfangFight*>(m)) dreadfang(r, res, w, gb, view, clock);
    else if (dynamic_cast<const raid::HeartbreakerFight*>(m)) heartbreaker(r, res, w, gb, view, clock);
}

void BossOverlay::places(const raid::Gigaboss& gb, std::vector<std::pair<Rect, Color>>& out) {
    const raid::BossMechanic* m = gb.mechanic();
    if (const auto* s = dynamic_cast<const raid::SkullmawFight*>(m)) {
        for (const auto& p : s->mirrors().places) out.push_back({p.area, p.holders ? kGreenC : kPinkC});
    } else if (const auto* d = dynamic_cast<const raid::DreadfangFight*>(m)) {
        for (const auto& p : d->anchors().places) out.push_back({p.area, p.locked ? kGreenC : kGoldC});
    } else if (const auto* h = dynamic_cast<const raid::HeartbreakerFight*>(m)) {
        for (const auto& p : h->ribs().places) out.push_back({p.area, h->exposed() || p.holders ? kGreenC : kGoldC});
    }
}

// ------------------------------------------------------------------------ Skullmaw
void BossOverlay::skullmaw(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock) {
    const auto& f = *dynamic_cast<const raid::SkullmawFight*>(gb.mechanic());
    if (!skull_) skull_ = res.animation("animations/skullmaw_fx.json");
    const Enemy* b = gb.boss(w);
    const float flashAge = f.lastFlash() ? static_cast<float>(gb.fightTicks() - f.lastFlash()) / static_cast<float>(kSimHz) : 99.0f;
    // Its eye-light in the darkness (phase 3): a cone sweeping the arena; the platforms it touches show.
    if (f.dark() && b && b->alive()) {
        const Vec2 apex = f.brow(*b);
        const float ang = f.searchAngle(gb.fightTicks()), spread = f.searchSpread(), len = 1500.0f;
        const Vec2 l{apex.x + std::cos(ang - spread) * len, apex.y + std::sin(ang - spread) * len};
        const Vec2 rr{apex.x + std::cos(ang + spread) * len, apex.y + std::sin(ang + spread) * len};
        r.fillTriangle(view.toScreen(apex), view.toScreen(l), view.toScreen(rr), Color{201, 168, 255, 46}, BlendMode::Additive);
        const Rect area{std::min({apex.x, l.x, rr.x}), std::min({apex.y, l.y, rr.y}), std::abs(l.x - rr.x) + std::abs(apex.x - (l.x + rr.x) * 0.5f) * 2.0f,
                        std::max({apex.y, l.y, rr.y}) - std::min({apex.y, l.y, rr.y})};
        const Rect seen = area.expanded(32.0f);
        w.physics().forEachIn(seen, [&](const Collider& c) {
            if (!c.enabled) return;
            const float y = c.box.y;
            for (float x = std::max(c.box.x, seen.x); x < std::min(c.box.right(), seen.right()); x += 16.0f) {
                const float dx = x + 8.0f - apex.x, dy = y - apex.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > len || d < 1.0f) continue;
                float diff = std::atan2(dy, dx) - ang;
                while (diff > 3.14159f) diff -= 6.28318f;
                while (diff < -3.14159f) diff += 6.28318f;
                if (std::abs(diff) > spread) continue;
                const float k = 1.0f - std::abs(diff) / spread;
                r.drawLine(view.toScreen({x, y}), view.toScreen({std::min(x + 16.0f, c.box.right()), y}), Color{236, 220, 255, a8(0.35f + 0.6f * k)},
                           3.0f * view.scale, BlendMode::Additive);
            }
        });
    }
    // Its mirrors: lit while held, a burst at its flash; a ring on the free ones while it charges.
    for (const auto& p : f.mirrors().places) {
        const Rect world{p.area.center().x - 32.0f, p.area.bottom() - 96.0f, 64.0f, 96.0f};
        const char* clip = flashAge < 0.4f ? "mirror_flash" : p.holders ? "mirror_lit" : "mirror";
        sprite(r, skull_, clip, clock, 8.0f, world, view);
        if (f.dark()) r.fillCircle(view.toScreen(p.area.center()), 40.0f * view.scale, Color{255, 106, 213, 40}, BlendMode::Additive);
        if (f.charging() && !p.holders) {
            const float pulse = 0.5f + 0.5f * std::sin(clock * 10.0f);
            r.drawRing(view.toScreen({p.area.center().x, p.area.y - 40.0f}), 22.0f * view.scale, 3.0f * view.scale, kPinkC.withAlpha(a8(0.4f + 0.5f * pulse)));
        }
    }
    // Its flash charging: its brow glowing more and more, and toward each mirror a thin line.
    if (f.charging() && b && b->alive()) {
        const Vec2 e = view.toScreen(f.brow(*b));
        const float k = f.chargeRatio();
        r.fillCircle(e, (30.0f + 90.0f * k) * view.scale, Color{255, 236, 255, a8(0.15f + 0.35f * k)}, BlendMode::Additive);
        r.drawRing(e, (140.0f - 100.0f * k) * view.scale, 4.0f * view.scale, kPinkC.withAlpha(a8(0.5f + 0.4f * k)), BlendMode::Additive);
        for (const auto& p : f.mirrors().places)
            r.drawLine(e, view.toScreen(p.area.center()), Color{255, 106, 213, a8(0.15f + 0.25f * k)}, 2.0f * view.scale, BlendMode::Additive);
    }
    if (flashAge < 0.6f && b && b->alive()) {
        // The gaze thrown back: beams from the mirrors held to its eyes.
        for (const auto& p : f.mirrors().places)
            if (f.lastHeld() >= f.mirrorsNeeded() || p.holders)
                r.drawLine(view.toScreen(p.area.center()), view.toScreen(f.brow(*b)), Color{255, 236, 255, a8(1.0f - flashAge / 0.6f)}, 6.0f * view.scale,
                           BlendMode::Additive);
    }
    // Its gazes: following (thin), frozen (bright, pulsing), firing (a thick beam).
    for (const auto& z : f.gazes()) {
        const Vec2 a = view.toScreen(z.from), e = view.toScreen(z.end), aim = view.toScreen(z.aim);
        const float k = f.gazeProgress(z);
        switch (f.gazeState(z)) {
            case raid::SkullmawFight::GazeState::Tracking:
                r.drawLine(a, e, Color{201, 168, 255, a8(0.25f + 0.25f * k)}, 2.0f * view.scale, BlendMode::Additive);
                r.drawRing(aim, 16.0f * view.scale, 2.0f * view.scale, kVioletC.withAlpha(140));
                break;
            case raid::SkullmawFight::GazeState::Locked: {
                const float pulse = 0.5f + 0.5f * std::sin(clock * 30.0f);
                r.drawLine(a, e, Color{255, 106, 213, a8(0.55f + 0.4f * pulse)}, 4.0f * view.scale, BlendMode::Additive);
                r.drawRing(aim, (22.0f - 10.0f * k) * view.scale, 3.0f * view.scale, kPinkC);
                break;
            }
            case raid::SkullmawFight::GazeState::Firing:
                r.drawLine(a, e, Color{255, 120, 220, 200}, f.gazeWidth() * view.scale, BlendMode::Additive);
                r.drawLine(a, e, Color{255, 246, 255, 240}, f.gazeWidth() * 0.35f * view.scale, BlendMode::Additive);
                r.fillCircle(e, 14.0f * view.scale, Color{255, 220, 255, 200}, BlendMode::Additive);
                break;
        }
    }
    if (f.blind() && b && b->alive()) {
        // Blind: its eyes put out by its own light, a halo of stars.
        const Vec2 c = view.toScreen(f.brow(*b));
        for (int k = 0; k < 6; ++k) {
            const float ang = clock * 2.0f + static_cast<float>(k) * 1.0472f;
            r.fillCircle({c.x + std::cos(ang) * 70.0f * view.scale, c.y - 40.0f * view.scale + std::sin(ang) * 18.0f * view.scale}, 4.0f * view.scale,
                         Color{255, 236, 180, 220}, BlendMode::Additive);
        }
    }
}

// ------------------------------------------------------------------------ Dreadfang
void BossOverlay::dreadfang(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock) {
    const auto& f = *dynamic_cast<const raid::DreadfangFight*>(gb.mechanic());
    if (!claws_) claws_ = res.animation("animations/dreadfang_fx.json");
    const Enemy* b = gb.boss(w);
    // Its anchors and their chains (taut to its arm when locked).
    for (int k = 0; k < static_cast<int>(f.anchors().places.size()); ++k) {
        const auto& p = f.anchors().places[static_cast<size_t>(k)];
        const Rect world{p.area.center().x - 32.0f, p.area.bottom() - 160.0f, 64.0f, 160.0f};
        const char* clip = p.locked || f.chained() ? "anchor_locked" : p.holders && f.guardUp() ? "anchor_held" : "anchor";
        sprite(r, claws_, clip, clock, 8.0f, world, view);
        if ((p.locked || f.chained()) && b && b->alive()) {
            const Vec2 a = view.toScreen({p.area.center().x, p.area.y - 40.0f}), e = view.toScreen(b->box.center());
            const float sway = f.chained() ? 0.0f : std::sin(clock * 3.0f + static_cast<float>(k)) * 6.0f;
            const int links = 14;
            for (int i = 0; i <= links; ++i) {
                const float t = static_cast<float>(i) / links;
                const Vec2 q{a.x + (e.x - a.x) * t, a.y + (e.y - a.y) * t + std::sin(t * 3.14159f) * sway * view.scale};
                r.drawRing(q, 5.0f * view.scale, 2.0f * view.scale, Color{170, 220, 240, 230});
            }
        }
        if (f.guardUp() && !f.chained()) {
            const Vec2 c = view.toScreen({p.area.center().x, p.area.y - 56.0f});
            if (p.locked) progressRing(r, c, 14.0f * view.scale, 3.0f * view.scale, f.relockIn(k, gb.fightTicks()) / 12.0f, kGreenC);
            else if (f.holdRatio(k) > 0.0f) progressRing(r, c, 14.0f * view.scale, 3.0f * view.scale, f.holdRatio(k), kGoldC);
        }
    }
    // Claws from the ground: a mark under each player, then ice claws bursting out.
    const int warn = f.stabs().warnSteps();
    for (const auto& m : f.stabs().marks) {
        const float k = std::clamp(1.0f - static_cast<float>(m.left) / std::max(1.0f, static_cast<float>(warn)), 0.0f, 1.0f);
        groundMark(r, view.toScreen({m.at.x, m.at.y - 2.0f}), f.stabs().width * 0.5f * view.scale, view.scale, k, kIceC);
        if (m.left <= static_cast<int>(0.25f * kSimHz)) {
            const int frame = std::clamp(static_cast<int>((0.25f * kSimHz - static_cast<float>(m.left)) / (0.12f * kSimHz)), 0, 3);
            sprite(r, claws_, "claws", clock, 10.0f, {m.at.x - 32.0f, m.at.y - 156.0f, 64.0f, 160.0f}, view, false, Color::white(), BlendMode::Alpha, frame);
        }
    }
    // Its sweeps: the storeys announced (a band, chevrons toward where it goes), then its blade crossing.
    for (const auto& s : f.sweeps()) {
        for (int idx : s.storeys) {
            const float y = f.storeys()[static_cast<size_t>(idx)];
            if (!f.bladeOut(s)) {
                const float pulse = 0.5f + 0.5f * std::sin(clock * 14.0f);
                const Rect band = view.toScreen(Rect{f.spanX0(), y - f.bandHeight(), f.spanX1() - f.spanX0(), f.bandHeight()});
                r.fillRect(band, Color{255, 90, 70, a8(0.12f + 0.15f * pulse)}, BlendMode::Additive);
                for (float x = f.spanX0() + 80.0f; x < f.spanX1(); x += 240.0f) {
                    const Vec2 c = view.toScreen({x, y - f.bandHeight() * 0.5f});
                    const float d = 10.0f * view.scale * static_cast<float>(s.dir);
                    r.fillTriangle({c.x + d, c.y}, {c.x - d, c.y - 8.0f * view.scale}, {c.x - d, c.y + 8.0f * view.scale}, kRedC.withAlpha(a8(0.4f + 0.4f * pulse)));
                }
            } else {
                const float x = f.bladeX(s);
                const float trail = 220.0f * static_cast<float>(s.dir);
                const Rect streak = view.toScreen(Rect{std::min(x, x - trail), y - f.bandHeight(), std::abs(trail), f.bandHeight()});
                r.fillRect(streak, Color{180, 240, 255, 70}, BlendMode::Additive);
                sprite(r, claws_, "blade", clock, 16.0f, {x - 32.0f, y - 156.0f, 64.0f, 160.0f}, view, s.dir < 0);
            }
        }
    }
    // Its nailing claws: a reticle closing on the mark, then the player held (the claw is an enemy).
    for (const auto& p : f.pins()) {
        const Vec2 c = view.toScreen({p.at.x, p.at.y - 24.0f});
        if (!p.nailed) {
            const float k = f.pinWarnRatio(p);
            r.drawRing(c, (90.0f - 60.0f * k) * view.scale, 3.0f * view.scale, kRedC.withAlpha(a8(0.5f + 0.5f * k)));
            r.drawLine({c.x - 40.0f * view.scale, c.y}, {c.x + 40.0f * view.scale, c.y}, kRedC.withAlpha(160), 2.0f * view.scale);
            r.drawLine({c.x, c.y - 40.0f * view.scale}, {c.x, c.y + 40.0f * view.scale}, kRedC.withAlpha(160), 2.0f * view.scale);
        } else {
            const float pulse = 0.5f + 0.5f * std::sin(clock * 8.0f);
            r.drawRing(c, 34.0f * view.scale, 3.0f * view.scale, kIceC.withAlpha(a8(0.5f + 0.4f * pulse)), BlendMode::Additive);
        }
    }
}

// ------------------------------------------------------------------------ Heartbreaker
void BossOverlay::heartbreaker(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock) {
    const auto& f = *dynamic_cast<const raid::HeartbreakerFight*>(gb.mechanic());
    if (!cage_) cage_ = res.animation("animations/heartbreaker_fx.json");
    const Enemy* b = gb.boss(w);
    // Its beat: a ring from its heart (thick and red on a strong beat).
    if (b && b->alive() && f.lastBeat() > 0) {
        const float age = static_cast<float>(gb.fightTicks() - f.lastBeat()) / static_cast<float>(kSimHz);
        if (age < 0.6f) {
            const Vec2 heart = view.toScreen({b->box.center().x, b->box.bottom() - b->box.h * 0.53f});
            const float k = age / 0.6f;
            r.drawRing(heart, (60.0f + (f.lastStrong() ? 420.0f : 180.0f) * k) * view.scale, (f.lastStrong() ? 8.0f : 3.0f) * view.scale,
                       kHeartC.withAlpha(a8((1.0f - k) * (f.lastStrong() ? 0.9f : 0.5f))), BlendMode::Additive);
        }
    }
    // Its ribs to pry open (levers at the flank towers), the hold of both.
    for (const auto& p : f.ribs().places) {
        const Rect world{p.area.center().x - 32.0f, p.area.bottom() - 128.0f, 64.0f, 128.0f};
        const bool right = b && p.area.center().x > b->box.center().x;
        const char* clip = f.exposed() ? "rib_open" : p.holders && f.cage() ? "rib_held" : "rib";
        sprite(r, cage_, clip, clock, 8.0f, world, view, right);
        if (f.cage() && !f.exposed()) progressRing(r, view.toScreen({p.area.center().x, p.area.y - 40.0f}), 14.0f * view.scale, 3.0f * view.scale, f.pryRatio(),
                                                   p.holders ? kGreenC : kGoldC);
    }
    // Its rib traps: two ribs rising on both sides of each mark, then snapping shut.
    const int warn = f.traps().warnSteps();
    for (const auto& m : f.traps().marks) {
        const float k = std::clamp(1.0f - static_cast<float>(m.left) / std::max(1.0f, static_cast<float>(warn)), 0.0f, 1.0f);
        groundMark(r, view.toScreen({m.at.x, m.at.y - 2.0f}), f.traps().width * 0.5f * view.scale, view.scale, k, kHeartC);
        const int frame = m.left <= 0 ? 3 : std::clamp(static_cast<int>(k * 3.0f), 0, 2);
        const float half = f.traps().width * 0.5f;
        sprite(r, cage_, "trap", clock, 8.0f, {m.at.x - half - 16.0f, m.at.y - 128.0f, 64.0f, 128.0f}, view, false, Color::white(), BlendMode::Alpha, frame);
        sprite(r, cage_, "trap", clock, 8.0f, {m.at.x + half - 48.0f, m.at.y - 128.0f, 64.0f, 128.0f}, view, true, Color::white(), BlendMode::Alpha, frame);
    }
    // Its waves along the floor.
    for (const auto& v : f.waves()) {
        const float x = f.waveFront(v), y = f.floorY() > 0.0f ? f.floorY() : (b ? b->box.bottom() : 0.0f);
        sprite(r, cage_, "wave", clock, 12.0f, {x - 32.0f, y - 128.0f, 64.0f, 128.0f}, view, v.dir < 0);
        const float trail = 140.0f * static_cast<float>(v.dir);
        r.fillRect(view.toScreen(Rect{std::min(x, x - trail), y - f.waveHeight() * 0.5f, std::abs(trail), f.waveHeight() * 0.5f}), Color{255, 93, 143, 60},
                   BlendMode::Additive);
    }
    // Its plates' round: a ring around it counting the seconds before they grow back.
    if (f.armoured() && f.windowLeft() > 0.0f && b && b->alive()) {
        const Vec2 c = view.toScreen(b->box.center());
        progressRing(r, c, std::max(b->box.w, b->box.h) * 0.62f * view.scale, 4.0f * view.scale, f.windowLeft() / 20.0f, kRedC);
    }
}

}  // namespace cosmic::raidui
