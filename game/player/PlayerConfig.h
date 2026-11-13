#pragma once

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

/// Every tunable movement / combat parameter of the player (data/player.json).
/// Jump physics are expressed in designer terms (height, time to apex) and the
/// gravity / initial velocity are derived from them.
struct PlayerConfig {
    // Ground movement
    float maxRunSpeed = 150.0f;
    float walkSpeed = 75.0f;
    float groundAcceleration = 1500.0f;
    float groundDeceleration = 1900.0f;  // ground friction
    float turnAcceleration = 2800.0f;
    // Air control
    float airAcceleration = 1100.0f;
    float airDeceleration = 450.0f;
    float airControl = 1.0f;
    // Jump
    float jumpHeight = 58.0f;
    float timeToApex = 0.38f;  // "durée de saut"
    float doubleJumpHeight = 44.0f;
    float fallGravityMultiplier = 1.65f;
    float jumpCutMultiplier = 0.45f;
    float apexGravityMultiplier = 0.6f;
    float apexThreshold = 40.0f;
    float maxFallSpeed = 360.0f;
    float fastFallSpeed = 460.0f;
    float coyoteTime = 0.1f;
    float jumpBuffer = 0.12f;
    int airJumps = 1;  // double jump
    // Wall
    float wallSlideSpeed = 70.0f;
    Vec2 wallJumpVelocity{175.0f, -285.0f};
    float wallJumpLock = 0.16f;
    // Slide / climb
    float slideSpeed = 235.0f;
    float slideDuration = 0.42f;
    float climbSpeed = 80.0f;
    // Hitbox
    Vec2 size{10.0f, 22.0f};
    Vec2 slideSize{12.0f, 11.0f};
    // Health / energy
    int maxHealth = 5;
    float invulnerability = 1.2f;
    Vec2 knockback{150.0f, -170.0f};
    float maxEnergy = 100.0f;
    float energyRegen = 14.0f;
    float energyRegenDelay = 0.7f;
    // Melee attack
    float attackDamage = 1.0f;
    float attackDuration = 0.28f;
    float attackCooldown = 0.32f;
    Vec2 attackRange{24.0f, 22.0f};
    // Swim (water)
    float swimSpeed = 90.0f;
    float waterGravityMultiplier = 0.35f;

    // Derived
    float gravity() const { return 2.0f * jumpHeight / (timeToApex * timeToApex); }
    float jumpVelocity() const { return 2.0f * jumpHeight / timeToApex; }
    float doubleJumpVelocity() const { return std::sqrt(2.0f * gravity() * doubleJumpHeight); }

    static PlayerConfig fromJson(const Json& j);
    Json toJson() const;
};

}  // namespace cosmic
