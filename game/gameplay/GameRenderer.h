#pragma once

#include <unordered_map>

#include "engine/animation/Animation.h"
#include "engine/rendering/Parallax.h"
#include "engine/rendering/Renderer.h"
#include "game/levels/TerrainRenderer.h"

namespace cosmic {

class GameWorld;
class ResourceManager;

struct RenderOptions {
    bool lighting = true;
    bool editor = false;
    bool debug = false;          // colliders, hitboxes, AI states
    float zoomOverride = 0.0f;   // editor camera
    bool mapView = false;        // the level drawn small (pause map): no foreground planes, telegraphs or flash
};

/// Draws a GameWorld: parallax planes, layered terrain and entities, enemies, player,
/// projectiles, particles, dynamic lighting and screen-space effects.
class GameRenderer {
public:
    void build(Renderer& r, ResourceManager& res, const GameWorld& world);
    /// The same, its terrain baked over several frames (a level drawn small in a menu): call
    /// bakeTerrain() each frame until it returns 0 (chunks left; those meeting `first` first).
    void buildProgressive(ResourceManager& res, const GameWorld& world);
    size_t bakeTerrain(Renderer& r, double budgetMs, const Rect* first = nullptr) { return terrain_.bakeSome(r, budgetMs, first); }
    void rebuildTerrain(Renderer& r, ResourceManager& res, const GameWorld& world);
    void render(Renderer& r, ResourceManager& res, GameWorld& world, const WorldView& view, float alpha,
                const RenderOptions& opt);
    const TerrainRenderer& terrain() const { return terrain_; }
    size_t lastLightCount() const { return lastLights_; }

private:
    void drawProjectiles(Renderer& r, GameWorld& world, float alpha, LightMap& lights);
    void drawDebug(Renderer& r, GameWorld& world);

    TerrainRenderer terrain_;
    ParallaxBackground parallax_;
    LightMap lights_;
    AnimationSetPtr projectiles_;
    TexturePtr ringTex_;
    size_t lastLights_ = 0;
};

}  // namespace cosmic
