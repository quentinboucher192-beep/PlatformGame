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

#include <algorithm>
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
/// 2.78: the armory before the lair (« Vestiaire de l'antre », armory_lair.json): a zone of the game only,
/// between the zone before the gigaboss's lair and the lair (never in the server's catalog, never reported).
constexpr int kLairArmory = 9;

struct ZoneInfo {
    int id = 0;
    std::string name, sub, level, inside, event;
    Vec2 mapPos;
    bool secret = false, boss = false;
    int groupsRequired = 1;
};

struct EdgeInfo {
    int a = 0, b = 0;
    std::string lock;  // "", "generators", "shortcut", "objective:<id>", 2.78: "ambushes:<zone>"
    bool secret = false;
};

struct ObjectiveInfo {
    std::string id;
    int index = 0, zone = 0;
    std::string label;
    bool optional = false;
    std::string role;  // "", "generator", "shortcut", "gigaboss", 2.78: "ambush" (an ambush of the zone before a lair)
    std::string pair;  // objectives of one pair are done by distinct groups
};

/// The raid metadata of one sub-zone (LevelData::metadata["raid"]).
struct Passage {
    std::string id;
    int to = 0;
    Rect rect;
    std::string lock, door;
    /// 2.78: a passage of the group (the armory's « Prêts »): the co-op plate (its id in `coops`) that
    /// opens it; at the step the plate is held by the players it needs, those standing on the plate take
    /// the passage together (nobody else). "" = a passage each player takes alone by walking into it.
    std::string plate;
    /// 2.78: the zone of the game the passage really leads to when it is not `to` (the lair's armory on
    /// the way to the lair: `to` stays the zone of the catalog, the edge the server knows), -1 none.
    int via = -1;
    /// The zone a player who takes it arrives in.
    int arrival() const { return via >= 0 ? via : to; }
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
    /// 2.68 (phase 3): plates of one group must all be held at the same step (the twin plates of
    /// the Poignet-verrou); "" = alone. Alone in the instance, the first plate of a group suffices.
    std::string group;
    /// 2.81.2 (titan 11): the ambushes of its zone laid before it (their objectives, `ambushIndex` their bits in
    /// the branch's state): it waits for every one of them beaten, besides its players (« Embuscade à vaincre
    /// d'abord »); never one only it leads to (tools/levelgen: check_mechanisms).
    std::vector<std::string> ambushes;
    std::vector<int> ambushIndex;
};

/// 2.68: the arena of a gigaboss (metadata.raid.arena, the same format for the seven branches):
/// its rectangle (the camera's global view, game/raid/ArenaCamera), its floor, its door, where the
/// group enters.
struct ArenaInfo {
    bool present = false;
    Rect rect;
    float floor = 0.0f;
    std::string door;
    Vec2 entry;
};
/// 2.68: the gigaboss of a zone (metadata.raid.gigaboss), its fight run by raid::Gigaboss.
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
    /// 2.69 (phase 4B): the enemy of each spot when it names its own ("" = weakEnemy).
    std::vector<std::string> weakEnemies;
    const std::string& weakEnemyAt(size_t k) const { return k < weakEnemies.size() && !weakEnemies[k].empty() ? weakEnemies[k] : weakEnemy; }
    float weakShare = 0.04f;           // share of its health a weak point takes from it when broken
    float hpPerPlayer = 0.55f;         // health × (1 + hpPerPlayer × (players − 1)), players seated (2 to 8)
    float weakHpPerPlayer = 0.25f;
    float pressurePerPlayer = 0.06f;   // its cooldowns ÷ (1 + pressurePerPlayer × (players in play − 1))
    /// « Poings de magma »: one fist per player in play every fistPeriods[phase − 1] seconds
    /// (÷ its pressure), announced fistWarn seconds before on a mark under each one.
    std::vector<float> fistPeriods;
    float fistWarn = 1.4f, fistWidth = 64.0f, fistDamage = 1.0f;
    /// 2.69 (phase 4B): what the other gigaboss of Asterion say and do beyond these rules. Their
    /// routine (game/raid/GigabossRoutine: "voidworm", "graviton", "titanus"; "" = Pyroclast's
    /// own rules above), read from `extra` (the whole metadata.raid.gigaboss); the objectives of
    /// the branch it reads (`links`: name -> bit of the branch's state, -1 unknown); the names of
    /// its blows and the texts of its fall (Pyroclast's by default); the sheet of its vents and
    /// marks (drawn by the combat screen); one line for the maps of the branch.
    std::string routine;
    Json extra;
    std::map<std::string, int> links;
    std::string fistName = "Poings de magma", jetName = "Jet de magma";
    bool fistRise = false;  // 2.69: its blows on the marks come out of the ground (drawn rising, not falling)
    std::string fallText = "Le bras droit d'Asterion s'éteint.";
    std::string goneText = "La Main est éteinte";
    std::string fx = "animations/pyroclast_vents.json";
    std::string summary = "Sa carapace : fermez ses 3 évents ensemble (3 s sur chacun) pour mettre son cœur à nu, puis brisez ses points faibles.";
    /// 2.74 (phase 6): the titan's links acting on its branch (id → bit of the branch's state), set
    /// when the titan is attached: the Cœur d'Asterion reads its « modifier » links (a part fallen).
    std::map<std::string, int> titanLinks;
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
    ArenaInfo arena;                          // 2.68: a gigaboss's arena (metadata.raid.arena)
    GigabossRules gigaboss;                   // 2.68 (phase 3): its gigaboss, when it is fought there
    /// 2.78: the way through the zone before a lair (metadata.raid.route, pixels): the distance left to the
    /// lair's passage is measured along it (the raids' HUD, « Passage vers l'antre : 124 m »).
    std::vector<Vec2> route;
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
    /// Every branch of the titan's catalog (data/raids/asterion/titan.json), in the order of its body,
    /// loaded once: the administrators' tests list them all, so a branch added later appears there
    /// without anything else to change.
    static const std::vector<const Branch*>& all();
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
    /// 2.75: the armory (« vestiaire », armory.json, tools/levelgen/raid_armory.py): zone 0, before the
    /// branch's zones, where each player picks the powers they take into the raid (the power tablets,
    /// their choice through InputFrame), then the whole group on the « Prêts » plate opens the way to
    /// the start zone. Not in the server's catalog: never reported, never on the maps.
    std::optional<LevelData> armoryLevel;
    ZoneRules armoryRules;
    ZoneInfo armoryInfo;
    bool hasArmory() const { return armoryLevel.has_value(); }
    /// 2.78: the armory before the lair (zone kLairArmory, armory_lair.json): the same tablets and « Prêts »
    /// plate as the first one, between the expedition and the gigaboss's arena (or the Cœur's).
    std::optional<LevelData> lairArmoryLevel;
    ZoneRules lairArmoryRules;
    ZoneInfo lairArmoryInfo;
    bool hasLairArmory() const { return lairArmoryLevel.has_value(); }
    /// 2.78: a zone where the players choose their powers (the first armory, 0, or the lair's).
    static bool isArmory(int zone) { return zone == 0 || zone == kLairArmory; }

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
    /// 2.78: the ambushes of `zone` (its objectives of role « ambush »): how many there are, how many
    /// are still to beat in `mask` (the lock « ambushes:<zone> » opens at 0).
    struct AmbushCount {
        int left = 0, total = 0;
    };
    AmbushCount ambushes(int zone, uint32_t mask) const;
    /// 2.78: « Embuscades restantes : 2 sur 4 » (what the closed passage and « Ce qu'il reste à faire » say).
    static std::string ambushesLeftText(const AmbushCount& c);
    /// Titan 9 (the ambush gates): whether the ambushes of `zone` open the way to a lair (the zone before a
    /// gigaboss: « l'antre s'ouvre »), else only the way on to the next zones of the branch (the entry's guard).
    bool ambushesOpenLair(int zone) const;
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
    /// 2.74 (phase 6): its heart, the final fight (titan.json « heart »: part « coeur »), apart from
    /// the seven branches of its body; empty part when the titan has none.
    TitanBranch heart;
};

/// 2.74 (phase 6): the shared state of the Cœur d'Asterion given to an instance with the branch's
/// state (RAID_STATE, relay → game, at a step): the health the OTHER instances of the event took
/// from it (ten-thousandths, validated by their consensus), the shared phase (1 to 4, 5: fallen,
/// validated), the relics of the seven branches validated (each one weakens it). Every game of the
/// instance applies it at that same step (lockstep stays exact). Zero everywhere else.
struct HeartShared {
    uint16_t others = 0;  // 0..10000
    uint8_t phase = 0;
    uint8_t relics = 0;
    bool operator==(const HeartShared& o) const { return others == o.others && phase == o.phase && relics == o.relics; }
    bool operator!=(const HeartShared& o) const { return !(*this == o); }
    /// The u32 of RAID_STATE (the relay's coop.ts writes the same): others | phase << 16 | relics << 24.
    uint32_t pack() const { return static_cast<uint32_t>(others) | (static_cast<uint32_t>(phase) << 16) | (static_cast<uint32_t>(relics) << 24); }
    static HeartShared unpack(uint32_t v) {
        HeartShared h;
        h.others = static_cast<uint16_t>(std::min<uint32_t>(v & 0xFFFFu, 10000u));
        h.phase = static_cast<uint8_t>(std::min<uint32_t>((v >> 16) & 0xFFu, 5u));
        h.relics = static_cast<uint8_t>(std::min<uint32_t>(v >> 24, 7u));
        return h;
    }
};
constexpr int kHeartUnits = 10000;
constexpr const char* kHeartPart = "coeur";

/// The metadata of a level read as raid rules (objective indices resolved in `b`).
ZoneRules parseZoneRules(const LevelData& level, const Branch& b);

}  // namespace cosmic::raid
