#pragma once

#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/core/Math.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/Damage.h"

namespace cosmic {

class GameWorld;
struct RenderContext;

enum class EnemyState {
    Dormant,
    Idle,
    Patrol,
    Alert,
    Chase,
    Telegraph,
    Attack,
    Recover,
    Defend,
    Flee,
    Hurt,
    TeleportOut,
    TeleportIn,
    PhaseTransition,
    Dead,
};
const char* enemyStateName(EnemyState s);

/// Runtime enemy driven by an EnemyDefinition: perception (range + line of sight),
/// movement behaviours, telegraphed attacks, special powers (teleport, guard,
/// reinforcements) and, for mini-bosses, multi-phase patterns.
class Enemy {
public:
    Enemy(const EnemyDefinition& def, AnimationSetPtr anim, Vec2 feet, bool faceLeft, std::string objectId,
          float patrolRange, uint32_t uid);

    void fixedUpdate(GameWorld& w, float dt);
    void update(GameWorld& w, float dt);
    void render(RenderContext& ctx) const;
    /// Telegraph overlays (warnings, laser sights, impact markers).
    void renderTelegraph(RenderContext& ctx) const;

    bool takeDamage(GameWorld& w, const DamageInfo& dmg);
    void wake(GameWorld& w);
    void alert(GameWorld& w);
    void kill(GameWorld& w);
    /// Silent removal (arena reset): no death effect, no reward.
    void despawn() {
        state_ = EnemyState::Dead;
        deathTimer_ = 0.0f;
    }
    void setDormant() { state_ = EnemyState::Dormant; engaged_ = false; }
    /// Editor test: performs the given attack now (telegraph included).
    bool forceAttack(GameWorld& w, const std::string& attackId);
    /// Editor preview: plays a clip directly.
    void previewClip(const std::string& clip) { playClip(clip, true); }

    bool alive() const { return state_ != EnemyState::Dead; }
    bool dormant() const { return state_ == EnemyState::Dormant; }
    bool removable() const { return state_ == EnemyState::Dead && deathTimer_ <= 0.0f; }
    bool engaged() const { return engaged_; }
    EnemyState state() const { return state_; }
    const EnemyDefinition& def() const { return def_; }
    const std::string& objectId() const { return objectId_; }
    uint32_t uid() const { return uid_; }
    float hpRatio() const { return maxHp_ > 0 ? hp_ / maxHp_ : 0.0f; }
    float hp() const { return hp_; }
    float maxHp() const { return maxHp_; }
    int phaseIndex() const { return phase_; }
    bool enraged() const { return enraged_; }
    const std::string& currentAttackName() const;
    std::string debugInfo() const;
    Vec2 center() const { return box.center(); }

    Rect box;
    Rect prevBox;
    Vec2 vel;
    float facing = 1.0f;
    uint32_t summonedBy = 0;
    bool countsForStats = true;

private:
    // AI
    void sense(GameWorld& w);
    void stepIdle(GameWorld& w, float dt);
    void stepEngaged(GameWorld& w, float dt);
    void stepTelegraph(GameWorld& w, float dt);
    void stepAttack(GameWorld& w, float dt);
    void moveTowards(GameWorld& w, Vec2 target, float speed, float dt);
    bool chooseAttack(GameWorld& w);
    void beginAttack(GameWorld& w);
    void performAttackStart(GameWorld& w);
    void performAttackActive(GameWorld& w, float dt);
    void finishAttack(GameWorld& w);
    void tryTeleport(GameWorld& w);
    void checkPhase(GameWorld& w);
    void applyPhysics(GameWorld& w, float dt);
    void contactDamage(GameWorld& w);
    bool groundAhead(const GameWorld& w) const;
    void face(float dir) { if (dir != 0.0f) facing = signf(dir); }
    float phaseSpeed() const;
    float phaseCooldown() const;
    std::vector<const AttackDef*> availableAttacks() const;
    void playClip(const std::string& clip, bool restart = false);
    void onAnimEvent(GameWorld& w, const std::string& e);

    EnemyDefinition def_;
    Animator anim_;
    std::string objectId_;
    uint32_t uid_;
    EnemyState state_ = EnemyState::Idle;
    Vec2 origin_;
    float patrolRange_ = 96.0f;
    float hp_ = 1.0f, maxHp_ = 1.0f;
    bool grounded_ = false;
    bool engaged_ = false;
    bool playerVisible_ = false;
    float playerDist_ = 1e9f;
    Vec2 playerPos_;
    float lostTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    float idleTurnTimer_ = 0.0f;
    float hopTimer_ = 0.0f;
    float hurtFlash_ = 0.0f;
    float invuln_ = 0.0f;
    float deathTimer_ = 0.0f;
    float teleportTimer_ = 0.0f;
    float defendTimer_ = 0.0f;
    float defendCooldown_ = 0.0f;
    float contactTimer_ = 0.0f;
    float hoverPhase_ = 0.0f;
    std::vector<float> attackCooldowns_;
    const AttackDef* attack_ = nullptr;
    int attackIndex_ = -1;
    bool attackStarted_ = false;
    bool attackHit_ = false;
    Vec2 aimDir_{1, 0};
    float laserAngle_ = 0.0f;
    float laserHitTimer_ = 0.0f;
    std::vector<Vec2> targets_;  // rain / eruption impact points chosen during the telegraph
    float warnTimer_ = 0.0f;
    Vec2 attackOrigin() const;   // projectile / beam origin of the current attack
    float chargeDistance_ = 0.0f;
    int phase_ = 0;
    int patternIndex_ = 0;
    Vec2 teleportTarget_;

    // Boss brain (v3): prediction, variety, anti-camping, combos, enrage.
    Vec2 predictPlayer(const GameWorld& w, float seconds) const;
    float telegraphScale() const;
    bool chooseBossAttack(GameWorld& w, const std::vector<const AttackDef*>& list);
    void fireProjectiles(GameWorld& w, const AttackDef& a);
    void spawnImpact(GameWorld& w, const AttackDef& a, Vec2 t);
    void slam(GameWorld& w, const AttackDef& a);
    float simDt_ = 1.0f / 120.0f;
    Vec2 playerVel_;
    Vec2 lastPlayerPos_;
    bool havePlayerPos_ = false;
    bool playerAirborne_ = false;
    float outOfReach_ = 0.0f;
    int comboLeft_ = 0;
    int lastAttack_ = -1, prevAttack_ = -1;
    float fightTime_ = 0.0f;
    bool enraged_ = false;
    int burstsLeft_ = 0;
    float burstTimer_ = 0.0f;
    struct Staged {
        Vec2 at;
        float time;
    };
    std::vector<Staged> staged_;  // staggered impacts still to come
    float attackClock_ = 0.0f;
    bool leapLanded_ = false;
};

}  // namespace cosmic
