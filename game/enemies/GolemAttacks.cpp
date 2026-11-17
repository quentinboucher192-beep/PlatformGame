// The attacks of the golem (2.52, the Monolithe) and its brain of a walker.
//
//  * « Carapace roulante » (shell): it curls into a shell of rune-carved stone (it glows, the
//    lane it will roll along is shown), then rolls at the target at full speed, bouncing off the
//    walls of its arena (and off the rim of a pit) once or twice, and stops toward the middle of
//    its floor. It hurts on contact and can be jumped over (a double jump clears it); curled it
//    takes less damage, uncurled it stays dizzy for a while and takes more.
//  * « Tremblement de terre » (quake): it pounds the ground and the arena shakes. Every wave, a
//    crack opens under the target (and others where a runner is not), glows, then bursts out of
//    the ground; rocks fall from the roof on marked spots. Standing still is punished, moving or
//    jumping avoids it.
//  * « Bond titanesque » (skyfall): it crouches and leaps out of sight; its shadow follows the
//    target on the ground, holds still, then it falls on it. The landing hurts around it and
//    sends shockwaves running along the floor both ways (to jump over); the ground shakes.
//
// A walker's brain: between two blows it takes a few steps to its fighting distance, on its own
// side of the target, inside its firm floor (no crust that may give way, never at the edge of a
// pit, never against a wall); pressed at an end of its floor with the target on the inside, it
// prefers what takes it past her (its leaps, its shell, its charge). It never falls into a pit:
// its leaps land on its floor, its charge stops before the end of it, its shell rolls back from
// the rim, and a new phase waits for it to stand on firm ground again.
//
// Like every attack they are announced during the wind-up and they are deterministic: positions
// come from the simulation, the only randomness is the world's gameplay generator (the effects'
// own one for what is only shown). The cracks and the falling rocks live in BossHazards.

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

/// std::clamp, with the middle when the range is empty (a narrow floor).
float clampIn(float v, float lo, float hi) { return lo <= hi ? std::clamp(v, lo, hi) : (lo + hi) * 0.5f; }

constexpr Vec2 kShell{72.0f, 72.0f};  // the curled golem: a double jump clears it
constexpr float kComfort = 48.0f;     // its body keeps this far from the ends of its floor
constexpr float kBeat = 0.25f;        // steps between two blows (scaled by the phase's cooldowns)
constexpr float kRise = 1000.0f;      // titanic leap: up and out of sight
constexpr float kTrack = 240.0f;      // its shadow follows the target (faster than she runs)
constexpr float kLock = 0.45f;        // then holds still this long before the fall
constexpr float kFallStart = 500.0f;
constexpr float kFallAccel = 2800.0f;
constexpr float kFallMax = 1500.0f;
constexpr float kSkid = 0.3f;         // the shell slows down to a halt
constexpr float kUncurl = 0.3f;       // it unfolds before it reels

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

/// The speed of its roll: faster in the later phases, never too fast to be jumped over.
float rollSpeed(const AttackDef& a, float phaseSpeed) { return std::max(160.0f, orDefault(a.lungeSpeed, 380.0f) * std::min(phaseSpeed, 1.2f)); }
}  // namespace

// ================================================================== its floor

std::pair<float, float> Enemy::floorSpan(const GameWorld& w, float x, float y, bool crust) const {
    // The surface at height y around x, as far as it stays level on both sides; a wall (at the
    // height of its knees or of its chest) or a pit ends it. Without `crust`, a platform that
    // may give way is no floor.
    const Rect arena = arenaArea(w);
    const Rect& b = w.level().bounds;
    const float lo = std::max(arena.x, b.x), hi = std::min(arena.right(), b.right());
    const uint32_t ignore = crust ? 0u : ColliderTag::Crumble;
    const float chest = std::max(16.0f, standSize_.y * 0.5f);
    auto ground = [&](float sx) {
        Vec2 from{sx, y - 6.0f}, to{sx, y + 6.0f};
        auto t = w.physics().raycast(from, to, false, ignore);
        return t && std::abs(from.y + (to.y - from.y) * *t - y) < 3.0f;
    };
    auto open = [&](float a, float c) {
        for (float h : {12.0f, chest})
            if (w.physics().raycast({a, y - h}, {c, y - h})) return false;
        return true;
    };
    constexpr float step = 8.0f;
    float l = x, r = x;
    while (l - step >= lo && x - l < 2400.0f && ground(l - step) && open(l, l - step)) l -= step;
    while (r + step <= hi && r - x < 2400.0f && ground(r + step) && open(r, r + step)) r += step;
    return {l, r};
}

void Enemy::refreshFloor(GameWorld& w) {
    floorTimer_ = 0.5f;
    if (!grounded_ && floorKnown_) return;  // in the air: the floor it left
    const float y = box.bottom();
    auto [l, r] = floorSpan(w, box.center().x, y, false);
    if (r - l < box.w * 0.5f && floorKnown_) return;  // not on its firm floor right now
    floorL_ = l;
    floorR_ = r;
    floorY_ = y;
    floorKnown_ = true;
    auto [rl, rr] = floorSpan(w, box.center().x, y, true);
    rollL_ = std::min(rl, l);
    rollR_ = std::max(rr, r);
}

float Enemy::walkLo() const { return floorL_ + standSize_.x * 0.5f + kComfort; }
float Enemy::walkHi() const { return floorR_ - standSize_.x * 0.5f - kComfort; }

void Enemy::setBody(const GameWorld& w, Vec2 size) {
    // Its box changes (curled, standing) around its feet; never into a wall (a little aside).
    MoveOptions opt;
    opt.isEnemy = true;
    const Vec2 feet = box.bottomCenter();
    for (float shift : {0.0f, 8.0f, -8.0f, 16.0f, -16.0f, 24.0f, -24.0f, 32.0f, -32.0f}) {
        Rect cand = Rect::fromBottomCenter(feet + Vec2{shift, 0.0f}, size);
        if (w.physics().overlapsSolid(cand, opt)) continue;
        box = cand;
        prevBox = Rect::fromBottomCenter(prevBox.bottomCenter() + Vec2{shift, 0.0f}, size);
        return;
    }
    box = Rect::fromBottomCenter(feet, size);  // nowhere better: as it is
    prevBox = Rect::fromBottomCenter(prevBox.bottomCenter(), size);
}

// ================================================================== its brain

float Enemy::golemSpot(GameWorld& w, float dt) {
    floorTimer_ -= dt;
    if (!floorKnown_ || floorTimer_ <= 0.0f) refreshFloor(w);
    const float bx = box.center().x, px = playerPos_.x;
    const float lo = walkLo(), hi = walkHi();
    const float above = box.bottom() - (playerPos_.y + 12.0f);
    // A ledge right over the target's head: what falls from above is not worth much there.
    {
        Vec2 head{px, playerPos_.y - 14.0f};
        sheltered_ = w.physics().raycast(head, head + Vec2{0.0f, -150.0f}, true).has_value();
    }
    // Where it fights from: at its distance from the target, on its own side of her, on its
    // floor (never at the edge of a pit, never against a wall); a perched target from closer.
    const float side = bx <= px ? -1.0f : 1.0f;
    const float reach = above > 48.0f ? def_.preferredDistance * 0.8f : def_.preferredDistance;
    float want = clampIn(px + side * reach, lo, hi);
    // Its floor pushes it to the other side of her: it never walks through her.
    const float toward = px - bx;
    if ((want - bx) * toward > 0.0f && std::abs(want - bx) > std::abs(toward) - 64.0f) want = px - signf(toward) * 64.0f;
    // Pressed at an end of its floor with the target on the inside: walking will not get it round
    // her; its leaps, its shell or its charge will (golemFit).
    const bool atEnd = (bx < lo + 24.0f && toward > 0.0f) || (bx > hi - 24.0f && toward < 0.0f);
    edgeTime_ = atEnd ? edgeTime_ + dt : 0.0f;
    return want;
}

void Enemy::golemWalk(float dt, float want, float pace) {
    // Its steps (never past the end of its floor). It faces the target (it steps back facing
    // her); a long way, it turns to walk.
    const float bx = box.center().x, gap = std::abs(want - bx);
    float dir = gap > 6.0f ? signf(want - bx) : 0.0f;
    if ((dir < 0.0f && box.x <= floorL_ + 2.0f) || (dir > 0.0f && box.right() >= floorR_ - 2.0f)) dir = 0.0f;
    const float speed = def_.speed * def_.chaseSpeedMultiplier * phaseSpeed() * pace;
    vel.x = approach(vel.x, dir * speed, 900.0f * dt);
    face(dir != 0.0f && gap > 140.0f ? dir : playerPos_.x - bx);
}

void Enemy::golemThink(GameWorld& w, float dt) {
    const float want = golemSpot(w, dt);
    // A beat of steps between two blows (none in a combo), longer when badly placed; pressed at
    // an end it acts at once.
    const float gap = std::abs(want - box.center().x);
    float beat = kBeat * phaseCooldown();
    if (gap > 90.0f) beat += std::min(0.4f, gap / 350.0f);
    if (comboLeft_ > 0 || edgeTime_ > 0.5f) beat = 0.0f;
    walkTime_ += dt;
    if (playerVisible_ && walkTime_ >= beat && chooseAttack(w)) return;
    golemWalk(dt, want, 1.0f);
    playClip(std::abs(vel.x) > 5.0f ? "walk" : "idle");
}

float Enemy::golemFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const {
    // The situation of the fight, for every attack of the golem: a target far away, close, perched
    // above it, sheltered under a ledge, in the air; pressed at an end of its floor.
    const float above = box.bottom() - (playerPos_.y + 12.0f);
    const bool perched = above > 48.0f;
    const bool cornered = edgeTime_ > 0.35f;
    const bool hfar = std::abs(playerPos_.x - box.center().x) > 420.0f;
    float k = 1.0f;
    switch (a.type) {
        case AttackType::Shell:
            if (perched) return 0.05f;  // it rolls on the floor
            k = (far ? 1.6f : (near ? 0.9f : 1.2f)) * (air ? 0.7f : 1.0f) * (sheltered_ ? 1.4f : 1.0f);
            break;
        case AttackType::Quake:
            if (w.hazards().crackCount(uid_) > 6) return 0.2f;
            k = (perched ? 1.8f : 1.0f) * (air ? 1.3f : 1.0f) * (sheltered_ ? 1.3f : 1.0f) * (near ? 0.8f : 1.0f);
            break;
        case AttackType::Skyfall:
            k = (hfar ? 2.2f : (near ? 0.4f : 1.0f)) * (perched ? 0.6f : 1.0f) * (sheltered_ ? 0.3f : 1.0f) * (air ? 1.2f : 1.0f);
            break;
        case AttackType::Leap: k = (hfar ? 1.4f : 1.0f) * (sheltered_ ? 0.5f : 1.0f); break;
        case AttackType::Charge: k = perched ? 0.2f : (sheltered_ ? 1.3f : 1.0f); break;
        case AttackType::Slam:
        case AttackType::Tracker: k = (perched ? 0.3f : 1.0f) * (sheltered_ ? 1.3f : 1.0f) * (hfar ? 0.5f : 1.0f); break;
        case AttackType::Rain:
        case AttackType::Eruption: k = (perched ? 1.5f : 1.0f) * (sheltered_ ? 0.4f : 1.0f); break;
        case AttackType::Arc:
        case AttackType::Laser:
        case AttackType::Prism: k = perched ? 1.5f : 1.0f; break;
        default: break;
    }
    // Pressed at an end of its floor: what takes it past the target first.
    if (cornered) {
        const bool moves = a.type == AttackType::Shell || a.type == AttackType::Skyfall || a.type == AttackType::Leap ||
                           a.type == AttackType::Charge;
        k *= moves ? 3.0f : 0.4f;
    }
    return k;
}

// ============================================================ the wind-up

void Enemy::rollLane(GameWorld& w) {
    // Its way: to the wall (or the rim of a pit) toward the target, back, and so on, the last leg
    // toward the middle of its floor where it stops.
    (void)w;
    const float half = kShell.x * 0.5f;
    const float lo = rollL_ + half + 1.0f, hi = rollR_ - half - 1.0f;
    float x = box.center().x;
    float dir = rollDir_;
    path_.clear();
    path_.push_back({x, floorY_});
    for (int leg = 0; leg <= bouncesLeft_; ++leg) {
        const bool last = leg == bouncesLeft_;
        float end = dir > 0.0f ? hi : lo;
        if (last && (rollStop_ - x) * dir > 0.0f) end = rollStop_;
        x = clampIn(end, lo, hi);
        path_.push_back({x, floorY_});
        dir = -dir;
    }
}

void Enemy::golemBegin(GameWorld& w) {
    const AttackDef& a = *attack_;
    path_.clear();
    shadowLocked_ = false;
    ringsLeft_ = 0;
    refreshFloor(w);
    switch (a.type) {
        case AttackType::Shell: {
            golem_ = GolemStage::Curl;
            stateTimer_ = std::max(stateTimer_, 0.7f);  // it curls: readable
            rollDir_ = signf(playerPos_.x - box.center().x);
            if (rollDir_ == 0.0f) rollDir_ = facing;
            face(rollDir_);
            bouncesLeft_ = a.bounces > 0 ? a.bounces : 1;
            // Its last leg ends toward the middle of its floor (a little aside, never the same).
            rollStop_ = clampIn((walkLo() + walkHi()) * 0.5f + w.rng().range(-90.0f, 90.0f), walkLo(), walkHi());
            rollLane(w);
            if (a.sound.empty()) w.playSound("sounds/golem_slam.wav", box.center(), 0.5f, 0.7f);
            break;
        }
        case AttackType::Quake:
            golem_ = GolemStage::Quake;
            stateTimer_ = std::max(stateTimer_, 0.5f);
            markPos_ = playerPos_;
            break;
        case AttackType::Skyfall:
            golem_ = GolemStage::Crouch;
            stateTimer_ = std::max(stateTimer_, 0.5f);  // it crouches
            shadow_ = {clampIn(playerPos_.x, walkLo(), walkHi()), floorY_};
            break;
        default: break;
    }
}

void Enemy::golemTelegraph(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    switch (a.type) {
        case AttackType::Shell:
            // The lane follows the target, then holds still: the roll goes where it shows.
            if (stateTimer_ > 0.35f) {
                float dir = signf(playerPos_.x - box.center().x);
                if (dir != 0.0f) rollDir_ = dir;
                face(rollDir_);
                rollLane(w);
            }
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                hopTimer_ = 0.08f;
                Color c = lightColor();
                w.particles().emit("rune_glow", box.center() + Vec2{w.fxRng().range(-30.0f, 30.0f), w.fxRng().range(-20.0f, 30.0f)}, 1.0f, {}, &c);
            }
            break;
        case AttackType::Skyfall:
            shadow_.x = clampIn(approach(shadow_.x, playerPos_.x, kTrack * dt), walkLo(), walkHi());
            break;
        case AttackType::Quake:
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                hopTimer_ = 0.12f;
                w.shake(0.06f);
            }
            break;
        default: break;
    }
}

// =========================================================== the attack

void Enemy::quakeWave(GameWorld& w) {
    // The ground shakes; a crack opens under the target (standing still is punished) and others
    // on both sides where a runner is not (farther than she runs before they burst); rocks come
    // loose from the roof onto marked spots around her.
    const AttackDef& a = *attack_;
    const Rect arena = arenaArea(w);
    w.shake(0.35f);
    w.rumble(0.5f, 0.3f, 160);
    w.playSound("sounds/golem_slam.wav", box.bottomCenter(), 0.9f, 0.8f);
    w.particles().emit("slam_dust", box.bottomCenter() + Vec2{facing * box.w * 0.4f, 0.0f}, 1.4f);
    const float half = orDefault(a.radius, 22.0f);
    const float gap = orDefault(a.spread, 150.0f);
    const float delay = std::max(0.45f, orDefault(a.stagger, 0.75f) * telegraphScale());
    const Color glow = lightColor();
    const int n = std::max(1, a.count);
    for (int k = 0; k < n; ++k) {
        // 0, +1, -1, +2, -2... spacings around the target.
        int step = (k + 1) / 2;
        float s = k == 0 ? 0.0f : (k % 2 == 1 ? 1.0f : -1.0f);
        float x = playerPos_.x + s * gap * static_cast<float>(step);
        if (x < arena.x + half + 4.0f || x > arena.right() - half - 4.0f) continue;
        auto g = groundAt(w, x, playerPos_.y);
        if (!g) continue;  // over a pit: nothing to break
        BossHazards::Crack c;
        c.owner = uid_;
        c.at = {x, *g};
        c.half = half;
        c.height = orDefault(a.height, 46.0f);
        c.delay = delay;
        c.damage = a.damage;
        c.knockback = std::max(a.knockback, 200.0f);
        c.color = glow;
        w.hazards().addCrack(c);
    }
    // The rocks: one on her, the others behind her (where she comes from: a runner keeps ahead
    // of them, one who stops or turns back walks into them).
    const int rocks = a.arms > 0 ? a.arms : 2;
    const float g = orDefault(a.gravity, 900.0f);
    const float top = arena.y + 10.0f;
    float behind = -signf(playerVel_.x);
    if (std::abs(playerVel_.x) < 30.0f) behind = w.rng().chance(0.5f) ? 1.0f : -1.0f;
    for (int k = 0; k < rocks; ++k) {
        const float x = clampIn(playerPos_.x + behind * 110.0f * static_cast<float>(k) + w.rng().range(-16.0f, 16.0f), arena.x + 24.0f,
                                arena.right() - 24.0f);
        // It lands on the surface she stands on (it crashes through the ledges above her).
        auto ground = groundAt(w, x, playerPos_.y);
        if (!ground) continue;  // over a pit
        const float gy = *ground;
        const float r = orDefault(a.projectileRadius, 13.0f);
        const float fall = std::max(8.0f, gy - top - r * 2.0f);
        const float v0 = std::max(0.0f, a.projectileSpeed);
        BossHazards::Crack c;
        c.owner = uid_;
        c.rock = true;
        c.at = {x, gy};
        c.fromY = top;
        c.half = r;
        c.height = 26.0f;
        c.delay = (-v0 + std::sqrt(v0 * v0 + 2.0f * g * fall)) / g;
        c.burst = 0.12f;
        c.damage = a.damage;
        c.knockback = std::max(a.knockback, 160.0f);
        c.color = glow;
        w.hazards().addCrack(c);
    }
}

void Enemy::shockwaves(GameWorld& w, int ring) {
    // Two waves running along the floor from its feet, both ways: low, to jump over.
    const AttackDef& a = *attack_;
    const float r = orDefault(a.projectileRadius, 10.0f);
    const float speed = std::max(60.0f, a.projectileSpeed) * (1.0f - 0.12f * static_cast<float>(ring));
    for (float dir : {-1.0f, 1.0f}) {
        Projectile p;
        p.pos = p.prevPos = {box.center().x + dir * (box.w * 0.5f + 6.0f), box.bottom() - 6.0f};  // the floor right under it
        p.vel = {dir * speed, 0.0f};
        p.damage = a.damage;
        p.type = DamageType::Explosion;
        p.clip = "shockwave";
        p.radius = r;
        p.life = std::max(0.3f, a.range / speed);
        p.light = lightColor();
        p.lightRadius = 44.0f;
        p.knockback = a.knockback;
        p.hitEffect = "slam_dust";
        w.spawnProjectile(p);
    }
    w.particles().emit("slam_dust", box.bottomCenter(), 1.6f);
}

void Enemy::landSkyfall(GameWorld& w) {
    const AttackDef& a = *attack_;
    box = Rect::fromBottomCenter({box.center().x, floorY_}, box.size());
    vel = {};
    dropThrough_ = false;
    grounded_ = true;
    golem_ = GolemStage::Land;
    golemTimer_ = std::max(0.5f, a.active);
    shadowLocked_ = false;
    playClip("land", true);
    w.shake(0.9f);
    w.rumble(0.9f, 0.7f, 320);
    w.playSound("sounds/pound_impact.wav", box.bottomCenter(), 1.0f, 0.6f);
    w.playSound("sounds/golem_slam.wav", box.bottomCenter(), 1.0f, 0.7f);
    w.particles().emit("slam_dust", box.bottomCenter() + Vec2{-box.w * 0.4f, 0.0f}, 2.0f);
    w.particles().emit("slam_dust", box.bottomCenter() + Vec2{box.w * 0.4f, 0.0f}, 2.0f);
    w.particles().emit("quake_rocks", box.bottomCenter() + Vec2{-box.w * 0.5f, 0.0f}, 1.6f);
    w.particles().emit("quake_rocks", box.bottomCenter() + Vec2{box.w * 0.5f, 0.0f}, 1.6f);
    // Under it and around its feet: crushed.
    const float reach = orDefault(a.radius, 26.0f);
    Rect stomp{box.x - reach, box.y, box.w + reach * 2.0f, box.h};
    w.forEachPlayer([&](Player& p) {  // 2.62: every player crushed
        if (!p.alive() || !stomp.intersects(p.box)) return;
        DamageInfo d;
        d.amount = std::max(a.damage, def_.contactDamage);
        d.type = DamageType::Contact;
        d.source = box.bottomCenter();
        d.knockback = std::max(a.knockback, 280.0f);
        w.damagePlayer(d);
    });
    shockwaves(w, 0);
    ringsLeft_ = std::max(0, a.count - 1);
    burstTimer_ = orDefault(a.burstInterval, 0.35f);
    if (ringsLeft_ > 0) golemTimer_ = std::max(golemTimer_, burstTimer_ * static_cast<float>(ringsLeft_) + 0.2f);
}

void Enemy::golemStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackClock_ = 0.0f;
    burstTimer_ = 0.0f;
    hopTimer_ = 0.0f;
    laserHitTimer_ = 0.0f;
    switch (a.type) {
        case AttackType::Shell:
            setBody(w, kShell);
            golem_ = GolemStage::Roll;
            golemTimer_ = 0.0f;
            vel.x = rollDir_ * rollSpeed(a, phaseSpeed()) * 0.4f;
            stateTimer_ = 16.0f;  // it ends itself (safety)
            w.shake(0.25f);
            w.playSound("sounds/dash.wav", box.center(), 0.9f, 0.5f);
            w.particles().emit("slam_dust", box.bottomCenter(), 1.4f);
            break;
        case AttackType::Quake:
            golem_ = GolemStage::Quake;
            stateTimer_ = orDefault(a.duration, 3.6f);
            burstTimer_ = 0.0f;  // the first wave at once
            hopTimer_ = 0.25f;
            break;
        case AttackType::Skyfall: {
            golem_ = GolemStage::Rise;
            golemTimer_ = 0.0f;
            vel = {0.0f, -kRise};
            climbSpot_ = {box.center().x, arenaArea(w).y - 40.0f};  // out of sight: above the roof of its arena
            dropThrough_ = true;
            stateTimer_ = 12.0f;  // it ends itself (safety)
            w.shake(0.4f);
            w.playSound("sounds/dash.wav", box.center(), 1.0f, 0.45f);
            w.particles().emit("slam_dust", box.bottomCenter(), 2.2f);
            break;
        }
        default: break;
    }
}

void Enemy::golemActive(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    switch (a.type) {
        case AttackType::Shell: {
            const float sp = rollSpeed(a, phaseSpeed());
            const float half = box.w * 0.5f;
            const float lo = rollL_ + half + 1.0f, hi = rollR_ - half - 1.0f;
            const float cx = box.center().x;
            if (golem_ == GolemStage::Roll) {
                vel.x = approach(vel.x, rollDir_ * sp, sp * 4.0f * dt);
                // A wall, the rim of a pit (or of the crust that gave way) ahead: it bounces back.
                const float lead = cx + rollDir_ * (half + std::abs(vel.x) * dt + 3.0f);
                Vec2 from{lead, box.bottom() - 6.0f}, to{lead, box.bottom() + 10.0f};
                const bool ground = w.physics().raycast(from, to, false).has_value();
                const bool wall = w.physics().raycast(box.center(), {lead, box.center().y}).has_value();
                const bool end = (rollDir_ > 0.0f && cx + vel.x * dt >= hi) || (rollDir_ < 0.0f && cx + vel.x * dt <= lo) || !ground || wall;
                const bool lastLeg = bouncesLeft_ <= 0;
                if (end && !lastLeg) {
                    --bouncesLeft_;
                    rollDir_ = -rollDir_;
                    vel.x = rollDir_ * sp * 0.55f;
                    face(rollDir_);
                    w.shake(0.45f);
                    w.rumble(0.5f, 0.3f, 140);
                    w.playSound("sounds/golem_slam.wav", box.center(), 1.0f, 1.1f);
                    w.particles().emit("slam_dust", {cx - rollDir_ * half, box.center().y}, 1.6f);
                    w.particles().emit("rock_shatter", {cx - rollDir_ * half, box.center().y}, 0.6f);
                    rollLane(w);
                } else if (end || (lastLeg && (cx - rollStop_) * rollDir_ >= 0.0f)) {
                    golem_ = GolemStage::Skid;
                    golemTimer_ = kSkid;
                    if (end) vel.x = 0.0f;
                }
                face(rollDir_);
            } else if (golem_ == GolemStage::Skid) {
                vel.x = approach(vel.x, 0.0f, sp / kSkid * dt);
                const float lead = cx + signf(vel.x) * (half + std::abs(vel.x) * dt + 3.0f);
                if (vel.x != 0.0f && !w.physics().raycast({lead, box.bottom() - 6.0f}, {lead, box.bottom() + 10.0f}, false)) vel.x = 0.0f;
                golemTimer_ -= dt;
                if (golemTimer_ <= 0.0f && grounded_) {
                    vel.x = 0.0f;
                    setBody(w, standSize_);
                    golem_ = GolemStage::Uncurl;
                    golemTimer_ = kUncurl;
                    playClip("uncurl", true);
                    w.particles().emit("slam_dust", box.bottomCenter(), 1.0f);
                }
            } else if (golem_ == GolemStage::Uncurl) {
                vel.x = 0.0f;
                golemTimer_ -= dt;
                if (golemTimer_ <= 0.0f) {
                    golem_ = GolemStage::Dizzy;
                    golemTimer_ = orDefault(a.duration, 1.6f);
                    playClip("dizzy", true);
                }
            } else if (golem_ == GolemStage::Dizzy) {
                vel.x = 0.0f;
                golemTimer_ -= dt;
                if (golemTimer_ <= 0.0f) stateTimer_ = 0.0f;
            }
            if (curled()) {
                // The shell hurts what it rolls into.
                if (laserHitTimer_ <= 0.0f && std::abs(vel.x) > 20.0f && w.playerIn(box.expanded(1.0f))) {
                    DamageInfo d;
                    d.amount = a.damage;
                    d.type = DamageType::Contact;
                    d.source = {box.center().x - signf(vel.x) * 8.0f, box.center().y + 6.0f};
                    d.knockback = a.knockback;
                    if (w.hurtPlayersIn(box.expanded(1.0f), d)) laserHitTimer_ = 0.6f;
                }
                hopTimer_ -= dt;
                if (hopTimer_ <= 0.0f && std::abs(vel.x) > 40.0f) {
                    hopTimer_ = 0.05f;
                    w.particles().emit("dust_run", box.bottomCenter(), 1.5f, {-signf(vel.x), -0.3f});
                }
            }
            break;
        }
        case AttackType::Quake:
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            burstTimer_ -= dt;
            if (burstTimer_ <= 0.0f) {
                burstTimer_ += orDefault(a.burstInterval, 0.8f);
                if (stateTimer_ > 0.3f) quakeWave(w);
            }
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                hopTimer_ = 0.25f;
                w.shake(0.14f);  // the whole arena trembles
            }
            break;
        case AttackType::Skyfall: {
            switch (golem_) {
                case GolemStage::Rise:
                    vel = {0.0f, -kRise};
                    golemTimer_ += dt;
                    if (box.bottom() < climbSpot_.y || golemTimer_ > 1.2f) {
                        // Out of sight: its shadow hunts the target.
                        golem_ = GolemStage::Hang;
                        golemTimer_ = orDefault(a.duration, 1.1f);
                        vel = {};
                    }
                    break;
                case GolemStage::Hang:
                    vel = {};
                    golemTimer_ -= dt;
                    if (golemTimer_ > kLock) {
                        shadow_.x = clampIn(approach(shadow_.x, playerPos_.x, kTrack * dt), walkLo(), walkHi());
                    } else if (!shadowLocked_) {
                        shadowLocked_ = true;
                        w.playSound("sounds/laser_charge.wav", shadow_, 0.6f, 0.6f);
                    }
                    if (golemTimer_ <= 0.0f) {
                        // Over its shadow, it falls.
                        box = Rect::fromBottomCenter({shadow_.x, std::min(box.bottom(), climbSpot_.y + 20.0f)}, box.size());
                        prevBox = box;
                        golem_ = GolemStage::Fall;
                        vel = {0.0f, kFallStart};
                        playClip("fall", true);
                        w.playSound("sounds/pound_dive.wav", shadow_, 1.0f, 0.6f);
                    }
                    break;
                case GolemStage::Fall:
                    vel.y = std::min(vel.y + kFallAccel * dt, kFallMax);
                    if (box.bottom() + vel.y * dt >= floorY_) landSkyfall(w);
                    else if (!attackHit_ && w.playerIn(box)) {
                        DamageInfo d;
                        d.amount = std::max(a.damage, def_.contactDamage);
                        d.type = DamageType::Contact;
                        d.source = {box.center().x, box.y};
                        d.knockback = std::max(a.knockback, 280.0f);
                        attackHit_ = w.hurtPlayersIn(box, d) || true;
                    }
                    break;
                case GolemStage::Land:
                    vel.x = 0.0f;
                    golemTimer_ -= dt;
                    if (ringsLeft_ > 0) {
                        burstTimer_ -= dt;
                        if (burstTimer_ <= 0.0f) {
                            burstTimer_ = orDefault(a.burstInterval, 0.35f);
                            shockwaves(w, a.count - ringsLeft_);
                            --ringsLeft_;
                            w.shake(0.3f);
                        }
                    }
                    if (golemTimer_ <= 0.0f && ringsLeft_ == 0) stateTimer_ = 0.0f;
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }
}

void Enemy::golemEnd(GameWorld& w, bool cut) {
    if (!attack_) return;
    // Never left in the sky nor curled up: on its floor under its shadow, standing. The cracks and
    // the rocks keep their own time.
    if (skyborne()) {
        box = Rect::fromBottomCenter({clampIn(shadow_.x, walkLo(), walkHi()), floorY_}, box.size());
        prevBox = box;
        vel = {};
    }
    if (box.w != standSize_.x || box.h != standSize_.y) setBody(w, standSize_);
    golem_ = GolemStage::None;
    dropThrough_ = false;
    shadowLocked_ = false;
    ringsLeft_ = 0;
    if (cut) vel.x = 0.0f;
}

// ================================================================== render

void Enemy::renderGolem(RenderContext& ctx, float prog) const {
    const AttackDef& a = *attack_;
    const bool tele = state_ == EnemyState::Telegraph;
    const float s = ctx.r.view().scale / 3.0f;  // screen thickness of world strokes
    const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 14.0f);
    const Vec2 c = lerpv(prevBox.center(), box.center(), ctx.alpha);
    const Color rune = lightColor();
    switch (a.type) {
        case AttackType::Shell: {
            if (tele && path_.size() >= 2) {
                // Its lane along the floor: a band the height of the shell, arrows running the way
                // it will roll, dimmer after each bounce; a flash where it bounces.
                const float gapArrows = 44.0f;
                for (size_t k = 1; k < path_.size(); ++k) {
                    Vec2 a0 = path_[k - 1], a1 = path_[k];
                    float len = std::abs(a1.x - a0.x);
                    if (len < 2.0f) continue;
                    float dir = signf(a1.x - a0.x);
                    const uint8_t al = static_cast<uint8_t>((k == 1 ? 1.0f : 0.5f) * (60 + 140 * prog));
                    float y = a0.y - kShell.y * 0.5f;
                    ctx.r.fillWorldRect({std::min(a0.x, a1.x), a0.y - kShell.y, len, kShell.y},
                                        Color{rune.r, rune.g, rune.b, static_cast<uint8_t>(al / 5)}, BlendMode::Additive);
                    ctx.r.fillWorldRect({std::min(a0.x, a1.x), a0.y - 3.0f, len, 3.0f}, Color{255, 90, 70, al}, BlendMode::Additive);
                    float run = 20.0f + std::fmod(ctx.time * 160.0f, gapArrows);
                    for (; run < len - 8.0f; run += gapArrows)
                        chevron(ctx.r, {a0.x + dir * run, y + (k == 1 ? 0.0f : 12.0f)}, {dir, 0.0f}, 16.0f,
                                Color{255, 220, 170, al}, 2.5f * s);
                    if (k + 1 < path_.size()) ctx.r.drawWorldGlow({a1.x + dir * kShell.x * 0.5f, y}, 28.0f, Color{255, 120, 80, al});
                }
            }
            if (curled() || tele) {
                // The runes of its shell blaze.
                ctx.r.drawWorldGlow(c + Vec2{0.0f, tele ? 0.0f : 0.0f}, box.w * (0.6f + 0.15f * pulse),
                                    rune.withAlpha(static_cast<uint8_t>(tele ? 40 + 90 * prog : 90 + 50 * pulse)));
                ctx.lights.add(c, 120.0f, rune);
            }
            if (rolling() && std::abs(vel.x) > 40.0f) {
                // Its wake: glows fading behind it, dust low on the ground.
                const float back = -signf(vel.x), len = std::min(150.0f, std::abs(vel.x) * 0.35f);
                for (int k = 1; k <= 5; ++k) {
                    const float f = static_cast<float>(k) / 5.0f;
                    ctx.r.drawWorldGlow(c + Vec2{back * len * f, box.h * 0.1f * f}, box.w * (0.45f - 0.25f * f),
                                        rune.withAlpha(static_cast<uint8_t>(90 * (1.0f - f))));
                }
            }
            if (dizzy()) {
                // Dizzy: rune pebbles circle over its head; its chest glows (strike it now).
                Vec2 head = c + Vec2{facing * box.w * 0.15f, -box.h * 0.62f};
                for (int k = 0; k < 4; ++k) {
                    float ang = ctx.time * 4.0f + static_cast<float>(k) * kPi * 0.5f;
                    Vec2 pp = head + Vec2{dmath::cos(ang) * 28.0f, dmath::sin(ang) * 8.0f};
                    ctx.r.fillWorldCircle(pp, 3.0f, Color{142, 130, 112, 255});
                    ctx.r.drawWorldGlow(pp, 7.0f, rune.withAlpha(150));
                }
                Vec2 chest = c + Vec2{facing * box.w * 0.1f, -box.h * 0.08f};
                ctx.r.drawWorldGlow(chest, box.w * (0.45f + 0.1f * pulse), Color{255, 214, 110, static_cast<uint8_t>(60 + 70 * pulse)});
                ctx.lights.add(chest, 110.0f, Color{255, 214, 110, 200});
            }
            break;
        }
        case AttackType::Quake:
            if (tele) {
                // Its fists in the ground: cracks of light run toward the target.
                Vec2 base = box.bottomCenter();
                float dir = signf(markPos_.x - base.x);
                if (dir == 0.0f) dir = facing;
                float reach = (box.w * 0.5f + 40.0f) + 160.0f * prog;
                Vec2 tip = base + Vec2{dir * reach, 0.0f};
                for (int k = 0; k < 3; ++k) {
                    float off = static_cast<float>(k - 1) * 2.0f;
                    ctx.r.drawWorldLine(base + Vec2{0.0f, off - 2.0f}, tip + Vec2{0.0f, off - 2.0f},
                                        rune.withAlpha(static_cast<uint8_t>(60 + 120 * prog)), 1.5f * s, BlendMode::Additive);
                }
                ctx.r.drawWorldGlow(tip, 18.0f, rune.withAlpha(static_cast<uint8_t>(60 + 100 * prog)));
            }
            break;
        case AttackType::Skyfall: {
            if (golem_ == GolemStage::Land) break;
            // Its shadow on the floor: wider and darker as it comes down; red once it holds still.
            float k = 0.3f;
            if (golem_ == GolemStage::Crouch) k = 0.3f * prog;
            else if (golem_ == GolemStage::Rise) k = 0.4f;
            else if (golem_ == GolemStage::Hang) k = 0.55f + 0.25f * (shadowLocked_ ? 1.0f : 0.0f);
            else if (golem_ == GolemStage::Fall) k = 0.8f + 0.2f * std::clamp(1.0f - (floorY_ - box.bottom()) / 600.0f, 0.0f, 1.0f);
            const Vec2 at = shadow_;
            const float wdt = standSize_.x * (0.5f + 0.7f * k);
            ctx.r.fillWorldRect(Rect::fromCenter(at + Vec2{0.0f, -3.0f}, {wdt, 6.0f}), Color{16, 10, 8, static_cast<uint8_t>(90 + 140 * k)});
            ctx.r.fillWorldRect(Rect::fromCenter(at + Vec2{0.0f, -2.0f}, {wdt * 0.7f, 4.0f}), Color{8, 4, 4, static_cast<uint8_t>(110 + 140 * k)});
            if (shadowLocked_ || golem_ == GolemStage::Fall) {
                const Color warn{255, 70, 60, static_cast<uint8_t>(160 + 80 * pulse)};
                ctx.r.fillWorldRect(Rect::fromCenter(at + Vec2{0.0f, -1.0f}, {standSize_.x + 52.0f, 3.0f}), warn, BlendMode::Additive);
                ctx.r.drawWorldGlow(at, standSize_.x * 0.7f, warn.withAlpha(static_cast<uint8_t>(70 + 60 * pulse)));
                for (int i = 0; i < 3; ++i) {
                    float y = at.y - 30.0f - static_cast<float>(i) * 20.0f + std::fmod(ctx.time * 60.0f, 20.0f);
                    chevron(ctx.r, {at.x, y}, {0.0f, 1.0f}, 16.0f, warn, 2.5f * s);
                }
                // Where its shockwaves will run.
                for (float side : {-1.0f, 1.0f})
                    dashedLine(ctx.r, at + Vec2{side * (standSize_.x * 0.5f + 8.0f), -6.0f}, at + Vec2{side * (standSize_.x * 0.5f + 150.0f), -6.0f},
                               Color{255, 180, 110, static_cast<uint8_t>(80 + 60 * pulse)}, 2.0f * s, 10.0f, 8.0f);
            } else if (golem_ != GolemStage::Crouch || prog > 0.2f) {
                dashedLine(ctx.r, at + Vec2{-wdt * 0.6f, -1.0f}, at + Vec2{wdt * 0.6f, -1.0f},
                           Color{255, 150, 90, static_cast<uint8_t>(80 + 80 * k)}, 2.0f * s, 8.0f, 6.0f);
            }
            if (golem_ == GolemStage::Fall) {
                // The wind of its fall above it.
                for (float dx : {-0.35f, 0.0f, 0.35f})
                    ctx.r.drawWorldLine(c + Vec2{dx * box.w, -box.h * 0.5f}, c + Vec2{dx * box.w, -box.h * 0.5f - 60.0f},
                                        Color{230, 220, 200, 90}, 2.0f * s, BlendMode::Additive);
            }
            break;
        }
        default: break;
    }
}

}  // namespace cosmic
