#pragma once

// 2.69 (phase 4B): the own rules of a gigaboss drawn over its arena by the raid's combat screen
// (display only, nothing here reaches the simulation): Skullmaw's gazes on every player, its
// charging flash, its mirrors and, in its darkness, the platforms its eye-light reveals; Dreadfang's
// claws from the ground, its sweeping blades and their storeys, its nailing claws, its anchors and
// chains; Heartbreaker's plates, its beats and their waves, its rib traps, its ribs to pry open.
// The sprites of each come from its generator (tools/assetgen/characters/boss_l_*.py: *_fx.png).

#include <utility>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/core/Math.h"

namespace cosmic {
class Renderer;
class ResourceManager;
class GameWorld;
struct WorldView;
namespace raid {
class Gigaboss;
}

namespace raidui {

class BossOverlay {
public:
    /// Over the world, under the HUD (`clock`: seconds, for the animations).
    void draw(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock);
    /// The places of its rules for the minimap (mirrors, anchors, ribs): their rect and colour.
    static void places(const raid::Gigaboss& gb, std::vector<std::pair<Rect, Color>>& out);

private:
    void skullmaw(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock);
    void dreadfang(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock);
    void heartbreaker(Renderer& r, ResourceManager& res, const GameWorld& w, const raid::Gigaboss& gb, const WorldView& view, float clock);

    AnimationSetPtr skull_, claws_, cage_;
};

}  // namespace raidui
}  // namespace cosmic
