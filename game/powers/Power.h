#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "game/powers/PowerDefinition.h"

namespace cosmic {

class Player;
class GameWorld;
struct RenderContext;

/// Base class of the player's powers. A power is created from a PowerDefinition
/// (data) by the PowerRegistry according to its `behavior`. Adding a power means adding
/// a subclass and registering it: the Player never needs to change.
class Power {
public:
    explicit Power(const PowerDefinition& def) : def_(def) {}
    virtual ~Power() = default;

    const PowerDefinition& def() const { return def_; }
    const std::string& id() const { return def_.id; }

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
    float cooldownRatio() const { return def_.cooldown > 0 ? cooldown_ / def_.cooldown : 0.0f; }

protected:
    virtual void onActivate(Player& p, GameWorld& w) = 0;
    PowerDefinition def_;
    float cooldown_ = 0.0f;
};

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
