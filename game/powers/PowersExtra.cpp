// Context abilities added with the v2 content: glide, ground pound, charged shot,
// grapple and the triple-jump upgrade (see Powers.h).

#include <cmath>

#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/gameplay/Entity.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

namespace {
bool freeMovingState(PlayerState s) {
    return s == PlayerState::Idle || s == PlayerState::Run || s == PlayerState::Jump || s == PlayerState::Fall;
}
}  // namespace

// ================================================================== Glide

void GlidePower::handleInput(Player& p, GameWorld& w, const PlayerInput& in, float dt) {
    // Starts when falling with jump held; continues while rising in an updraft; a new jump
    // press (double jump) folds the wings until the next fall.
    bool airborne = !p.grounded() && !p.inWater && !p.onLadder && !p.movementPower() &&
                    (p.state() == PlayerState::Fall || p.state() == PlayerState::Jump);
    bool want = airborne && in.isHeld(Action::Jump) && !in.isPressed(Action::Jump) &&
                (gliding_ || p.vel.y > 15.0f);
    if (want && !gliding_) {
        w.playSound(def_.sound.empty() ? "sounds/glide_open.wav" : def_.sound, p.center(), 0.7f);
        w.particles().emit("glide_open", p.center() + Vec2{0, -6}, 1.0f);
    }
    gliding_ = want;
    p.glideFallSpeed = gliding_ ? def_.param("fallSpeed", 52.0f) : 0.0f;
    p.glideAirSpeed = gliding_ ? def_.param("airSpeed", 175.0f) : 0.0f;
    if (!gliding_) return;
    w.sustainSound("glide", "sounds/glide_loop.wav", 0.45f);
    if (p.updraft > 0.0f) w.sustainSound("updraft", "sounds/updraft_loop.wav", 0.4f);
    trailTimer_ -= dt;
    if (trailTimer_ <= 0.0f) {
        trailTimer_ = 0.08f;
        w.particles().emit("glide_trail", p.center() + Vec2{-p.facing * 6.0f, -8.0f}, 1.0f);
    }
}

void GlidePower::cancel(Player& p, GameWorld& /*w*/) {
    gliding_ = false;
    p.glideFallSpeed = 0.0f;
    p.glideAirSpeed = 0.0f;
}

// ============================================================ Ground pound

bool GroundPoundPower::wantsActivation(const Player& p, const GameWorld& /*w*/, const PlayerInput& in) const {
    return in.isPressed(Action::Attack) && in.move.y > 0.5f && !p.grounded() && freeMovingState(p.state());
}

bool GroundPoundPower::canActivate(const Player& p, const GameWorld& /*w*/) const {
    return phase_ == Phase::None && !p.grounded() && !p.inWater;
}

void GroundPoundPower::onActivate(Player& p, GameWorld& w) {
    phase_ = Phase::Hover;
    timer_ = def_.param("hover", 0.12f);
    startY_ = p.box.bottom();
    hit_.clear();
    p.vel = {0.0f, -30.0f};
    p.setInvulnerable(timer_ + 0.1f);
    w.particles().emit("pound_charge", p.center(), 1.0f);
}

void GroundPoundPower::movePlayer(Player& p, GameWorld& w, const PlayerInput& /*in*/, float dt) {
    timer_ -= dt;
    switch (phase_) {
        case Phase::Hover:
            p.vel = {0.0f, -25.0f};
            p.physicsMove(w, dt);
            if (timer_ <= 0.0f) {
                phase_ = Phase::Dive;
                timer_ = 3.0f;
                w.playSound("sounds/pound_dive.wav", p.center(), 0.8f);
            }
            break;
        case Phase::Dive: {
            float speed = def_.param("diveSpeed", 560.0f);
            p.vel = {0.0f, speed};
            p.setInvulnerable(0.08f);
            // Break cracked floors just below the feet before touching them.
            Rect below{p.box.x - 3.0f, p.box.bottom() - 2.0f, p.box.w + 6.0f, speed * dt + 6.0f};
            w.powerHit("ground_pound", below);
            // Enemies in the way take a hit (once each).
            DamageInfo d;
            d.amount = def_.param("diveDamage", 1.0f);
            d.type = DamageType::GroundPound;
            d.source = p.center() - Vec2{0, 12};
            d.knockback = 160.0f;
            d.fromPlayer = true;
            w.damageEnemies(p.box.expanded(2), d, this);
            w.particles().emit("pound_trail", p.center() + Vec2{0, -6}, 1.0f);
            p.physicsMove(w, dt);
            if (p.grounded()) impact(p, w);
            else if (p.inWater || timer_ <= 0.0f) phase_ = Phase::None;
            break;
        }
        case Phase::Land:
            p.vel = {0.0f, 30.0f};
            p.physicsMove(w, dt);
            if (timer_ <= 0.0f) phase_ = Phase::None;
            break;
        case Phase::None: break;
    }
}

void GroundPoundPower::impact(Player& p, GameWorld& w) {
    phase_ = Phase::Land;
    timer_ = def_.param("landLock", 0.22f);
    float radius = def_.param("radius", 56.0f);
    float fall = p.box.bottom() - startY_;
    Vec2 feet = p.feet();
    DamageInfo d;
    d.amount = def_.param("damage", 2.0f) + (fall > 160.0f ? 1.0f : 0.0f);
    d.type = DamageType::GroundPound;
    d.source = feet + Vec2{0, 6};
    d.knockback = 280.0f;
    d.fromPlayer = true;
    w.damageEnemies(Rect::fromCenter(feet - Vec2{0, 10}, {radius * 2.0f, 40.0f}), d, nullptr);
    w.powerHit("ground_pound", Rect::fromCenter(feet + Vec2{0, 6}, {radius * 1.2f, 20.0f}));
    w.particles().emit("pound_impact", feet, 1.0f);
    w.particles().emit("slam_dust", feet, 1.2f);
    w.playSound("sounds/pound_impact.wav", feet, 1.0f);
    w.shake(0.45f);
    w.hitStop(0.05f);
    w.rumble(0.8f, 0.6f, 220);
    p.forceAnimation("pound_land", timer_ + 0.1f);
}

void GroundPoundPower::cancel(Player& /*p*/, GameWorld& /*w*/) { phase_ = Phase::None; }

const char* GroundPoundPower::animationClip(const Player& /*p*/) const {
    if (phase_ == Phase::Hover || phase_ == Phase::Dive) return "pound_dive";
    if (phase_ == Phase::Land) return "pound_land";
    return nullptr;
}

// ============================================================ Charged shot

float ChargeShotPower::chargeRatio() const {
    float t = def_.param("chargeTime", 0.85f);
    return std::clamp((hold_ - 0.25f) / std::max(0.05f, t - 0.25f), 0.0f, 1.0f);
}

void ChargeShotPower::handleInput(Player& p, GameWorld& w, const PlayerInput& in, float dt) {
    anim_ += dt;
    bool allowed = !p.movementPower() && !p.dashing() && !p.inWater &&
                   (freeMovingState(p.state()) || p.state() == PlayerState::WallSlide);
    if (!allowed) {
        cancel(p, w);
        return;
    }
    if (in.isHeld(Action::Attack)) {
        hold_ += dt;
        charging_ = hold_ >= 0.25f;
        if (charging_) w.sustainSound("charge", "sounds/charge_loop.wav", full_ ? 0.3f : 0.5f);
        if (charging_ && !full_ && hold_ >= def_.param("chargeTime", 0.85f)) {
            full_ = true;
            w.playSound("sounds/charge_ready.wav", p.center(), 0.8f);
            w.particles().emit("charge_ready", p.center() + Vec2{p.facing * 9.0f, -2.0f}, 1.0f);
        }
        sparkTimer_ -= dt;
        if (charging_ && sparkTimer_ <= 0.0f) {
            sparkTimer_ = full_ ? 0.05f : 0.1f;
            w.particles().emit("charge_spark", p.center() + Vec2{p.facing * 9.0f, -2.0f}, full_ ? 1.0f : 0.6f);
        }
        return;
    }
    if (hold_ > 0.0f) {
        if (full_) {
            if (ready(p)) {
                activate(p, w);
                w.stats().powersUsed.insert(id());
                w.emit(id(), "powerUsed", {{"x", p.center().x}, {"y", p.center().y}});
            } else if (p.energy < def_.energyCost) {
                w.playSound("sounds/energy_low.wav", 0.5f);
                w.hint("Énergie insuffisante", 1.2f);
            }
        }
        hold_ = 0.0f;
        charging_ = full_ = false;
    }
}

void ChargeShotPower::onActivate(Player& p, GameWorld& w) {
    Vec2 dir = p.aimDirection();
    if (dir.y > 0.5f && p.grounded()) dir = {p.facing, 0.0f};
    if (dir.lengthSq() < 0.01f) dir = {p.facing, 0.0f};
    dir = dir.normalized();
    float speed = def_.param("speed", 380.0f);
    Projectile pr;
    pr.pos = pr.prevPos = p.center() + Vec2{dir.x * 12.0f, dir.y * 12.0f - 2.0f};
    pr.vel = dir * speed;
    pr.radius = def_.param("radius", 8.0f);
    pr.damage = def_.param("damage", 4.0f);
    pr.type = DamageType::ChargeShot;
    pr.fromPlayer = true;
    pr.life = def_.param("range", 420.0f) / std::max(1.0f, speed);
    pr.clip = "charge_shot";
    pr.pierce = static_cast<int>(def_.param("pierce", 3));
    pr.light = def_.color;
    pr.lightRadius = 64.0f;
    pr.hitEffect = "charge_hit";
    pr.hitSound = "sounds/wave_hit.wav";
    pr.knockback = 260.0f;
    w.spawnProjectile(pr);
    p.forceAnimation("shoot", 0.24f);
    if (!p.grounded()) p.vel.x -= dir.x * def_.param("recoil", 70.0f);
    w.particles().emit("charge_muzzle", pr.pos, 1.0f, dir);
    w.shake(0.18f);
    w.rumble(0.3f, 0.6f, 120);
}

void ChargeShotPower::cancel(Player& /*p*/, GameWorld& /*w*/) {
    hold_ = 0.0f;
    charging_ = full_ = false;
}

const char* ChargeShotPower::animationClip(const Player& p) const {
    if (charging_ && p.grounded() && std::abs(p.vel.x) < 30.0f) return "charge";
    return nullptr;
}

void ChargeShotPower::render(const Player& p, RenderContext& ctx) const {
    if (!charging_) return;
    float k = chargeRatio();
    Vec2 hand = p.renderFeet(ctx.alpha) + Vec2{p.facing * 9.0f, -13.0f};
    float pulse = 1.0f + 0.15f * std::sin(anim_ * (full_ ? 26.0f : 14.0f));
    Color c = def_.color;
    ctx.r.drawWorldGlow(hand, (10.0f + 16.0f * k) * pulse, c.withAlpha(static_cast<uint8_t>(90 + 120 * k)));
    ctx.r.fillWorldCircle(hand, (1.5f + 3.0f * k) * pulse, full_ ? Color{230, 255, 251, 255} : c);
    ctx.lights.add(hand, 30.0f + 40.0f * k, c);
}

// ================================================================= Grapple

const LevelEntity* GrapplePower::findTarget(const Player& p, const GameWorld& w, float range) {
    const LevelEntity* best = nullptr;
    float bestScore = 1e9f;
    Vec2 from = p.center() + Vec2{0, -4};
    for (const auto& e : w.entities()) {
        if (!e->active || e->type() != "grapple_point") continue;
        Vec2 a = e->box.center();
        Vec2 d = a - from;
        float dist = d.length();
        if (dist > range || dist < 18.0f) continue;
        if (d.y > 40.0f) continue;  // anchors below the player are not reachable
        if (w.physics().raycast(from, a, false, ColliderTag::EnemyOnly)) continue;
        float score = dist - (signf(d.x) == p.facing ? 40.0f : 0.0f) + (d.y > 0 ? 30.0f : 0.0f);
        if (score < bestScore) {
            bestScore = score;
            best = e.get();
        }
    }
    return best;
}

bool GrapplePower::wantsActivation(const Player& p, const GameWorld& w, const PlayerInput& in) const {
    if (!in.isPressed(def_.input)) return false;
    if (p.grounded() && w.nearestInteractable()) return false;  // levers and signs keep priority
    return findTarget(p, w, def_.param("range", 170.0f)) != nullptr;
}

bool GrapplePower::canActivate(const Player& p, const GameWorld& w) const {
    return phase_ == Phase::None && !p.dashing() && findTarget(p, w, def_.param("range", 170.0f));
}

void GrapplePower::onActivate(Player& p, GameWorld& w) {
    const LevelEntity* t = findTarget(p, w, def_.param("range", 170.0f));
    if (!t) return;
    anchor_ = t->box.center();
    hook_ = p.center() + Vec2{p.facing * 6.0f, -6.0f};
    phase_ = Phase::Throw;
    timer_ = 0.0f;
    stuck_ = 0.0f;
    if (std::abs(anchor_.x - p.center().x) > 2.0f) p.facing = signf(anchor_.x - p.center().x);
    p.vel *= 0.3f;
}

void GrapplePower::movePlayer(Player& p, GameWorld& w, const PlayerInput& in, float dt) {
    timer_ += dt;
    if (phase_ == Phase::Throw) {
        Vec2 d = anchor_ - hook_;
        float step = def_.param("hookSpeed", 1100.0f) * dt;
        if (d.length() <= step) {
            hook_ = anchor_;
            phase_ = Phase::Pull;
            timer_ = 0.0f;
            w.playSound("sounds/grapple_hit.wav", anchor_, 0.9f);
            w.particles().emit("grapple_hit", anchor_, 1.0f);
            w.rumble(0.2f, 0.4f, 80);
        } else {
            hook_ += d.normalized() * step;
        }
        p.vel = p.vel * 0.85f + Vec2{0.0f, 120.0f * dt};
        p.physicsMove(w, dt);
        return;
    }
    // Pull
    Vec2 to = anchor_ - p.center();
    float dist = to.length();
    float speed = def_.param("pullSpeed", 430.0f);
    if (dist < 14.0f) {
        release(p, w, true);
        return;
    }
    if (in.isPressed(Action::Jump)) {
        release(p, w, true);
        return;
    }
    Vec2 before = p.box.pos();
    p.vel = to.normalized() * speed;
    p.physicsMove(w, dt);
    float moved = (p.box.pos() - before).length();
    stuck_ = moved < speed * dt * 0.3f ? stuck_ + dt : 0.0f;
    if (stuck_ > 0.1f || timer_ > 1.5f) release(p, w, false);
    w.particles().emit("grapple_trail", p.center(), 0.5f);
}

void GrapplePower::release(Player& p, GameWorld& /*w*/, bool boost) {
    Vec2 dir = (anchor_ - p.center()).normalized();
    phase_ = Phase::None;
    Vec2 v = boost ? Vec2{dir.x * def_.param("pullSpeed", 430.0f) * 0.5f, -def_.param("releaseBoost", 230.0f)}
                   : p.vel * 0.3f;
    p.launch(v, true);
}

void GrapplePower::cancel(Player& /*p*/, GameWorld& /*w*/) { phase_ = Phase::None; }

const char* GrapplePower::animationClip(const Player& /*p*/) const {
    if (phase_ == Phase::Throw) return "grapple";
    if (phase_ == Phase::Pull) return "grapple_pull";
    return nullptr;
}

void GrapplePower::render(const Player& p, RenderContext& ctx) const {
    if (phase_ == Phase::None) return;
    Vec2 hand = p.renderFeet(ctx.alpha) + Vec2{p.facing * 6.0f, -16.0f};
    Color c = def_.color;
    ctx.r.drawWorldLine(hand, hook_, c.withAlpha(90), 4.0f, BlendMode::Additive);
    ctx.r.drawWorldLine(hand, hook_, Color{230, 255, 251, 255}, 1.5f);
    ctx.r.drawWorldGlow(hook_, 14.0f, c.withAlpha(160));
    ctx.r.fillWorldCircle(hook_, 2.5f, Color{230, 255, 251, 255});
    ctx.lights.add(hook_, 40.0f, c);
}

}  // namespace cosmic
