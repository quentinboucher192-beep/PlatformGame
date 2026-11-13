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

    Rect frameRect(float time, float speed) const;
};

/// Named decoration sprites packed in one atlas (sprites/decor/*.json).
class DecorAtlas {
public:
    bool load(ResourceManager& res, const std::string& jsonPath);
    const DecorSprite* find(const std::string& name) const;
    const TexturePtr& texture() const { return texture_; }
    std::vector<std::string> names() const;
    const std::string& texturePath() const { return texturePath_; }

    /// Shared atlas instance for the current asset mounts.
    static DecorAtlas& shared(ResourceManager& res);

private:
    std::map<std::string, DecorSprite> sprites_;
    TexturePtr texture_;
    std::string texturePath_;
};

}  // namespace cosmic
