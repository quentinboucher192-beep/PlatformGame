#pragma once

// 2.71: the HUD of the clan raids (docs/RAIDS.md §15), shared by the combat (RaidGameScene) and the
// HUD editor's « Raids » layout (HudEditorScene, its example of 8 players): where each element goes
// (RaidHudLayout: the default places around the game's own HUD, or the player's), and how each one
// is drawn from plain data, every text measured into its box (fit, wrap, « … et N de plus »):
//
// * the group panel (8 rows), the objectives, the alerts, the danger band, the contributions, the
//   network line or the « TEST ADMIN » badge;
// * the minimap and the large map (Action::RaidMap): the zone's terrain baked once into a texture
//   from the generated tiles (tools/assetgen/environment/raid_visuals.py: rock, its lit edge, the
//   one-way ledges, lava, the area the magma floods), the marks as icons (this player, the allies in
//   their colours, the enemies, the boss, the objectives, the passages open or locked, the doors,
//   the checkpoints, the co-op plates, the gigaboss's vents) and a legend measured into its room.
//
// Display only: nothing here takes part in the simulation.

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "saves/RaidHudLayout.h"

namespace cosmic {

class GameWorld;
class ResourceManager;
class UIContext;
struct GameplaySettings;

namespace raidhud {

/// What the game's HUD covers at its raid places: this computer's vitals and powers, the gigaboss's bar.
struct HudAreas {
    Rect vitals, powers, boss;
};

/// The area of an element on this screen: its default place (around `hud`), or the player's
/// (top-left corner, fractions of the screen), sized by its scale. Vitals, Powers, Boss: from `hud`.
Rect area(UIContext& ui, const RaidHudLayout& l, RaidHudElement e, const HudAreas& hud);
/// The scale an element is drawn at (0.5 to 2).
float scaleOf(const RaidHudLayout& l, RaidHudElement e);
/// The settings the game's HUD (Hud) is drawn with in a raid: its vitals, powers and gigaboss bar
/// at the raid layout's places (shown or not), the focus mode off (the raids have their own layout).
GameplaySettings hudSettings(const GameplaySettings& base);

// ---------------------------------------------------------------- the panels, from plain data

struct GroupRow {
    std::string name;
    Color color;
    float health = 0.0f;  // 0..1
    bool vacant = false;
    bool playing = false;
    std::string state;
    Color stateColor;
};
struct Group {
    std::string title;
    std::vector<GroupRow> rows;
    std::string footer;
};
void drawGroup(UIContext& ui, const Rect& r, float k, const Group& g);

struct Line {
    std::string mark, text;
    Color color;
};
/// The objectives: each line wrapped (3 lines at most), what does not fit counted in a last line.
void drawObjectives(UIContext& ui, const Rect& r, float k, const std::string& title, const std::vector<Line>& lines);
/// The alerts from the top of `r` (2 lines each); the boxes drawn (for the arrows to keep clear).
std::vector<Rect> drawAlerts(UIContext& ui, const Rect& r, float k, const std::vector<Line>& alerts);
void drawDanger(UIContext& ui, const Rect& r, float k, const std::string& text);
/// The raid's chrono, in the style of the levels' timer: the time since the instance started (steps
/// of the simulation: the same on every computer), the zone's time under it.
void drawTimer(UIContext& ui, const Rect& r, float k, float instanceSeconds, float zoneSeconds);
/// A line in a rounded box (the network, the contributions); `test`: the « TEST ADMIN » badge's look.
void drawLineBox(UIContext& ui, const Rect& r, float k, const std::string& text, Color textColor, bool test);

/// A box of the HUD (its rounded panel).
void hudBox(UIContext& ui, const Rect& r);

// --------------------------------------------------------------------------------- the maps

enum class Icon {
    Me, Ally, Enemy, Boss, Objective, ObjectiveDone, ObjectiveOptional, Exit, ExitLocked, Door, Checkpoint, CheckpointOn,
    Plate, PlateDone, Vent, VentClosed, Magma, Count
};
const char* iconCell(Icon i);   // its cell in ui/raids/minimap.json
const char* iconLabel(Icon i);  // its name in the legend

struct Mark {
    Icon icon = Icon::Enemy;
    Vec2 at;                     // world
    Color tint = Color::white();  // the allies: their seat's colour
};
struct MagmaArea {
    Rect rect;       // world: the area it floods
    bool active = false;
    bool warning = false;
};
/// A zone of the branch on the large map (its name and state).
struct ZoneChip {
    std::string name, state;
    Color color;
    bool here = false;
};

class Minimap {
public:
    /// The zone changed (or the screen's device): its terrain is baked again on the next draw.
    void invalidate() { world_ = nullptr; }
    /// The small map in `r` (title at the left, legend at the right of its top row): the whole zone
    /// when it fits, else a window around `focus`.
    void drawSmall(UIContext& ui, ResourceManager& res, const GameWorld& w, const Rect& r, float k, const std::string& title,
                   const std::vector<Mark>& marks, const std::vector<MagmaArea>& magma, Vec2 focus);
    /// The large map over most of the screen: the zone (and an overview of it when it is wider than
    /// the view), the branch's zones, the whole legend, how to close it; `header` at the right of the
    /// title (the danger now). `pan`: world pixels the player dragged it by.
    void drawLarge(UIContext& ui, ResourceManager& res, const GameWorld& w, const Rect& r, const std::string& title, const std::string& header,
                   Color headerColor, const std::vector<Mark>& marks, const std::vector<MagmaArea>& magma, Vec2 focus, float pan,
                   const std::vector<ZoneChip>& zones, const std::string& closeHint);
    /// The world pixels per screen pixel the large map last used (dragging it).
    float largeScale() const { return largeScale_; }

private:
    bool ensure(UIContext& ui, ResourceManager& res, const GameWorld& w);
    void drawTerrain(UIContext& ui, const GameWorld& w, const Rect& dst, const Rect& win, const std::vector<MagmaArea>& magma);
    void drawMarks(UIContext& ui, const Rect& dst, const Rect& win, const std::vector<Mark>& marks, float size);
    /// The legend's items in a row from (x, cy) to `right`: as many as fit; the width used.
    float legendRow(UIContext& ui, float x, float cy, float right, float k, const std::vector<Icon>& icons);
    const GameWorld* world_ = nullptr;
    TexturePtr terrain_;  // the zone baked at 1/kTexel
    TexturePtr atlas_;
    std::vector<Rect> cells_;  // by Icon
    Rect magmaCell_;
    Rect bounds_;
    float largeScale_ = 1.0f;
};

}  // namespace raidhud

}  // namespace cosmic
