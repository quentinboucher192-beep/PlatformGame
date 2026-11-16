#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Scene.h"
#include "game/levels/LevelData.h"
#include "game/powers/PowerKeys.h"

namespace cosmic {

class GameServices;

/// The keys of the powers of `level` for this player (2.31): their own choice for that level,
/// else the level's (powerKeys), else the suggestions, against the player's commands; `extra`:
/// powers given besides the level's own (trainings). Exactly the keys of a run of that level.
PowerKeyMap playerPowerKeys(GameServices& g, const LevelData& level, const std::vector<std::string>& extra = {});
/// The key of one power in `level` for this player (see levelPowerKey in PowerKeys.h). Draw
/// it with Glyphs::drawPowerKey (keyboard or gamepad, the device in use) or, for any power,
/// Glyphs::drawPowerUse (its key, or its move / « Passif » in the dashed gold frame).
ResolvedPowerKey playerPowerKey(GameServices& g, const LevelData& level, const std::string& power, const std::vector<std::string>& extra = {});

/// « Touches de ce niveau »: the powers of `level` used with a key of their own, with their key
/// and their gamepad button for this player, changed like in the settings (any key, mouse
/// button, notch of the wheel; any button or trigger); « Par défaut » goes back to the level's
/// key. A key of a base command is flagged (the command wins). Opened from the level panels
/// (before « Jouer ») and from the pause menu; `extra`: powers given besides the level's own.
/// `carried` (2.50, from the pause menu): the powers in hand, by slot (index = slot, "" = empty),
/// shown first with the key and the button they use now (a shared gamepad button moved).
std::unique_ptr<Scene> makeLevelPowerKeys(GameServices& g, LevelData level, std::vector<std::string> extra = {},
                                          std::vector<std::string> carried = {});

}  // namespace cosmic
