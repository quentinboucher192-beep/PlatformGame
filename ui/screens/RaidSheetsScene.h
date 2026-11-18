#pragma once
// 2.73: CLANS › RAIDS › « Fiches » (also from the sheet of an event and from the lair of each branch
// map): the sheet of Asterion and of each of its seven gigaboss, shown to every player, whether they
// met them or not (game/raid/RaidSheets). Pushed over the current screen; « ‹ Retour » / Échap / B
// pops it. Keyboard, gamepad and mouse: the list of the sheets, the titan's parts, the tabs, the
// attacks (each unfolds with its example) and the buttons are focus stops.
//
// Asterion's sheet: the titan (ui/widgets/AsterionFigure: a part hovered or focused shows its
// tooltip, activated opens the sheet of its gigaboss), its lore, the raid's structure, its heart, its
// seven parts (their gigaboss, what destroying them does, the links of the titan) and the rewards.
// A gigaboss's sheet: its sprite (each phase's look in turn), its difficulty, its phases, every
// attack and mechanism unfolding with an animated example and how to avoid it (ui/widgets/
// BossSheetWidgets), its collective mechanisms (how many players, how long), its weak points, what
// counts in the contributions, where it lives (branch, expedition, lair), what its fall does to
// Asterion, its badge and rewards.
//
// The fixed panels (header, list, the sheet's top) are placed by layoutRaidSheets(): every text in
// them is measured into its rect (made smaller, then shortened); tests/test_raid_sheets.cpp checks
// it at several sizes and in both languages.

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Text.h"

namespace cosmic {

class GameServices;
class Scene;
class UIContext;
namespace raid {
struct RaidSheets;
}

/// One text of a fixed panel: its rect, its text as drawn (fitted, else shortened) and its size.
struct RaidSheetText {
    Rect r;
    std::string text;
    float size = 0.0f;  // unscaled (as UIContext::label takes it)
    float px = 0.0f;    // on screen
    FontFace face = FontFace::Body;
};

struct RaidSheetsLayout {
    Rect screen, back, list, panel;
    RaidSheetText title;
    struct Row {
        Rect r, portrait;
        RaidSheetText name, sub;
    };
    std::vector<Row> rows;  // the list at scroll 0 (rows below its bottom scroll)
    float rowH = 0.0f;
    // The sheet shown.
    bool titan = false;     // Asterion's (the figure at the left)
    Rect box;               // the animated gigaboss, or the titan's figure
    RaidSheetText name, subtitle, caption;
    std::vector<RaidSheetText> chips;  // pills of facts (those that fit)
    Rect difficulty;        // a gigaboss's stars
    Rect tabs;
    std::vector<std::string> tabLabels;
    Rect content;           // the scroll region of the tab
};

/// The fixed panels of the sheets' screen for sheet `id` ("asterion" or a gigaboss's), at the size
/// of `ui` (its begin() called for this frame).
RaidSheetsLayout layoutRaidSheets(UIContext& ui, const raid::RaidSheets& sheets, const std::string& id);

/// What the screen shows now (tests): the sheet, the tab, the rows of the attacks list drawn by the
/// last frame, the blocks of the tab drawn, and whether anything was hidden (never).
struct RaidSheetsView {
    bool valid = false;
    std::string sheet;
    int tab = 0;
    int attackRows = 0;
    int blocks = 0;
    bool hidden = false;
};
RaidSheetsView raidSheetsView(const Scene* s);

}  // namespace cosmic
