#include "game/levels/TerrainRenderer.h"

#include <cmath>
#include <set>
#include <unordered_map>

#include "engine/core/Log.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"

namespace cosmic {

namespace {
const char* kMaterials[] = {"ruin_stone", "ancient_metal", "crystal", "root"};

struct Grid {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<uint8_t> mat;  // 0 = empty, else material index + 1
    std::vector<uint8_t> deco; // surface decoration allowed
    uint8_t at(int x, int y) const {
        if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h) return 0;
        return mat[(y - y0) * w + (x - x0)];
    }
    bool decoAt(int x, int y) const {
        if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h) return false;
        return deco[(y - y0) * w + (x - x0)] != 0;
    }
};

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
}  // namespace

void TerrainRenderer::loadTilesets(ResourceManager& res) {
    tilesets_.clear();
    for (const char* m : kMaterials) {
        auto j = res.json(std::string("textures/tiles/") + m + ".json");
        if (!j) continue;
        Tileset t;
        t.name = m;
        t.texture = res.texture(jstr(*j, "texture"));
        t.tileSize = jget<int>(*j, "tileSize", 16);
        t.maskRow = jget<int>(*j, "maskRow", 0);
        t.variantRow = jget<int>(*j, "variantRow", 1);
        t.variantCount = std::max(1, jget<int>(*j, "variantCount", 16));
        t.innerCornerRow = jget<int>(*j, "innerCornerRow", 2);
        t.topDecorRow = jget<int>(*j, "topDecorRow", 3);
        t.topDecorCount = std::max(1, jget<int>(*j, "topDecorCount", 8));
        t.topDecorOffsetY = jget<int>(*j, "topDecorOffsetY", -12);
        t.mapColor = colorFromJson(j->value("mapColor", Json()), t.mapColor);
        tilesets_[m] = t;
    }
    platformRows_.clear();
    if (auto pj = res.json("textures/tiles/platforms.json")) {
        platforms_ = res.texture(jstr(*pj, "texture", "textures/tiles/platforms.png"));
        if (pj->contains("styles") && (*pj)["styles"].is_object())
            for (auto& [k, v] : (*pj)["styles"].items())
                if (v.is_number_integer()) platformRows_[k] = v.get<int>();
    }
}

const Tileset* TerrainRenderer::tileset(const std::string& material) const {
    auto it = tilesets_.find(material);
    return it == tilesets_.end() ? nullptr : &it->second;
}

size_t TerrainRenderer::chunkCount() const {
    size_t n = 0;
    for (const auto& [k, v] : chunks_) n += v.size();
    return n;
}

void TerrainRenderer::build(Renderer& r, ResourceManager& res, const LevelData& level) {
    chunks_.clear();
    loadTilesets(res);

    std::map<std::string, std::vector<const LevelObject*>> solidsByLayer, platformsByLayer;
    for (const auto& o : level.objects) {
        const LevelLayer* l = level.findLayer(o.layer);
        if (l && !l->visible) continue;
        if (o.type == "solid") solidsByLayer[o.layer].push_back(&o);
        if (o.type == "oneway") platformsByLayer[o.layer].push_back(&o);
    }
    std::set<std::string> layers;
    for (auto& [k, v] : solidsByLayer) layers.insert(k);
    for (auto& [k, v] : platformsByLayer) layers.insert(k);

    for (const auto& layerId : layers) {
        const auto& solids = solidsByLayer[layerId];
        const auto& plats = platformsByLayer[layerId];
        // Tile grid covering every solid of the layer.
        Grid g;
        if (!solids.empty()) {
            int minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
            for (const auto* o : solids) {
                minX = std::min(minX, static_cast<int>(std::floor(o->rect.x / kTile)));
                minY = std::min(minY, static_cast<int>(std::floor(o->rect.y / kTile)));
                maxX = std::max(maxX, static_cast<int>(std::ceil(o->rect.right() / kTile)));
                maxY = std::max(maxY, static_cast<int>(std::ceil(o->rect.bottom() / kTile)));
            }
            g.x0 = minX;
            g.y0 = minY;
            g.w = std::max(0, maxX - minX);
            g.h = std::max(0, maxY - minY);
            if (static_cast<int64_t>(g.w) * g.h > 4'000'000) {
                Log::error("Terrain layer '{}' too large to bake", layerId);
                continue;
            }
            g.mat.assign(static_cast<size_t>(g.w) * g.h, 0);
            g.deco.assign(static_cast<size_t>(g.w) * g.h, 0);
            for (const auto* o : solids) {
                int mi = 1;
                std::string m = o->propStr("material", "ruin_stone");
                for (int i = 0; i < 4; ++i)
                    if (m == kMaterials[i]) mi = i + 1;
                bool deco = o->prop<bool>("decorTop", true);
                int tx0 = static_cast<int>(std::floor(o->rect.x / kTile)), tx1 = static_cast<int>(std::ceil(o->rect.right() / kTile));
                int ty0 = static_cast<int>(std::floor(o->rect.y / kTile)), ty1 = static_cast<int>(std::ceil(o->rect.bottom() / kTile));
                for (int y = ty0; y < ty1; ++y)
                    for (int x = tx0; x < tx1; ++x) {
                        size_t idx = static_cast<size_t>((y - g.y0) * g.w + (x - g.x0));
                        g.mat[idx] = static_cast<uint8_t>(mi);
                        g.deco[idx] = deco ? 1 : 0;
                    }
            }
        }

        // Which chunks contain something?
        std::set<std::pair<int, int>> used;
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
                if (g.mat[static_cast<size_t>(y * g.w + x)])
                    used.insert({floorDiv(g.x0 + x, kChunkTiles), floorDiv(g.y0 + y, kChunkTiles)});
        for (const auto* p : plats) {
            int tx0 = static_cast<int>(std::floor(p->rect.x / kTile)), tx1 = static_cast<int>(std::ceil(p->rect.right() / kTile));
            int ty = static_cast<int>(std::floor(p->rect.y / kTile));
            for (int x = tx0; x < tx1; ++x) used.insert({floorDiv(x, kChunkTiles), floorDiv(ty, kChunkTiles)});
        }

        const int chunkPx = kChunkTiles * kTile;
        const int texSize = chunkPx + 2 * kMargin;
        for (const auto& [cx, cy] : used) {
            TexturePtr tex = r.createTarget(texSize, texSize, false);
            if (!tex) continue;
            r.setTarget(tex);
            SDL_SetRenderDrawColor(r.sdl(), 0, 0, 0, 0);
            SDL_SetRenderDrawBlendMode(r.sdl(), SDL_BLENDMODE_NONE);
            SDL_RenderClear(r.sdl());
            const float ox = static_cast<float>(cx * chunkPx - kMargin);
            const float oy = static_cast<float>(cy * chunkPx - kMargin);
            auto draw = [&](const Tileset& ts, int col, int row, float wx, float wy) {
                Rect src{static_cast<float>(col * ts.tileSize), static_cast<float>(row * ts.tileSize),
                         static_cast<float>(ts.tileSize), static_cast<float>(ts.tileSize)};
                r.drawTexture(*ts.texture, &src, {wx - ox, wy - oy, static_cast<float>(kTile), static_cast<float>(kTile)});
            };
            // Pass 1: tiles + inner corners
            for (int ty = cy * kChunkTiles; ty < (cy + 1) * kChunkTiles; ++ty) {
                for (int tx = cx * kChunkTiles; tx < (cx + 1) * kChunkTiles; ++tx) {
                    uint8_t m = g.at(tx, ty);
                    if (!m) continue;
                    const Tileset* ts = tileset(kMaterials[m - 1]);
                    if (!ts || !ts->texture) continue;
                    int mask = (g.at(tx, ty - 1) == m ? 1 : 0) | (g.at(tx + 1, ty) == m ? 2 : 0) |
                               (g.at(tx, ty + 1) == m ? 4 : 0) | (g.at(tx - 1, ty) == m ? 8 : 0);
                    float wx = static_cast<float>(tx * kTile), wy = static_cast<float>(ty * kTile);
                    if (mask == 15) {
                        uint32_t h = hash2(tx, ty, 7);
                        // Mostly the first variants, occasional details.
                        int v = (h % 100) < 70 ? static_cast<int>(h % 4) : static_cast<int>((h >> 8) % ts->variantCount);
                        draw(*ts, v, ts->variantRow, wx, wy);
                        if (g.at(tx - 1, ty - 1) != m) draw(*ts, 0, ts->innerCornerRow, wx, wy);
                        if (g.at(tx + 1, ty - 1) != m) draw(*ts, 1, ts->innerCornerRow, wx, wy);
                        if (g.at(tx - 1, ty + 1) != m) draw(*ts, 2, ts->innerCornerRow, wx, wy);
                        if (g.at(tx + 1, ty + 1) != m) draw(*ts, 3, ts->innerCornerRow, wx, wy);
                    } else {
                        draw(*ts, mask, ts->maskRow, wx, wy);
                    }
                }
            }
            // Pass 2: surface decoration on exposed tops (drawn over neighbours' tiles)
            for (int ty = cy * kChunkTiles; ty < (cy + 1) * kChunkTiles; ++ty) {
                for (int tx = cx * kChunkTiles; tx < (cx + 1) * kChunkTiles; ++tx) {
                    uint8_t m = g.at(tx, ty);
                    if (!m || g.at(tx, ty - 1) || !g.decoAt(tx, ty)) continue;
                    const Tileset* ts = tileset(kMaterials[m - 1]);
                    if (!ts || !ts->texture) continue;
                    uint32_t h = hash2(tx, ty, 99);
                    if (h % 10 < 2) continue;  // leave some bare spots
                    int v = static_cast<int>((h >> 4) % ts->topDecorCount);
                    draw(*ts, v, ts->topDecorRow, static_cast<float>(tx * kTile),
                         static_cast<float>(ty * kTile + ts->topDecorOffsetY));
                }
            }
            // One-way platforms
            if (platforms_) {
                for (const auto* p : plats) {
                    int ty = static_cast<int>(std::floor(p->rect.y / kTile));
                    if (floorDiv(ty, kChunkTiles) != cy) continue;
                    auto it = platformRows_.find(p->propStr("style", "ruin_bridge"));
                    int row = it == platformRows_.end() ? 0 : it->second;
                    int cols = std::max(1, static_cast<int>(std::round(p->rect.w / kTile)));
                    for (int i = 0; i < cols; ++i) {
                        int tx = static_cast<int>(std::floor(p->rect.x / kTile)) + i;
                        if (floorDiv(tx, kChunkTiles) != cx) continue;
                        int col = cols == 1 ? 1 : (i == 0 ? 0 : (i == cols - 1 ? 2 : 1));
                        Rect src{col * 16.0f, row * 16.0f, 16, 16};
                        r.drawTexture(*platforms_, &src, {p->rect.x + i * 16.0f - ox, p->rect.y - oy, 16, 16});
                    }
                }
            }
            r.setTarget(nullptr);
            chunks_[layerId].push_back({cx, cy, tex});
        }
    }
}

void TerrainRenderer::render(Renderer& r, const std::string& layerId) const {
    auto it = chunks_.find(layerId);
    if (it == chunks_.end()) return;
    const Rect vis = r.view().visibleWorld();
    const float chunkPx = static_cast<float>(kChunkTiles * kTile);
    for (const Chunk& c : it->second) {
        Rect world{c.cx * chunkPx - kMargin, c.cy * chunkPx - kMargin, chunkPx + 2 * kMargin, chunkPx + 2 * kMargin};
        if (!world.intersects(vis)) continue;
        r.drawWorldTexture(*c.tex, nullptr, world);
    }
}

}  // namespace cosmic
