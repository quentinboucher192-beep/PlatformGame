#pragma once
// Drawing of the clans (2.66): the emblem (a shape and two colours on a rounded tile, like the
// frames of the Collection) and the « [TAG] » before a username.

#include <string>

#include "engine/core/Math.h"
#include "game/online/Clans.h"

namespace cosmic {

class UIContext;

/// The emblem in the square `r`: a tile of the second colour (dark background, bright border)
/// and the shape of the first colour in its middle.
void drawClanEmblem(UIContext& ui, const Rect& r, const clans::Emblem& e);

/// The colour of a clan role (chief gold, officer violet, member dimmed).
Color clanRoleColor(UIContext& ui, const std::string& role);

}  // namespace cosmic
