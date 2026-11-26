#pragma once

// 2.91 — « Startpos et points de mesure »: the StartPos and the measure points of a level version
// (StartPosStore, MeasureStore), shown on a strip of the level and in two lists, to start from a
// StartPos, delete, rename, move A / B, and the display options. Drawn by the pause menu of a
// training (Pause › « Startpos et mesures ») and by ENTRAÎNEMENT › « Startpos et mesures ».

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;

class TrainingMarksPanel {
public:
    struct Context {
        std::string levelKey;  // GhostStore::levelKey
        Rect levelBounds;      // the level (the strip, left to right)
        std::vector<std::pair<float, std::string>> checkpoints;  // their x in the level, their name
        /// Where a place lies on the route (« Clairière → Sous-bois · 3 % »), "" when unknown.
        std::function<std::string(Vec2)> where;
        /// « Partir de SP n » (an id; 0: the training's own start); nullptr: not offered.
        std::function<void(int)> startFrom;
        /// « Déplacer A / B » (a pair's id): the next measure marks place them again; nullptr: not offered.
        std::function<void(int)> movePair;
    };
    /// Draws the panel in `area`; its widgets are named under the current UI id.
    void draw(GameServices& g, const Rect& area, const Context& c);
    /// A text field is being edited (the screen leaves Échap to it).
    bool editing() const { return renaming_ > 0; }

private:
    void drawStrip(GameServices& g, const Rect& r, const Context& c);
    void drawStartPos(GameServices& g, const Rect& r, const Context& c);
    void drawMeasures(GameServices& g, const Rect& r, const Context& c);
    int selectedSp_ = 0;
    int selectedPair_ = 0;
    int renaming_ = 0;
    std::string renameText_;
    bool confirmClear_ = false;
};

}  // namespace cosmic
