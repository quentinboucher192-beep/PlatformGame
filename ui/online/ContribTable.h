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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class InputManager;
namespace raids {
struct Results;
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
};

struct Table {
    std::string title;  // « Zone 7 · Voûte crânienne »
    std::string sub;    // « Groupe 1 · 8 joueurs · terminée à 12:34 »
    std::vector<Row> rows;
};

struct Model {
    std::string heading = "TABLEAU DES CONTRIBUTIONS";
    std::vector<Table> zones;  // the « Zone » tab's tables (the newest first)
    Table raid;                // « Raid entier »
    std::string legend;        // where the numbers come from (one line or two)
    std::string tooltipNote;   // the last line of each tooltip
    std::string tabKey;        // the key that changes tab besides LB / RB (« Tab »)
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
    std::vector<Rect> rows;           // one per row shown, in sorted order
    int hidden = 0;                   // rows that did not fit (« … et N de plus »)
    float rowFont = 18.0f, headFont = 15.0f;
    std::vector<std::string> legendLines, keyLines;
    std::vector<Rect> texts;          // every text box drawn (for the overlap tests)
};

class View {
public:
    int tab = 0;        // 0: « Zone », 1: « Raid entier »
    int zone = 0;       // the zone shown in the « Zone » tab (index in Model::zones)
    Col sort = Col::Boss;
    int selected = -1;  // the row shown in detail by the keys (sorted order), -1 none
    /// Its keys: Tab (its tabKey action), Q / E, LB / RB: tabs; Gauche / Droite: the sorted column;
    /// Haut / Bas: a row's detail; Entrée / A: the next zone; Échap / B: close (true).
    bool handleKeys(const InputManager& in, int zones);
};

/// Lays the table out in `r` (the whole panel).
Layout layout(UIContext& ui, const Rect& r, const Model& m, const View& v);
/// Draws it; true when « Fermer » was clicked. `mouse`: where the pointer is (tooltips), when it moves.
bool draw(UIContext& ui, const Rect& r, const Model& m, View& v);
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
