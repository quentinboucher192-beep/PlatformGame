#pragma once

// 2.65: a branch of a clan raid (docs/RAIDS.md), phase 2: « BRAS DROIT » of Asterion. The
// catalog (data/raids/asterion/bras_droit/branch.json, written by tools/levelgen/raid_bras_droit.py,
// the server has its own copy) and the eight sub-zones, each a level with its raid metadata
// (spawn points, passages, objectives, magma flow, sealed door).
//
// The state of a branch is a set of objectives done: bit i = objective i of the catalog (fixed
// order). Everything else follows from it, the same way in the game and on the server: the
// generators, the shortcut, the passages open, the zones reachable and controlled.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/levels/LevelData.h"

namespace cosmic::raid {

constexpr int kMaxInstancePlayers = 8;
constexpr const char* kBranchLevel = "raid:asterion_bras_droit";

struct ZoneInfo {
    int id = 0;
    std::string name, sub, level, inside, event;
    Vec2 mapPos;
    bool secret = false, boss = false;
    int groupsRequired = 1;
};

struct EdgeInfo {
    int a = 0, b = 0;
    std::string lock;  // "", "generators", "shortcut", "objective:<id>"
    bool secret = false;
};

struct ObjectiveInfo {
    std::string id;
    int index = 0, zone = 0;
    std::string label;
    bool optional = false;
    std::string role;  // "", "generator", "shortcut"
    std::string pair;  // objectives of one pair are done by distinct groups
};

/// The raid metadata of one sub-zone (LevelData::metadata["raid"]).
struct Passage {
    std::string id;
    int to = 0;
    Rect rect;
    std::string lock, door;
};
struct ZoneObjective {
    std::string id, source, trigger;
    int index = -1;
};
struct Magma {
    bool present = false;
    std::string zone;  // the damage zone switched on
    float period = 90.0f, first = 45.0f, warn = 3.0f, duration = 8.0f;
};
/// 2.67: the gigaboss of a zone (metadata.raid.gigaboss), its fight run by raid::Gigaboss.
struct GigabossRules {
    bool present = false;
    std::string boss;       // object id of the gigaboss in the level
    std::string name;       // « Pyroclast »
    std::string objective;  // the objective of the branch its fall accomplishes
    std::vector<std::string> ventIds;
    std::vector<Rect> vents;           // its vents: a player standing on one closes it
    int carapaceFrom = 2, carapaceUntil = 3;  // its carapace protects it in phases [from, until) (1-based)
    float hold = 3.0f;                 // seconds on a vent to close it
    float reopen = 15.0f;              // a closed vent lights again after this (unless all are closed)
    float exposed = 10.0f;             // its weak point bare this long once every vent is closed
    float exposedMult = 2.0f;          // the damage it takes meanwhile
    float jetPeriod = 6.0f, jetWarn = 1.2f, jetTime = 1.0f, jetHeight = 112.0f;  // an open vent's jet of magma
    float jetDamage = 1.0f;
    std::string weakEnemy;             // its weak points (enemies stuck to it while it is bare)
    std::vector<Vec2> weakSpots;       // from its feet, x toward where it faces
    float weakShare = 0.04f;           // share of its health a weak point takes from it when broken
    float hpPerPlayer = 0.55f;         // health × (1 + hpPerPlayer × (players − 1)), players seated (2 to 8)
    float weakHpPerPlayer = 0.25f;
    float pressurePerPlayer = 0.06f;   // its cooldowns ÷ (1 + pressurePerPlayer × (players in play − 1))
    /// « Poings de magma »: one fist per player in play every fistPeriods[phase − 1] seconds
    /// (÷ its pressure), announced fistWarn seconds before on a mark under each one.
    std::vector<float> fistPeriods;
    float fistWarn = 1.4f, fistWidth = 64.0f, fistDamage = 1.0f;
    /// The arena (world pixels): the camera's global view of the fight (display only).
    Rect arena;
};

/// 2.67: plates (or a lift's deck) that need several players standing on EACH at once
/// (metadata.raid.groupSwitches): `need` per plate (fewer when fewer seats are taken), held `hold`
/// seconds; then `action` (« open », « activate ») goes to each of `targets`.
struct GroupSwitch {
    std::string id, label, action;
    std::vector<Rect> plates;
    int need = 2;
    std::vector<std::string> targets;
    float hold = 1.5f;
};

/// 2.67: what a zone's way takes a group (metadata.raid.estimate, written by the level's generator).
struct RouteEstimate {
    bool present = false;
    int players = 8;
    float minutes = 0.0f;
    float routeX0 = 0.0f, routeX1 = 0.0f;  // the route (px)
    int climb = 0;                          // tiles climbed
};

struct ZoneRules {
    int zone = 0;
    std::map<std::string, std::vector<Vec2>> spawns;  // "start" or the zone one comes from
    std::vector<Passage> passages;
    std::vector<ZoneObjective> objectives;
    Magma magma;
    std::string sealedDoor, sealedLabel;
    GigabossRules gigaboss;  // 2.67
    std::vector<GroupSwitch> switches;  // 2.67
    RouteEstimate estimate;             // 2.67
};

class Branch {
public:
    /// The catalog and the eight zones from `dir` (data/raids/asterion/bras_droit); false with `error`.
    bool load(const std::filesystem::path& dir, std::string* error = nullptr);
    /// The catalog alone (tests, the server's copy).
    bool parseCatalog(const Json& j, std::string* error = nullptr);
    /// The branch of the game (data dir), loaded once; null when it cannot be read.
    static const Branch* builtin();
    /// 2.67: every branch of the raid in the game's data (data/raids/asterion/<branch>/branch.json,
    /// in the order of their folders), loaded once: the administrators' tests list them all, so a
    /// branch added later appears there without anything else to change.
    static const std::vector<const Branch*>& all();

    std::string id, name, gigaboss;
    int revision = 0;
    int startZone = 1;
    int generatorsNeeded = 2;
    std::vector<ZoneInfo> zones;
    std::vector<EdgeInfo> edges;
    std::vector<ObjectiveInfo> objectives;
    std::vector<LevelData> levels;     // by zone (index id - 1)
    std::vector<ZoneRules> rules;      // by zone

    const ZoneInfo* zone(int id) const;
    const ObjectiveInfo* objective(const std::string& id) const;
    int objectiveIndex(const std::string& id) const;
    const LevelData* level(int zone) const;
    const ZoneRules* zoneRules(int zone) const;

    // --------------------------------------------------------------- rules of a state
    static bool done(uint32_t mask, int index) { return index >= 0 && index < 32 && (mask >> index) & 1u; }
    int generators(uint32_t mask) const;
    bool shortcut(uint32_t mask) const;
    bool lockOpen(const std::string& lock, uint32_t mask) const;
    /// Every objective of the zone that is not optional is done (a zone without one: never).
    bool controlled(int zone, uint32_t mask) const;
    /// Zones reachable from the start (by edges whose lock is open), by id (index 0 unused).
    std::vector<bool> reachable(uint32_t mask) const;
    std::vector<const EdgeInfo*> edgesOf(int zone) const;
    /// The other objective of a pair already done in `mask` (empty when none).
    std::string pairDone(const ObjectiveInfo& o, uint32_t mask) const;
};

/// The metadata of a level read as raid rules (objective indices resolved in `b`).
ZoneRules parseZoneRules(const LevelData& level, const Branch& b);

}  // namespace cosmic::raid
