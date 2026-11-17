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
enum class AttackType {
    Contact, Melee, Lunge, Projectile, Spread, Arc, Slam, Charge, Laser, Summon, Rain, Eruption, Leap,
    // 2.19: the attacks of the guardians of Aethra (game/enemies/GuardianAttacks.cpp).
    Tracker,     // a bump runs under the ground toward the target, then a spike bursts out
    Cage,        // two walls of thorns rise on both sides of the target; spores fall between them
    Vortex,      // the target is pulled toward the enemy (a limited pull: running away is enough)
    Mirror,      // two translucent doubles copy its attacks (reduced damage, one hit breaks them)
    FrostFloor,  // the ground of the arena turns to ice for a while (less grip)
    Prism,       // a beam that bounces off walls and ceiling, its path shown during the wind-up
    Orbit,       // flames circle around the enemy, the ring widening then closing
    Spiral,      // shots in a turning spiral of several arms
    Flood,       // lava rises up to the platforms for a while, then goes down
    Well,        // a black hole pulls the target and bends the player's shots
    Blink,       // a mark behind the target; the enemy teleports there and strikes
    Ring,        // a ring expands from the enemy, with a single opening
    // 2.50: the attacks of Pyrrhax, the dragon of Les Confins (game/enemies/DragonAttacks.cpp).
    Dive,       // a line toward where the target will be, then a dash through it and beyond; it climbs back
    Perch,      // it lands where the target stands, breathes a lasting cloud of fire along the ground, takes off
    CloudShot,  // fireballs that leave a burning cloud where they burst
    Gust,       // its wings push the target away (a limited push), ash and embers in the wind
    // 2.52: the attacks of the Monolithe, the golem of Les Confins (game/enemies/GolemAttacks.cpp).
    Shell,    // it curls into a stone shell and rolls at the target, bouncing off the walls; armoured, then dizzy
    Quake,    // the arena shakes: cracks open under the target and burst, rocks fall from the roof on marks
    Skyfall,  // it leaps out of sight, its shadow follows the target, it falls on it: shockwaves along the floor
    // 2.56: the attacks of the Phénix, the phoenix of Les Confins (game/enemies/PhoenixAttacks.cpp).
    Sunbeam,  // columns of sunlight fall from the roof onto marks, one after the other (a march, or from both walls)
    Sweep,    // its wings throw a blade of feathers across the arena, low (jump over it) or high (stay down)
    // 2.57: the spells of the Liche, the lich of Les Confins (game/enemies/LicheAttacks.cpp).
    Glyph,  // runes traced on the ground she stands on: armed, a rune bursts when she steps on it, all burst at their end
    Hex,    // a circle of runes closes around her: still inside, she is cursed (her jumps grow heavy for a while)
    // 2.58: the machinery of the Titan Mécanique, the war machine of Les Confins (game/enemies/TitanAttacks.cpp).
    Press,    // the presses of its foundry come down from the roof onto the floor (every other one, all but one, or a march)
    Missile,  // reticles lock onto the target one after another; a missile falls on each locked spot
    Belt,     // the conveyor belts of its foundry start running (toward it, or away from it) for a while
    Count
};

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
    // v4 (2.19, guardians of Aethra): parameters of the new attack types. 0 = the type's
    // default (see GuardianAttacks.cpp), so an attack made in the editor works at once.
    float duration = 0.0f;  // how long the effect lasts (s); Mirror: life of the doubles (0 = until broken)
    float pull = 0.0f;      // Vortex / Well: pull speed (px/s), always below the player's run speed
    float radius = 0.0f;    // Cage: half the gap; Orbit: widest ring; Well: core; Blink: strike; Tracker: spike half-width
    float height = 0.0f;    // Cage: walls; Flood: lava above the floor; Tracker: spike
    int bounces = 0;        // Prism: bounces off walls and ceiling
    int arms = 0;           // Spiral (and Orbit with a spiral): arms of shots
    float grip = 0.0f;      // FrostFloor: grip of the frozen ground (1 = normal)
    // Prism: `arms` 2 fires two beams mirrored on both sides of the enemy, which cross (2.33).
    /// Look of a lasting effect (2.33, appearance only, not in the fingerprints): Flood
    /// "plasma" (a tide of star plasma instead of lava), Cage "star" (walls of starlight).
    /// 2.53 (the Kraken): Flood "tide" (dark water), Cage "tentacle" (tentacles out of the floor),
    /// CloudShot / Perch "ink" (clouds of ink instead of fire).
    /// 2.56 (the Phénix): CloudShot "ash" (clouds of grey ash, embers glowing in them).
    /// 2.57 (the Liche): Cage "bone" (walls of stacked bones, skulls and ribs).
    std::string look;
    // v5 (2.50, the dragon): Perch: damage it takes while landed (0 = the type's default, 1.5).
    // 2.52, the golem: Shell: damage it takes while dizzy after its roll (0 = 1.5).
    float vulnerability = 0.0f;
    // v6 (2.52, the golem): Shell: damage it takes while curled in its shell (0 = the type's default, 0.35).
    float armor = 0.0f;
    // v7 (2.57, the Liche): Hex: how high the cursed player still jumps (a ratio of her jumps, 0 =
    // the type's default, 0.72) for `duration` seconds.
    float curse = 0.0f;

    bool isProjectileType() const {
        return type == AttackType::Projectile || type == AttackType::Spread || type == AttackType::Arc ||
               type == AttackType::Rain || type == AttackType::Eruption;
    }
    /// The attack types of the guardians of Aethra (2.19).
    bool isGuardianType() const { return type >= AttackType::Tracker && type <= AttackType::Ring; }
    /// The attack types of the dragon (2.50, Pyrrhax).
    bool isDragonType() const { return type >= AttackType::Dive && type <= AttackType::Gust; }
    /// The attack types of the golem (2.52, the Monolithe).
    bool isGolemType() const { return type >= AttackType::Shell && type <= AttackType::Skyfall; }
    /// The attack types of the phoenix (2.56, the Phénix).
    bool isPhoenixType() const { return type >= AttackType::Sunbeam && type <= AttackType::Sweep; }
    /// The attack types of the lich (2.57, the Liche).
    bool isLicheType() const { return type >= AttackType::Glyph && type <= AttackType::Hex; }
    /// The attack types of the war machine (2.58, the Titan Mécanique).
    bool isTitanType() const { return type >= AttackType::Press && type <= AttackType::Belt; }

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
    /// Seconds in this phase after which the boss enrages (2.33, the giga boss's last phase);
    /// 0 = only the boss's own enrageTime.
    float enrageTime = 0.0f;
    // Appearance only (2.33, not in the fingerprints):
    std::string animation;           // sheet of this phase ("" = the enemy's own): a new look
    Color lightColor{0, 0, 0, 0};    // its light during this phase (alpha 0 = the enemy's own)
    std::string enterSound;          // played when the phase starts, with the roar
    /// 2.56, the Phénix's « Cendres »: when its health reaches the next phase's threshold, the
    /// boss burns to ashes on the floor of its arena (it cannot be hurt), then is reborn with
    /// this share of its health (> 0) and the next phase begins. 0 = the phase ends as any other.
    float rebirth = 0.0f;
    /// Appearance only (2.56): ash falls over the arena and veils it during this phase (and
    /// while the boss lies in its ashes), Nova and the HUD stay clear.
    bool ashes = false;
    /// 2.57, the Liche's « Phylactère »: when its health reaches the next phase's threshold, the
    /// boss hides its soul in its phylactery (this enemy, set on one of its altars): it cannot be
    /// hurt until the crystal is broken, then the next phase begins. "" = the phase ends as any other.
    std::string phylactery;
    /// 2.58, the Titan's « Surchauffe »: each attack it ends fills this share of its heat gauge;
    /// full, it vents its steam (low jets on both sides, then its core laid bare: it takes more
    /// damage for a while) and the gauge empties. 0 = no gauge in this phase.
    float heat = 0.0f;
    /// 2.58, the Titan's « Calage »: when its health reaches the next phase's threshold, the machine
    /// stalls (it cannot be hurt) and these enemies (its pressure valves) appear on the valve spots
    /// of its arena: all of them turned before it starts again, its boiler bursts (a heavy blow);
    /// otherwise it restarts with a pressure wave. Then the next phase begins. "" = no stall.
    std::string stall;
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
    /// From this phase on (1 = the first), the arena darkens around the boss (Nyxara, 2.19);
    /// 0 = never. Appearance only.
    int darkPhase = 0;
    /// Colour the sprite is multiplied by (white = its own colours): spectral echoes, the
    /// Astrarque's starlight. Appearance only.
    Color tint = Color::white();
    /// For variants of another enemy (the echoes of the bosses): the original's id. The
    /// news of the main menu announce the original only.
    std::string variantOf;
    /// Its last stand (2.33, Singularis's « Dernière lumière »): when its health runs out the
    /// boss does not die at once: it cannot be hurt any more, goes to the middle of its arena
    /// and performs these attacks one after the other, then dies. Empty = it dies at once.
    std::vector<std::string> finalAttacks;
    std::string finalText;  // the message shown when the last stand begins

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
