#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class Renderer;
class ResourceManager;
struct EnemyDefinition;

/// Badge of a boss: a hexagon framing its portrait (a frame of its own sprite, so every
/// boss looks different) or the symbol chosen in the enemy editor ("Aspect"). Drawn on the
/// level progress bar, in the enemy editor and in the level editor's inspector.
namespace BossBadge {
const std::vector<std::string>& icons();       // "portrait", "crown", "skull"...
const std::vector<std::string>& iconLabels();  // French names
Color color(const EnemyDefinition& d);
/// `radius`: inner radius in pixels. `defeated` greys it and crosses it out.
void draw(Renderer& r, ResourceManager& res, const EnemyDefinition& d, Vec2 center, float radius, bool defeated, float time);
}  // namespace BossBadge

}  // namespace cosmic
