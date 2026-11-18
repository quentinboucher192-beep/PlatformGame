#include "game/enemies/Enemy.h"

#include <cmath>
#include <format>

#include "engine/resources/ResourceManager.h"
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
    for (const auto& a : def_.attacks) {
        if (a.type == AttackType::Mirror) hasMirror_ = true;
        if (a.isDragonType()) hasDragon_ = true;
        if (a.isGolemType()) hasGolem_ = true;  // 2.52: the golem walks with its own brain
        if (a.isPhoenixType()) hasPhoenix_ = true;  // 2.56: the phoenix glides, its columns, its blade
        if (a.isLicheType()) hasLiche_ = true;      // 2.57: the lich, its altars, its runes, its curse
        if (a.isTitanType()) hasTitan_ = true;      // 2.58: the war machine, its presses, its missiles, its belts
    }
    standSize_ = def_.size;
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
    // 2.52: the golem's clips on a sheet without them (an enemy made in the editor).
    else if ((clip == "curl" || clip == "crouch") && anim_.has("telegraph")) anim_.play("telegraph", restart);
    else if ((clip == "roll" || clip == "leap" || clip == "fall" || clip == "land" || clip == "quake") && anim_.has("attack"))
        anim_.play("attack", restart);
    else if ((clip == "uncurl" || clip == "dizzy") && anim_.has("hurt")) anim_.play("hurt", restart);
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
    float slow = slowTimer_ > 0.0f ? 0.5f : 1.0f;  // 2.19: Nova de givre on a boss
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size()))
        return def_.phases[phase_].speedMultiplier * (enraged_ ? 1.2f : 1.0f) * slow;
    return slow;
}

float Enemy::phaseCooldown() const {
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size()))
        return def_.phases[phase_].cooldownMultiplier * (enraged_ ? 0.7f : 1.0f) / pressure_;  // 2.67: a raid's pressure (1 elsewhere)
    return 1.0f / pressure_;
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
    face(w.nearestPlayer(box.center()).center().x - box.center().x);  // 2.62: its nearest player
}

// ======================================================================= AI

void Enemy::sense(GameWorld& w) {
    const Player& p = w.player();
    playerPos_ = w.enemyAim();  // Nova, or the decoy while there is one (2.19)
    playerDist_ = distance(box.center(), playerPos_);
    bool inRange = playerDist_ <= (engaged_ ? def_.loseRange : def_.detectionRange);
    bool los = true;
    if (inRange && def_.needsLineOfSight) {
        Vec2 eye = box.center() + Vec2{0, -box.h * 0.25f};
        los = w.physics().lineOfSight(eye, playerPos_);
    }
    playerVisible_ = p.alive() && !p.respawning() && inRange && los;
    // Player motion, for the bosses' predictions (smoothed, per fixed step).
    if (havePlayerPos_) {
        Vec2 v = (playerPos_ - lastPlayerPos_) * 120.0f;
        if (v.lengthSq() > 900.0f * 900.0f) v = {};  // respawn / teleport
        playerVel_ = playerVel_ * 0.8f + v * 0.2f;
    }
    lastPlayerPos_ = playerPos_;
    havePlayerPos_ = true;
    playerAirborne_ = !p.grounded();
}

Vec2 Enemy::predictPlayer(const GameWorld& w, float seconds) const {
    Vec2 v = playerVel_;
    Vec2 p = playerPos_ + Vec2{v.x * seconds, 0.0f};
    const Rect& b = w.level().bounds;
    p.x = std::clamp(p.x, b.x + 16.0f, b.right() - 16.0f);
    if (playerAirborne_) {
        // Where a jumping player comes down: the ground below the predicted x.
        Vec2 from{p.x, playerPos_.y - 8.0f};
        Vec2 to{p.x, playerPos_.y + 360.0f};
        if (auto t = w.physics().raycast(from, to, true)) p.y = from.y + (to.y - from.y) * *t - 12.0f;
    }
    return p;
}

float Enemy::telegraphScale() const {
    float k = 1.0f;
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size())) k *= def_.phases[phase_].telegraphMultiplier;
    if (enraged_) k *= 0.85f;
    return k;
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
    tick(slowTimer_);
    if (frozenTimer_ > 0.0f && state_ != EnemyState::Dead && state_ != EnemyState::Dormant) {
        // Frozen solid: no thinking, no attack, falls if it was in the air.
        frozenTimer_ = std::max(0.0f, frozenTimer_ - dt);
        vel.x = 0.0f;
        if (def_.flying) vel.y = 0.0f;
        else applyPhysics(w, dt);
        return;
    }

    if (state_ == EnemyState::Dormant) return;
    if (state_ == EnemyState::Dead) {
        deathTimer_ -= dt;
        vel.x = approach(vel.x, 0.0f, 400.0f * dt);
        if (!def_.flying) applyPhysics(w, dt);
        return;
    }

    if (mirrorOf_ != 0) {
        // A double of a guardian (2.19): it fades with time, or with its original.
        const Enemy* original = w.findEnemy(mirrorOf_);
        if (lifeTimer_ > 0.0f) lifeTimer_ = std::max(0.0f, lifeTimer_ - dt);
        if (!original || !original->alive() || (expires_ && lifeTimer_ <= 0.0f)) {
            vanishing = true;
            countsForStats = false;
            w.particles().emit("frost_burst", box.center(), 1.2f);
            kill(w);
            return;
        }
    }
    if (hasMirror_) {
        doubles_ = 0;
        for (const auto& e : w.enemies())
            if (e->mirrorOf_ == uid_ && e->alive()) ++doubles_;
    }
    sense(w);
    if (lastStand_) {
        // « Dernière lumière » (2.33): its final attacks, then it dies.
        stateTimer_ -= dt;
        simDt_ = dt;
        stepLastStand(w, dt);
        applyPhysics(w, dt);
        contactDamage(w);
        return;
    }
    checkPhase(w);
    if (warded_) stepWard(w);  // 2.57: the Liche's phylactery broken, the next phase begins
    if (!warded_ && phylactery_ != 0) {
        // A fight resumed (setWounds) while its soul was hidden: the crystal goes with the ward.
        if (Enemy* c = w.findEnemy(phylactery_)) {
            c->vanishing = true;
            c->despawn();
        }
        phylactery_ = 0;
    }
    if (hasTitan_ && !stalled() && !valves_.empty()) clearValves(w);  // 2.58: a fight resumed while it was stalled
    stateTimer_ -= dt;
    simDt_ = dt;
    if (def_.isBoss && engaged_) {
        fightTime_ += dt;
        phaseTime_ += dt;
        // Rage: after the boss's own time in the fight, or after its phase's time (2.33).
        const float phaseRage = phase_ < static_cast<int>(def_.phases.size()) ? def_.phases[phase_].enrageTime : 0.0f;
        const bool due = (def_.enrageTime > 0.0f && fightTime_ >= def_.enrageTime) || (phaseRage > 0.0f && phaseTime_ >= phaseRage);
        if (!enraged_ && due && state_ != EnemyState::PhaseTransition) {
            // A fight that drags on: the boss gets faster and hits more often.
            enraged_ = true;
            WorldMessage m;
            m.style = WorldMessage::Style::Boss;
            m.text = "Le gardien entre en rage !";
            m.subtitle = "Rage";
            m.duration = 2.0f;
            w.message(m);
            w.playSound("sounds/boss_roar.wav", box.center(), 1.0f);
            w.flash({255, 60, 40, 255}, 0.2f);
            w.shake(0.5f);
            w.particles().emit("boss_phase", box.center(), 1.5f);
            // The level may answer (2.33: the arena of Singularis closes in).
            if (!objectId_.empty()) w.emit(objectId_, "bossEnrage");
        }
    }

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
            if (hasTitan_ && venting()) {  // 2.58: the Titan vents its steam, its core bare
                stepVent(w, dt);
                break;
            }
            vel.x = approach(vel.x, 0.0f, 700.0f * dt);
            if (hasPhoenix_ && def_.flying && def_.isBoss && engaged_) {
                phoenixGlide(w, dt, 0.8f);  // 2.56: the phoenix glides between its blows
            } else if (hasLiche_ && def_.flying && def_.isBoss && engaged_) {
                licheHover(w, dt);  // 2.57: the lich floats over its altar
            } else if (def_.flying && def_.isBoss && engaged_) {
                // Flying bosses keep repositioning between attacks (preferred distance, above).
                Vec2 toP = playerPos_ - box.center();
                float d = toP.length();
                Vec2 target = box.center();
                if (d > def_.preferredDistance * 1.3f) target = box.center() + toP.normalized() * 60.0f;
                else if (d < def_.preferredDistance * 0.8f) target = box.center() - toP.normalized() * 60.0f;
                target.y = std::min(target.y, playerPos_.y - 50.0f);
                moveTowards(w, target, def_.speed * 0.7f, dt);
            } else if (def_.flying) {
                vel = lerpv(vel, {}, damp(4.0f, dt));
            }
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
            if (hasLiche_) {  // 2.57: from one altar to another
                licheBlink(w, dt);
                break;
            }
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
            if (inAshes()) {  // 2.56: the Phénix burns to ashes, lies in them, is reborn
                stepAshes(w, dt);
                break;
            }
            if (hasTitan_ && stalled()) {  // 2.58: the Titan stalled, its valves to turn
                stepStall(w, dt);
                break;
            }
            vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            if (stateTimer_ <= 0.0f) {
                state_ = EnemyState::Chase;
                patternIndex_ = 0;
                comboLeft_ = 0;
                staged_.clear();
            }
            break;
        default: break;
    }
    // 2.56: the Phénix never stays against a wall nor under the roof of its arena (not during a
    // dive, which goes to its end, nor in its ashes).
    if (hasPhoenix_ && def_.flying && def_.isBoss && engaged_ && !inAshes() && stage_ != Stage::Dash) phoenixKeepOff(w);
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

    // 2.52: the golem (a walking boss with its attacks) has its own brain (GolemAttacks.cpp).
    if (hasGolem_ && def_.isBoss && !def_.flying) {
        golemThink(w, dt);
        return;
    }
    // 2.57: the lich floats over its altars and teleports between them (LicheAttacks.cpp).
    if (hasLiche_ && def_.isBoss && def_.flying) {
        licheThink(w, dt);
        return;
    }
    // 2.58: the war machine walks with the golem's steps and runs its machinery (TitanAttacks.cpp).
    if (hasTitan_ && def_.isBoss && !def_.flying) {
        titanThink(w, dt);
        return;
    }

    if (playerVisible_ && chooseAttack(w)) return;

    float speed = def_.speed * def_.chaseSpeedMultiplier * phaseSpeed();
    switch (def_.reaction) {
        case ReactionType::Chase: {
            Vec2 target = playerPos_;
            if (def_.flying) target.y -= 28.0f;
            if (def_.isBoss && !def_.flying && box.bottom() - (playerPos_.y + 12.0f) > 48.0f) {
                // Player perched above: step back to get an angle for shots and leaps
                // instead of waiting under the ledge.
                float side = box.center().x >= playerPos_.x ? 1.0f : -1.0f;
                const Rect& b = w.level().bounds;
                if ((side > 0 && playerPos_.x + 200.0f > b.right() - 40.0f) || (side < 0 && playerPos_.x - 200.0f < b.x + 40.0f)) side = -side;
                target.x = playerPos_.x + side * 190.0f;
                moveTowards(w, target, speed, dt);
                if (std::abs(target.x - box.center().x) < 8.0f) face(playerPos_.x - box.center().x);
                playClip(std::abs(vel.x) > def_.speed * 1.2f ? "run" : (std::abs(vel.x) > 5.0f ? "walk" : "idle"));
                break;
            }
            if (playerDist_ > def_.attackRange * 0.7f || def_.flying)
                moveTowards(w, target, speed, dt);
            else
                vel.x = approach(vel.x, 0.0f, 900.0f * dt), face(playerPos_.x - box.center().x);
            playClip(def_.flying ? "move" : (std::abs(vel.x) > def_.speed * 1.2f ? "run" : "walk"));
            break;
        }
        case ReactionType::KeepDistance: {
            if (hasPhoenix_ && def_.flying && def_.isBoss) {
                phoenixGlide(w, dt, 1.0f);  // 2.56: « vol plané »
                break;
            }
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

void Enemy::setWounds(float hp, int phase) {
    phaseTime_ = 0.0f;
    ashes_ = AshStage::None;  // 2.56: a fight resumed never starts in the Phénix's ashes
    // 2.57: nor with the Liche's soul in its phylactery (the crystal goes at the next step).
    warded_ = false;
    phylacteryAltar_ = -1;
    // 2.58: nor stalled nor venting (the valves go at the next step), its heat gauge empty.
    titan_ = TitanStage::None;
    heat_ = 0.0f;
    hp_ = std::clamp(hp, 1.0f, maxHp_);
    phase_ = def_.phases.empty() ? 0 : std::clamp(phase, 0, static_cast<int>(def_.phases.size()) - 1);
}

void Enemy::checkPhase(GameWorld& w) {
    if (!def_.isBoss || def_.phases.empty()) return;
    int next = phase_ + 1;
    if (next >= static_cast<int>(def_.phases.size())) return;
    if (hpRatio() > def_.phases[next].hpThreshold) return;
    // 2.52: a rolling or leaping golem roars once back on its floor (a new phase may break the
    // ground of its arena: never while it is over it).
    if (hasGolem_ && golemBusy()) return;
    if (inAshes()) return;  // 2.56: in its ashes, the next phase comes with its rebirth
    if (warded_) return;    // 2.57: its soul in its phylactery, the next phase comes when it breaks
    if (stalled()) return;  // 2.58: stalled, the next phase comes when it restarts
    if (hasTitan_ && !def_.phases[static_cast<size_t>(phase_)].stall.empty()) {
        // 2.58, the Titan's « Calage »: the machine stalls, its valves appear.
        beginStall(w, next);
        return;
    }
    if (!def_.phases[static_cast<size_t>(phase_)].phylactery.empty()) {
        // 2.57, the Liche's « Phylactère »: its soul hidden in a crystal on an altar.
        beginWard(w, next);
        return;
    }
    if (def_.phases[static_cast<size_t>(phase_)].rebirth > 0.0f) {
        // 2.56, the Phénix's « Cendres »: it burns to ashes, then is reborn into the next phase.
        beginAshes(w, next);
        return;
    }
    enterPhase(w, next);
}

void Enemy::enterPhase(GameWorld& w, int next) {
    phase_ = next;
    phaseTime_ = 0.0f;
    const BossPhase& ph = def_.phases[phase_];
    if (!ph.enterSound.empty()) w.playSound(ph.enterSound, box.center(), 1.0f);
    if (attack_ && attack_->isGuardianType()) guardianEnd(w);  // a channel stops with the roar
    if (attack_ && attack_->isDragonType()) dragonEnd(w);      // a dive or a landing too (2.50)
    if (attack_ && attack_->isGolemType()) golemEnd(w);        // its quake, its curl, its crouch (2.52)
    if (attack_ && attack_->isPhoenixType()) phoenixEnd(w);    // its blade (2.56)
    if (attack_ && attack_->isLicheType()) licheEnd(w);        // its runes stay, its curse is lifted (2.57)
    if (attack_ && attack_->isTitanType()) titanEnd(w);        // its presses fall on (2.58)
    if (hasTitan_ && venting()) {                              // its vents close with the roar (2.58)
        titan_ = TitanStage::None;
        heat_ = 0.0f;
    }
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
    opt.dropThrough = dropThrough_;  // 2.50: the dragon dives and lands through the one-way ledges
    if (hasPhoenix_) opt.ignoreTags |= ColliderTag::Crumble;  // 2.56: the Phénix flies through its perches of embers
    if (hasLiche_) opt.dropThrough = true;  // 2.57: the Liche floats through the ledges and the tops of its altars
    if (skyborne()) {
        // 2.52: the golem's titanic leap goes out of sight and comes down on its mark (through
        // the roof of its arena, out of the camera's view): no collision on the way.
        box.x += vel.x * dt;
        box.y += vel.y * dt;
        grounded_ = false;
        return;
    }
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
        } else if (state_ == EnemyState::Attack && attack_ && attack_->type == AttackType::Leap && vel.y < 0.0f) {
            // Rising along a ledge: keeps its momentum to land on top of it.
        } else {
            vel.x = 0;
        }
    }
}

void Enemy::contactDamage(GameWorld& w) {
    if (def_.contactDamage <= 0.0f || contactTimer_ > 0.0f || state_ == EnemyState::TeleportOut ||
        state_ == EnemyState::TeleportIn || inAshes())  // 2.56: its ashes do not burn on contact
        return;
    // 2.62: every player it touches (alone: the only one).
    bool hit = false;
    w.forEachPlayer([&](Player& p) {
        if (!p.alive() || !box.expanded(-1).intersects(p.box)) return;
        DamageInfo d;
        d.amount = def_.contactDamage;
        d.type = DamageType::Contact;
        d.source = box.center();
        d.knockback = 160.0f;
        if (w.damagePlayer(d)) hit = true;
    });
    if (hit) contactTimer_ = 0.5f;
}

// ================================================================== damage

bool Enemy::takeDamage(GameWorld& w, const DamageInfo& dmg) {
    if (!alive() || dormant() || invuln_ > 0.0f) return false;
    if (outOfSight()) return false;  // 2.52: the golem above its arena, out of reach
    if (inAshes()) {
        // 2.56: the Phénix in its ashes cannot be hurt (sparks fly off them).
        if (hurtFlash_ <= 0.0f) w.particles().emit("block_sparks", box.center(), 0.5f);
        return false;
    }
    if (lastStand_) {
        // Nothing hurts it any more: its last light must be survived.
        if (hurtFlash_ <= 0.0f) w.particles().emit("block_sparks", box.center(), 0.6f);
        return false;
    }
    if (state_ == EnemyState::TeleportOut || state_ == EnemyState::TeleportIn) return false;
    if (stalled()) {
        // 2.58: the Titan stalled: its armour is locked, its valves must be turned.
        if (hurtFlash_ <= 0.0f) w.particles().emit("block_sparks", box.center(), 0.6f);
        hurtFlash_ = 0.05f;
        return false;
    }
    if (warded_) {
        // 2.57: the Liche's soul is in its phylactery: nothing hurts it until the crystal breaks.
        if (hurtFlash_ <= 0.0f) w.particles().emit("block_sparks", box.center(), 0.6f);
        hurtFlash_ = 0.05f;
        return false;
    }
    if (raidGuard_) {
        // 2.67: a raid's gigaboss behind its carapace (game/raid/Gigaboss): its vents must be closed.
        if (hurtFlash_ <= 0.0f) w.particles().emit("block_sparks", box.center(), 0.6f);
        hurtFlash_ = 0.05f;
        return false;
    }
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
    // 2.50: the dragon landed by its « Atterrissage » is exposed (a window to punish it).
    if (perched_ && attack_) mult *= attack_->vulnerability > 0.0f ? attack_->vulnerability : 1.5f;
    // 2.52: the golem curled in its shell is armoured; dizzy after its roll it is exposed.
    if (hasGolem_ && attack_ && attack_->type == AttackType::Shell) {
        if (curled()) {
            mult *= attack_->armor > 0.0f ? attack_->armor : 0.35f;
            w.particles().emit("block_sparks", box.center(), 0.8f);
        } else if (dizzy()) {
            mult *= attack_->vulnerability > 0.0f ? attack_->vulnerability : 1.5f;
        }
    }
    // 2.58: the Titan venting its steam: its core is bare.
    if (hasTitan_ && coreExposed()) mult *= 1.8f;
    mult *= raidMult_;  // 2.67: a raid's gigaboss, its weak point bare (1 elsewhere)
    float amount = dmg.amount * mult;
    hp_ -= amount;
    w.noteEnemyDamaged(*this, dmg, amount);  // 2.54: statistics only (which attack of Nova reached it); 2.67: and how much
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
        if (def_.isBoss && !def_.finalAttacks.empty() && mirrorOf_ == 0) beginLastStand(w);
        else kill(w);
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

void Enemy::wound(GameWorld& w, float amount) {
    // 2.67: one of a raid gigaboss's weak points broken: a blow that ignores its guard.
    if (!alive() || dormant() || lastStand_ || amount <= 0.0f) return;
    hp_ -= amount;
    hurtFlash_ = 0.3f;
    engaged_ = true;
    w.particles().emit("hit_weak", box.center(), 2.0f);
    w.shake(0.45f);
    if (hp_ <= 0.0f) {
        if (def_.isBoss && !def_.finalAttacks.empty() && mirrorOf_ == 0) beginLastStand(w);
        else kill(w);
    }
}

void Enemy::kill(GameWorld& w) {
    if (state_ == EnemyState::Dead) return;
    hp_ = 0.0f;
    state_ = EnemyState::Dead;
    deathTimer_ = 0.9f;
    attack_ = nullptr;
    perched_ = dropThrough_ = false;
    stage_ = Stage::None;
    ashes_ = AshStage::None;
    warded_ = false;  // 2.57: its crystal vanishes with it (a boss's creatures do)
    titan_ = TitanStage::None;  // 2.58: its valves too
    if (hasGolem_ && golem_ != GolemStage::None) {  // 2.52: it dies standing, on its floor
        if (skyborne()) box = Rect::fromBottomCenter({box.center().x, floorY_}, box.size());
        box = Rect::fromBottomCenter(box.bottomCenter(), standSize_);
        prevBox = box;
        golem_ = GolemStage::None;
    }
    playClip(anim_.has("death") ? "death" : "hurt", true);
    w.playSound(def_.deathSound, box.center(), 1.0f);
    w.particles().emit(def_.isBoss ? "boss_death" : "enemy_death", box.center(), def_.isBoss ? 2.5f : 1.0f);
    w.shake(def_.isBoss ? 1.0f : 0.2f);
    w.onEnemyKilled(*this);
}

void Enemy::onAnimEvent(GameWorld& w, const std::string& e) {
    if (e.rfind("sfx:", 0) == 0) w.playSound("sounds/" + e.substr(4) + ".wav", box.center(), 0.8f);
    if (e == "footstep" && grounded_ && hasTitan_ && def_.isBoss) {  // 2.58: the Titan's steps shake the foundry
        w.playSound("sounds/golem_slam.wav", box.bottomCenter(), 0.35f, 0.7f);
        w.shake(0.08f);
    }
    if (e == "footstep" && grounded_ && def_.family == EnemyFamily::Heavy) {
        w.playSound("sounds/land.wav", box.bottomCenter(), 0.4f, 0.6f);
        w.shake(0.05f);
    }
}

void Enemy::freeze(float seconds) {
    if (state_ == EnemyState::Dead || state_ == EnemyState::Dormant) return;
    if (def_.isBoss) slowTimer_ = std::max(slowTimer_, seconds);
    else frozenTimer_ = std::max(frozenTimer_, seconds);
}

void Enemy::update(GameWorld& w, float dt) {
    if (def_.isBoss && shownLook_ != phase_) refreshLook(w);
    anim_.update(dt, [&](const std::string& e) { onAnimEvent(w, e); });
}

// ============================================================ the giga boss (2.33)

std::string Enemy::phaseAnimation() const {
    std::string path = def_.animation;
    for (int i = 0; i <= phase_ && i < static_cast<int>(def_.phases.size()); ++i)
        if (!def_.phases[static_cast<size_t>(i)].animation.empty()) path = def_.phases[static_cast<size_t>(i)].animation;
    return path;
}

Color Enemy::lightColor() const {
    if (def_.isBoss && phase_ < static_cast<int>(def_.phases.size()) && def_.phases[static_cast<size_t>(phase_)].lightColor.a > 0)
        return def_.phases[static_cast<size_t>(phase_)].lightColor;
    return def_.lightColor;
}

void Enemy::refreshLook(GameWorld& w) {
    // Display only: a boss whose phase has its own sheet changes look (the roar of the new
    // phase plays on it). The simulation never reads the animation.
    shownLook_ = phase_;
    std::string path = phaseAnimation();
    if (path == shownAnim_) return;
    bool first = shownAnim_.empty();
    shownAnim_ = path;
    if (first && path == def_.animation) return;  // the sheet it was made with
    if (!def_.assetRoot.empty()) w.resources().pushMount(def_.assetRoot);
    AnimationSetPtr set = w.resources().animation(path);
    if (!def_.assetRoot.empty()) w.resources().popMount();
    if (!set) return;
    std::string clip = anim_.current();
    anim_.setSet(std::move(set));
    playClip(clip.empty() ? "idle" : clip, true);
}

void Enemy::beginLastStand(GameWorld& w) {
    // Out of health: it cannot be hurt any more, flies to the middle of its arena and gives
    // its last light (its final attacks), then dies.
    lastStand_ = true;
    hp_ = 0.0f;
    finalIndex_ = 0;
    if (attack_ && attack_->isGuardianType()) guardianEnd(w);
    if (attack_ && attack_->isDragonType()) dragonEnd(w);
    if (attack_ && attack_->isGolemType()) golemEnd(w);
    if (attack_ && attack_->isPhoenixType()) phoenixEnd(w);
    if (attack_ && attack_->isLicheType()) licheEnd(w);
    if (attack_ && attack_->isTitanType()) titanEnd(w);
    titan_ = TitanStage::None;  // 2.58: no vent nor stall in its last stand
    attack_ = nullptr;
    staged_.clear();
    comboLeft_ = 0;
    burstsLeft_ = 0;
    state_ = EnemyState::PhaseTransition;
    stateTimer_ = 1.8f;
    playClip("roar", true);
    w.playSound("sounds/boss_phase.wav", 1.0f);
    w.playSound(def_.alertSound, box.center(), 1.0f);
    w.shake(0.9f);
    w.flash({255, 255, 255, 255}, 0.35f);
    w.particles().emit("boss_phase", box.center(), 2.5f);
    if (!def_.finalText.empty()) {
        WorldMessage m;
        m.style = WorldMessage::Style::Boss;
        m.text = def_.finalText;
        m.subtitle = def_.name;
        m.duration = 2.6f;
        w.message(m);
    }
    if (!objectId_.empty()) w.emit(objectId_, "bossLastStand");
}

void Enemy::stepLastStand(GameWorld& w, float dt) {
    switch (state_) {
        case EnemyState::PhaseTransition: {
            // To the middle of its arena, a little above (its rings reach every corner).
            Rect arena = arenaArea(w);
            Vec2 spot{arena.center().x, arena.y + arena.h * 0.42f};
            if (def_.flying) moveTowards(w, spot, std::max(def_.speed, 90.0f) * 1.6f, dt);
            else vel.x = approach(vel.x, 0.0f, 900.0f * dt);
            face(playerPos_.x - box.center().x);
            if (stateTimer_ <= 0.0f) state_ = EnemyState::Chase;
            break;
        }
        case EnemyState::Telegraph:
            stepTelegraph(w, dt);
            break;
        case EnemyState::Attack:
            stepAttack(w, dt);
            if (state_ == EnemyState::Recover) stateTimer_ = std::min(stateTimer_, 0.3f);
            break;
        case EnemyState::Recover:
            if (def_.flying) vel = lerpv(vel, {}, damp(4.0f, dt));
            if (stateTimer_ <= 0.0f) state_ = EnemyState::Chase;
            break;
        default:
            // Between two attacks: the next one, or its end.
            if (def_.flying) vel = lerpv(vel, {}, damp(4.0f, dt));
            if (finalIndex_ < def_.finalAttacks.size()) {
                const std::string& id = def_.finalAttacks[finalIndex_++];
                for (size_t i = 0; i < def_.attacks.size(); ++i)
                    if (def_.attacks[i].id == id) {
                        attack_ = &def_.attacks[i];
                        attackIndex_ = static_cast<int>(i);
                        beginAttack(w);
                        break;
                    }
            } else {
                lastStand_ = false;
                kill(w);
            }
            break;
    }
}

}  // namespace cosmic
