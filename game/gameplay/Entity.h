#pragma once

#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Parallax.h"
#include "engine/rendering/Renderer.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class GameWorld;
class ResourceManager;
class TextRenderer;

/// Everything an entity needs to draw itself.
struct RenderContext {
    Renderer& r;
    ResourceManager& res;
    LightMap& lights;
    float alpha = 1.0f;   // interpolation between fixed steps
    float time = 0.0f;    // world time (animations)
    bool editor = false;  // editor preview: show invisible zones, no gameplay state
};

/// Base class of every interactive level object created from a LevelObject
/// (platforms, hazards, pickups, doors, zones, decor...).
class LevelEntity {
public:
    explicit LevelEntity(const LevelObject& obj) : obj_(obj), box(obj.rect) {}
    virtual ~LevelEntity() = default;

    const std::string& id() const { return obj_.id; }
    const std::string& type() const { return obj_.type; }
    const LevelObject& object() const { return obj_; }

    /// Called once every entity of the level exists (resolve targets, colliders).
    virtual void init(GameWorld& /*w*/) {}
    virtual void fixedUpdate(GameWorld& /*w*/, float /*dt*/) {}
    virtual void update(GameWorld& /*w*/, float /*dt*/) {}
    virtual void render(RenderContext& /*ctx*/) {}

    // Player overlap notifications (only if wantsPlayerOverlap()).
    virtual bool wantsPlayerOverlap() const { return false; }
    virtual Rect overlapBox() const { return box; }
    virtual void onPlayerEnter(GameWorld& /*w*/) {}
    virtual void onPlayerStay(GameWorld& /*w*/, float /*dt*/) {}
    virtual void onPlayerExit(GameWorld& /*w*/) {}

    // Interaction (Interact button)
    virtual bool interactable() const { return false; }
    virtual void interact(GameWorld& /*w*/) {}
    virtual const char* interactPrompt() const { return "Interagir"; }

    /// Event actions: open, close, toggle, enable, disable, activate, spawn, break...
    virtual bool handleAction(GameWorld& /*w*/, const std::string& /*action*/, const Json& /*params*/) { return false; }
    /// Area hit by a player power (wave projectile, impulse shockwave, dash...).
    virtual bool onPowerHit(GameWorld& /*w*/, const std::string& /*power*/, const Rect& /*area*/) { return false; }
    /// The player respawned at a checkpoint.
    virtual void onPlayerRespawn(GameWorld& /*w*/) {}
    virtual std::string debugInfo() const { return {}; }

    Rect box;
    int layerOrder = 0;
    bool foreground = false;
    bool active = true;
    bool visible = true;
    bool removed = false;
    bool playerInside = false;

protected:
    LevelObject obj_;
};

}  // namespace cosmic
