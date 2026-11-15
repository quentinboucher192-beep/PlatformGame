#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputTypes.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class UIContext;
struct PowerDefinition;
struct PowerKeyMap;
struct ResolvedPowerKey;

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

/// A key of the keyboard / mouse (`pad` false) or a gamepad binding (`pad` true): its keycap or
/// its gamepad icon, « — » for none. Left edge at `pos.x`, centred on `pos.y`; returns the
/// width (`draw` false: only measures).
float drawBinding(UIContext& ui, SDL_Scancode key, const PadBinding& padBinding, bool pad, Vec2 pos, float height, bool draw = true);
/// Whether the prompts follow the gamepad (the device in use).
bool showsPad(UIContext& ui);
/// 2.31: the key of a power in a level (game/powers/PowerKeys.h: levelPowerKey,
/// levelPowerKeys), as the HUD shows it: its key on the keyboard / mouse or its button on the
/// gamepad, following the device in use; « — » when it has none.
float drawPowerKey(UIContext& ui, const ResolvedPowerKey& key, Vec2 pos, float height, bool draw = true);
/// How a power is used in a level: its own key (drawPowerKey, from `keys`) for a power with a
/// key, else its move or « Passif » in the dashed gold frame (drawFreePowerUse, `passiveText`).
float drawPowerUse(UIContext& ui, const PowerDefinition& def, const PowerKeyMap& keys, Vec2 pos, float height, float alpha = 1.0f,
                   bool draw = true, const std::string& passiveText = "Passif");
}  // namespace Glyphs

}  // namespace cosmic
