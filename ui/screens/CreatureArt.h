#pragma once

// Creatures and planets drawn in the menus (Collection → Galerie, boss statistics): display
// only, never part of a run.

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
struct EnemyDefinition;

/// A layer of a planet's backdrop: its texture and the tint it is drawn with.
struct BackdropLayer {
    std::string texture;
    Color tint = Color::white();
};
/// The layers of a planet's backdrop: its own folder (backgrounds/<planet>/), or the layers of
/// its first level (a planet drawn with another one's layers, tinted: Les Confins). Only files
/// that exist (2.49.1: never the checkerboard of a missing texture).
const std::vector<BackdropLayer>& planetBackdrop(GameServices& g, const std::string& planet);

/// A planet's backdrop (its parallax layers stacked) in `dst`.
void drawPlanetBackdrop(GameServices& g, const std::string& planet, const Rect& dst, Color tint = Color::white());

/// The frame of the creature's clip `clipName` at `t` seconds (looping; its idle clip when it
/// has no such clip), fitted in `box` (whole pixel-art multiples when it is enlarged), in its
/// own tint multiplied by `tintMul`.
void drawCreature(GameServices& g, const EnemyDefinition& def, const Rect& box, float t, Color tintMul, const std::string& clipName = "idle");

/// The creature's clips in a showing order: idle, moving, each attack after its wind-up, then
/// the others (summon, roar, hurt, death...).
std::vector<std::string> creatureClips(GameServices& g, const EnemyDefinition& def);
/// Length of one loop of a clip (seconds; 0 when it has none).
float creatureClipDuration(GameServices& g, const EnemyDefinition& def, const std::string& clip);
/// A clip's name in words (« Charge », « Préparation : Barrage »...).
std::string creatureClipLabel(const EnemyDefinition& def, const std::string& clip);

}  // namespace cosmic
