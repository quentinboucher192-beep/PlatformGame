// The attacks of the dragon (2.50, Pyrrhax): « Piqué » (a line shown toward where Nova will be,
// then a dash through it and beyond, and back up to its hunting height), « Atterrissage » (it
// lands where she stands, breathes a cloud of fire that spreads along the ground and lingers,
// stays exposed for a while, then takes off), « Boule de feu à nuage » (fireballs leaving a
// burning cloud where they burst) and « Rafale d'ailes » (the wind of its wings pushes her
// away, ash and embers in it). Like every attack they are announced during the wind-up before
// they can hurt, and they are deterministic: positions come from the simulation, the only
// randomness is the world's gameplay generator. The clouds and the wind live in BossHazards.

#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {
// Defaults of the parameters left at 0 (an attack made in the enemy editor works at once).
float orDefault(float v, float d) { return v > 0.0f ? v : d; }

/// std::clamp, with the middle when the range is empty (a narrow arena).
float clampIn(float v, float lo, float hi) { return lo <= hi ? std::clamp(v, lo, hi) : (lo + hi) * 0.5f; }

constexpr float kDiveLock = 0.45f;    // the line holds still this long before the dash (a dodge)
constexpr float kClimbSpeed = 300.0f; // back up after a dive, or off the ground
constexpr float kPerchBeat = 0.3f;    // landed, before the breath
constexpr float kPerchSpread = 0.65f; // the breath's cloud reaches its length
constexpr float kHoverAbove = 190.0f; // its hunting height above the target

/// How fast it comes down onto its landing mark.
float descentSpeed(const AttackDef& a, float phaseSpeed) { return std::max(240.0f, orDefault(a.lungeSpeed, 620.0f) * phaseSpeed); }

void dashedLine(Renderer& r, Vec2 a, Vec2 b, Color c, float thicknessPx, float dash = 10.0f, float gap = 7.0f) {
    Vec2 d = b - a;
    float len = d.length();
    if (len < 0.5f) return;
    Vec2 u = d / len;
    for (float s = 0.0f; s < len; s += dash + gap) r.drawWorldLine(a + u * s, a + u * std::min(len, s + dash), c, thicknessPx, BlendMode::Additive);
}

/// An arrowhead at `at` pointing along `dir`.
void chevron(Renderer& r, Vec2 at, Vec2 dir, float size, Color c, float px) {
    Vec2 n{-dir.y, dir.x};
    Vec2 tip = at + dir * (size * 0.5f);
    Vec2 back = at - dir * (size * 0.5f);
    r.drawWorldLine(back + n * (size * 0.6f), tip, c, px, BlendMode::Additive);
    r.drawWorldLine(back - n * (size * 0.6f), tip, c, px, BlendMode::Additive);
}
}  // namespace

// ================================================================= choosing

float Enemy::dragonFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const {
    const BossHazards& hz = w.hazards();
    const float above = box.bottom() - (playerPos_.y + 12.0f);  // > 0: the target stands higher than its claws
    switch (a.type) {
        case AttackType::Dive:
            if (!def_.flying) return 0.0f;  // a dive needs wings
            return (above > 24.0f ? 0.4f : 1.2f) * (far ? 1.2f : 1.0f);
        case AttackType::Perch: return (air ? 0.5f : 1.1f) * (hz.cloudCount(uid_) >= 3 ? 0.3f : 1.0f);
        case AttackType::CloudShot: return (far ? 1.3f : 1.0f) * (air ? 0.8f : 1.0f);
        case AttackType::Gust: return hz.hasWind(uid_) ? 0.0f : (near ? 1.6f : (far ? 0.5f : 1.0f));
        default: return 1.0f;
    }
}

float Enemy::belowFit(const AttackDef& a) const {
    // Right under the dragon: what comes down on the target, rarely what passes over it.
    switch (a.type) {
        case AttackType::Dive: return 3.5f;
        case AttackType::Perch: return 2.5f;
        case AttackType::CloudShot: return 1.8f;
        case AttackType::Rain: return 1.6f;
        case AttackType::Eruption: return 1.4f;
        case AttackType::Gust:
        case AttackType::Orbit:
        case AttackType::Blink: return 1.2f;
        case AttackType::Spread:
        case AttackType::Ring:
        case AttackType::Spiral: return 0.7f;
        case AttackType::Laser:
        case AttackType::Prism:
        case AttackType::Vortex: return 0.4f;
        default: return 1.0f;
    }
}

float Enemy::diveSpeed() const { return std::max(300.0f, orDefault(attack_ ? attack_->lungeSpeed : 0.0f, 760.0f) * phaseSpeed()); }

Vec2 Enemy::hoverSpot(const GameWorld& w) const {
    // Back up to its hunting height above the target, under the roof of its arena.
    const Rect arena = arenaArea(w);
    float y = std::max(playerPos_.y - kHoverAbove, arena.y + box.h * 0.5f + 24.0f);
    float x = clampIn(box.center().x + diveDir_.x * 60.0f, arena.x + box.w, arena.right() - box.w);
    return {x, y};
}

// ============================================================ the wind-up

void Enemy::dragonBegin(GameWorld& w) {
    const AttackDef& a = *attack_;
    path_.clear();
    path2_.clear();
    stage_ = Stage::None;
    stageTimer_ = 0.0f;
    diveLocked_ = false;
    perched_ = false;
    dropThrough_ = false;
    switch (a.type) {
        case AttackType::Dive:
            divesLeft_ = std::max(1, a.count);
            // Long enough to read: the line holds still for kDiveLock before the dash.
            stateTimer_ = std::max(stateTimer_, kDiveLock + 0.15f);
            aimDive(w, stateTimer_);
            if (a.sound.empty()) w.playSound("sounds/boss_roar_fire.wav", box.center(), 0.5f, 1.25f);
            break;
        case AttackType::Perch:
            stateTimer_ = std::max(stateTimer_, 0.6f);
            chooseLanding(w);
            break;
        case AttackType::CloudShot: aimClouds(w); break;
        case AttackType::Gust:
            markPos_ = box.center();
            if (a.sound.empty()) w.playSound("sounds/inhale.wav", box.center(), 0.8f, 0.7f);
            break;
        default: break;
    }
}

void Enemy::aimDive(GameWorld& w, float eta) {
    const AttackDef& a = *attack_;
    const Vec2 from = box.center();
    // Where the target will be when the dragon gets there (through its body).
    Vec2 aim = playerPos_;
    if (a.lead > 0.0f) aim = predictPlayer(w, (std::max(0.0f, eta) + distance(from, playerPos_) / diveSpeed()) * a.lead);
    if (distance(aim, from) < 8.0f) aim = from + Vec2{facing * 4.0f, 40.0f};
    markPos_ = aim;
    traceDive(w);
}

void Enemy::traceDive(GameWorld& w) {
    // Through the point and beyond (the one-way ledges do not stop it). Where its body meets the
    // ground first, it levels off and skims along it for what is left of the way (a swoop: the
    // steeper the dive, the shorter the skim); a wall or the roof ends it.
    const Vec2 from = box.center();
    Vec2 d = markPos_ - from;
    float len = d.length();
    if (len > 1.0f) diveDir_ = d / len;
    const float reach = len + orDefault(attack_->radius, 220.0f);
    const float edge = diveDir_.y >= 0.0f ? box.h * 0.5f : -box.h * 0.5f;  // its belly (or its back)
    const Vec2 lead = from + Vec2{0.0f, edge};
    path_ = {from, from + diveDir_ * reach};
    if (auto t = w.physics().raycast(lead, lead + diveDir_ * reach)) {
        const float done = reach * *t;
        Vec2 contact = from + diveDir_ * done;
        path_ = {from, contact};
        const float side = signf(diveDir_.x);
        if (diveDir_.y > 0.2f && side != 0.0f) {
            float skim = (reach - done) * std::abs(diveDir_.x);
            Vec2 a = contact + Vec2{side * box.w * 0.5f, 0.0f};
            if (auto u = w.physics().raycast(a, a + Vec2{side * skim, 0.0f})) skim *= *u;
            if (skim > 4.0f) path_.push_back(contact + Vec2{side * skim, 0.0f});
        }
    }
    diveLength_ = 0.0f;
    for (size_t k = 1; k < path_.size(); ++k) diveLength_ += distance(path_[k - 1], path_[k]);
}

void Enemy::chooseLanding(GameWorld& w) {
    // Where the target will stand: on the ground under it (a ledge too, when the dragon fits on
    // it, else the floor below), never into a wall.
    const AttackDef& a = *attack_;
    const Rect arena = arenaArea(w);
    const float half = box.w * 0.5f;
    Vec2 aim = a.lead > 0.0f ? predictPlayer(w, (stateTimer_ + 0.4f) * a.lead) : playerPos_;
    float x = clampIn(aim.x, arena.x + half + 12.0f, arena.right() - half - 12.0f);
    auto span = [&](float gx, float gy, bool oneWay, float& sl, float& sr) {
        // The surface around gx at height gy: as far as the ground stays level on both sides.
        auto level = [&](float sx) {
            Vec2 from{sx, gy - 40.0f}, to{sx, gy + 40.0f};
            auto t = w.physics().raycast(from, to, oneWay);
            return t && std::abs(from.y + (to.y - from.y) * *t - gy) < 3.0f;
        };
        sl = sr = gx;
        while (sl - 8.0f > arena.x && gx - sl < 900.0f && level(sl - 8.0f)) sl -= 8.0f;
        while (sr + 8.0f < arena.right() && sr - gx < 900.0f && level(sr + 8.0f)) sr += 8.0f;
    };
    auto groundBelow = [&](float gx, bool oneWay) -> std::optional<float> {
        Vec2 from{gx, aim.y - 40.0f}, to{gx, std::max(aim.y, arena.bottom()) + 120.0f};
        if (auto t = w.physics().raycast(from, to, oneWay)) return from.y + (to.y - from.y) * *t;
        return std::nullopt;
    };
    float gy = groundBelow(x, true).value_or(arena.bottom());
    float sl = x, sr = x;
    span(x, gy, true, sl, sr);
    if (sr - sl < box.w + 8.0f) {  // a ledge too narrow for it: the floor below
        if (auto g = groundBelow(x, false)) gy = *g;
        span(x, gy, false, sl, sr);
    }
    x = clampIn(x, sl + half + 2.0f, sr - half - 2.0f);
    markPos_ = {x, gy};
    path_ = {{sl, gy}, {sr, gy}};  // the surface the breath runs along
}

void Enemy::aimClouds(GameWorld& w) {
    // The fireballs are lobbed onto where the target will be: their impacts are shown.
    const AttackDef& a = *attack_;
    const Vec2 origin = attackOrigin();
    float flight = std::clamp(std::abs(playerPos_.x - origin.x) / std::max(60.0f, a.projectileSpeed), 0.6f, 1.5f);
    Vec2 aim = a.lead > 0.0f ? predictPlayer(w, (std::max(0.0f, stateTimer_) + flight) * a.lead) : playerPos_;
    aimDir_ = (aim - origin).normalized();
    if (aimDir_.lengthSq() < 0.01f) aimDir_ = {facing, 0.0f};
    face(aimDir_.x);
    path_.clear();
    if (a.gravity <= 0.0f) return;  // straight shots: aimed, nothing to show on the ground
    const Rect arena = arenaArea(w);
    const int n = std::max(1, a.count);
    const float gap = std::max(a.spread, 40.0f);
    for (int i = 0; i < n; ++i) {
        float x = clampIn(aim.x + (static_cast<float>(i) - (n - 1) * 0.5f) * gap, arena.x + 16.0f, arena.right() - 16.0f);
        path_.push_back({x, groundAt(w, x, aim.y).value_or(aim.y + 11.0f)});
    }
}

void Enemy::dragonTelegraph(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    switch (a.type) {
        case AttackType::Dive:
            // The line follows the target, then holds still: the dash goes where it shows.
            if (!diveLocked_ && stateTimer_ > kDiveLock) {
                aimDive(w, stateTimer_);
            } else {
                if (!diveLocked_) w.playSound("sounds/laser_charge.wav", box.center(), 0.6f, 1.4f);
                diveLocked_ = true;
                traceDive(w);
            }
            // It draws back along its line, wings raised.
            if (def_.flying) vel = lerpv(vel, diveDir_ * -40.0f, damp(6.0f, dt));
            face(diveDir_.x);
            break;
        case AttackType::Perch: face(markPos_.x - box.center().x); break;
        case AttackType::CloudShot:
            if (stateTimer_ > 0.2f) aimClouds(w);
            break;
        default: break;
    }
}

// =========================================================== the attack

void Enemy::startDash(GameWorld& w) {
    traceDive(w);
    stage_ = Stage::Dash;
    stageTimer_ = 0.0f;
    diveTravel_ = 0.0f;
    lastCenter_ = box.center();
    attackHit_ = false;
    dropThrough_ = true;
    face(diveDir_.x);
    vel = diveDir_ * diveSpeed();
    playClip(attack_->attackAnim, true);
    w.playSound("sounds/pound_dive.wav", box.center(), 1.0f, 0.8f);
    w.shake(0.15f);
}

void Enemy::land(GameWorld& w) {
    // Touchdown on its mark (where there is room for it): the ground shakes, under it hurts.
    const AttackDef& a = *attack_;
    MoveOptions opt;
    opt.isEnemy = true;
    opt.dropThrough = true;
    Rect cand = Rect::fromBottomCenter(markPos_, box.size());
    if (!w.physics().overlapsSolid(cand, opt)) box = cand;
    vel = {};
    dropThrough_ = false;
    perched_ = true;
    stage_ = Stage::Landed;
    stageTimer_ = kPerchBeat;
    face(playerPos_.x - box.center().x);
    playClip("roar", true);
    w.shake(0.6f);
    w.rumble(0.6f, 0.5f, 220);
    w.playSound("sounds/pound_impact.wav", box.bottomCenter(), 1.0f, 0.7f);
    w.particles().emit("slam_dust", box.bottomCenter(), 2.2f);
    Player& p = w.player();
    Rect stomp = Rect::fromCenter(box.bottomCenter() + Vec2{0.0f, -box.h * 0.5f}, {box.w + 40.0f, box.h + 8.0f});
    if (stomp.intersects(p.box)) {
        DamageInfo d;
        d.amount = std::max(a.damage, def_.contactDamage);
        d.type = DamageType::Contact;
        d.source = box.bottomCenter();
        d.knockback = std::max(a.knockback, 260.0f);
        w.damagePlayer(d);
    }
}

void Enemy::breathe(GameWorld& w) {
    // A cloud of fire breathed along the ground toward the target (both sides with two arms);
    // it spreads from its mouth and stays when it takes off. The surface's ends stop it.
    const AttackDef& a = *attack_;
    face(playerPos_.x - box.center().x);
    playClip("special", true);
    if (a.sound.empty()) w.playSound("sounds/boss_roar_fire.wav", box.center(), 1.0f);
    w.shake(0.3f);
    const float reach = orDefault(a.radius, 300.0f);
    const float h = orDefault(a.height, 44.0f);
    const float gy = box.bottom();
    const float sl = path_.size() == 2 ? path_[0].x : box.center().x - reach - box.w;
    const float sr = path_.size() == 2 ? path_[1].x : box.center().x + reach + box.w;
    const float mouth = box.center().x + facing * box.w * 0.35f;
    float left, right, origin;
    if (a.arms >= 2) {
        origin = box.center().x;
        left = std::max(sl, origin - box.w * 0.5f - reach);
        right = std::min(sr, origin + box.w * 0.5f + reach);
    } else {
        origin = mouth;
        left = facing > 0.0f ? box.center().x : std::max(sl, mouth - reach);
        right = facing > 0.0f ? std::min(sr, mouth + reach) : box.center().x;
    }
    BossHazards::Cloud c;
    c.owner = uid_;
    c.area = {left, gy - h, std::max(8.0f, right - left), h};
    c.originX = origin;
    c.spread = kPerchSpread;
    c.time = orDefault(a.duration, 3.5f);
    c.damage = a.damage;
    c.ground = true;
    c.color = lightColor();
    c.ink = a.look == "ink";  // 2.52, the Kraken (display only)
    w.hazards().addCloud(c);
}

void Enemy::fireClouds(GameWorld& w) {
    const AttackDef& a = *attack_;
    const Vec2 origin = attackOrigin();
    auto shot = [&](Vec2 vel0, float gravity) {
        Projectile p;
        p.pos = p.prevPos = origin;
        p.vel = vel0;
        p.gravity = gravity;
        p.damage = a.damage;
        p.type = DamageType::Projectile;
        p.clip = a.projectile;
        p.radius = a.projectileRadius > 0 ? a.projectileRadius : 7.0f;
        p.life = 4.0f;
        p.light = lightColor();
        p.lightRadius = 40.0f;
        p.knockback = a.knockback;
        p.hitSound = "sounds/explosion.wav";
        p.hitEffect = a.hitEffect.empty() ? "fire_burst" : a.hitEffect;
        p.cloudRadius = orDefault(a.radius, 56.0f);
        p.cloudTime = orDefault(a.duration, 3.0f);
        p.cloudDamage = a.damage;
        p.cloudOwner = uid_;
        p.cloudColor = lightColor();
        p.cloudInk = a.look == "ink";
        w.spawnProjectile(p);
    };
    if (a.gravity > 0.0f && !path_.empty()) {
        // Lobbed onto the marks shown during the wind-up.
        for (const Vec2& t : path_) {
            float dx = t.x - origin.x;
            float T = std::clamp(std::abs(dx) / std::max(60.0f, a.projectileSpeed), 0.6f, 1.5f);
            float dy = (t.y - 6.0f) - origin.y;
            shot({dx / T, (dy - 0.5f * a.gravity * T * T) / T}, a.gravity);
        }
    } else {
        const float base = dmath::atan2(aimDir_.y, aimDir_.x);
        const int n = std::max(1, a.count);
        for (int i = 0; i < n; ++i) {
            float off = n > 1 ? (static_cast<float>(i) - (n - 1) * 0.5f) * degToRad(a.spread) : 0.0f;
            shot(Vec2::fromAngle(base + off) * a.projectileSpeed, a.gravity);
        }
    }
    w.particles().emit("muzzle_enemy", origin, 1.2f, aimDir_);
    if (a.sound.empty()) w.playSound("sounds/fireball.wav", origin, 0.9f);
}

void Enemy::blowEmbers(GameWorld& w) {
    // Embers thrown from the tips of its wings toward the target, carried by the gust.
    const AttackDef& a = *attack_;
    const Vec2 c = box.center();
    float side = signf(playerPos_.x - c.x);
    if (side == 0.0f) side = facing;
    const Vec2 from = c + Vec2{side * box.w * 0.5f, -box.h * 0.2f};
    const Vec2 to = playerPos_ - from;
    const float base = dmath::atan2(to.y, to.x);
    for (int i = 0; i < std::max(1, a.count); ++i) {
        Projectile p;
        p.pos = p.prevPos = from;
        p.vel = Vec2::fromAngle(base + degToRad(w.rng().range(-a.spread, a.spread))) * std::max(60.0f, a.projectileSpeed);
        p.damage = a.damage;
        p.type = DamageType::Projectile;
        p.clip = a.projectile;
        p.radius = a.projectileRadius > 0 ? a.projectileRadius : 4.0f;
        p.life = 3.5f;
        p.light = lightColor();
        p.knockback = a.knockback;
        p.hitEffect = a.hitEffect.empty() ? "projectile_burst" : a.hitEffect;
        w.spawnProjectile(p);
    }
}

void Enemy::dragonStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackClock_ = 0.0f;
    burstTimer_ = 0.0f;
    hopTimer_ = 0.0f;
    switch (a.type) {
        case AttackType::Dive:
            startDash(w);
            stateTimer_ = 6.0f + 3.0f * static_cast<float>(divesLeft_);  // it ends itself (safety)
            break;
        case AttackType::Perch:
            stage_ = Stage::Descend;
            // Time to reach its mark (it flies through the one-way ledges), with a margin.
            stageTimer_ = distance(box.bottomCenter(), markPos_) / descentSpeed(a, phaseSpeed()) + 0.6f;
            dropThrough_ = true;
            stateTimer_ = 12.0f;  // it ends itself (safety)
            playClip("move", true);
            w.playSound("sounds/pound_dive.wav", box.center(), 0.9f, 0.6f);
            break;
        case AttackType::CloudShot:
            fireClouds(w);
            if (a.bursts > 1) {
                burstsLeft_ = a.bursts - 1;
                burstTimer_ = a.burstInterval;
                stateTimer_ = std::max(stateTimer_, static_cast<float>(a.bursts - 1) * a.burstInterval + 0.05f);
            }
            break;
        case AttackType::Gust: {
            BossHazards::Wind wd;
            wd.owner = uid_;
            wd.center = box.center();
            wd.speed = orDefault(a.pull, 100.0f);
            wd.range = std::max(200.0f, a.range);
            wd.time = orDefault(a.duration, 2.2f);
            w.hazards().addWind(wd);
            stateTimer_ = wd.time;
            burstTimer_ = 0.3f;
            w.shake(0.35f);
            if (a.sound.empty()) w.playSound("sounds/dash.wav", box.center(), 1.0f, 0.5f);
            break;
        }
        default: break;
    }
}

void Enemy::dragonActive(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    Player& p = w.player();
    const Vec2 c = box.center();
    switch (a.type) {
        case AttackType::Dive:
            switch (stage_) {
                case Stage::Dash: {
                    const float sp = diveSpeed();
                    const float moved = distance(c, lastCenter_);
                    diveTravel_ += moved;
                    lastCenter_ = c;
                    stageTimer_ += dt;
                    // Down its line, then skimming along the ground (the swoop's second leg).
                    Vec2 way = diveDir_;
                    if (path_.size() == 3 && diveTravel_ >= distance(path_[0], path_[1]) - 2.0f) way = (path_[2] - path_[1]).normalized();
                    vel = way * sp;
                    if (!attackHit_ && box.expanded(4.0f).intersects(p.box)) {
                        DamageInfo d;
                        d.amount = a.damage;
                        d.type = DamageType::Contact;
                        d.source = c;
                        d.knockback = a.knockback;
                        attackHit_ = w.damagePlayer(d) || true;
                    }
                    hopTimer_ -= dt;
                    if (hopTimer_ <= 0.0f) {
                        hopTimer_ = 0.03f;
                        Color fire = lightColor();
                        w.particles().emit("dive_trail", c - diveDir_ * (box.w * 0.3f), 1.0f, {}, &fire);
                    }
                    const bool blocked = stageTimer_ > 0.05f && moved < sp * dt * 0.3f;
                    if (diveTravel_ >= diveLength_ - 4.0f || blocked || stageTimer_ > 2.5f) {
                        if (blocked) {  // into the ground (or a wall): it hits hard
                            w.shake(0.45f);
                            w.particles().emit("slam_dust", box.bottomCenter(), 1.6f);
                            w.playSound("sounds/golem_slam.wav", c, 0.9f);
                        }
                        // Out of the dive, back up to its height (the next dive of a chain first).
                        --divesLeft_;
                        stage_ = divesLeft_ > 0 ? Stage::Climb : Stage::Return;
                        stageTimer_ = 0.0f;
                        climbSpot_ = hoverSpot(w);
                        playClip("move", true);
                    }
                    break;
                }
                case Stage::Climb:
                case Stage::Return: {
                    stageTimer_ += dt;
                    Vec2 to = climbSpot_ - c;
                    float dist = to.length();
                    Vec2 want = dist > 2.0f ? to / dist * (kClimbSpeed * phaseSpeed()) : Vec2{};
                    vel = lerpv(vel, want, damp(7.0f, dt));
                    face(playerPos_.x - c.x);
                    if (dist < 30.0f || stageTimer_ > 1.0f) {
                        if (stage_ == Stage::Return) {
                            stateTimer_ = 0.0f;
                            break;
                        }
                        // The next dive of the chain: a new line, held still before it goes.
                        stage_ = Stage::Aim;
                        stageTimer_ = std::max(kDiveLock, orDefault(a.burstInterval, 0.55f) * telegraphScale());
                        aimDive(w, stageTimer_);
                        diveLocked_ = true;
                        playClip(a.telegraphAnim, true);
                        if (!a.telegraphSound.empty()) w.playSound(a.telegraphSound, c, 0.7f);
                    }
                    break;
                }
                case Stage::Aim:
                    stageTimer_ -= dt;
                    vel = lerpv(vel, diveDir_ * -40.0f, damp(6.0f, dt));
                    traceDive(w);
                    face(diveDir_.x);
                    if (stageTimer_ <= 0.0f) startDash(w);
                    break;
                default: break;
            }
            break;
        case AttackType::Perch:
            switch (stage_) {
                case Stage::Descend: {
                    stageTimer_ -= dt;
                    Vec2 to = markPos_ - box.bottomCenter();
                    float dist = to.length();
                    float sp = descentSpeed(a, phaseSpeed());
                    if (dist <= sp * dt + 1.0f) {
                        land(w);
                    } else if (stageTimer_ <= 0.0f) {
                        // Held back on its way (never seen in its arena): no landing, it flies up.
                        vel = {};
                        dropThrough_ = false;
                        stage_ = Stage::TakeOff;
                        stageTimer_ = 0.0f;
                        climbSpot_ = hoverSpot(w);
                    } else {
                        vel = to / dist * sp;
                    }
                    break;
                }
                case Stage::Landed:
                    vel = {};
                    stageTimer_ -= dt;
                    if (stageTimer_ <= 0.0f) {
                        breathe(w);
                        stage_ = Stage::Hold;
                        stageTimer_ = kPerchSpread + orDefault(a.active, 1.2f);
                    }
                    break;
                case Stage::Hold: {
                    vel = {};
                    float before = stageTimer_;
                    stageTimer_ -= dt;
                    // The breath pours out of its mouth while the cloud spreads.
                    if (before > orDefault(a.active, 1.2f)) {
                        hopTimer_ -= dt;
                        if (hopTimer_ <= 0.0f) {
                            hopTimer_ = 0.04f;
                            Color fire = lightColor();
                            Vec2 mouth{c.x + facing * box.w * 0.45f, box.bottom() - box.h * 0.3f};
                            w.particles().emit("eruption_fire", mouth, 0.9f, {facing, 0.25f}, &fire);
                        }
                    } else if (anim_.current() == "special") {
                        playClip("idle", true);
                    }
                    if (stageTimer_ <= 0.0f) {
                        // It takes off: no longer exposed.
                        perched_ = false;
                        stage_ = Stage::TakeOff;
                        stageTimer_ = 0.0f;
                        climbSpot_ = hoverSpot(w);
                        playClip("move", true);
                        w.playSound("sounds/dash.wav", c, 0.8f, 0.6f);
                        w.particles().emit("slam_dust", box.bottomCenter(), 1.2f);
                    }
                    break;
                }
                case Stage::TakeOff: {
                    stageTimer_ += dt;
                    Vec2 to = climbSpot_ - c;
                    float dist = to.length();
                    Vec2 want = dist > 2.0f ? to / dist * (kClimbSpeed * phaseSpeed()) : Vec2{};
                    vel = lerpv(vel, want, damp(6.0f, dt));
                    face(playerPos_.x - c.x);
                    if (dist < 30.0f || stageTimer_ > 1.2f) stateTimer_ = 0.0f;
                    break;
                }
                default: break;
            }
            break;
        case AttackType::CloudShot:
            if (def_.flying) vel = lerpv(vel, {}, damp(5.0f, dt));
            if (burstsLeft_ > 0) {
                burstTimer_ -= dt;
                if (burstTimer_ <= 0.0f) {
                    --burstsLeft_;
                    burstTimer_ = a.burstInterval;
                    aimClouds(w);
                    fireClouds(w);
                }
            }
            break;
        case AttackType::Gust:
            if (def_.flying) vel = lerpv(vel, {}, damp(5.0f, dt));
            // Ash torn off by its wings (display), embers thrown into the wind.
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                hopTimer_ = 0.05f;
                for (float side : {-1.0f, 1.0f})
                    w.particles().emit("wing_ash", c + Vec2{side * box.w * 0.5f, w.fxRng().range(-30.0f, 60.0f)}, 1.0f, {side, 0.05f});
            }
            burstTimer_ -= dt;
            if (burstTimer_ <= 0.0f) {
                burstTimer_ = orDefault(a.burstInterval, 0.6f);
                blowEmbers(w);
            }
            break;
        default: break;
    }
}

void Enemy::dragonEnd(GameWorld& w, bool cut) {
    if (!attack_) return;
    // Its wings stop: the wind with them. An attack cut short (a new phase, its last stand)
    // leaves it where it is, still. The clouds keep their own time.
    if (attack_->type == AttackType::Gust) w.hazards().endChannel(w, uid_);
    if (cut) vel = {};
    stage_ = Stage::None;
    perched_ = false;
    dropThrough_ = false;
    diveLocked_ = false;
}

// ================================================================== render

void Enemy::renderDragon(RenderContext& ctx, float prog) const {
    const AttackDef& a = *attack_;
    const bool tele = state_ == EnemyState::Telegraph;
    const float s = ctx.r.view().scale / 3.0f;  // screen thickness of world strokes
    const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 14.0f);
    const Vec2 c = lerpv(prevBox.center(), box.center(), ctx.alpha);
    const Color fire = lightColor();
    switch (a.type) {
        case AttackType::Dive: {
            const bool aiming = tele || stage_ == Stage::Aim;
            if (aiming && path_.size() >= 2) {
                // Its way: from its body down the line, then along the ground (the skim).
                std::vector<Vec2> way = path_;
                way[0] = c;
                if (!diveLocked_) {
                    // Following its prey: a thin dotted line and a ring on it.
                    for (size_t k = 1; k < way.size(); ++k)
                        dashedLine(ctx.r, way[k - 1], way[k], Color{255, 190, 120, static_cast<uint8_t>(80 + 80 * prog)}, 1.5f * s, 8.0f, 8.0f);
                    ctx.r.drawWorldCircle(markPos_, 14.0f, Color{255, 170, 90, static_cast<uint8_t>(90 + 80 * prog)}, 1.5f * s);
                    break;
                }
                // Locked: the lane its body will sweep (its own height), a bright line, arrows
                // running the way it will go, a cross on the point it aims at.
                const float gap = 46.0f;
                float run = box.w * 0.5f - std::fmod(ctx.time * 140.0f, gap);  // distance to the next arrow
                for (size_t k = 1; k < way.size(); ++k) {
                    Vec2 a0 = way[k - 1], a1 = way[k];
                    Vec2 d = a1 - a0;
                    float len = d.length();
                    if (len < 1.0f) continue;
                    Vec2 dir = d / len;
                    ctx.r.drawWorldLine(a0, a1, Color{255, 50, 40, static_cast<uint8_t>(36 + 40 * pulse)}, box.h * 0.9f * ctx.r.view().scale,
                                        BlendMode::Additive);
                    ctx.r.drawWorldLine(a0, a1, Color{255, 110, 70, 220}, 2.5f * s, BlendMode::Additive);
                    for (; run < len - 8.0f; run += gap)
                        if (run > 0.0f) chevron(ctx.r, a0 + dir * run, dir, 14.0f, Color{255, 220, 160, 230}, 2.5f * s);
                    run -= len;
                }
                const float r = 16.0f + 4.0f * pulse;
                ctx.r.drawWorldCircle(markPos_, r, Color{255, 70, 60, 240}, 2.5f * s);
                ctx.r.drawWorldLine(markPos_ + Vec2{-r - 6.0f, 0.0f}, markPos_ + Vec2{r + 6.0f, 0.0f}, Color{255, 70, 60, 220}, 2.0f * s);
                ctx.r.drawWorldLine(markPos_ + Vec2{0.0f, -r - 6.0f}, markPos_ + Vec2{0.0f, r + 6.0f}, Color{255, 70, 60, 220}, 2.0f * s);
                ctx.r.drawWorldGlow(markPos_, 30.0f, Color{255, 60, 40, static_cast<uint8_t>(60 + 60 * pulse)});
            } else if (stage_ == Stage::Dash) {
                // Its wake of fire (2.52: never wider than a dragon's, the Kraken's body is twice as
                // tall: a streak, not a block of light).
                Vec2 mv = vel.lengthSq() > 1.0f ? vel.normalized() : diveDir_;
                Vec2 tail = c - mv * std::min(diveTravel_ + 20.0f, 170.0f);
                ctx.r.drawWorldLine(tail, c, fire.withAlpha(150), std::min(box.h * 0.6f, 46.0f) * ctx.r.view().scale, BlendMode::Additive);
                ctx.r.drawWorldGlow(c, box.w * 0.8f, fire.withAlpha(120));
                ctx.lights.add(c, 170.0f, fire);
            }
            break;
        }
        case AttackType::Perch: {
            if (tele || stage_ == Stage::Descend) {
                // Where it will land: its shadow on the ground, arrows down onto it.
                const float k = tele ? prog : 1.0f;
                const Color warn{255, 77, 109, static_cast<uint8_t>(80 + 140 * k)};
                ctx.r.fillWorldRect(Rect::fromCenter(markPos_ + Vec2{0.0f, -3.0f}, {box.w * (0.5f + 0.5f * k), 6.0f}),
                                    Color{24, 6, 10, static_cast<uint8_t>(120 + 100 * k)});
                ctx.r.fillWorldRect(Rect::fromCenter(markPos_ + Vec2{0.0f, -1.0f}, {box.w + 40.0f, 3.0f}), warn, BlendMode::Additive);
                ctx.r.drawWorldGlow(markPos_, box.w * 0.6f, warn.withAlpha(static_cast<uint8_t>(50 + 70 * k)));
                for (int i = 0; i < 3; ++i) {
                    float y = markPos_.y - 24.0f - static_cast<float>(i) * 18.0f + std::fmod(ctx.time * 36.0f, 18.0f);
                    chevron(ctx.r, {markPos_.x, y}, {0.0f, 1.0f}, 14.0f, warn, 2.5f * s);
                }
                if (tele) dashedLine(ctx.r, c, markPos_ + Vec2{0.0f, -box.h * 0.5f}, Color{255, 150, 90, static_cast<uint8_t>(60 + 100 * k)}, 1.5f * s, 8.0f, 8.0f);
            }
            if (perched_) {
                // Landed: its chest glows gold (a window to strike it harder).
                Vec2 chest = c + Vec2{facing * box.w * 0.1f, -box.h * 0.05f};
                ctx.r.drawWorldGlow(chest, box.w * (0.5f + 0.12f * pulse), Color{255, 214, 110, static_cast<uint8_t>(60 + 70 * pulse)});
                ctx.lights.add(chest, 120.0f, Color{255, 214, 110, 200});
            }
            break;
        }
        case AttackType::CloudShot:
            if (tele)
                for (const Vec2& t : path_) {
                    // Where the fireballs will burst (their cloud will stay there).
                    float r = 10.0f + 22.0f * prog;
                    ctx.r.fillWorldRect(Rect::fromCenter(t + Vec2{0.0f, -1.0f}, {r * 2.0f, 3.0f}),
                                        Color{fire.r, fire.g, fire.b, static_cast<uint8_t>(90 + 140 * prog)}, BlendMode::Additive);
                    ctx.r.drawWorldGlow(t, r * 1.4f, fire.withAlpha(static_cast<uint8_t>(40 + 80 * prog)));
                }
            break;
        case AttackType::Gust:
            if (tele) {
                // The air gathering under its wings, about to blow outward on both sides.
                for (float side : {-1.0f, 1.0f})
                    for (int i = 0; i < 3; ++i) {
                        float x = c.x + side * (box.w * 0.5f + 16.0f + static_cast<float>(i) * 18.0f + std::fmod(ctx.time * 30.0f, 18.0f));
                        chevron(ctx.r, {x, c.y + 10.0f}, {side, 0.0f}, 14.0f, Color{230, 215, 200, static_cast<uint8_t>(70 + 150 * prog)}, 2.5f * s);
                    }
            }
            break;
        default: break;
    }
}

}  // namespace cosmic
