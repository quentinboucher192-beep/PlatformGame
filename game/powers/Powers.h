#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/powers/Power.h"

namespace cosmic {

/// Fast burst in the aimed direction with invulnerability; breaks crystal walls and
/// damages enemies on the way. Limited air uses, restored on landing.
class DashPower : public Power {
public:
    using Power::Power;
    bool canActivate(const Player& p, const GameWorld& w) const override;
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;
    bool controlsMovement() const override { return dashing_; }
    bool active() const override { return dashing_; }
    void onLand() override { refill_ = true; }
    void cancel(Player& p, GameWorld& w) override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    bool dashing_ = false;
    bool refill_ = true;  // computed on the first step (air dash upgrades)
    int airUses_ = 1;
    float hitTimer_ = 0.0f;
};

/// Ranged energy crescent: damages enemies, activates receptors, dissolves energy barriers.
class WavePower : public Power {
public:
    using Power::Power;

protected:
    void onActivate(Player& p, GameWorld& w) override;
};

/// Temporary bubble absorbing damage and reflecting enemy projectiles; lets the
/// player cross plasma curtains.
class ShieldPower : public Power {
public:
    using Power::Power;
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;
    void render(const Player& p, RenderContext& ctx) const override;
    bool active() const override { return remaining_ > 0.0f; }
    void cancel(Player& p, GameWorld& w) override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    float remaining_ = 0.0f;
    float anim_ = 0.0f;
};

/// Upward propulsion with a shockwave that knocks enemies back and breaks cracked rocks.
class ImpulsePower : public Power {
public:
    using Power::Power;
    bool canActivate(const Player& p, const GameWorld& w) const override;
    void onLand() override { uses_ = static_cast<int>(def_.param("airUses", 1)); }

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    int uses_ = 1;
};

/// Short-range blink in the aimed direction; passes through phase barriers.
class TeleportPower : public Power {
public:
    using Power::Power;
    bool canActivate(const Player& p, const GameWorld& w) const override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    bool findDestination(const Player& p, const GameWorld& w, Vec2& feetOut) const;
};

/// Hold jump while falling: slow descent with better air speed. Updrafts lift a
/// gliding player. No energy cost.
class GlidePower : public Power {
public:
    using Power::Power;
    PowerTrigger trigger() const override { return PowerTrigger::StandardKeys; }
    std::vector<Action> standardKeys() const override { return {Action::Jump}; }
    const char* standardKeysHow() const override { return "maintenue en tombant"; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player&, const GameWorld&, const PlayerInput&) const override { return false; }
    void handleInput(Player& p, GameWorld& w, const PlayerInput& in, float dt) override;
    bool active() const override { return gliding_; }
    void cancel(Player& p, GameWorld& w) override;
    const char* animationClip(const Player&) const override { return gliding_ ? "glide" : nullptr; }

protected:
    void onActivate(Player&, GameWorld&) override {}

private:
    bool gliding_ = false;
    float trailTimer_ = 0.0f;
};

/// Down + attack in the air: short hover then a vertical dive; the landing shockwave
/// damages and stuns nearby enemies and breaks cracked floors.
class GroundPoundPower : public Power {
public:
    using Power::Power;
    PowerTrigger trigger() const override { return PowerTrigger::StandardKeys; }
    std::vector<Action> standardKeys() const override { return {Action::MoveDown, Action::Attack}; }
    const char* standardKeysHow() const override { return "en l'air"; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player& p, const GameWorld& w, const PlayerInput& in) const override;
    bool canActivate(const Player& p, const GameWorld& w) const override;
    bool overridesMovement() const override { return phase_ != Phase::None; }
    void movePlayer(Player& p, GameWorld& w, const PlayerInput& in, float dt) override;
    bool active() const override { return phase_ != Phase::None; }
    void cancel(Player& p, GameWorld& w) override;
    const char* animationClip(const Player&) const override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    void impact(Player& p, GameWorld& w);
    enum class Phase { None, Hover, Dive, Land } phase_ = Phase::None;
    float timer_ = 0.0f;
    float startY_ = 0.0f;
    std::vector<const void*> hit_;
};

/// Hold attack to charge a plasma shot, release to fire it (piercing, big damage,
/// activates receptors). A tap still performs the normal melee attack.
class ChargeShotPower : public Power {
public:
    using Power::Power;
    PowerTrigger trigger() const override { return PowerTrigger::StandardKeys; }
    std::vector<Action> standardKeys() const override { return {Action::Attack}; }
    const char* standardKeysHow() const override { return "maintenue, puis relâchée"; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player&, const GameWorld&, const PlayerInput&) const override { return false; }
    void handleInput(Player& p, GameWorld& w, const PlayerInput& in, float dt) override;
    bool active() const override { return charging_; }
    void cancel(Player& p, GameWorld& w) override;
    const char* animationClip(const Player& p) const override;
    void render(const Player& p, RenderContext& ctx) const override;
    float chargeRatio() const;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    float hold_ = 0.0f;
    bool charging_ = false;
    bool full_ = false;
    float sparkTimer_ = 0.0f;
    float anim_ = 0.0f;
};

/// Interact near a grapple point: an energy line pulls the player to the anchor, then
/// releases them with momentum and restores the air jumps.
class GrapplePower : public Power {
public:
    using Power::Power;
    /// Its own button (Interagir in the data), never the key of its slot.
    PowerTrigger trigger() const override { return PowerTrigger::StandardKeys; }
    std::vector<Action> standardKeys() const override { return {def_.input}; }
    const char* standardKeysHow() const override { return "près d'un point d'ancrage"; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player& p, const GameWorld& w, const PlayerInput& in) const override;
    bool canActivate(const Player& p, const GameWorld& w) const override;
    bool overridesMovement() const override { return phase_ != Phase::None; }
    void movePlayer(Player& p, GameWorld& w, const PlayerInput& in, float dt) override;
    bool active() const override { return phase_ != Phase::None; }
    void cancel(Player& p, GameWorld& w) override;
    void render(const Player& p, RenderContext& ctx) const override;
    const char* animationClip(const Player&) const override;
    /// Best anchor for the player right now (nullptr if none in range / in sight).
    /// `skip` (if set) is an anchor position to ignore (the one just released).
    static const class LevelEntity* findTarget(const Player& p, const GameWorld& w, float range,
                                               const Vec2* skip = nullptr);
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    void release(Player& p, GameWorld& w, bool boost);
    enum class Phase { None, Throw, Pull } phase_ = Phase::None;
    const Vec2* recentAnchor() const { return sinceRelease_ < 0.35f ? &anchor_ : nullptr; }
    Vec2 anchor_;
    Vec2 hook_;
    float timer_ = 0.0f;
    float stuck_ = 0.0f;
    float sinceRelease_ = 10.0f;
};

/// Passive upgrade: extra air jumps (triple jump), used with Jump in the air.
class AirJumpPower : public Power {
public:
    using Power::Power;
    bool passive() const override { return true; }
    PowerTrigger trigger() const override { return PowerTrigger::StandardKeys; }
    std::vector<Action> standardKeys() const override { return {Action::Jump}; }
    const char* standardKeysHow() const override { return "en l'air"; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player&, const GameWorld&, const PlayerInput&) const override { return false; }
    int extraJumps() const { return static_cast<int>(def_.param("jumps", 1)); }

protected:
    void onActivate(Player&, GameWorld&) override {}
};

/// Slows the enemies and their shots for a few seconds (special button).
class TimeSlowPower : public Power {
public:
    using Power::Power;

protected:
    void onActivate(Player& p, GameWorld& w) override;
};

/// Passive: shards, relics and orbs nearby fly to the player.
class MagnetPower : public Power {
public:
    using Power::Power;
    bool passive() const override { return true; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player&, const GameWorld&, const PlayerInput&) const override { return false; }
    float radius() const { return def_.param("radius", 150.0f); }

protected:
    void onActivate(Player&, GameWorld&) override {}
};

/// Passive upgrade of the Dash: more dashes before touching the ground.
class AirDashPower : public Power {
public:
    using Power::Power;
    bool passive() const override { return true; }
    bool exclusiveInput() const override { return false; }
    bool wantsActivation(const Player&, const GameWorld&, const PlayerInput&) const override { return false; }
    int extraDashes() const { return static_cast<int>(def_.param("dashes", 1)); }

protected:
    void onActivate(Player&, GameWorld&) override {}
};

/// Bouncing plasma bomb (special button): explodes after its fuse or on an enemy, damages
/// around it and breaks cracked rock like the Impulse.
class PlasmaBombPower : public Power {
public:
    using Power::Power;

protected:
    void onActivate(Player& p, GameWorld& w) override;
};

// ------------------------------------------------- 2.19: powers of the sanctuaries

/// Lame boomerang: an energy blade thrown ahead (aimed like the Wave) that slows down,
/// turns back and returns to Nova. It hits every enemy it crosses once on the way out and
/// once on the way back (it goes through them), switches receptors on, and brings back the
/// shards and orbs it touches. A wall on the way out sends it back early; on the way back
/// nothing stops it. One blade at a time.
class BoomerangPower : public Power {
public:
    using Power::Power;
    bool canActivate(const Player& p, const GameWorld& w) const override;
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;
    void render(const Player& p, RenderContext& ctx) const override;
    bool active() const override { return flying_; }
    void cancel(Player& p, GameWorld& w) override;
    bool flying() const { return flying_; }
    bool returning() const { return returning_; }
    Vec2 bladePos() const { return pos_; }
    /// Enemies hit by the blade in flight (out, back): tests, demos.
    int hitsOut() const { return static_cast<int>(hitOut_.size()); }
    int hitsBack() const { return static_cast<int>(hitBack_.size()); }
    /// Blades caught back by Nova since the power was granted.
    int caught() const { return caught_; }

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    void turnBack(GameWorld& w);
    void strike(GameWorld& w);
    float range() const { return def_.param("range", 260.0f); }
    float speed() const { return def_.param("speed", 600.0f); }
    bool flying_ = false;
    bool returning_ = false;
    Vec2 pos_, prevPos_, dir_;
    float speed_ = 0.0f;
    float age_ = 0.0f;
    float spin_ = 0.0f, prevSpin_ = 0.0f;
    float trailTimer_ = 0.0f;
    int caught_ = 0;
    std::vector<uint32_t> hitOut_, hitBack_;  // enemy uids hit on each leg
    std::vector<std::string> carried_;        // collectibles brought back (entity ids)
};

/// Nova de givre: a burst of cold around Nova. Enemies in the radius freeze (a frozen
/// enemy is a platform, see GameWorld), bosses are only slowed, and the enemy shots in the
/// radius shatter.
class FrostNovaPower : public Power {
public:
    using Power::Power;
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;
    void render(const Player& p, RenderContext& ctx) const override;
    /// Enemies frozen and shots destroyed by the last nova (tests, demos).
    int lastFrozen() const { return lastFrozen_; }
    int lastShattered() const { return lastShattered_; }

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    Vec2 center_;
    float ring_ = 10.0f;  // seconds since the last nova (the ring is drawn for a moment)
    int lastFrozen_ = 0, lastShattered_ = 0;
};

/// Leurre holographique: a hologram of Nova left where she stands; for a few seconds the
/// enemies and bosses aim at it instead of her (GameWorld::setDecoy), until an attack
/// touches it.
class DecoyPower : public Power {
public:
    using Power::Power;
    bool active() const override;
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;
    void cancel(Player& p, GameWorld& w) override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    bool decoyUp_ = false;  // a decoy of this power is in the world
};

/// Balise de rappel: a first press drops a beacon where Nova stands (free); a second press
/// within the window brings her back to it at once (energy cost), speed reset and briefly
/// invulnerable. After the window the beacon fades and the next press drops a new one.
class RecallPower : public Power {
public:
    using Power::Power;
    float activationCost(const Player& /*p*/) const override {
        return beacon_ ? def_.energyCost : def_.param("placeCost", 0.0f);
    }
    bool canActivate(const Player& p, const GameWorld& w) const override;
    void handleInput(Player& p, GameWorld& w, const PlayerInput& in, float dt) override;
    void fixedUpdate(Player& p, GameWorld& w, float dt) override;
    void render(const Player& p, RenderContext& ctx) const override;
    bool active() const override { return beacon_; }
    void cancel(Player& p, GameWorld& w) override;
    bool beaconPlaced() const { return beacon_; }
    Vec2 beaconFeet() const { return feet_; }
    float beaconRemaining() const { return beacon_ ? timer_ : 0.0f; }
    /// Returns to the beacon since the power was granted (tests, demos).
    int warps() const { return warps_; }

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    bool blocked(const Player& p, const GameWorld& w) const;
    bool beacon_ = false;
    Vec2 feet_;
    float timer_ = 0.0f;
    float anim_ = 0.0f;
    float warpFx_ = 10.0f;  // seconds since the last return (trail drawn for a moment)
    Vec2 warpFrom_, warpTo_;
    int warps_ = 0;
};

}  // namespace cosmic
