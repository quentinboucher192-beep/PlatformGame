#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ContentDatabase;

/// Difficulty of a level, out of 5, computed from its content instead of being typed in by
/// its author. The intended route (start -> every checkpoint in order -> exit, from the
/// LevelAnalyzer) is walked and what the player meets along it is measured:
///
///  * jumps: every airborne stretch of the route, compared with the player's single jump
///    (span and height); a jump near or beyond the single jump's limits counts more, and
///    much more when a miss is fatal (lava, acid, spikes, bottomless pit);
///  * hazards next to the route: spikes, lasers (by their duty cycle), deadly liquids,
///    damage zones, crumbling and moving platforms, slippery ice;
///  * enemies near the route, by their health, damage, attack speed and kind (ranged,
///    flying, teleporting...);
///  * the boss: health, damage per second of its attacks, telegraph times, phases;
///  * the distance between checkpoints (what a mistake costs);
///  * healing (health orbs, heart containers) lowers the score; a tight time limit raises it.
///
/// Hazards, jumps and enemies are densities per 100 tiles of route, so a long easy level does
/// not score like a short hard one. The weighted sum goes through a saturating curve
/// (5 (1 - exp(-(raw - 6) / 8))) and is rounded to half a star. The formula is versioned (kRatingVersion): a stored rating made
/// with another version is recomputed.
struct DifficultyRating {
    static constexpr int kRatingVersion = 2;

    float score = 0.0f;  // 0.5 .. 5, by half stars
    float raw = 0.0f;    // weighted sum before the curve
    bool reachable = true;

    float routeTiles = 0.0f;
    // Components (already weighted, they add up to `raw`).
    float jumps = 0.0f;
    float hazards = 0.0f;
    float enemies = 0.0f;
    float boss = 0.0f;
    float checkpoints = 0.0f;
    float healing = 0.0f;  // negative
    float timer = 0.0f;
    float length = 0.0f;

    // Facts behind the components (for the editor and the documentation).
    int preciseJumps = 0;   // jumps beyond 75 % of the full jump (double jump included)
    int deadlyJumps = 0;    // ... of which a miss is fatal
    int hazardCount = 0;    // hazards next to the route
    int enemyCount = 0;     // enemies next to the route (boss excluded)
    float longestCheckpointGap = 0.0f;  // tiles of route
    float healPer100 = 0.0f;            // health points available per 100 tiles

    Json toJson() const;
    static DifficultyRating fromJson(const Json& j);
    /// "3,5/5"
    std::string label() const;
    /// Short French explanation of the main factors ("sauts précis au-dessus du vide, ...").
    std::string summary() const;
};

DifficultyRating rateDifficulty(const LevelData& level, const ContentDatabase& content, const LevelAnalysis& analysis);
DifficultyRating rateDifficulty(const LevelData& level, const ContentDatabase& content);

/// Rounded stars for the places that show whole stars.
int ratingStars(float score);

}  // namespace cosmic
