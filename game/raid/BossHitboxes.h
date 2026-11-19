#pragma once

// 2.75: where to hit a gigaboss, by phase and state (docs/RAIDS.md §18): the diagram of its sheet
// (ui/screens/RaidSheetsScene, « Mécanismes » › « Où frapper »). Every rectangle comes from the data the
// fight itself reads, never placed by hand:
//
// * its body: the enemy's box (EnemyDefinition::size from its feet, as Enemy places it); it hurts on
//   contact (contactDamage); while its armour holds (the phases [carapace) of its rules, `veil` for
//   Skullmaw, when it has a mechanism) nothing hurts it;
// * its weak points: the spots of its rules (GigabossRules::weakSpots, each its enemy's size), placed
//   by raid::spotFeet — the very function Gigaboss uses to stick them to it — while it is bare;
// * the targets of its mechanism on its body (Titanus's restraints, Heartbreaker's plates: `spots` of
//   its rules), what threatens from it (Skullmaw's eyes);
// * the plates of its collective mechanism in its arena (vents, hooks, anchors, arteries, mirrors,
//   gravity generators: their rectangles in its lair), and where it stands (its object);
// * 2.75.2: Heartbreaker's plates guard it in its FIRST phase (HeartbreakerRoutine), its ribs in its
//   armour phases; the sprites of its weak points and targets where the fight sticks them; and each of
//   its attacks, on the pose of its own animation, with what it sends from its body (hitAttack).
//
// Pure: no screen. Tested by tests/test_raid_sheets.cpp (a diagram per phase, the rectangles those of
// the fight's rules).

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {
class ContentDatabase;
struct AttackDef;
struct EnemyDefinition;
struct LevelData;
}  // namespace cosmic

namespace cosmic::raid {

struct GigabossRules;

struct HitPart {
    enum class Kind { Weak, Armour, Hurt, Mechanism } kind = Kind::Weak;
    Rect rect;          // on its body: from its feet (0, 0), facing right; a plate: in its arena
    std::string label;  // « Point faible · Noyau de Pyroclast », « Corps (blindé) »…
    std::string enemy;  // 2.75.2: the enemy stuck there (a weak point, a target), its feet at the rect's bottom: its sprite drawn there ("" none)
};

/// 2.75.2: what one of its attacks sends from its body, from its feet facing right (its type's code:
/// game/enemies/EnemyAttacks, GuardianAttacks, GolemAttacks; attackOriginOf, orbitRadii).
struct HitShape {
    enum class Kind {
        Area,    // `rect` hurts (a rush: its whole body; a blow at its feet; where it lands)
        Origin,  // its shots and beams leave from `at`
        Ring,    // a circle of `radius` around `at` (its crown of flames, its expanding ring; its shots born on it when !hurts)
        Beam,    // a beam from `at` to `to`
        Wave,    // a shockwave along the floor from `at` to `to`
    } kind = Kind::Area;
    Rect rect;
    Vec2 at, to;
    float radius = 0.0f;
    bool hurts = true;  // false: where it comes from (drawn pink), true: what hurts (red)
};

/// 2.75.2: one of its attacks on the diagram: the pose of its own animation and what it sends.
struct HitAttack {
    std::string id, name;
    std::vector<int> phases;                 // the phases that use it (1-based)
    std::string telegraphClip, attackClip;   // the clips of its sheet: its wind-up, then its blow
    std::vector<HitShape> shapes;
    std::string reach;                       // « Ruée : tout son corps blesse »…
};

/// One phase in one state: « Combat », « Blindé » (its mechanism to do first), « À nu ».
struct HitboxView {
    int phase = 1;
    std::string state;      // "fight", "armour", "exposed"
    std::string stateName;  // « Combat », « Blindé », « À nu »
    std::string hitWhen;    // « Frappez ici quand… »
    std::vector<HitPart> parts;  // on its body
    std::vector<HitPart> plates; // the collective mechanism's plates in its arena (armour only)
};

struct BossHitboxes {
    Rect body;          // from its feet
    Vec2 feet;          // in its lair (its object), for the arena's strip
    Rect arena;         // its arena (metadata.raid.arena), else what holds it and its plates
    std::string mechanism;  // « Évents » …: what its plates are called ("" none)
    std::vector<HitboxView> views;  // by phase, then state
    std::vector<HitAttack> attacks;  // 2.75.2: those of its phases, in the order of its definition
};

/// 2.75.2: what `a` sends from the body of `boss`, from its feet facing right, and its sentence.
HitAttack hitAttack(const AttackDef& a, const EnemyDefinition& boss);

/// Where a spot of its rules is on it: the point at its feet's level of its box's centre, `spot` from
/// there (x toward where it faces) — what Gigaboss and the routines place their weak points and targets at.
Vec2 spotFeet(const Rect& bossBox, float facing, Vec2 spot);

/// The diagram of a gigaboss from its fight's rules (`lair`: its zone's level, for where it stands).
BossHitboxes bossHitboxes(const GigabossRules& rules, const EnemyDefinition& boss, const ContentDatabase& content, const LevelData* lair, int phaseCount);

}  // namespace cosmic::raid
