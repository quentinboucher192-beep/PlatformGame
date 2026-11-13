#include "game/gameplay/Damage.h"

namespace cosmic {

namespace {
struct Info {
    const char* id;
    const char* label;
};
constexpr Info kInfo[] = {
    {"melee", "Lame d'énergie"}, {"wave", "Onde énergétique"}, {"dash", "Dash"},       {"impulse", "Impulsion"},
    {"contact", "Contact"},       {"projectile", "Projectile"}, {"hazard", "Piège"},    {"explosion", "Explosion"},
    {"ground_pound", "Frappe sismique"}, {"charge_shot", "Tir chargé"},
};
}  // namespace

const char* damageTypeId(DamageType t) { return kInfo[static_cast<int>(t)].id; }
const char* damageTypeLabel(DamageType t) { return kInfo[static_cast<int>(t)].label; }

DamageType damageTypeFromId(const std::string& id) {
    for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
        if (id == kInfo[i].id) return static_cast<DamageType>(i);
    return DamageType::Contact;
}

}  // namespace cosmic
