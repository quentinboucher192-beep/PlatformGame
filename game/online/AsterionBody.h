#pragma once
// The body of Asterion, le Dévoreur de mondes (clan raids): the sprite of the titan cut into
// layers (assets/ui/raids/asterion.json, made by tools/assetgen/environment/raid_asterion.py),
// its branches and their gigabosses (data/raids/asterion.json), the state of each part (the
// server's « body » of an event, « intact » without it), what a state looks like, the part under
// a point (hit masks) and the texts of a part's tooltip. Display only: no rule of a run.

#include <map>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic::asterion {

enum class State { Intact, Attacked, Weakened, Destroyed, Unknown };

/// « intact », « attaque », « affaibli », « detruit », « inconnu » (the server's words); anything
/// else is « intact ».
State stateFromString(const std::string& s);

struct Cell {
    int x = 0, y = 0;  // top left of a frame in the sheet
};
struct Span {
    int y = 0, x0 = 0, x1 = 0;  // [x0, x1[ on row y of the canvas
};

/// A layer of the sprite: its box on the canvas, its cells per look (« intact » with its frames,
/// « affaibli », « detruit », « inconnu », « contour »), its hit mask and the point its focus
/// sits on. The aura has no mask: it is never a part.
struct Layer {
    std::string id;
    int x = 0, y = 0, w = 0, h = 0;
    float fps = 0.0f;
    int lift = 0;  // rows it rises while the titan breathes in
    int anchorX = 0, anchorY = 0;
    std::map<std::string, std::vector<Cell>> cells;
    std::vector<Span> mask;
    bool hasMask() const { return !mask.empty(); }
    bool contains(int cx, int cy) const;
    /// The cell of `look` at `time` (its frames looping at fps); null when it has none.
    const Cell* cellAt(const std::string& look, float time) const;
};

struct Sprite {
    bool valid = false;
    int canvasW = 0, canvasH = 0;
    std::string sheet;  // texture (relative to assets/)
    std::vector<Layer> layers;  // drawing order
    const Layer* layer(const std::string& id) const;
};
Sprite parseSprite(const Json& j);

/// A branch (or the heart): its name, its gigaboss (empty for the heart) and what destroying it
/// does to Asterion (empty when unknown).
struct Part {
    std::string id, name, boss, effect;
};
std::vector<Part> parseParts(const Json& j);

/// The state of each part from an event of the server (its « body »: id → state); the parts it
/// does not name are intact.
std::map<std::string, State> statesOf(const Json& event);
State stateOf(const std::map<std::string, State>& states, const std::string& id);

/// How a state is drawn: the look of the layer, its rim (orange, pulsing: under attack), whether
/// the heart's glow shows and how strong it is.
struct Visual {
    std::string look = "intact";
    bool attackRim = false;
    float glow = 1.0f;  // 0: none
};
Visual visualOf(State s);

/// Rows each layer rises at `time` (the titan breathes: 0 or its `lift`, every 2 s).
int breathLift(const Layer& l, float time);

/// The index (in `sprite.layers`) of the part at canvas point (x, y), the topmost first; -1 when
/// none. `lifts` (optional, one per layer): rows each layer is drawn above its place.
int hitTest(const Sprite& sprite, int x, int y, const std::vector<int>& lifts = {});

/// The texts of a part's tooltip (French; translated where drawn, the gigaboss's name never).
struct Tooltip {
    std::string title;   // « CRÂNE »
    std::string boss;    // « Skullmaw », « inconnu » when the part is not discovered, empty for the heart
    std::string state;   // « Intact », « Sous attaque »…
    std::string effect;  // empty when unknown or not discovered
};
std::string stateLabel(State s);
Tooltip tooltipOf(const Part& part, State s);

}  // namespace cosmic::asterion
