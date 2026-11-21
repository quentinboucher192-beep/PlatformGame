#pragma once
// COSMIC STYLE KIT (2.83, the main menu's look, to restyle the other menus and ADMINISTRATION with).
//
// Every piece draws with the UIContext of the frame and takes part in its keyboard / gamepad /
// mouse navigation (plates and diamonds are UIContext hotspots: arrows, D-pad, stick, hover,
// click). Sizes are in screen pixels; `unit` is the number of screen pixels for one pixel of the
// 1280 x 720 reference layout (CosmicLayout::unit), font sizes are given in those reference pixels.
// Motion follows the player's settings (cstyle::Motion: no screen shake = reduced motion, the
// particle quality = how many motes, streaks and trail stars).
//
//   cstyle::Anims            per-widget 0..1 focus animations (one per screen).
//   cstyle::plateButton      chamfered « cockpit » plate: pixel icon, accent cell, label, subtitle,
//                            optional count pill, banner thumbnail and progress bar (`big`); the
//                            focused one slides, glows, shows a sweep and a chevron, its icon pops.
//   cstyle::diamondButton    diamond icon button with an unread-count badge (`danger`: red focus).
//   cstyle::HoloCard         holographic card: glass, cyan edge, scanlines, flicker-in (flicker())
//                            between begin() and end(); text() lays kicker / title / big value /
//                            paragraph / meta into a rect, fitted and never overflowing.
//   cstyle::TitleLogo        chrome « COSMIC » (letters drop in, light sweep, chromatic split) and
//                            neon « PLATFORMER » (letter flicker), Nova gliding on a dotted orbit
//                            ring behind and in front of the title, with a star trail.
//   cstyle::PlanetBackdrop   parallax of the campaign's planets (each planet's first level gives
//                            its layers and tints: Les Confins' tinted Pyralis), cycling every 15 s
//                            with a hyperspace warp (streaks + flash), twinkling motes, a dark veil
//                            on the left; drawNames() lists the planets, the current one lit with
//                            the progress of its cycle underlined.
//   cstyle::Runner           Nova running on a light runway; jump() when the selection changes.
//   cstyle::menuIcon         a 24 x 24 icon of assets/ui/menu_icons.png (tools/assetgen/environment/menu_icons.py).

#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"

namespace cosmic {

class UIContext;
class ContentDatabase;

namespace cstyle {

/// What the settings allow: `reduced` (screen shake at 0) drops the warp's flash and streaks, the
/// title's glitch, the slides and drops; `particles` (0 low, 1 medium, 2 high) scales the motes,
/// streaks and trail.
struct Motion {
    bool reduced = false;
    int particles = 2;
    static Motion from(int particleQuality, float screenShake) { return {screenShake <= 0.01f, particleQuality}; }
};

/// Per-widget focus animations (0..1, eased towards the target).
class Anims {
public:
    float step(const std::string& key, bool on, float dt, float speed = 10.0f);

private:
    std::unordered_map<std::string, float> v_;
};

/// Screen pixels per reference pixel, and the reference frame (1280 x 720) centred on the screen.
struct CosmicLayout {
    float unit = 1.0f;
    Vec2 origin;
    static CosmicLayout of(float screenW, float screenH);
    Vec2 at(float x, float y) const { return {origin.x + x * unit, origin.y + y * unit}; }
    Rect rect(float x, float y, float w, float h) const { return {origin.x + x * unit, origin.y + y * unit, w * unit, h * unit}; }
    float px(float v) const { return v * unit; }
};

/// The upper case of a French or English text (accented letters included).
std::string upper(const std::string& s);

/// Index of a menu icon by id (continue, campaign, challenges, coop, online, news, achievements,
/// collection, stats, settings, quit, admin); -1 when unknown.
int menuIconIndex(const std::string& id);
/// Draws that icon into `dst` (nearest filtering), rotated by `angleDeg` around its centre.
void menuIcon(UIContext& ui, const std::string& id, const Rect& dst, float angleDeg = 0.0f, Color tint = Color::white());

/// Chamfered polygon (top-left and bottom-right corners cut by `cut`).
std::vector<Vec2> chamfer(const Rect& r, float cut);
/// A convex polygon filled with a horizontal gradient.
void fillPolyH(UIContext& ui, const std::vector<Vec2>& pts, Color left, Color right);
/// Its outline, `thickness` pixels wide.
void strokePoly(UIContext& ui, const std::vector<Vec2>& pts, Color c, float thickness);
/// A red counter pill centred on `c` (« 99+ » beyond 99), `h` pixels high.
void badgePill(UIContext& ui, Vec2 c, int count, float h, Color fill);

struct Plate {
    std::string label;     // drawn in capitals
    std::string sub;       // one line, elided
    std::string icon;      // menuIcon id
    Color accent{94, 224, 255, 255};
    bool big = false;      // taller, with a thumbnail and a progress bar
    TexturePtr thumb;      // big: banner at the right
    float progress = -1;   // big: 0..1 (below 0: no bar)
    int badge = 0;         // red count pill (unread…)
    std::string feature;   // « NOUVEAU » feature (UIContext::newBadge)
};
/// A plate at `r` (its place in the navigation; the drawing slides from it). `appear` 0..1 slides
/// it in from the left. Returns true when activated (Enter / A / click).
bool plateButton(UIContext& ui, Anims& anims, const std::string& id, const Rect& r, const Plate& p, float unit, float appear, float t,
                 float dt, const Motion& m);

struct Diamond {
    std::string icon;
    int badge = 0;
    bool danger = false;
    std::string feature;  // « NOUVEAU »: a small gold mark on the diamond (newBadge without its pill)
};
/// A diamond of half-diagonal `half` centred on `c`; returns true when activated. `isNew` (optional):
/// whether its « NOUVEAU » is due.
bool diamondButton(UIContext& ui, Anims& anims, const std::string& id, Vec2 c, float half, const Diamond& d, float appear, float t,
                   float dt, bool* isNew = nullptr);

class HoloCard {
public:
    /// The content changed: it flickers in from now.
    void flicker(float t) { t0_ = t; }
    /// Glass, edge and glow, then a clip on the card while it flickers in (call end() after).
    void begin(UIContext& ui, const Rect& r, float t, float unit, const Motion& m);
    /// Scanlines and the flicker's light; pops the clip.
    void end(UIContext& ui, float t);

    struct Text {
        std::string kicker, head, big, para, meta;
        Color bigColor{255, 205, 110, 255};
        bool headVerbatim = false;  // the head is a player's name: never translated
    };
    /// Lays the texts into `r` (kicker, head, big value, paragraph wrapped, meta at the bottom):
    /// each fitted or shortened, the paragraph dropped when there is no room for it.
    static void text(UIContext& ui, const Rect& r, const Text& tx, float unit);

private:
    Rect r_;
    float t0_ = -10.0f;
    bool reduced_ = false;
};

class TitleLogo {
public:
    /// Starts the drop-in of the letters again.
    void restart(float t) { t0_ = t; }
    /// The title centred on `cx`, its box starting at `top` (240 reference pixels high, the orbit
    /// ring 660 wide around it).
    void draw(UIContext& ui, float cx, float top, float unit, float t, const Motion& m);

private:
    void drawOrbit(UIContext& ui, float cx, float top, float unit, float t, const Motion& m, bool back);
    TexturePtr target_;
    float t0_ = 0.0f;
};

class PlanetBackdrop {
public:
    static constexpr float kCycle = 15.0f;  // seconds on each planet
    static constexpr float kWarp = 1.4f;    // the hyperspace jump

    struct Planet {
        std::string id, name;
        Color color{57, 240, 216, 255};
        std::vector<std::pair<std::string, Color>> layers;  // sky, far, mid, near: texture and tint
    };
    /// The campaign's planets (content order); their layers are read off the main thread from
    /// each planet's first level, and used as soon as they are there.
    void start(const ContentDatabase& content);
    void update(UIContext& ui, float dt, const Motion& m);
    /// Layers, motes, the veil (dark at the left edge, clear at `veilRight`) and the warp, whose
    /// streaks come from `warpCenter`.
    void render(UIContext& ui, Vec2 warpCenter, float veilRight, float unit, const Motion& m);
    /// The planets' names on one line centred on `c`, fitted to `maxW`; the current one lit and
    /// its cycle underlined.
    void drawNames(UIContext& ui, Vec2 c, float maxW, float fontRef, float unit);
    void warpNow();
    int current() const { return current_; }
    const std::vector<Planet>& planets() const { return planets_; }

private:
    void ensureMotes(const Motion& m);
    std::vector<Planet> planets_;
    std::future<std::vector<std::vector<std::pair<std::string, Color>>>> loading_;
    int current_ = 0;
    float clock_ = 0.0f, nextWarp_ = kCycle, warpT_ = -1.0f;
    bool swapped_ = false;
    float scroll_[4] = {0, 0, 0, 0};
    float speed_ = 1.0f;
    size_t preload_ = 0;
    struct Mote {
        float x, y, s, p, v;
    };
    std::vector<Mote> motes_;
    int moteQuality_ = -1;
};

class Runner {
public:
    void jump(float t) {
        if (jumpT_ < 0.0f) jumpT_ = t;
    }
    /// The runway from `x0` to `x1` at `groundY`, Nova at `novaX` (96 reference pixels high).
    void draw(UIContext& ui, float x0, float x1, float novaX, float groundY, float unit, float t, const Motion& m);

private:
    float jumpT_ = -1.0f, landT_ = -10.0f;
};

}  // namespace cstyle
}  // namespace cosmic
