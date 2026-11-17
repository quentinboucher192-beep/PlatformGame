// Hazards: spikes, liquids (acid, water, lava), damage / slow zones and lasers.

#include <cmath>

#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/core/DetMath.h"

namespace cosmic {

namespace {
int quarterTurns(float rotation) {
    int q = static_cast<int>(std::lround(rotation / 90.0f)) % 4;
    return q < 0 ? q + 4 : q;
}
}  // namespace

// ------------------------------------------------------------------- spikes

void SpikesEntity::init(GameWorld& w) {
    tex_ = w.resources().texture("sprites/objects/spikes.png");
    // Forgiving hitbox: only the pointy part hurts.
    int q = quarterTurns(obj_.rotation);
    hitbox_ = box;
    switch (q) {
        case 0: hitbox_ = {box.x + 2, box.y + box.h * 0.45f, box.w - 4, box.h * 0.55f}; break;
        case 1: hitbox_ = {box.x, box.y + 2, box.w * 0.55f, box.h - 4}; break;
        case 2: hitbox_ = {box.x + 2, box.y, box.w - 4, box.h * 0.55f}; break;
        case 3: hitbox_ = {box.x + box.w * 0.45f, box.y + 2, box.w * 0.55f, box.h - 4}; break;
    }
}

void SpikesEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    if (obj_.prop<bool>("instantKill", false)) {
        if (!w.player().invulnerable()) w.player().kill(w);
        return;
    }
    // Direction the points face: 0 up, 1 right (90°), 2 down, 3 left (270°).
    int q = quarterTurns(obj_.rotation);
    Vec2 out = q == 0 ? Vec2{0, -1} : q == 1 ? Vec2{1, 0} : q == 2 ? Vec2{0, 1} : Vec2{-1, 0};
    DamageInfo d;
    d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
    d.type = DamageType::Hazard;
    d.source = q == 0 || q == 2 ? Vec2{w.player().center().x, box.center().y - out.y * 8.0f} : box.center() - out * 16.0f;
    d.knockback = 120.0f;
    if (w.damagePlayer(d)) {
        // Pop the player out of the spikes, away from their base (never into the wall).
        Vec2 push = q == 0 ? Vec2{0, -260} : q == 2 ? Vec2{0, 120} : Vec2{out.x * 200.0f, -150.0f};
        w.player().vel = push;
    }
}

void SpikesEntity::render(RenderContext& ctx) {
    if (!tex_) return;
    int q = quarterTurns(obj_.rotation);
    bool vertical = q == 1 || q == 3;
    float length = vertical ? box.h : box.w;
    int count = std::max(1, static_cast<int>(std::round(length / 16.0f)));
    for (int i = 0; i < count; ++i) {
        Rect src{static_cast<float>((i * 7 + static_cast<int>(box.x)) % 2) * 16.0f, 0, 16, 16};
        Rect dst;
        if (!vertical)
            dst = {box.x + i * 16.0f, box.bottom() - 16.0f, 16, 16};
        else
            dst = {box.x + (box.w - 16.0f) * 0.5f, box.y + i * 16.0f, 16, 16};
        if (q == 2) dst.y = box.y;
        ctx.r.drawWorldTexture(*tex_, &src, dst, Color::white(), BlendMode::Alpha, false, q * 90.0f);
    }
    ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, {176, 76, 255, 255});
}

// ------------------------------------------------------------------- liquid

void LiquidEntity::init(GameWorld& w) {
    kind_ = obj_.propStr("kind", "acid");
    tex_ = w.resources().texture("sprites/objects/liquid_" + kind_ + ".png");
}

bool LiquidEntity::swimmable() const { return kind_ == "water" || kind_ == "icewater"; }

void LiquidEntity::onPlayerEnter(GameWorld& w) {
    w.particles().emit(swimmable() ? "splash_water" : "splash_acid", {w.player().center().x, box.y}, 1.0f);
    w.playSound("sounds/land.wav", {w.player().center().x, box.y}, 0.6f, 0.7f);
}

void LiquidEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    Player& p = w.player();
    if (swimmable()) {
        p.inWater = true;
        return;
    }
    if (p.alive() && !p.respawning()) {
        w.particles().emit(kind_ == "lava" ? "lava_burn" : "acid_dissolve", p.center(), 1.0f);
        p.kill(w);
    }
}

void LiquidEntity::fixedUpdate(GameWorld& w, float dt) {
    // Enemies (walkers) die in deadly liquids.
    if (!swimmable()) {
        for (auto& e : w.enemies())
            if (e->alive() && !e->def().flying && overlapBox().intersects(e->box)) e->kill(w);
    }
    bubbleTimer_ -= dt;
    if (bubbleTimer_ <= 0.0f) {
        Rect vis = w.camera().visibleArea(1920, 1080).expanded(64);
        float x0 = std::max(box.x + 4, vis.x), x1 = std::min(box.right() - 4, vis.right());
        bubbleTimer_ = std::max(0.08f, 1.2f / std::max(1.0f, (x1 - x0) / 64.0f));
        if (x1 <= x0) return;
        float x = w.fxRng().range(x0, x1);
        const char* fx = kind_ == "acid" ? "acid_bubbles" : kind_ == "lava" ? "lava_bubbles" : "bubbles";
        w.particles().emit(fx, {x, box.y + 4}, 1.0f);
    }
}

void LiquidEntity::render(RenderContext& ctx) {
    if (!tex_) return;
    float amp = obj_.prop<float>("waveHeight", 1.0f);
    int frame = static_cast<int>(ctx.time * 8.0f) % 8;
    BlendMode bm = BlendMode::Alpha;
    const Rect vis = ctx.r.view().visibleWorld().expanded(32);
    float startX = box.x + std::max(0.0f, std::floor((vis.x - box.x) / 16.0f) * 16.0f);
    float endX = std::min(box.right(), vis.right());
    float endY = std::min(box.bottom(), vis.bottom() + 16.0f);
    for (float x = startX; x < endX - 0.5f; x += 16.0f) {
        float w = std::min(16.0f, box.right() - x);
        float bob = dmath::sin(ctx.time * 2.0f + x * 0.05f) * amp;
        Rect src{static_cast<float>(frame) * 16.0f, 0, w, 16};
        ctx.r.drawWorldTexture(*tex_, &src, {x, box.y + bob - 2, w, 16}, Color::white(), bm);
        for (float y = box.y + 14 + bob; y < endY - 0.5f; y += 16.0f) {
            float h = std::min(16.0f, box.bottom() - y);
            Rect bsrc{static_cast<float>((frame + static_cast<int>(x / 16)) % 8) * 16.0f, 16, w, h};
            ctx.r.drawWorldTexture(*tex_, &bsrc, {x, y, w, h}, Color::white(), bm);
        }
    }
    Color glow = kind_ == "acid"       ? Color{157, 255, 58, 255}
                 : kind_ == "lava"     ? Color{255, 120, 40, 255}
                 : kind_ == "icewater" ? Color{95, 176, 255, 255}
                                       : Color{31, 120, 140, 255};
    for (float x = std::floor(startX / 96.0f) * 96.0f + 40.0f; x < endX + 96.0f; x += 96.0f)
        if (x > box.x && x < box.right()) ctx.lights.add({x, box.y}, 110.0f, glow);
}

// -------------------------------------------------------------- damage zone

void DamageZoneEntity::init(GameWorld&) {
    // 2.33: a zone may start off (switched on by an event: the plasma of Singularis's arena).
    active = obj_.prop<bool>("active", true);
    shown_ = active ? 1.0f : 0.0f;
}

void DamageZoneEntity::onPlayerStay(GameWorld& w, float dt) {
    if (!active) return;
    Player& pl = w.player();
    if (obj_.prop<bool>("bounce", false) && pl.alive() && pl.vel.y > -200.0f) {
        // 2.33: it throws Nova back up (plasma: hurt, never stuck in it).
        pl.launch({pl.vel.x * 0.5f, -390.0f}, false);
    }
    tick_ -= dt;
    if (tick_ > 0.0f) return;
    tick_ = obj_.prop<float>("interval", 0.8f);
    if (obj_.prop<bool>("shieldProtects", true) && w.player().shieldActive()) return;
    DamageInfo d;
    d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
    d.type = DamageType::Hazard;
    d.source = box.center();
    d.knockback = 60.0f;
    d.ignoreShield = !obj_.prop<bool>("shieldProtects", true);
    w.damagePlayer(d);
}

void DamageZoneEntity::fixedUpdate(GameWorld& w, float dt) {
    if (!playerInside) tick_ = 0.0f;
    shown_ = active ? std::min(1.0f, shown_ + dt / 0.6f) : std::max(0.0f, shown_ - dt / 0.4f);
    if (!active || !obj_.prop<bool>("visible", true)) return;
    fxTimer_ -= dt;
    if (fxTimer_ <= 0.0f) {
        const std::string style = obj_.propStr("style", "toxic");
        bool plasma = style == "plasma";
        bool abyss = style == "abyss";  // 2.53: the dark water of the Kraken's arena
        bool embers = style == "embers";  // 2.55: the embers under the ash of the Phénix's nest
        fxTimer_ = std::max(0.05f, (plasma || abyss || embers ? 1.2f : 2.0f) / std::max(1.0f, box.w * box.h / 2000.0f));
        w.particles().emit(plasma ? "lava_bubbles" : (abyss ? "bubbles" : (embers ? "embers_rising" : "toxic_mist")),
                           {w.fxRng().range(box.x, box.right()), w.fxRng().range(box.y, box.bottom())}, 1.0f);
    }
}

bool DamageZoneEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    const std::string style = obj_.propStr("style", "toxic");
    if ((action == "enable" || action == "activate") && !active && (style == "plasma" || style == "abyss" || style == "embers")) {
        w.playSound("sounds/lava_rise.wav", box.center(), 0.9f);
        w.shake(0.3f);
    }
    if (action == "enable" || action == "activate") active = true;
    else if (action == "disable") active = false;
    else if (action == "toggle") active = !active;
    else return false;
    return true;
}

void DamageZoneEntity::render(RenderContext& ctx) {
    if (ctx.editor) {
        ctx.r.fillWorldRect(box, {255, 60, 60, 40});
        ctx.r.drawWorldRect(box, {255, 60, 60, 160});
    }
    if (!obj_.prop<bool>("visible", true) || shown_ <= 0.0f) return;
    if (obj_.propStr("style", "toxic") == "plasma") {
        renderPlasma(ctx);
        return;
    }
    if (obj_.propStr("style", "toxic") == "abyss") {
        renderAbyss(ctx);
        return;
    }
    if (obj_.propStr("style", "toxic") == "embers") {
        renderEmbers(ctx);
        return;
    }
    if (active) {
        ctx.r.fillWorldRect(box, {157, 255, 58, 18}, BlendMode::Additive);
        ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, {120, 200, 60, 255});
    }
}

void DamageZoneEntity::renderPlasma(RenderContext& ctx) const {
    // Star plasma (2.33): a pool (wider than tall: a boiling surface) or a curtain (taller
    // than wide: shimmering edges), fading in when switched on.
    const float a = shown_;
    auto fx = ctx.res.animation("animations/plasma_flood.json");
    const AnimationClip* surf = fx && fx->texture ? fx->clip("surface") : nullptr;
    const AnimationClip* body = fx && fx->texture ? fx->clip("body") : nullptr;
    const bool pool = box.w >= box.h;
    if (pool) {
        ctx.r.fillWorldRect(box, Color{120, 20, 92, static_cast<uint8_t>(225 * a)});
        if (surf && body) {
            int frame = static_cast<int>(ctx.time * 10.0f);
            float fw = static_cast<float>(fx->frameWidth), fh = static_cast<float>(fx->frameHeight);
            for (float x = box.x; x < box.right(); x += fw) {
                float wdt = std::min(fw, box.right() - x);
                Rect s = fx->frameRect(*surf, frame % std::max(1, surf->frameCount()));
                s.w = s.w * (wdt / fw);
                ctx.r.drawWorldTexture(*fx->texture, &s, {x, box.y - fh * 0.5f, wdt, fh}, Color{255, 255, 255, static_cast<uint8_t>(255 * a)});
                for (float y = box.y + fh * 0.5f; y < box.bottom(); y += fh) {
                    Rect b = fx->frameRect(*body, (frame + static_cast<int>(x / fw)) % std::max(1, body->frameCount()));
                    float hgt = std::min(fh, box.bottom() - y);
                    b.w = b.w * (wdt / fw);
                    b.h = b.h * (hgt / fh);
                    ctx.r.drawWorldTexture(*fx->texture, &b, {x, y, wdt, hgt}, Color{255, 255, 255, static_cast<uint8_t>(255 * a)});
                }
            }
        }
    } else {
        ctx.r.fillWorldRect(box, Color{255, 90, 200, static_cast<uint8_t>(70 * a)}, BlendMode::Additive);
        ctx.r.fillWorldRect(box, Color{90, 10, 70, static_cast<uint8_t>(90 * a)});
        // Bright, wavering inner edges and drifting bands of light.
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            float wv = 2.0f * dmath::sin(ctx.time * 5.0f + y * 0.07f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, Color{255, 200, 240, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, Color{255, 200, 240, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
        }
        for (int k = 0; k < 6; ++k) {
            float ph = std::fmod(ctx.time * 0.25f + static_cast<float>(k) / 6.0f, 1.0f);
            float y = box.bottom() - ph * box.h;
            ctx.r.fillWorldRect({box.x, y, box.w, 3.0f}, Color{255, 150, 230, static_cast<uint8_t>(60 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    }
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f)
        ctx.lights.add({x, pool ? box.y : box.center().y}, 120.0f, Color{255, 110, 220, static_cast<uint8_t>(255 * a)});
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 140.0f, Color{255, 110, 220, static_cast<uint8_t>(255 * a)});
}

void DamageZoneEntity::renderEmbers(RenderContext& ctx) const {
    // The embers under the ash of the Phénix's nest (2.55): a pool (grey ash crusted over a bed
    // of embers that breathe, their glow showing through, sparks rising) or a curtain (a wall of
    // fire, its heat wavering, ash swept up in it), fading in when switched on. Display only.
    const float a = shown_;
    const float t = ctx.time;
    const bool pool = box.w >= box.h;
    if (pool) {
        ctx.r.fillWorldRect(box, Color{46, 26, 22, static_cast<uint8_t>(235 * a)});
        ctx.r.fillWorldRect({box.x, box.y + 6.0f, box.w, std::max(0.0f, box.h - 6.0f)}, Color{255, 90, 30, static_cast<uint8_t>(40 * a)},
                            BlendMode::Additive);
        for (float x = box.x; x < box.right(); x += 8.0f) {
            const float wdt = std::min(8.0f, box.right() - x);
            const float breath = 0.5f + 0.5f * dmath::sin(t * 1.6f + x * 0.031f) * dmath::sin(t * 0.7f + x * 0.013f);
            ctx.r.fillWorldRect({x, box.y - 1.0f, wdt, 4.0f}, Color{120, 108, 100, static_cast<uint8_t>(230 * a)});
            ctx.r.fillWorldRect({x, box.y + 3.0f, wdt, 3.0f},
                                Color{255, static_cast<uint8_t>(110 + 90 * breath), 40, static_cast<uint8_t>(200 * a * breath)}, BlendMode::Additive);
        }
        const int sparks = static_cast<int>(box.w / 40.0f);
        for (int k = 0; k < sparks; ++k) {
            const float ph = std::fmod(t * 0.6f + static_cast<float>(k) * 0.618f, 1.0f);
            const float x = box.x + std::fmod(static_cast<float>(k) * 97.3f, box.w) + 5.0f * dmath::sin(t * 2.0f + static_cast<float>(k));
            const float y = box.y - ph * 60.0f;
            ctx.r.fillWorldRect({x, y, 2.0f, 2.0f}, Color{255, 190, 90, static_cast<uint8_t>(220 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    } else {
        ctx.r.fillWorldRect(box, Color{255, 100, 40, static_cast<uint8_t>(70 * a)}, BlendMode::Additive);
        ctx.r.fillWorldRect(box, Color{60, 20, 14, static_cast<uint8_t>(100 * a)});
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            const float wv = 2.0f * dmath::sin(t * 6.0f + y * 0.09f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, Color{255, 200, 120, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, Color{255, 200, 120, static_cast<uint8_t>(200 * a)}, BlendMode::Additive);
        }
        for (int k = 0; k < 8; ++k) {
            const float ph = std::fmod(t * 0.5f + static_cast<float>(k) / 8.0f, 1.0f);
            const float x = box.x + box.w * (0.1f + 0.8f * std::fmod(static_cast<float>(k) * 0.381f, 1.0f));
            const float y = box.bottom() - ph * box.h;
            ctx.r.fillWorldRect({x, y, 3.0f, 18.0f}, Color{255, 170, 80, static_cast<uint8_t>(80 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    }
    const Color glow{255, 120, 50, static_cast<uint8_t>(255 * a)};
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f) ctx.lights.add({x, pool ? box.y : box.center().y}, 120.0f, glow);
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 140.0f, glow);
}

void DamageZoneEntity::renderAbyss(RenderContext& ctx) const {
    // The dark water of the abyss (2.53, the Kraken's arena): a pool (a swell lit in cyan, foam
    // on its crests, bubbles rising) or a curtain (a wall of falling water, wavering edges),
    // fading in when switched on. Display only (time-driven).
    const float a = shown_;
    const float t = ctx.time;
    const bool pool = box.w >= box.h;
    const Color deep{10, 22, 52, static_cast<uint8_t>(228 * a)};
    const Color crest{122, 230, 255, static_cast<uint8_t>(210 * a)};
    if (pool) {
        ctx.r.fillWorldRect(box, deep);
        ctx.r.fillWorldRect({box.x, box.y + 8.0f, box.w, std::max(0.0f, box.h - 8.0f)}, Color{60, 150, 230, static_cast<uint8_t>(30 * a)},
                            BlendMode::Additive);
        for (float x = box.x; x < box.right(); x += 8.0f) {
            const float wdt = std::min(8.0f, box.right() - x);
            const float swell = 2.5f * dmath::sin(t * 1.8f + x * 0.025f) + 1.2f * dmath::sin(t * 4.3f + x * 0.11f);
            ctx.r.fillWorldRect({x, box.y - 2.0f + swell, wdt, 4.0f}, Color{30, 70, 120, static_cast<uint8_t>(240 * a)});
            ctx.r.fillWorldRect({x, box.y - 2.0f + swell, wdt, 2.0f}, crest, BlendMode::Additive);
            if (swell < -2.2f) ctx.r.fillWorldRect({x, box.y - 4.0f + swell, wdt, 2.0f}, Color{235, 250, 255, static_cast<uint8_t>(180 * a)}, BlendMode::Additive);
        }
        const int bubbles = static_cast<int>(box.w / 48.0f);
        for (int k = 0; k < bubbles; ++k) {
            const float ph = std::fmod(t * 0.5f + static_cast<float>(k) * 0.618f, 1.0f);
            const float x = box.x + std::fmod(static_cast<float>(k) * 97.3f, box.w) + 3.0f * dmath::sin(t * 3.0f + static_cast<float>(k));
            const float y = box.bottom() - ph * box.h;
            ctx.r.drawWorldCircle({x, y}, 1.2f + 1.2f * ph, Color{200, 240, 255, static_cast<uint8_t>(140 * a * ph)}, 1.0f);
        }
    } else {
        ctx.r.fillWorldRect(box, deep);
        ctx.r.fillWorldRect(box, Color{60, 150, 230, static_cast<uint8_t>(40 * a)}, BlendMode::Additive);
        for (float y = box.y; y < box.bottom(); y += 6.0f) {
            const float wv = 2.0f * dmath::sin(t * 5.0f + y * 0.07f);
            ctx.r.fillWorldRect({box.x - 1.0f + wv, y, 3.0f, 6.0f}, crest, BlendMode::Additive);
            ctx.r.fillWorldRect({box.right() - 2.0f - wv, y, 3.0f, 6.0f}, crest, BlendMode::Additive);
        }
        // Streaks of water falling down the curtain.
        for (int k = 0; k < 8; ++k) {
            const float ph = std::fmod(t * 0.6f + static_cast<float>(k) / 8.0f, 1.0f);
            const float x = box.x + box.w * (0.1f + 0.8f * std::fmod(static_cast<float>(k) * 0.381f, 1.0f));
            const float y = box.y + ph * box.h;
            ctx.r.fillWorldRect({x, y, 2.0f, 26.0f}, Color{170, 230, 255, static_cast<uint8_t>(70 * a * (1.0f - ph))}, BlendMode::Additive);
        }
    }
    const Color glow{90, 200, 255, static_cast<uint8_t>(255 * a)};
    for (float x = box.x + 40.0f; x < box.right(); x += 120.0f) ctx.lights.add({x, pool ? box.y : box.center().y}, 120.0f, glow);
    if (!pool)
        for (float y = box.y + 60.0f; y < box.bottom(); y += 140.0f) ctx.lights.add({box.center().x, y}, 140.0f, glow);
}

// ---------------------------------------------------------------- slow zone

void SlowZoneEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    w.player().speedMultiplier = std::min(w.player().speedMultiplier, obj_.prop<float>("factor", 0.5f));
}

namespace {
// Cloud look of the slow zones: tint + particle presets per style.
struct CloudStyle {
    Color tint;
    const char* motes;
    const char* puffs;
};
CloudStyle cloudStyle(const std::string& s) {
    if (s == "frost") return {{127, 246, 255, 255}, "frosted", "frost_mist"};
    if (s == "ash") return {{170, 150, 140, 255}, "ash_fall", "ash_cloud"};
    return {{224, 138, 255, 255}, "spores", "spore_cloud"};
}
}  // namespace

void SlowZoneEntity::fixedUpdate(GameWorld& w, float dt) {
    if (!obj_.prop<bool>("visible", true)) return;
    CloudStyle st = cloudStyle(obj_.propStr("style", "spores"));
    float area = std::max(1.0f, box.w * box.h / 3000.0f);
    fxTimer_ -= dt;
    if (fxTimer_ <= 0.0f) {
        fxTimer_ = std::max(0.04f, 0.9f / area);
        w.particles().emit(st.motes, {w.fxRng().range(box.x, box.right()), w.fxRng().range(box.y, box.bottom())}, 1.0f);
        if (w.fxRng().chance(0.85f))
            w.particles().emit(st.puffs, {w.fxRng().range(box.x + 8, box.right() - 8), w.fxRng().range(box.y + 8, box.bottom() - 4)},
                               1.0f);
    }
}

void SlowZoneEntity::render(RenderContext& ctx) {
    if (ctx.editor) ctx.r.drawWorldRect(box, {120, 120, 255, 160});
    if (!obj_.prop<bool>("visible", true)) return;
    CloudStyle st = cloudStyle(obj_.propStr("style", "spores"));
    // Soft, slowly drifting glowing blobs (no hard edge) give the cloud its volume.
    int n = std::clamp(static_cast<int>(box.w * box.h / 1100.0f), 4, 40);
    for (int i = 0; i < n; ++i) {
        float hx = std::fmod(dmath::sin(static_cast<float>(i) * 12.9898f + box.x) * 43758.5453f, 1.0f);
        float hy = std::fmod(dmath::sin(static_cast<float>(i) * 78.233f + box.y) * 12345.678f, 1.0f);
        hx = hx < 0 ? hx + 1.0f : hx;
        hy = hy < 0 ? hy + 1.0f : hy;
        float drift = std::fmod(hx + ctx.time * (0.008f + 0.004f * static_cast<float>(i % 3)), 1.0f);
        Vec2 c{box.x + 10.0f + drift * std::max(0.0f, box.w - 20.0f),
               box.y + 10.0f + hy * std::max(0.0f, box.h - 20.0f) + dmath::sin(ctx.time * 0.7f + static_cast<float>(i)) * 3.0f};
        float r = 18.0f + 16.0f * std::fmod(hx * 7.0f, 1.0f);
        ctx.r.drawWorldGlow(c, r, st.tint.withAlpha(52));
    }
    ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, st.tint.withAlpha(120));
}

// -------------------------------------------------------------------- laser

void LaserEntity::init(GameWorld& w) {
    enabled_ = obj_.prop<bool>("active", true);
    cycle_ = obj_.prop<float>("phase", 0.0f);
    emitter_.load(w.resources(), "animations/laser_emitter.json", "off");
    beam_ = w.resources().texture("sprites/objects/laser_beam.png");
    end_ = origin();
}

Vec2 LaserEntity::origin() const {
    float a = degToRad(obj_.rotation);
    Vec2 dir{dmath::cos(a), dmath::sin(a)};
    return box.center() - dir * (box.w * 0.5f - 8.0f);
}

void LaserEntity::fixedUpdate(GameWorld& w, float dt) {
    emitter_.anim.update(dt);
    hitTimer_ = std::max(0.0f, hitTimer_ - dt);
    float onT = obj_.prop<float>("onTime", 1.5f), offT = obj_.prop<float>("offTime", 1.5f);
    float warnT = std::min(obj_.prop<float>("warnTime", 0.6f), offT);
    bool wasOn = on_;
    if (!enabled_) {
        on_ = warning_ = false;
    } else if (offT <= 0.0f) {
        on_ = true;
        warning_ = false;
    } else {
        cycle_ += dt;
        float period = onT + offT;
        float t = std::fmod(cycle_, period);
        on_ = t < onT;
        warning_ = !on_ && t > period - warnT;
    }
    if (on_ != wasOn) {
        emitter_.anim.play(on_ ? "on" : "off", true);
        if (on_) w.playSound("sounds/laser_fire.wav", box.center(), 0.5f);
    }
    if (warning_ && emitter_.anim.current() != "charging") {
        emitter_.anim.play("charging", true);
        w.playSound("sounds/laser_charge.wav", box.center(), 0.35f);
    }
    float a = degToRad(obj_.rotation);
    Vec2 o = origin();
    Vec2 far = o + Vec2{dmath::cos(a), dmath::sin(a)} * (box.w - 8.0f);
    end_ = far;
    if (on_) {
        if (auto t = w.physics().raycast(o + Vec2{dmath::cos(a), dmath::sin(a)} * 9.0f, far)) end_ = o + (far - o) * *t;
        Player& p = w.player();
        if (hitTimer_ <= 0.0f && p.alive() && Collision::segmentIntersectsRect(o, end_, p.box.expanded(-1))) {
            DamageInfo d;
            d.amount = static_cast<float>(obj_.prop<int>("damage", 1));
            d.type = DamageType::Hazard;
            d.source = {p.center().x, p.center().y + 10};
            if (w.damagePlayer(d)) hitTimer_ = 0.5f;
        }
        if (static_cast<int>(cycle_ * 30) % 3 == 0) w.particles().emit("laser_sparks", end_, 0.5f);
    }
}

bool LaserEntity::handleAction(GameWorld&, const std::string& action, const Json&) {
    if (action == "enable" || action == "activate" || action == "close") enabled_ = true;
    else if (action == "disable" || action == "open") enabled_ = false;
    else if (action == "toggle") enabled_ = !enabled_;
    else return false;
    return true;
}

std::string LaserEntity::debugInfo() const { return enabled_ ? (on_ ? "actif" : "en attente") : "désactivé"; }

void LaserEntity::render(RenderContext& ctx) {
    Vec2 o = origin();
    float ang = obj_.rotation;
    // The emitter sprite rotates around its bottom-centre pivot (8 px below its centre):
    // place that pivot so the sprite centre stays on the beam origin for any rotation.
    float ar = degToRad(ang);
    emitter_.draw(ctx, o + Vec2{-8.0f * dmath::sin(ar), 8.0f * dmath::cos(ar)}, false, Color::white(), ang);
    if (ctx.editor) {
        float a = degToRad(ang);
        ctx.r.drawWorldLine(o, o + Vec2{dmath::cos(a), dmath::sin(a)} * (box.w - 8), {255, 77, 109, 120}, 1.0f);
        return;
    }
    if (warning_) {
        float flick = 0.5f + 0.5f * dmath::sin(ctx.time * 40.0f);
        ctx.r.drawWorldLine(o, end_, Color{255, 77, 109, static_cast<uint8_t>(60 + 80 * flick)}, 1.0f);
    }
    if (on_ && beam_) {
        Vec2 d = end_ - o;
        float len = d.length();
        int frame = static_cast<int>(ctx.time * 16.0f) % 4;
        for (float s = 0; s < len; s += 16.0f) {
            float segLen = std::min(16.0f, len - s);
            Vec2 c = o + d.normalized() * (s + segLen * 0.5f);
            Rect src{frame * 16.0f, 0, segLen, 8};
            ctx.r.drawWorldTexture(*beam_, &src, Rect::fromCenter(c, {segLen, 8}), Color::white(), BlendMode::Additive,
                                   false, ang);
        }
        ctx.lights.add((o + end_) * 0.5f, len * 0.5f + 20.0f, {255, 77, 109, 255});
        ctx.r.drawWorldGlow(end_, 10.0f, {255, 140, 160, 200});
    }
}

}  // namespace cosmic
