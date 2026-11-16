#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/powers/PowerDefinition.h"

namespace cosmic {

class Player;
class GameWorld;
struct RenderContext;
struct PlayerInput;

/// How the player uses a power (2.24: the keys belong to the power slots).
enum class PowerTrigger {
    SlotKey,       // the key of the slot it is equipped in (« Pouvoir 1 » ...)
    StandardKeys,  // a move of the character: Jump, Attack, Down + Attack, Interact...
    Passive,       // nothing to press: it works on its own
};

/// Base class of the player's powers. A power is created from a PowerDefinition
/// (data) by the PowerRegistry according to its `behavior`. Adding a power means adding
/// a subclass and registering it: the Player never needs to change.
class Power {
public:
    explicit Power(const PowerDefinition& def) : def_(def), key_(def.input) {}
    virtual ~Power() = default;

    const PowerDefinition& def() const { return def_; }
    const std::string& id() const { return def_.id; }

    /// What the player presses to use it. A power that keeps the default activation uses
    /// the key of its slot; the others override this (and standardKeys()).
    virtual PowerTrigger trigger() const { return passive() ? PowerTrigger::Passive : PowerTrigger::SlotKey; }
    /// StandardKeys: the moves that use it, in order (Bas + Attaque: {MoveDown, Attack}).
    virtual std::vector<Action> standardKeys() const { return {}; }
    /// StandardKeys: how they are used, in French, said after the keys (« en l'air »).
    virtual const char* standardKeysHow() const { return ""; }
    /// The action that uses the power: the key of its slot (PowerSlot1..8) for a slot-key
    /// power when the keys are per slot, its own `input` otherwise; Action::Count: none
    /// (a slot beyond the last key). Set by the Player whenever its powers change.
    Action key() const { return key_; }
    void setKey(Action a) { key_ = a; }

    /// Input rule. By default a power fires when its key (key()) is pressed; context powers
    /// (ground pound, grapple...) and passive upgrades override it.
    virtual bool wantsActivation(const Player& p, const GameWorld& w, const PlayerInput& in) const;
    /// Called every controllable step with the input, before activation (hold / release
    /// logic: glide, charged shot).
    virtual void handleInput(Player& /*p*/, GameWorld& /*w*/, const PlayerInput& /*in*/, float /*dt*/) {}
    /// A power granted on the same input as another one replaces it, unless one of them
    /// is contextual (several powers can share the attack or jump buttons).
    virtual bool exclusiveInput() const { return true; }
    /// Passive upgrades are never activated (triple jump).
    virtual bool passive() const { return false; }
    /// While true the power moves the player itself through movePlayer() (ground pound,
    /// grapple); attacks and interactions are suspended.
    virtual bool overridesMovement() const { return false; }
    virtual void movePlayer(Player& /*p*/, GameWorld& /*w*/, const PlayerInput& /*in*/, float /*dt*/) {}
    /// Animation clip imposed while the power is in use (nullptr = automatic).
    virtual const char* animationClip(const Player& /*p*/) const { return nullptr; }

    /// Energy the next activation costs: the definition's cost, except for powers whose
    /// first use is free (the recall beacon is placed for nothing, coming back costs).
    virtual float activationCost(const Player& /*p*/) const { return def_.energyCost; }
    /// Cooldown / energy / situational checks.
    bool ready(const Player& p) const;
    virtual bool canActivate(const Player& /*p*/, const GameWorld& /*w*/) const { return true; }
    /// Called when the input is pressed and ready() && canActivate(): consumes energy
    /// and starts the cooldown, then calls onActivate().
    void activate(Player& p, GameWorld& w);
    virtual void fixedUpdate(Player& p, GameWorld& w, float dt);
    virtual void render(const Player& /*p*/, RenderContext& /*ctx*/) const {}
    /// True while the power controls the player's movement (dash...).
    virtual bool controlsMovement() const { return false; }
    virtual bool active() const { return false; }
    virtual void cancel(Player& /*p*/, GameWorld& /*w*/) {}
    /// Air uses (impulse, dash) are restored when landing.
    virtual void onLand() {}

    float cooldownRemaining() const { return cooldown_; }
    /// 2.52, statistics only: the tag of its last use (GameWorld::newAttackTag), 0 before.
    uint32_t attackTag() const { return attackTag_; }
    float cooldownRatio() const { return def_.cooldown > 0 ? cooldown_ / def_.cooldown : 0.0f; }

protected:
    virtual void onActivate(Player& p, GameWorld& w) = 0;
    /// Whether key() is pressed in this step.
    bool keyPressed(const PlayerInput& in) const;
    PowerDefinition def_;
    float cooldown_ = 0.0f;
    Action key_;
    uint32_t attackTag_ = 0;
};

/// How a power of the data is used (HUD, power guide, choice of the power to replace).
struct PowerTriggerInfo {
    PowerTrigger kind = PowerTrigger::SlotKey;
    std::vector<Action> keys;  // StandardKeys only
    std::string how;           // StandardKeys only, French (« en l'air »)
};
PowerTriggerInfo powerTriggerInfo(const Power& p);
/// Same, for a definition (asks a power built from it; SlotKey for an unknown behaviour).
PowerTriggerInfo powerTriggerInfo(const PowerDefinition& def);

/// Factory of powers by behaviour name.
class PowerRegistry {
public:
    using Factory = std::function<std::unique_ptr<Power>(const PowerDefinition&)>;
    static PowerRegistry& instance();
    void registerBehavior(const std::string& behavior, Factory f);
    std::unique_ptr<Power> create(const PowerDefinition& def) const;
    bool has(const std::string& behavior) const { return factories_.count(behavior) > 0; }
    std::vector<std::string> behaviors() const;

private:
    PowerRegistry();
    std::map<std::string, Factory> factories_;
};

}  // namespace cosmic
