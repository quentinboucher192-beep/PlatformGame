#pragma once
// The name of each power over its tablet (2.77 in a raid's armory, 2.81.2 in every level, solo and
// coop): its icon and energy cost under it when there is room. Screen boxes, measured and fitted
// (fitFont, else shortened), never over another name, a tablet or a ledge. Display only: nothing
// here touches the simulation.

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class GameWorld;
class UIContext;

struct PowerLabel {
    std::string power;
    Rect box;            // the whole label (screen)
    std::string name;    // fitted, shortened when it had to be
    float size = 13.0f;  // its font size (unscaled)
    int icon = -1;       // in ui/icons_powers.png, -1 when there is no room
    std::string cost;    // « 10 d'énergie », "" when there is no room
};

/// `armoryOnly`: only the tablets of a raid's armory (they keep their power for everyone, whatever
/// their state); otherwise every power tablet and station that offers a power (a station a
/// challenge turned off offers nothing: no name).
std::vector<PowerLabel> powerLabels(UIContext& ui, const GameWorld& w, const WorldView& view, bool armoryOnly);
/// The level's messages on screen keep the names off: none during a title, a boss's name, a secret or
/// a dialogue (a few seconds), the hints' line added to `keepOut`. False: no name now.
bool powerLabelsAllowed(UIContext& ui, GameWorld& w, std::vector<Rect>& keepOut);
/// Draws them (the ones on screen), after dropping those over `keepOut` (the HUD as drawn).
void drawPowerLabels(UIContext& ui, std::vector<PowerLabel> labels, const std::vector<Rect>& keepOut = {});

}  // namespace cosmic
