#pragma once

#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/camera/Camera.h"
#include "game/gameplay/Entity.h"
#include "game/levels/DecorAtlas.h"

namespace cosmic {

class Enemy;
struct PowerDefinition;

/// Helper: loads an animation set once and plays clips.
struct AnimatedSprite {
    AnimationSetPtr set;
    Animator anim;
    void load(ResourceManager& res, const std::string& path, const std::string& clip);
    void draw(RenderContext& ctx, Vec2 anchor, bool flip = false, Color tint = Color::white(), float angle = 0.0f,
              BlendMode blend = BlendMode::Alpha) const;
};

// ================================================================ platforms

class MovingPlatformEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    Vec2 lastDelta() const { return delta_; }
    uint32_t colliderId() const { return collider_; }
    std::string debugInfo() const override;

private:
    uint32_t collider_ = 0;
    Vec2 start_, end_, delta_, prevPos_;
    float t_ = 0.0f, dir_ = 1.0f, wait_ = 0.0f, length_ = 1.0f;
    bool moving_ = true;
    bool triggered_ = false;
    // 2.33: in orbit around its starting place (an ellipse of radii |dx|, |dy|).
    bool orbit_ = false;
    Vec2 center_;
    float angle_ = 0.0f;
    TexturePtr tex_;
    Vec2 orbitPos(float angle) const;
    // 2.64: a carrying bubble (object type « bubble »): it waits where it was born, leaves when
    // Nova stands on it, goes once along its route, pops at the end (its collider off) and is
    // born again at its place `respawn` seconds later.
    bool bubble_ = false;
    bool popped_ = false;
    float reborn_ = 0.0f;   // seconds until it is born again (popped)
    float shown_ = 1.0f;    // display only: 0 → 1 as it grows back, the burst after a pop
    float burst_ = 0.0f;
    Vec2 burstAt_;
    void bubbleStep(GameWorld& w, float dt);
    void renderBubble(RenderContext& ctx) const;

public:
    bool isBubble() const { return bubble_; }
    bool popped() const { return popped_; }
    void update(GameWorld& w, float dt) override;
};

class CrumblePlatformEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    void onPlayerRespawn(GameWorld& w) override;
    /// 2.33: "break" (or "activate", "open"): it gives way at once, by an event.
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;

private:
    void giveWay(GameWorld& w);
    /// 2.56, style « ember »: a perch of embers of the Phénix's nest; it catches fire under Nova,
    /// burns to ash, then is reborn from it (display only: the same simulation as any other).
    void renderEmber(RenderContext& ctx) const;
    enum class Phase { Solid, Shaking, Fallen } phase_ = Phase::Solid;
    uint32_t collider_ = 0;
    float timer_ = 0.0f;
    TexturePtr tex_;
    bool ember_ = false;
};

/// 2.57: an altar of the Liche's crypt (« Autel de la Liche »): a block of carved stone whose top
/// can be stood on (a ledge); the Liche floats over the altars of its arena and teleports between
/// them (game/enemies/LicheAttacks.cpp). Drawn by the game: its runes take the light of the boss's
/// phase and glow when the Liche floats over it, or is about to appear there.
class AltarEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;

private:
    uint32_t collider_ = 0;
    Color color_{170, 120, 255, 255};  // display: the soul fire of its runes
    float glow_ = 0.0f;                // display: how bright they burn (0 .. 1.5)
    bool crystal_ = false;             // display: it holds the phylactery
};

/// 2.58, the machinery of the Titan's foundry, drawn by the game (game/enemies/TitanAttacks.cpp
/// drives it through BossHazards):
///  * a conveyor belt (« Tapis roulant »): a strip of the floor (its rect: the top of the floor);
///    while the Titan runs its belts it carries Nova along, its arrows showing which way;
///  * a press (« Presse hydraulique »): its housing in the roof (its rect); its head and its rod come
///    down onto the surface under it when the Titan sets it off;
///  * a valve spot (« Vanne de pression »): a pipe and its flange on a wall or a ledge; when the Titan
///    stalls, a valve (an enemy to turn by hitting it) appears there.
class ConveyorEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;

private:
    float scroll_ = 0.0f;   // display: how far its slats have moved
    float speed_ = 0.0f;    // display: its speed now (px/s, signed)
    float warn_ = 0.0f;     // display: about to start (its arrows flash)
    Color color_{255, 194, 60, 255};
};

class PressEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;

private:
    bool busy_ = false;     // display: the Titan's press there is out (it draws the head)
    float hiss_ = 0.0f;
    Color color_{255, 194, 60, 255};
};

class ValveSpotEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;

private:
    float glow_ = 0.0f;     // display: a valve waits there to be turned
    Color color_{255, 194, 60, 255};
};

class PowerGateEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool onPowerHit(GameWorld& w, const std::string& power, const Rect& area) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    void onPlayerRespawn(GameWorld& w) override;

private:
    void destroy(GameWorld& w);
    std::string power_;
    uint32_t collider_ = 0;
    bool broken_ = false;
    float crack_ = 0.0f;
    float hintCooldown_ = 0.0f;
    AnimatedSprite sprite_;
    TexturePtr beam_, icons_;
};

class BouncePadEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerStay(GameWorld& w, float dt) override;

private:
    AnimatedSprite sprite_;
    float cooldown_ = 0.0f;
};

class LadderEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerStay(GameWorld& w, float dt) override;

private:
    const DecorSprite* vine_ = nullptr;
    TexturePtr tex_;
};

// ================================================================== hazards

class SpikesEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    Rect overlapBox() const override { return hitbox_; }
    void onPlayerStay(GameWorld& w, float dt) override;

private:
    Rect hitbox_;
    TexturePtr tex_;
};

class LiquidEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    Rect overlapBox() const override { return {box.x, box.y + 5, box.w, box.h - 5}; }
    void onPlayerStay(GameWorld& w, float dt) override;
    void onPlayerEnter(GameWorld& w) override;

    bool swimmable() const;

private:
    std::string kind_;
    TexturePtr tex_;
    float bubbleTimer_ = 0.0f;
};

class DamageZoneEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerStay(GameWorld& w, float dt) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    /// 2.72, style « magma » (the raids' rising magma, docs/RAIDS.md §15): it rises from the bottom
    /// of its box when switched on (`rise` seconds) and drains when switched off (`fall`); only the
    /// part under its surface hurts, the players as before and every enemy caught in it alike
    /// (not the bosses). Drawn over the players and the enemies: nothing shows through it, a
    /// glow at its surface above what it hides. « warn » (sent `warn` s before a rise) shows its
    /// telegraph: the level it will reach, glowing cracks, heat.
    Rect overlapBox() const override;
    bool drawnOverActors() const override { return magma_; }
    bool magma() const { return magma_; }
    /// Share of the box under the magma now (0: drained, 1: full); the part that hurts.
    float magmaLevel() const { return level_; }
    Rect magmaBody() const;
    bool warning() const { return warnLeft_ > 0.0f; }

private:
    void renderMagma(RenderContext& ctx) const;
    void renderPlasma(RenderContext& ctx) const;
    void renderAbyss(RenderContext& ctx) const;  // 2.53: the dark water of the Kraken's arena
    void renderEmbers(RenderContext& ctx) const;  // 2.56: the embers under the ash of the Phénix's nest
    void renderSouls(RenderContext& ctx) const;   // 2.57: the ossuary of the Liche's crypt, its souls
    float tick_ = 0.0f;
    float fxTimer_ = 0.0f;
    float shown_ = 0.0f;  // display: fades in when enabled (2.33)
    // 2.72, style « magma »: its level (simulation), draining after « disable », the telegraph,
    // the enemies' burn, and what it hides (display: the x of each body under its surface).
    bool magma_ = false;
    bool draining_ = false;
    float level_ = 0.0f;
    float warnLeft_ = 0.0f;
    float enemyTick_ = 0.0f;
    std::vector<float> hidden_;
};

class SlowZoneEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerStay(GameWorld& w, float dt) override;

private:
    float fxTimer_ = 0.0f;
};

class LaserEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    std::string debugInfo() const override;

private:
    Vec2 origin() const;
    Vec2 endPoint() const { return end_; }
    float cycle_ = 0.0f;
    bool enabled_ = true;
    bool on_ = false;
    bool warning_ = false;
    float hitTimer_ = 0.0f;
    Vec2 end_;
    AnimatedSprite emitter_;
    TexturePtr beam_;
};

// ================================================================= pickups

class CollectibleEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    Rect overlapBox() const override { return box.expanded(3); }
    void onPlayerEnter(GameWorld& w) override;
    /// 2.33: a pickup hidden until an event ("spawn", "activate", "enable") shows it.
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    void makeDrop(float vx, float vy);
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;
    /// Brought back by the boomerang blade (2.19): moved with it, picked up on reaching Nova.
    bool carriable() const { return type() == "shard" || type() == "energy_orb" || type() == "health_orb"; }
    void carryTo(Vec2 center) {
        box = Rect::fromCenter(center, box.size());
        base_ = center;
        dropping_ = false;
    }

private:
    AnimatedSprite sprite_;
    Vec2 base_;
    Vec2 vel_;
    bool dropping_ = false;
    float pickupDelay_ = 0.0f;
    float life_ = -1.0f;
    float seed_ = 0.0f;
};

class PowerUnlockEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void render(RenderContext& ctx) override;
    void update(GameWorld& w, float dt) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerEnter(GameWorld& w) override;
    const std::string& power() const { return power_; }
    /// The player took this tablet's power and left `dropped` in exchange: the tablet now
    /// offers the dropped power (nothing is ever lost, the player can come back for it).
    void exchange(GameWorld& w, const std::string& dropped);
    /// A challenge without checkpoints restarting: the tablet offers its own power again.
    void restore(GameWorld& w);
    /// A power station (tools/levelgen) that the challenge turned off: it offers nothing.
    bool disabledByChallenge(const GameWorld& w) const;
    /// Feedback of a power just obtained (title, sound, particles).
    static void celebrate(GameWorld& w, const PowerDefinition& def, Vec2 at, const std::string& message,
                          const std::string& dropped = {});

private:
    AnimatedSprite sprite_;
    std::string power_;
    float t_ = 0.0f;
    bool owned_ = false;  // display only (the player carries this power)
    bool stationOff_ = false;  // display only (a station a challenge turned off: dimmed)
};

// ============================================================= interactive

class CheckpointEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerEnter(GameWorld& w) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    bool activated() const { return activated_; }
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;

private:
    void activate(GameWorld& w, bool silent);
    AnimatedSprite sprite_;
    bool activated_ = false;
};

class ExitEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    Rect overlapBox() const override { return box.expanded(-6); }
    void onPlayerEnter(GameWorld& w) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;

private:
    AnimatedSprite sprite_;
    bool locked_ = false;
    bool near_ = false;
};

class DoorEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    bool isOpen() const { return open_; }
    void setOpen(GameWorld& w, bool open, bool silent = false);
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;

private:
    uint32_t collider_ = 0;
    bool open_ = false;
    bool initialOpen_ = false;
    AnimatedSprite sprite_;
};

class LeverEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool interactable() const override { return !(used_ && oneShot_); }
    void interact(GameWorld& w) override;
    const char* interactPrompt() const override { return "Actionner"; }
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;

private:
    AnimatedSprite sprite_;
    bool on_ = false;
    bool used_ = false;
    bool oneShot_ = true;
};

class ReceptorEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool onPowerHit(GameWorld& w, const std::string& power, const Rect& area) override;
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;

private:
    AnimatedSprite sprite_;
    bool on_ = false;
    float timer_ = 0.0f;
};

class SignEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    Rect overlapBox() const override { return box.expanded(18); }
    void onPlayerEnter(GameWorld& w) override;
    bool interactable() const override { return !auto_; }
    void interact(GameWorld& w) override;
    const char* interactPrompt() const override { return "Lire"; }

private:
    AnimatedSprite sprite_;
    bool auto_ = true;
};

// =================================================================== zones

class TriggerEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerEnter(GameWorld& w) override;
    void onPlayerExit(GameWorld& w) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;

private:
    bool fired_ = false;
};

class CombatZoneEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    Rect overlapBox() const override { return box.expanded(-12); }
    void onPlayerEnter(GameWorld& w) override;
    void onPlayerRespawn(GameWorld& w) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    std::string debugInfo() const override;
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;
    bool started() const { return started_; }
    bool cleared() const { return cleared_; }
    size_t waveCount() const { return waves_.size(); }
    /// Whether one of the arena's waves names this enemy object.
    bool lists(const std::string& objectId) const;
    /// Waves beaten so far (the current one counts once its last enemy is down).
    size_t wavesDone() const { return cleared_ ? waves_.size() : started_ ? wave_ + (waveDelay_ > 0.0f ? 1 : 0) : 0; }
    /// The waves an arena will have, read from the level (same rules as init()).
    static size_t countWaves(const LevelObject& zone, const LevelData& level);

    /// 2.62.2 (simulation 14): an ambush, an arena whose waves hold no boss (a boss's arena
    /// keeps its own rules: it starts again, the boss keeping its wounds). A fall during an
    /// ambush does not reset it: it waits as Nova left it (wave, enemies beaten, the others
    /// where they are with their life), its doors open to let her back in, and closing behind
    /// her as she comes back (suspend(), then onPlayerEnter()). An enemy of an ambush being
    /// fought that leaves the arena is brought back into it (bringBack()).
    bool ambush() const { return ambush_; }
    bool fighting() const { return started_ && !cleared_; }
    /// Waiting for Nova after her fall (its doors open).
    bool suspended() const { return suspended_; }
    /// The wave being fought (0-based; during the pause between two waves, the next one).
    size_t currentWave() const { return waveDelay_ > 0.0f ? wave_ + 1 : wave_; }
    /// The enemies listed by its waves (all waves).
    size_t enemyCount() const;
    /// Enemies still to beat: those alive of the current wave and every one of the waves to come.
    int enemiesLeft(GameWorld& w) const;
    /// The enemies of the current wave alive and awake (display: the HUD's arrows).
    std::vector<const Enemy*> fightingEnemies(GameWorld& w) const;
    /// Nova fell during the fight: an ambush waits for her, its doors open (no effect on
    /// another arena, nor on an ambush not being fought).
    void suspend(GameWorld& w);

private:
    void startWave(GameWorld& w, size_t index);
    void setDoors(GameWorld& w, bool open);
    void bringBack(GameWorld& w);
    /// Where an enemy brought back lands: its spawn point when it is in the arena and free,
    /// otherwise the free place of the arena nearest to it (on the ground for a walker).
    Vec2 returnPoint(GameWorld& w, const Enemy& e, Vec2 spawn) const;
    std::vector<std::vector<std::string>> waves_;
    std::vector<std::string> doors_;
    size_t wave_ = 0;
    bool started_ = false;
    bool cleared_ = false;
    bool ambush_ = false;
    bool suspended_ = false;
    float waveDelay_ = 0.0f;
};

class CameraZoneEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void render(RenderContext& ctx) override;
    const CameraZone& zone() const { return zone_; }
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;

private:
    CameraZone zone_;
};

class SecretAreaEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerEnter(GameWorld& w) override;
    void update(GameWorld& w, float dt) override;
    float reveal() const { return reveal_; }

private:
    float reveal_ = 0.0f;
};

/// "enemy" objects: spawns (and re-spawns) an Enemy, possibly dormant until an event.
class EnemySpawnEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;
    Enemy* enemy(GameWorld& w) const;
    /// Recreates the enemy (combat zone reset). `dormant` overrides the object's prop.
    void respawn(GameWorld& w, bool dormant);
    /// Combat zone reset after the player's death: a boss comes back with the wounds it had
    /// (life and phase), a defeated boss stays defeated. Other enemies are recreated.
    void respawnKeepingWounds(GameWorld& w, bool dormant);
    bool enemyAlive(GameWorld& w) const;
    bool defeated() const { return defeated_; }
    /// Where its enemy appears (the feet).
    Vec2 spawnFeet() const { return obj_.rect.bottomCenter(); }
    Json keepState(GameWorld& w) const override;
    void restoreKept(GameWorld& w, const Json& state) override;

private:
    uint32_t enemyUid_ = 0;
    bool defeated_ = false;  // a boss beaten in an arena that was reset
    AnimationSetPtr previewAnim_;
};

// =================================================================== decor

class DecorEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void render(RenderContext& ctx) override;
    float fadeOverride = 1.0f;  // secret areas fade foreground decor

private:
    const DecorSprite* sprite_ = nullptr;
    TexturePtr tex_;
    Color tint_ = Color::white();
    float alpha_ = 1.0f;
    float animSpeed_ = 1.0f;
    float sway_ = 0.0f;
    bool glow_ = true;
    float seed_ = 0.0f;
};

class LightEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void render(RenderContext& ctx) override;
};

class EmitterEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void update(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;

private:
    float timer_ = 0.0f;
};

// ================================================================ abilities

/// Anchor for the Grappin power; glows when it is the current target.
class GrapplePointEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool targeted() const { return targeted_; }

private:
    AnimatedSprite sprite_;
    bool targeted_ = false;
    float range_ = 170.0f;
};

/// Rising wind column: lifts a gliding player, slows a falling one.
class UpdraftEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerStay(GameWorld& w, float dt) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;

private:
    AnimatedSprite vent_;
    bool on_ = true;
    float fxTimer_ = 0.0f;
};

}  // namespace cosmic
