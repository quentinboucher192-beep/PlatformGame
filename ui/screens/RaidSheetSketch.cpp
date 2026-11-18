#include "ui/screens/RaidSheetSketch.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/raid/RaidSheets.h"
#include "ui/screens/CreatureArt.h"

namespace cosmic {

namespace {

constexpr float kPi = 3.14159265f;

/// A long announce or hold drawn shorter (the loop stays short; the caption gives the real one).
float shown(float seconds, float fallback, float most = 3.0f) { return std::clamp(seconds > 0.0f ? seconds : fallback, 0.3f, most); }

/// The players of the drawing: three allies in their colours (a capsule and a head).
const Color kAllies[] = {{120, 220, 255, 255}, {255, 210, 122, 255}, {150, 255, 170, 255}, {255, 150, 210, 255}};

struct Ctx {
    GameServices& g;
    Renderer& r;
    Rect f;
    AnimationSetPtr fx;
    const EnemyDefinition* boss;
    Color tint;
    float t;
    float floorY() const { return f.y + f.h * 0.84f; }
    float unit() const { return f.h / 180.0f; }  // the drawing's pixel (a frame 180 high)
};

void ally(const Ctx& c, Vec2 feet, int k, float alpha = 1.0f) {
    const float u = c.unit();
    const Color col = kAllies[k % 4].withAlphaF(alpha);
    c.r.fillRoundedRect({feet.x - 5.0f * u, feet.y - 16.0f * u, 10.0f * u, 13.0f * u}, 3.0f * u, col);
    c.r.fillCircle({feet.x, feet.y - 20.0f * u}, 4.5f * u, col);
}

/// A clip of the gigaboss's effects sheet in `dst` (its frames at `fps`).
bool clip(const Ctx& c, const std::string& name, const Rect& dst, float fps, Color tint = Color::white(), bool flip = false) {
    if (name.empty() || !c.fx || !c.fx->texture) return false;
    const AnimationClip* cl = c.fx->clip(name);
    if (!cl || cl->frameCount() == 0) return false;
    const Rect src = c.fx->frameRect(*cl, static_cast<int>(c.t * fps) % cl->frameCount());
    c.r.drawTexture(*c.fx->texture, &src, dst, tint, BlendMode::Alpha, flip);
    return true;
}

/// A clip of the sheet (48 × 144) standing on `feet`, `h` high.
Rect clipBox(Vec2 feet, float h) { return {feet.x - h / 6.0f, feet.y - h, h / 3.0f, h}; }

/// The gigaboss itself, in `box` (its idle clip).
void bossAt(const Ctx& c, const Rect& box, Color mul = Color::white()) {
    if (c.boss) drawCreature(c.g, *c.boss, box, c.t, mul, "idle");
}

void ground(const Ctx& c) {
    c.r.fillGradientV({c.f.x, c.f.y, c.f.w, c.f.h}, Color{14, 18, 40, 255}, Color{6, 8, 18, 255});
    c.r.fillRect({c.f.x, c.floorY(), c.f.w, c.f.bottom() - c.floorY()}, Color{30, 26, 40, 255});
    c.r.fillRect({c.f.x, c.floorY(), c.f.w, std::max(1.0f, 2.0f * c.unit())}, c.tint.withAlpha(150));
}

/// Marks under every player, then the blow (falling from above, or out of the ground).
void marks(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const int n = std::clamp(k.count > 0 ? k.count : 3, 1, 4);
    const float warn = shown(k.warn, 1.4f), cycle = warn + 1.4f, lt = std::fmod(c.t, cycle);
    bossAt(c, {c.f.x + c.f.w * 0.36f, c.f.y + 4.0f * u, c.f.w * 0.28f, c.f.h * 0.42f}, Color{255, 255, 255, 170});
    const float markW = 34.0f * u;
    for (int i = 0; i < n; ++i) {
        const float x0 = c.f.x + c.f.w * (static_cast<float>(i) + 1.0f) / static_cast<float>(n + 1);
        const Vec2 mark{x0, c.floorY()};
        // The player steps out once the mark is lit (half the announce), alternately left and right.
        const float out = std::clamp((lt - warn * 0.45f) / (warn * 0.35f), 0.0f, 1.0f);
        const float dir = i % 2 ? 1.0f : -1.0f;
        if (lt < warn + 0.6f) {
            const float p = std::clamp(lt / warn, 0.0f, 1.0f);
            c.r.drawRing(mark, markW * 0.5f, 2.0f * u, Color{255, 90, 40, static_cast<uint8_t>(120 + 120 * p)});
            c.r.drawArc(mark, markW * 0.35f, 3.0f * u, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * p, Color{255, 200, 120, 230});
        }
        ally(c, {x0 + dir * markW * 1.1f * out, c.floorY()}, i);
        if (lt >= warn && lt < warn + 0.6f) {  // the blow
            const float fall = std::clamp((warn + 0.25f - lt) / 0.25f, 0.0f, 1.0f);
            const float h = 70.0f * u;
            Rect dst = clipBox(mark, h);
            if (!k.rise) dst.y -= 60.0f * u * fall;
            if (!clip(c, k.clip, dst, 10.0f)) {  // no clip: a rock falling on the mark
                const float y = mark.y - 10.0f * u - 80.0f * u * fall;
                c.r.fillCircle({mark.x, y}, 10.0f * u, Color{120, 104, 96, 255});
                c.r.drawCircle({mark.x, y}, 10.0f * u, Color{60, 50, 48, 255}, 2.0f * u);
            }
        }
    }
}

/// A gaze per player from its eye: it follows, freezes (brighter), fires along that line.
void beams(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const float track = shown(k.track, 1.5f), lock = shown(k.lock, 0.45f, 1.5f), fire = shown(k.fire, 0.35f, 1.0f);
    const float cycle = track + lock + fire + 1.0f, lt = std::fmod(c.t, cycle);
    const Rect bb{c.f.center().x - c.f.w * 0.14f, c.f.y + 2.0f * u, c.f.w * 0.28f, c.f.h * 0.4f};
    bossAt(c, bb);
    const Vec2 eye{bb.center().x, bb.y + bb.h * 0.55f};
    for (int i = 0; i < 3; ++i) {
        const float base = c.f.x + c.f.w * (0.18f + 0.32f * static_cast<float>(i));
        auto where = [&](float time) { return base + std::sin(time * 1.7f + static_cast<float>(i) * 2.0f) * 24.0f * u; };
        const float aimT = std::min(lt, track);
        const Vec2 aim{where(std::floor(c.t / cycle) * cycle + aimT), c.floorY() - 10.0f * u};
        // The player: moving while tracked, stepping out of the line once it freezes.
        const float step = std::clamp((lt - track) / (lock + fire * 0.5f), 0.0f, 1.0f);
        const float px = lt < track ? where(c.t) : aim.x + (i % 2 ? 1.0f : -1.0f) * 30.0f * u * step;
        ally(c, {px, c.floorY()}, i);
        if (lt >= track + lock + fire) continue;
        const Vec2 d{aim.x - eye.x, aim.y - eye.y};
        const float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
        const Vec2 end{eye.x + d.x / len * c.f.h * 1.6f, eye.y + d.y / len * c.f.h * 1.6f};
        if (lt < track) c.r.drawLine(eye, end, c.tint.withAlpha(90), 1.5f * u);
        else if (lt < track + lock) c.r.drawLine(eye, end, c.tint.withAlpha(200), 2.5f * u);
        else {
            c.r.drawLine(eye, end, c.tint.withAlpha(140), 9.0f * u, BlendMode::Additive);
            c.r.drawLine(eye, end, Color{255, 255, 255, 230}, 3.0f * u);
        }
    }
}

/// Shockwaves (or a blade) along the floor; a player jumps over each.
void waves(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const int storeys = std::clamp(k.storeys, 1, 3);
    const float cycle = 2.6f, lt = std::fmod(c.t, cycle), p = lt / cycle;
    std::vector<float> floors = {c.floorY()};
    for (int s = 1; s < storeys; ++s) {
        const float y = c.floorY() - static_cast<float>(s) * 46.0f * u;
        floors.push_back(y);
        c.r.fillRect({c.f.x + 8.0f * u, y, c.f.w - 16.0f * u, 3.0f * u}, Color{90, 96, 130, 255});
    }
    const float h = 46.0f * u;
    if (!k.wall) bossAt(c, {c.f.center().x - c.f.w * 0.12f, c.f.y + 4.0f * u, c.f.w * 0.24f, c.floorY() - c.f.y - 4.0f * u});
    for (size_t s = 0; s < floors.size(); ++s) {
        const float fy = floors[s];
        std::vector<std::pair<float, bool>> crests;  // x, mirrored
        if (k.wall) crests.push_back({c.f.x + c.f.w * (p * 1.2f - 0.1f), false});
        else {
            crests.push_back({c.f.center().x + c.f.w * 0.5f * p, false});
            crests.push_back({c.f.center().x - c.f.w * 0.5f * p, true});
        }
        // A player on this storey jumps as the crest passes under them.
        const float px = c.f.x + c.f.w * (s % 2 ? 0.28f : 0.78f);
        float lift = 0.0f;
        for (const auto& [x, flip] : crests) {
            const float dist = std::abs(x - px) / (40.0f * u);
            if (dist < 1.0f) lift = std::max(lift, std::cos(dist * kPi * 0.5f) * 30.0f * u);
            if (!clip(c, k.clip, clipBox({x, fy}, h), 12.0f, Color::white(), flip))
                c.r.fillTriangle({x - 8.0f * u, fy}, {x + 8.0f * u, fy}, {x, fy - 16.0f * u}, c.tint);
        }
        ally(c, {px, fy - lift}, static_cast<int>(s));
    }
}

/// Plates held together (or targets broken together), then the gigaboss bare.
void plates(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const int n = std::clamp(k.count > 0 ? k.count : 3, 1, 6);
    const float walk = 0.5f, hold = shown(k.hold, 3.0f), cycle = walk * static_cast<float>(n) + hold + 2.0f, lt = std::fmod(c.t, cycle);
    const float doneAt = walk * static_cast<float>(n) + hold;
    const bool done = lt >= doneAt;
    const Rect bb{c.f.center().x - c.f.w * 0.13f, c.f.y + 4.0f * u, c.f.w * 0.26f, c.f.h * 0.48f};
    if (done) c.r.drawGlow(bb.center(), bb.h * 0.7f, c.tint.withAlpha(static_cast<uint8_t>(120 + 100 * std::sin(c.t * 8.0f))));
    bossAt(c, bb);
    for (int i = 0; i < n; ++i) {
        const float x = c.f.x + c.f.w * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const bool high = n > 2 && i % 2 == 1;  // some on ledges: the group splits
        const float y = high ? c.floorY() - 52.0f * u : c.floorY();
        if (high) c.r.fillRect({x - 22.0f * u, y, 44.0f * u, 3.0f * u}, Color{90, 96, 130, 255});
        const float arrive = walk * static_cast<float>(i + 1);
        const bool on = lt >= arrive;
        const float held = std::clamp((lt - walk * static_cast<float>(n)) / hold, 0.0f, 1.0f);
        if (k.breaks) {
            // A target struck: cracks as the window runs, all break together at the end.
            const float rad = 11.0f * u;
            const Vec2 tc{x, y - 40.0f * u};
            if (!done) {
                c.r.fillCircle(tc, rad, Color{150, 140, 170, 255});
                c.r.drawCircle(tc, rad, c.tint, 2.0f * u);
                if (on) c.r.drawLine({tc.x - rad * 0.6f, tc.y - rad * 0.4f}, {tc.x + rad * 0.5f * held, tc.y + rad * 0.5f}, Color{40, 30, 40, 255}, 2.0f * u);
            } else {
                for (int s = 0; s < 6; ++s) {
                    const float a = static_cast<float>(s) * kPi / 3.0f;
                    const float dd = (lt - doneAt) * 40.0f * u;
                    c.r.fillCircle({tc.x + std::cos(a) * dd, tc.y + std::sin(a) * dd}, 3.0f * u, Color{150, 140, 170, 200});
                }
            }
        } else {
            const std::string name = done && !k.clipDone.empty() ? k.clipDone : on && !k.clipHeld.empty() ? k.clipHeld : k.clip;
            if (!clip(c, name, clipBox({x, y}, 64.0f * u), 7.0f)) c.r.fillRect({x - 12.0f * u, y - 5.0f * u, 24.0f * u, 5.0f * u}, on ? c.tint : Color{120, 120, 140, 255});
            if (on && !done) {
                const Vec2 rc{x, y - 52.0f * u};
                c.r.drawRing(rc, 9.0f * u, 2.5f * u, Color{40, 30, 30, 200});
                c.r.drawArc(rc, 9.0f * u, 2.5f * u, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * held, Color{255, 210, 122, 255});
            }
        }
        // The player walks to it from the middle.
        const float wp = std::clamp(lt / arrive, 0.0f, 1.0f);
        const float sx = c.f.center().x + (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * 12.0f * u;
        const float px = sx + (x - sx) * wp + (k.breaks ? 16.0f * u : 0.0f);
        const float py = on ? y : c.floorY() + (y - c.floorY()) * std::clamp((wp - 0.7f) / 0.3f, 0.0f, 1.0f);
        ally(c, {px, py}, i);
    }
}

/// The arena cut in sectors: one announced, then turned over (its players fall upward).
void sectors(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const int n = std::clamp(k.count > 0 ? k.count : 4, 2, 6);
    const float warn = shown(k.warn, 2.0f), on = shown(k.time, 4.5f), cycle = warn + on + 0.8f;
    const int idx = static_cast<int>(std::floor(c.t / cycle)) % n;
    const float lt = std::fmod(c.t, cycle);
    const float sw = c.f.w / static_cast<float>(n);
    c.r.fillRect({c.f.x, c.f.y, c.f.w, 4.0f * u}, Color{180, 120, 255, 180});  // the vault's field
    for (int i = 0; i < n; ++i) {
        const Rect band{c.f.x + sw * static_cast<float>(i), c.f.y, sw, c.floorY() - c.f.y};
        if (i > 0) c.r.fillRect({band.x, band.y, std::max(1.0f, u), band.h}, Color{80, 80, 120, 120});
        // A slab under which a player shelters.
        const Rect slab{band.center().x - 14.0f * u, c.f.y + 30.0f * u, 28.0f * u, 4.0f * u};
        c.r.fillRect(slab, Color{110, 110, 150, 255});
        if (i == idx && lt < warn + on) {
            const bool flipped = lt >= warn;
            const Color col = flipped ? Color{170, 110, 255, 70} : Color{255, 170, 60, static_cast<uint8_t>(30 + 40 * std::sin(c.t * 10.0f) * std::sin(c.t * 10.0f))};
            c.r.fillRect(band, col);
            c.r.drawRect(band, flipped ? Color{190, 140, 255, 230} : Color{255, 190, 90, 230}, 2.0f * u);
            for (int a = 0; a < 2; ++a) {
                const float ax = band.x + band.w * (0.3f + 0.4f * static_cast<float>(a));
                const float ay = band.y + band.h * 0.55f - std::fmod(c.t * 30.0f * u, 20.0f * u);
                c.r.fillTriangle({ax - 6.0f * u, ay}, {ax + 6.0f * u, ay}, {ax, ay - 9.0f * u}, Color{255, 230, 180, 220});
            }
        }
        // Its player: the one in the turned sector falls to the vault, under the slab it is safe.
        float py = c.floorY();
        if (i == idx && lt >= warn && lt < warn + on) py = std::max(slab.bottom() + 24.0f * u, c.floorY() - (lt - warn) * 220.0f * u);
        ally(c, {band.center().x + (i % 2 ? 12.0f : -12.0f) * u, py}, i);
    }
}

/// A tide rising over the arena; the players climb on the high platforms.
void tide(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const float warn = shown(k.warn, 3.0f), hold = shown(k.time, 6.0f, 2.5f), cycle = warn + 1.0f + hold + 1.0f + 0.6f;
    const float lt = std::fmod(c.t, cycle);
    const float high = c.floorY() - 70.0f * u;
    for (float x : {0.22f, 0.5f, 0.78f}) c.r.fillRect({c.f.x + c.f.w * x - 26.0f * u, high, 52.0f * u, 4.0f * u}, Color{100, 130, 90, 255});
    float level = 0.0f;  // 0..1 of the tide's height
    if (lt >= warn && lt < warn + 1.0f) level = (lt - warn) / 1.0f;
    else if (lt >= warn + 1.0f && lt < warn + 1.0f + hold) level = 1.0f;
    else if (lt >= warn + 1.0f + hold && lt < warn + 2.0f + hold) level = 1.0f - (lt - warn - 1.0f - hold);
    const float top = c.floorY() - level * 52.0f * u;
    if (level > 0.0f) {
        c.r.fillRect({c.f.x, top, c.f.w, c.floorY() - top + 2.0f}, Color{90, 220, 80, 150});
        c.r.fillRect({c.f.x, top, c.f.w, 2.0f * u}, Color{190, 255, 150, 220});
    } else if (lt < warn) {
        const uint8_t a = static_cast<uint8_t>(60 + 60 * std::sin(c.t * 9.0f) * std::sin(c.t * 9.0f));
        c.r.fillRect({c.f.x, c.floorY() - 52.0f * u, c.f.w, 52.0f * u}, Color{120, 255, 100, static_cast<uint8_t>(a / 3)});
    }
    for (int i = 0; i < 3; ++i) {
        const float x = c.f.x + c.f.w * (0.22f + 0.28f * static_cast<float>(i));
        const float climb = std::clamp((lt - warn * 0.3f) / (warn * 0.5f), 0.0f, 1.0f);
        const float fall = std::clamp((lt - warn - 2.0f - hold) / 0.5f, 0.0f, 1.0f);
        const float y = c.floorY() + (high - c.floorY()) * std::max(0.0f, climb - fall);
        ally(c, {x, y}, i);
    }
}

/// Darkness: the eye-light sweeps the arena and shows the platforms it touches.
void cone(const Ctx& c, const raid::SheetSketch& k) {
    const float u = c.unit();
    c.r.fillRect(c.f, Color{3, 3, 8, 255});
    const Vec2 eye{c.f.center().x, c.f.y + 26.0f * u};
    bossAt(c, {eye.x - c.f.w * 0.1f, c.f.y - 10.0f * u, c.f.w * 0.2f, c.f.h * 0.34f}, Color{120, 110, 140, 255});
    const float period = std::clamp(k.period > 0.0f ? k.period : 7.0f, 2.0f, 8.0f);
    const float angle = kPi * 0.5f + std::sin(c.t * 2.0f * kPi / period) * 0.75f, spread = 0.28f;
    const float reach = c.f.h * 1.4f;
    const Vec2 a{eye.x + std::cos(angle - spread) * reach, eye.y + std::sin(angle - spread) * reach};
    const Vec2 b{eye.x + std::cos(angle + spread) * reach, eye.y + std::sin(angle + spread) * reach};
    c.r.fillTriangle(eye, a, b, Color{255, 240, 255, 45}, BlendMode::Additive);
    const float ys[] = {0.84f, 0.62f, 0.72f, 0.5f, 0.66f};
    for (int i = 0; i < 5; ++i) {
        const float x = c.f.x + c.f.w * (0.1f + 0.2f * static_cast<float>(i));
        const float y = c.f.y + c.f.h * ys[i];
        const float pa = std::atan2(y - eye.y, x - eye.x);
        const bool lit = std::abs(pa - angle) < spread;
        c.r.fillRect({x - 26.0f * u, y, 52.0f * u, 4.0f * u}, lit ? Color{230, 220, 255, 255} : Color{24, 22, 34, 255});
        if (i == 2) ally(c, {x, y}, 0, lit ? 1.0f : 0.35f);
    }
}

/// Its reinforcements out of their holes (the sprites of the enemies its rules name).
void summon(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    const int holes = std::clamp(k.count > 0 ? k.count : 3, 1, 5);
    const float cycle = 2.8f;
    bossAt(c, {c.f.center().x - c.f.w * 0.12f, c.f.y + 2.0f * u, c.f.w * 0.24f, c.f.h * 0.46f}, Color{255, 255, 255, 190});
    for (int i = 0; i < holes; ++i) {
        const float x = c.f.x + c.f.w * (static_cast<float>(i) + 0.5f) / static_cast<float>(holes);
        c.r.fillRect({x - 18.0f * u, c.floorY() - 2.0f * u, 36.0f * u, 6.0f * u}, Color{8, 4, 12, 255});
        if (k.enemies.empty()) continue;
        const EnemyDefinition* e = c.g.content.enemy(k.enemies[static_cast<size_t>(i) % k.enemies.size()]);
        if (!e) continue;
        // Out of the hole, one after the other, then up and away.
        const float lt = std::fmod(c.t + static_cast<float>(i) * 0.45f, cycle);
        const float rise = std::clamp(lt / 0.6f, 0.0f, 1.0f);
        const float sz = 30.0f * u;
        const Rect box{x - sz * 0.5f + (lt > 0.6f ? (lt - 0.6f) * 18.0f * u * (i % 2 ? 1.0f : -1.0f) : 0.0f), c.floorY() - sz * rise, sz, sz};
        c.r.pushClip({c.f.x, c.f.y, c.f.w, c.floorY() - c.f.y});
        drawCreature(c.g, *e, box, c.t, Color::white(), "idle");
        c.r.popClip();
    }
    ally(c, {c.f.x + c.f.w * 0.08f, c.floorY()}, 0);
}

/// A clip of the sheet in a loop (its announce, then the danger itself when `clipHeld` is set).
void loop(const Ctx& c, const raid::SheetSketch& k) {
    ground(c);
    const float u = c.unit();
    bossAt(c, {c.f.x + 6.0f * u, c.f.y + 6.0f * u, c.f.w * 0.36f, c.floorY() - c.f.y - 6.0f * u});
    const float cycle = 2.4f, lt = std::fmod(c.t, cycle);
    const std::string name = !k.clipHeld.empty() && lt >= 1.2f ? k.clipHeld : k.clip;
    const float h = c.floorY() - c.f.y - 10.0f * u;
    if (!clip(c, name, clipBox({c.f.x + c.f.w * 0.68f, c.floorY()}, h), 9.0f)) c.r.drawGlow({c.f.x + c.f.w * 0.68f, c.f.center().y}, h * 0.3f, c.tint);
    ally(c, {c.f.x + c.f.w * 0.9f, c.floorY()}, 0);
}

}  // namespace

void drawRaidSketch(GameServices& g, const Rect& frame, const raid::SheetSketch& k, const std::string& fx, const EnemyDefinition* boss, Color tint,
                    float t) {
    Renderer& r = g.app.renderer();
    Ctx c{g, r, frame, fx.empty() ? AnimationSetPtr{} : g.app.resources().animation(fx), boss, tint, t};
    if (k.kind == "mark") marks(c, k);
    else if (k.kind == "beam") beams(c, k);
    else if (k.kind == "wave") waves(c, k);
    else if (k.kind == "plates") plates(c, k);
    else if (k.kind == "zone") sectors(c, k);
    else if (k.kind == "rise") tide(c, k);
    else if (k.kind == "cone") cone(c, k);
    else if (k.kind == "summon") summon(c, k);
    else loop(c, k);
}

std::string raidSketchCaption(const raid::SheetSketch& k) {
    const std::string kind = k.kind;
    if (kind == "mark") return "Schéma animé · annonce " + sheetNumber(k.warn, 2) + " s, quittez la marque";
    if (kind == "beam") return "Schéma animé · suit " + sheetNumber(k.track, 2) + " s, se fige " + sheetNumber(k.lock, 2) + " s, tire";
    if (kind == "wave") return std::string("Schéma animé · sautez au passage");
    if (kind == "plates")
        return k.breaks ? "Schéma animé · " + std::to_string(k.count) + " cibles à briser en " + sheetNumber(k.hold, 2) + " s"
                        : "Schéma animé · " + std::to_string(k.count) + " à tenir ensemble, " + sheetNumber(k.hold, 2) + " s";
    if (kind == "zone") return "Schéma animé · annonce " + sheetNumber(k.warn, 2) + " s, puis " + sheetNumber(k.time, 2) + " s à l'envers";
    if (kind == "rise") return "Schéma animé · annonce " + sheetNumber(k.warn, 2) + " s, montez";
    if (kind == "summon") return "Schéma animé · " + std::to_string(k.count) + " trous, ses renforts en sortent";
    if (kind == "cone") return "Schéma animé · le faisceau balaie l'arène en " + sheetNumber(k.period, 2) + " s";
    return std::string("Animation du combat, planche d'effets du gigaboss");
}

}  // namespace cosmic
