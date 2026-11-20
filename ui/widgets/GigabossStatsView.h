#pragma once

// 2.77.3: the « Statistiques » of a gigaboss of Asterion (and of its heart): its health by the size of the
// group (2 to 8 players, the relics for the heart), its phases (where each begins, in % and in health, its
// states), the hearts its contact and each of its blows take, the multipliers of its parts, its speed, its
// size, its rhythm. Every number comes from raid::SheetStats (its definition, its lair's rules, its parts'
// rules); nothing is estimated. Shown in the tab « Combat » of its raid sheet (ui/screens/RaidSheetsScene)
// and in STATISTIQUES › Boss › « Fiche » (ui/screens/BossStatsScene).
//
// Measured first (layoutStats: each item placed after the one before, its texts fitted to its row), then
// drawn. Each number answers the mouse (hover) and the keyboard / gamepad (one focus stop per number): its
// tooltip says where it comes from (drawTip, over everything, once the screen is drawn).

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"
#include "game/raid/RaidSheets.h"

namespace cosmic {

class UIContext;

namespace gbstats {

struct Item {
    enum class Kind { Title, Heading, Note, Row, Bar, Chips };
    Kind kind = Kind::Row;
    Rect r;                          // the whole item (inside the column)
    Rect labelR, valueR, extraR, barR;  // Row and Bar: their parts, side by side, inside r
    std::string label, value, extra; // extra: Bar, the health without relic (grey)
    float fraction = 0.0f, fractionBase = 0.0f;  // Bar: of the widest health shown
    float size = 14.0f, labelSize = 14.0f, valueSize = 14.0f;  // unscaled font sizes, fitted
    std::vector<std::string> lines;  // Note: wrapped (translated already); Chips: their texts
    std::vector<Rect> chips;
    Color color{238, 240, 255, 255};
    std::string tipTitle;            // its tooltip (empty: none)
    std::vector<std::string> tip;
};

struct Layout {
    std::vector<Item> items;
    float height = 0.0f;
};

/// What to show: everything (a gigaboss's sheet, STATISTIQUES › Boss), or the heart's middle column of its
/// « Combat » (its health with `relics`, the shared health and phases, its contact).
enum class Mode { Full, Heart };

/// The items from (x, y), `w` wide. `relics`: the relics validated (the heart's health), 0 elsewhere.
Layout layoutStats(UIContext& ui, const raid::SheetStats& s, float x, float y, float w, Mode mode = Mode::Full, int relics = 0);

/// The tooltip of the number hovered or focused (drawTip draws it over everything).
struct Tip {
    std::string title;
    std::vector<std::string> lines;
    Rect anchor;
    bool on = false;
};

/// Draws the items (their focus stops named `id` + their index) and keeps the tooltip of the one hovered or
/// focused in `tip` (left as it is when none).
void drawStats(UIContext& ui, const Layout& L, const std::string& id, Tip& tip);
/// The tooltip beside its anchor, inside the screen, measured (its box as high as its lines); clears it.
void drawTip(UIContext& ui, Tip& tip);
/// The box drawTip gives `tip` on this screen (tests).
Rect tipBox(UIContext& ui, const Tip& tip, std::vector<std::vector<std::string>>* wrapped = nullptr);

/// « 1 269 »: a whole number with a space between thousands (as French writes it; translated when drawn).
std::string thousands(int v);

}  // namespace gbstats

}  // namespace cosmic
