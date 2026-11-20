#pragma once

// 2.72: the HUD of the clan raids (docs/RAIDS.md §15), shared by the combat (RaidGameScene) and the
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
#include "engine/rendering/Renderer.h"
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

/// 2.74 (phase 6): the Cœur d'Asterion's shared health (docs/RAIDS.md §16): what is left for every
/// group (gold), this group's part (green) and the other groups' (violet) at its right, the shared
/// phases' marks (75 / 50 / 25 %), a title and its phase on the top row, a line under the bar.
struct HeartBar {
    float left = 1.0f, own = 0.0f, others = 0.0f;  // fractions of its health
    std::string title, phase, sub;
    bool fallen = false;
};
void drawHeartBar(UIContext& ui, const Rect& r, float k, const HeartBar& h);

/// 2.78: the distance to the lair's passage (the zone before the lair): « Passage vers l'antre : 124 m »,
/// an arrow toward where to go (along the route), and its state (« fermé — embuscades restantes : 2 sur 4 »,
/// « ouvert »). Both lines measured into the box.
struct LairPassage {
    std::string title, state;
    bool open = false;
    Vec2 dir{1.0f, 0.0f};
};
void drawLairPassage(UIContext& ui, const Rect& r, float k, const LairPassage& p);
/// Its two lines as drawn (the first one fitted, the state wrapped), for the tests.
struct LairPassageLayout {
    Rect arrow, title, state;
};
LairPassageLayout layoutLairPassage(UIContext& ui, const Rect& r, float k);

/// A box of the HUD (its rounded panel).
void hudBox(UIContext& ui, const Rect& r);

/// 2.75: the « Tableau des contributions » of a zone (docs/RAIDS.md §19): one row per player (what the
/// simulation counted, the same on every computer), its share of the damage dealt to the zone's boss
/// and mini-bosses (of the zone's damage when it had none) as a bar and a percentage, then damage,
/// support, mechanisms and objectives. Shown at the end of the zone and in « Résultats du raid ».
struct ZoneRow {
    std::string name;
    Color color = Color::white();
    bool me = false;
    int bossDamage = 0, damage = 0, support = 0, mechanisms = 0, objectives = 0;  // damage in hearts (tenths / 10)
};
struct ZoneTable {
    std::string title, sub;
    std::vector<ZoneRow> rows;
};
/// The six columns of a table `width` wide (x from its left): player, share, damage, support,
/// mechanisms, objectives; `header` their titles (the share's says whose damage it counts).
struct ZoneColumns {
    float x[6] = {}, w[6] = {};
    std::string header[6];
};
ZoneColumns zoneColumns(const ZoneTable& t, float width);
/// Each row's share (0..1): of the boss damage when anyone hurt a boss, else of the damage.
std::vector<float> zoneShares(const ZoneTable& t);
/// The table in `r` (title, sub-title, header, rows; « … et N de plus » when they do not fit).
void drawZoneTable(UIContext& ui, const Rect& r, float k, const ZoneTable& t);
/// 2.75.5: the height drawZoneTable needs to show every row of `t` at `width` (the raid's menu).
float zoneTableHeight(UIContext& ui, const ZoneTable& t, float width, float k);

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
    std::string missing;  // 2.75: what it still misses (its second line), "" when finished
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

/// 2.77: in a raid's armory, the name of each power over its tablet (read in data/powers), its icon and
/// its energy cost under it when there is room: screen boxes, measured and fitted (fitFont, else shortened),
/// never over another name, a tablet or a ledge, whatever the tablet's state (an armory's tablet keeps its
/// power for everyone). Display only.
struct ArmoryLabel {
    std::string power;
    Rect box;            // the whole label (screen)
    std::string name;    // fitted, shortened when it had to be
    float size = 13.0f;  // its font size (unscaled)
    int icon = -1;       // in ui/icons_powers.png, -1 when there is no room
    std::string cost;    // « 10 d'énergie », "" when there is no room
};
std::vector<ArmoryLabel> armoryLabels(UIContext& ui, const GameWorld& w, const WorldView& view);
void drawArmoryLabels(UIContext& ui, const std::vector<ArmoryLabel>& labels);

}  // namespace raidhud

}  // namespace cosmic
