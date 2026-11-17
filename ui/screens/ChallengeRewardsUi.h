#pragma once

// Rewards of the challenges (2.35) as the screens show them: never a list of words, but each
// reward's own preview (the suit, a sample of the trail, the frame's ring, the title's chip,
// the victory effect) in a tile with the mark of its tier (Or / Argent / Bronze) and
// « obtenu », and a tooltip on the tile under the mouse or the keyboard / gamepad focus: its
// name, its category, its condition, my progress (« 3 / 10 »), « Obtenu ». A tile opens the
// reward in Collection → Vestiaire. The texts of the conditions are shared with the Collection.
// 2.43: the rewards of one challenge (an administrator's: an item of the catalogue or a title
// created on Cosmic Online, each with its condition on that challenge) come first in the row,
// with the mark of their condition and a tooltip of their own.
// 2.60: an « exclusive » item (only obtained as the reward of a challenge: every creation of
// Cosmic Online, and the catalogue's rewards of the challenges) wears an aura around its
// thumbnail wherever it is drawn as an item, and its tooltip says where it comes from:
// « Exclusif : défi du jour du 10/11/2026 · Or atteint » (what the server granted, kept on this
// computer), « Exclusif aux défis » when nothing more is known.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/DailyChallenges.h"

namespace cosmic {

class GameServices;
class UIContext;
struct CosmeticItem;
struct CosmeticProgress;

/// 2.43: a title's colour ("gold", "silver", "cyan", "violet"; anything else: gold).
Color titleColor(const std::string& color);
/// « Or atteint », « Top 10 final », « Niveau fini ».
std::string prizeConditionShort(const ChallengePrize& p);
/// The whole condition: « Atteindre le palier Or sur ce défi. »…
std::string prizeConditionText(const ChallengePrize& p);
/// Where a reward was won: « Défi du jour du 20/11 », « Défi de la semaine 47 », « Défi du mois de novembre 2026 ».
std::string wonOriginText(const WonReward& w);
/// What a reward of a challenge looks like, as an item: the catalogue's (copied) or the server
/// title's; nullopt for an item this game does not know (a newer one).
std::optional<CosmeticItem> prizeLook(GameServices& g, const ChallengePrize& p);

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

// ------------------------------------------------------------------ exclusive items (2.60)

/// Only obtained as the reward of a challenge: every creation of Cosmic Online (a suit, a trail,
/// a frame, a victory effect, a title) and the catalogue's items unlocked by challenge results.
bool exclusiveItem(const CosmeticItem& it);
/// The colour of the aura and of the tooltips of the exclusive items (a soft gold).
Color exclusiveColor();
/// The aura of an exclusive item: a soft glow that pulses around `box` (its thumbnail, corner
/// `radius`), never beyond `spread` from its edge (the caller keeps that margin clear: no text,
/// no neighbour in it). Display only.
void drawExclusiveAura(UIContext& ui, const Rect& box, float radius, float spread);
/// « défi du jour du 10/11/2026 », « défi de la semaine du 09/11/2026 », « défi du mois de
/// novembre 2026 » (`kind` "daily", "weekly", "monthly"; `date` its first day); "" when unreadable.
std::string challengeOriginText(const std::string& kind, const std::string& period, const std::string& date);
/// « Or atteint », « Top 10 final », « Niveau fini » ("" for an unknown condition).
std::string conditionShort(const std::string& condition, int value);
/// « Exclusif : défi du jour du 10/11/2026 · Or atteint » (the condition when known); « Exclusif
/// aux défis » when the challenge is not readable.
std::string exclusiveOriginText(const std::string& kind, const std::string& period, const std::string& date, const std::string& condition = {},
                                int value = 0);
/// Where an exclusive item comes from, as this game knows it: the challenge where I won it (GET
/// /players/me/rewards, kept for offline play), else a catalogue reward's condition (« Exclusif
/// aux défis : Réussir… »), else « Exclusif aux défis ».
std::string exclusiveText(GameServices& g, const CosmeticItem& it);
/// An item of the reserve never given yet (seen only by an administrator).
std::string reserveText();
/// The administration's tooltip of a creation of GET /admin/creations: the first challenge that
/// gives it (`given_by`), « Exclusif aux défis » (won, its challenge unknown), else the reserve
/// (`reserve` set: never given).
std::string adminCreationTip(const Json& creation, bool* reserve = nullptr);

/// The tooltip of an exclusive item: its name, then where it comes from, by the tile hovered
/// (mouse) or focused (keyboard, gamepad). offer() while drawing the tiles, draw() once the
/// rest of the screen is drawn (over everything, always inside the screen, its text measured).
class ExclusiveTip {
public:
    void clear() { text_.clear(); }
    /// `tile` shows its tooltip this frame when it is hovered (with the mouse) or focused (otherwise).
    void offer(UIContext& ui, const Rect& tile, bool hovered, bool focused, const std::string& name, const std::string& text, bool reserve = false);
    bool active() const { return !text_.empty(); }
    void draw(GameServices& g);

private:
    Rect tile_;
    std::string name_, text_;
    bool reserve_ = false;
};

/// A row of reward tiles (focusable) and the tooltip of the one hovered or focused.
class ChallengeRewardRow {
public:
    /// 2.43: the rewards of the challenge `c` (nullptr: none), drawn first by draw(); to call
    /// each frame before it.
    void setPrizes(GameServices& g, const DailyChallenge* c);
    bool hasPrizes() const { return !prizes_.empty(); }
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
    bool showingTooltip() const { return tip_ != nullptr || tipPrize_ >= 0; }

private:
    struct Prize {
        CosmeticItem item;
        ChallengePrize prize;
        bool won = false;
        std::string exclusive;  // 2.60: « Exclusif : défi du jour du… » ("" for an item that is not exclusive)
    };
    void drawPrizeTooltip(GameServices& g, const Prize& pz);

    const CosmeticItem* tip_ = nullptr;
    int tipPrize_ = -1;
    Rect tipTile_;
    bool newDue_ = false;
    std::vector<Prize> prizes_;
    std::string prizeChallenge_;
    int64_t prizeEndsMs_ = 0;
};

}  // namespace cosmic
