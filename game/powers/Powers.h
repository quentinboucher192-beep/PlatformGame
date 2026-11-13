#pragma once

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
    void onLand() override { airUses_ = static_cast<int>(def_.param("airUses", 1)); }
    void cancel(Player& p, GameWorld& w) override;

protected:
    void onActivate(Player& p, GameWorld& w) override;

private:
    bool dashing_ = false;
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

}  // namespace cosmic
