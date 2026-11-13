#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class ResourceManager;

struct ParallaxLayerDef {
    std::string texture;
    Vec2 factor{0.5f, 0.5f};  // 0 = fixed to the screen, 1 = moves with the world
    Vec2 offset;              // world pixels
    Vec2 scroll;              // automatic scrolling (px / s)
    bool repeatX = true;
    bool fill = false;        // scale to cover the whole screen (skies)
    bool foreground = false;  // drawn over the world
    Color tint = Color::white();
    Color fillBelow = Color::transparent();  // solid colour under the layer
    Color fillAbove = Color::transparent();

    static ParallaxLayerDef fromJson(const Json& j);
    Json toJson() const;
};

/// Multi-plane parallax background.
class ParallaxBackground {
public:
    void load(ResourceManager& res, const std::vector<ParallaxLayerDef>& layers, float referenceY);
    void update(float dt) { time_ += dt; }
    /// Draws background (foreground=false) or foreground layers for the given view.
    void render(Renderer& r, const WorldView& view, bool foreground) const;
    bool empty() const { return layers_.empty(); }

private:
    struct Layer {
        ParallaxLayerDef def;
        TexturePtr tex;
    };
    std::vector<Layer> layers_;
    float referenceY_ = 0.0f;
    float time_ = 0.0f;
};

/// Screen-space light accumulation multiplied over the scene: ambient darkness with
/// coloured additive lights (bioluminescence, projectiles, explosions...).
class LightMap {
public:
    struct Light {
        Vec2 pos;
        float radius;
        Color color;
    };
    void clear() { lights_.clear(); }
    void add(Vec2 worldPos, float radius, Color color) { lights_.push_back({worldPos, radius, color}); }
    /// Renders the light map and multiplies it over what has been drawn so far.
    void apply(Renderer& r, const WorldView& view, Color ambient);
    size_t count() const { return lights_.size(); }

private:
    TexturePtr target_;
    std::vector<Light> lights_;
};

}  // namespace cosmic
