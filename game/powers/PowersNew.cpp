// Powers added in 2.5: time slow, magnet, air dash, plasma bomb.

#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

// ============================================================ Time slow

void TimeSlowPower::onActivate(Player& p, GameWorld& w) {
    w.slowTime(def_.param("duration", 3.5f), def_.param("factor", 0.35f));
    w.playSound("sounds/teleport_in.wav", p.center(), 0.9f, 0.6f);
    w.flash(def_.color, 0.2f);
    w.particles().emit("teleport_out", p.center(), 1.4f);
    p.setCastPose(0.2f);
    w.rumble(0.2f, 0.2f, 120);
}

// =========================================================== Plasma bomb

void PlasmaBombPower::onActivate(Player& p, GameWorld& w) {
    Vec2 dir = p.aimDirection();
    if (dir.lengthSq() < 0.01f || (dir.y > 0.5f && p.grounded())) dir = {p.facing, 0.0f};
    // Thrown in an arc: forward and slightly up.
    Vec2 v = Vec2{dir.x, std::min(dir.y, 0.0f) - 0.55f}.normalized() * def_.param("speed", 300.0f);
    Projectile pr;
    pr.pos = pr.prevPos = p.center() + Vec2{p.facing * 8.0f, -6.0f};
    pr.vel = v + Vec2{p.vel.x * 0.4f, 0.0f};
    pr.gravity = def_.param("gravity", 820.0f);
    pr.radius = 6.0f;
    pr.damage = def_.param("damage", 3.0f);
    pr.type = DamageType::Explosion;
    pr.fromPlayer = true;
    pr.life = def_.param("fuse", 1.3f);
    pr.clip = "bomb";
    pr.spin = true;
    pr.light = def_.color;
    pr.lightRadius = 40.0f;
    pr.explosionRadius = def_.param("radius", 60.0f);
    pr.bounces = static_cast<int>(def_.param("bounces", 3));
    pr.knockback = 260.0f;
    w.spawnProjectile(pr);
    p.setCastPose(0.2f);
    w.playSound("sounds/wave_cast.wav", p.center(), 0.7f, 0.7f);
}

}  // namespace cosmic
