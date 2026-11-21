#pragma once

#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

/// 2.83: where everything of the main menu goes (the 1280 x 720 layout of the mockup, scaled and
/// centred: ui/widgets/CosmicStyle CosmicLayout). Plates on the left, the icon row and its label
/// under them, the hints at the bottom left; the title, the planets' names, the holographic card
/// and the alerts (update, gifts) on the right; the runway along the bottom right.
/// tests/test_cosmic_menu.cpp checks that nothing overlaps at 1280 x 720 and 1920 x 1080.
struct MainMenuLayout {
    float unit = 1.0f;
    std::vector<Rect> plates;   // [Continuer,] Campagne, Défis, Coopération, En ligne (unfocused place)
    std::vector<Vec2> diamonds; // Notifications, Succès, Collection, Statistiques, Paramètres, [Administration,] Quitter
    float diamondHalf = 28.0f;
    Rect iconLabel;             // the focused icon's name, under the row
    Rect title;                 // « COSMIC PLATFORMER » with its orbit
    float titleCx = 0.0f, titleTop = 0.0f;
    Rect names;                 // the planets' names
    Rect card, preview, cardText;
    Rect alerts;                // the column of the update / gift alerts (stacked from its top)
    float alertH = 58.0f, alertGap = 8.0f;
    Rect chips;                 // top right: connection and version
    Rect hints;                 // bottom left: the buttons' hints and the device
    float runwayX0 = 0, runwayX1 = 0, novaX = 0, groundY = 0;
    Vec2 warpCenter;
    float veilRight = 0.0f;

    static MainMenuLayout compute(float screenW, float screenH, bool hasContinue, bool admin);
    /// What a plate covers once focused (slid right, with its glow).
    Rect plateExtent(int i) const { return {plates[i].x - 8.0f * unit, plates[i].y - 8.0f * unit, plates[i].w + 32.0f * unit, plates[i].h + 16.0f * unit}; }
    /// The width left for a plate's label (from its text column to its chevron or thumbnail).
    float labelRoom(int i, bool big) const { return plates[i].w - (big ? 78.0f + 18.0f + 142.0f : 78.0f + 18.0f + 22.0f) * unit; }
};

}  // namespace cosmic
