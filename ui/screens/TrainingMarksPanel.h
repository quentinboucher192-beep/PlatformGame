#pragma once

// 2.91 — « Startpos et points de mesure »: the StartPos and the measure points of a level version
// (StartPosStore, MeasureStore), shown on a strip of the level and in two lists, to start from a
// StartPos, delete, rename, move A / B, and the display options. Drawn by the pause menu of a
// training (Pause › « Startpos et mesures ») and by ENTRAÎNEMENT › « Startpos et mesures ».
//
// 2.92: the strip shows the level itself (TrainingStrip), the StartPos pennants and the A / B
// gates at their real places. From ENTRAÎNEMENT (`placedState` given) they are also added there
// (a click on the strip, or its cursor with the arrows and Entrée / A, then the pixel), moved
// and deleted (confirmed): the « Réglage fin » preview shows the level around the one chosen,
// enlarged, with the pennant, Nova's silhouette or the gate at its exact place; the arrows
// (buttons, keys, d-pad) move a gate by 1 pixel, by 8 with Maj or LB / RB held.
//
// 2.95: a StartPos lies on the SOB path (game/ghost/SobPath.h), drawn on the strip and in the
// preview: placed on its point nearest to the place chosen, refused where the route was never
// crossed (no best replay there) or where another StartPos is; the arrows move it along the path
// (the previous / next point, 8 with Maj or LB / RB held). One of before lying near the path is
// placed on it; the others are « à replacer » (never used to start).

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/StartPos.h"
#include "game/ghost/SobPath.h"
#include "ui/screens/TrainingStrip.h"

namespace cosmic {

class GameServices;
class UIContext;
struct StartPosEntry;

class TrainingMarksPanel {
public:
    struct Context {
        std::string levelKey;  // GhostStore::levelKey
        std::string levelId;   // 2.92: the level drawn in the strip and the preview ("": the schematic bar)
        statsart::LevelArt* art = nullptr;  // 2.92: the screen's pictures (updated by it); nullptr: none
        Rect levelBounds;      // the level (the strip, left to right)
        std::vector<std::pair<float, std::string>> checkpoints;  // their x in the level, their name
        std::vector<std::pair<std::string, std::string>> names;  // 2.95: their id and their name
        /// Where a place lies on the route (« Clairière → Sous-bois · 3 % »), "" when unknown.
        std::function<std::string(Vec2)> where;
        /// « Partir de SP n » (an id; 0: the training's own start); nullptr: not offered.
        std::function<void(int)> startFrom;
        /// « Déplacer A / B » (a pair's id): the next measure marks place them again; nullptr: not offered.
        std::function<void(int)> movePair;
        /// 2.92: StartPos and measure points added, moved and deleted here (ENTRAÎNEMENT); false:
        /// listed only (the pause).
        bool edit = false;
    };
    TrainingMarksPanel() = default;
    TrainingMarksPanel(const TrainingMarksPanel&) = delete;
    TrainingMarksPanel& operator=(const TrainingMarksPanel&) = delete;
    /// What was moved and not yet written goes to the saves.
    ~TrainingMarksPanel();
    /// Draws the panel in `area`; its widgets are named under the current UI id.
    void draw(GameServices& g, const Rect& area, const Context& c);
    /// Échap belongs to the panel this frame (a text field, a placement, the preview).
    bool editing() const { return renaming_ > 0 || placing_ != Place::None || previewFocused_ || backUsed_; }
    /// The preview (or its arrows) has the focus with LB / RB held: the screen's tabs keep still.
    bool holdsShoulders(UIContext& ui) const;

    /// What the preview shows (tests).
    enum class Pick { None, StartPos, GateA, GateB };
    Pick pick() const { return pick_; }
    /// 2.95: why the StartPos being set was refused there (not saved).
    enum class Refusal { NoPath, Taken };
    std::optional<Refusal> refused() const { return refused_; }
    /// 2.95: the SOB path of the level version shown (nullptr while it is read).
    const SobPath* sobPath() const { return path_.path(); }
    /// 2.95: an attempt can start from it (on the path, its segment's best has its replay).
    static bool usable(GameServices& g, const std::string& levelKey, const StartPosEntry& e);

private:
    enum class Place { None, StartPos, Gate };
    void drawStrip(GameServices& g, const Rect& r, const Context& c);
    void drawEditor(GameServices& g, const Rect& r, const Context& c);
    void drawPreview(GameServices& g, const Rect& r, const Context& c, Vec2 at);
    void drawStartPos(GameServices& g, const Rect& r, const Context& c);
    void drawMeasures(GameServices& g, const Rect& r, const Context& c);
    /// Where the one chosen is now (nullopt: nothing chosen, or gone).
    std::optional<Vec2> pickedAt(GameServices& g, const Context& c) const;
    void choose(Pick p, int id);
    /// A place chosen on the strip: a StartPos or a gate there.
    void placeAt(GameServices& g, const Context& c, Vec2 world);
    /// The one chosen moved to `to` (a gate; a StartPos: the point of the path nearest to it).
    void moveTo(GameServices& g, const Context& c, Vec2 to);
    /// 2.95: a StartPos chosen moved `delta` points along the SOB path.
    void stepAlong(GameServices& g, const Context& c, int delta);
    /// 2.95: a StartPos set on the path's point nearest to `world` (within `radius`): kept, or refused.
    void setOnPath(GameServices& g, const Context& c, Vec2 world, float radius);
    float routeAt(Vec2 feet) const;
    static std::string nameOf(const Context& c, const std::string& checkpointId);
    int selectedSp_ = 0;
    int selectedPair_ = 0;
    int renaming_ = 0;
    std::string renameText_;
    bool confirmClear_ = false;
    // 2.92
    TrainingStrip strip_{"marks"};
    Pick pick_ = Pick::None;
    int pickId_ = 0;
    std::optional<Vec2> work_;           // where the one chosen is being set (refused there: not saved)
    std::optional<Refusal> refused_;
    SobPathCache path_;                  // 2.95
    bool draft_ = false;                 // a new StartPos not kept yet (placed where it was refused)
    Place placing_ = Place::None;
    float cursor_ = 0.5f;                // the strip's cursor (keyboard, gamepad), 0..1 across
    int zoom_ = 2;                       // the preview's enlargement
    Rect lastView_;                      // the world box of the preview's last picture shown
    int confirmDelSp_ = 0, confirmDelPair_ = 0;
    bool previewFocused_ = false, nudgeFocus_ = false, backUsed_ = false;
    std::string wantFocus_;              // given at the next frame (once its widget is there)
    bool dirtySp_ = false, dirtyMeasures_ = false;
    float editedAt_ = 0.0f;
    GameServices* g_ = nullptr;
};

}  // namespace cosmic
