#pragma once

#include <cstdint>
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
    Crouch,  // 2.17: Down on the ground, even standing still (smaller hitbox to dodge)
};
const char* playerStateName(PlayerState s);

/// 2.71 (simulation 15): the direction of the basic attack, chosen when it starts: Up (Up
/// alone, on the ground or in the air), Down (Down alone, in the air only: on the ground Down
/// stays the crouch) or Forward.
enum class SlashDirection : uint8_t { Forward, Up, Down };
/// The first simulation where the attack keeps the aim it started with (its direction, and the
/// bounce of a strike downwards in the air: GameWorld::simulationVersion; a replay recorded
/// before plays with the rules of its time: the aim read at the moment of the hit).
constexpr int kAimedSlashSimulation = 15;
/// 2.90 (simulation 18): after a dash the player stays out of reach of the enemies' blows (their
/// strikes, shots, beams and contact) a little longer: its duration + kDashBlowGuardSeconds (0.31 s
/// for the dash of 0.16 s, 0.21 s before; the dash itself is not longer). The hazards of a level
/// (spikes) keep the guard of before (+0.05 s), so no route through a level changes. A replay
/// recorded before plays with the rules of its time.
constexpr int kLongDashGuardSimulation = 18;
constexpr float kDashGuardSeconds = 0.05f;
constexpr float kDashBlowGuardSeconds = 0.15f;

/// The player character: state machine, precise platforming physics, melee combat,
/// powers, health / energy and animation selection.
class Player {
public:
    void configure(const PlayerConfig& cfg, AnimationSetPtr anim);
    /// Places the player with its feet at `feet`. `materialize` plays the respawn effect.
    void spawn(GameWorld& w, Vec2 feet, bool faceLeft, bool materialize);

    void fixedUpdate(GameWorld& w, const PlayerInput& in, float dt);
    /// 2.92 (simulation 19): a step while the level's start is held (GameWorld::startHeld): she only
    /// appears (her materialisation goes on, then she waits as she appeared, standing: her pose at
    /// rest); nothing else of hers moves or counts down, so that she is the same when the clock
    /// starts, whether she appeared during the intro or after it was skipped.
    void heldStep(float dt);
    /// Display only (each image): the clip of its state, its frames advancing. Every player drawn needs
    /// it, a team's other players included (GameWorld::update).
    void update(GameWorld& w, float dt);
    void render(RenderContext& ctx) const;
    /// The dash afterimages alive (display only): each fades in kAfterimageLife seconds of the game's
    /// time (fixedUpdate), never more than kMaxAfterimages of them.
    static constexpr float kAfterimageLife = 0.25f;
    static constexpr size_t kMaxAfterimages = 10;
    size_t afterimageCount() const { return afterimages_.size(); }

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
    float hurtCarry = 0.0f;  // titan 13: the fraction of a blow not taken yet (GameWorld::hurtScale)
    float energy = 100.0f;

    // -------------------------------------------------- damage / heal
    /// Returns true if the damage was taken.
    bool hurt(GameWorld& w, const DamageInfo& dmg);
    void kill(GameWorld& w);
    void heal(int amount) { health = std::min(maxHealth(), health + amount); }
    /// Hearts: configured maximum plus the heart containers collected in this level.
    int maxHealth() const { return cfg_.maxHealth + bonusHealth; }
    int bonusHealth = 0;
    void addEnergy(float amount) { energy = std::min(cfg_.maxEnergy, energy + amount); }
    bool spendEnergy(float amount);

    // --------------------------------------------------------- powers
    /// Keys of the powers (rule of the run, set before the start powers are granted):
    /// true since 2.24, each slot has its key (Action::PowerSlot1..8) and uses the power
    /// carried there; false for the replays recorded before, where each power had its own
    /// button (PowerDefinition::input) and two powers on the same button could not be
    /// carried together.
    void setKeysPerSlot(bool on);
    bool keysPerSlot() const { return keysPerSlot_; }
    /// Grants a power outright (start powers, scripted gifts, tests): the same power, or
    /// (keys per power) a power on the same exclusive input, is replaced; the slot limit is
    /// not checked.
    void grantPower(const PowerDefinition& def);
    /// Result of offering a power (tablet picked up).
    enum class Offer { Granted, AlreadyOwned, NeedsChoice };
    /// Grants the power if a slot is free and (keys per power) no carried power uses the
    /// same exclusive input; otherwise the player must choose (replace a power or leave the
    /// new one).
    Offer offerPower(const PowerDefinition& def);
    /// Index of the carried power the new one would have to replace because they share an
    /// exclusive input (-1 = none: any slot can be replaced; always -1 with keys per slot).
    int conflictingPower(const PowerDefinition& def) const;
    /// Replaces the power in `slot` by `def` and returns the id of the power removed.
    std::string replacePower(int slot, const PowerDefinition& def);
    /// Removes a carried power (tests, scripted levels). Returns false if not carried.
    bool removePower(const std::string& id);
    /// Powers the player can carry at once (0 = no limit), set by the level.
    void setPowerSlots(int slots) { powerSlots_ = std::max(0, slots); }
    int powerSlots() const { return powerSlots_; }
    void refreshPassivePowers();
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
    void restoreAirJumps() { airJumpsLeft_ = airJumpCount(); }
    /// Air jumps available after leaving the ground (double jump + upgrades).
    int airJumpCount() const { return cfg_.airJumps + extraAirJumps_; }
    int airJumpsLeft() const { return airJumpsLeft_; }
    /// Moves the box with the current velocity (used by powers that drive the movement).
    void physicsMove(GameWorld& w, float dt) { moveAndCollide(w, dt); }
    /// The power currently driving the movement (ground pound, grapple), if any.
    Power* movementPower() const;
    /// Glide (set every step by the glide power): capped fall speed and air speed.
    float glideFallSpeed = 0.0f;
    float glideAirSpeed = 0.0f;
    bool gliding() const { return glideFallSpeed > 0.0f; }
    /// Temporary slow (ice attacks, webs).
    void applySlow(float multiplier, float seconds);
    bool slowed() const { return slowTimer_ > 0.0f; }
    /// 2.57, the Liche's « Maléfice »: for `seconds`, her jumps (from the ground, in the air, off a
    /// wall) reach only `heightRatio` of their height; the stronger curse and the longer time hold.
    void applyCurse(float heightRatio, float seconds);
    bool cursed() const { return curseTimer_ > 0.0f; }
    float curseTime() const { return curseTimer_; }
    float curseRatio() const { return curseTimer_ > 0.0f ? curseRatio_ : 1.0f; }
    void setShield(bool on) { shield_ = on; }
    bool shieldActive() const { return shield_; }
    void setCastPose(float seconds) { castTimer_ = seconds; }
    void teleportTo(Vec2 feet);
    void setInvulnerable(float seconds) { invulnTimer_ = std::max(invulnTimer_, seconds); }
    /// 2.90: out of reach of the enemies' blows only (not of the level's hazards) for `seconds`.
    void setBlowGuard(float seconds) { blowGuardTimer_ = std::max(blowGuardTimer_, seconds); }
    bool blowGuarded() const { return blowGuardTimer_ > 0.0f; }
    void setPhased(float seconds) { phasedTimer_ = seconds; }
    void launch(Vec2 velocity, bool resetJumps);
    MoveOptions moveOptions() const;
    Vec2 aimDirection() const { return aim_; }
    /// 2.71: the attack under way (attacking()) and its direction (display, tests).
    bool attacking() const { return attackTimer_ > 0.0f; }
    SlashDirection slashDirection() const { return slashDir_; }
    /// The direction an attack started now would take (Up alone, Down alone in the air).
    SlashDirection slashDirectionFor(Vec2 aim, bool grounded) const;
    /// The area an attack in `dir` strikes, from the player's box now.
    Rect slashArea(SlashDirection dir) const;

    // ------------------------------------------------- environment (set by the world each step)
    bool onLadder = false;
    Rect ladderRect;
    bool inWater = false;
    float speedMultiplier = 1.0f;
    float updraft = 0.0f;         // upward wind speed of the updraft zone the player is in
    float groundFriction = 1.0f;  // friction of the material under the feet (ice < 1)

    // ------------------------------------------------------- control
    bool controlEnabled = true;
    void setCinematicWalk(float speed) { cinematicWalk_ = speed; }
    void setHidden(bool hidden);
    void startRespawnAnimation();
    void startVictory();
    void forceAnimation(const std::string& clip, float seconds);
    bool respawning() const { return state_ == PlayerState::Respawn; }

    /// 2.79 (raids: the simulated players' lookahead, game/raid/nav/BodySim): the private state of the
    /// movement, read only. A light copy of the physics plays a few steps ahead from it; nothing here
    /// changes the player.
    struct Motion {
        PlayerState state = PlayerState::Idle;
        bool grounded = false, jumpCut = false, crouchBox = false;
        uint32_t groundId = 0;
        int airJumpsLeft = 0, extraAirJumps = 0, wallDir = 0;
        float coyote = 0.0f, jumpBuffer = 0.0f, wallJumpLock = 0.0f, slideTimer = 0.0f, dropThrough = 0.0f;
        float attackTimer = 0.0f, attackCooldown = 0.0f, invuln = 0.0f, hurtTimer = 0.0f, phased = 0.0f;
        float slowTimer = 0.0f, slowMultiplier = 1.0f, curseTimer = 0.0f, curseRatio = 1.0f, energyRegenTimer = 0.0f;
        Vec2 dashDir;
        float dashSpeed = 0.0f, dashTimer = 0.0f;
    };
    Motion motion() const;

    // ---------------------------------------------------- animation
    Animator anim;
    /// Suit (2.19 cosmetics): a recoloured copy of the animation set's sheet, same layout.
    /// Display only, never read by the simulation.
    const Texture* skin = nullptr;
    float hurtFlash() const { return hurtFlash_; }

private:
    void stepMovement(GameWorld& w, const PlayerInput& in, float dt);
    void stepClimb(GameWorld& w, const PlayerInput& in, float dt);
    void stepSlide(GameWorld& w, const PlayerInput& in, float dt);
    /// Size of the crouching hitbox (same width, a bit more than half the height).
    Vec2 crouchSize() const;
    bool crouchBox_ = false;  // the hitbox is the crouching one
    void tryJump(GameWorld& w, const PlayerInput& in);
    void handlePowers(GameWorld& w, const PlayerInput& in, float dt);
    /// Gives each carried power its key (slot key or own input), after any change.
    void assignPowerKeys();
    bool sharesExclusiveInput(const Power& owned, const PowerDefinition& def, const Power& created) const;
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
    int extraAirJumps_ = 0;
    int powerSlots_ = 0;
    bool keysPerSlot_ = false;
    float slowTimer_ = 0.0f;
    float slowMultiplier_ = 1.0f;
    float curseTimer_ = 0.0f;  // 2.57: the Liche's « Maléfice »
    float curseRatio_ = 1.0f;
    float curseSpeed() const;  // the factor of her jump speeds while cursed (sqrt of the height ratio)
    float wallJumpLockTimer_ = 0.0f;
    int wallDir_ = 0;
    float slideTimer_ = 0.0f;
    float dropThroughTimer_ = 0.0f;
    float invulnTimer_ = 0.0f;
    float blowGuardTimer_ = 0.0f;  // 2.90: setBlowGuard
    float hurtTimer_ = 0.0f;
    float hurtFlash_ = 0.0f;
    float deathTimer_ = 0.0f;
    float respawnTimer_ = 0.0f;
    float energyRegenTimer_ = 0.0f;
    float attackTimer_ = 0.0f;
    float attackCooldown_ = 0.0f;
    bool attackHitDone_ = false;
    uint32_t meleeTag_ = 0;  // 2.54, statistics only: the tag of the last swing (GameWorld::newAttackTag)
    bool attackAir_ = false;
    SlashDirection slashDir_ = SlashDirection::Forward;  // 2.71: chosen when the attack starts
    Vec2 slashAim_{1, 0};                                 // 2.71: the aim held when it started
    /// The clip of the attack under way: its direction when the sheet has it (2.71).
    const char* attackClip() const;
    float castTimer_ = 0.0f;
    float interactTimer_ = 0.0f;
    float landTimer_ = 0.0f;
    float doubleJumpAnimTimer_ = 0.0f;
    float forcedAnimTimer_ = 0.0f;
    std::string forcedAnim_;
    float cinematicWalk_ = 0.0f;
    bool heldStill_ = false;  // 2.92, display only: held at the level's start (heldStep), shown standing
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
