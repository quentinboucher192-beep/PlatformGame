// Hazards: spikes, liquids (acid, water, lava), damage / slow zones and lasers.

#include <cmath>

#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

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
        float x = w.rng().range(x0, x1);
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
        float bob = std::sin(ctx.time * 2.0f + x * 0.05f) * amp;
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

void DamageZoneEntity::onPlayerStay(GameWorld& w, float dt) {
    if (!active) return;
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
    if (!active || !obj_.prop<bool>("visible", true)) return;
    fxTimer_ -= dt;
    if (fxTimer_ <= 0.0f) {
        fxTimer_ = std::max(0.05f, 2.0f / std::max(1.0f, box.w * box.h / 2000.0f));
        w.particles().emit("toxic_mist", {w.rng().range(box.x, box.right()), w.rng().range(box.y, box.bottom())}, 1.0f);
    }
}

bool DamageZoneEntity::handleAction(GameWorld&, const std::string& action, const Json&) {
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
    if (active && obj_.prop<bool>("visible", true)) {
        ctx.r.fillWorldRect(box, {157, 255, 58, 18}, BlendMode::Additive);
        ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.6f, {120, 200, 60, 255});
    }
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
        w.particles().emit(st.motes, {w.rng().range(box.x, box.right()), w.rng().range(box.y, box.bottom())}, 1.0f);
        if (w.rng().chance(0.85f))
            w.particles().emit(st.puffs, {w.rng().range(box.x + 8, box.right() - 8), w.rng().range(box.y + 8, box.bottom() - 4)},
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
        float hx = std::fmod(std::sin(static_cast<float>(i) * 12.9898f + box.x) * 43758.5453f, 1.0f);
        float hy = std::fmod(std::sin(static_cast<float>(i) * 78.233f + box.y) * 12345.678f, 1.0f);
        hx = hx < 0 ? hx + 1.0f : hx;
        hy = hy < 0 ? hy + 1.0f : hy;
        float drift = std::fmod(hx + ctx.time * (0.008f + 0.004f * static_cast<float>(i % 3)), 1.0f);
        Vec2 c{box.x + 10.0f + drift * std::max(0.0f, box.w - 20.0f),
               box.y + 10.0f + hy * std::max(0.0f, box.h - 20.0f) + std::sin(ctx.time * 0.7f + static_cast<float>(i)) * 3.0f};
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
    Vec2 dir{std::cos(a), std::sin(a)};
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
    Vec2 far = o + Vec2{std::cos(a), std::sin(a)} * (box.w - 8.0f);
    end_ = far;
    if (on_) {
        if (auto t = w.physics().raycast(o + Vec2{std::cos(a), std::sin(a)} * 9.0f, far)) end_ = o + (far - o) * *t;
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
    emitter_.draw(ctx, o + Vec2{-8.0f * std::sin(ar), 8.0f * std::cos(ar)}, false, Color::white(), ang);
    if (ctx.editor) {
        float a = degToRad(ang);
        ctx.r.drawWorldLine(o, o + Vec2{std::cos(a), std::sin(a)} * (box.w - 8), {255, 77, 109, 120}, 1.0f);
        return;
    }
    if (warning_) {
        float flick = 0.5f + 0.5f * std::sin(ctx.time * 40.0f);
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
