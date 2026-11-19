#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/rendering/Parallax.h"
#include "engine/rendering/Renderer.h"
#include "game/levels/TerrainRenderer.h"

namespace cosmic {

class GameWorld;
class ResourceManager;
struct RenderContext;

struct RenderOptions {
    bool lighting = true;
    bool editor = false;
    bool debug = false;          // colliders, hitboxes, AI states
    float zoomOverride = 0.0f;   // editor camera
    bool mapView = false;        // the level drawn small (pause map): no foreground planes, telegraphs or flash
    /// 2.60, display only: told about the shapes of the boss's attacks — the enemies' shots, the
    /// telegraphs and the lasting effects that hurt — to fade the blocks of the HUD over them
    /// (Hud::threatProbe). nullptr: none.
    CoverageProbe* threats = nullptr;
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
    /// 2.62, a team: the colour of each player (souls, standby ghosts). Display only.
    void setTeamColors(std::vector<Color> colors) { teamColors_ = std::move(colors); }
    Color teamColor(int i) const;
    /// 2.75, display only: the players of this computer (bit i: player i), drawn fully opaque; the
    /// others (online teammates, simulated players) semi-transparent with an outline of their colour,
    /// so that they never hide this computer's player. Every bit set (the default): all opaque.
    void setOpaquePlayers(uint32_t mask) { opaquePlayers_ = mask; }
    uint32_t opaquePlayers() const { return opaquePlayers_; }
    /// The opacity of another player's body (display only; tests).
    static constexpr float kOtherPlayerOpacity = 0.5f;
    /// 2.77, display only: where the soul of player `i` is drawn (its feet): it glides from where its
    /// player died to where it waits (GameWorld::TeamMember::soul), and again when it is moved (a liquid
    /// rising over it, a passage). `fallback` when it is not down.
    Vec2 soulShown(int i, Vec2 fallback) const {
        return i >= 0 && i < static_cast<int>(souls_.size()) && souls_[static_cast<size_t>(i)].down ? souls_[static_cast<size_t>(i)].at : fallback;
    }
    /// How long a soul takes to glide `distance` world pixels (seconds of the world).
    static float soulGlideTime(float distance) { return distance <= 0.5f ? 0.0f : std::min(1.0f, 0.3f + distance / 700.0f); }
    size_t lastLightCount() const { return lastLights_; }

private:
    void drawProjectiles(Renderer& r, GameWorld& world, float alpha, LightMap& lights, CoverageProbe* threats);
    void drawDebug(Renderer& r, GameWorld& world);
    /// 2.56, the Phénix's « Cendres »: ash falling over the arena and a grey veil away from Nova
    /// (around her the view stays clear; the HUD is drawn after the world, never veiled).
    void drawAshVeil(Renderer& r, GameWorld& world, const WorldView& view, float alpha);
    /// 2.62: the players of a team: in play as usual, a soul for a player down (its revival
    /// as a ring filling up), a grey ghost for a player in standby.
    void drawTeam(RenderContext& ctx, GameWorld& world);
    std::vector<Color> teamColors_;
    uint32_t opaquePlayers_ = 0xFFFFFFFFu;
    struct SoulGlide {
        bool seen = false;  // drawn once on its feet: its next soul glides from where it died
        bool down = false;
        Vec2 from, to, at;
        float t = 0.0f, duration = 0.0f;
    };
    std::vector<SoulGlide> souls_;  // display: the souls as drawn (2.77)
    float soulClock_ = -1.0f;       // display: the world time of the last team drawn

    TerrainRenderer terrain_;
    ParallaxBackground parallax_;
    LightMap lights_;
    AnimationSetPtr projectiles_;
    TexturePtr ringTex_;
    size_t lastLights_ = 0;
    float ashVeil_ = 0.0f;     // display: how thick the Phénix's ashes are (fades in and out)
    float ashClock_ = -1.0f;   // display: the world time of the last frame drawn
};

}  // namespace cosmic
