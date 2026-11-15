#pragma once

#include "engine/core/Math.h"

namespace cosmic {

class UIContext;

/// The frame of an EXTRÊME level (one life): a blood-dark backdrop lit from below, a cracked
/// border that pulses like a heartbeat, flames licking the bottom edge, embers rising and
/// thorns at the corners. Drawn instead of the usual panel, before the panel's content.
/// `seed` varies the flames between two frames on the same screen.
/// `flameHeight`: tallest flame in unscaled pixels (kept under the panel's lowest text).
void drawExtremeFrame(UIContext& ui, const Rect& box, float radius, float seed = 0.0f, float flameHeight = 46.0f);

/// A list row of an EXTRÊME level, drawn over the row (never over its text): a beating red
/// border and embers rising at its right end.
void drawExtremeRowAccent(UIContext& ui, const Rect& row, float seed = 0.0f);

}  // namespace cosmic
