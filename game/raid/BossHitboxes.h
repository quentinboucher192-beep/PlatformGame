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
//   gravity generators: their rectangles in its lair), and where it stands (its object).
//
// Pure: no screen. Tested by tests/test_raid_sheets.cpp (a diagram per phase, the rectangles those of
// the fight's rules).

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {
class ContentDatabase;
struct EnemyDefinition;
struct LevelData;
}  // namespace cosmic

namespace cosmic::raid {

struct GigabossRules;

struct HitPart {
    enum class Kind { Weak, Armour, Hurt, Mechanism } kind = Kind::Weak;
    Rect rect;          // on its body: from its feet (0, 0), facing right; a plate: in its arena
    std::string label;  // « Point faible · Noyau de Pyroclast », « Corps (blindé) »…
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
};

/// Where a spot of its rules is on it: the point at its feet's level of its box's centre, `spot` from
/// there (x toward where it faces) — what Gigaboss and the routines place their weak points and targets at.
Vec2 spotFeet(const Rect& bossBox, float facing, Vec2 spot);

/// The diagram of a gigaboss from its fight's rules (`lair`: its zone's level, for where it stands).
BossHitboxes bossHitboxes(const GigabossRules& rules, const EnemyDefinition& boss, const ContentDatabase& content, const LevelData* lair, int phaseCount);

}  // namespace cosmic::raid
