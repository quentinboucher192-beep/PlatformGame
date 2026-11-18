#pragma once

// 2.72: the sheets of Asterion and of its seven gigaboss (CLANS › RAIDS › « Fiches », the sheet of an
// event, the lair of each branch map: ui/screens/RaidSheetsScene). Shown to every player, whether
// they met them or not (nothing is hidden: no lock, no « ??? »).
//
// Everything comes from the game's own data, never written twice:
// * the words: data/raids/asterion_sheets.json (lore, each mechanism of a routine, how to avoid
//   each kind of attack, the heart). Its numbers are slots ({0}…) filled from the rules the
//   routines read (metadata.raid.gigaboss of the lair, zone 8 of the branch) or from the routines'
//   own constants (game/raid/GigabossTunables.h): a tunable changed there changes its sheet;
// * the gigaboss itself: its definition (data/enemies: phases, attacks, last stand, difficulty);
// * where it lives: its branch's catalogue (data/raids/asterion/<part>: lair, expedition, the
//   objective of its fall, the links of the titan);
// * its part of the titan: data/raids/asterion.json (name, what destroying it does);
// * its rewards: the achievements (its badge, condition « raid_reward ») and the cosmetics of the
//   raid (« raid »: "asterion").
//
// A sheet is added by data: an entry of `gigaboss` names its part, its enemy and (when it is not
// zone 8 of the part's branch) its lair. The heart's own fight comes that way (its part
// « coeur »); until then Asterion's sheet says what is known of it (`asterion.heart`).
//
// Pure: no screen, no world. Tested by tests/test_raid_sheets.cpp.

#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {
class ContentDatabase;
class AchievementCatalog;
class CosmeticCatalog;
struct EnemyDefinition;
}  // namespace cosmic

namespace cosmic::raid {

struct GigabossRules;

/// A value of the sheets' data in French ("1,5", "8 / 6,5 / 5"); `ok` false when `ref` does not
/// resolve in `rules` (the lair's metadata.raid.gigaboss). See the comment of
/// data/raids/asterion_sheets.json for the forms of `ref`.
std::string sheetValue(const std::string& ref, const Json& rules, bool* ok = nullptr);
/// A text of the sheets' data: a string, or {"text": "…{0}…", "values": [refs]} with its slots
/// filled. What does not resolve is shown « ? » and named in `problems`.
std::string sheetText(const Json& line, const Json& rules, std::vector<std::string>* problems = nullptr);
/// The phases (1-based, among `phaseCount`) of a mechanism's `phases`: a list, "all",
/// "range:<path>", "from:<path>", "nonzero:<path>". Empty with `ok` false when it does not resolve.
std::vector<int> sheetPhases(const Json& spec, const Json& rules, int phaseCount, bool* ok = nullptr);
/// « Phases 1 et 3 », « Phase 2 », « Toutes les phases ».
std::string phasesLabel(const std::vector<int>& phases, int phaseCount);

struct SheetFact {
    std::string label, value;
};

/// The animated drawing of a mechanism (ui/screens/RaidSheetSketch), its numbers resolved.
struct SheetSketch {
    std::string kind;       // mark, beam, wave, plates, zone, rise, cone, summon, clip ("" none)
    std::string clip, clipHeld, clipDone;  // clips of the gigaboss's effects sheet (`fx`)
    int count = 0;          // marks, plates, sectors, storeys (0: the drawing's own)
    float warn = 0.0f;      // seconds: the announce
    float hold = 0.0f;      // seconds: a plate held, a window
    float time = 0.0f;      // seconds: a danger on
    float track = 0.0f, lock = 0.0f, fire = 0.0f;  // a gaze
    float period = 0.0f;
    bool rise = false;      // out of the ground (not falling from above)
    bool wall = false;      // from a wall to the other (not from the gigaboss to both walls)
    bool breaks = false;    // targets to break (not plates to stand on)
    int storeys = 1;
    std::vector<std::string> enemies;  // summon: the enemies it lets out (their sprites)
};

/// A mechanism of a gigaboss's routine (what its engine attacks are not).
struct SheetMechanic {
    std::string id;
    std::string kind;       // "each" (on every player), "collective", "arena"
    std::string name, summary, text, hint, players;
    std::vector<int> phases;
    std::vector<SheetFact> facts;
    /// Collective: what it asks by the players in play (« 1 joueur » → « 1 »…), when the rules say,
    /// and what that number counts (« Miroirs à tenir »).
    std::vector<SheetFact> byPlayers;
    std::string byPlayersLabel;
    SheetSketch sketch;
};

/// A phase of the fight: its engine attacks and the routine's mechanisms on in it.
struct SheetPhase {
    int number = 1;
    std::string name, from, enterText;
    std::vector<std::string> attacks;     // names of its attacks (data/enemies)
    std::vector<std::string> mechanics;   // names of its mechanisms
};

struct SheetWeakPoint {
    std::string enemy, name, description;
    int count = 0;
};

struct SheetReward {
    std::string kind;  // "badge", "title", "suit"
    std::string name, text;
    int stars = 0;     // a badge: its achievement's stars
};

struct GigabossSheet {
    std::string id, enemy, part, partName;
    const EnemyDefinition* def = nullptr;  // null when the content has no such enemy (yet)
    std::string name, title, description;
    int stars = 0;                          // difficulty (game/enemies/BossDifficulty), 0 without def
    // Where it lives.
    std::string branchId, branchName;
    int lairZone = 0, zones = 0;
    std::string lairName, lairSub, expedition, objective;
    float expeditionMinutes = 0.0f, branchMinutes = 0.0f;
    std::vector<std::string> links;  // the links of the titan its branch gives or receives
    // What its fall does to Asterion.
    std::string effect, fallText, summary;
    // Its fight.
    std::string fx;  // its effects sheet (animations/…)
    std::vector<SheetPhase> phases;
    std::string lastStandText;
    std::vector<std::string> lastStand;
    std::vector<SheetMechanic> mechanics;
    std::vector<SheetWeakPoint> weak;
    float exposed = 0.0f, exposedMult = 1.0f, weakShare = 0.0f;
    std::vector<std::string> scaling, contributions;
    std::vector<SheetReward> rewards;  // its badge first
    /// What could not be read (a value, a phase, its enemy, its lair…): empty for a sound sheet.
    std::vector<std::string> problems;
};

struct AsterionPart {
    std::string id, name, boss, effect;
    std::string sheet;               // its gigaboss's sheet ("" none)
    std::vector<std::string> links;  // the links of the titan its branch gives
};

struct AsterionSheet {
    std::string title, subtitle;
    std::vector<std::string> lore;
    std::vector<AsterionPart> parts;   // the seven branches, in the order of its body
    AsterionPart heart;                // its cosmic heart
    std::vector<std::string> heartText;
    std::string heartSheet;            // the heart's own sheet once the data has one
    int branches = 0, zones = 0, lairs = 0, players = 0;
    float minMinutes = 0.0f, maxMinutes = 0.0f;  // a branch without its gigaboss (the catalogues' estimate)
    std::vector<SheetReward> rewards;
    std::vector<std::string> contributions;
};

struct RaidSheets {
    AsterionSheet asterion;
    std::vector<GigabossSheet> gigaboss;  // in the order of the titan's body
    std::vector<std::pair<std::string, std::string>> avoid;  // attack type id → how to avoid it
    std::vector<std::string> problems;

    const GigabossSheet* find(const std::string& id) const;
    const GigabossSheet* ofPart(const std::string& part) const;
    /// How to avoid an attack of type `type` (its id in data/enemies, « laser »…); "" when unknown.
    std::string avoidOf(const std::string& type) const;
};

/// The sheets from data/raids/asterion_sheets.json and the game's data (see above).
RaidSheets loadRaidSheets(const ContentDatabase& content, const AchievementCatalog& achievements, const CosmeticCatalog& cosmetics);
/// The same from a given sheets' file (tests).
RaidSheets buildRaidSheets(const Json& data, const ContentDatabase& content, const AchievementCatalog& achievements,
                           const CosmeticCatalog& cosmetics);

}  // namespace cosmic::raid
