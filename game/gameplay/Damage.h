#pragma once

#include <string>

#include "engine/core/Math.h"

namespace cosmic {

enum class DamageType { Melee, Wave, Dash, Impulse, Contact, Projectile, Hazard, Explosion, Count };

const char* damageTypeId(DamageType t);
const char* damageTypeLabel(DamageType t);
DamageType damageTypeFromId(const std::string& id);

struct DamageInfo {
    float amount = 1.0f;
    DamageType type = DamageType::Contact;
    Vec2 source;            // world position of the source (knockback direction)
    float knockback = 160.0f;
    bool fromPlayer = false;
    bool ignoreShield = false;
};

}  // namespace cosmic
