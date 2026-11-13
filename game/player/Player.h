#pragma once

#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/input/InputManager.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/gameplay/Damage.h"
#include "game/player/PlayerConfig.h"
#include "game/powers/Power.h"

namespace cosmic {

class GameWorld;
struct RenderContext;

enum class PlayerState {
    Idle,
    Run,
    Jump,
    Fall,
    WallSlide,
    Climb,
    Slide,
    Dash,
    Swim,
    Hurt,
    Dead,
    Respawn,
    Victory,
    Hidden,
};
const char* playerStateName(PlayerState s);

/// The player character: state machine, precise platforming physics, melee combat,
/// powers, health / energy and animation selection.
class Player {
public:
    void configure(const PlayerConfig& cfg, AnimationSetPtr anim);
    /// Places the player with its feet at `feet`. `materialize` plays the respawn effect.
    void spawn(GameWorld& w, Vec2 feet, bool faceLeft, bool materialize);

    void fixedUpdate(GameWorld& w, const PlayerInput& in, float dt);
    void update(GameWorld& w, float dt);
    void render(RenderContext& ctx) const;

    // ----------------------------------------------------------- state
    PlayerState state() const { return state_; }
    const PlayerConfig& config() const { return cfg_; }
    bool grounded() const { return grounded_; }
    uint32_t groundCollider() const { return groundId_; }
    bool dead() const { return state_ == PlayerState::Dead; }
    bool alive() const { return state_ != PlayerState::Dead && state_ != PlayerState::Hidden; }
    bool invulnerable() const;
    Vec2 center() const { return box.center(); }
    Vec2 feet() const { return box.bottomCenter(); }
    Vec2 renderFeet(float alpha) const { return lerpv(prevBox.bottomCenter(), box.bottomCenter(), alpha); }

    Rect box;
    Rect prevBox;
    Vec2 vel;
    float facing = 1.0f;
    int health = 5;
    float energy = 100.0f;

    // -------------------------------------------------- damage / heal
    /// Returns true if the damage was taken.
    bool hurt(GameWorld& w, const DamageInfo& dmg);
    void kill(GameWorld& w);
    void heal(int amount) { health = std::min(cfg_.maxHealth, health + amount); }
    void addEnergy(float amount) { energy = std::min(cfg_.maxEnergy, energy + amount); }
    bool spendEnergy(float amount);

    // --------------------------------------------------------- powers
    void grantPower(const PowerDefinition& def);
    bool hasPower(const std::string& id) const;
    Power* power(const std::string& id);
    const std::vector<std::unique_ptr<Power>>& powers() const { return powers_; }
    void clearPowers() { powers_.clear(); }
    const std::set<std::string>& powersUsed() const { return powersUsed_; }

    // Used by powers
    void beginDash(Vec2 dir, float speed, float duration);
    void endDash();
    bool dashing() const { return state_ == PlayerState::Dash; }
    Vec2 dashDirection() const { return dashDir_; }
    void restoreAirJumps() { airJumpsLeft_ = cfg_.airJumps; }
    void setShield(bool on) { shield_ = on; }
    bool shieldActive() const { return shield_; }
    void setCastPose(float seconds) { castTimer_ = seconds; }
    void teleportTo(Vec2 feet);
    void setInvulnerable(float seconds) { invulnTimer_ = std::max(invulnTimer_, seconds); }
    void setPhased(float seconds) { phasedTimer_ = seconds; }
    void launch(Vec2 velocity, bool resetJumps);
    MoveOptions moveOptions() const;
    Vec2 aimDirection() const { return aim_; }

    // ------------------------------------------------- environment (set by the world each step)
    bool onLadder = false;
    Rect ladderRect;
    bool inWater = false;
    float speedMultiplier = 1.0f;

    // ------------------------------------------------------- control
    bool controlEnabled = true;
    void setCinematicWalk(float speed) { cinematicWalk_ = speed; }
    void setHidden(bool hidden);
    void startRespawnAnimation();
    void startVictory();
    void forceAnimation(const std::string& clip, float seconds);
    bool respawning() const { return state_ == PlayerState::Respawn; }

    // ---------------------------------------------------- animation
    Animator anim;
    float hurtFlash() const { return hurtFlash_; }

private:
    void stepMovement(GameWorld& w, const PlayerInput& in, float dt);
    void stepClimb(GameWorld& w, const PlayerInput& in, float dt);
    void stepSlide(GameWorld& w, const PlayerInput& in, float dt);
    void tryJump(GameWorld& w, const PlayerInput& in);
    void handlePowers(GameWorld& w, const PlayerInput& in);
    void handleAttack(GameWorld& w, const PlayerInput& in, float dt);
    void handleInteract(GameWorld& w, const PlayerInput& in);
    void moveAndCollide(GameWorld& w, float dt);
    void onLanded(GameWorld& w, float impactSpeed);
    void setHitbox(Vec2 size);
    int wallDirection(const GameWorld& w) const;
    void chooseAnimation();
    void onAnimEvent(GameWorld& w, const std::string& e);

    PlayerConfig cfg_;
    PlayerState state_ = PlayerState::Idle;
    bool grounded_ = false;
    bool wasGrounded_ = false;
    uint32_t groundId_ = 0;
    float coyoteTimer_ = 0.0f;
    float jumpBufferTimer_ = 0.0f;
    bool jumpCutAvailable_ = false;
    int airJumpsLeft_ = 1;
    float wallJumpLockTimer_ = 0.0f;
    int wallDir_ = 0;
    float slideTimer_ = 0.0f;
    float dropThroughTimer_ = 0.0f;
    float invulnTimer_ = 0.0f;
    float hurtTimer_ = 0.0f;
    float hurtFlash_ = 0.0f;
    float deathTimer_ = 0.0f;
    float respawnTimer_ = 0.0f;
    float energyRegenTimer_ = 0.0f;
    float attackTimer_ = 0.0f;
    float attackCooldown_ = 0.0f;
    bool attackHitDone_ = false;
    bool attackAir_ = false;
    float castTimer_ = 0.0f;
    float interactTimer_ = 0.0f;
    float landTimer_ = 0.0f;
    float doubleJumpAnimTimer_ = 0.0f;
    float forcedAnimTimer_ = 0.0f;
    std::string forcedAnim_;
    float cinematicWalk_ = 0.0f;
    float phasedTimer_ = 0.0f;
    float runDustTimer_ = 0.0f;
    bool footstepAlt_ = false;
    bool shield_ = false;
    Vec2 aim_{1, 0};

    // Dash
    Vec2 dashDir_;
    float dashSpeed_ = 0.0f;
    float dashTimer_ = 0.0f;
    struct Afterimage {
        Vec2 feet;
        Rect src;
        bool flip;
        float life;
    };
    std::deque<Afterimage> afterimages_;
    float afterimageTimer_ = 0.0f;

    std::vector<std::unique_ptr<Power>> powers_;
    std::set<std::string> powersUsed_;
    GameWorld* world_ = nullptr;  // for animation event callbacks
};

}  // namespace cosmic
