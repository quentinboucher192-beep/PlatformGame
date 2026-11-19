#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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

    /// Frozen (power « Nova de givre », 2.19): a non-boss enemy stops for `seconds` (no move,
    /// no attack; its top can be stood on); a boss is only slowed for that time.
    void freeze(float seconds);
    bool frozen() const { return frozenTimer_ > 0.0f; }
    bool slowed() const { return slowTimer_ > 0.0f; }

    /// A double of a guardian (« Reflets », 2.19): it never chooses an attack, it copies
    /// those of `original`; it lives `life` seconds (0 = until a hit breaks it).
    void makeDouble(uint32_t original, float life) {
        mirrorOf_ = original;
        lifeTimer_ = life;
        expires_ = life > 0.0f;
    }
    uint32_t mirrorOf() const { return mirrorOf_; }
    /// Doubles of this guardian still standing (« Reflets »).
    int doubles() const { return doubles_; }
    /// Guardian attacks (2.19): the bouncing path of a prism, the mark of a shadow step, the
    /// radius of an eclipse ring... (tests, display).
    const std::vector<Vec2>& attackPath() const { return path_; }
    /// The second, mirrored beam of a prism with two arms (2.33); empty otherwise.
    const std::vector<Vec2>& attackPath2() const { return path2_; }
    Vec2 attackMark() const { return markPos_; }
    float ringRadius() const { return ringRadius_; }
    float ringGapAngle() const { return angle_; }
    /// The dragon (2.50): landed by its « Atterrissage » (it takes more damage), and the line
    /// of a dive shown during its wind-up: locked once it no longer follows the target.
    bool perched() const { return perched_; }
    bool diveLocked() const { return diveLocked_; }
    bool diving() const { return stage_ == Stage::Dash; }
    /// The golem (2.52, Monolithe): curled in its shell (it takes less damage), rolling, dizzy
    /// after a roll (it takes more), out of sight in its titanic leap (it cannot be hurt), and
    /// the firm floor it walks on (no crust that gives way, no pit).
    bool curled() const { return golem_ == GolemStage::Curl || golem_ == GolemStage::Roll || golem_ == GolemStage::Skid; }
    bool rolling() const { return golem_ == GolemStage::Roll; }
    bool dizzy() const { return golem_ == GolemStage::Dizzy; }
    bool skyborne() const { return golem_ == GolemStage::Rise || golem_ == GolemStage::Hang || golem_ == GolemStage::Fall; }
    bool outOfSight() const { return golem_ == GolemStage::Hang; }
    bool shadowLocked() const { return shadowLocked_; }
    Vec2 shadow() const { return shadow_; }
    float floorLeft() const { return floorL_; }
    float floorRight() const { return floorR_; }
    /// The phoenix (2.56, the Phénix): burnt to ashes on the floor of its arena between two
    /// phases (it cannot be hurt), then reborn (BossPhase::rebirth); the lane of its blade of
    /// feathers (low: jump over it; high: stay down) and the columns of its sun (attackPath).
    bool inAshes() const { return ashes_ != AshStage::None; }
    bool ashesBurning() const { return ashes_ == AshStage::Burn; }
    bool ashesLying() const { return ashes_ == AshStage::Ash; }
    bool rising() const { return ashes_ == AshStage::Rise; }
    Vec2 ashSpot() const { return ashSpot_; }
    bool laneLow() const { return laneLow_; }
    Rect lane() const { return lane_; }
    /// Ash veils the arena (display): in a phase with ashes, or while it lies in its ashes.
    bool ashVeil() const;
    /// The lich (2.57, the Liche): it floats over the altars of its arena (the top of each, by x)
    /// and teleports from one to another (the next one is shown before it appears there); at the
    /// end of a phase with a phylactery it hides its soul in it (the crystal, an enemy set on an
    /// altar): it cannot be hurt until the crystal is broken. Its spells: the marks of its runes
    /// (attackPath), the circle of its curse (attackMark, ringRadius).
    bool isLiche() const { return hasLiche_; }
    const std::vector<Vec2>& altars() const { return altars_; }
    int altarIndex() const { return altar_; }
    int altarNext() const { return altarNext_; }
    bool warded() const { return warded_; }
    uint32_t phylactery() const { return phylactery_; }
    int phylacteryAltar() const { return phylacteryAltar_; }
    /// The curse of its last « Maléfice » reached Nova (tests, statistics of display).
    bool hexLanded() const { return hexLanded_; }
    /// The war machine (2.58, the Titan Mécanique): it walks; each attack it ends heats it (the
    /// gauge of its phase, BossPhase::heat); full, it vents its steam: jets along the ground on both
    /// sides, then its core laid bare (it takes more damage). At the end of a phase with a stall
    /// (BossPhase::stall) it stalls: it cannot be hurt, its pressure valves appear on the valve spots
    /// of its arena, and it restarts after a while (stallLeft); all valves turned before, its boiler
    /// bursts. Its machinery: the presses of its arena (pressSpots), its conveyor belts (beltStrips).
    bool isTitan() const { return hasTitan_; }
    float heat() const { return heat_; }
    bool venting() const { return titan_ == TitanStage::Vent; }
    /// Venting, its jets over: its core is bare (it takes kVentVulnerability times the damage).
    bool coreExposed() const;
    float ventLeft() const { return venting() ? titanTimer_ : 0.0f; }
    int vents() const { return vents_; }
    bool stalled() const { return titan_ == TitanStage::Stall; }
    float stallLeft() const { return stalled() ? titanTimer_ : 0.0f; }
    int valvesTotal() const { return valvesTotal_; }
    int valvesLeft() const;
    const std::vector<uint32_t>& valves() const { return valves_; }
    const std::vector<Vec2>& valveSpots() const { return valveSpots_; }
    /// How its last stall ended: its boiler burst (every valve turned in time).
    bool boilerBurst() const { return boilerBurst_; }
    struct PressSpot {
        float x = 0.0f;     // its middle
        float half = 64.0f;
        float top = 0.0f;   // the bottom of its housing
        float floor = 0.0f; // the surface under it
    };
    const std::vector<PressSpot>& pressSpots() const { return pressSpots_; }
    const std::vector<Rect>& beltStrips() const { return beltStrips_; }
    /// The presses of its last « Presses hydrauliques » (indices in pressSpots), round by round, and
    /// the column spared in a pattern « toutes sauf une » (-1: none).
    const std::vector<std::vector<int>>& pressRounds() const { return pressRounds_; }

    /// 2.68, the gigaboss of a raid (game/raid/Gigaboss.h): rules set from outside, step by step,
    /// by the raid's run; neutral otherwise (a level plays exactly as before). `guard`: nothing
    /// hurts it (its carapace); `mult`: the damage it takes is multiplied (its weak point bare).
    void setRaidGuard(bool guard, float mult) {
        raidGuard_ = guard;
        raidMult_ = mult;
    }
    bool raidGuarded() const { return raidGuard_; }
    float raidMultiplier() const { return raidMult_; }
    /// 2.68: its cooldowns are divided by `p` (more players in the raid, more pressure).
    void setPressure(float p) { pressure_ = p > 0.0f ? p : 1.0f; }
    float pressure() const { return pressure_; }
    /// 2.68: its health, and what is left of it, times `factor` (the players of a raid).
    void scaleHealth(float factor) {
        maxHp_ *= factor;
        hp_ *= factor;
    }
    /// 2.68: health taken at once from outside its own rules (one of its weak points broken).
    void wound(GameWorld& w, float amount);
    /// 2.68: the attack it is winding up or performing (nullptr between two).
    const AttackDef* currentAttack() const { return attack_; }
    /// Projectile / beam origin of the current attack (2.76.2: public, its drawing's when it fights by parts).
    Vec2 attackOrigin() const;

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
    /// Boss fights resumed after the player's death: the boss keeps its wounds and its phase.
    void setWounds(float hp, int phase);
    bool enraged() const { return enraged_; }
    /// Seconds spent in the current phase (fight engaged).
    float phaseTime() const { return phaseTime_; }
    /// Its last stand (2.33, EnemyDefinition::finalAttacks): out of health, it cannot be hurt
    /// and performs its final attacks before dying.
    bool lastStand() const { return lastStand_; }
    /// The sheet of its current phase (EnemyDefinition / BossPhase::animation).
    std::string phaseAnimation() const;
    /// Its light (the current phase's colour when the phase has one).
    Color lightColor() const;
    const std::string& currentAttackName() const;
    std::string debugInfo() const;
    Vec2 center() const { return box.center(); }
    /// Its sprite as drawn now (display: where it shows on screen, Hud::bossOnScreen).
    const Animator& animator() const { return anim_; }

    Rect box;
    Rect prevBox;
    Vec2 vel;
    float facing = 1.0f;
    uint32_t summonedBy = 0;
    bool countsForStats = true;
    /// Dies with its summoner (2.17): no reward, no split.
    bool vanishing = false;

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
    void beginLastStand(GameWorld& w);
    void stepLastStand(GameWorld& w, float dt);
    void refreshLook(GameWorld& w);  // display: the sheet of the current phase
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
    /// 2.76.2: whether it fights by its hit parts (fightsByParts), refreshed at each fixed step: its
    /// shots then leave its drawing (attackOrigin, « origins » of its parts).
    bool byParts_ = false;
    std::string objectId_;
    uint32_t uid_;
    EnemyState state_ = EnemyState::Idle;
    Vec2 origin_;
    // 2.76.0: where its flight began (the rules of 16 keep it within fleeMaxDistance of it).
    Vec2 fleeAnchor_;
    bool fleeAnchored_ = false;
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
    float frozenTimer_ = 0.0f;  // 2.19: Nova de givre (non-boss enemies)
    float slowTimer_ = 0.0f;    // 2.19: Nova de givre (bosses: half speed)
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
    float phaseTime_ = 0.0f;  // 2.33: rage counted in a phase
    bool enraged_ = false;
    bool lastStand_ = false;  // 2.33: « Dernière lumière »
    size_t finalIndex_ = 0;
    int shownLook_ = -1;      // display only: the phase whose sheet is shown
    std::string shownAnim_;
    int burstsLeft_ = 0;
    float burstTimer_ = 0.0f;
    struct Staged {
        Vec2 at;
        float time;
    };
    std::vector<Staged> staged_;  // staggered impacts still to come
    float attackClock_ = 0.0f;
    bool leapLanded_ = false;

    // Guardians of Aethra (2.19): the new attack types, in GuardianAttacks.cpp.
    void guardianBegin(GameWorld& w);   // wind-up: targets, marks, paths
    void guardianStart(GameWorld& w);   // the attack goes off
    void guardianActive(GameWorld& w, float dt);
    void guardianEnd(GameWorld& w);     // the attack is over (or interrupted)
    void renderGuardian(RenderContext& ctx, float prog) const;
    float guardianFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const;
    std::optional<float> groundAt(const GameWorld& w, float x, float fromY) const;
    Rect arenaArea(const GameWorld& w) const;
    void copyToDoubles(GameWorld& w);
    void spawnDoubles(GameWorld& w, const AttackDef& a);
    void fireSpiral(GameWorld& w, const AttackDef& a);
    std::vector<Vec2> path_;   // Prism path, Cage wall bases, Mirror spots
    std::vector<Vec2> path2_;  // the mirrored beam of a two-armed prism (2.33)
    Vec2 markPos_;             // Tracker bump, Blink mark, Well centre, Ring centre
    float spin_ = 1.0f;        // Orbit / Spiral turning direction
    float angle_ = 0.0f;       // Orbit / Spiral angle, Ring opening
    float ringRadius_ = 0.0f;
    bool burst_ = false;       // Tracker: the spike is out
    uint32_t mirrorOf_ = 0;    // a double: the guardian it copies
    float lifeTimer_ = 0.0f;   // a double: seconds left
    bool expires_ = false;     // a double with a life (else until broken)
    int doubles_ = 0;
    bool hasMirror_ = false;

    // The dragon (2.50, Pyrrhax): the new attack types, in DragonAttacks.cpp.
    enum class Stage : uint8_t { None, Aim, Dash, Climb, Return, Descend, Landed, Hold, TakeOff };
    void dragonBegin(GameWorld& w);               // wind-up: the dive's line, the landing mark, the impacts
    void dragonTelegraph(GameWorld& w, float dt);  // the wind-up, step by step
    void dragonStart(GameWorld& w);
    void dragonActive(GameWorld& w, float dt);
    void dragonEnd(GameWorld& w, bool cut = true);  // the attack is over (or cut short)
    void renderDragon(RenderContext& ctx, float prog) const;
    float dragonFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const;
    /// Right under the dragon: how much an attack is worth (only for the bosses with its attacks).
    float belowFit(const AttackDef& a) const;
    float diveSpeed() const;
    void aimDive(GameWorld& w, float eta);  // a new target for the dive, then its line
    void traceDive(GameWorld& w);           // the line from where it is, through the target and beyond
    void startDash(GameWorld& w);
    void chooseLanding(GameWorld& w);
    void land(GameWorld& w);
    void breathe(GameWorld& w);
    void aimClouds(GameWorld& w);
    void fireClouds(GameWorld& w);
    void blowEmbers(GameWorld& w);
    Vec2 hoverSpot(const GameWorld& w) const;
    bool hasDragon_ = false;     // it has the dragon's attacks (it reads a target right under it)
    Stage stage_ = Stage::None;  // Dive / Perch: the step of the attack
    float stageTimer_ = 0.0f;
    int divesLeft_ = 0;
    bool diveLocked_ = false;    // the line holds still: the dash follows it
    Vec2 diveDir_{0.0f, 1.0f};
    float diveLength_ = 0.0f;
    float diveTravel_ = 0.0f;
    Vec2 lastCenter_;
    Vec2 climbSpot_;
    bool perched_ = false;       // landed: it takes more damage
    bool dropThrough_ = false;   // diving or landing: through the one-way ledges

    // The golem (2.52, the Monolithe): its new attack types and a walker's brain, in GolemAttacks.cpp.
    enum class GolemStage : uint8_t { None, Curl, Roll, Skid, Uncurl, Dizzy, Crouch, Rise, Hang, Fall, Land, Quake };
    void golemBegin(GameWorld& w);                // wind-up: the lane of the roll, the shadow, the first quake
    void golemTelegraph(GameWorld& w, float dt);  // the wind-up, step by step
    void golemStart(GameWorld& w);
    void golemActive(GameWorld& w, float dt);
    void golemEnd(GameWorld& w, bool cut = true);  // the attack is over (or cut short)
    void renderGolem(RenderContext& ctx, float prog) const;
    float golemFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const;
    /// Its steps between two attacks: toward the target, back toward the middle of its floor,
    /// never to the edge of a pit; it attacks when placed (or when it has walked enough).
    void golemThink(GameWorld& w, float dt);
    float golemSpot(GameWorld& w, float dt);  // where it wants to stand (and whether it is pressed at an end)
    void golemWalk(float dt, float want, float pace);
    /// The firm floor under it, as far as it stays level on both sides (walls, pits and the crust
    /// that may give way end it); `roll` counts the crust too (the shell rolls over it).
    void refreshFloor(GameWorld& w);
    std::pair<float, float> floorSpan(const GameWorld& w, float x, float y, bool crust) const;
    float walkLo() const;  // where its middle may stand on its floor
    float walkHi() const;
    bool golemBusy() const { return golem_ == GolemStage::Roll || golem_ == GolemStage::Skid || skyborne(); }
    void setBody(const GameWorld& w, Vec2 size);  // its box (curled or standing), on its feet
    void rollLane(GameWorld& w);  // the way the shell will roll (shown during the wind-up)
    void quakeWave(GameWorld& w);
    void landSkyfall(GameWorld& w);
    void shockwaves(GameWorld& w, int ring);
    bool hasGolem_ = false;
    GolemStage golem_ = GolemStage::None;
    float golemTimer_ = 0.0f;
    Vec2 standSize_;              // its standing box (the shell is smaller)
    bool floorKnown_ = false;
    float floorL_ = 0.0f, floorR_ = 0.0f, floorY_ = 0.0f;  // its firm floor
    float rollL_ = 0.0f, rollR_ = 0.0f;                    // where the shell can roll (crust too)
    float floorTimer_ = 0.0f;
    float edgeTime_ = 0.0f;       // pressed at an end of its floor with the target on the inside
    float walkTime_ = 0.0f;       // walking before its next attack
    bool sheltered_ = false;      // a ledge right over the target's head
    float rollDir_ = 1.0f;
    float rollStop_ = 0.0f;       // where the last leg of its roll ends (toward the middle)
    int bouncesLeft_ = 0;
    int ringsLeft_ = 0;           // shockwaves still to send after a titanic landing
    Vec2 shadow_;                 // where it will fall (titanic leap)
    bool shadowLocked_ = false;

    // The phoenix (2.56, the Phénix): its new attack types, its glide and its rebirth, in
    // PhoenixAttacks.cpp.
    enum class AshStage : uint8_t { None, Burn, Ash, Rise };
    void phoenixBegin(GameWorld& w);   // wind-up: the columns' marks, the blade's lane
    void phoenixStart(GameWorld& w);
    void phoenixActive(GameWorld& w, float dt);
    void phoenixEnd(GameWorld& w, bool cut = true);
    void renderPhoenix(RenderContext& ctx, float prog) const;
    void renderAshes(RenderContext& ctx) const;
    float phoenixFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const;
    /// Its glide between two attacks (« vol plané »): wide swoops from one side of the target
    /// to the other, above her, never into a wall, a corner or the roof.
    void phoenixGlide(GameWorld& w, float dt, float pace, bool steer = true);
    bool glidesDuring(const AttackDef& a) const;  // it glides on while it throws this
    void phoenixKeepOff(GameWorld& w);  // a soft push off the walls and the roof of its arena
    void planColumns(GameWorld& w);
    void planLane(GameWorld& w, bool low);
    void throwBlade(GameWorld& w);
    void beginAshes(GameWorld& w, int next);
    void stepAshes(GameWorld& w, float dt);
    void enterPhase(GameWorld& w, int next);  // the roar of a new phase (checkPhase, a rebirth)
    bool hasPhoenix_ = false;
    AshStage ashes_ = AshStage::None;
    float ashTimer_ = 0.0f;
    int ashNext_ = 0;          // the phase it is reborn into
    Vec2 ashSpot_;             // where its ashes lie (on the floor)
    float emberTimer_ = 0.0f;  // embers thrown out of its ashes
    bool laneLow_ = true;      // the blade's lane: low (jump over it) or high (stay down)
    Rect lane_;
    float laneFloor_ = 0.0f;
    int bladesLeft_ = 0;
    float glideClock_ = 0.0f;

    // The lich (2.57, the Liche): its spells (runes, curse), its altars and its phylactery, in
    // LicheAttacks.cpp.
    void licheBegin(GameWorld& w);  // wind-up: the marks of the runes, the circle of the curse
    void licheStart(GameWorld& w);
    void licheActive(GameWorld& w, float dt);
    void licheEnd(GameWorld& w, bool cut = true);
    void renderLiche(RenderContext& ctx, float prog) const;
    void renderAltarFx(RenderContext& ctx) const;  // its altar, the next one, its phylactery's thread
    float licheFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const;
    /// Between two spells: over its altar, or away to another one (after a few spells there, or
    /// when Nova stays right against it).
    void licheThink(GameWorld& w, float dt);
    void licheHover(GameWorld& w, float dt);
    void licheBlink(GameWorld& w, float dt);  // its teleport (TeleportOut, then TeleportIn)
    void findAltars(GameWorld& w);
    Vec2 altarSpot(int k) const;  // where its feet float over altar k
    int chooseAltar(GameWorld& w) const;
    void beginTeleport(GameWorld& w, int k);
    void planRunes(GameWorld& w);
    void beginWard(GameWorld& w, int next);
    void stepWard(GameWorld& w);
    bool hasLiche_ = false;
    std::vector<Vec2> altars_;  // the top of each altar of its arena (by x); spots of its floor without one
    bool altarsKnown_ = false;
    int altar_ = -1;            // the altar it floats over
    int altarNext_ = -1;        // the one it teleports to (shown)
    int castsHere_ = 0;
    bool teleportDue_ = false;
    float closeTime_ = 0.0f;    // Nova right against it
    bool warded_ = false;       // its soul in its phylactery: it cannot be hurt
    int wardNext_ = 0;
    uint32_t phylactery_ = 0;   // the crystal (its uid)
    int phylacteryAltar_ = -1;
    bool hexLanded_ = false;

    // The war machine (2.58, the Titan Mécanique): its machinery (presses, missiles, belts), its
    // heat gauge and its vents, its stall and its valves, in TitanAttacks.cpp.
    enum class TitanStage : uint8_t { None, Vent, Stall };
    void titanBegin(GameWorld& w);  // wind-up: which presses, the reticles' launch
    void titanStart(GameWorld& w);
    void titanActive(GameWorld& w, float dt);
    void titanEnd(GameWorld& w, bool cut = true);
    void renderTitan(RenderContext& ctx, float prog) const;
    void renderTitanFx(RenderContext& ctx) const;  // its heat gauge, its bare core, its stall
    float titanFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const;
    /// A walker's brain (the golem's steps, GolemAttacks.cpp); venting or stalled, it stands.
    void titanThink(GameWorld& w, float dt);
    void findMachinery(GameWorld& w);  // the presses, the belts and the valve spots of its arena
    void planPresses(GameWorld& w);
    void titanAfterAttack(GameWorld& w);  // the heat of an attack; full, it vents
    void beginVent(GameWorld& w);
    void stepVent(GameWorld& w, float dt);
    void beginStall(GameWorld& w, int next);
    void stepStall(GameWorld& w, float dt);
    void endStall(GameWorld& w, bool burst);
    void clearValves(GameWorld& w);
    bool hasTitan_ = false;
    TitanStage titan_ = TitanStage::None;
    float titanTimer_ = 0.0f;   // venting: seconds left; stalled: seconds before it restarts
    float heat_ = 0.0f;
    int vents_ = 0;
    bool machineryKnown_ = false;
    std::vector<PressSpot> pressSpots_;
    std::vector<Rect> beltStrips_;
    std::vector<Vec2> valveSpots_;
    std::vector<uint32_t> valves_;
    int valvesTotal_ = 0;
    int valvesLeft_ = 0;
    int stallNext_ = 0;
    bool boilerBurst_ = false;
    std::vector<std::vector<int>> pressRounds_;

    // 2.68: the gigaboss of a raid (setRaidGuard, setPressure).
    bool raidGuard_ = false;
    float raidMult_ = 1.0f;
    float pressure_ = 1.0f;
};

}  // namespace cosmic
