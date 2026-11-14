#pragma once

#include <string>
#include <vector>

#include "engine/rendering/Renderer.h"

namespace cosmic {

class ResourceManager;

/// Textured beams (2.8): the bosses' lasers and breaths drawn with an animated strip of
/// animations/beams.json (flame, frost, plasma, thorn, star) instead of plain lines. The
/// strip has a start cap (at the mouth), a body repeated along the beam and an end cap.
namespace BeamRenderer {

/// Styles of the atlas, in the order of the editor's list ("" = the drawn line of old).
const std::vector<std::string>& styles();
const std::vector<std::string>& styleLabels();  // French names, "" first ("Trait lumineux")

/// Colour of a style's glow and light (the beam colour of the attack for the drawn line).
Color styleColor(const std::string& style, Color fallback);

/// Draws the beam from `origin` (world) towards `angle` (radians) through the renderer's
/// current view. Returns false when the style does not exist (the caller draws a line).
bool draw(Renderer& r, ResourceManager& res, const std::string& style, Vec2 origin, float angle, float length,
          float width, float time, Color tint = Color::white());

}  // namespace BeamRenderer

}  // namespace cosmic
