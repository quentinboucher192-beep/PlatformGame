#pragma once

// Rewards of the challenges (2.34) as the screens show them: never a list of words, but each
// reward's own preview (the suit, a sample of the trail, the frame's ring, the title's chip,
// the victory effect) in a tile with the mark of its tier (Or / Argent / Bronze) and
// « obtenu », and a tooltip on the tile under the mouse or the keyboard / gamepad focus: its
// name, its category, its condition, my progress (« 3 / 10 »), « Obtenu ». A tile opens the
// reward in Collection → Vestiaire. The texts of the conditions are shared with the Collection.

#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
struct CosmeticItem;
struct CosmeticProgress;

/// The condition of a reward of the challenges, whole: « Réussir un défi de la semaine en Or. »
std::string challengeConditionText(const CosmeticItem& it);
/// What counts for it: « À l'heure ou hors délai… » / « Une partie hors délai ne compte pas… ».
std::string challengeConditionNote(const CosmeticItem& it);
/// Short (the cards of the Collection): « 1 défi en Or », « Semaine en Or », « 7 jours de suite ».
std::string challengeConditionShort(const CosmeticItem& it);
/// « 3 / 10 », and what it counts: « Progression » or « Meilleure série ».
std::string challengeProgressText(const CosmeticItem& it, const CosmeticProgress& p);
/// 1 bronze, 2 silver, 3 gold: the tier the reward asks for (a series: any finished run, 1).
int challengeRewardTier(const CosmeticItem& it);
Color challengeTierColor(int tier);

/// The preview of a cosmetic as the Collection draws it, inside `box` (`unlocked` false: dimmed).
void drawCosmeticPreview(GameServices& g, const CosmeticItem& it, const Rect& box, float time, bool unlocked);

/// A row of reward tiles (focusable) and the tooltip of the one hovered or focused.
class ChallengeRewardRow {
public:
    /// Width of the tile of `it` at height `h`: a square, a title as wide as its chip.
    static float tileWidth(GameServices& g, const CosmeticItem& it, float h);
    /// The row's width (gaps included).
    static float width(GameServices& g, const std::vector<const CosmeticItem*>& items, float h);
    /// Tiles from `x`, top `y`, `h` high, as many as fit in `maxW` (the others: not drawn).
    /// Returns the reward activated (click, Entrée, button A), nullptr when none. `feature`: a
    /// novelty (« NOUVEAU ») seen once a tile gets the focus; newDue() says whether the caller
    /// still has its pill to draw.
    const CosmeticItem* draw(GameServices& g, const std::string& idPrefix, const std::vector<const CosmeticItem*>& items, float x, float y,
                             float h, float maxW, const std::string& feature = {});
    bool newDue() const { return newDue_; }
    /// The tooltip of the tile hovered (mouse) or focused (keyboard, gamepad), over the rest:
    /// to call once everything else of the screen is drawn.
    void drawTooltip(GameServices& g);
    bool showingTooltip() const { return tip_ != nullptr; }

private:
    const CosmeticItem* tip_ = nullptr;
    Rect tipTile_;
    bool newDue_ = false;
};

}  // namespace cosmic
