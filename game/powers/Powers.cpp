#include "game/powers/Powers.h"

#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"

namespace cosmic {

// =================================================================== Dash

bool DashPower::canActivate(const Player& p, const GameWorld& /*w*/) const {
    if (p.dashing()) return false;
    return p.grounded() || airUses_ > 0;
}

void DashPower::onActivate(Player& p, GameWorld& w) {
    Vec2 dir = p.aimDirection();
    if (p.grounded() && dir.y > 0.1f) dir = {p.facing, 0.0f};
    if (dir.lengthSq() < 0.01f) dir = {p.facing, 0.0f};
    if (!p.grounded()) --airUses_;
    float speed = def_.param("speed", 430.0f);
    float duration = def_.param("duration", 0.16f);
    p.beginDash(dir, speed, duration);
    if (dir.x != 0.0f) p.facing = signf(dir.x);
    p.setInvulnerable(duration + 0.05f);
    dashing_ = true;
    hitTimer_ = 0.0f;
    w.particles().emit("dash_burst", p.center(), 1.0f, -dir);
    w.shake(0.12f);
    w.rumble(0.2f, 0.4f, 80);
}

void DashPower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    if (refill_) {
        // Landed: air dashes back (one more per Air Dash upgrade carried).
        refill_ = false;
        int extra = 0;
        for (const auto& pw : p.powers())
            if (const auto* ad = dynamic_cast<const AirDashPower*>(pw.get())) extra += ad->extraDashes();
        airUses_ = static_cast<int>(def_.param("airUses", 1)) + extra;
    }
    if (!dashing_) return;
    if (!p.dashing()) {
        dashing_ = false;
        return;
    }
    hitTimer_ -= dt;
    if (hitTimer_ <= 0.0f) {
        DamageInfo d;
        d.amount = def_.param("damage", 1.0f);
        d.type = DamageType::Dash;
        d.source = p.center() - p.dashDirection() * 10.0f;
        d.knockback = 120.0f;
        d.fromPlayer = true;
        if (w.damageEnemies(p.box.expanded(3), d, this) > 0) {
            hitTimer_ = 0.12f;
            w.hitStop(0.03f);
        }
    }
    w.powerHit("dash", p.box.expanded(2));
    w.particles().emit("dash_trail", p.center(), 1.0f);
}

void DashPower::cancel(Player& p, GameWorld& /*w*/) {
    if (dashing_) p.endDash();
    dashing_ = false;
}

// =================================================================== Wave

void WavePower::onActivate(Player& p, GameWorld& w) {
    Vec2 dir = p.aimDirection();
    if (dir.y > 0.5f && p.grounded()) dir = {p.facing, 0.0f};
    if (std::abs(dir.x) < 0.1f && std::abs(dir.y) < 0.1f) dir = {p.facing, 0.0f};
    Projectile pr;
    pr.pos = pr.prevPos = p.center() + Vec2{dir.x * 10.0f, dir.y * 10.0f - 2.0f};
    pr.vel = dir.normalized() * def_.param("speed", 330.0f);
    pr.radius = def_.param("radius", 7.0f);
    pr.damage = def_.param("damage", 2.0f);
    pr.type = DamageType::Wave;
    pr.fromPlayer = true;
    pr.life = def_.param("range", 300.0f) / std::max(1.0f, def_.param("speed", 330.0f));
    pr.clip = "player_wave";
    pr.pierce = static_cast<int>(def_.param("pierce", 1));
    pr.light = def_.color;
    pr.lightRadius = 48.0f;
    pr.hitEffect = "wave_hit";
    pr.hitSound = "sounds/wave_hit.wav";
    pr.knockback = 200.0f;
    w.spawnProjectile(pr);
    p.setCastPose(0.22f);
    if (!p.grounded()) p.vel.x -= dir.x * def_.param("recoil", 40.0f);
    w.particles().emit("wave_muzzle", pr.pos, 1.0f, dir);
    w.rumble(0.1f, 0.3f, 60);
}

// ================================================================= Shield

void ShieldPower::onActivate(Player& p, GameWorld& w) {
    remaining_ = def_.duration > 0 ? def_.duration : 2.0f;
    p.setShield(true);
    p.setCastPose(0.18f);
    w.particles().emit("shield_up", p.center(), 1.0f);
}

void ShieldPower::fixedUpdate(Player& p, GameWorld& w, float dt) {
    Power::fixedUpdate(p, w, dt);
    anim_ += dt;
    if (remaining_ <= 0.0f) return;
    remaining_ -= dt;
    if (remaining_ <= 0.0f) {
        p.setShield(false);
        if (!def_.soundEnd.empty()) w.playSound(def_.soundEnd, p.center(), 0.7f);
        // Never leave the player stuck inside a plasma curtain.
        if (w.physics().overlapsSolid(p.box, p.moveOptions())) {
            MoveOptions o = p.moveOptions();
            for (float push = 2; push < 48; push += 2) {
                for (float dir : {-1.0f, 1.0f}) {
                    Rect r = p.box.translated({dir * push, 0});
                    if (!w.physics().overlapsSolid(r, o)) {
                        p.box = r;
                        return;
                    }
                }
            }
        }
    }
}

void ShieldPower::render(const Player& p, RenderContext& ctx) const {
    if (remaining_ <= 0.0f) return;
    auto tex = ctx.res.texture("textures/fx/shield.png");
    Vec2 c = lerpv(p.prevBox.center(), p.box.center(), ctx.alpha);
    float total = def_.duration > 0 ? def_.duration : 2.0f;
    float fadeOut = std::clamp(remaining_ / 0.4f, 0.0f, 1.0f);
    // Blink when about to expire.
    if (remaining_ < 0.5f && static_cast<int>(remaining_ * 16) % 2 == 0) fadeOut *= 0.4f;
    float appear = std::clamp((total - remaining_) / 0.12f, 0.0f, 1.0f);
    float size = 40.0f * (0.6f + 0.4f * appear);
    if (tex) {
        int frames = std::max(1, tex->width() / std::max(1, tex->height()));
        int f = static_cast<int>(anim_ * 12.0f) % frames;
        float fw = static_cast<float>(tex->width() / frames);
        Rect src{f * fw, 0, fw, static_cast<float>(tex->height())};
        ctx.r.drawWorldTexture(*tex, &src, Rect::fromCenter(c, {size, size}),
                               Color::white().withAlphaF(0.85f * fadeOut), BlendMode::Additive);
    }
    ctx.r.drawWorldGlow(c, size * 0.9f, def_.color.withAlphaF(0.25f * fadeOut));
    ctx.lights.add(c, 80.0f, def_.color);
}

void ShieldPower::cancel(Player& p, GameWorld& /*w*/) {
    remaining_ = 0.0f;
    p.setShield(false);
}

// ================================================================ Impulse

bool ImpulsePower::canActivate(const Player& p, const GameWorld& /*w*/) const { return p.grounded() || uses_ > 0; }

void ImpulsePower::onActivate(Player& p, GameWorld& w) {
    if (!p.grounded()) --uses_;
    float v = def_.param("velocity", 380.0f);
    p.launch({p.vel.x * 0.8f, -v}, false);
    float radius = def_.param("radius", 46.0f);
    Vec2 c = p.box.bottomCenter() + Vec2{0, -4};
    Rect area = Rect::fromCenter(c, {radius * 2, radius * 1.4f});
    DamageInfo d;
    d.amount = def_.param("damage", 1.0f);
    d.type = DamageType::Impulse;
    d.source = c;
    d.knockback = def_.param("knockback", 280.0f);
    d.fromPlayer = true;
    w.damageEnemies(area, d, this);
    w.powerHit("impulse", area);
    w.particles().emit("impulse_ring", c, 1.0f);
    w.shake(0.25f);
    w.rumble(0.5f, 0.3f, 140);
    p.setCastPose(0.15f);
}

// =============================================================== Teleport

bool TeleportPower::findDestination(const Player& p, const GameWorld& w, Vec2& feetOut) const {
    Vec2 dir = p.aimDirection();
    if (dir.lengthSq() < 0.01f) dir = {p.facing, 0.0f};
    dir = dir.normalized();
    float maxDist = def_.param("distance", 80.0f);
    MoveOptions solidOnly = p.moveOptions();
    // Teleport crosses phase barriers but stops before other solids.
    Vec2 from = p.center();
    auto hit = w.physics().raycast(from, from + dir * maxDist, false, ColliderTag::Phase | ColliderTag::ShieldPass);
    float limit = hit ? std::max(0.0f, *hit * maxDist - 6.0f) : maxDist;
    for (float d = limit; d >= 12.0f; d -= 3.0f) {
        Vec2 c = from + dir * d;
        Rect cand = Rect::fromCenter(c, p.box.size());
        if (!w.physics().overlapsSolid(cand, solidOnly)) {
            feetOut = cand.bottomCenter();
            return true;
        }
    }
    return false;
}

bool TeleportPower::canActivate(const Player& p, const GameWorld& w) const {
    Vec2 tmp;
    return !p.dashing() && findDestination(p, w, tmp);
}

void TeleportPower::onActivate(Player& p, GameWorld& w) {
    Vec2 dest;
    if (!findDestination(p, w, dest)) return;
    Vec2 origin = p.center();
    w.particles().emit("teleport_out", origin, 1.0f);
    p.teleportTo(dest);
    p.setPhased(0.08f);
    p.setInvulnerable(0.25f);
    w.particles().emit("teleport_in", p.center(), 1.0f);
    w.playSound("sounds/teleport_in.wav", p.center(), 0.8f);
    w.flash({176, 76, 255, 255}, 0.08f);
    w.powerHit("teleport", Rect::fromCenter((origin + p.center()) * 0.5f,
                                           {std::abs(origin.x - p.center().x) + 8, std::abs(origin.y - p.center().y) + 8}));
}

}  // namespace cosmic
