#pragma once

// 2.77.3: the tab « Combat » of the sheet of the Cœur d'Asterion (CLANS › RAIDS › Fiches), in three columns:
//
// * « Ce que le clan a accompli »: the titan (its sprite, ui/widgets/AsterionFigure) with its seven parts —
//   a part whose gigaboss fell is crossed in green, ◆ a relic of its branch brought back. While an event runs
//   and its titan's map was received (raids::lastTitan), the clan's REAL state: the parts fallen (the heart's
//   links « modifier » on), the relics validated (their number: the map does not say which branches). Else, or
//   once a part or a relic is changed (click, Entrée / A on its buttons or on the titan), a simulation; R (or its
//   button) brings the real state back. « En raid, le Cœur s'ouvre quand les 7 Gigaboss sont tombés. »
// * « Statistiques »: its health for an instance of 2 to 8 players, with the relics (and without, in grey),
//   the health shared by every instance of the clan, the shared phases, its contact (ui/widgets/GigabossStatsView).
// * « Ses attaques selon les parties tombées »: the seven links « modifier » of the titan on it (their labels as
//   the titan's map writes them): green with a drawn tick, weakened; red with « ! », still on.
//
// Every number and word comes from the data (its definition, its arena's rules, titan.json): nothing invented.
// Measured first (layoutHeart), then drawn; narrow screens stack the columns.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/raid/RaidSheets.h"
#include "ui/widgets/GigabossStatsView.h"

namespace cosmic {

class GameServices;
class UIContext;
class AsterionFigure;

namespace heartsheet {

/// What the left column shows: the clan's real state (from the titan's map) or a simulation.
struct State {
    uint8_t fallen = 0;   // bit k: the part of heartModifiers[k] fallen
    uint8_t relics = 0;   // bit k: the relic of that part's branch (the simulation)
    bool real = false;    // the clan's real state (an event running, its titan's map received)
    int realRelics = 0;   // its relics validated (the map gives their number only)
    bool realAvailable = false;
    int relicCount() const;
    bool down(int k) const { return (fallen >> k) & 1u; }
};
/// The clan's real state from a titan's map (GET /raids/events/{id}/titan); `ok` false when the map is not of
/// an event running or says nothing of the heart.
State realState(const raid::RaidSheets& s, const Json& titan, bool* ok);

struct Part {
    std::string part, name;
    Rect row, name_, fallBtn, relicBtn;
};
struct Card {
    Rect r, icon, tag;
    std::vector<std::string> lines;  // its label, wrapped (translated already)
    std::vector<Rect> lineR;
    std::string tagText;
    bool weakened = false;
};
struct Layout {
    bool stacked = false;
    Rect left, middle, right;           // the three panels
    Rect leftTitle, middleTitleUnused, rightTitle;
    std::vector<std::string> modeLines; // the left column's line on its state (wrapped)
    Rect modeR;
    Rect figure;
    std::vector<Part> parts;
    Rect totals;
    std::string totalsText;
    std::vector<std::string> honest;    // « En raid, le Cœur s'ouvre… » (wrapped)
    Rect honestR;
    Rect resetBtn;                      // empty without a real state to bring back
    gbstats::Layout stats;
    std::vector<Card> cards;
    std::vector<std::string> legend;
    Rect legendR;
    float height = 0.0f;
};

/// Its places in `area` (x, y, w; its height is what it needs: the content scrolls).
Layout layoutHeart(UIContext& ui, const raid::RaidSheets& s, const State& st, const Rect& area);

/// What the player did this frame.
struct Action {
    int toggleFall = -1, toggleRelic = -1;  // a part (index in heartModifiers)
    bool reset = false;
};
/// Draws it (`figure`: the titan, its tooltip drawn by the caller over everything); `tip`: the statistics'.
Action drawHeart(GameServices& g, const raid::RaidSheets& s, const State& st, const Layout& L, AsterionFigure& figure, gbstats::Tip& tip);

}  // namespace heartsheet

}  // namespace cosmic
