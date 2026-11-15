#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
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
    /// The same, baked over several frames (a level drawn small in a menu): `prepare` lays
    /// the tiles out, then each `bakeSome` bakes chunks for about `budgetMs`, those meeting
    /// `first` (world area) before the others; render() draws the ones baked so far. Returns
    /// how many chunks are left. Once all are baked, the result is the one of build().
    void prepare(ResourceManager& res, const LevelData& level);
    size_t bakeSome(Renderer& r, double budgetMs, const Rect* first = nullptr);
    size_t pendingChunks() const { return todo_.size(); }
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
    struct Layer;  // a layer's tiles and one-way platforms, laid out (TerrainRenderer.cpp)
    void bake(Renderer& r, const std::string& layerId, const Layer& layer, int cx, int cy);
    std::map<std::string, std::shared_ptr<Layer>> layers_;
    std::vector<std::pair<std::string, std::pair<int, int>>> todo_;  // chunks to bake (layer, cx, cy)
    std::map<std::string, Tileset> tilesets_;
    TexturePtr platforms_;
    std::map<std::string, int> platformRows_;
    std::map<std::string, std::vector<Chunk>> chunks_;
};

}  // namespace cosmic
