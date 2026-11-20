#pragma once

// 2.80 (docs/COOP.md « Passages qui se ferment »): a passage that closes behind a team (an arena's doors, a
// door, a gate or a laser an event closes) waits, open, until every player still there is through it
// (GameWorld::teamThrough, rules of cooperation 4). This panel says who it waits for and how far:
// « Porte : en attente de MVP (12 m) », « … de NovaRider (à terre, 30 m) ». Display only: it reads the
// simulation's state and changes nothing of it.
//
// Every line is measured (smaller down to 75 %, then shortened with « … ») inside its panel; the panel
// goes at the top, centred, under every block of the HUD it would cover (downed::place).

#include <functional>
#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameWorld;
class UIContext;

namespace passagewait {

/// One line per passage waiting (the same players: one line), `maxLines` at most, the players it waits for
/// nearest first; `nameOf(i)`: the name of player i (its seat), written as the player chose it.
std::vector<std::string> lines(const GameWorld& w, const std::function<std::string(int)>& nameOf, size_t maxLines = 2);
/// One line: the passage's kind (« door », « power_gate », « laser »…) and the players it waits for.
struct Waited {
    std::string name;
    int metres = 0;
    bool down = false;
};
std::string line(const std::string& kind, const std::vector<Waited>& players);

struct Layout {
    Rect panel;
    std::vector<Rect> lineBoxes;
    std::vector<std::string> shown;  // each line as drawn (fitted, shortened if it had to be)
    std::vector<float> sizes;        // its font size (unscaled)
};
constexpr float kLineSize = 16.0f;
constexpr float kLineH = 24.0f;
/// The panel at its place: centred at the top, under every rect of `taken` it would cover.
Layout placed(UIContext& ui, const std::vector<std::string>& lines, const std::vector<Rect>& taken);
void draw(UIContext& ui, const Layout& l);

}  // namespace passagewait

}  // namespace cosmic
