#pragma once
// COSMIC STYLE KIT, second part (2.84): what the campaign's map and levels, the pause menu, the
// results and the COSMIC ONLINE frame needed on top of ui/widgets/CosmicStyle.h (whose API stays as
// it is). Same conventions: screen pixels, `unit` = screen pixels per reference pixel of the
// 1280 x 720 layout (CosmicLayout::unit), font sizes in reference pixels, Motion from the settings.
//
//   cstyle::PlateEx / plateButtonEx   the cockpit plate with more: dimmed (locked), selected (the
//                                     current section), a room kept at the right for the caller's
//                                     drawing (pills, stars, a switch), the label on two lines.
//   cstyle::ChromeText                any title in the chrome of « COSMIC » (gradient, sweep, fade-in).
//   cstyle::neonText                  any title in the neon of « PLATFORMER » (glow, a flicker now and then).
//   cstyle::PlanetScenery             the parallax of ONE planet (the context's: the planet chosen, the
//                                     level's), fading from one to the next; layers read and loaded
//                                     off the main thread or one per frame, never blocking.
//   cstyle::star                      a star, filled by `fill` (0..1) from its centre.
//   cstyle::novaClip                  a clip of Nova (assets/animations/nova.json) at a time into it.
//   cstyle::hintWidth / hintBar       the « [A] Valider » hints measured, as many as fit.
//   cstyle::caption                   the focused diamond's name, fitted (and its « NOUVEAU »).

#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputTypes.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/Text.h"
#include "ui/widgets/CosmicStyle.h"

namespace cosmic {

class UIContext;
class ContentDatabase;
struct LevelData;

namespace cstyle {

struct PlateEx {
    Plate plate;
    bool enabled = true;     // false: dimmed (a locked entry); it still takes the focus, the caller refuses it
    bool selected = false;   // the current section: accent cell lit, a light fill
    bool chevron = true;     // the « › » of the focused plate at the right
    bool wrap = false;       // no subtitle: the label on two lines when one is too short for it
    float rightW = 0.0f;     // room kept at the right (before the chevron) for drawRight
    /// Draws in that room (`right`: its rect, slid with the plate; `k`: the focus 0..1).
    std::function<void(const Rect& right, float k)> drawRight;
};
/// A plate (see plateButton) with those additions; returns true when activated (also when not enabled).
bool plateButtonEx(UIContext& ui, Anims& anims, const std::string& id, const Rect& r, const PlateEx& p, float unit, float appear, float t,
                   float dt, const Motion& m);
/// Reference pixels a plate keeps at its left (accent cell and icon) and right (chevron) around its label.
inline float plateLabelLeft() { return 78.0f; }
inline float plateLabelRightPad(bool chevron) { return chevron ? 18.0f + 22.0f : 18.0f; }

class ChromeText {
public:
    void restart(float t) { t0_ = t; }
    /// `text` (French, translated here) in the chrome of « COSMIC »: Title face `sizePx` screen pixels
    /// high, shrunk to `maxW`; `pos` is its left (Align::Left) or its centre (Align::Center, Right: its
    /// right end), at mid-height. Returns the size drawn (the box of the letters).
    Vec2 draw(UIContext& ui, const std::string& text, Vec2 pos, float sizePx, float maxW, Align a, float t, const Motion& m, float unit);
    /// The size draw() would take.
    static Vec2 measure(UIContext& ui, const std::string& text, float sizePx, float maxW);

private:
    TexturePtr target_;
    float t0_ = 0.0f;
};

/// `text` (French) in neon (glow, a flicker now and then unless reduced), sized as ChromeText.
Vec2 neonText(UIContext& ui, const std::string& text, Vec2 pos, float sizePx, float maxW, Align a, float t, const Motion& m, float unit,
              Color c = Color{159, 243, 255, 255});

class PlanetScenery {
public:
    /// Every planet of the content (teasers included): layers read off the main thread from each
    /// planet's first level (else its backgrounds/<id>/ folder).
    void start(const ContentDatabase& content);
    /// Shows that planet (a short fade from the one shown; `instant`: at once).
    void show(const std::string& planetId, bool instant = false);
    /// Shows a level's own layers (its sky, far, mid, near ones; the textures the game already has).
    void useLevel(const LevelData& level);
    void update(UIContext& ui, float dt, const Motion& m);
    /// Layers, motes, the veil (dark at the left, clear at `veilRight`), all at `alpha`.
    void render(UIContext& ui, float veilRight, float unit, const Motion& m, float alpha = 1.0f);
    const std::string& shown() const { return shown_; }

private:
    using Layers = std::vector<std::pair<std::string, Color>>;
    const Layers* layersOf(const std::string& id) const;
    std::vector<std::pair<std::string, Layers>> planets_;
    std::future<std::vector<Layers>> loading_;
    std::vector<std::string> ids_;
    Layers level_;
    bool useLevel_ = false;
    std::string shown_, wanted_;
    float fade_ = 1.0f;  // 1: the shown planet in full
    std::vector<std::string> ready_;  // textures loaded (one per frame)
    float clock_ = 0.0f;
    struct Mote {
        float x, y, s, p, v;
    };
    std::vector<Mote> motes_;
    int moteQuality_ = -1;
};

/// A five-pointed star of outer radius `r` centred on `c`: `off` everywhere, `on` grown from its
/// centre by `fill` (0..1), its outline in `edge` (transparent: none).
void star(UIContext& ui, Vec2 c, float r, float fill, Color on, Color off, Color edge = Color{0, 0, 0, 0});

/// Nova's `clip` at `time` seconds into it (held on its last frame when it does not loop), the cell
/// `size` pixels square centred on `c`.
void novaClip(UIContext& ui, const std::string& clip, float time, Vec2 c, float size, bool flip = false, Color tint = Color::white());

struct Hint {
    Action action;
    std::string text;
    float gapAfter = -1.0f;  // pixels to the next one (below 0: the bar's gap)
};
/// What Glyphs::drawHint takes at `height`.
float hintWidth(UIContext& ui, Action a, const std::string& text, float height);
/// The hints from `pos` (left end, centre line), as many as fit in `maxW` (in order); returns the width used.
float hintBar(UIContext& ui, const std::vector<Hint>& hints, Vec2 pos, float height, float gap, float maxW);

/// `text` (French) in capitals, fitted into `r` at its left (centre line), and a small « NOUVEAU »
/// after it when `isNew`.
void caption(UIContext& ui, const std::string& text, const Rect& r, float fontRef, float unit, Color c, bool isNew = false);

/// The width UIContext::extremeBadge takes at `height`.
float extremeBadgeWidth(UIContext& ui, float height);

}  // namespace cstyle
}  // namespace cosmic
