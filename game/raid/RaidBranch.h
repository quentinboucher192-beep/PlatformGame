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
struct ZoneRules {
    int zone = 0;
    std::map<std::string, std::vector<Vec2>> spawns;  // "start" or the zone one comes from
    std::vector<Passage> passages;
    std::vector<ZoneObjective> objectives;
    Magma magma;
    std::string sealedDoor, sealedLabel;
};

class Branch {
public:
    /// The catalog and the eight zones from `dir` (data/raids/asterion/bras_droit); false with `error`.
    bool load(const std::filesystem::path& dir, std::string* error = nullptr);
    /// The catalog alone (tests, the server's copy).
    bool parseCatalog(const Json& j, std::string* error = nullptr);
    /// The branch of the game (data dir), loaded once; null when it cannot be read.
    static const Branch* builtin();

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
