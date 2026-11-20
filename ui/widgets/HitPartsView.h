#pragma once

// 2.76.0: « Où frapper » by parts (docs/HITPARTS.md): a boss's real sprite, the clip and frame chosen,
// its hit parts over it (game/enemies/HitParts: assets/hitparts, the roles of data/enemies « parts »),
// each filled with the colour and the pattern of its role. Used by the sheets of the classic bosses and of
// the gigaboss of the campaign (BossStatsScene, tab « Où frapper »), by the sheets of Asterion's gigaboss
// (RaidSheetsScene, « Mécanismes » › « Où frapper ») and by the enemy editor (Boss › Parties).
//
// 2.77.1 (docs/HITBOX_PARTS.md): its box is never drawn any more (the precise shape of each frame is what
// takes and gives the blows); the three diagrams share one stage (HitStage): play / pause and frame by frame,
// zoom from 0,5× to 6× (around the mouse, or the centre with the keys and the gamepad) and pan, « Recentrer »,
// the drawing always clipped to its frame; and a tooltip over each part (under the mouse; Tab or the gamepad's
// Y go from part to part) with what it does, read from the boss's data (partFacts): the damage the boss takes
// there, what its contact costs, the attacks during which it also strikes and their damage. The legend has one
// entry per role (its colour and its pattern), its rule written whole, and scrolls when it is long.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Text.h"
#include "game/enemies/HitParts.h"

namespace cosmic {

class GameServices;
class Renderer;
class UIContext;
struct EnemyDefinition;

/// The colour of a role: green a weak point, blue a normal part, grey armour, red what hurts.
Color partRoleColor(PartRole r);
/// The colour of a part of `rules` (its role).
Color partColor(const PartRules& rules, const std::string& name);
/// 2.76.2: the pattern of a role over a convex polygon (on the screen), so that a part reads on a sprite of any
/// colour: a weak point cross-hatched, a normal part in upright stripes, armour in close flat stripes (its
/// plates), what hurts in thick diagonal stripes; each line over a dark one. `unit`: the pixels of one
/// unscaled pixel (UIContext::s(1)).
void drawPartPattern(Renderer& r, const std::vector<Vec2>& poly, PartRole role, float unit);
/// A thick outline over a dark one.
void drawPartOutline(Renderer& r, const std::vector<Vec2>& poly, Color c, float width, float unit);
/// The square of a legend: the colour and the pattern of a role.
void drawPartSwatch(Renderer& r, const Rect& box, PartRole role, float unit);
/// Draws the pieces of one cell, `toScreen` placing a vertex of the frame on the screen: each in the colour
/// and the pattern of its role, outlined; `highlight` (a part's name) outlined in white over the others.
void drawHitPieces(Renderer& r, float lineWidth, const HitPartsSheet& sheet, const std::vector<HitPartPiece>& pieces, const PartRules& rules,
                   const std::function<Vec2(Vec2)>& toScreen, uint8_t fill = 70, const std::string& highlight = {});
/// A part's name as shown: its first letter a capital (« tête » → « Tête », « épaule » → « Épaule »).
std::string partNameLabel(const std::string& name);
/// 2.76.2: the attacks during which a part also hurts (`hurtDuring`), by the names the boss's sheet gives
/// them (« Morsure », translated when shown), never an identifier: an attack the boss does not have is
/// left out (an écho that lacks one of its original's).
std::vector<std::string> hurtDuringNames(const PartRule& rule, const EnemyDefinition& d);
/// « blesse aussi pendant : Morsure, Ruée » (empty: none, or a part that always hurts).
std::string hurtDuringLabel(const PartRule& rule, const EnemyDefinition& d);
/// 2.77.1: whether a part's name is a plural, for the verb after it (« Jointures blessent », « Tête blesse »):
/// its first word ends with « s » or « x », but « bras », « avant-bras » and « corps ».
bool partIsPlural(const std::string& name);
/// 2.77.1: « Jointures blessent aussi pendant : Ruée des griffes », « Tête blesse aussi pendant : Morsure »
/// (the verb agreeing with the part; empty: none, or a part that always hurts).
std::string partHurtsAlsoLine(const PartRule& rule, const EnemyDefinition& d);
/// « Repos », « Élan », « Attaque »… the name of a clip on a chip (its id when it has none).
std::string poseLabel(const std::string& clip);
/// The clips of a sheet worth a chip, in the order of a fight (repos, déplacement, élan, attaque…).
std::vector<std::string> poseClips(const std::vector<std::string>& clips);

// ------------------------------------------------------------------ 2.77.1: what a part does (its tooltip)

/// What a part does in its boss's fight, read from its rules and its definition — never estimated:
/// * the blows the boss takes there: its multiplier (`damage` of its rule; a part that only hurts takes none);
/// * what touching it costs: the boss's contact damage (Enemy::contactDamage: from the rules of 17 any of its
///   parts hurts on contact, kWholeBodyContactSimulation);
/// * the attacks during which it also strikes, with their damage (Enemy::performAttackActive, its rush
///   « lunge » as Lunge does): a part that hurts during each attack of the boss that has damage, a part of
///   `hurtDuring` during those it names.
struct PartFacts {
    std::string part;            // its name in the sheet (« jointures »)
    PartRole role = PartRole::Normal;
    bool byParts = false;        // its fight uses its parts (0 < « partsFromVersion » ≤ kSimulationVersion)
    int fromVersion = 0;         // « partsFromVersion » (0: its sheets show its parts, its fight keeps its box)
    float dealt = 1.0f;          // the multiplier of the blows it takes (0: a part that only hurts)
    float contact = 0.0f;        // what its contact costs (EnemyDefinition::contactDamage, 0: nothing)
    struct During {
        std::string attack;      // the attack's name (as its sheet shows it)
        float damage = 0.0f;     // its damage (DamageInfo::amount)
        bool strikes = true;     // false: an attack without damage, the part strikes nothing during it
    };
    std::vector<During> during;  // in the order of the boss's attacks, each name once
};
PartFacts partFacts(const std::string& part, const PartRules& rules, const EnemyDefinition& d);
/// The hearts a hit of `amount` takes at the level's damage factor × 1 (Player::hurt: at least one, rounded):
/// « 2 cœurs », « 1 cœur »; « aucun » for nothing (≤ 0).
std::string heartsLabel(float amount);
/// The tooltip of a part: its name, its role, then its lines (French, translated when drawn):
/// « Dégâts infligés au boss : × 2 » (« aucun » for a part that only hurts), « Dégâts reçus au contact : 2 cœurs »,
/// « Pendant Ruée des griffes : 2 cœurs » for each attack (grouped by damage past eight); a boss that still fights
/// with its box says so. `context`: a line about the state shown (a gigaboss's armour), first.
struct PartTip {
    std::string title;
    PartRole role = PartRole::Normal;
    bool byParts = false;
    std::vector<std::string> lines;
};
PartTip partTip(const PartFacts& f, const std::string& context = {});

// ------------------------------------------------------------------ 2.77.1: the legend

/// One line of a diagram's legend.
struct HitLegendLine {
    std::string text;
    Color color{238, 240, 255, 255};
    int role = -1;                     // its swatch: this role's colour and pattern (-1: `swatch`, when it has alpha)
    Color swatch{0, 0, 0, 0};
    FontFace face = FontFace::Bold;
    float size = 13.0f;                // unscaled
    bool sub = false;                  // under the entry above, at its text's left (its parts' names)
    float gapBefore = 0.0f;            // unscaled
};
/// The legend of a sheet's parts: « Ses parties », one entry per role (its colour and its pattern, its multiplier
/// when its parts share one, its parts' names under it), the parts that also hurt during attacks, then how its
/// fight uses them, the sentence whole. `extraRoles[4]` (optional, PartRole order): roles drawn besides its parts
/// (a gigaboss's weak points stuck to it): their entry even without a part of that role. `title`: « Ses parties ».
std::vector<HitLegendLine> partsLegend(const HitPartsSheet* sheet, const PartRules& rules, const EnemyDefinition& d, const bool* extraRoles = nullptr,
                                       bool title = true);

// ------------------------------------------------------------------ 2.77.1: the stage

/// What a diagram drew last (tests: nothing overlaps, every text holds in its room, the box is never drawn).
struct HitStageLayout {
    struct Text {
        std::string text;
        Rect r;              // the room it was measured in (wrapped texts: their width, the height they take)
        float px = 0.0f;     // its size on the screen
        FontFace face = FontFace::Body;
    };
    std::vector<Text> texts;  // every text the stage, its bar, its keys, its legend and its tooltip drew
    std::vector<Rect> boxes;  // the elements outside the legend (bar's buttons and texts, view, keys line, legend)
    std::vector<Rect> legendBoxes;  // the lines of the legend, in its content (scrolled; their own column)
    Rect view;                // the drawing (clipped to it)
    Rect legend;              // the legend's frame (empty: none)
    Rect body;                // where the boss's box would be on the screen (it is never drawn)
    Rect tip;                 // the tooltip (empty: none)
    Vec2 tipAnchor;           // the point it keeps clear of (the mouse, or the part chosen with the keys)
    bool tipFromMouse = false;
    std::string tipPart;      // its part
    std::vector<std::string> tipLines;  // its lines (French), the title first
    std::vector<std::pair<std::string, Vec2>> partAt;  // each part of the frame: the centre of its largest piece (screen)
    float zoom = 1.0f;
    bool paused = false;
    int frame = 0, frames = 0;
    float legendContent = 0.0f;  // the legend's content height (scrolled past its frame)
    uint64_t drawnAt = 0;     // the application's frame
};

/// The interactive stage of a « Où frapper » diagram: its clock (play, pause, frame by frame), its view (zoom,
/// pan, recentre), the part under the mouse or chosen with the keys / the gamepad, and its tooltip, drawn last.
///
/// Per frame, the caller: advance(dt); frame(durations) — the frame of its pose shown now; controls() — the bar;
/// place(view, world) then input() — the keys, the gamepad and the mouse; draws its sprite and its pieces with
/// toScreen() inside pushClip(view()); parts() with the pieces drawn; setTip() with the facts of shownPart();
/// help() under it; and at the very end of its frame drawTip(), over everything.
///
/// Keys (the view focused): Espace / Entrée pause and play; ← → the previous / next frame in pause; + − zoom;
/// Maj + flèches pan; Tab / Maj + Tab the next / previous part (Tab from anywhere gives the view the focus).
/// Gamepad: A pause; the cross ← → the frames in pause; the triggers or the right stick zoom; the left stick
/// pans when zoomed; Y / X the next / previous part. Mouse: the wheel zooms around the pointer, a drag pans,
/// the buttons of the bar do the rest; the tooltip follows the pointer, never over it, never off the screen.
class HitStage {
public:
    static constexpr float kMinZoom = 0.5f, kMaxZoom = 6.0f;

    // ---------------------------------------------------------------- its clock
    /// The clock runs while playing (`dt` clamped to 0,1 s).
    void advance(float dt);
    /// The frame shown of a pose whose frames last `durations` (seconds), played again and again with `rest`
    /// seconds more on its last frame; in pause the frame stepped to. Call once per frame.
    int frame(const std::vector<float>& durations, float rest = 0.0f);
    /// Another pose: its first frame (pause and zoom kept).
    void restart();
    /// Another boss: its first frame, playing, at 1× and centred, no part chosen.
    void reset();
    void togglePause();
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    /// Pauses, then shows the next (+1) or previous (−1) frame, round.
    void step(int delta);
    int frameIndex() const { return frame_; }
    int frameCount() const { return frames_; }
    /// The pose's clock (frozen in pause): other sprites of the diagram follow it.
    float clock() const { return t_; }

    // ---------------------------------------------------------------- its view
    /// The screen rectangle of the drawing and the rectangle of the world (from the boss's feet) it shows at 1×,
    /// `margin` pixels inside it.
    void place(const Rect& view, const Rect& world, float margin);
    /// Zooms by `factor`, `around` (a point of the screen) staying under itself; clamped to [kMinZoom, kMaxZoom].
    void zoomBy(float factor, Vec2 around);
    void setZoom(float zoom, Vec2 around);
    void zoomBy(float factor) { zoomBy(factor, view_.center()); }
    float zoom() const { return zoom_; }
    /// Moves the drawing by `d` pixels (clamped: the centre of the view stays on the drawing).
    void panBy(Vec2 d);
    Vec2 pan() const { return pan_; }
    /// « Recentrer »: 1×, centred.
    void recenter();
    Vec2 toScreen(Vec2 world) const;
    Rect toScreen(const Rect& world) const;
    /// Screen pixels per unit of the world.
    float pixels() const { return k_ * zoom_; }
    const Rect& view() const { return view_; }
    const Rect& world() const { return world_; }

    // ---------------------------------------------------------------- its input
    /// The keys, the gamepad and the mouse (call after place(), before drawing). `id`: its widget (the focus).
    /// True when the mouse clicked in the view without dragging it (the enemy editor chooses a part there).
    bool input(UIContext& ui, const std::string& id, float dt);
    bool focused(const UIContext& ui) const;
    /// Its frame when it has the focus (after drawing).
    void drawFocus(UIContext& ui) const;

    // ---------------------------------------------------------------- its parts
    /// The part under the mouse (the smallest piece there), else, the view focused, the one chosen with Tab or
    /// the gamepad (it follows its pieces); `toScreen` places a vertex of the frame as they were drawn.
    void parts(UIContext& ui, const HitPartsSheet& sheet, const std::vector<HitPartPiece>& pieces, const std::function<Vec2(Vec2)>& toScreen);
    /// The part whose tooltip shows ("" none).
    const std::string& shownPart() const { return shown_; }
    /// The part chosen with the keys (the editor: its list too).
    const std::string& chosen() const { return chosen_; }
    void choose(const std::string& part) { chosen_ = part; }
    /// The tooltip of shownPart(), drawn by drawTip().
    void setTip(const PartTip& tip);

    // ---------------------------------------------------------------- its bar, its keys, its tooltip
    /// The bar: pause / play, previous and next frame, « image n / N », − and + with the zoom, « Recentrer »
    /// (measured; on a second row when it does not hold on one). Returns its height.
    float controls(UIContext& ui, const std::string& id, float x, float y, float w);
    /// The height the bar takes at width `w`.
    float controlsHeight(UIContext& ui, float w) const;
    /// The keys of the device in use, wrapped (the room of the longest kept: nothing moves when the device
    /// changes). `picks`: a click chooses a part (the editor). Returns its height.
    float help(UIContext& ui, float x, float y, float w, bool picks = false);
    float helpHeight(UIContext& ui, float w, bool picks = false) const;
    /// The tooltip, over everything (measured, wrapped, inside the screen, never over the pointer). Call last.
    void drawTip(UIContext& ui);

    // ---------------------------------------------------------------- tests, captures
    /// What it drew this frame (reset by controls()).
    HitStageLayout& layout() { return layout_; }
    const HitStageLayout& layout() const { return layout_; }
    /// The layout of the last stage drawn (tests).
    static const HitStageLayout& last();
    /// Captures (--hit-view <zoom>[:<part>[:<frame>]]): the next stage drawn takes this zoom (around that
    /// part), shows that part's tooltip and pauses on that frame (1-based, 0: playing).
    static void requestCapture(float zoom, const std::string& part, int frame);

private:
    float clampZoom(float z) const;
    void clampPan();
    std::string helpText(UIContext& ui, int device, bool picks) const;
    void addText(const std::string& t, const Rect& r, float px, FontFace face);

    // clock
    float t_ = 0.0f;
    bool paused_ = false;
    int frame_ = 0, frames_ = 0;
    std::vector<float> durations_;
    float rest_ = 0.0f;
    // view
    Rect view_, world_;
    float k_ = 1.0f, zoom_ = 1.0f;
    Vec2 pan_;
    // input
    std::string id_;
    bool dragging_ = false, pressed_ = false;
    float dragged_ = 0.0f;
    bool padNorth_ = false, padWest_ = false;
    int cycle_ = 0;  // Tab / Y / X asked: the next (+1) or previous (−1) part, applied by parts()
    // parts
    std::string chosen_, shown_;
    Vec2 anchor_;
    bool fromMouse_ = false;
    PartTip tip_;
    bool tipSet_ = false;
    bool keyTip_ = false;      // a part chosen with Tab / Y / X: its tooltip shows while the view has the focus
    bool captureTip_ = false;  // a capture's part: its tooltip shows until the pointer or a key is used
    bool drawn_ = false;       // drawn this frame (advance() … drawTip())
    HitStageLayout layout_;
};

/// 2.77.1: draws a legend in `box` (its frame filled), every text whole (wrapped, never shortened); scrolled when
/// it is longer (the wheel over it, its bar; focused, ↑ ↓). `id`: its scroll region and its focus. Its lines go
/// to `layout` (legendBoxes, texts) when given.
void drawHitLegend(UIContext& ui, const std::string& id, const Rect& box, const std::vector<HitLegendLine>& lines, HitStageLayout* layout = nullptr);

/// The « Où frapper » panel of a boss's sheet: a chip per phase whose sheet differs, a chip per pose,
/// the stage (its bar, its sprite animated with its parts, its keys), the legend at its right.
class HitPartsView {
public:
    /// Fills `area`. `dt`: the time since the last frame (the animation).
    void draw(GameServices& g, const Rect& area, const EnemyDefinition& d, float dt);
    /// The tooltip, over everything: call at the end of the frame.
    void drawOverlay(GameServices& g);
    /// For the captures (--boss-stats <boss>:frapper[=<clip>]): that pose.
    void choosePose(const std::string& clip) { wanted_ = clip; }
    const HitStage& stage() const { return stage_; }

private:
    std::string of_, wanted_;
    int phase_ = 0, pose_ = 0;
    HitStage stage_;
};

}  // namespace cosmic
