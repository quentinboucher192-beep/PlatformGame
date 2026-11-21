#pragma once
// COSMIC FRAME (2.83): the main menu's look (ui/widgets/CosmicStyle) for the other screens and
// ADMINISTRATION, as drop-in replacements of their frame pieces. Each one keeps the behaviour of
// the UIContext widget it stands for (the same widget runs underneath, invisible: navigation,
// shortcuts, clicks, sounds, read-only forms) and only changes the drawing.
//
//   cframe::Backdrop   the planets' parallax (cstyle::PlanetBackdrop) under a veil: same calls
//                      as MenuBackground (init / update / render(g, dim)).
//   cframe::title      the screen's title in chrome letters (or neon), fitted when a width is given.
//   cframe::panel      a holographic glass panel (cstyle::HoloCard), for UIContext::panel.
//   cframe::tabs       a bar of cockpit plates (equal widths), for UIContext::tabs.
//   cframe::button     a cockpit plate, for UIContext::button (primary: glow and light sweep).
//   cframe::back       « Retour »: a diamond icon button (cstyle::diamondButton) and its label.
//   cframe::diamond    the same for a secondary action (another icon).
// Motion follows the player's settings (the last Backdrop updated gives it).

#include <string>
#include <vector>

#include "engine/rendering/Text.h"
#include "ui/widgets/CosmicStyle.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

class GameServices;

namespace cframe {

/// What the player's settings allow (screen shake, particle quality).
cstyle::Motion motion(const GameServices& g);
/// The motion the frame pieces use now (the last backdrop's).
const cstyle::Motion& currentMotion();
/// Screen pixels per reference pixel of the 1280 x 720 layout.
float unit(const UIContext& ui);

class Backdrop {
public:
    void init(GameServices& g);
    void update(float dt);
    /// The planets, their motes and warp, then a veil `dim` dark (0..1) over the whole screen.
    void render(GameServices& g, float dim = 0.0f);

private:
    cstyle::PlanetBackdrop bg_;
    GameServices* g_ = nullptr;
};

/// The title in chrome letters (white over a blue band, dark outline, a light sweeping it) or,
/// `neon`, in glowing cyan; `size` in UI pixels like UIContext::label, shrunk to `maxW` (> 0).
/// Returns the size drawn.
Vec2 title(UIContext& ui, const std::string& text, Vec2 pos, float size, Align a = Align::Left, VAlign va = VAlign::Top,
           float maxW = 0.0f, bool neon = false);
/// What title() would cover (for placing the pieces after it).
Vec2 measureTitle(UIContext& ui, const std::string& text, float size, float maxW = 0.0f);

/// Holographic glass (the main menu's card); `highlight`: a brighter edge (dialogs).
void panel(UIContext& ui, const Rect& r, bool highlight = false, float radius = 10.0f);

/// UIContext::tabs (same rules: Q / E, LB / RB, clicks, badges) drawn as cockpit plates.
bool tabs(UIContext& ui, const std::string& name, const Rect& r, const std::vector<std::string>& labels, int& active,
          const std::vector<int>& badges = {});

/// UIContext::button drawn as a cockpit plate.
bool button(UIContext& ui, const std::string& name, const Rect& r, const std::string& text, const UIContext::ButtonStyle& st);
inline bool button(UIContext& ui, const std::string& name, const Rect& r, const std::string& text) {
    return button(ui, name, r, text, UIContext::ButtonStyle{});
}

/// « Retour » (or `text`) in `r`: a diamond icon at the left, its label after it; the whole rect
/// is clicked. Returns true when activated.
bool back(UIContext& ui, const std::string& name, const Rect& r, const std::string& text = "Retour", const std::string& icon = "quit");
/// A secondary action: a diamond with a menu icon (cstyle::menuIcon ids) at the left of `r`, its
/// label after it (`lit`: the label lit, an option that is on). Returns true when activated.
bool diamond(UIContext& ui, const std::string& name, const Rect& r, const std::string& text, const std::string& icon, bool lit = false);

}  // namespace cframe
}  // namespace cosmic
