#pragma once

#include <array>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"
#include "game/gameplay/Damage.h"

namespace cosmic {

enum class EnemyFamily { Ground, Robot, Flying, Turret, Fast, Heavy, Caster, MiniBoss, Count };
enum class MovementType { Idle, Patrol, Guard, Fly, Hop, Stationary, Count };
enum class ReactionType { None, Chase, KeepDistance, Flee, GuardZone, Count };
enum class AttackType { Contact, Melee, Lunge, Projectile, Spread, Arc, Slam, Charge, Laser, Summon, Rain, Eruption, Leap, Count };

const char* familyId(EnemyFamily f);
const char* familyLabel(EnemyFamily f);
const char* movementId(MovementType m);
const char* movementLabel(MovementType m);
const char* reactionId(ReactionType r);
const char* reactionLabel(ReactionType r);
const char* attackTypeId(AttackType a);
const char* attackTypeLabel(AttackType a);
template <typename E>
E enumFromId(const std::string& id, const char* (*idFn)(E), E fallback);

/// One attack of an enemy. Every attack is telegraphed (wind-up), then active, then
/// followed by a recovery window during which the enemy is vulnerable.
struct AttackDef {
    std::string id = "attack";
    std::string name = "Attaque";
    AttackType type = AttackType::Melee;
    float damage = 1.0f;
    float range = 28.0f;
    float minRange = 0.0f;
    float telegraph = 0.5f;
    float active = 0.2f;
    float recover = 0.5f;
    float cooldown = 1.5f;
    float weight = 1.0f;  // random selection weight
    std::string telegraphAnim = "telegraph";
    std::string attackAnim = "attack";
    // Projectiles
    std::string projectile = "drone_bolt";  // clip of animations/projectiles.json
    float projectileSpeed = 160.0f;
    int count = 1;
    float spread = 0.0f;  // degrees between projectiles
    float gravity = 0.0f;
    // Movement attacks
    float lungeSpeed = 260.0f;
    float knockback = 170.0f;
    // Summon
    std::string summonId = "glowmite";
    int summonCount = 2;
    int maxSummoned = 4;
    // Laser
    float laserLength = 320.0f;
    float sweepDegrees = 0.0f;
    std::string sound;
    std::string telegraphSound = "sounds/enemy_telegraph.wav";
    // v2
    bool hasOrigin = false;
    Vec2 origin;                       // projectile / beam origin from the feet (x mirrored with facing)
    float projectileRadius = 0.0f;     // 0 = automatic
    std::string hitEffect;             // particle preset on impact ("" = default)
    float slow = 0.0f;                 // > 0: hit slows the player to this speed ratio
    float slowDuration = 0.0f;
    float explosionRadius = 0.0f;      // > 0: projectiles explode (bombs)
    float rainHeight = 220.0f;         // Rain: spawn height above the player
    std::string warningEffect = "eruption_warning_fire";  // Eruption: telegraph particles on the ground
    std::string eruptionEffect = "eruption_fire";         // Eruption: burst when it erupts
    Color beamColor{255, 77, 109, 255};                    // Laser / breath colour
    /// Texture of the beam (animations/beams.json: flame, frost, plasma, thorn, star);
    /// "" = the drawn line of old. Appearance only (not in the level fingerprints).
    std::string beamTexture;
    float beamWidth = 14.0f;  // thickness of the textured beam (world px), appearance only
    // v3 (bosses): smarter attacks
    float lead = 0.0f;           // 0..1: aims where the player will be (projectiles, rain, eruptions, lunges, leaps)
    int bursts = 1;              // projectile attacks: volleys fired during the active window
    float burstInterval = 0.3f;  // seconds between volleys
    float homing = 0.0f;         // projectiles turn toward the player (radians per second)
    float homingTime = 1.2f;     // ... during this many seconds
    float stagger = 0.0f;        // rain / eruption: impacts one after another along a line (s between impacts)
    float leapTime = 0.8f;       // Leap: flight time to the predicted landing point

    bool isProjectileType() const {
        return type == AttackType::Projectile || type == AttackType::Spread || type == AttackType::Arc ||
               type == AttackType::Rain || type == AttackType::Eruption;
    }

    Json toJson() const;
    static AttackDef fromJson(const Json& j);
};

struct BossPhase {
    std::string name = "Phase";
    float hpThreshold = 1.0f;  // phase starts when hp ratio <= threshold
    std::vector<std::string> attacks;
    float speedMultiplier = 1.0f;
    float cooldownMultiplier = 1.0f;
    std::string enterText;
    bool summonOnEnter = false;
    int combo = 1;                    // attacks chained without a full recovery
    float telegraphMultiplier = 1.0f; // < 1: shorter wind-ups
    Json toJson() const;
    static BossPhase fromJson(const Json& j);
};

struct DropDef {
    std::string type = "energy_orb";  // energy_orb, health_orb, shard
    float chance = 0.5f;
};

/// Complete enemy archetype (data/enemies/<id>.json or the user's enemies folder).
struct EnemyDefinition {
    std::string id;
    std::string name;
    std::string description;
    EnemyFamily family = EnemyFamily::Ground;
    std::string animation = "animations/glowmite.json";
    Vec2 size{14, 10};
    float scale = 1.0f;
    float hp = 2.0f;
    float contactDamage = 1.0f;
    float speed = 40.0f;
    float chaseSpeedMultiplier = 1.6f;
    bool flying = false;
    float knockbackResist = 0.0f;  // 0..1
    float detectionRange = 140.0f;
    float attackRange = 28.0f;
    float loseRange = 260.0f;
    bool needsLineOfSight = true;
    MovementType movement = MovementType::Patrol;
    ReactionType reaction = ReactionType::Chase;
    float fleeHealthRatio = 0.0f;
    float preferredDistance = 110.0f;  // KeepDistance
    float hopVelocity = 220.0f;
    std::vector<AttackDef> attacks;
    // Special abilities ("pouvoirs")
    bool canTeleport = false;
    float teleportCooldown = 4.0f;
    float teleportRange = 110.0f;
    bool canDefend = false;
    float defendDuration = 1.2f;
    float defendCooldown = 3.5f;
    bool callsReinforcements = false;  // alerts nearby enemies when spotting the player
    float alertRadius = 220.0f;
    std::array<float, static_cast<size_t>(DamageType::Count)> damageMultipliers{};  // 1 = normal
    int scoreValue = 100;
    std::vector<DropDef> drops;
    Color lightColor{255, 77, 109, 255};
    float lightRadius = 30.0f;
    std::string hurtSound = "sounds/enemy_hurt.wav";
    std::string deathSound = "sounds/enemy_death.wav";
    std::string alertSound = "sounds/enemy_alert.wav";
    std::string deathSpawnId;  // spawned when killed (slimes splitting)
    int deathSpawnCount = 0;
    bool isBoss = false;
    std::string bossTitle;
    std::vector<BossPhase> phases;
    /// Seconds of fight after which the boss enrages (faster, shorter cooldowns); 0 = never.
    float enrageTime = 0.0f;
    /// Look of the boss on the level progress bar (and in the editors): "portrait" (its own
    /// sprite) or a symbol (crown, skull, flame, snowflake, eye, star); colour "" = gold.
    std::string barIcon = "portrait";
    std::string barColor;
    /// Colour the sprite is multiplied by (white = its own colours): spectral echoes, the
    /// Astrarque's starlight. Appearance only.
    Color tint = Color::white();
    /// For variants of another enemy (the echoes of the bosses): the original's id. The
    /// news of the main menu announce the original only.
    std::string variantOf;

    // Runtime bookkeeping
    std::string sourcePath;
    fs::path assetRoot;  // non-empty for packaged enemies
    bool builtin = false;

    EnemyDefinition();
    float multiplier(DamageType t) const { return damageMultipliers[static_cast<size_t>(t)]; }
    const AttackDef* attack(const std::string& attackId) const;
    Json toJson() const;
    static EnemyDefinition fromJson(const Json& j);
};

}  // namespace cosmic
