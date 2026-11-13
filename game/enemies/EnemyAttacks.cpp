// Enemy attacks (telegraph -> active -> recover) and rendering.

#include <cmath>

#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "engine/core/DetMath.h"

namespace cosmic {

namespace {
constexpr float kLeapGravity = 900.0f;  // same as the enemies' gravity (Enemy.cpp)
}  // namespace

std::vector<const AttackDef*> Enemy::availableAttacks() const {
    std::vector<const AttackDef*> out;
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size()) && !def_.phases[phase_].attacks.empty()) {
        for (const auto& id : def_.phases[phase_].attacks)
            if (const AttackDef* a = def_.attack(id)) out.push_back(a);
        return out;
    }
    for (const auto& a : def_.attacks) out.push_back(&a);
    return out;
}

bool Enemy::chooseAttack(GameWorld& w) {
    auto list = availableAttacks();
    if (def_.isBoss) return chooseBossAttack(w, list);
    std::vector<const AttackDef*> candidates;
    float total = 0.0f;
    for (const AttackDef* a : list) {
        if (a->type == AttackType::Contact) continue;
        size_t idx = static_cast<size_t>(a - def_.attacks.data());
        if (idx >= attackCooldowns_.size() || attackCooldowns_[idx] > 0.0f) continue;
        if (playerDist_ > a->range || playerDist_ < a->minRange) continue;
        if (a->type == AttackType::Summon) {
            int alive = 0;
            for (const auto& e : w.enemies())
                if (e->summonedBy == uid_ && e->alive()) ++alive;
            if (alive >= a->maxSummoned) continue;
        }
        candidates.push_back(a);
        total += a->weight;
    }
    if (candidates.empty()) return false;
    const AttackDef* chosen = candidates.front();
    float r = w.rng().range(0.0f, total);
    for (const AttackDef* a : candidates) {
        r -= a->weight;
        if (r <= 0.0f) {
            chosen = a;
            break;
        }
    }
    attack_ = chosen;
    attackIndex_ = static_cast<int>(chosen - def_.attacks.data());
    beginAttack(w);
    return true;
}

// Bosses read the situation instead of cycling a fixed pattern: distance, a player in the
// air or perched above, the attacks just used (never the same one twice in a row), a player
// out of reach for too long (punished by the attacks that reach everywhere), combos.
bool Enemy::chooseBossAttack(GameWorld& w, const std::vector<const AttackDef*>& list) {
    struct Cand {
        const AttackDef* a;
        float score;
    };
    std::vector<Cand> cands;
    const bool near = playerDist_ < 120.0f;
    const bool far = playerDist_ > 260.0f;
    const float above = box.bottom() - (playerPos_.y + 12.0f);  // > 0: the player stands higher
    const bool air = playerAirborne_ || above > 48.0f;
    const bool inCombo = comboLeft_ > 0;
    // Right under a perched player: only what reaches over the ledge is worth it; otherwise
    // the boss steps back first (movement code).
    const bool underneath = !def_.flying && above > 48.0f && std::abs(playerPos_.x - box.center().x) < 260.0f;
    bool reachable = false;
    auto reach = [](const AttackDef* a) { return a->type == AttackType::Laser ? std::min(a->range, a->laserLength) : a->range; };
    for (const AttackDef* a : list) {
        if (a->type == AttackType::Contact) continue;
        if (playerDist_ <= reach(a) && playerDist_ >= a->minRange) reachable = true;
    }
    outOfReach_ = reachable ? 0.0f : outOfReach_ + simDt_;
    const bool punish = outOfReach_ > 1.6f;
    int alive = 0;
    for (const auto& e : w.enemies())
        if (e->summonedBy == uid_ && e->alive()) ++alive;
    float total = 0.0f;
    for (const AttackDef* a : list) {
        if (a->type == AttackType::Contact) continue;
        size_t idx = static_cast<size_t>(a - def_.attacks.data());
        if (idx >= attackCooldowns_.size()) continue;
        // In a combo the next blow ignores cooldowns (but never repeats the last ones).
        if (attackCooldowns_[idx] > 0.0f && !(inCombo && static_cast<int>(idx) != lastAttack_ && static_cast<int>(idx) != prevAttack_)) continue;
        bool inRange = playerDist_ <= reach(a) && playerDist_ >= a->minRange;
        if (a->type == AttackType::Summon && alive >= a->maxSummoned) continue;
        if (a->type == AttackType::Leap && def_.flying) continue;
        if (underneath && a->type != AttackType::Leap && a->type != AttackType::Rain && a->type != AttackType::Eruption &&
            a->type != AttackType::Arc && a->type != AttackType::Summon)
            continue;
        float fit = a->weight;
        switch (a->type) {
            case AttackType::Melee: fit *= near ? 2.2f : (far ? 0.2f : 0.8f); break;
            case AttackType::Slam: fit *= (near ? 1.8f : 1.0f) * (air ? 0.35f : 1.0f); break;
            case AttackType::Charge: fit *= (far ? 1.5f : 1.0f) * (above > 48.0f ? 0.1f : (air ? 0.5f : 1.0f)); break;
            case AttackType::Lunge: fit *= (air ? 1.3f : 1.0f) * (far ? 1.2f : 1.0f); break;
            case AttackType::Projectile:
            case AttackType::Spread: fit *= (air ? 1.5f : 1.0f) * (far ? 1.4f : 1.0f); break;
            case AttackType::Arc: fit *= (far ? 1.3f : 1.0f) * (above > 48.0f ? 1.4f : 1.0f); break;
            case AttackType::Laser: fit *= (air ? 1.6f : 1.0f) * (far ? 1.2f : 1.0f); break;
            case AttackType::Rain: fit *= far ? 1.3f : 1.0f; break;
            case AttackType::Eruption: fit *= (air ? 0.7f : 1.0f) * (far ? 1.2f : 1.0f); break;
            case AttackType::Leap: fit *= (far ? 1.8f : 0.6f) * (above > 48.0f ? 2.0f : 1.0f); break;
            case AttackType::Summon: fit *= alive > 0 ? 0.3f : 0.7f; break;
            default: break;
        }
        if (!inRange) {
            if (!punish) continue;
            float k = 0.0f;
            switch (a->type) {
                case AttackType::Leap: k = 3.0f; break;
                case AttackType::Rain: k = 2.5f; break;
                case AttackType::Eruption: k = 2.0f; break;
                case AttackType::Arc: k = 1.8f; break;
                case AttackType::Laser:
                case AttackType::Projectile:
                case AttackType::Spread:
                case AttackType::Lunge: k = 1.5f; break;
                case AttackType::Charge: k = std::abs(above) < 32.0f ? 1.2f : 0.0f; break;
                default: break;
            }
            if (k <= 0.0f) continue;
            fit *= k;
        }
        if (static_cast<int>(idx) == lastAttack_) fit *= 0.1f;
        else if (static_cast<int>(idx) == prevAttack_) fit *= 0.45f;
        fit *= w.rng().range(0.7f, 1.3f);
        cands.push_back({a, fit});
        total += fit;
    }
    if (cands.empty() || total <= 0.0f) {
        comboLeft_ = 0;
        return false;
    }
    const AttackDef* chosen = cands.front().a;
    float r = w.rng().range(0.0f, total);
    for (const Cand& c : cands) {
        r -= c.score;
        if (r <= 0.0f) {
            chosen = c.a;
            break;
        }
    }
    if (inCombo) --comboLeft_;
    else if (phase_ < static_cast<int>(def_.phases.size())) comboLeft_ = std::max(0, def_.phases[phase_].combo - 1);
    outOfReach_ = 0.0f;
    attack_ = chosen;
    attackIndex_ = static_cast<int>(chosen - def_.attacks.data());
    prevAttack_ = lastAttack_;
    lastAttack_ = attackIndex_;
    ++patternIndex_;
    beginAttack(w);
    return true;
}

bool Enemy::forceAttack(GameWorld& w, const std::string& attackId) {
    if (!alive() || dormant()) return false;
    for (size_t i = 0; i < def_.attacks.size(); ++i) {
        if (def_.attacks[i].id != attackId) continue;
        sense(w);
        engaged_ = true;
        attack_ = &def_.attacks[i];
        attackIndex_ = static_cast<int>(i);
        beginAttack(w);
        return true;
    }
    return false;
}

void Enemy::beginAttack(GameWorld& w) {
    state_ = EnemyState::Telegraph;
    stateTimer_ = attack_->telegraph * telegraphScale() / std::max(0.3f, phaseSpeed());
    if (def_.isBoss) stateTimer_ = std::max(0.3f, stateTimer_);  // always readable
    attackStarted_ = false;
    attackHit_ = false;
    attackClock_ = 0.0f;
    burstsLeft_ = 0;
    staged_.clear();
    leapLanded_ = false;
    face(playerPos_.x - box.center().x);
    aimDir_ = (playerPos_ - box.center()).normalized();
    if (aimDir_.lengthSq() < 0.01f) aimDir_ = {facing, 0};
    {
        Vec2 la = (playerPos_ - attackOrigin()).normalized();
        if (la.lengthSq() < 0.01f) la = aimDir_;
        laserAngle_ = dmath::atan2(la.y, la.x) - degToRad(attack_->sweepDegrees * 0.5f) * facing;
    }
    playClip(attack_->telegraphAnim, true);
    if (!attack_->telegraphSound.empty()) w.playSound(attack_->telegraphSound, box.center(), 0.7f);
    // Rain and eruptions announce where they will land.
    targets_.clear();
    if (attack_->type == AttackType::Rain || attack_->type == AttackType::Eruption) {
        int n = std::max(1, attack_->count);
        float spacing = std::max(attack_->spread, 24.0f);
        // Aimed where the player will be when it lands (lead), not where it stands now.
        Vec2 aim = attack_->lead > 0.0f ? predictPlayer(w, (attack_->telegraph + 0.3f) * attack_->lead) : playerPos_;
        float dir = signf(aim.x - box.center().x);
        if (dir == 0.0f) dir = facing;
        for (int i = 0; i < n; ++i) {
            float x;
            if (attack_->stagger > 0.0f) {
                // A line that runs from the boss through the player's landing point and beyond
                // (centred on the player when it is right next to the boss, or above it).
                float start = box.center().x + dir * (box.w * 0.5f + 8.0f);
                float end = aim.x + dir * spacing * 2.0f;
                if (dir * (end - start) < spacing * 3.0f) {
                    start = aim.x - dir * spacing * (n - 1) * 0.5f;
                    end = aim.x + dir * spacing * (n - 1) * 0.5f;
                }
                x = n > 1 ? start + (end - start) * static_cast<float>(i) / static_cast<float>(n - 1) : aim.x;
            } else {
                x = aim.x + (static_cast<float>(i) - (n - 1) * 0.5f) * spacing + w.rng().range(-6.0f, 6.0f);
            }
            Vec2 from{x, aim.y - 40.0f};
            Vec2 to{x, aim.y + 400.0f};
            if (def_.isBoss) {  // bosses may stand above or below the player
                from.y = std::min(aim.y, box.center().y) - 40.0f;
                to.y = std::max(aim.y, box.bottom()) + 400.0f;
            }
            float groundY = aim.y;
            if (auto t = w.physics().raycast(from, to, true)) groundY = from.y + (to.y - from.y) * *t;
            targets_.push_back({x, groundY});
        }
        warnTimer_ = 0.0f;
    }
    if (attack_->type == AttackType::Laser) w.playSound("sounds/laser_charge.wav", box.center(), 0.8f);
}

Vec2 Enemy::attackOrigin() const {
    if (attack_ && attack_->hasOrigin)
        return box.bottomCenter() + Vec2{facing * attack_->origin.x * def_.scale, attack_->origin.y * def_.scale};
    if (attack_ && attack_->type == AttackType::Laser) return box.center() + Vec2{facing * box.w * 0.3f, -box.h * 0.2f};
    return box.center() + Vec2{facing * box.w * 0.4f, -box.h * 0.1f};
}

void Enemy::stepTelegraph(GameWorld& w, float dt) {
    if (!def_.flying) vel.x = approach(vel.x, 0.0f, 900.0f * dt);
    else vel = lerpv(vel, {}, damp(6.0f, dt));
    if (!targets_.empty() && attack_) {
        warnTimer_ -= dt;
        if (warnTimer_ <= 0.0f) {
            warnTimer_ = 0.07f;
            for (const Vec2& t : targets_) w.particles().emit(attack_->warningEffect, t, 1.0f);
        }
    }
    // Ranged attackers keep tracking during the wind-up; melee commits early.
    if (attack_ && (attack_->type == AttackType::Projectile || attack_->type == AttackType::Arc || attack_->type == AttackType::Lunge) &&
        stateTimer_ > 0.15f) {
        Vec2 aim = playerPos_;
        if (attack_->lead > 0.0f) {
            float speed = attack_->type == AttackType::Lunge ? attack_->lungeSpeed : attack_->projectileSpeed;
            aim = predictPlayer(w, distance(box.center(), playerPos_) / std::max(60.0f, speed) * attack_->lead);
        }
        aimDir_ = (aim - box.center()).normalized();
        face(aimDir_.x);
    }
    if (stateTimer_ <= 0.0f && attack_) {
        state_ = EnemyState::Attack;
        stateTimer_ = attack_->active;
        playClip(attack_->attackAnim, true);
        performAttackStart(w);
    }
}

void Enemy::spawnImpact(GameWorld& w, const AttackDef& a, Vec2 t) {
    bool rain = a.type == AttackType::Rain;
    Projectile p;
    if (rain) {
        p.pos = p.prevPos = {t.x + w.rng().range(-4.0f, 4.0f), std::min(t.y, playerPos_.y) - a.rainHeight};
        p.vel = {w.rng().range(-12.0f, 12.0f), a.projectileSpeed};
        p.gravity = a.gravity;
        p.life = 4.0f;
    } else {
        p.pos = p.prevPos = t + Vec2{0, -6.0f};
        p.vel = {w.rng().range(-20.0f, 20.0f), -a.projectileSpeed};
        p.gravity = a.gravity > 0 ? a.gravity : 700.0f;
        p.life = 2.0f;
        w.particles().emit(a.eruptionEffect, t, 1.0f);
    }
    p.damage = a.damage;
    p.type = DamageType::Projectile;
    p.clip = a.projectile;
    p.radius = a.projectileRadius > 0 ? a.projectileRadius : 5.0f;
    p.light = def_.lightColor;
    p.knockback = a.knockback;
    p.hitEffect = a.hitEffect.empty() ? "projectile_burst" : a.hitEffect;
    p.slow = a.slow;
    p.slowDuration = a.slowDuration;
    p.explosionRadius = a.explosionRadius;
    w.spawnProjectile(p);
    if (!rain && a.stagger > 0.0f) w.playSound("sounds/golem_slam.wav", t, 0.35f, 1.4f);
}

void Enemy::fireProjectiles(GameWorld& w, const AttackDef& a) {
    Vec2 origin = attackOrigin();
    Vec2 baseDir = a.type == AttackType::Spread ? Vec2{facing, 0.0f} : aimDir_;
    if (a.type == AttackType::Spread && a.lead > 0.0f) baseDir = aimDir_;  // aimed fans (bosses)
    float baseAngle = dmath::atan2(baseDir.y, baseDir.x);
    Vec2 target = a.lead > 0.0f ? predictPlayer(w, distance(origin, playerPos_) / std::max(60.0f, a.projectileSpeed) * a.lead) : playerPos_;
    for (int i = 0; i < a.count; ++i) {
        float off = a.count > 1 ? (i - (a.count - 1) * 0.5f) * degToRad(a.spread) : 0.0f;
        Projectile p;
        p.pos = p.prevPos = origin;
        if (a.type == AttackType::Arc) {
            // Ballistic lob that lands near the player (or where it is going).
            float g = a.gravity > 0 ? a.gravity : 500.0f;
            float dx = target.x - origin.x + off * 60.0f;
            float tFlight = std::clamp(std::abs(dx) / std::max(40.0f, a.projectileSpeed), 0.5f, 1.4f);
            float dy = target.y - origin.y;
            p.vel = {dx / tFlight, (dy - 0.5f * g * tFlight * tFlight) / tFlight};
            p.gravity = g;
        } else {
            p.vel = Vec2::fromAngle(baseAngle + off) * a.projectileSpeed;
            p.gravity = a.gravity;
        }
        p.damage = a.damage;
        p.type = DamageType::Projectile;
        p.clip = a.projectile;
        p.radius = a.projectileRadius > 0 ? a.projectileRadius : (a.projectile == "boss_orb" ? 7.0f : 4.5f);
        p.life = a.homing > 0.0f ? std::max(4.0f, a.homingTime + 2.5f) : 4.0f;
        p.light = def_.lightColor;
        p.knockback = a.knockback;
        p.hitSound = "sounds/explosion.wav";
        p.hitEffect = a.hitEffect.empty() ? "projectile_burst" : a.hitEffect;
        p.slow = a.slow;
        p.slowDuration = a.slowDuration;
        p.explosionRadius = a.explosionRadius;
        p.homing = a.homing;
        p.homingTime = a.homingTime;
        if (a.explosionRadius > 0.0f) p.hitSound.clear();
        w.spawnProjectile(p);
    }
    w.particles().emit("muzzle_enemy", origin, 1.0f, baseDir);
    if (a.sound.empty()) w.playSound("sounds/enemy_shoot.wav", origin, 0.7f);
}

void Enemy::slam(GameWorld& w, const AttackDef& a) {
    Vec2 base = box.bottomCenter();
    w.shake(def_.isBoss ? 0.6f : 0.4f);
    w.rumble(0.6f, 0.4f, 200);
    w.playSound("sounds/golem_slam.wav", base, 1.0f);
    w.particles().emit("slam_dust", base, def_.isBoss ? 2.0f : 1.2f);
    for (float dir : {-1.0f, 1.0f}) {
        Projectile p;
        p.pos = p.prevPos = base + Vec2{dir * box.w * 0.5f, -6.0f};
        p.vel = {dir * a.projectileSpeed, 0.0f};
        p.damage = a.damage;
        p.type = DamageType::Explosion;
        p.clip = "shockwave";
        p.radius = 7.0f;
        p.life = a.range / std::max(30.0f, a.projectileSpeed);
        p.light = {255, 179, 71, 255};
        p.knockback = a.knockback;
        p.hitEffect = "slam_dust";
        w.spawnProjectile(p);
    }
    // Direct impact area
    DamageInfo d;
    d.amount = a.damage;
    d.type = DamageType::Explosion;
    d.source = base;
    d.knockback = a.knockback;
    if (Rect::fromCenter(base, {box.w + 30, 24}).intersects(w.player().box)) w.damagePlayer(d);
}

void Enemy::performAttackStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackStarted_ = true;
    if (!a.sound.empty()) w.playSound(a.sound, box.center(), 0.9f);
    switch (a.type) {
        case AttackType::Rain:
        case AttackType::Eruption: {
            bool rain = a.type == AttackType::Rain;
            if (a.stagger > 0.0f) {
                // One impact after another: the line chases the player.
                for (size_t k = 0; k < targets_.size(); ++k) staged_.push_back({targets_[k], static_cast<float>(k) * a.stagger});
                stateTimer_ = std::max(stateTimer_, static_cast<float>(targets_.size()) * a.stagger + 0.05f);
            } else {
                for (const Vec2& t : targets_) spawnImpact(w, a, t);
            }
            if (!rain) {
                w.shake(0.25f);
                w.playSound("sounds/golem_slam.wav", box.center(), 0.6f, 1.3f);
            } else if (a.sound.empty()) {
                w.playSound("sounds/enemy_shoot.wav", box.center(), 0.6f, 0.8f);
            }
            targets_.clear();
            break;
        }
        case AttackType::Projectile:
        case AttackType::Spread:
        case AttackType::Arc:
            fireProjectiles(w, a);
            if (a.bursts > 1) {
                burstsLeft_ = a.bursts - 1;
                burstTimer_ = a.burstInterval;
                stateTimer_ = std::max(stateTimer_, static_cast<float>(a.bursts - 1) * a.burstInterval + 0.05f);
            }
            break;
        case AttackType::Lunge:
            vel.x = facing * a.lungeSpeed * phaseSpeed();
            vel.y = def_.flying ? aimDir_.y * a.lungeSpeed : -120.0f;
            if (def_.flying) vel = aimDir_ * a.lungeSpeed * (a.lead > 0.0f ? phaseSpeed() : 1.0f);
            break;
        case AttackType::Charge:
            chargeDistance_ = 0.0f;
            w.playSound("sounds/dash.wav", box.center(), 0.8f, 0.7f);
            break;
        case AttackType::Slam: slam(w, a); break;
        case AttackType::Leap: {
            // Ballistic jump onto the point where the player will be, high enough to clear
            // ledges (a perched player is not safe): apex at least 64 px above the higher end.
            Vec2 tgt = predictPlayer(w, std::max(a.lead, 0.5f));
            float groundY = tgt.y;
            Vec2 from{tgt.x, tgt.y - 40.0f}, to{tgt.x, tgt.y + 400.0f};
            if (auto t = w.physics().raycast(from, to, true)) groundY = from.y + (to.y - from.y) * *t;
            float dx = std::clamp(tgt.x - box.center().x, -640.0f, 640.0f);
            if (std::abs(dx) < std::abs(tgt.x - box.center().x)) {
                // Too far for one leap: land as close as possible, on the ground there.
                float lx = box.center().x + dx;
                groundY = box.bottom();
                Vec2 f2{lx, std::min(tgt.y, box.bottom()) - 40.0f}, t2{lx, box.bottom() + 400.0f};
                if (auto t = w.physics().raycast(f2, t2, true)) groundY = f2.y + (t2.y - f2.y) * *t;
            }
            float dy = groundY - box.bottom();  // < 0: landing higher
            const float g = kLeapGravity;
            float apex = std::max({std::max(0.0f, -dy) + 64.0f, 96.0f + std::abs(dx) * 0.1f, g * a.leapTime * a.leapTime / 8.0f});
            float vy = -std::sqrt(2.0f * g * apex);
            float T = (-vy + std::sqrt(std::max(0.0f, vy * vy + 2.0f * g * dy))) / g;
            vel.x = dx / std::max(0.2f, T);
            vel.y = vy;
            face(dx);
            stateTimer_ = T + 0.8f;
            w.playSound("sounds/dash.wav", box.center(), 0.9f, 0.6f);
            w.particles().emit("slam_dust", box.bottomCenter(), 1.2f);
            break;
        }
        case AttackType::Laser:
            w.playSound("sounds/laser_fire.wav", box.center(), 0.9f);
            w.shake(0.2f);
            break;
        case AttackType::Summon: {
            playClip("summon", true);
            w.playSound("sounds/summon.wav", box.center(), 0.9f);
            int alive = 0;
            for (const auto& e : w.enemies())
                if (e->summonedBy == uid_ && e->alive()) ++alive;
            for (int i = 0; i < a.summonCount && alive < a.maxSummoned; ++i, ++alive) {
                float side = (i % 2 == 0) ? facing : -facing;
                Vec2 pos = box.bottomCenter() + Vec2{side * (28.0f + 18.0f * static_cast<float>(i / 2)), 0.0f};
                if (def_.flying) pos.y += 10.0f;
                if (Enemy* e = w.spawnEnemy(a.summonId, pos, side < 0)) {
                    e->summonedBy = uid_;
                    e->countsForStats = false;
                    e->alert(w);
                }
            }
            break;
        }
        case AttackType::Melee:
        case AttackType::Contact:
        default: break;
    }
}

void Enemy::performAttackActive(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    Player& p = w.player();
    attackClock_ += dt;
    // Staggered impacts (eruption lines) and further volleys (bursts), re-aimed each time.
    for (size_t k = 0; k < staged_.size();) {
        if (attackClock_ >= staged_[k].time) {
            spawnImpact(w, a, staged_[k].at);
            staged_.erase(staged_.begin() + static_cast<std::ptrdiff_t>(k));
        } else {
            ++k;
        }
    }
    if (burstsLeft_ > 0) {
        burstTimer_ -= dt;
        if (burstTimer_ <= 0.0f) {
            --burstsLeft_;
            burstTimer_ = a.burstInterval;
            Vec2 aim = a.lead > 0.0f ? predictPlayer(w, distance(box.center(), playerPos_) / std::max(60.0f, a.projectileSpeed) * a.lead) : playerPos_;
            aimDir_ = (aim - box.center()).normalized();
            if (aimDir_.lengthSq() < 0.01f) aimDir_ = {facing, 0.0f};
            face(aimDir_.x);
            fireProjectiles(w, a);
        }
    }
    DamageInfo d;
    d.amount = a.damage;
    d.type = DamageType::Contact;
    d.source = box.center();
    d.knockback = a.knockback;
    d.slow = a.slow;
    d.slowDuration = a.slowDuration;
    switch (a.type) {
        case AttackType::Melee: {
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            Rect hit = Rect::fromCenter(box.center() + Vec2{facing * (box.w * 0.5f + a.range * 0.4f), 0},
                                        {a.range * 0.9f, box.h + 4});
            if (!attackHit_ && hit.intersects(p.box)) attackHit_ = w.damagePlayer(d) || true;
            break;
        }
        case AttackType::Lunge:
            if (!attackHit_ && box.expanded(2).intersects(p.box)) attackHit_ = w.damagePlayer(d) || true;
            if (!def_.flying) vel.x = approach(vel.x, 0.0f, 300.0f * dt);
            break;
        case AttackType::Charge: {
            float sp = a.lungeSpeed * phaseSpeed();
            vel.x = facing * sp;
            chargeDistance_ += sp * dt;
            if (!attackHit_ && box.expanded(2).intersects(p.box)) attackHit_ = w.damagePlayer(d) || true;
            hopTimer_ -= dt;  // reused as dust emission timer while charging
            if (hopTimer_ <= 0.0f && grounded_) {
                hopTimer_ = 0.06f;
                w.particles().emit("dust_run", box.bottomCenter(), 1.4f, {-facing, -0.3f});
            }
            if (chargeDistance_ > a.range * 2.5f) stateTimer_ = 0.0f;
            break;
        }
        case AttackType::Laser: {
            float sweep = degToRad(a.sweepDegrees) * facing;
            float progress = a.active > 0 ? 1.0f - std::max(0.0f, stateTimer_) / a.active : 1.0f;
            float ang = laserAngle_ + sweep * progress;
            Vec2 origin = attackOrigin();
            Vec2 end = origin + Vec2::fromAngle(ang) * a.laserLength;
            if (auto t = w.physics().raycast(origin, end)) end = origin + (end - origin) * *t;
            if (laserHitTimer_ <= 0.0f && Collision::segmentIntersectsRect(origin, end, p.box)) {
                d.type = DamageType::Projectile;
                if (w.damagePlayer(d)) laserHitTimer_ = 0.4f;
            }
            w.particles().emit("laser_sparks", end, 0.4f);
            break;
        }
        case AttackType::Leap:
            if (!leapLanded_ && attackClock_ > 0.12f && grounded_) {
                leapLanded_ = true;
                vel.x = 0.0f;
                slam(w, a);
                stateTimer_ = std::min(stateTimer_, 0.15f);
            }
            break;
        default:
            if (!def_.flying) vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            break;
    }
}

void Enemy::stepAttack(GameWorld& w, float dt) {
    if (!attack_) {
        state_ = EnemyState::Chase;
        return;
    }
    performAttackActive(w, dt);
    if (stateTimer_ <= 0.0f) finishAttack(w);
}

void Enemy::finishAttack(GameWorld& w) {
    (void)w;
    if (attackIndex_ >= 0 && attackIndex_ < static_cast<int>(attackCooldowns_.size()))
        attackCooldowns_[attackIndex_] = attack_->cooldown * phaseCooldown();
    state_ = EnemyState::Recover;
    stateTimer_ = attack_->recover * phaseCooldown();
    if (def_.isBoss && comboLeft_ > 0) stateTimer_ = 0.12f;  // the next blow of the combo follows at once
    staged_.clear();
    burstsLeft_ = 0;
    playClip(anim_.has("recover") ? "recover" : "idle", true);
    attack_ = nullptr;
}

// ================================================================= render

void Enemy::render(RenderContext& ctx) const {
    if (!anim_.set() || !anim_.set()->texture) return;
    if (state_ == EnemyState::Dormant && !ctx.editor) return;
    const Texture& tex = *anim_.set()->texture;
    Vec2 feet = lerpv(prevBox.bottomCenter(), box.bottomCenter(), ctx.alpha);
    bool flip = facing < 0.0f;
    Rect src = anim_.sourceRect();
    Rect dst = anim_.destRect(feet, flip, def_.scale);
    Color tint = Color::white();
    if (state_ == EnemyState::Dormant) tint = {255, 255, 255, 110};
    if (state_ == EnemyState::Dead) tint.a = static_cast<uint8_t>(std::clamp(deathTimer_ / 0.9f, 0.0f, 1.0f) * 255);
    ctx.r.drawWorldTexture(tex, &src, dst, tint, BlendMode::Alpha, flip);
    // Hurt flash and telegraph pulse (additive overlays of the same frame)
    if (hurtFlash_ > 0.0f)
        ctx.r.drawWorldTexture(tex, &src, dst, Color{255, 255, 255, static_cast<uint8_t>(hurtFlash_ / 0.14f * 220)},
                               BlendMode::Additive, flip);
    if (state_ == EnemyState::Telegraph) {
        float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 28.0f);
        ctx.r.drawWorldTexture(tex, &src, dst, Color{255, 60, 90, static_cast<uint8_t>(60 + 120 * pulse)},
                               BlendMode::Additive, flip);
    }
    if (state_ == EnemyState::Defend) {
        Vec2 c = feet + Vec2{facing * box.w * 0.55f, -box.h * 0.5f};
        ctx.r.drawWorldGlow(c, 14.0f, {255, 179, 71, 120});
    }
    if (state_ != EnemyState::Dead && state_ != EnemyState::Dormant) {
        Vec2 eye = feet + Vec2{facing * box.w * 0.2f, -box.h * 0.6f};
        ctx.lights.add(eye, def_.lightRadius, def_.lightColor);
    }
}

void Enemy::renderTelegraph(RenderContext& ctx) const {
    if (state_ != EnemyState::Telegraph && state_ != EnemyState::Attack && state_ != EnemyState::Alert) return;
    Vec2 feet = lerpv(prevBox.bottomCenter(), box.bottomCenter(), ctx.alpha);
    Vec2 top = feet + Vec2{0, -box.h * def_.scale - 10.0f};
    if (state_ == EnemyState::Alert || state_ == EnemyState::Telegraph) {
        auto warn = ctx.res.texture("textures/fx/warning.png");
        float bob = dmath::sin(ctx.time * 12.0f) * 1.5f;
        if (warn) {
            float s = state_ == EnemyState::Alert ? 12.0f : 14.0f;
            ctx.r.drawWorldTexture(*warn, nullptr, Rect::fromCenter(top + Vec2{0, bob}, {s, s}));
        }
    }
    if (!attack_) return;
    float prog = state_ == EnemyState::Telegraph ? 1.0f - std::max(0.0f, stateTimer_) / std::max(0.01f, attack_->telegraph)
                                                 : 1.0f;
    Color warnCol{255, 77, 109, static_cast<uint8_t>(80 + 140 * prog)};
    switch (attack_->type) {
        case AttackType::Laser: {
            Vec2 origin = attackOrigin();
            Color bc = attack_->beamColor;
            float ang = laserAngle_;
            if (state_ == EnemyState::Attack) {
                float progress = attack_->active > 0 ? 1.0f - std::max(0.0f, stateTimer_) / attack_->active : 1.0f;
                ang += degToRad(attack_->sweepDegrees) * facing * progress;
            }
            Vec2 end = origin + Vec2::fromAngle(ang) * attack_->laserLength;
            if (state_ == EnemyState::Telegraph) {
                ctx.r.drawWorldLine(origin, end, bc.withAlpha(warnCol.a), 1.0f + 2.0f * prog);
            } else {
                float s = ctx.r.view().scale / 3.0f;
                ctx.r.drawWorldLine(origin, end, bc.withAlpha(230), 6.0f * s, BlendMode::Additive);
                ctx.r.drawWorldLine(origin, end, {255, 240, 240, 255}, 2.0f * s, BlendMode::Additive);
                ctx.r.drawWorldGlow(origin, 16.0f, bc.withAlpha(160));
                ctx.lights.add((origin + end) * 0.5f, attack_->laserLength * 0.6f, bc);
            }
            break;
        }
        case AttackType::Slam:
            if (state_ == EnemyState::Telegraph) {
                Rect zone = Rect::fromCenter(box.bottomCenter() + Vec2{0, -3}, {attack_->range * 2.0f * prog + box.w, 6});
                ctx.r.fillWorldRect(zone, Color{255, 77, 109, static_cast<uint8_t>(60 + 80 * prog)}, BlendMode::Additive);
            }
            break;
        case AttackType::Rain:
        case AttackType::Eruption:
            if (state_ == EnemyState::Telegraph) {
                for (const Vec2& t : targets_) {
                    float r = 6.0f + 10.0f * prog;
                    ctx.r.fillWorldRect(Rect::fromCenter(t + Vec2{0, -1}, {r * 2.0f, 3.0f}), warnCol, BlendMode::Additive);
                    ctx.r.drawWorldGlow(t, r * 1.4f, warnCol.withAlpha(static_cast<uint8_t>(40 + 60 * prog)));
                }
            }
            break;
        case AttackType::Leap:
            if (state_ == EnemyState::Telegraph) {
                // Where it is going to land: under the player.
                Vec2 c = playerPos_ + Vec2{0.0f, 12.0f};
                float r = 10.0f + 18.0f * prog;
                ctx.r.fillWorldRect(Rect::fromCenter(c, {r * 2.0f, 3.0f}), warnCol, BlendMode::Additive);
                ctx.r.drawWorldGlow(c, r * 1.3f, warnCol.withAlpha(static_cast<uint8_t>(50 + 70 * prog)));
            }
            break;
        case AttackType::Charge:
        case AttackType::Lunge:
            if (state_ == EnemyState::Telegraph) {
                Vec2 c = box.center();
                Vec2 tip = c + Vec2{facing * (box.w * 0.5f + 18.0f + 10.0f * prog), 0};
                ctx.r.drawWorldLine(c + Vec2{facing * box.w * 0.5f, 0}, tip, warnCol, 3.0f);
            }
            break;
        default: break;
    }
}

}  // namespace cosmic
