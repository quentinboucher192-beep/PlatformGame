#pragma once

// 2.76.0: « Où frapper » by parts (docs/HITPARTS.md): a boss's real sprite, the clip and frame chosen,
// its hit parts over it (game/enemies/HitParts: assets/hitparts, the roles of data/enemies « parts »),
// each filled with the colour of its role, its box as a thin frame. Used by the sheets of the classic
// bosses and of the gigaboss of the campaign (BossStatsScene, tab « Où frapper ») and by the sheets of
// Asterion's gigaboss (RaidSheetsScene, « Mécanismes » › « Où frapper »).

#include <functional>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/enemies/HitParts.h"

namespace cosmic {

class GameServices;
class Renderer;
struct EnemyDefinition;

/// The colour of a role: green a weak point, blue a normal part, grey armour, red what hurts.
Color partRoleColor(PartRole r);
/// The colour of a part of `rules` (its role).
Color partColor(const PartRules& rules, const std::string& name);
/// Draws the pieces of one cell, `toScreen` placing a vertex of the frame on the screen.
void drawHitPieces(Renderer& r, float lineWidth, const HitPartsSheet& sheet, const std::vector<HitPartPiece>& pieces, const PartRules& rules,
                   const std::function<Vec2(Vec2)>& toScreen, uint8_t fill = 70);
/// « Repos », « Élan », « Attaque »… the name of a clip on a chip (its id when it has none).
std::string poseLabel(const std::string& clip);
/// The clips of a sheet worth a chip, in the order of a fight (repos, déplacement, élan, attaque…).
std::vector<std::string> poseClips(const std::vector<std::string>& clips);

/// The « Où frapper » panel of a boss's sheet: a chip per phase whose sheet differs, a chip per pose,
/// its sprite animated with its parts and its box, the legend at its right (each part: its role and its
/// multiplier, measured, never past its box), how its fight uses them.
class HitPartsView {
public:
    /// Fills `area`. `dt`: the time since the last frame (the animation).
    void draw(GameServices& g, const Rect& area, const EnemyDefinition& d, float dt);
    /// For the captures (--boss-stats <boss>:frapper[=<clip>]): that pose.
    void choosePose(const std::string& clip) { wanted_ = clip; }

private:
    std::string of_, wanted_;
    int phase_ = 0, pose_ = 0;
    float t_ = 0.0f;
};

}  // namespace cosmic
