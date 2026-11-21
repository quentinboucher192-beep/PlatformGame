#pragma once

// 2.78 (docs/RAIDS.md §26): the « Tableau des contributions » of the raids, redrawn (maquette of 2.78):
//
// * two tabs, « Zone » (one finished zone's table; Entrée / A, or its buttons, go to the next zone) and
//   « Raid entier » (every zone added up per player);
// * a row per player: rank, colour, name (« Vous » framed), then one column per category, each with its
//   icon and its colour (ui/raids/contrib.png, tools/assetgen/environment/raid_contrib_icons.py): damage
//   to the boss and to the mini-bosses as horizontal bars proportional to the best (their share in %),
//   support, mechanisms, objectives, deaths, revivals done and received as numbers; a crown, measured
//   AFTER the number, on the best of each category;
// * sorted by any column (Gauche / Droite, a click on its header; ▼ drawn on the sorted one);
// * a tooltip per number (the mouse over it, or Haut / Bas on the sorted column) explaining it, always
//   inside the screen; a band « Meilleurs de la zone », a legend saying where the numbers come from,
//   a line of keys;
// * the numbers of a table of before 2.78 (no mini-bosses, deaths nor revivals) are written « — ».
//
// Everything measured into its place (fitFont, elide, wrap): nothing overlaps, in French and English,
// from 1280 × 720 up. Display and this computer's inputs only: the numbers come from the simulation (or
// the server's results), never invented here.
//
// 2.79: ONE contributions screen everywhere (the raid's menu, « Simuler un raid complet », the results):
//
// * the titan Asterion drawn at its left (ui/widgets/AsterionFigure, its pixel art by parts) when the model
//   has its parts: each part (a branch, the heart) has a callout beside the drawing (its gigaboss's portrait,
//   the groups there, fallen or not, its relic brought back) joined to its point on the titan; a part is
//   chosen by a click (on it or on its callout), by Q / E or LB / RB, and lit when hovered;
// * « Raid entier » (its button above the titan): every group, a header per group (its share of the damage),
//   its players under it; a part chosen: that part's groups and their zones;
// * the rows scroll (the wheel, the bar held with the mouse, Haut / Bas from row to row, at the keyboard or
//   the gamepad): seven groups of eight players fit at 1280 × 720 without ever getting smaller than readable;
// * opaque: nothing of the game shows under its texts.

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/online/AsterionBody.h"

namespace cosmic {

class AsterionFigure;
class GameServices;
namespace raids {
struct Results;
}
namespace raid {
class RaidRun;
}
class UIContext;

namespace contrib {

enum class Col : int { Player, Boss, Mini, Support, Mechanisms, Objectives, Deaths, Revives, Revived, Count };
constexpr int kCols = static_cast<int>(Col::Count);

/// A player's line (damage in tenths of health, as the simulation counts it).
struct Row {
    std::string name;
    Color color = Color::white();
    bool me = false;
    int64_t bossTenths = 0;   // the boss AND the mini-bosses (ZoneContribution::bossDamage)
    int64_t miniTenths = 0;   // the mini-bosses alone (part of bossTenths)
    int64_t damageTenths = 0; // every enemy of the zone
    int support = 0, mechanisms = 0, objectives = 0;
    int deaths = 0, revives = 0, revived = 0;
    bool v2 = true;  // mini-bosses, deaths and revivals known (a table of before 2.78: « — »)
    int section = -1;  // 2.79: its group's block (Table::sections), -1: none
    std::string tag;   // 2.79: its group (« G3 ») after its name, in a table of several groups
    /// 2.79: its damage zone by zone (tenths, every enemy), shown under its row when it is opened.
    std::vector<std::pair<std::string, int64_t>> zoneDamage;
};

struct Table {
    std::string title;  // « Zone 7 · Voûte crânienne »
    std::string sub;    // « Groupe 1 · 8 joueurs · terminée à 12:34 »
    std::vector<Row> rows;
    /// 2.79: a header per group (« Groupe 3 · CRÂNE · Voûte crânienne »): its rows under it, sorted among
    /// themselves; the shares and the crowns stay those of the whole table.
    std::vector<std::string> sections;
};

/// 2.79: a zone's tab of a part (« 1. Mâchoire », « Antre · Skullmaw »), its progress under its name.
struct ZoneTab {
    std::string name;
    float progress = 0.0f;  // its objectives done (0..1)
    bool reached = false;   // a group got there (else greyed: nothing to show)
    int table = -1;         // its table (Part::zones), -1: nothing counted there yet
};

/// 2.79: a group's card in « Raid entier » (a click opens its part).
struct GroupCard {
    std::string name;       // « Groupe 1 · CRÂNE »
    std::string state;      // « Skullmaw tombé », « en combat · Voûte crânienne », « éliminé »
    std::string enemy;      // its part's gigaboss (the portrait), empty: the heart's light
    bool fallen = false, wiped = false;
    std::vector<int> dots;  // its part's zones: 0 not reached, 1 where it is, 2 finished
    int deaths = 0, revives = 0, objectives = 0;
    std::string best;       // who dealt it the most damage
    int part = -1;          // the part a click opens (Model::parts)
};

/// 2.79: a part of the titan (a branch, or the heart) as the drawing shows it, and its tables.
struct Part {
    std::string id;        // the titan's layer (« crane », « coeur »: assets/ui/raids/asterion.json)
    std::string name;      // « CRÂNE »
    std::string gigaboss;  // its gigaboss's name (« Skullmaw »), empty for the heart
    std::string enemy;     // its gigaboss's enemy (the portrait), empty: the heart's light
    asterion::State state = asterion::State::Intact;
    bool fallen = false;   // its gigaboss down (the heart: the raid won)
    bool relic = false;    // its relic brought back (the optional objective of its secret zone)
    bool open = true;      // its numbers are on this computer (else `closed` says where they are)
    std::string closed;
    std::vector<int> groups;  // the groups there now (their numbers)
    std::vector<Table> zones; // its zones' tables (the newest first)
    Table whole;              // the whole part: its groups, a header each
    std::vector<ZoneTab> tabs; // its zones' tabs after « Toute la branche » (empty: the tabs « Zone » / « Toute la branche »)
    /// 2.86: its gigaboss's (the heart's) bar on its callout, never invented: its REAL remaining health (0..1: a
    /// simulation's on this computer, the heart's validated shared health from the server), -1 unknown; its phase
    /// (1.., 0 unknown). Fallen: an empty bar « Vaincu »; a phase alone: a greyed bar « — · phase N »; neither, or
    /// not discovered: no bar.
    float health = -1.0f;
    int phase = 0;
};

/// 2.86: what a part's bar says (« 62 % · phase 2 », « Vaincu », « — · phase 2 »), empty: no bar.
std::string healthText(const Part& p);

/// 2.86: a part's callout measured (the rows of drawCallout): every box inside the callout, none over another.
struct Callout {
    bool twoRows = false;
    Rect name, portrait, groups, pill, relic, bar;  // empty: not drawn
    std::string barText;   // healthText(), measured into `bar` at barFont
    float barFont = 11.0f; // unscaled (as UIContext::label takes it)
    float pillFont = 11.0f;
};
Callout calloutLayout(UIContext& ui, const Rect& c, const Part& p);
/// 2.86: the gigaboss (the heart) of `run`'s branch as its simulation has it now: true, with its remaining health
/// (0..1) and its phase (1..), while the run is in its lair and the fight is there; false otherwise (no bar).
bool lairBoss(const raid::RaidRun& run, float& health, int& phase);

struct Model {
    std::string heading = "TABLEAU DES CONTRIBUTIONS";
    std::vector<Table> zones;  // the « Zone » tab's tables (the newest first)
    Table raid;                // « Raid entier »
    std::string legend;        // where the numbers come from (one line or two)
    std::string tooltipNote;   // the last line of each tooltip
    /// 2.79: the titan's parts (the seven branches, then the heart); empty: no titan (the tables above alone).
    std::vector<Part> parts;
    std::string partKey = "Q / E, LB / RB";  // the keys that choose a part
    std::string extraKeys;     // a last piece of the keys' line (the panel around it), empty: none
    /// 2.79: drawn inside another panel (its own title and « Fermer »): no dimming, no heading.
    bool embedded = false;
    std::vector<GroupCard> groups;  // 2.79: « Raid entier »'s cards (above every player's row)
};

/// What one column says of one row.
struct Cell {
    bool known = true;     // false: « — »
    double value = 0.0;    // what it is sorted by (a share for the bars)
    std::string text;      // « 31 % », « 4 », « — »
    float bar = -1.0f;     // the bar's length (0..1 of the best), -1: not a bar
    bool best = false;     // the crown
};

/// The table of a tab, its columns computed (headers, shares, the best, the sort).
struct Prepared {
    const Table* table = nullptr;
    bool bossShare = true;  // the first bar counts the boss (else, without any boss, every enemy)
    std::array<std::string, kCols> headers;
    std::vector<int> order;                       // the rows, sorted
    std::vector<std::array<Cell, kCols>> cells;   // by row (table order)
};
Prepared prepare(const Table& t, Col sort);

/// The places of everything drawn, each measured and after the one before (tests: nothing overlaps).
struct Layout {
    Rect panel, heading, close, sub, sortLine, header, best, legend, keys;
    std::array<Rect, 2> tabs;
    Rect prevZone, nextZone;  // empty when the tab has a single zone
    std::array<Rect, kCols> cols;     // x and width of each column (header row's y and height)
    std::vector<Rect> rows;           // one per row, in the order shown, unscrolled (the content from list.y)
    int hidden = 0;                   // always 0 since 2.79 (the rows scroll)
    float rowFont = 18.0f, headFont = 15.0f;
    std::array<std::vector<std::string>, kCols> headLines;  // 2.79: each header whole (translated), on one line or two
    std::vector<std::string> legendLines, keyLines;
    std::vector<Rect> texts;          // every text box drawn (for the overlap tests)
    // 2.79
    Rect list;                        // the rows' view (scrolled when contentH is taller)
    float contentH = 0.0f;
    std::vector<int> rowOf;           // the row (Table::rows) of each of `rows`
    std::vector<Rect> sections;       // the group headers, unscrolled like `rows`
    std::vector<int> sectionOf;       // their index in Table::sections
    Rect table;                       // the table's side (the whole width without a titan)
    Rect titan, overview, figure, caption;  // the titan's column, its « Raid entier » button, the drawing, its line
    std::vector<Rect> callouts;       // one per part (Model::parts)
    std::vector<Vec2> anchors;        // where each part is on the drawing (its callout's line ends there)
    std::vector<std::string> captionLines;
    float titanScale = 0.0f;          // the drawing's scale (whole multiples once enlarged), 0: no drawing
    std::vector<Rect> partTabs;       // « Toute la branche », then each zone's tab (a part chosen)
    std::vector<Rect> cards;          // « Raid entier »: a card per group, unscrolled (their view: cardsView)
    Rect cardsView;
    float cardsH = 0.0f;
    Rect message;                     // a closed part: why (instead of the rows)
    Rect expand;                      // the opened row's zones, unscrolled like `rows` (empty: none)
};

class View {
public:
    int tab = 0;        // 0: « Zone », 1: « Raid entier » (a part chosen: the whole part)
    int zone = 0;       // the zone shown in the « Zone » tab (index in its zones)
    Col sort = Col::Boss;
    int selected = -1;  // the row shown in detail by the keys (the order shown), -1 none
    int part = -1;      // 2.79: the titan's part shown (Model::parts), -1: « Raid entier », every group
    int expanded = -1;  // 2.79: the row opened (Table::rows): its damage zone by zone under it
    bool toggle = false;  // 2.79: Entrée / A pressed: the selected row opened or closed at the next draw
    FreshKeys keys;       // titan 13: tabs, parts and sort on a fresh press only (keys.reset() when it opens again)
    /// Its keys: Q / E, LB / RB: tabs, or (`parts` > 0) the titan's part; Gauche / Droite: the sorted column;
    /// Haut / Bas: a row's detail (the rows scroll to it); Entrée / A: the next zone; Échap / B: close (true).
    /// 2.86: never Tab (the raid menu's key does nothing on this screen).
    bool handleKeys(const InputManager& in, int zones, int parts = 0);
    /// 2.79: with the model: Q / E, LB / RB: « Raid entier » and the parts; Entrée / A: the selected row opened
    /// (its zones) or closed, no row selected (2.86, instead of Tab): the part's next zone reached.
    bool handleKeys(const InputManager& in, const Model& m);
    /// 2.79: chooses a part (-1: the whole raid); the zone and the row start again.
    void choose(int p);
};

/// 2.79: the zone tables of what the view shows (the part chosen, else the whole raid).
const std::vector<Table>& zonesOf(const Model& m, const View& v);

/// Lays the table out in `r` (the whole panel).
Layout layout(UIContext& ui, const Rect& r, const Model& m, const View& v);
/// Draws it; true when « Fermer » was clicked. Without GameServices the titan's callouts are drawn
/// without the drawing nor the portraits (tests).
bool draw(UIContext& ui, const Rect& r, const Model& m, View& v);
/// 2.79: with the titan drawn by `fig` (its parts lit, its tooltip) and the gigabosses' portraits.
bool draw(GameServices& g, AsterionFigure& fig, const Rect& r, const Model& m, View& v);
/// 2.79: the titan's parts as the game's data name them (data/raids/asterion.json: id, name, gigaboss;
/// data/raids/asterion_sheets.json: its gigaboss's enemy), in the order of its body, the heart last; empty
/// when the data cannot be read. The callers fill in their state, their groups and their tables.
std::vector<Part> titanParts();
/// The tooltip of a cell: its box (inside the screen) and its lines, for `anchor` (the cell).
struct Tooltip {
    Rect box;
    std::string title;
    std::vector<std::string> lines;
    Col col = Col::Boss;
};
Tooltip tooltip(UIContext& ui, const Model& m, const View& v, const Prepared& p, int row, Col col, const Rect& anchor);
/// 2.78.1: the server's validated tables (GET /raids/events/{id}/results, the recap of a clan: `zone_tables`,
/// `raid_totals`) as the table shows them, a table of before 2.78 with « — ». `raidSub`: whose zones « Raid entier » adds.
Model modelOfResults(const raids::Results& r, const std::string& raidSub);

/// A category's name, its explanation and its colour.
const char* colName(Col c);
Color colColor(Col c);

}  // namespace contrib

}  // namespace cosmic
