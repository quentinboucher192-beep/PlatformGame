#pragma once

#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/camera/Camera.h"
#include "game/gameplay/Entity.h"
#include "game/levels/DecorAtlas.h"

namespace cosmic {

class Enemy;

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
    TexturePtr tex_;
};

class CrumblePlatformEntity : public LevelEntity {
public:
    using LevelEntity::LevelEntity;
    void init(GameWorld& w) override;
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    void onPlayerRespawn(GameWorld& w) override;

private:
    enum class Phase { Solid, Shaking, Fallen } phase_ = Phase::Solid;
    uint32_t collider_ = 0;
    float timer_ = 0.0f;
    TexturePtr tex_;
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
    void fixedUpdate(GameWorld& w, float dt) override;
    void render(RenderContext& ctx) override;
    bool wantsPlayerOverlap() const override { return true; }
    void onPlayerStay(GameWorld& w, float dt) override;
    bool handleAction(GameWorld& w, const std::string& action, const Json& params) override;

private:
    float tick_ = 0.0f;
    float fxTimer_ = 0.0f;
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
    void makeDrop(float vx, float vy);

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

private:
    AnimatedSprite sprite_;
    std::string power_;
    float t_ = 0.0f;
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

private:
    void startWave(GameWorld& w, size_t index);
    void setDoors(GameWorld& w, bool open);
    std::vector<std::vector<std::string>> waves_;
    std::vector<std::string> doors_;
    size_t wave_ = 0;
    bool started_ = false;
    bool cleared_ = false;
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
    bool enemyAlive(GameWorld& w) const;

private:
    uint32_t enemyUid_ = 0;
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
