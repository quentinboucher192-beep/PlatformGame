#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class GameServices;

/// Renders a 480x270 preview of a level around `focus` (default: player start) and
/// returns it encoded as PNG (used in packages and the import preview).
std::vector<uint8_t> renderLevelThumbnail(GameServices& g, const LevelData& level, const std::string& assetRoot = {},
                                          const Vec2* focus = nullptr);
/// Decodes PNG bytes into a texture.
TexturePtr textureFromPng(GameServices& g, const std::vector<uint8_t>& png);

}  // namespace cosmic
