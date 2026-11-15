#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputTypes.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class UIContext;

/// Button prompts that follow the device in use: keycaps for the keyboard, Xbox /
/// PlayStation / generic icons for gamepads (switching live).
namespace Glyphs {
const char* sheetFor(GamepadStyle style);
/// Draws the primary binding of `a` with its left edge at `pos.x`, vertically centred
/// on `pos.y`. Returns the drawn width.
float drawAction(UIContext& ui, Action a, Vec2 pos, float height);
float drawKeycap(UIContext& ui, const std::string& label, Vec2 pos, float height);
float drawPadButton(UIContext& ui, const PadBinding& b, Vec2 pos, float height);
/// "[A] Valider   [B] Retour" style hint bar; returns its width.
float drawHint(UIContext& ui, Action a, const std::string& text, Vec2 pos, float height);
/// Width drawAction() would take, without drawing.
float measureAction(UIContext& ui, Action a, float height);
/// Several actions pressed together, « [S] + [J] » (left edge at `pos.x`, centred on
/// `pos.y`); returns the width. `draw` false: only measures.
float drawKeys(UIContext& ui, const std::vector<Action>& keys, Vec2 pos, float height, bool draw = true);

/// Powers that do not use the key of their slot (passive, or used with a move of the
/// character: glider, triple jump, ground pound, charged shot, grapple) wear this frame
/// everywhere — HUD, choice of the power to replace, power guide: a dashed gold outline,
/// unlike the solid frame of the powers on their slot key.
Color freePowerColor();
void drawFreePowerFrame(UIContext& ui, const Rect& r, float radius, float thickness, float alpha = 1.0f);
/// How such a power is used, in that frame: « [S] + [J] en l'air », « Passif : toujours
/// actif » (no keys). Left edge at `pos.x`, centred on `pos.y`, `height` = the keys; returns
/// the width of the badge (`draw` false: only measures).
float drawFreePowerUse(UIContext& ui, const std::vector<Action>& keys, const std::string& text, Vec2 pos, float height,
                       float alpha = 1.0f, bool draw = true);
}  // namespace Glyphs

}  // namespace cosmic
