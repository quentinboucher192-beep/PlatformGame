#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/Damage.h"

namespace cosmic {

struct Projectile {
    Vec2 pos;
    Vec2 prevPos;
    Vec2 vel;
    float radius = 4.0f;
    float damage = 1.0f;
    DamageType type = DamageType::Projectile;
    bool fromPlayer = false;
    float life = 3.0f;
    float age = 0.0f;
    float gravity = 0.0f;
    std::string clip = "drone_bolt";
    int pierce = 0;              // extra targets it can go through
    bool reflected = false;
    bool alive = true;
    Color light{255, 140, 80, 255};
    float lightRadius = 26.0f;
    std::string hitEffect = "hit_spark";
    std::string hitSound;
    float knockback = 140.0f;
    float slow = 0.0f;             // see DamageInfo::slow
    float slowDuration = 0.0f;
    float explosionRadius = 0.0f;  // > 0: explodes on impact (bombs)
    bool spin = false;             // drawn spinning instead of aligned with the velocity
    float homing = 0.0f;           // enemy projectiles: turn rate toward the player (rad/s)
    float homingTime = 0.0f;       // ... while age < homingTime
    std::vector<const void*> hitTargets;  // already damaged (piercing)
};

}  // namespace cosmic
