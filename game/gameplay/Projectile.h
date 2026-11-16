#pragma once

#include <cstdint>
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
    int bounces = 0;               // > 0: bounces off the scenery instead of exploding (plasma bomb)
    bool spin = false;             // drawn spinning instead of aligned with the velocity
    float homing = 0.0f;           // enemy projectiles: turn rate toward the player (rad/s)
    float homingTime = 0.0f;       // ... while age < homingTime
    // 2.50 (the dragon's « Boule de feu à nuage »): where it bursts, an enemy shot leaves a
    // burning cloud of this radius for cloudTime seconds (BossHazards::Cloud). 0 = none.
    float cloudRadius = 0.0f;
    float cloudTime = 0.0f;
    float cloudDamage = 0.0f;
    uint32_t cloudOwner = 0;
    Color cloudColor{255, 122, 46, 255};  // display only
    std::vector<const void*> hitTargets;  // already damaged (piercing)
    // 2.53, statistics only (never read by the simulation): set by GameWorld::spawnProjectile.
    uint32_t uid = 0;           // a serial of the run's shots
    uint32_t attackTag = 0;     // Nova's shot: the attack it belongs to (GameWorld::newAttackTag)
    uint32_t owner = 0;         // an enemy's shot: the enemy that fired it (Enemy::uid), 0 unknown
    uint8_t origin = 0;         // an enemy's shot: HurtOrigin of what fired it
    std::string attackName;     // an enemy's shot: the attack it belongs to (its name)
};

}  // namespace cosmic
