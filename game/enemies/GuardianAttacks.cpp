// The attacks of the four guardians of Aethra (2.19): Mycélia (racine traqueuse, cage de
// ronces, inspiration), Aurorax (reflets, lac gelé, prisme), Pyrogyre (couronne de feu,
// spirale solaire, marée de lave) and Nyxara (puits gravitationnel, pas de l'ombre, onde
// d'éclipse). Like every attack they are announced during the wind-up (telegraph) before
// they can hurt, and they are deterministic: positions come from the simulation, the only
// randomness is the world's gameplay generator. Lasting effects on the arena (pulls, frozen
// ground, lava, walls of thorns) live in the world's BossHazards.

#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BeamRenderer.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {
// Defaults of the parameters left at 0 (an attack made in the enemy editor works at once).
float orDefault(float v, float d) { return v > 0.0f ? v : d; }

/// std::clamp, with the middle when the range is empty (a narrow arena).
float clampIn(float v, float lo, float hi) { return lo <= hi ? std::clamp(v, lo, hi) : (lo + hi) * 0.5f; }

constexpr float kTrackerWarn = 0.35f;   // the bump shakes before the spike bursts out
constexpr float kTrackerBurst = 0.25f;  // the spike stays out
constexpr float kRingThickness = 12.0f;

float travelOf(const AttackDef& a) { return orDefault(a.duration, 2.5f); }

float distToSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float len2 = ab.lengthSq();
    float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    return distance(p, a + ab * t);
}

float angleDiff(float a, float b) {
    float d = a - b;
    while (d > kPi) d -= 2.0f * kPi;
    while (d < -kPi) d += 2.0f * kPi;
    return d;
}

void dashedLine(Renderer& r, Vec2 a, Vec2 b, Color c, float thicknessPx, float dash = 10.0f, float gap = 7.0f) {
    Vec2 d = b - a;
    float len = d.length();
    if (len < 0.5f) return;
    Vec2 u = d / len;
    for (float s = 0.0f; s < len; s += dash + gap) r.drawWorldLine(a + u * s, a + u * std::min(len, s + dash), c, thicknessPx, BlendMode::Additive);
}

const AnimationClip* fxClip(const AnimationSetPtr& fx, const char* name) { return fx && fx->texture ? fx->clip(name) : nullptr; }

void drawClip(RenderContext& ctx, const AnimationSetPtr& fx, const AnimationClip* c, Vec2 center, float size, Color tint = Color::white(),
              float angleDeg = 0.0f) {
    int frame = static_cast<int>(ctx.time * 12.0f) % std::max(1, c->frameCount());
    Rect src = fx->frameRect(*c, frame);
    float k = size / static_cast<float>(std::max(fx->frameWidth, fx->frameHeight));
    Rect dst = Rect::fromCenter(center, {fx->frameWidth * k, fx->frameHeight * k});
    ctx.r.drawWorldTexture(*fx->texture, &src, dst, tint, BlendMode::Alpha, false, angleDeg);
}
}  // namespace

// ================================================================= helpers

std::optional<float> Enemy::groundAt(const GameWorld& w, float x, float fromY) const {
    Vec2 from{x, fromY - 40.0f}, to{x, fromY + 480.0f};
    if (auto t = w.physics().raycast(from, to, true)) return from.y + (to.y - from.y) * *t;
    return std::nullopt;
}

Rect Enemy::arenaArea(const GameWorld& w) const {
    if (auto a = w.arenaOf(*this)) return *a;
    // No arena (enemy editor, test rooms): the space around the enemy.
    Rect b = w.level().bounds;
    Rect r{box.center().x - 640.0f, box.bottom() - 420.0f, 1280.0f, 440.0f};
    float l = std::max(r.x, b.x), rt = std::min(r.right(), b.right());
    return {l, r.y, rt - l, r.h};
}

float Enemy::guardianFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const {
    const BossHazards& hz = w.hazards();
    switch (a.type) {
        case AttackType::Tracker: return (far ? 1.3f : 1.0f) * (air ? 0.8f : 1.0f);
        case AttackType::Cage: return air ? 0.5f : 1.2f;
        case AttackType::Vortex: return far ? 1.4f : (near ? 0.4f : 1.0f);
        case AttackType::Mirror: return doubles_ > 0 ? 0.0f : 2.2f;  // never two pairs
        case AttackType::FrostFloor: return hz.hasIce(uid_) ? 0.0f : 1.2f;
        case AttackType::Prism: return air ? 1.4f : 1.0f;
        case AttackType::Orbit: return near ? 1.6f : 1.0f;
        case AttackType::Spiral: return 1.0f;
        case AttackType::Flood: return hz.hasFlood(uid_) ? 0.0f : 1.3f;
        case AttackType::Well: return hz.hasPull(uid_) ? 0.0f : (far ? 1.3f : 1.0f);
        case AttackType::Blink: return far ? 1.5f : 0.8f;
        case AttackType::Ring: return 1.0f;
        default: return 1.0f;
    }
}

// ============================================================ the wind-up

void Enemy::guardianBegin(GameWorld& w) {
    const AttackDef& a = *attack_;
    path_.clear();
    path2_.clear();
    burst_ = false;
    ringRadius_ = 0.0f;
    const Rect arena = arenaArea(w);
    switch (a.type) {
        case AttackType::Tracker:
            // The root sets off from the guardian's feet, toward the target.
            markPos_ = {box.center().x + facing * (box.w * 0.5f + 8.0f), box.bottom()};
            break;
        case AttackType::Cage: {
            // Two walls on both sides of the target, shown on the ground before they rise.
            float half = orDefault(a.radius, 130.0f);
            float cx = clampIn(playerPos_.x, arena.x + half + 12.0f, arena.right() - half - 12.0f);
            float base = groundAt(w, cx, playerPos_.y).value_or(playerPos_.y + 11.0f);
            markPos_ = {cx, base};
            for (float side : {-1.0f, 1.0f}) {
                float x = cx + side * half;
                float g = groundAt(w, x, playerPos_.y).value_or(base);
                if (std::abs(g - base) > 64.0f) g = base;
                path_.push_back({x, g});
            }
            break;
        }
        case AttackType::Mirror: {
            // Where the two doubles will stand: on the ground on both sides of the guardian.
            for (float side : {-1.0f, 1.0f}) {
                Vec2 spot = box.bottomCenter();
                for (float d : {170.0f, 120.0f, 70.0f}) {
                    float x = clampIn(box.center().x + side * d, arena.x + box.w, arena.right() - box.w);
                    if (def_.flying) {
                        spot = {x, box.bottom()};
                        break;
                    }
                    if (auto g = groundAt(w, x, box.bottom()); g && std::abs(*g - box.bottom()) < 48.0f) {
                        spot = {x, *g};
                        break;
                    }
                }
                path_.push_back(spot);
            }
            break;
        }
        case AttackType::Prism: {
            // Several upward diagonals are traced (bounces off walls and ceiling); the one
            // passing closest to where the target will be is kept and shown.
            const int bounces = a.bounces > 0 ? a.bounces : 2;
            const float segLen = std::max(200.0f, a.laserLength);
            const Vec2 origin = attackOrigin();
            const Vec2 aim = predictPlayer(w, std::max(0.3f, a.telegraph));
            MoveOptions solidOnly;
            auto solidAt = [&](Vec2 p) { return w.physics().overlapsSolid({p.x - 0.25f, p.y - 0.25f, 0.5f, 0.5f}, solidOnly); };
            float best = 1e9f;
            float sideBest[2] = {1e9f, 1e9f};  // 2.33: the best path on each side (two arms)
            std::vector<Vec2> sidePath[2];
            float chosenSide = facing;
            for (float side : {facing, -facing})
                for (float deg : {35.0f, 45.0f, 55.0f, 65.0f}) {
                    Vec2 d{side * dmath::cos(degToRad(deg)), -dmath::sin(degToRad(deg))};
                    std::vector<Vec2> pts{origin};
                    Vec2 p = origin;
                    for (int k = 0; k <= bounces; ++k) {
                        Vec2 end = p + d * segLen;
                        auto t = w.physics().raycast(p, end);
                        if (!t) {
                            pts.push_back(end);
                            break;
                        }
                        Vec2 hit = p + (end - p) * *t;
                        pts.push_back(hit);
                        if (k == bounces) break;
                        Vec2 back = hit - d * 1.5f;
                        bool sx = solidAt(back + Vec2{signf(d.x) * 3.0f, 0.0f});
                        bool sy = solidAt(back + Vec2{0.0f, signf(d.y) * 3.0f});
                        if (!sx && !sy) sx = sy = true;  // a corner: straight back
                        if (sx) d.x = -d.x;
                        if (sy) d.y = -d.y;
                        p = back;
                    }
                    float score = 1e9f;
                    for (size_t k = 1; k < pts.size(); ++k) score = std::min(score, distToSegment(aim, pts[k - 1], pts[k]));
                    if (score < best - 0.01f) {
                        best = score;
                        path_ = pts;
                        chosenSide = side;
                    }
                    int si = side == facing ? 0 : 1;
                    if (score < sideBest[si] - 0.01f) {
                        sideBest[si] = score;
                        sidePath[si] = pts;
                    }
                }
            // Two arms: the second beam goes up the other side, mirrored, and the two cross.
            if (a.arms >= 2) path2_ = sidePath[chosenSide == facing ? 1 : 0];
            break;
        }
        case AttackType::Orbit:
        case AttackType::Spiral:
            spin_ = w.rng().chance(0.5f) ? 1.0f : -1.0f;
            angle_ = dmath::atan2(playerPos_.y - box.center().y, playerPos_.x - box.center().x);
            break;
        case AttackType::Flood: {
            // The tide's line is shown across the arena before it rises.
            float floorY = arena.bottom();
            if (!w.arenaOf(*this)) floorY = groundAt(w, box.center().x, box.bottom()).value_or(box.bottom());
            path_ = {{arena.x, floorY}, {arena.right(), floorY}};
            markPos_ = {arena.center().x, floorY - orDefault(a.height, 64.0f)};
            break;
        }
        case AttackType::Well: {
            // The hole opens between the target and the guardian, at the height of a runner.
            float side = signf(box.center().x - playerPos_.x);
            if (side == 0.0f) side = facing;
            float x = clampIn(playerPos_.x + side * 150.0f, arena.x + 40.0f, arena.right() - 40.0f);
            float g = groundAt(w, x, playerPos_.y).value_or(playerPos_.y + 11.0f);
            if (std::abs(g - (playerPos_.y + 11.0f)) > 80.0f) g = playerPos_.y + 11.0f;
            markPos_ = {x, g - 30.0f};
            break;
        }
        case AttackType::Blink: {
            // A mark behind the target (behind Nova's back; away from the guardian for a decoy).
            float behind = w.decoyActive() ? signf(playerPos_.x - box.center().x) : -w.player().facing;
            if (behind == 0.0f) behind = -facing;
            float py = playerPos_.y;
            markPos_ = {playerPos_.x, groundAt(w, playerPos_.x, py).value_or(py + 11.0f)};
            for (float side : {behind, -behind}) {
                float x = playerPos_.x + side * 40.0f;
                if (x < arena.x + box.w || x > arena.right() - box.w) continue;
                if (w.physics().raycast(playerPos_, {x, py})) continue;  // a wall in between
                auto g = groundAt(w, x, py);
                if (!g || std::abs(*g - (py + 11.0f)) > 40.0f) continue;
                markPos_ = {x, *g};
                break;
            }
            break;
        }
        case AttackType::Ring: {
            // One opening, somewhere in the lower half (reachable from the ground), drawn by the
            // gameplay generator: it changes every time, the same in a replay.
            markPos_ = box.center();
            angle_ = degToRad(w.rng().range(25.0f, 155.0f));
            ringRadius_ = std::max(box.w, box.h) * 0.5f * def_.scale;
            break;
        }
        default: break;
    }
}

// =========================================================== the attack

void Enemy::guardianStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackClock_ = 0.0f;
    laserHitTimer_ = 0.0f;
    burstTimer_ = 0.0f;
    switch (a.type) {
        case AttackType::Tracker:
            stateTimer_ = travelOf(a) + kTrackerWarn + kTrackerBurst;
            w.playSound("sounds/burrow.wav", markPos_, 0.8f);
            break;
        case AttackType::Cage: {
            float h = orDefault(a.height, 136.0f);
            float secs = orDefault(a.duration, 4.0f);
            for (const Vec2& base : path_)
                w.hazards().addWall(w, uid_, {base.x - 8.0f, base.y - h, 16.0f, h}, secs, a.damage, signf(markPos_.x - base.x), a.look);
            stateTimer_ = secs;
            burstTimer_ = 0.35f;
            w.shake(0.35f);
            if (a.sound.empty()) w.playSound("sounds/thorn_wall.wav", markPos_, 0.9f);
            break;
        }
        case AttackType::Vortex: {
            BossHazards::Pull p;
            p.owner = uid_;
            p.center = box.center();
            p.speed = orDefault(a.pull, 80.0f);
            p.range = std::max(160.0f, a.range);
            p.time = orDefault(a.duration, 2.0f);
            w.hazards().addPull(p);
            stateTimer_ = p.time;
            if (a.sound.empty()) w.playSound("sounds/inhale.wav", box.center(), 0.9f);
            break;
        }
        case AttackType::Mirror:
            spawnDoubles(w, a);
            if (a.sound.empty()) w.playSound("sounds/mirror_split.wav", box.center(), 0.9f);
            break;
        case AttackType::FrostFloor:
            w.hazards().addIce(w, uid_, arenaArea(w), orDefault(a.grip, 0.15f), orDefault(a.duration, 5.0f));
            w.particles().emit("frost_burst", box.bottomCenter(), 2.0f);
            w.shake(0.2f);
            if (a.sound.empty()) w.playSound("sounds/ice_floor.wav", box.center(), 0.9f);
            break;
        case AttackType::Prism:
            w.shake(0.25f);
            if (a.sound.empty()) w.playSound("sounds/prism.wav", box.center(), 0.9f);
            break;
        case AttackType::Orbit:
            stateTimer_ = orDefault(a.duration, 4.0f);
            if (a.sound.empty()) w.playSound("sounds/orbit_flames.wav", box.center(), 0.9f);
            break;
        case AttackType::Spiral:
            stateTimer_ = orDefault(a.duration, 3.0f);
            if (a.sound.empty()) w.playSound("sounds/spiral.wav", box.center(), 0.9f);
            break;
        case AttackType::Flood: {
            BossHazards::Flood f;
            f.owner = uid_;
            f.left = path_.size() == 2 ? path_[0].x : box.center().x - 600.0f;
            f.right = path_.size() == 2 ? path_[1].x : box.center().x + 600.0f;
            f.floorY = path_.size() == 2 ? path_[0].y : box.bottom();
            f.height = orDefault(a.height, 64.0f);
            f.hold = orDefault(a.duration, 4.0f);
            f.damage = a.damage;
            f.look = a.look;
            w.hazards().addFlood(f);
            w.shake(0.4f);
            if (a.sound.empty()) w.playSound("sounds/lava_rise.wav", box.center(), 1.0f);
            break;
        }
        case AttackType::Well: {
            BossHazards::Pull p;
            p.owner = uid_;
            p.center = markPos_;
            p.speed = orDefault(a.pull, 85.0f);
            p.range = std::max(160.0f, a.range);
            p.core = orDefault(a.radius, 22.0f);
            p.damage = a.damage;
            p.time = orDefault(a.duration, 4.0f);
            p.bendShots = true;
            w.hazards().addPull(p);
            stateTimer_ = p.time;
            if (a.sound.empty()) w.playSound("sounds/gravity_well.wav", markPos_, 1.0f);
            break;
        }
        case AttackType::Blink: {
            // Out of the shadows onto the mark (never inside a wall: higher, or stays put).
            Vec2 from = box.center();
            float lift = def_.flying ? 8.0f : 0.0f;
            MoveOptions opt;
            opt.isEnemy = true;
            for (int k = 0; k < 6; ++k) {
                Rect cand = Rect::fromBottomCenter({markPos_.x, markPos_.y - lift - 8.0f * k}, box.size());
                if (w.physics().overlapsSolid(cand, opt) || !w.level().bounds.containsRect(cand)) continue;
                box = cand;
                prevBox = box;
                vel = {};
                break;
            }
            face(playerPos_.x - box.center().x);
            w.particles().emit("teleport_out_enemy", from, 1.2f);
            w.particles().emit("teleport_in_enemy", box.center(), 1.4f);
            w.shake(0.3f);
            if (a.sound.empty()) w.playSound("sounds/shadow_step.wav", box.center(), 1.0f);
            break;
        }
        case AttackType::Ring:
            markPos_ = box.center();
            stateTimer_ = (orDefault(a.radius, 560.0f) - ringRadius_) / std::max(40.0f, a.projectileSpeed);
            if (a.sound.empty()) w.playSound("sounds/eclipse_ring.wav", box.center(), 1.0f);
            break;
        default: break;
    }
}

void Enemy::fireSpiral(GameWorld& w, const AttackDef& a) {
    int arms = a.arms > 0 ? a.arms : 2;
    for (int k = 0; k < arms; ++k) {
        float ang = angle_ + static_cast<float>(k) * 2.0f * kPi / static_cast<float>(arms);
        Projectile p;
        p.pos = p.prevPos = box.center() + Vec2::fromAngle(ang) * (std::max(box.w, box.h) * 0.4f);
        p.vel = Vec2::fromAngle(ang) * a.projectileSpeed;
        p.damage = a.damage;
        p.type = DamageType::Projectile;
        p.clip = a.projectile;
        p.radius = a.projectileRadius > 0 ? a.projectileRadius : 4.5f;
        p.life = 4.0f;
        p.light = def_.lightColor;
        p.knockback = a.knockback;
        p.hitEffect = a.hitEffect.empty() ? "projectile_burst" : a.hitEffect;
        w.spawnProjectile(p);
    }
    angle_ += spin_ * degToRad(a.spread > 0.0f ? a.spread : 13.0f);
}

void Enemy::guardianActive(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    Player& p = w.player();
    if (!def_.flying) vel.x = approach(vel.x, 0.0f, 900.0f * dt);
    DamageInfo d;
    d.amount = a.damage;
    d.type = DamageType::Projectile;
    d.source = box.center();
    d.knockback = a.knockback;
    d.slow = a.slow;
    d.slowDuration = a.slowDuration;
    const float t = attackClock_;
    switch (a.type) {
        case AttackType::Tracker: {
            const float travel = travelOf(a);
            if (t < travel) {
                // Under the ground toward the target, slower than Nova runs: she can outrun it.
                float speed = std::min(a.projectileSpeed, 130.0f);
                float nx = approach(markPos_.x, playerPos_.x, speed * dt);
                if (auto g = groundAt(w, nx, markPos_.y); g && std::abs(*g - markPos_.y) < 40.0f) markPos_ = {nx, *g};
                hopTimer_ -= dt;
                if (hopTimer_ <= 0.0f) {
                    hopTimer_ = 0.09f;
                    w.particles().emit("burrow_dirt", markPos_, 0.5f);
                }
            } else if (t >= travel + kTrackerWarn && !burst_) {
                burst_ = true;
                w.particles().emit(a.hitEffect.empty() ? "eruption_spore" : a.hitEffect, markPos_, 1.4f);
                w.shake(0.35f);
                w.playSound("sounds/root_burst.wav", markPos_, 1.0f);
            }
            if (burst_ && !attackHit_) {
                float half = orDefault(a.radius, 16.0f), h = orDefault(a.height, 44.0f);
                Rect spike{markPos_.x - half, markPos_.y - h, half * 2.0f, h};
                if (spike.intersects(p.box)) {
                    d.source = {markPos_.x, markPos_.y + 10.0f};
                    attackHit_ = w.damagePlayer(d) || true;
                }
            }
            break;
        }
        case AttackType::Cage: {
            // Spores drift down between the walls.
            burstTimer_ -= dt;
            if (burstTimer_ <= 0.0f && path_.size() == 2) {
                burstTimer_ = orDefault(a.burstInterval, 0.45f);
                float h = orDefault(a.height, 136.0f);
                Projectile s;
                float x = w.rng().range(path_[0].x + 14.0f, path_[1].x - 14.0f);
                s.pos = s.prevPos = {x, std::min(path_[0].y, path_[1].y) - h + 10.0f};
                s.vel = {w.rng().range(-14.0f, 14.0f), std::min(a.projectileSpeed, 140.0f)};
                s.gravity = 60.0f;
                s.damage = a.damage;
                s.type = DamageType::Projectile;
                s.clip = a.projectile;
                s.radius = a.projectileRadius > 0 ? a.projectileRadius : 5.0f;
                s.life = 3.5f;
                s.light = {200, 255, 150, 255};
                s.knockback = 120.0f;
                s.hitEffect = "acid_burst";
                w.spawnProjectile(s);
            }
            break;
        }
        case AttackType::Vortex:
            // Its mouth hurts: the pull ends in its contact.
            if (!attackHit_ && box.expanded(4.0f).intersects(p.box)) {
                d.type = DamageType::Contact;
                attackHit_ = w.damagePlayer(d) || true;
            }
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                hopTimer_ = 0.12f;
                w.particles().emit("spores", box.center() + Vec2{facing * 60.0f, -10.0f}, 0.5f);
            }
            break;
        case AttackType::Prism:
            for (const std::vector<Vec2>* beam : {&path_, &path2_}) {
                if (laserHitTimer_ > 0.0f) break;
                for (size_t k = 1; k < beam->size(); ++k)
                    if (Collision::segmentIntersectsRect((*beam)[k - 1], (*beam)[k], p.box)) {
                        if (w.damagePlayer(d)) laserHitTimer_ = 0.45f;
                        break;
                    }
            }
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                hopTimer_ = 0.1f;
                for (const std::vector<Vec2>* beam : {&path_, &path2_})
                    for (size_t k = 1; k + 1 < beam->size(); ++k) w.particles().emit("laser_sparks", (*beam)[k], 0.4f);
            }
            break;
        case AttackType::Orbit: {
            const float dur = std::max(0.1f, orDefault(a.duration, 4.0f));
            const float rMin = std::max(box.w, box.h) * 0.5f * def_.scale + 14.0f;
            const float rMax = std::max(rMin + 20.0f, orDefault(a.radius, 150.0f));
            const float r = rMin + (rMax - rMin) * dmath::sin(kPi * std::clamp(t / dur, 0.0f, 1.0f));
            const float omega = degToRad(a.spread > 0.0f ? a.spread : 85.0f);
            const int n = std::max(1, a.count);
            const float fr = a.projectileRadius > 0 ? a.projectileRadius : 8.0f;
            // The crown follows its target slowly: the gaps, not the distance, are the way out.
            if (def_.flying) moveTowards(w, playerPos_ + Vec2{0.0f, -40.0f}, 40.0f, dt);
            if (laserHitTimer_ <= 0.0f)
                for (int k = 0; k < n; ++k) {
                    float ang = angle_ + spin_ * omega * t + static_cast<float>(k) * 2.0f * kPi / static_cast<float>(n);
                    Vec2 fp = box.center() + Vec2{dmath::cos(ang), dmath::sin(ang)} * r;
                    if (Collision::circleIntersectsRect(fp, fr, p.box)) {
                        d.source = fp;
                        if (w.damagePlayer(d)) laserHitTimer_ = 0.5f;
                        break;
                    }
                }
            if (a.arms > 0) {  // Supernova: the spiral fires through the crown
                burstTimer_ -= dt;
                if (burstTimer_ <= 0.0f) {
                    burstTimer_ = orDefault(a.burstInterval, 0.2f);
                    fireSpiral(w, a);
                }
            }
            break;
        }
        case AttackType::Spiral:
            burstTimer_ -= dt;
            while (burstTimer_ <= 0.0f) {
                burstTimer_ += orDefault(a.burstInterval, 0.12f);
                fireSpiral(w, a);
            }
            break;
        case AttackType::Blink:
            if (!attackHit_ && t <= 0.18f && Collision::circleIntersectsRect(box.center(), orDefault(a.radius, 46.0f), p.box)) {
                d.type = DamageType::Contact;
                attackHit_ = w.damagePlayer(d) || true;
            }
            break;
        case AttackType::Ring: {
            ringRadius_ += std::max(40.0f, a.projectileSpeed) * dt;
            if (!attackHit_) {
                const float gapHalf = degToRad(orDefault(a.spread, 50.0f)) * 0.5f;
                const Rect& b = p.box;
                for (Vec2 s : {b.center(), b.pos(), Vec2{b.right(), b.y}, Vec2{b.x, b.bottom()}, Vec2{b.right(), b.bottom()}}) {
                    float dist = distance(s, markPos_);
                    if (std::abs(dist - ringRadius_) > kRingThickness * 0.5f) continue;
                    float ang = dmath::atan2(s.y - markPos_.y, s.x - markPos_.x);
                    if (std::abs(angleDiff(ang, angle_)) <= gapHalf) continue;  // through the opening
                    d.source = markPos_;
                    attackHit_ = w.damagePlayer(d) || true;
                    break;
                }
            }
            break;
        }
        default: break;
    }
}

void Enemy::guardianEnd(GameWorld& w) {
    // What the guardian channels stops with it: the pull of its breath or of its well, the
    // walls of its cage. The ice and the lava keep their own time.
    if (!attack_) return;
    switch (attack_->type) {
        case AttackType::Vortex:
        case AttackType::Well:
        case AttackType::Cage: w.hazards().endChannel(w, uid_); break;
        default: break;
    }
}

// ============================================================== reflections

void Enemy::spawnDoubles(GameWorld& w, const AttackDef& a) {
    // Copies of the guardian: one hit breaks one, they strike with it (weaker), never
    // multiply and leave nothing behind. Only the true one has health to lose.
    EnemyDefinition dd = def_;
    dd.name = "Reflet : " + def_.name;
    dd.animation = phaseAnimation();  // 2.33: the look of its current phase
    dd.isBoss = false;
    dd.phases.clear();
    dd.hp = 1.0f;
    dd.drops.clear();
    dd.scoreValue = 0;
    dd.contactDamage = std::min(1.0f, def_.contactDamage);
    dd.knockbackResist = 1.0f;
    dd.tint = Color{170, 225, 255, 140};
    dd.lightRadius = def_.lightRadius * 0.5f;
    dd.deathSpawnCount = 0;
    for (auto& m : dd.damageMultipliers) m = std::max(1.0f, m);
    dd.attacks.clear();
    for (const AttackDef& x : def_.attacks) {
        if (x.type == AttackType::Mirror || x.type == AttackType::FrostFloor || x.type == AttackType::Summon) continue;
        AttackDef c = x;
        c.damage = std::max(1.0f, std::floor(x.damage * 0.5f));
        c.sound.clear();
        dd.attacks.push_back(c);
    }
    for (const Vec2& spot : path_) {
        Vec2 feet = def_.flying ? Vec2{spot.x, box.bottom()} : spot;
        if (Enemy* e = w.spawnEnemy(dd, feet, spot.x > box.center().x)) {
            e->summonedBy = uid_;
            e->countsForStats = false;
            e->makeDouble(uid_, a.duration);
            e->alert(w);
            w.particles().emit("frost_burst", e->center(), 1.5f);
        }
    }
    doubles_ = static_cast<int>(path_.size());
}

void Enemy::copyToDoubles(GameWorld& w) {
    for (const auto& e : w.enemies()) {
        if (e->mirrorOf_ != uid_ || !e->alive()) continue;
        EnemyState s = e->state();
        if (s != EnemyState::Chase && s != EnemyState::Alert && s != EnemyState::Recover && s != EnemyState::Idle &&
            s != EnemyState::Patrol)
            continue;
        if (e->def().attack(attack_->id)) e->forceAttack(w, attack_->id);
    }
}

// ================================================================== render

void Enemy::renderGuardian(RenderContext& ctx, float prog) const {
    const AttackDef& a = *attack_;
    const bool tele = state_ == EnemyState::Telegraph;
    const float s = ctx.r.view().scale / 3.0f;  // screen thickness of world strokes
    const Color warn{255, 77, 109, static_cast<uint8_t>(80 + 140 * prog)};
    auto fx = ctx.res.animation("animations/projectiles.json");
    const float t = attackClock_;
    switch (a.type) {
        case AttackType::Tracker: {
            if (tele) {
                ctx.r.drawWorldGlow(markPos_, 10.0f + 14.0f * prog, Color{180, 120, 60, static_cast<uint8_t>(80 + 100 * prog)});
                break;
            }
            const float travel = travelOf(a);
            if (!burst_) {
                // The bump in the ground; it shakes and glows red just before bursting.
                bool warning = t >= travel;
                float shake = warning ? dmath::sin(ctx.time * 60.0f) * 1.5f : 0.0f;
                Vec2 c = markPos_ + Vec2{shake, 0.0f};
                ctx.r.fillWorldCircle(c + Vec2{0, 1.0f}, 9.0f, Color{92, 60, 38, 255});
                ctx.r.fillWorldCircle(c + Vec2{0, -1.0f}, 5.0f, Color{140, 95, 60, 255});
                if (warning) ctx.r.drawWorldGlow(c, 26.0f, warn);
            } else if (t < travel + kTrackerWarn + kTrackerBurst) {
                float half = orDefault(a.radius, 16.0f), h = orDefault(a.height, 44.0f);
                float k = std::min(1.0f, (t - travel - kTrackerWarn) / 0.08f);
                auto rootFx = ctx.res.animation("animations/root_spike.json");  // art 2.19: its own sheet
                if (const AnimationClip* c = fxClip(rootFx, "erupt")) {
                    drawClip(ctx, rootFx, c, markPos_ + Vec2{0, -h * k * 0.5f}, h * k);
                } else {
                    // A root spike: stacked slabs narrowing to a point, a violet glow at its tip.
                    const int n = 8;
                    for (int i = 0; i < n; ++i) {
                        float y0 = markPos_.y - h * k * static_cast<float>(i + 1) / n;
                        float hw = half * (1.0f - static_cast<float>(i) / n);
                        ctx.r.fillWorldRect({markPos_.x - hw, y0, hw * 2.0f, h * k / n + 0.5f}, Color{120, 80, 48, 255});
                    }
                    ctx.r.drawWorldGlow(markPos_ + Vec2{0, -h * k}, 10.0f, Color{176, 140, 255, 200});
                }
                ctx.lights.add(markPos_ + Vec2{0, -h * 0.5f}, 60.0f, Color{180, 140, 255, 255});
            }
            break;
        }
        case AttackType::Cage:
            if (tele) {
                float h = orDefault(a.height, 136.0f);
                for (const Vec2& b : path_) {
                    ctx.r.fillWorldRect({b.x - 8.0f, b.y - 3.0f, 16.0f, 3.0f}, warn, BlendMode::Additive);
                    dashedLine(ctx.r, b, b + Vec2{0, -h * prog}, warn, 2.0f * s);
                    ctx.r.drawWorldGlow(b, 18.0f, warn.withAlpha(static_cast<uint8_t>(50 + 80 * prog)));
                }
            }
            break;
        case AttackType::Vortex:
            if (tele) {
                for (int k = 0; k < 6; ++k) {
                    float r = 120.0f * (1.0f - std::fmod(ctx.time * 1.5f + k / 6.0f, 1.0f)) + 20.0f;
                    ctx.r.drawWorldCircle(box.center(), r, Color{200, 160, 255, static_cast<uint8_t>(40 + 50 * prog)}, 1.5f * s);
                }
            }
            break;
        case AttackType::Mirror:
            if (tele) {
                for (const Vec2& p : path_) {
                    ctx.r.drawWorldGlow(p + Vec2{0, -box.h * 0.5f}, box.h * 0.8f, Color{160, 230, 255, static_cast<uint8_t>(60 + 120 * prog)});
                    dashedLine(ctx.r, box.center(), p + Vec2{0, -box.h * 0.5f}, Color{190, 240, 255, static_cast<uint8_t>(60 + 100 * prog)}, 1.5f * s, 6.0f, 6.0f);
                }
            }
            break;
        case AttackType::FrostFloor:
            if (tele) {
                // Frost creeps out from its feet across the floor.
                float reach = 40.0f + 520.0f * prog;
                ctx.r.fillWorldRect({box.center().x - reach, box.bottom() - 2.0f, reach * 2.0f, 4.0f},
                                    Color{190, 240, 255, static_cast<uint8_t>(60 + 120 * prog)}, BlendMode::Additive);
            }
            break;
        case AttackType::Prism: {
            if (path_.size() < 2) break;
            if (tele) {
                for (const std::vector<Vec2>* beam : {&path_, &path2_})
                    for (size_t k = 1; k < beam->size(); ++k)
                        dashedLine(ctx.r, (*beam)[k - 1], (*beam)[k], Color{220, 250, 255, static_cast<uint8_t>(110 + 140 * prog)}, 3.0f * s,
                                   12.0f, 7.0f);
                break;
            }
            Color bc = a.beamColor;
            std::vector<std::pair<Vec2, Vec2>> segs;
            for (const std::vector<Vec2>* beam : {&path_, &path2_})
                for (size_t k = 1; k < beam->size(); ++k) segs.push_back({(*beam)[k - 1], (*beam)[k]});
            for (const auto& [p0, p1] : segs) {
                Vec2 dd = p1 - p0;
                float len = dd.length();
                float ang = dmath::atan2(dd.y, dd.x);
                if (!BeamRenderer::draw(ctx.r, ctx.res, a.beamTexture, p0, ang, len, a.beamWidth, ctx.time)) {
                    ctx.r.drawWorldLine(p0, p1, bc.withAlpha(220), 10.0f * s, BlendMode::Additive);
                    ctx.r.drawWorldLine(p0, p1, {245, 255, 255, 255}, 3.5f * s, BlendMode::Additive);
                }
                ctx.r.drawWorldGlow(p1, 16.0f, bc.withAlpha(160));
                ctx.lights.add((p0 + p1) * 0.5f, std::min(260.0f, len * 0.5f), bc);
            }
            break;
        }
        case AttackType::Orbit: {
            const float dur = std::max(0.1f, orDefault(a.duration, 4.0f));
            const float rMin = std::max(box.w, box.h) * 0.5f * def_.scale + 14.0f;
            const float rMax = std::max(rMin + 20.0f, orDefault(a.radius, 150.0f));
            const float r = tele ? rMin : rMin + (rMax - rMin) * dmath::sin(kPi * std::clamp(t / dur, 0.0f, 1.0f));
            const float omega = degToRad(a.spread > 0.0f ? a.spread : 85.0f);
            const int n = std::max(1, a.count);
            const float fr = a.projectileRadius > 0 ? a.projectileRadius : 8.0f;
            const AnimationClip* flame = fxClip(fx, a.projectile.c_str());
            if (!tele) ctx.r.drawWorldCircle(box.center(), r, Color{255, 177, 59, 60}, 1.0f * s);
            for (int k = 0; k < n; ++k) {
                float ang = angle_ + (tele ? 0.0f : spin_ * omega * t) + static_cast<float>(k) * 2.0f * kPi / static_cast<float>(n);
                Vec2 fp = box.center() + Vec2{dmath::cos(ang), dmath::sin(ang)} * r;
                uint8_t al = static_cast<uint8_t>(tele ? 80 + 150 * prog : 255);
                if (flame) drawClip(ctx, fx, flame, fp, fr * 3.0f, Color{255, 255, 255, al});
                else {
                    ctx.r.fillWorldCircle(fp, fr, Color{255, 122, 42, al});
                    ctx.r.fillWorldCircle(fp, fr * 0.5f, Color{255, 230, 150, al});
                }
                ctx.r.drawWorldGlow(fp, fr * 2.5f, Color{255, 150, 60, static_cast<uint8_t>(al / 2)});
                if (!tele) ctx.lights.add(fp, 40.0f, Color{255, 150, 60, 255});
            }
            break;
        }
        case AttackType::Spiral:
            if (tele) {
                int arms = a.arms > 0 ? a.arms : 2;
                for (int k = 0; k < arms; ++k) {
                    Vec2 dir = Vec2::fromAngle(angle_ + static_cast<float>(k) * 2.0f * kPi / static_cast<float>(arms));
                    dashedLine(ctx.r, box.center(), box.center() + dir * (40.0f + 40.0f * prog), warn, 2.0f * s, 6.0f, 5.0f);
                }
            }
            break;
        case AttackType::Flood:
            if (tele && path_.size() == 2) {
                // The line the lava will reach, and the floor already bubbling.
                float y = markPos_.y;
                dashedLine(ctx.r, {path_[0].x, y}, {path_[1].x, y}, Color{255, 140, 60, static_cast<uint8_t>(90 + 150 * prog)}, 2.5f * s, 14.0f, 9.0f);
                for (float x = path_[0].x + 30.0f; x < path_[1].x; x += 90.0f) {
                    float bob = dmath::sin(ctx.time * 6.0f + x) * 3.0f;
                    ctx.r.drawWorldLine({x, path_[0].y - 6.0f + bob}, {x, path_[0].y - 6.0f - 10.0f * prog + bob},
                                        Color{255, 180, 80, static_cast<uint8_t>(80 + 140 * prog)}, 2.0f * s, BlendMode::Additive);
                }
                ctx.r.fillWorldRect({path_[0].x, path_[0].y - 4.0f, path_[1].x - path_[0].x, 4.0f},
                                    Color{255, 90, 30, static_cast<uint8_t>(60 + 120 * prog)}, BlendMode::Additive);
            }
            break;
        case AttackType::Well:
            if (tele) {
                float r = orDefault(a.radius, 22.0f) * prog;
                ctx.r.drawWorldGlow(markPos_, 30.0f + 40.0f * prog, Color{140, 80, 220, static_cast<uint8_t>(60 + 120 * prog)});
                ctx.r.fillWorldCircle(markPos_, std::max(2.0f, r), Color{6, 0, 14, static_cast<uint8_t>(120 + 120 * prog)});
                ctx.r.drawWorldCircle(markPos_, orDefault(a.radius, 22.0f) + 3.0f, Color{255, 118, 150, static_cast<uint8_t>(80 + 140 * prog)}, 1.5f * s);
            }
            break;
        case AttackType::Blink:
            if (tele) {
                Vec2 c = markPos_ + Vec2{0, -14.0f};
                auto markFx = ctx.res.animation("animations/shadow_mark.json");  // art 2.19: its own sheet
                if (const AnimationClip* m = fxClip(markFx, "idle")) drawClip(ctx, markFx, m, c, 36.0f, Color{255, 255, 255, static_cast<uint8_t>(120 + 135 * prog)});
                else {
                    ctx.r.drawWorldCircle(c, 16.0f, Color{255, 118, 150, static_cast<uint8_t>(110 + 140 * prog)}, 2.0f * s);
                    ctx.r.drawWorldLine(c + Vec2{-8, -8}, c + Vec2{8, 8}, Color{255, 118, 150, 230}, 2.0f * s);
                    ctx.r.drawWorldLine(c + Vec2{8, -8}, c + Vec2{-8, 8}, Color{255, 118, 150, 230}, 2.0f * s);
                }
                ctx.r.drawWorldGlow(c, 24.0f + 10.0f * prog, Color{255, 118, 150, static_cast<uint8_t>(60 + 100 * prog)});
            } else if (t < 0.25f) {
                ctx.r.drawWorldCircle(box.center(), orDefault(a.radius, 46.0f), Color{255, 118, 150, static_cast<uint8_t>(220 * (1.0f - t / 0.25f))}, 3.0f * s);
            }
            break;
        case AttackType::Ring: {
            const float gapHalf = degToRad(orDefault(a.spread, 50.0f)) * 0.5f;
            Vec2 c = tele ? box.center() : markPos_;
            float r = tele ? ringRadius_ + 6.0f * prog : ringRadius_;
            if (tele) {
                // Where the opening will be.
                Vec2 dir = Vec2::fromAngle(angle_);
                dashedLine(ctx.r, c + dir * r, c + dir * (r + 40.0f + 30.0f * prog), Color{180, 255, 200, static_cast<uint8_t>(100 + 150 * prog)}, 2.5f * s, 6.0f, 4.0f);
            }
            const int steps = 72;
            float start = angle_ + gapHalf, span = 2.0f * kPi - 2.0f * gapHalf;
            // A dark band in a violet glow, bright rims; the opening's lips glow green.
            const float thick = kRingThickness * ctx.r.view().scale * (tele ? 0.4f : 1.0f);
            Color glow = tele ? Color{150, 90, 230, static_cast<uint8_t>(40 + 80 * prog)} : Color{170, 100, 255, 170};
            Color dark = tele ? Color{60, 24, 100, static_cast<uint8_t>(60 + 100 * prog)} : Color{34, 8, 58, 240};
            Color edge = tele ? Color{200, 160, 255, static_cast<uint8_t>(60 + 120 * prog)} : Color{235, 205, 255, 255};
            const float rim = kRingThickness * 0.5f;
            for (int k = 0; k < steps; ++k) {
                float a0 = start + span * static_cast<float>(k) / steps, a1 = start + span * static_cast<float>(k + 1) / steps;
                Vec2 d0 = Vec2::fromAngle(a0), d1 = Vec2::fromAngle(a1);
                Vec2 p0 = c + d0 * r, p1 = c + d1 * r;
                ctx.r.drawWorldLine(p0, p1, glow, thick * 2.0f, BlendMode::Additive);
                ctx.r.drawWorldLine(p0, p1, dark, thick);
                if (!tele) {
                    ctx.r.drawWorldLine(c + d0 * (r - rim), c + d1 * (r - rim), edge, 1.5f * s, BlendMode::Additive);
                    ctx.r.drawWorldLine(c + d0 * (r + rim), c + d1 * (r + rim), edge, 1.5f * s, BlendMode::Additive);
                }
            }
            for (float lip : {angle_ - gapHalf, angle_ + gapHalf})
                ctx.r.drawWorldGlow(c + Vec2::fromAngle(lip) * r, kRingThickness * (tele ? 0.8f : 1.4f),
                                    Color{150, 255, 190, static_cast<uint8_t>(tele ? 60 + 120 * prog : 220)});
            if (!tele) ctx.lights.add(c, r, Color{150, 100, 230, 200});
            break;
        }
        default: break;
    }
}

}  // namespace cosmic
