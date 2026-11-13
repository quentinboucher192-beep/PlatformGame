#pragma once

#include <string>

#include "engine/core/Math.h"
#include "engine/input/InputTypes.h"

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
}  // namespace Glyphs

}  // namespace cosmic
