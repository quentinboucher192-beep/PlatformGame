#pragma once

// 2.65: a branch of a clan raid (docs/RAIDS.md), phase 2: « BRAS DROIT » of Asterion. The
// catalog (data/raids/asterion/bras_droit/branch.json, written by tools/levelgen/raid_bras_droit.py,
// the server has its own copy) and the eight sub-zones, each a level with its raid metadata
// (spawn points, passages, objectives, magma flow, sealed door).
//
// The state of a branch is a set of objectives done: bit i = objective i of the catalog (fixed
// order). Everything else follows from it, the same way in the game and on the server: the
// generators, the shortcut, the passages open, the zones reachable and controlled.
//
// 2.68 (phase 4): the seven branches of the titan (the six others are written by
// tools/levelgen/raid_asterion.py, with the titan's catalog data/raids/asterion/titan.json). The
// titan links them (spec §7): an objective of one branch acts on another (a passage opens, a
// danger, defences or reinforcements stop). Each link is a bit of the target branch's state, 24
// and up (the objectives are below): the server sets it once the source objectives are accepted,
// the relay gives it at a step (RAID_STATE) like the rest of the state.

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
/// The first bit of a branch's state that is a link of the titan (objectives below).
constexpr int kLinkBit0 = 24;

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
    // 2.68: what the danger is called, what to do, what the band says while it runs (French;
    // the BRAS DROIT's magma flow when its zones do not say).
    std::string name = "Coulée de magma", hint = "montez", message = "Coulée de magma ! Restez sur les plateformes hautes";
};

/// 2.68: a link of the titan that acts on this branch (titan.json): its bit in the branch's
/// state, what it does ("open": the passages locked "link:<id>"; "disable": the objects of its
/// zones are switched off; "remove": the enemies (objects) of its zones are gone; "hazard_off":
/// the periodic danger of its zones stops), the zones it acts on and where it comes from.
struct EffectInfo {
    std::string id, kind, label;
    int bit = -1;
    std::vector<int> zones;
    std::vector<std::string> objects;
    std::vector<std::pair<std::string, std::string>> from;  // (branch id, objective id)
    bool inZone(int zone) const;
};

/// 2.68: the texts of a lock (catalog « lock_texts »): why a passage is closed, the alert when it
/// opens, the line of the objectives panel.
struct LockText {
    std::string hint, message, objective;
};
/// 2.68: a co-op mechanism of a zone (the expeditions to the lairs): when `players` players in
/// play (fewer when fewer are in play) stand together in `rect`, `action` is done on `targets`
/// (the doors of a lock on every floor open, a goods lift starts), once, for everyone.
struct CoopPlate {
    std::string id, label, message, action;
    Rect rect;
    int players = 2;
    std::vector<std::string> targets;
};
struct ZoneRules {
    int zone = 0;
    std::map<std::string, std::vector<Vec2>> spawns;  // "start", the zone one comes from, "cp:<checkpoint>"
    std::vector<Passage> passages;
    std::vector<ZoneObjective> objectives;
    Magma magma;
    std::string sealedDoor, sealedLabel;
    std::vector<CoopPlate> coops;             // 2.68
    std::vector<std::string> checkpoints;     // 2.68: the group starts again at the last one reached
};

class Branch {
public:
    /// The catalog and the eight zones from `dir` (data/raids/asterion/bras_droit); false with `error`.
    bool load(const std::filesystem::path& dir, std::string* error = nullptr);
    /// The catalog alone (tests, the server's copy).
    bool parseCatalog(const Json& j, std::string* error = nullptr);
    /// The branch `part` of the game (data/raids/asterion/<part>, with the titan's links), loaded
    /// once; null when it cannot be read. Without a part: the BRAS DROIT.
    static const Branch* builtin(const std::string& part = "bras_droit");
    /// 2.68: the links of the titan (titan.json) that act on this branch, and its revision.
    void attachTitan(const Json& titan);

    std::string id, name, gigaboss;
    std::string part = "bras_droit";  // the titan's part (its data folder)
    int revision = 0;
    int titanRevision = 0;
    std::map<std::string, LockText> lockTexts;
    std::vector<EffectInfo> effects;
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
    const EffectInfo* effect(const std::string& id) const;
    /// The relay's level of an instance: « raid:<id> », and its hash « <revision>.<titan revision> ».
    std::string levelId() const { return "raid:" + id; }
    std::string levelHash() const { return std::to_string(revision) + "." + std::to_string(titanRevision); }
    /// The texts of a lock (the catalog's, else those of the BRAS DROIT; a link: its own label).
    LockText lockText(const std::string& lock) const;

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
    /// 2.68: a link of the titan is on in `mask`; the periodic danger of `zone` is stopped by one.
    static bool on(uint32_t mask, const EffectInfo& e) { return e.bit >= 0 && e.bit < 32 && (mask >> e.bit) & 1u; }
    bool hazardOff(int zone, uint32_t mask) const;
};

/// 2.68: the titan (data/raids/asterion/titan.json, written by tools/levelgen/raid_asterion.py):
/// its seven branches in the order of its body and the links between them.
struct TitanBranch {
    std::string part, id, dir;
};
class Titan {
public:
    bool parse(const Json& j, std::string* error = nullptr);
    static const Titan* builtin();
    const TitanBranch* branch(const std::string& part) const;

    int revision = 0;
    std::vector<TitanBranch> branches;
    Json links = Json::array();
};

/// The metadata of a level read as raid rules (objective indices resolved in `b`).
ZoneRules parseZoneRules(const LevelData& level, const Branch& b);

}  // namespace cosmic::raid
