#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

namespace {
struct Named {
    const char* id;
    const char* label;
};
constexpr Named kFamilies[] = {{"ground", "Créature terrestre"}, {"robot", "Robot"},          {"flying", "Créature volante"},
                               {"turret", "Tourelle"},           {"fast", "Ennemi rapide"},   {"heavy", "Ennemi lourd"},
                               {"caster", "Utilisateur de pouvoirs"}, {"miniboss", "Mini-boss"}};
constexpr Named kMovements[] = {{"idle", "Immobile (garde)"}, {"patrol", "Patrouille"},  {"guard", "Surveillance"},
                                {"fly", "Vol"},               {"hop", "Sauts"},          {"stationary", "Fixe (tourelle)"}};
constexpr Named kReactions[] = {{"none", "Aucune"},
                                {"chase", "Poursuite"},
                                {"keepDistance", "Garde ses distances (tir)"},
                                {"flee", "Fuite"},
                                {"guardZone", "Protège sa zone"}};
constexpr Named kAttacks[] = {{"contact", "Contact"},           {"melee", "Corps à corps"},  {"lunge", "Bond"},
                              {"projectile", "Projectile visé"}, {"spread", "Salve en éventail"}, {"arc", "Tir en cloche"},
                              {"slam", "Onde de choc au sol"}, {"charge", "Charge"},      {"laser", "Rayon laser"},
                              {"summon", "Appel de renforts"}, {"rain", "Pluie de projectiles"},
                              {"eruption", "Éruption au sol"}, {"leap", "Saut sur le joueur"}};
}  // namespace

const char* familyId(EnemyFamily f) { return kFamilies[static_cast<int>(f)].id; }
const char* familyLabel(EnemyFamily f) { return kFamilies[static_cast<int>(f)].label; }
const char* movementId(MovementType m) { return kMovements[static_cast<int>(m)].id; }
const char* movementLabel(MovementType m) { return kMovements[static_cast<int>(m)].label; }
const char* reactionId(ReactionType r) { return kReactions[static_cast<int>(r)].id; }
const char* reactionLabel(ReactionType r) { return kReactions[static_cast<int>(r)].label; }
const char* attackTypeId(AttackType a) { return kAttacks[static_cast<int>(a)].id; }
const char* attackTypeLabel(AttackType a) { return kAttacks[static_cast<int>(a)].label; }

template <typename E>
E enumFromId(const std::string& id, const char* (*idFn)(E), E fallback) {
    for (int i = 0; i < static_cast<int>(E::Count); ++i)
        if (id == idFn(static_cast<E>(i))) return static_cast<E>(i);
    return fallback;
}
template EnemyFamily enumFromId(const std::string&, const char* (*)(EnemyFamily), EnemyFamily);
template MovementType enumFromId(const std::string&, const char* (*)(MovementType), MovementType);
template ReactionType enumFromId(const std::string&, const char* (*)(ReactionType), ReactionType);
template AttackType enumFromId(const std::string&, const char* (*)(AttackType), AttackType);

Json AttackDef::toJson() const {
    Json j{{"id", id},
           {"name", name},
           {"type", attackTypeId(type)},
           {"damage", damage},
           {"range", range},
           {"minRange", minRange},
           {"telegraph", telegraph},
           {"active", active},
           {"recover", recover},
           {"cooldown", cooldown},
           {"weight", weight},
           {"telegraphAnim", telegraphAnim},
           {"attackAnim", attackAnim},
           {"knockback", knockback},
           {"sound", sound},
           {"telegraphSound", telegraphSound}};
    if (isProjectileType()) {
        j["projectile"] = projectile;
        j["projectileSpeed"] = projectileSpeed;
        j["count"] = count;
        j["spread"] = spread;
        j["gravity"] = gravity;
        if (projectileRadius > 0) j["projectileRadius"] = projectileRadius;
        if (explosionRadius > 0) j["explosionRadius"] = explosionRadius;
        if (!hitEffect.empty()) j["hitEffect"] = hitEffect;
    }
    if (type == AttackType::Rain) j["rainHeight"] = rainHeight;
    if (type == AttackType::Eruption) {
        j["warningEffect"] = warningEffect;
        j["eruptionEffect"] = eruptionEffect;
    }
    if (hasOrigin) j["origin"] = {origin.x, origin.y};
    if (slow > 0) {
        j["slow"] = slow;
        j["slowDuration"] = slowDuration;
    }
    if (type == AttackType::Lunge || type == AttackType::Charge) j["lungeSpeed"] = lungeSpeed;
    if (type == AttackType::Summon) {
        j["summonId"] = summonId;
        j["summonCount"] = summonCount;
        j["maxSummoned"] = maxSummoned;
    }
    if (type == AttackType::Laser) {
        j["laserLength"] = laserLength;
        j["sweepDegrees"] = sweepDegrees;
        j["color"] = beamColor.toHex();
    }
    if (lead > 0) j["lead"] = lead;
    if (bursts > 1) {
        j["bursts"] = bursts;
        j["burstInterval"] = burstInterval;
    }
    if (homing > 0) {
        j["homing"] = homing;
        j["homingTime"] = homingTime;
    }
    if (stagger > 0) j["stagger"] = stagger;
    if (type == AttackType::Leap) j["leapTime"] = leapTime;
    return j;
}

AttackDef AttackDef::fromJson(const Json& j) {
    AttackDef a;
    a.id = jstr(j, "id", "attack");
    a.name = jstr(j, "name", a.id);
    a.type = enumFromId(jstr(j, "type", "melee"), &attackTypeId, AttackType::Melee);
    a.damage = std::clamp(jget<float>(j, "damage", 1.0f), 0.0f, 50.0f);
    a.range = std::max(0.0f, jget<float>(j, "range", 28.0f));
    a.minRange = std::max(0.0f, jget<float>(j, "minRange", 0.0f));
    a.telegraph = std::clamp(jget<float>(j, "telegraph", 0.5f), 0.0f, 10.0f);
    a.active = std::clamp(jget<float>(j, "active", 0.2f), 0.0f, 10.0f);
    a.recover = std::clamp(jget<float>(j, "recover", 0.5f), 0.0f, 10.0f);
    a.cooldown = std::clamp(jget<float>(j, "cooldown", 1.5f), 0.0f, 60.0f);
    a.weight = std::max(0.01f, jget<float>(j, "weight", 1.0f));
    a.telegraphAnim = jstr(j, "telegraphAnim", "telegraph");
    a.attackAnim = jstr(j, "attackAnim", "attack");
    a.projectile = jstr(j, "projectile", "drone_bolt");
    a.projectileSpeed = jget<float>(j, "projectileSpeed", 160.0f);
    a.count = std::clamp(jget<int>(j, "count", 1), 1, 24);
    a.spread = jget<float>(j, "spread", 0.0f);
    a.gravity = jget<float>(j, "gravity", 0.0f);
    a.lungeSpeed = jget<float>(j, "lungeSpeed", 260.0f);
    a.knockback = jget<float>(j, "knockback", 170.0f);
    a.summonId = jstr(j, "summonId", "glowmite");
    a.summonCount = std::clamp(jget<int>(j, "summonCount", 2), 1, 8);
    a.maxSummoned = std::clamp(jget<int>(j, "maxSummoned", 4), 1, 16);
    a.laserLength = jget<float>(j, "laserLength", 320.0f);
    a.sweepDegrees = jget<float>(j, "sweepDegrees", 0.0f);
    a.sound = jstr(j, "sound");
    a.telegraphSound = jstr(j, "telegraphSound", "sounds/enemy_telegraph.wav");
    if (j.contains("origin") && j["origin"].is_array() && j["origin"].size() == 2) {
        a.hasOrigin = true;
        a.origin = vec2FromJson(j["origin"], {});
    }
    a.projectileRadius = std::clamp(jget<float>(j, "projectileRadius", 0.0f), 0.0f, 40.0f);
    a.hitEffect = jstr(j, "hitEffect");
    a.slow = std::clamp(jget<float>(j, "slow", 0.0f), 0.0f, 1.0f);
    a.slowDuration = std::clamp(jget<float>(j, "slowDuration", a.slow > 0 ? 1.5f : 0.0f), 0.0f, 10.0f);
    a.explosionRadius = std::clamp(jget<float>(j, "explosionRadius", 0.0f), 0.0f, 200.0f);
    a.lead = std::clamp(jget<float>(j, "lead", 0.0f), 0.0f, 1.0f);
    a.bursts = std::clamp(jget<int>(j, "bursts", 1), 1, 8);
    a.burstInterval = std::clamp(jget<float>(j, "burstInterval", 0.3f), 0.05f, 3.0f);
    a.homing = std::clamp(jget<float>(j, "homing", 0.0f), 0.0f, 12.0f);
    a.homingTime = std::clamp(jget<float>(j, "homingTime", 1.2f), 0.0f, 6.0f);
    a.stagger = std::clamp(jget<float>(j, "stagger", 0.0f), 0.0f, 1.0f);
    a.leapTime = std::clamp(jget<float>(j, "leapTime", 0.8f), 0.3f, 2.0f);
    a.rainHeight = std::clamp(jget<float>(j, "rainHeight", 220.0f), 40.0f, 800.0f);
    a.warningEffect = jstr(j, "warningEffect", "eruption_warning_fire");
    a.eruptionEffect = jstr(j, "eruptionEffect", "eruption_fire");
    a.beamColor = colorFromJson(j.value("color", Json()), a.beamColor);
    return a;
}

Json BossPhase::toJson() const {
    return {{"name", name},
            {"hpThreshold", hpThreshold},
            {"attacks", attacks},
            {"speedMultiplier", speedMultiplier},
            {"cooldownMultiplier", cooldownMultiplier},
            {"enterText", enterText},
            {"summonOnEnter", summonOnEnter},
            {"combo", combo},
            {"telegraphMultiplier", telegraphMultiplier}};
}

BossPhase BossPhase::fromJson(const Json& j) {
    BossPhase p;
    p.name = jstr(j, "name", "Phase");
    p.hpThreshold = std::clamp(jget<float>(j, "hpThreshold", 1.0f), 0.0f, 1.0f);
    if (j.contains("attacks") && j["attacks"].is_array())
        for (const auto& a : j["attacks"])
            if (a.is_string()) p.attacks.push_back(a.get<std::string>());
    p.speedMultiplier = std::clamp(jget<float>(j, "speedMultiplier", 1.0f), 0.2f, 4.0f);
    p.cooldownMultiplier = std::clamp(jget<float>(j, "cooldownMultiplier", 1.0f), 0.1f, 4.0f);
    p.enterText = jstr(j, "enterText");
    p.summonOnEnter = jget<bool>(j, "summonOnEnter", false);
    p.combo = std::clamp(jget<int>(j, "combo", 1), 1, 5);
    p.telegraphMultiplier = std::clamp(jget<float>(j, "telegraphMultiplier", 1.0f), 0.4f, 2.0f);
    return p;
}

EnemyDefinition::EnemyDefinition() { damageMultipliers.fill(1.0f); }

const AttackDef* EnemyDefinition::attack(const std::string& attackId) const {
    for (const auto& a : attacks)
        if (a.id == attackId) return &a;
    return nullptr;
}

Json EnemyDefinition::toJson() const {
    Json j;
    j["format"] = "cosmic.enemy";
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["family"] = familyId(family);
    j["animation"] = animation;
    j["size"] = cosmic::toJson(size);
    j["scale"] = scale;
    j["hp"] = hp;
    j["contactDamage"] = contactDamage;
    j["speed"] = speed;
    j["chaseSpeedMultiplier"] = chaseSpeedMultiplier;
    j["flying"] = flying;
    j["knockbackResist"] = knockbackResist;
    j["detectionRange"] = detectionRange;
    j["attackRange"] = attackRange;
    j["loseRange"] = loseRange;
    j["needsLineOfSight"] = needsLineOfSight;
    j["movement"] = movementId(movement);
    j["reaction"] = reactionId(reaction);
    j["fleeHealthRatio"] = fleeHealthRatio;
    j["preferredDistance"] = preferredDistance;
    j["hopVelocity"] = hopVelocity;
    Json atk = Json::array();
    for (const auto& a : attacks) atk.push_back(a.toJson());
    j["attacks"] = atk;
    j["powers"] = {{"teleport", {{"enabled", canTeleport}, {"cooldown", teleportCooldown}, {"range", teleportRange}}},
                   {"defend", {{"enabled", canDefend}, {"duration", defendDuration}, {"cooldown", defendCooldown}}},
                   {"reinforcements", {{"enabled", callsReinforcements}, {"radius", alertRadius}}}};
    Json mult = Json::object();
    for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
        if (damageMultipliers[i] != 1.0f) mult[damageTypeId(static_cast<DamageType>(i))] = damageMultipliers[i];
    j["damageMultipliers"] = mult;
    Json drop = Json::array();
    for (const auto& d : drops) drop.push_back({{"type", d.type}, {"chance", d.chance}});
    j["rewards"] = {{"score", scoreValue}, {"drops", drop}};
    if (!deathSpawnId.empty() && deathSpawnCount > 0) j["deathSpawn"] = {{"id", deathSpawnId}, {"count", deathSpawnCount}};
    j["light"] = {{"color", lightColor.toHex()}, {"radius", lightRadius}};
    if (tint != Color::white()) j["tint"] = tint.toHex();
    if (!variantOf.empty()) j["variantOf"] = variantOf;
    j["sounds"] = {{"hurt", hurtSound}, {"death", deathSound}, {"alert", alertSound}};
    if (isBoss) {
        Json ph = Json::array();
        for (const auto& p : phases) ph.push_back(p.toJson());
        j["boss"] = {{"title", bossTitle}, {"phases", ph}};
        if (enrageTime > 0) j["boss"]["enrageTime"] = enrageTime;
        if (barIcon != "portrait") j["boss"]["barIcon"] = barIcon;
        if (!barColor.empty()) j["boss"]["barColor"] = barColor;
    }
    return j;
}

EnemyDefinition EnemyDefinition::fromJson(const Json& j) {
    EnemyDefinition d;
    d.id = jstr(j, "id");
    d.name = jstr(j, "name", d.id);
    d.description = jstr(j, "description");
    d.family = enumFromId(jstr(j, "family", "ground"), &familyId, EnemyFamily::Ground);
    d.animation = jstr(j, "animation", d.animation);
    d.size = vec2FromJson(j.value("size", Json()), d.size);
    d.size.x = std::clamp(d.size.x, 2.0f, 400.0f);
    d.size.y = std::clamp(d.size.y, 2.0f, 400.0f);
    d.scale = std::clamp(jget<float>(j, "scale", 1.0f), 0.25f, 4.0f);
    d.hp = std::clamp(jget<float>(j, "hp", 2.0f), 0.1f, 10000.0f);
    d.contactDamage = std::clamp(jget<float>(j, "contactDamage", 1.0f), 0.0f, 50.0f);
    d.speed = std::clamp(jget<float>(j, "speed", 40.0f), 0.0f, 1000.0f);
    d.chaseSpeedMultiplier = std::clamp(jget<float>(j, "chaseSpeedMultiplier", 1.6f), 0.1f, 5.0f);
    d.flying = jget<bool>(j, "flying", false);
    d.knockbackResist = std::clamp(jget<float>(j, "knockbackResist", 0.0f), 0.0f, 1.0f);
    d.detectionRange = std::max(0.0f, jget<float>(j, "detectionRange", 140.0f));
    d.attackRange = std::max(0.0f, jget<float>(j, "attackRange", 28.0f));
    d.loseRange = std::max(d.detectionRange, jget<float>(j, "loseRange", 260.0f));
    d.needsLineOfSight = jget<bool>(j, "needsLineOfSight", true);
    d.movement = enumFromId(jstr(j, "movement", "patrol"), &movementId, MovementType::Patrol);
    d.reaction = enumFromId(jstr(j, "reaction", "chase"), &reactionId, ReactionType::Chase);
    d.fleeHealthRatio = std::clamp(jget<float>(j, "fleeHealthRatio", 0.0f), 0.0f, 1.0f);
    d.preferredDistance = jget<float>(j, "preferredDistance", 110.0f);
    d.hopVelocity = jget<float>(j, "hopVelocity", 220.0f);
    if (j.contains("attacks") && j["attacks"].is_array())
        for (const auto& a : j["attacks"]) d.attacks.push_back(AttackDef::fromJson(a));
    if (j.contains("powers")) {
        const Json& p = j["powers"];
        if (p.contains("teleport")) {
            d.canTeleport = jget<bool>(p["teleport"], "enabled", false);
            d.teleportCooldown = jget<float>(p["teleport"], "cooldown", 4.0f);
            d.teleportRange = jget<float>(p["teleport"], "range", 110.0f);
        }
        if (p.contains("defend")) {
            d.canDefend = jget<bool>(p["defend"], "enabled", false);
            d.defendDuration = jget<float>(p["defend"], "duration", 1.2f);
            d.defendCooldown = jget<float>(p["defend"], "cooldown", 3.5f);
        }
        if (p.contains("reinforcements")) {
            d.callsReinforcements = jget<bool>(p["reinforcements"], "enabled", false);
            d.alertRadius = jget<float>(p["reinforcements"], "radius", 220.0f);
        }
    }
    if (j.contains("damageMultipliers") && j["damageMultipliers"].is_object())
        for (auto& [k, v] : j["damageMultipliers"].items())
            if (v.is_number()) d.damageMultipliers[static_cast<size_t>(damageTypeFromId(k))] = std::clamp(v.get<float>(), 0.0f, 10.0f);
    if (j.contains("deathSpawn") && j["deathSpawn"].is_object()) {
        d.deathSpawnId = jstr(j["deathSpawn"], "id");
        d.deathSpawnCount = std::clamp(jget<int>(j["deathSpawn"], "count", 2), 0, 6);
    }
    if (j.contains("rewards")) {
        d.scoreValue = jget<int>(j["rewards"], "score", 100);
        if (j["rewards"].contains("drops") && j["rewards"]["drops"].is_array())
            for (const auto& dj : j["rewards"]["drops"])
                d.drops.push_back({jstr(dj, "type", "energy_orb"), std::clamp(jget<float>(dj, "chance", 0.5f), 0.0f, 1.0f)});
    }
    if (j.contains("light")) {
        d.lightColor = colorFromJson(j["light"].value("color", Json()), d.lightColor);
        d.lightRadius = jget<float>(j["light"], "radius", d.lightRadius);
    }
    if (j.contains("tint")) d.tint = colorFromJson(j["tint"], Color::white());
    d.variantOf = jstr(j, "variantOf", "");
    if (j.contains("sounds")) {
        d.hurtSound = jstr(j["sounds"], "hurt", d.hurtSound);
        d.deathSound = jstr(j["sounds"], "death", d.deathSound);
        d.alertSound = jstr(j["sounds"], "alert", d.alertSound);
    }
    if (j.contains("boss") && j["boss"].is_object()) {
        d.isBoss = true;
        d.bossTitle = jstr(j["boss"], "title", d.name);
        d.enrageTime = std::clamp(jget<float>(j["boss"], "enrageTime", 0.0f), 0.0f, 3600.0f);
        d.barIcon = jstr(j["boss"], "barIcon", "portrait");
        d.barColor = jstr(j["boss"], "barColor", "");
        if (j["boss"].contains("phases") && j["boss"]["phases"].is_array())
            for (const auto& p : j["boss"]["phases"]) d.phases.push_back(BossPhase::fromJson(p));
        std::stable_sort(d.phases.begin(), d.phases.end(), [](const auto& a, const auto& b) { return a.hpThreshold > b.hpThreshold; });
    }
    return d;
}

}  // namespace cosmic
