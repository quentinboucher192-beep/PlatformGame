#include "game/player/PlayerConfig.h"

namespace cosmic {

PlayerConfig PlayerConfig::fromJson(const Json& j) {
    PlayerConfig c;
    auto f = [&](const char* k, float& v, float lo, float hi) { v = std::clamp(jget<float>(j, k, v), lo, hi); };
    f("maxRunSpeed", c.maxRunSpeed, 10, 1000);
    f("walkSpeed", c.walkSpeed, 5, 1000);
    f("groundAcceleration", c.groundAcceleration, 10, 20000);
    f("groundDeceleration", c.groundDeceleration, 10, 20000);
    f("turnAcceleration", c.turnAcceleration, 10, 30000);
    f("airAcceleration", c.airAcceleration, 10, 20000);
    f("airDeceleration", c.airDeceleration, 0, 20000);
    f("airControl", c.airControl, 0, 2);
    f("jumpHeight", c.jumpHeight, 4, 400);
    f("timeToApex", c.timeToApex, 0.08f, 2.0f);
    f("doubleJumpHeight", c.doubleJumpHeight, 0, 400);
    f("fallGravityMultiplier", c.fallGravityMultiplier, 0.2f, 5);
    f("jumpCutMultiplier", c.jumpCutMultiplier, 0, 1);
    f("apexGravityMultiplier", c.apexGravityMultiplier, 0.1f, 2);
    f("apexThreshold", c.apexThreshold, 0, 200);
    f("maxFallSpeed", c.maxFallSpeed, 50, 2000);
    f("fastFallSpeed", c.fastFallSpeed, 50, 2000);
    f("coyoteTime", c.coyoteTime, 0, 0.5f);
    f("jumpBuffer", c.jumpBuffer, 0, 0.5f);
    c.airJumps = std::clamp(jget<int>(j, "airJumps", c.airJumps), 0, 5);
    f("wallSlideSpeed", c.wallSlideSpeed, 0, 500);
    c.wallJumpVelocity = vec2FromJson(j.value("wallJumpVelocity", Json()), c.wallJumpVelocity);
    f("wallJumpLock", c.wallJumpLock, 0, 1);
    f("slideSpeed", c.slideSpeed, 10, 1000);
    f("slideDuration", c.slideDuration, 0.05f, 3);
    f("climbSpeed", c.climbSpeed, 5, 500);
    c.size = vec2FromJson(j.value("size", Json()), c.size);
    c.slideSize = vec2FromJson(j.value("slideSize", Json()), c.slideSize);
    c.maxHealth = std::clamp(jget<int>(j, "maxHealth", c.maxHealth), 1, 20);
    f("invulnerability", c.invulnerability, 0, 5);
    c.knockback = vec2FromJson(j.value("knockback", Json()), c.knockback);
    f("maxEnergy", c.maxEnergy, 1, 1000);
    f("energyRegen", c.energyRegen, 0, 500);
    f("energyRegenDelay", c.energyRegenDelay, 0, 10);
    f("attackDamage", c.attackDamage, 0, 50);
    f("attackDuration", c.attackDuration, 0.05f, 2);
    f("attackCooldown", c.attackCooldown, 0.05f, 3);
    c.attackRange = vec2FromJson(j.value("attackRange", Json()), c.attackRange);
    f("swimSpeed", c.swimSpeed, 10, 500);
    f("waterGravityMultiplier", c.waterGravityMultiplier, 0.05f, 1);
    return c;
}

Json PlayerConfig::toJson() const {
    return {{"maxRunSpeed", maxRunSpeed},
            {"walkSpeed", walkSpeed},
            {"groundAcceleration", groundAcceleration},
            {"groundDeceleration", groundDeceleration},
            {"turnAcceleration", turnAcceleration},
            {"airAcceleration", airAcceleration},
            {"airDeceleration", airDeceleration},
            {"airControl", airControl},
            {"jumpHeight", jumpHeight},
            {"timeToApex", timeToApex},
            {"doubleJumpHeight", doubleJumpHeight},
            {"fallGravityMultiplier", fallGravityMultiplier},
            {"jumpCutMultiplier", jumpCutMultiplier},
            {"apexGravityMultiplier", apexGravityMultiplier},
            {"apexThreshold", apexThreshold},
            {"maxFallSpeed", maxFallSpeed},
            {"fastFallSpeed", fastFallSpeed},
            {"coyoteTime", coyoteTime},
            {"jumpBuffer", jumpBuffer},
            {"airJumps", airJumps},
            {"wallSlideSpeed", wallSlideSpeed},
            {"wallJumpVelocity", cosmic::toJson(wallJumpVelocity)},
            {"wallJumpLock", wallJumpLock},
            {"slideSpeed", slideSpeed},
            {"slideDuration", slideDuration},
            {"climbSpeed", climbSpeed},
            {"size", cosmic::toJson(size)},
            {"slideSize", cosmic::toJson(slideSize)},
            {"maxHealth", maxHealth},
            {"invulnerability", invulnerability},
            {"knockback", cosmic::toJson(knockback)},
            {"maxEnergy", maxEnergy},
            {"energyRegen", energyRegen},
            {"energyRegenDelay", energyRegenDelay},
            {"attackDamage", attackDamage},
            {"attackDuration", attackDuration},
            {"attackCooldown", attackCooldown},
            {"attackRange", cosmic::toJson(attackRange)},
            {"swimSpeed", swimSpeed},
            {"waterGravityMultiplier", waterGravityMultiplier}};
}

}  // namespace cosmic
