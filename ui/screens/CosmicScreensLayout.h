#pragma once

#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

/// 2.84: where everything goes on the screens restyled with the Cosmic kit (ui/widgets/CosmicStyle
/// and CosmicStyle2): the 1280 x 720 reference layout scaled and centred (CosmicLayout), each place
/// one after the other, never over another. tests/test_cosmic_screens.cpp checks them at 1280 x 720
/// and 1920 x 1080, and that their labels fit in French and in English.

/// Campagne › carte des planètes: the title, the star map, the holographic card of the planet
/// chosen (its banner, its texts, its progress and medals), « Explorer », « Menu principal » and the hints.
struct PlanetMapLayout {
    float unit = 1.0f;
    Rect title, subtitle;
    Rect map;           // the orbits and planets (drawn clipped to it)
    float mapScale = 1.0f;  // screen pixels per orbit unit (the whole system fits the map)
    Rect card, banner, cardText, progress;
    Rect explore;       // the plate
    Vec2 back;          // the diamond
    float diamondHalf = 22.0f;
    Rect backCaption, hints;
    static PlanetMapLayout compute(float screenW, float screenH);
    /// The extent of the plate once focused (slid right, glowing).
    Rect exploreExtent() const { return {explore.x - 8 * unit, explore.y - 8 * unit, explore.w + 32 * unit, explore.h + 16 * unit}; }
};

/// Campagne › niveaux d'une planète: the title, the level cards, the holographic card of the level
/// chosen, « Jouer », the diamonds (Fantômes…, Touches…) with their caption, the way back and the hints.
struct LevelSelectLayout {
    float unit = 1.0f;
    Rect title, subtitle;
    Rect list;
    float cardH = 66.0f, cardGap = 8.0f;  // screen pixels
    Rect card;          // the level's details (holographic)
    Rect play;
    std::vector<Vec2> diamonds;  // Fantômes…, Touches…
    float diamondHalf = 22.0f;
    Rect caption;       // the focused diamond's name
    Vec2 back;
    Rect backCaption, hints;
    static LevelSelectLayout compute(float screenW, float screenH);
    Rect playExtent() const { return {play.x - 8 * unit, play.y - 8 * unit, play.w + 32 * unit, play.h + 16 * unit}; }
};

/// Fin de niveau: the title, the level's name, the line under it; « Revoir la partie » and
/// « Segments » (diamonds, their caption), « Classement en ligne » and the players' rating; the
/// card of the run (the time counting up, the statistics) and the card of the medal (« NOUVEAU
/// RECORD », Nova's victory, the medal, its stars filling, the goals); the race / bosses lines,
/// the online status or the note, the plates of the actions.
struct ResultsLayout {
    float unit = 1.0f;
    Rect title, levelName, subLine;
    std::vector<Vec2> diamonds;  // Revoir la partie, Segments
    float diamondHalf = 22.0f;
    Rect caption;
    Rect online, rating;
    Rect stats, statsTime, statsRows;
    float rowH = 34.0f;  // screen pixels, seven rows
    Rect medalCard, record, nova, medal, medalName, stars, goalsTitle, goals;
    float goalH = 28.0f;
    Rect info, status, buttons;
    float buttonGap = 16.0f;
    static ResultsLayout compute(float screenW, float screenH);
};

}  // namespace cosmic
