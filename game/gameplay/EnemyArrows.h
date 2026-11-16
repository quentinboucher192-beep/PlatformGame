#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

/// 2.52: arrows on the border of the screen towards the enemies already met that are out of
/// view, one per pack (with « ×N » when it holds several). Display only: the simulation never
/// sees them (GameScene draws them; Paramètres › Jeu turns them off). The boss has its own
/// arrow (GameScene::drawBossArrow).
namespace EnemyArrows {

/// Where the arrows may go and their sizes (screen pixels; the defaults are the game's at 1080p).
struct Layout {
    Rect screen;                   // the whole screen
    float inset = 41.0f;           // from the screen's border to the centre of an arrow
    float arrowSize = 54.0f;       // side of an arrow's square
    Vec2 badgeSize{56.0f, 32.0f};  // the widest count (« ×N »), next to its arrow on the inner side
    float gap = 6.0f;              // kept free around an arrow and its count
    /// Along the border: enemies whose directions (from the screen's centre) meet it closer
    /// than `packGap` form one pack, a pack covering at most `packSpan` of it.
    float packGap = 120.0f;
    float packSpan = 360.0f;
    std::vector<Rect> keepOut;     // HUD elements, the boss arrow…: never under an arrow nor a count
};

struct Arrow {
    Vec2 pos;            // centre of the arrow, `inset` inside the border
    float angle = 0.0f;  // radians (0: right, y down), from the arrow towards the pack's centre
    int count = 0;       // enemies of the pack
    Vec2 target;         // centre of the pack (barycentre of its enemies), screen pixels
    Rect box;            // the arrow's square
    Rect badge;          // where its count is written (empty for a single enemy)
    Rect area;           // all it covers (arrow, count and the gap around them)
};

/// The arrows towards the enemies whose boxes (screen pixels) are `enemies`. None for an enemy
/// on screen, even partly. The others form packs by where their direction from the screen's
/// centre meets the border; one arrow per pack, on that border where the line towards the
/// pack's barycentre meets it, turned towards that barycentre. An arrow (with its count) over
/// a keep-out area slides along the border to the nearest free place (none: not shown); two
/// arrows that would touch become one, for both packs. Sorted clockwise from the top left.
std::vector<Arrow> layout(const Layout& l, const std::vector<Rect>& enemies);

/// The enemies already met during this attempt: seen on screen (alive, awake) at least once.
/// A dead enemy, or one no longer in the level, is forgotten: back, it has to be seen again.
class Tracker {
public:
    struct Enemy {
        uint32_t uid = 0;
        Rect box;           // screen pixels
        bool shown = true;  // alive, awake and not a boss (the boss has its own arrow)
    };
    /// Notes the enemies on screen now; returns the boxes of those met that are out of it.
    std::vector<Rect> update(const Rect& screen, const std::vector<Enemy>& enemies);
    bool met(uint32_t uid) const { return met_.count(uid) > 0; }
    size_t size() const { return met_.size(); }
    void reset() { met_.clear(); }

private:
    std::unordered_set<uint32_t> met_;
};

}  // namespace EnemyArrows

}  // namespace cosmic
