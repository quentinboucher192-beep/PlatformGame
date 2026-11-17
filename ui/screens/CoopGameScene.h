#pragma once

// 2.62: cooperation on the EXTRÊME levels (docs/COOP.md): the lobby and the game of a team.

#include <memory>
#include <optional>

#include "engine/core/Scene.h"
#include "game/coop/CoopLink.h"

namespace cosmic {

class GameServices;

/// The game of a team: `link` gives the team frames (LocalCoopLink on one PC).
std::unique_ptr<Scene> makeCoopGame(GameServices& g, CoopSetup setup, std::unique_ptr<CoopLink> link);
/// The lobby (« COOPÉRATION »): the level, the team, the rules; `previous` keeps the team and
/// the level of the game just played.
std::unique_ptr<Scene> makeCoopLobby(GameServices& g, std::optional<CoopSetup> previous = std::nullopt);

}  // namespace cosmic
