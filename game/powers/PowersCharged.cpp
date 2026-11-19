// Power added in 2.50: Boomerang chargé (see Powers.h).

#include <algorithm>
#include <cmath>
#include <optional>

#include "engine/core/DetMath.h"
#include "engine/input/InputManager.h"
#include "engine/physics/Collision.h"
#include "engine/rendering/Renderer.h"
#include "game/enemies/HitParts.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

namespace {

constexpr float kEps = 1e-3f;           // time comparisons (a step is 8,3 ms): float sums never miss a tick
constexpr float kFlightMaxLife = 4.0f;  // in flight (spin not counted): a blade Nova outran fades away
constexpr float kTickGrace = 0.15f;     // a blow met by a brief invulnerability is tried again this long
constexpr float kFlash = 0.16f;         // display: flash of a blow on the target
const Color kCore{230, 255, 251, 255};
const Color kGrey{118, 130, 150, 255};

/// Whether the blade can hurt this enemy at all (the brief invulnerability after any blow aside).
bool hurtable(const Enemy& e) {
    return e.alive() && !e.dormant() && !e.lastStand() && e.state() != EnemyState::TeleportOut &&
           e.state() != EnemyState::TeleportIn && e.def().multiplier(DamageType::Wave) > 0.0f;
}

}  // namespace

// ---------------------------------------------------------------- charge

bool BoomerangChargePower::keyHeld(const PlayerInput& in) const { return key_ != Action::Count && in.isHeld(key_); }

float BoomerangChargePower::chargeRatio() const { return std::clamp(hold_ / std::max(0.05f, chargeMax()), 0.0f, 1.0f); }

float BoomerangChargePower::rangeFor(float charge) const {
    const float r0 = def_.param("range", 260.0f);
    const float r1 = def_.param("rangeMax", r0);
    return r0 + (r1 - r0) * std::clamp(charge, 0.0f, 1.0f);
}

float BoomerangChargePower::spinFor(float charge) const {
    // The times of the table (tap, then each notch of the gauge), linear in between.
    std::vector<float> t;
    if (def_.params.contains("spinTimes") && def_.params.at("spinTimes").is_array())
        for (const auto& v : def_.params.at("spinTimes"))
            if (v.is_number()) t.push_back(std::max(0.0f, v.get<float>()));
    if (t.empty()) t = {0.4f, 2.0f};
    if (t.size() == 1) return t.front();
    const float x = std::clamp(charge, 0.0f, 1.0f) * static_cast<float>(t.size() - 1);
    const size_t i = std::min(static_cast<size_t>(x), t.size() - 2);
    return t[i] + (t[i + 1] - t[i]) * (x - static_cast<float>(i));
}

bool BoomerangChargePower::canActivate(const Player& p, const GameWorld& /*w*/) const {
    // Not from a ladder nor a slide (like every power but the dash).
    return phase_ == Phase::None && p.alive() && p.state() != PlayerState::Climb && p.state() != PlayerState::Slide;
}

void BoomerangChargePower::handleInput(Player& p, GameWorld& w, const PlayerInput& in, float dt) {
    if (p.movementPower() || !p.alive()) {  // grapple, ground pound: the charge is lost
        hold_ = 0.0f;
        charging_ = false;
        notch_ = 0;
        return;
    }
    if (keyHeld(in)) {
        // One blade at a time: the charge starts once it is back (and its recharge over).
        if (!charging_ && (phase_ != Phase::None || cooldown_ > 0.0f)) return;
        // The gauge counts from the step after the press: a tap is no charge at all.
        if (charging_) {
            hold_ = std::min(hold_ + dt, chargeMax());
            if (hold_ > chargeMax() - 0.25f * dt) hold_ = chargeMax();  // full is exactly full (float sums)
        }
        charging_ = true;
        const bool enough = p.energy + 0.001f >= activationCost(p);
        if (enough) w.sustainSound("boomerang_charge", "sounds/charge_loop.wav", notch_ >= 3 ? 0.25f : 0.4f);
        const int notch = std::clamp(static_cast<int>(chargeRatio() * 3.0f + 1e-4f), 0, 3);
        if (notch > notch_) {
            notch_ = notch;
            if (enough) {
                w.particles().emit(notch == 3 ? "charge_ready" : "charge_spark", p.center(), notch == 3 ? 1.0f : 0.7f);
                if (notch == 3) w.playSound("sounds/charge_ready.wav", p.center(), 0.7f, 1.15f);
            }
        }
        return;
    }
    if (!charging_) return;
    // Released: the throw, with the charge reached.
    const float charge = chargeRatio();
    hold_ = 0.0f;
    charging_ = false;
    notch_ = 0;
    if (!canActivate(p, w)) return;
    if (!ready(p)) {
        if (p.energy + 0.001f < activationCost(p)) {
            w.playSound("sounds/energy_low.wav", 0.5f);
            w.hint("Énergie insuffisante", 1.2f);
        }
        return;
    }
    lastCharge_ = charge;
    activate(p, w);  // the energy, the throw sound, then onActivate
    w.stats().powersUsed.insert(id());
    w.emit(id(), "powerUsed", {{"x", p.center().x}, {"y", p.center().y}});
}

// ----------------------------------------------------------------- blade

void BoomerangChargePower::onActivate(Player& p, GameWorld& w) {
    Vec2 dir = p.aimDirection();
    if (dir.y > 0.5f && p.grounded()) dir = {p.facing, 0.0f};
    if (dir.lengthSq() < 0.01f) dir = {p.facing, 0.0f};
    dir_ = dir.normalized();
    pos_ = prevPos_ = p.center() + Vec2{dir_.x * 10.0f, dir_.y * 10.0f - 2.0f};
    range_ = rangeFor(lastCharge_);
    spin_ = spinFor(lastCharge_);
    speed_ = speed();
    phase_ = Phase::Out;
    flight_ = spinTime_ = nextTick_ = 0.0f;
    pendingTick_ = false;
    target_ = 0;
    ticks_ = 0;
    hitBack_.clear();
    carried_.clear();
    angle_ = prevAngle_ = trailTimer_ = 0.0f;
    p.setCastPose(0.2f);
    w.particles().emit("wave_muzzle", pos_, 1.0f, dir_);
    w.rumble(0.1f + 0.2f * lastCharge_, 0.25f + 0.2f * lastCharge_, 70);
}

void BoomerangChargePower::turnBack(GameWorld& w) {
    phase_ = Phase::Back;
    speed_ = 0.0f;
    w.particles().emit("boomerang_turn", pos_, 1.0f);
}

void BoomerangChargePower::latch(GameWorld& w, Enemy& e) {
    if (!hurtable(e)) {
        // Nothing to cut into (immune, out of reach in its last stand): it bounces off.
        w.particles().emit("block_sparks", pos_, 0.6f);
        w.playSound("sounds/shield_block.wav", pos_, 0.35f, 1.5f);
        turnBack(w);
        return;
    }
    DamageInfo d;
    d.amount = def_.param("damage", 1.0f);
    d.type = DamageType::Wave;
    d.source = pos_ - dir_ * 8.0f;
    d.knockback = def_.param("knockback", 150.0f);
    d.fromPlayer = true;
    const bool landed = e.takeDamage(w, d);
    if (!landed && e.state() == EnemyState::Defend) {
        // Its guard turns the blade away, like a wall.
        turnBack(w);
        return;
    }
    // Latched (an impact met by a brief invulnerability still holds on): it spins on it.
    phase_ = Phase::Spin;
    target_ = e.uid();
    spinTime_ = 0.0f;
    nextTick_ = std::max(0.05f, def_.param("tickInterval", 0.3f));
    pendingTick_ = false;
    pos_ = e.center();
    flash_ = kFlash;
    flashPos_ = pos_;
    w.particles().emit("wave_hit", pos_, 1.0f);
    w.playSound("sounds/wave_hit.wav", pos_, 0.7f, 0.9f);
    w.hitStop(0.03f);
    w.rumble(0.2f, 0.35f, 60);
    if (!e.alive() || !hurtable(e)) endSpin(w);  // the impact was enough
}

void BoomerangChargePower::endSpin(GameWorld& w) {
    // Back to Nova; the target already had its share, the way back never hits it again.
    pendingTick_ = false;
    hitBack_.clear();
    if (target_ != 0) hitBack_.push_back(target_);
    turnBack(w);
}

void BoomerangChargePower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    anim_ += dt;
    flash_ = std::max(0.0f, flash_ - dt);
    if (phase_ == Phase::None) return;
    prevPos_ = pos_;
    prevAngle_ = angle_;
    angle_ += dt * (phase_ == Phase::Spin ? 30.0f : 22.0f);
    const float r = def_.param("radius", 10.0f);
    // Constant deceleration: the blade stops exactly at its range, then speeds up on the way back.
    const float decel = speed() * speed() / (2.0f * std::max(1.0f, range_));
    if (phase_ == Phase::Out) {
        flight_ += dt;
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
        if (phase_ == Phase::Out) {
            // The first enemy it touches (of two touched in one step, the nearer to where it came from).
            Enemy* first = nullptr;
            float best = 0.0f;
            for (auto& e : w.enemies()) {
                if (!e->alive() || e->dormant() || !Collision::circleIntersectsRect(pos_, r, e->box)) continue;
                float dist = (e->center() - prevPos_).lengthSq();
                if (!first || dist < best) {
                    first = e.get();
                    best = dist;
                }
            }
            if (first) latch(w, *first);
        }
    } else if (phase_ == Phase::Spin) {
        Enemy* e = w.findEnemy(target_);
        if (!e || !hurtable(*e)) {
            endSpin(w);  // dead, gone or out of reach: back at once
        } else {
            pos_ = e->center();
            spinTime_ += dt;
            const float every = std::max(0.05f, def_.param("tickInterval", 0.3f));
            if (!pendingTick_ && nextTick_ <= spin_ + kEps && spinTime_ + kEps >= nextTick_) {
                pendingTick_ = true;
                nextTick_ += every;
            }
            if (pendingTick_) {
                // Only the target: an enemy touching the spinning blade takes nothing.
                DamageInfo d;
                d.amount = def_.param("damage", 1.0f);
                d.type = DamageType::Wave;
                d.source = e->center() - dir_ * 8.0f;
                d.knockback = 0.0f;
                d.fromPlayer = true;
                if (e->takeDamage(w, d)) {
                    pendingTick_ = false;
                    nextTick_ = std::max(nextTick_, spinTime_ + every);  // a late blow pushes the next one
                    ++ticks_;
                    flash_ = kFlash;
                    flashPos_ = pos_;
                    w.particles().emit("wave_hit", pos_, 0.7f);
                    w.playSound("sounds/wave_hit.wav", pos_, 0.45f, 1.1f + 0.05f * static_cast<float>(ticks_ % 4));
                }
            }
            if (!hurtable(*e)) {
                endSpin(w);  // that blow ended it (or began its last stand)
            } else if (spinTime_ + kEps >= spin_ && (!pendingTick_ || spinTime_ >= spin_ + kTickGrace)) {
                endSpin(w);
            }
        }
    } else {  // Back
        flight_ += dt;
        Vec2 to = p.center() - pos_;
        float d = to.length();
        speed_ = std::min(speed() * 1.1f, speed_ + decel * 1.3f * dt);
        float stepLen = speed_ * dt;
        if (d <= stepLen + 10.0f || !p.alive()) {
            // Caught: what it brought back lands in Nova's hands (picked up next step).
            phase_ = Phase::None;
            ++caught_;
            cooldown_ = def_.cooldown;  // the recharge starts now
            for (const auto& id : carried_)
                if (auto* c = dynamic_cast<CollectibleEntity*>(w.entity(id)); c && !c->removed) c->carryTo(p.center());
            carried_.clear();
            w.playSound("sounds/boomerang_return.wav", p.center(), 0.8f);
            w.particles().emit("boomerang_catch", p.center(), 1.0f);
            return;
        }
        pos_ += to * (stepLen / std::max(0.001f, d));
        strikeBack(w);
    }
    if (phase_ != Phase::Spin && flight_ > kFlightMaxLife) {
        phase_ = Phase::None;
        cooldown_ = def_.cooldown;
        carried_.clear();
        w.particles().emit("wave_hit", pos_, 0.6f);
        return;
    }
    collect(w);
    trailTimer_ -= dt;
    if (trailTimer_ <= 0.0f && phase_ != Phase::Spin) {
        trailTimer_ = 0.03f;
        w.particles().emit("boomerang_trail", pos_, 1.0f);
    }
}

void BoomerangChargePower::strikeBack(GameWorld& w) {
    const float r = def_.param("radius", 10.0f);
    Vec2 travel = pos_ - prevPos_;
    if (travel.lengthSq() < 1e-6f) travel = -dir_;
    for (auto& e : w.enemies()) {
        float mult = 1.0f;  // 2.76.0: the part it touches, else its box as before
        if (!e->alive() || e->dormant() || !reaches(*e, w, pos_, r, &mult)) continue;
        if (std::find(hitBack_.begin(), hitBack_.end(), e->uid()) != hitBack_.end()) continue;
        DamageInfo d;
        d.amount = def_.param("damage", 1.0f) * mult;
        d.type = DamageType::Wave;
        d.source = pos_ - travel.normalized() * 8.0f;
        d.knockback = def_.param("knockback", 150.0f);
        d.fromPlayer = true;
        // Each enemy once: one still flashing from another blow is hit a moment later.
        if (e->takeDamage(w, d)) {
            hitBack_.push_back(e->uid());
            w.particles().emit("wave_hit", pos_, 0.8f);
            w.playSound("sounds/wave_hit.wav", pos_, 0.6f, 1.15f);
            w.hitStop(0.025f);
        }
    }
}

void BoomerangChargePower::collect(GameWorld& w) {
    const float r = def_.param("radius", 10.0f);
    Rect area = Rect::fromCenter(pos_, {r * 2.0f, r * 2.0f});
    w.powerHit("boomerang", area);  // receptors switch on as with the Lame boomerang
    for (const auto& ent : w.entities()) {
        auto* c = dynamic_cast<CollectibleEntity*>(ent.get());
        if (!c || c->removed || !c->active || !c->carriable() || !area.expanded(2.0f).intersects(c->box)) continue;
        if (std::find(carried_.begin(), carried_.end(), c->id()) == carried_.end()) carried_.push_back(c->id());
    }
    for (const auto& id : carried_)
        if (auto* c = dynamic_cast<CollectibleEntity*>(w.entity(id)); c && !c->removed) c->carryTo(pos_);
}

void BoomerangChargePower::cancel(Player& /*p*/, GameWorld& /*w*/) {
    phase_ = Phase::None;
    charging_ = false;
    hold_ = 0.0f;
    notch_ = 0;
    pendingTick_ = false;
    flash_ = 0.0f;
    hitBack_.clear();
    carried_.clear();
}

// --------------------------------------------------------------- display

void BoomerangChargePower::render(const Player& p, RenderContext& ctx) const {
    Renderer& r = ctx.r;
    const WorldView& v = r.view();
    if (charging_) {
        // The gauge: a ring around Nova filling clockwise from the top, notches at 1/3, 2/3 and
        // full; grey while the energy is short (nothing would be thrown).
        Vec2 c = p.renderFeet(ctx.alpha) + Vec2{0.0f, -p.box.h * 0.5f};
        const float k = chargeRatio();
        const bool enough = p.energy + 0.001f >= activationCost(p);
        const Color fill = enough ? def_.color : kGrey;
        const Vec2 sc = v.toScreen(c);
        const float R = 17.0f * v.scale, th = std::max(1.5f, 2.4f * v.scale);
        r.drawRing(sc, R, th, Color{8, 12, 26, 150});
        if (k > 0.0f) r.drawArc(sc, R, th, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * k, fill);
        for (int i = 1; i <= 3; ++i) {
            const float a = -kPi * 0.5f + 2.0f * kPi * static_cast<float>(i) / 3.0f;
            const Vec2 u{std::cos(a), std::sin(a)};
            const bool reached = notch_ >= i;
            const Color nc = reached ? (enough ? kCore : Color{200, 206, 216, 255}) : Color{70, 82, 104, 230};
            r.drawLine(sc + u * (R - th * 1.6f), sc + u * (R + th * 1.6f), nc, std::max(1.0f, 1.3f * v.scale));
        }
        if (enough) {
            float pulse = notch_ >= 3 ? 1.0f + 0.15f * std::sin(anim_ * 22.0f) : 1.0f;
            r.drawWorldGlow(c, (14.0f + 10.0f * k) * pulse, def_.color.withAlphaF(0.12f + 0.25f * k));
            ctx.lights.add(c, 26.0f + 30.0f * k, def_.color);
        }
    }
    if (flash_ > 0.0f) {
        // A blow on the target: a bright burst.
        const float t = flash_ / kFlash;
        r.drawWorldGlow(flashPos_, 14.0f + 10.0f * (1.0f - t), kCore.withAlphaF(0.7f * t));
        r.drawWorldCircle(flashPos_, 5.0f + 9.0f * (1.0f - t), kCore.withAlphaF(t), std::max(1.0f, 1.5f * v.scale));
    }
    if (phase_ == Phase::None) return;
    // The tri-blade: three swept blades of light around a bright core.
    const Vec2 c = lerpv(prevPos_, pos_, ctx.alpha);
    const float spin = prevAngle_ + (angle_ - prevAngle_) * ctx.alpha;
    const float fade = phase_ == Phase::Spin ? 1.0f : std::clamp((kFlightMaxLife - flight_) / 0.3f, 0.0f, 1.0f);
    r.drawWorldGlow(c, phase_ == Phase::Spin ? 22.0f : 17.0f, def_.color.withAlphaF(0.45f * fade));
    if (phase_ == Phase::Spin) r.drawWorldCircle(c, 11.0f, def_.color.withAlphaF(0.35f), std::max(1.0f, 1.2f * v.scale));
    for (int i = 0; i < 3; ++i) {
        const float a = spin + 2.0f * kPi * static_cast<float>(i) / 3.0f;
        const Vec2 u{std::cos(a), std::sin(a)}, n{-u.y, u.x};
        const Vec2 tip = c + u * 9.5f + n * 2.6f, front = c + n * 2.4f, back = c - n * 1.4f + u * 1.2f;
        r.fillTriangle(v.toScreen(front), v.toScreen(tip), v.toScreen(back), def_.color.withAlphaF(fade));
        r.drawLine(v.toScreen(c + u * 2.0f + n * 1.2f), v.toScreen(tip), kCore.withAlphaF(fade), std::max(1.0f, 0.9f * v.scale));
    }
    r.fillWorldCircle(c, 2.6f, kCore.withAlphaF(fade));
    ctx.lights.add(c, 50.0f, def_.color);
}

}  // namespace cosmic
