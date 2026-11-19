// Powers added in 2.19, one per sanctuary: Lame boomerang (Mycélia), Nova de givre
// (Aurorax), Leurre holographique (Pyrogyre), Balise de rappel (Nyxara). See Powers.h.

#include <algorithm>
#include <cmath>
#include <optional>

#include "engine/core/DetMath.h"
#include "engine/input/InputManager.h"
#include "engine/physics/Collision.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/HitParts.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

namespace {

constexpr const char* kFxSheet = "animations/power_fx.json";
constexpr float kBladeMaxLife = 4.0f;  // a blade never caught (Nova outran it) fades away

/// Draws frame `frame` of `clip` from the power effects sheet, centred on `c` (or standing
/// on `c` when `feet`). Returns false when the sheet is missing.
bool drawFx(RenderContext& ctx, const char* clip, int frame, Vec2 c, bool feet, Color tint, bool flip = false,
            BlendMode blend = BlendMode::Alpha) {
    auto set = ctx.res.animation(kFxSheet);
    if (!set || !set->texture) return false;
    const AnimationClip* cl = set->clip(clip);
    if (!cl || cl->frameCount() <= 0) return false;
    Rect src = set->frameRect(*cl, ((frame % cl->frameCount()) + cl->frameCount()) % cl->frameCount());
    Vec2 size{static_cast<float>(set->frameWidth), static_cast<float>(set->frameHeight)};
    Rect dst = feet ? Rect::fromBottomCenter(c, size) : Rect::fromCenter(c, size);
    ctx.r.drawWorldTexture(*set->texture, &src, dst, tint, blend, flip);
    return true;
}

}  // namespace

// ======================================================== Lame boomerang

bool BoomerangPower::canActivate(const Player& p, const GameWorld& /*w*/) const { return !flying_ && !p.dashing(); }

void BoomerangPower::onActivate(Player& p, GameWorld& w) {
    Vec2 dir = p.aimDirection();
    if (dir.y > 0.5f && p.grounded()) dir = {p.facing, 0.0f};
    if (dir.lengthSq() < 0.01f) dir = {p.facing, 0.0f};
    dir_ = dir.normalized();
    pos_ = prevPos_ = p.center() + Vec2{dir_.x * 10.0f, dir_.y * 10.0f - 2.0f};
    speed_ = speed();
    flying_ = true;
    returning_ = false;
    age_ = spin_ = prevSpin_ = trailTimer_ = 0.0f;
    hitOut_.clear();
    hitBack_.clear();
    carried_.clear();
    p.setCastPose(0.2f);
    w.particles().emit("wave_muzzle", pos_, 1.0f, dir_);
    w.rumble(0.1f, 0.25f, 60);
}

void BoomerangPower::turnBack(GameWorld& w) {
    returning_ = true;
    speed_ = 0.0f;
    w.particles().emit("boomerang_turn", pos_, 1.0f);
}

void BoomerangPower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    if (!flying_) return;
    age_ += dt;
    prevPos_ = pos_;
    prevSpin_ = spin_;
    spin_ += dt * 22.0f;
    // Constant deceleration: the blade stops exactly at its range, then speeds up on the way back.
    const float decel = speed() * speed() / (2.0f * std::max(1.0f, range()));
    if (!returning_) {
        speed_ = std::max(0.0f, speed_ - decel * dt);
        Vec2 step = dir_ * (speed_ * dt);
        bool inWall = w.physics().overlapsSolid(Rect::fromCenter(pos_, {4.0f, 4.0f}));
        auto hit = inWall ? std::optional<float>(0.0f) : w.physics().raycast(pos_, pos_ + step, false, ColliderTag::EnemyOnly);
        if (hit) {
            pos_ += step * std::max(0.0f, *hit - 0.05f);
            w.particles().emit("block_sparks", pos_, 0.6f);
            w.playSound("sounds/shield_block.wav", pos_, 0.35f, 1.5f);
            turnBack(w);
        } else {
            pos_ += step;
            if (speed_ <= speed() * 0.03f) turnBack(w);
        }
    } else {
        Vec2 to = p.center() - pos_;
        float d = to.length();
        speed_ = std::min(speed() * 1.1f, speed_ + decel * 1.3f * dt);
        float stepLen = speed_ * dt;
        if (d <= stepLen + 10.0f || !p.alive()) {
            // Caught: what it brought back lands in Nova's hands (picked up next step).
            flying_ = returning_ = false;
            ++caught_;
            for (const auto& id : carried_)
                if (auto* c = dynamic_cast<CollectibleEntity*>(w.entity(id)); c && !c->removed) c->carryTo(p.center());
            carried_.clear();
            w.playSound("sounds/boomerang_return.wav", p.center(), 0.8f);
            w.particles().emit("boomerang_catch", p.center(), 1.0f);
            return;
        }
        pos_ += to * (stepLen / std::max(0.001f, d));
    }
    if (age_ > kBladeMaxLife) {
        flying_ = returning_ = false;
        carried_.clear();
        w.particles().emit("wave_hit", pos_, 0.6f);
        return;
    }
    strike(w);
    trailTimer_ -= dt;
    if (trailTimer_ <= 0.0f) {
        trailTimer_ = 0.03f;
        w.particles().emit("boomerang_trail", pos_, 1.0f);
    }
}

void BoomerangPower::strike(GameWorld& w) {
    const float r = def_.param("radius", 9.0f);
    auto& hits = returning_ ? hitBack_ : hitOut_;
    Vec2 travel = returning_ ? (pos_ - prevPos_) : dir_;
    if (travel.lengthSq() < 1e-6f) travel = dir_;
    for (auto& e : w.enemies()) {
        if (!e->alive() || e->dormant()) continue;
        float mult = 1.0f;  // 2.76.0: the part it touches, else its box as before
        if (!reaches(*e, w, pos_, r, &mult)) continue;
        if (std::find(hits.begin(), hits.end(), e->uid()) != hits.end()) continue;
        DamageInfo d;
        d.amount = def_.param("damage", 1.0f) * mult;
        d.type = DamageType::Wave;
        d.source = pos_ - travel.normalized() * 8.0f;
        d.knockback = def_.param("knockback", 150.0f);
        d.fromPlayer = true;
        // Only a blow that lands counts: an enemy still flashing from the other leg is hit
        // a moment later, while the blade goes through it.
        if (e->takeDamage(w, d)) {
            hits.push_back(e->uid());
            w.particles().emit("wave_hit", pos_, 0.8f);
            w.playSound("sounds/wave_hit.wav", pos_, 0.6f, returning_ ? 1.15f : 1.0f);
            w.hitStop(0.025f);
        }
    }
    Rect area = Rect::fromCenter(pos_, {r * 2.0f, r * 2.0f});
    w.powerHit("boomerang", area);  // receptors switch on like with the Wave
    for (const auto& ent : w.entities()) {
        auto* c = dynamic_cast<CollectibleEntity*>(ent.get());
        if (!c || c->removed || !c->active || !c->carriable() || !area.expanded(2.0f).intersects(c->box)) continue;
        if (std::find(carried_.begin(), carried_.end(), c->id()) == carried_.end()) carried_.push_back(c->id());
    }
    for (const auto& id : carried_)
        if (auto* c = dynamic_cast<CollectibleEntity*>(w.entity(id)); c && !c->removed) c->carryTo(pos_);
}

void BoomerangPower::cancel(Player& /*p*/, GameWorld& /*w*/) {
    flying_ = returning_ = false;
    carried_.clear();
}

void BoomerangPower::render(const Player& /*p*/, RenderContext& ctx) const {
    if (!flying_) return;
    Vec2 c = lerpv(prevPos_, pos_, ctx.alpha);
    float spin = prevSpin_ + (spin_ - prevSpin_) * ctx.alpha;
    float fade = std::clamp((kBladeMaxLife - age_) / 0.3f, 0.0f, 1.0f);
    ctx.r.drawWorldGlow(c, 16.0f, def_.color.withAlphaF(0.45f * fade));
    if (!drawFx(ctx, "boomerang", static_cast<int>(spin), c, false, Color::white().withAlphaF(fade), dir_.x < 0.0f)) {
        // No sheet: a spinning bar of light.
        Vec2 a{dmath::cos(spin) * 7.0f, dmath::sin(spin) * 7.0f};
        ctx.r.drawWorldLine(c - a, c + a, Color{230, 255, 251, 255}, 2.5f);
    }
    ctx.lights.add(c, 50.0f, def_.color);
}

// ========================================================= Nova de givre

void FrostNovaPower::onActivate(Player& p, GameWorld& w) {
    center_ = p.center();
    ring_ = 0.0f;
    lastFrozen_ = lastShattered_ = 0;
    const float radius = def_.param("radius", 110.0f);
    for (auto& e : w.enemies()) {
        if (!e->alive() || e->dormant() || !Collision::circleIntersectsRect(center_, radius, e->box)) continue;
        // Enemy::freeze: an enemy is frozen solid, a boss only slowed down.
        e->freeze(e->def().isBoss ? def_.param("bossSlow", 2.0f) : def_.param("freeze", 1.5f));
        ++lastFrozen_;
        w.particles().emit("frost_burst", e->center(), 1.0f);
    }
    for (auto& pr : w.projectiles()) {
        if (!pr.alive || pr.fromPlayer || distance(pr.pos, center_) > radius + pr.radius) continue;
        pr.alive = false;  // frozen in flight, it shatters
        ++lastShattered_;
        w.particles().emit("ice_burst", pr.pos, 0.8f);
    }
    if (lastShattered_ > 0) w.playSound("sounds/freeze.wav", center_, 0.6f, 1.2f);
    w.particles().emit("frost_nova", center_, 1.0f);
    w.particles().emit("frost_nova_shards", center_, 1.0f);
    w.flash({159, 232, 255, 255}, 0.12f);
    w.shake(0.2f);
    w.rumble(0.3f, 0.5f, 140);
    p.setCastPose(0.22f);
}

void FrostNovaPower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    ring_ += dt;
}

void FrostNovaPower::render(const Player& /*p*/, RenderContext& ctx) const {
    constexpr float kRing = 0.35f;
    if (ring_ >= kRing) return;
    const float radius = def_.param("radius", 110.0f);
    float t = ring_ / kRing;
    float r = radius * (1.0f - (1.0f - t) * (1.0f - t));  // ease out to the full radius
    float a = 1.0f - t;
    Color ice{159, 232, 255, 255};
    ctx.r.fillWorldCircle(center_, r, ice.withAlphaF(0.12f * a), BlendMode::Additive);
    ctx.r.drawWorldCircle(center_, r, Color{223, 247, 255, 255}.withAlphaF(0.9f * a), 2.5f);
    ctx.r.drawWorldCircle(center_, r * 0.82f, ice.withAlphaF(0.5f * a), 1.5f);
    ctx.lights.add(center_, radius * 1.2f, ice);
}

// =================================================== Leurre holographique

bool DecoyPower::active() const { return decoyUp_; }

void DecoyPower::onActivate(Player& p, GameWorld& w) {
    // Standing Nova where she is (even crouched or sliding: the hologram stands).
    Vec2 c = p.feet() - Vec2{0.0f, p.config().size.y * 0.5f};
    w.setDecoy(c, def_.param("duration", 3.0f), p.facing);
    decoyUp_ = true;
    w.particles().emit("hologram_in", c, 1.0f);
    p.setCastPose(0.2f);
    w.rumble(0.1f, 0.2f, 60);
}

void DecoyPower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    decoyUp_ = decoyUp_ && w.decoyActive();
}

void DecoyPower::cancel(Player& /*p*/, GameWorld& w) {
    if (decoyUp_) w.clearDecoy();
    decoyUp_ = false;
}

// ====================================================== Balise de rappel

bool RecallPower::blocked(const Player& p, const GameWorld& w) const {
    // Something closed over the beacon since (a door...): Nova would end up in a wall.
    return w.physics().overlapsSolid(Rect::fromBottomCenter(feet_, p.box.size()), p.moveOptions());
}

bool RecallPower::canActivate(const Player& p, const GameWorld& w) const { return !beacon_ || !blocked(p, w); }

void RecallPower::handleInput(Player& p, GameWorld& w, const PlayerInput& in, float /*dt*/) {
    if (beacon_ && keyPressed(in) && ready(p) && blocked(p, w))
        w.hint("La balise est bloquée : impossible d'y revenir.", 1.6f);
}

void RecallPower::onActivate(Player& p, GameWorld& w) {
    if (!beacon_) {
        // First press: the beacon, free.
        beacon_ = true;
        feet_ = p.feet();
        timer_ = def_.param("window", 5.0f);
        w.playSound("sounds/recall_set.wav", feet_, 0.8f);
        w.particles().emit("recall_set", feet_ + Vec2{0.0f, -8.0f}, 1.0f);
        p.setCastPose(0.15f);
        return;
    }
    // Second press within the window: back to the beacon at once.
    warpFrom_ = p.center();
    if (p.dashing()) p.endDash();
    p.teleportTo(feet_);
    p.vel = {0.0f, 0.0f};
    p.setInvulnerable(def_.param("invulnerability", 0.2f));
    warpTo_ = p.center();
    warpFx_ = 0.0f;
    beacon_ = false;
    ++warps_;
    w.particles().emit("teleport_out", warpFrom_, 1.0f);
    w.particles().emit("recall_arrive", warpTo_, 1.0f);
    w.playSound("sounds/recall_warp.wav", warpTo_, 0.9f);
    w.flash({180, 140, 255, 255}, 0.08f);
    w.rumble(0.3f, 0.3f, 100);
}

void RecallPower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    anim_ += dt;
    warpFx_ += dt;
    if (!beacon_) return;
    timer_ -= dt;
    if (timer_ <= 0.0f) {
        // The window is over: the beacon fades, the next press drops a new one.
        beacon_ = false;
        w.particles().emit("recall_fade", feet_ + Vec2{0.0f, -8.0f}, 1.0f);
    }
}

void RecallPower::cancel(Player& /*p*/, GameWorld& /*w*/) { beacon_ = false; }

void RecallPower::render(const Player& p, RenderContext& ctx) const {
    const Color violet = def_.color;
    if (warpFx_ < 0.3f) {
        // The way back, fading.
        float a = 1.0f - warpFx_ / 0.3f;
        ctx.r.drawWorldLine(warpFrom_, warpTo_, violet.withAlphaF(0.35f * a), 6.0f, BlendMode::Additive);
        ctx.r.drawWorldLine(warpFrom_, warpTo_, Color{240, 230, 255, 255}.withAlphaF(0.8f * a), 1.5f);
    }
    if (!beacon_) return;
    float window = std::max(0.1f, def_.param("window", 5.0f));
    float blink = timer_ < 1.0f && static_cast<int>(timer_ * 10.0f) % 2 == 0 ? 0.45f : 1.0f;
    // A faint link from Nova to her beacon.
    Vec2 top = feet_ + Vec2{0.0f, -18.0f};
    ctx.r.drawWorldLine(p.renderFeet(ctx.alpha) + Vec2{0.0f, -11.0f}, top, violet.withAlphaF(0.18f * blink), 1.0f);
    ctx.r.drawWorldGlow(feet_ + Vec2{0.0f, -10.0f}, 16.0f, violet.withAlphaF(0.4f * blink));
    if (!drawFx(ctx, "beacon", static_cast<int>(anim_ * 10.0f), feet_, true, Color::white().withAlphaF(blink))) {
        ctx.r.drawWorldLine(feet_, top, violet, 2.0f);
        ctx.r.fillWorldCircle(top, 3.0f, Color{240, 230, 255, 255});
    }
    // What is left of the window: one pip per second above the beacon.
    int pips = static_cast<int>(std::ceil(window - 0.001f));
    int lit = static_cast<int>(std::ceil(timer_ - 0.001f));
    float x0 = feet_.x - (pips - 1) * 2.5f;
    for (int i = 0; i < pips; ++i) {
        Vec2 c{x0 + i * 5.0f, feet_.y - 27.0f};
        ctx.r.fillWorldCircle(c, 1.4f, i < lit ? Color{240, 230, 255, 255}.withAlphaF(blink) : violet.withAlphaF(0.3f));
    }
    ctx.lights.add(feet_ + Vec2{0.0f, -10.0f}, 46.0f, violet);
}

}  // namespace cosmic
