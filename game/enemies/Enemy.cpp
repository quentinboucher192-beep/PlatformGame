#include "game/enemies/Enemy.h"

#include <cmath>
#include <format>

#include "game/gameplay/GameWorld.h"
#include "engine/core/DetMath.h"

namespace cosmic {

namespace {
constexpr float kGravity = 900.0f;
constexpr float kMaxFall = 420.0f;
}  // namespace

const char* enemyStateName(EnemyState s) {
    switch (s) {
        case EnemyState::Dormant: return "Dormant";
        case EnemyState::Idle: return "Idle";
        case EnemyState::Patrol: return "Patrol";
        case EnemyState::Alert: return "Alert";
        case EnemyState::Chase: return "Chase";
        case EnemyState::Telegraph: return "Telegraph";
        case EnemyState::Attack: return "Attack";
        case EnemyState::Recover: return "Recover";
        case EnemyState::Defend: return "Defend";
        case EnemyState::Flee: return "Flee";
        case EnemyState::Hurt: return "Hurt";
        case EnemyState::TeleportOut: return "TeleportOut";
        case EnemyState::TeleportIn: return "TeleportIn";
        case EnemyState::PhaseTransition: return "Phase";
        case EnemyState::Dead: return "Dead";
    }
    return "?";
}

Enemy::Enemy(const EnemyDefinition& def, AnimationSetPtr anim, Vec2 feet, bool faceLeft, std::string objectId,
             float patrolRange, uint32_t uid)
    : def_(def), objectId_(std::move(objectId)), uid_(uid) {
    anim_.setSet(std::move(anim));
    box = Rect::fromBottomCenter(feet, def_.size);
    prevBox = box;
    origin_ = feet;
    facing = faceLeft ? -1.0f : 1.0f;
    patrolRange_ = patrolRange;
    hp_ = maxHp_ = def_.hp;
    attackCooldowns_.assign(def_.attacks.size(), 0.0f);
    for (size_t i = 0; i < attackCooldowns_.size(); ++i) attackCooldowns_[i] = 0.4f + 0.25f * static_cast<float>(i);
    hoverPhase_ = static_cast<float>(uid % 17) * 0.37f;
    teleportTimer_ = def_.teleportCooldown;
    state_ = def_.movement == MovementType::Patrol || def_.movement == MovementType::Fly || def_.movement == MovementType::Hop
                 ? EnemyState::Patrol
                 : EnemyState::Idle;
    playClip("idle", true);
}

void Enemy::playClip(const std::string& clip, bool restart) {
    if (anim_.has(clip)) {
        anim_.play(clip, restart);
        return;
    }
    // Fallbacks so custom sheets with fewer clips still animate.
    if (clip == "run" && anim_.has("walk")) anim_.play("walk", restart);
    else if ((clip == "walk" || clip == "move") && anim_.has("move")) anim_.play("move", restart);
    else if ((clip == "walk" || clip == "move") && anim_.has("run")) anim_.play("run", restart);
    else if (clip == "recover" || clip == "defend" || clip == "summon" || clip == "roar") anim_.play("idle", restart);
    else if (clip.rfind("telegraph", 0) == 0 && anim_.has("telegraph")) anim_.play("telegraph", restart);
    else if (anim_.has("idle")) anim_.play("idle", restart);
}

const std::string& Enemy::currentAttackName() const {
    static const std::string none;
    return attack_ ? attack_->name : none;
}

std::string Enemy::debugInfo() const {
    return std::format("{} [{}] hp {:.1f}/{:.0f}{}", def_.id, enemyStateName(state_), hp_, maxHp_,
                       attack_ ? " atk:" + attack_->id : std::string());
}

float Enemy::phaseSpeed() const {
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size())) return def_.phases[phase_].speedMultiplier;
    return 1.0f;
}

float Enemy::phaseCooldown() const {
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size())) return def_.phases[phase_].cooldownMultiplier;
    return 1.0f;
}

void Enemy::wake(GameWorld& w) {
    if (state_ != EnemyState::Dormant) return;
    state_ = EnemyState::TeleportIn;
    stateTimer_ = 0.45f;
    playClip(anim_.has("teleport_in") ? "teleport_in" : "idle", true);
    w.particles().emit("enemy_spawn", box.center(), def_.isBoss ? 3.0f : 1.0f);
    w.playSound("sounds/summon.wav", box.center(), 0.8f);
}

void Enemy::alert(GameWorld& w) {
    if (!alive() || dormant() || engaged_) return;
    engaged_ = true;
    if (state_ == EnemyState::TeleportIn) return;  // keeps its appearance animation
    state_ = EnemyState::Alert;
    stateTimer_ = 0.3f;
    face(w.player().center().x - box.center().x);
}

// ======================================================================= AI

void Enemy::sense(GameWorld& w) {
    const Player& p = w.player();
    playerPos_ = p.center();
    playerDist_ = distance(box.center(), playerPos_);
    bool inRange = playerDist_ <= (engaged_ ? def_.loseRange : def_.detectionRange);
    bool los = true;
    if (inRange && def_.needsLineOfSight) {
        Vec2 eye = box.center() + Vec2{0, -box.h * 0.25f};
        los = w.physics().lineOfSight(eye, playerPos_);
    }
    playerVisible_ = p.alive() && !p.respawning() && inRange && los;
}

bool Enemy::groundAhead(const GameWorld& w) const {
    Rect probe{facing > 0 ? box.right() : box.x - 4.0f, box.bottom(), 4.0f, 6.0f};
    bool found = false;
    w.physics().forEachIn(probe, [&](const Collider& c) {
        if (c.enabled && c.box.intersects(probe)) found = true;
    });
    return found;
}

void Enemy::moveTowards(GameWorld& w, Vec2 target, float speed, float dt) {
    if (def_.flying) {
        Vec2 d = target - box.center();
        float len = d.length();
        Vec2 want = len > 2.0f ? d / len * speed : Vec2{};
        vel = lerpv(vel, want, damp(4.0f, dt));
        face(d.x);
    } else {
        float dx = target.x - box.center().x;
        float dir = std::abs(dx) > 4.0f ? signf(dx) : 0.0f;
        face(dir);
        bool safe = dir == 0.0f || groundAhead(w) || def_.movement == MovementType::Hop;
        vel.x = approach(vel.x, safe ? dir * speed : 0.0f, 900.0f * dt);
        if (def_.movement == MovementType::Hop && grounded_ && dir != 0.0f) {
            hopTimer_ -= dt;
            if (hopTimer_ <= 0.0f) {
                vel.y = -def_.hopVelocity;
                hopTimer_ = 0.6f;
            }
        }
    }
}

void Enemy::fixedUpdate(GameWorld& w, float dt) {
    prevBox = box;
    auto tick = [dt](float& t) { t = std::max(0.0f, t - dt); };
    tick(hurtFlash_);
    tick(invuln_);
    tick(teleportTimer_);
    tick(defendCooldown_);
    tick(contactTimer_);
    tick(laserHitTimer_);
    for (auto& c : attackCooldowns_) tick(c);
    hoverPhase_ += dt;

    if (state_ == EnemyState::Dormant) return;
    if (state_ == EnemyState::Dead) {
        deathTimer_ -= dt;
        vel.x = approach(vel.x, 0.0f, 400.0f * dt);
        if (!def_.flying) applyPhysics(w, dt);
        return;
    }

    sense(w);
    checkPhase(w);
    stateTimer_ -= dt;

    switch (state_) {
        case EnemyState::Idle:
        case EnemyState::Patrol: stepIdle(w, dt); break;
        case EnemyState::Alert:
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            if (stateTimer_ <= 0.0f) state_ = EnemyState::Chase;
            break;
        case EnemyState::Chase:
        case EnemyState::Flee: stepEngaged(w, dt); break;
        case EnemyState::Telegraph: stepTelegraph(w, dt); break;
        case EnemyState::Attack: stepAttack(w, dt); break;
        case EnemyState::Recover:
            vel.x = approach(vel.x, 0.0f, 700.0f * dt);
            if (def_.flying) vel = lerpv(vel, {}, damp(4.0f, dt));
            if (stateTimer_ <= 0.0f) state_ = EnemyState::Chase;
            break;
        case EnemyState::Defend:
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            face(playerPos_.x - box.center().x);
            if (stateTimer_ <= 0.0f) {
                state_ = EnemyState::Chase;
                defendCooldown_ = def_.defendCooldown;
            }
            break;
        case EnemyState::Hurt:
            if (def_.flying) vel = lerpv(vel, {}, damp(5.0f, dt));
            else vel.x = approach(vel.x, 0.0f, 500.0f * dt);
            if (stateTimer_ <= 0.0f) state_ = engaged_ ? EnemyState::Chase : EnemyState::Patrol;
            break;
        case EnemyState::TeleportOut:
            vel = {};
            if (stateTimer_ <= 0.0f) {
                box = Rect::fromBottomCenter(teleportTarget_, box.size());
                prevBox = box;
                state_ = EnemyState::TeleportIn;
                stateTimer_ = 0.3f;
                playClip(anim_.has("teleport_in") ? "teleport_in" : "idle", true);
                // Ground teleporters (burrowers) dig out instead of blinking.
                if (def_.flying) w.particles().emit("teleport_in_enemy", box.center(), 1.0f);
                else w.particles().emit("burrow_dirt", box.bottomCenter(), 1.0f);
                face(playerPos_.x - box.center().x);
            }
            break;
        case EnemyState::TeleportIn:
            vel = {};
            if (stateTimer_ <= 0.0f) state_ = engaged_ ? EnemyState::Chase : EnemyState::Patrol;
            break;
        case EnemyState::PhaseTransition:
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            if (stateTimer_ <= 0.0f) {
                state_ = EnemyState::Chase;
                patternIndex_ = 0;
            }
            break;
        default: break;
    }
    applyPhysics(w, dt);
    contactDamage(w);
}

void Enemy::stepIdle(GameWorld& w, float dt) {
    float speed = def_.speed;
    switch (def_.movement) {
        case MovementType::Patrol: {
            playClip("walk");
            if (!def_.flying) {
                if (box.center().x > origin_.x + patrolRange_) face(-1);
                if (box.center().x < origin_.x - patrolRange_) face(1);
                if (grounded_ && !groundAhead(w)) face(-facing);
                vel.x = approach(vel.x, facing * speed, 600.0f * dt);
            }
            break;
        }
        case MovementType::Guard:
        case MovementType::Idle:
        case MovementType::Stationary:
            vel.x = approach(vel.x, 0.0f, 600.0f * dt);
            idleTurnTimer_ -= dt;
            if (idleTurnTimer_ <= 0.0f && def_.movement == MovementType::Guard) {
                idleTurnTimer_ = 2.2f;
                face(-facing);
            }
            playClip("idle");
            break;
        case MovementType::Fly: {
            float t = hoverPhase_;
            Vec2 target{origin_.x + dmath::sin(t * 0.6f) * patrolRange_, origin_.y - box.h * 0.5f + dmath::sin(t * 2.1f) * 6.0f};
            moveTowards(w, target, speed, dt);
            playClip("move");
            break;
        }
        case MovementType::Hop: {
            if (box.center().x > origin_.x + patrolRange_) face(-1);
            if (box.center().x < origin_.x - patrolRange_) face(1);
            hopTimer_ -= dt;
            if (grounded_) vel.x = approach(vel.x, 0.0f, 800.0f * dt);
            if (grounded_ && hopTimer_ <= 0.0f) {
                vel = {facing * speed, -def_.hopVelocity * 0.8f};
                hopTimer_ = 0.9f;
            }
            playClip(grounded_ ? "idle" : "walk");
            break;
        }
        default: break;
    }
    if (playerVisible_) {
        engaged_ = true;
        state_ = EnemyState::Alert;
        stateTimer_ = def_.isBoss ? 0.05f : 0.32f;
        face(playerPos_.x - box.center().x);
        w.playSound(def_.alertSound, box.center(), 0.6f);
        if (def_.callsReinforcements) {
            for (auto& other : w.enemies()) {
                if (other.get() != this && distance(other->center(), box.center()) < def_.alertRadius) other->alert(w);
            }
            w.particles().emit("alert_wave", box.center(), 1.0f);
        }
    }
}

void Enemy::stepEngaged(GameWorld& w, float dt) {
    if (!playerVisible_) {
        lostTimer_ += dt;
        if (lostTimer_ > 2.5f) {
            engaged_ = false;
            lostTimer_ = 0.0f;
            state_ = def_.movement == MovementType::Patrol || def_.movement == MovementType::Fly ? EnemyState::Patrol
                                                                                                 : EnemyState::Idle;
            return;
        }
    } else {
        lostTimer_ = 0.0f;
    }

    // Flee when weak
    if (def_.fleeHealthRatio > 0.0f && hpRatio() <= def_.fleeHealthRatio && !def_.isBoss) {
        state_ = EnemyState::Flee;
        Vec2 away = box.center() + (box.center() - playerPos_).normalized() * 100.0f;
        if (def_.flying) away.y -= 30.0f;
        moveTowards(w, away, def_.speed * def_.chaseSpeedMultiplier, dt);
        playClip(def_.flying ? "move" : "run");
        if (def_.canTeleport && teleportTimer_ <= 0.0f && playerDist_ < 70.0f) tryTeleport(w);
        return;
    }

    // Guard (frontal block)
    if (def_.canDefend && defendCooldown_ <= 0.0f && playerVisible_ && playerDist_ < 70.0f &&
        signf(box.center().x - w.player().center().x) == w.player().facing) {
        state_ = EnemyState::Defend;
        stateTimer_ = def_.defendDuration;
        playClip("defend", true);
        return;
    }

    if (def_.canTeleport && teleportTimer_ <= 0.0f && playerVisible_ && (playerDist_ < 56.0f || w.rng().chance(0.004f))) {
        tryTeleport(w);
        return;
    }

    if (playerVisible_ && chooseAttack(w)) return;

    float speed = def_.speed * def_.chaseSpeedMultiplier * phaseSpeed();
    switch (def_.reaction) {
        case ReactionType::Chase: {
            Vec2 target = playerPos_;
            if (def_.flying) target.y -= 28.0f;
            if (playerDist_ > def_.attackRange * 0.7f || def_.flying)
                moveTowards(w, target, speed, dt);
            else
                vel.x = approach(vel.x, 0.0f, 900.0f * dt), face(playerPos_.x - box.center().x);
            playClip(def_.flying ? "move" : (std::abs(vel.x) > def_.speed * 1.2f ? "run" : "walk"));
            break;
        }
        case ReactionType::KeepDistance: {
            Vec2 toP = playerPos_ - box.center();
            float d = toP.length();
            Vec2 target = box.center();
            if (d < def_.preferredDistance * 0.8f) target = box.center() - toP.normalized() * 40.0f;
            else if (d > def_.preferredDistance * 1.3f) target = box.center() + toP.normalized() * 40.0f;
            if (def_.flying) target.y = std::min(target.y, playerPos_.y - 40.0f);
            moveTowards(w, target, def_.speed, dt);
            face(toP.x);
            playClip(def_.flying ? "move" : (std::abs(vel.x) > 5 ? "walk" : "idle"));
            break;
        }
        case ReactionType::GuardZone: {
            bool inside = std::abs(playerPos_.x - origin_.x) < patrolRange_ * 1.5f;
            Vec2 target = inside ? playerPos_ : origin_;
            moveTowards(w, target, inside ? speed : def_.speed, dt);
            playClip(std::abs(vel.x) > 5 ? "walk" : "idle");
            break;
        }
        case ReactionType::Flee:
        case ReactionType::None:
        default:
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            face(playerPos_.x - box.center().x);
            playClip("idle");
            break;
    }
}

void Enemy::tryTeleport(GameWorld& w) {
    teleportTimer_ = def_.teleportCooldown;
    for (int attempt = 0; attempt < 12; ++attempt) {
        float ang = w.rng().range(0.0f, 2.0f * kPi);
        Vec2 c = playerPos_ + Vec2::fromAngle(ang) * def_.teleportRange;
        if (!def_.flying) c.y = playerPos_.y;
        Rect cand = Rect::fromCenter(c, box.size());
        MoveOptions opt;
        opt.isEnemy = true;
        if (w.physics().overlapsSolid(cand, opt)) continue;
        if (!w.level().bounds.containsRect(cand)) continue;
        if (!def_.flying && !w.physics().groundBelow(cand, opt)) continue;
        teleportTarget_ = cand.bottomCenter();
        state_ = EnemyState::TeleportOut;
        stateTimer_ = 0.32f;
        playClip(anim_.has("teleport_out") ? "teleport_out" : "idle", true);
        if (def_.flying) {
            w.particles().emit("teleport_out_enemy", box.center(), 1.0f);
            w.playSound("sounds/wisp_teleport.wav", box.center(), 0.8f);
        } else {
            w.particles().emit("burrow_dirt", box.bottomCenter(), 1.0f);
            w.playSound("sounds/burrow.wav", box.center(), 0.8f);
        }
        return;
    }
}

void Enemy::checkPhase(GameWorld& w) {
    if (!def_.isBoss || def_.phases.empty()) return;
    int next = phase_ + 1;
    if (next >= static_cast<int>(def_.phases.size())) return;
    if (hpRatio() > def_.phases[next].hpThreshold) return;
    phase_ = next;
    const BossPhase& ph = def_.phases[phase_];
    state_ = EnemyState::PhaseTransition;
    stateTimer_ = 1.6f;
    invuln_ = 1.6f;
    attack_ = nullptr;
    playClip("roar", true);
    w.playSound("sounds/boss_roar.wav", box.center(), 1.0f);
    w.playSound("sounds/boss_phase.wav", 0.9f);
    w.shake(0.7f);
    w.flash({255, 77, 109, 255}, 0.25f);
    w.particles().emit("boss_phase", box.center(), 2.0f);
    if (!ph.enterText.empty()) {
        WorldMessage m;
        m.style = WorldMessage::Style::Boss;
        m.text = ph.enterText;
        m.subtitle = ph.name;
        m.duration = 2.5f;
        w.message(m);
    }
    if (ph.summonOnEnter) {
        for (const auto& a : def_.attacks)
            if (a.type == AttackType::Summon) {
                for (int i = 0; i < a.summonCount; ++i) {
                    Vec2 pos = box.bottomCenter() + Vec2{(i % 2 ? -1.0f : 1.0f) * (40.0f + 20.0f * i), 0.0f};
                    if (Enemy* e = w.spawnEnemy(a.summonId, pos, i % 2 == 1)) {
                        e->summonedBy = uid_;
                        e->countsForStats = false;
                        e->alert(w);
                    }
                }
                break;
            }
    }
    w.emit(objectId_, "bossPhase", {{"phase", phase_ + 1}});
}

void Enemy::applyPhysics(GameWorld& w, float dt) {
    MoveOptions opt;
    opt.isEnemy = true;
    if (def_.flying && state_ != EnemyState::Dead) {
        w.physics().move(box, vel * dt, opt);
        grounded_ = false;
        return;
    }
    vel.y = std::min(vel.y + kGravity * dt, kMaxFall);
    MoveResult r = w.physics().move(box, vel * dt, opt);
    if (r.hitBottom) {
        if (vel.y > 0) vel.y = 0;
        grounded_ = true;
    } else {
        grounded_ = w.physics().groundBelow(box, opt) != nullptr && vel.y >= 0;
    }
    if (r.hitTop && vel.y < 0) vel.y = 0;
    if (r.hitLeft || r.hitRight) {
        if (state_ == EnemyState::Patrol) face(-facing);
        if (state_ == EnemyState::Attack && attack_ && attack_->type == AttackType::Charge) {
            // Charging into a wall: stunned.
            w.shake(0.4f);
            w.playSound("sounds/golem_slam.wav", box.center(), 0.9f);
            w.particles().emit("slam_dust", {r.hitRight ? box.right() : box.x, box.center().y}, 1.5f);
            state_ = EnemyState::Recover;
            stateTimer_ = attack_->recover * 1.8f;
            playClip("recover", true);
            vel.x = -facing * 60.0f;
        } else {
            vel.x = 0;
        }
    }
}

void Enemy::contactDamage(GameWorld& w) {
    if (def_.contactDamage <= 0.0f || contactTimer_ > 0.0f || state_ == EnemyState::TeleportOut ||
        state_ == EnemyState::TeleportIn)
        return;
    Player& p = w.player();
    if (!p.alive() || !box.expanded(-1).intersects(p.box)) return;
    DamageInfo d;
    d.amount = def_.contactDamage;
    d.type = DamageType::Contact;
    d.source = box.center();
    d.knockback = 160.0f;
    if (w.damagePlayer(d)) contactTimer_ = 0.5f;
}

// ================================================================== damage

bool Enemy::takeDamage(GameWorld& w, const DamageInfo& dmg) {
    if (!alive() || dormant() || invuln_ > 0.0f) return false;
    if (state_ == EnemyState::TeleportOut || state_ == EnemyState::TeleportIn) return false;
    // Guard-breaking hits: impulse, dash, ground pound (from above) and the charged shot.
    bool breaksGuard = dmg.type == DamageType::Impulse || dmg.type == DamageType::Dash ||
                       dmg.type == DamageType::GroundPound || dmg.type == DamageType::ChargeShot;
    if (state_ == EnemyState::Defend && !breaksGuard) {
        float fromSide = signf(dmg.source.x - box.center().x);
        if (fromSide == facing || fromSide == 0.0f) {
            w.playSound("sounds/shield_block.wav", box.center(), 0.8f, 0.8f);
            w.particles().emit("block_sparks", box.center() + Vec2{facing * box.w * 0.5f, 0}, 1.0f);
            invuln_ = 0.12f;
            return false;
        }
    }
    if (state_ == EnemyState::Defend && breaksGuard && dmg.type != DamageType::Dash) {
        // Guard broken
        state_ = EnemyState::Hurt;
        stateTimer_ = 0.8f;
        defendCooldown_ = def_.defendCooldown;
    }
    float mult = def_.multiplier(dmg.type);
    if (mult <= 0.0f) {
        w.particles().emit("block_sparks", box.center(), 0.6f);
        w.playSound("sounds/shield_block.wav", box.center(), 0.5f, 1.3f);
        invuln_ = 0.15f;
        return false;
    }
    float amount = dmg.amount * mult;
    hp_ -= amount;
    hurtFlash_ = 0.14f;
    invuln_ = 0.12f;
    engaged_ = true;
    float dir = box.center().x >= dmg.source.x ? 1.0f : -1.0f;
    float kb = dmg.knockback * (1.0f - def_.knockbackResist);
    if (def_.flying)
        vel = (box.center() - dmg.source).normalized() * kb;
    else
        vel.x = dir * kb, vel.y = std::min(vel.y, -kb * 0.35f);
    w.particles().emit(mult > 1.2f ? "hit_weak" : "hit_spark", box.center(), 1.0f);
    if (hp_ <= 0.0f) {
        kill(w);
        return true;
    }
    w.playSound(def_.hurtSound, box.center(), 0.8f, w.fxRng().range(0.95f, 1.1f));
    bool staggers = def_.knockbackResist < 0.75f && !def_.isBoss;
    if (staggers && state_ != EnemyState::PhaseTransition) {
        state_ = EnemyState::Hurt;
        stateTimer_ = 0.22f;
        attack_ = nullptr;
        playClip("hurt", true);
    }
    return true;
}

void Enemy::kill(GameWorld& w) {
    if (state_ == EnemyState::Dead) return;
    hp_ = 0.0f;
    state_ = EnemyState::Dead;
    deathTimer_ = 0.9f;
    attack_ = nullptr;
    playClip(anim_.has("death") ? "death" : "hurt", true);
    w.playSound(def_.deathSound, box.center(), 1.0f);
    w.particles().emit(def_.isBoss ? "boss_death" : "enemy_death", box.center(), def_.isBoss ? 2.5f : 1.0f);
    w.shake(def_.isBoss ? 1.0f : 0.2f);
    w.onEnemyKilled(*this);
}

void Enemy::onAnimEvent(GameWorld& w, const std::string& e) {
    if (e.rfind("sfx:", 0) == 0) w.playSound("sounds/" + e.substr(4) + ".wav", box.center(), 0.8f);
    if (e == "footstep" && grounded_ && def_.family == EnemyFamily::Heavy) {
        w.playSound("sounds/land.wav", box.bottomCenter(), 0.4f, 0.6f);
        w.shake(0.05f);
    }
}

void Enemy::update(GameWorld& w, float dt) {
    anim_.update(dt, [&](const std::string& e) { onAnimEvent(w, e); });
}

}  // namespace cosmic
