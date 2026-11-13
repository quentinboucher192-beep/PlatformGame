#pragma once

#include <map>
#include <string>
#include <vector>

#include "engine/rendering/Renderer.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ResourceManager;

struct Tileset {
    std::string name;
    TexturePtr texture;
    int tileSize = 16;
    int maskRow = 0, variantRow = 1, variantCount = 16, innerCornerRow = 2, topDecorRow = 3, topDecorCount = 8;
    int topDecorOffsetY = -12;
    Color mapColor{60, 80, 100, 255};
};

/// Autotiles the terrain (4-neighbour masks, interior variants, inner corners, surface
/// decoration) and bakes it into 256x256 chunk textures per layer, so a huge level costs
/// a handful of draw calls per frame.
class TerrainRenderer {
public:
    static constexpr int kChunkTiles = 16;
    static constexpr int kTile = 16;
    static constexpr int kMargin = 16;  // room for surface decoration overhang

    void build(Renderer& r, ResourceManager& res, const LevelData& level);
    /// True if `material` has a tileset slot in the renderer.
    static bool supportsMaterial(const std::string& material);
    void render(Renderer& r, const std::string& layerId) const;
    void clear() { chunks_.clear(); }
    const Tileset* tileset(const std::string& material) const;
    size_t chunkCount() const;

private:
    struct Chunk {
        int cx, cy;
        TexturePtr tex;
    };
    void loadTilesets(ResourceManager& res);
    std::map<std::string, Tileset> tilesets_;
    TexturePtr platforms_;
    std::map<std::string, int> platformRows_;
    std::map<std::string, std::vector<Chunk>> chunks_;
};

}  // namespace cosmic
