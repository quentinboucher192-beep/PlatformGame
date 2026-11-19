#pragma once

// 2.76.0: the hit parts of a boss, drawn from its sprite (docs/HITPARTS.md).
//
// tools/assetgen/hitparts.py writes, for every sheet a boss is drawn with, assets/hitparts/<sheet>.json:
// for every cell a clip shows, its named parts (head, wings, tail… or its body), each a few CONVEX
// polygons whose vertices are pixel corners of the frame (integers). The game places them as it places
// the frame (Animator::destRect): (vertex − pivot) × scale from its feet, mirrored when it faces left.
//
// The rules of the parts are in data/enemies/<id>.json: « parts » (how its sprite is cut, `profile`; each
// part's role — weak point, normal, armour, hurts —, damage multiplier, and the attacks during which it
// also hurts, `hurtDuring`) and « partsFromVersion », the version of the simulation from which its fight
// uses them (absent: its sheets show them, its fight keeps its box). They are not part of an enemy's
// definition (EnemyDefinition) nor of a level's fingerprint: the records are kept; the rules of the run
// decide (a replay of before plays with its box). docs/HITBOX_PARTS.md: how to plug a boss in.
//
// A boss that fights by parts (Pyrrhax from 16, the gigaboss of the campaign from 17, GameWorld::simulationVersion()
// ≥ partsFromVersion): its animation advances in the fixed step (its pose is part of the simulation; from 17
// the sheet of its phase too, kPartsLookSimulation), the player's blows and powers hit the
// part they touch — weak point first, then a normal part, then armour — times its multiplier, and only its
// parts that hurt (claws, breath, its head while it bites) hurt on contact. Every other boss keeps its box;
// its sheets show its parts (« affichage »).
//
// Collision: separating axes between a convex polygon and a box or a circle, in float with the operations
// in a fixed order (the build has -ffp-contract=off): the same on every computer.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class Enemy;
class GameWorld;
struct AnimationClip;
struct AnimationSet;

/// The version of the simulation of the first boss that fights by parts (Pyrrhax, 2.76.0); each boss names
/// its own (« partsFromVersion »).
constexpr int kHitPartsSimulation = 16;
/// 2.76.2: from this version, a boss that fights by parts takes the sheet of its phase in the fixed step
/// (its pose kept, Animator::swapSet), never in a display update: its parts are the same at any frame rate.
constexpr int kPartsLookSimulation = 17;

/// One convex piece of a part in a cell: vertices in the frame's pixels (origin at its top left).
struct HitPartPiece {
    int part = 0;
    std::vector<Vec2> pts;
};

/// The hit parts of a sheet (assets/hitparts/<sheet>.json).
struct HitPartsSheet {
    std::string sheet, profile;
    int frameWidth = 0, frameHeight = 0;
    Vec2 pivot;
    std::vector<std::string> parts;                                  // their names, in the file's order
    std::map<std::pair<int, int>, std::vector<HitPartPiece>> cells;  // (row, column) of the sheet
    /// The pieces of a cell (null: a cell no clip shows).
    const std::vector<HitPartPiece>* cell(int row, int column) const;
    /// The pieces of frame `frame` of `clip` of `set` (null without them).
    const std::vector<HitPartPiece>* frame(const AnimationSet& set, const AnimationClip& clip, int frame) const;
    /// Reads a file's JSON ("sameAs" is resolved by of()). Null when it is not a hit parts file.
    static std::shared_ptr<HitPartsSheet> parse(const Json& j);
    /// The hit parts of a sheet (its JSON's path relative to assets, "animations/pyrrhax.json"); read
    /// once and kept (thread-safe). Null when the sheet has none.
    static const HitPartsSheet* of(const std::string& sheetPath);
};

enum class PartRole { Weak, Normal, Armour, Hurt };

struct PartRule {
    std::string name;
    PartRole role = PartRole::Normal;
    float damage = 1.0f;                  // the multiplier of the damage it takes (0 for a part that only hurts)
    std::vector<std::string> hurtDuring;  // it also hurts during these attacks (its head while it bites)
};

/// The rules of an enemy's parts (data/enemies/<id>.json « parts »).
struct PartRules {
    int fromVersion = 0;  // « partsFromVersion »: its fight uses its parts from this version on (0: never, drawn only)
    std::string profile;  // how hitparts.py cuts its sprite ("dragon", "" = its silhouette)
    std::vector<PartRule> rules;
    /// 2.76.2 (« origins »): where the shots of an attack leave when it fights by parts, in pixels of its
    /// sprite from its feet (× its scale, like an attack's origin): its drawing, without changing its
    /// definition (nor the fingerprint of its levels); a replay of before keeps the attack's own.
    std::map<std::string, Vec2> origins;
    const PartRule* rule(const std::string& name) const;
    /// The origin of an attack's shots by its parts (null: the attack's own).
    const Vec2* origin(const std::string& attackId) const;
    /// The role and multiplier of a part without a rule: a normal part, × 1.
    PartRule ruleOr(const std::string& name) const;
    /// `parts`: the « parts » block; `fromVersion`: « partsFromVersion » (0 without it).
    static PartRules parse(const Json& parts, int fromVersion = 0);
    /// The rules of an official enemy (read once and kept, thread-safe); empty without « parts ».
    static const PartRules& of(const std::string& enemyId);
    /// 2.76.2: reads them again from data/enemies/<id>.json (after the enemy editor wrote them).
    static void reload(const std::string& enemyId);
};

/// 2.76.2 (enemy editor): the text of an enemy's file (data/enemies/<id>.json) with the roles of its parts
/// replaced by `rules` — every other key, the order of the keys and the roles already there, their
/// indentation kept (a file written with its own rules comes back byte for byte). Empty with `error` set
/// when the text is not an enemy's file.
std::string withPartRules(const std::string& fileText, const PartRules& rules, std::string* error = nullptr);
/// Writes them into data/enemies/<id>.json and reads them again (PartRules::reload). False with `error`.
bool savePartRules(const std::string& enemyId, const PartRules& rules, std::string* error = nullptr);

const char* partRoleId(PartRole r);
/// « Point faible », « Partie normale », « Blindée », « Blesse ».
const char* partRoleLabel(PartRole r);
/// « × 2 », « × 0,5 », « — » (a part that only hurts).
std::string partDamageLabel(const PartRule& r);

/// A piece placed in the world: its feet at `feet`, × `scale`, mirrored when `flip` (facing left).
std::vector<Vec2> placePiece(const HitPartPiece& piece, const HitPartsSheet& sheet, Vec2 feet, float scale, bool flip);
/// Separating axes: a convex polygon (either winding) against a box or a circle.
bool convexOverlapsRect(const std::vector<Vec2>& poly, const Rect& r);
bool convexOverlapsCircle(const std::vector<Vec2>& poly, Vec2 c, float radius);
/// Whether a point is inside a convex polygon (either winding).
bool convexContains(const std::vector<Vec2>& poly, Vec2 p);

struct PartHit {
    std::string name;
    PartRole role = PartRole::Normal;
    float damage = 1.0f;
};

/// Whether `e` fights by parts in `w`: its sheet has them, the run's rules are ≥ its « partsFromVersion ».
bool fightsByParts(const Enemy& e, const GameWorld& w);
/// The part of `e` (its current pose) that `area` / the circle touches: a weak point first, then a
/// normal part, then armour (a part that only hurts is never hit). nullopt: none.
std::optional<PartHit> partHitBy(const Enemy& e, const Rect& area);
std::optional<PartHit> partHitBy(const Enemy& e, Vec2 c, float radius);
/// Whether a blow reaches `e` in `w`: by its parts when it fights by them (`mult` gets the multiplier of
/// the part touched), else by its box exactly as before 2.76 (`mult` 1).
bool reaches(const Enemy& e, const GameWorld& w, const Rect& area, float* mult);
bool reaches(const Enemy& e, const GameWorld& w, Vec2 c, float radius, float* mult);
/// Whether a part of `e` that hurts touches `box` (its claws, its breath; a part of `hurtDuring` while
/// that attack is under way).
bool hurtsWithParts(const Enemy& e, const Rect& box);

}  // namespace cosmic
