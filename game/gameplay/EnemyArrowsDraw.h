#pragma once

// 2.71: the off-screen arrows of a view as the solo levels draw them (GameScene), shared with the
// raids (RaidGameScene, docs/RAIDS.md §15): the enemies already met that are out of view (amber, the
// boss left out: it has its own arrow) and those of the ambush being fought (violet, met or not),
// one arrow per pack with its « ×N », kept off the HUD. Display only.

#include <vector>

#include "engine/rendering/Renderer.h"
#include "game/gameplay/EnemyArrows.h"

namespace cosmic {

class GameWorld;
class ResourceManager;
class UIContext;

namespace EnemyArrows {

struct Targets {
    std::vector<Rect> usual;   // the enemies already met, out of view (screen pixels)
    std::vector<Rect> ambush;  // the enemies of the ambush being fought, out of view
};

/// Notes in `tracker` the enemies on screen in `view` (alive, awake, not a boss, not of the ambush
/// being fought) and returns the boxes of those to point at (`screen`: the whole screen).
Targets targets(GameWorld& w, const WorldView& view, const Rect& screen, Tracker& tracker);

/// The sizes of the solo levels at this UI scale, the counts measured for `maxCount`.
Layout standardLayout(UIContext& ui, const Rect& screen, size_t maxCount);

/// Draws the ambush's arrows first, then the usual ones (fainter for a pack far away), each set
/// never under `l.keepOut` nor under the other; `areas` (may be null) gets what each covers.
void draw(UIContext& ui, ResourceManager& res, Layout l, const Targets& t, std::vector<Rect>* areas);

}  // namespace EnemyArrows

}  // namespace cosmic
