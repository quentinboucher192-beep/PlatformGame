#pragma once

#include <map>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"

namespace cosmic {

class ResourceManager;

struct DecorSprite {
    std::string name;
    Rect rect;          // first frame in the atlas
    Vec2 pivot;         // in frame pixels
    int frames = 1;
    float fps = 0.0f;
    Color glow = Color::transparent();
    float glowRadius = 0.0f;
    Vec2 glowOffset;
    TexturePtr texture;        // atlas texture holding this sprite
    std::string atlas;         // atlas JSON (relative asset path)
    std::string texturePath;   // atlas PNG (relative asset path)

    Rect frameRect(float time, float speed) const;
};

/// Named decoration sprites packed in atlases (sprites/decor/<planet>_decor.json).
/// Several atlases can be merged (one per planet); sprite names are unique.
class DecorAtlas {
public:
    /// Replaces the content with one atlas.
    bool load(ResourceManager& res, const std::string& jsonPath);
    /// Adds the sprites of another atlas (names already present are kept).
    bool append(ResourceManager& res, const std::string& jsonPath, bool quiet = false);
    const DecorSprite* find(const std::string& name) const;
    /// Texture of the first atlas (kept for single-atlas users).
    const TexturePtr& texture() const { return texture_; }
    /// Names grouped by atlas, in load order.
    std::vector<std::string> names() const { return order_; }
    const std::string& texturePath() const { return texturePath_; }

    /// Atlases shipped with the game, merged by shared().
    static const std::vector<std::string>& builtinAtlases();
    /// Shared atlas instance (every planet atlas) for the current asset mounts.
    static DecorAtlas& shared(ResourceManager& res);

private:
    std::map<std::string, DecorSprite> sprites_;
    std::vector<std::string> order_;
    TexturePtr texture_;
    std::string texturePath_;
};

}  // namespace cosmic
