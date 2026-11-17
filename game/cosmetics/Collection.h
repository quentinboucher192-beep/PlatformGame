#pragma once

// Collection (2.19): cosmetic rewards unlocked by the achievements' stars (2.35: or by the
// challenge results Cosmic Online verified), and what the player has discovered (tracks heard,
// creatures met). Display only: nothing here ever changes a run, a replay or a record.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/cosmetics/CreationLooks.h"
#include "game/cosmetics/SuitRecolor.h"

namespace cosmic {

class AchievementBook;

struct CosmeticCategory {
    std::string id;    // suit, trail, frame, title, victory
    std::string name;  // « Combinaisons »...
};

/// 2.35: a reward of the challenges, unlocked by results that Cosmic Online verified (never by
/// a run kept on this computer only): `count` challenges of `kind` (daily, weekly, monthly or
/// any) with a verified run at `tier` or better, received in time or late (« challenge »), or
/// `count` periods of `kind` in a row with a verified run received in time (« challengeStreak »).
struct ChallengeUnlock {
    bool streak = false;
    std::string kind;  // "daily", "weekly", "monthly", "any" (results only); "": not a challenge reward
    int tier = 0;      // results: 1 bronze, 2 silver, 3 gold (that tier or better); a streak: any (1)
    int count = 0;
};

struct CosmeticItem {
    std::string id;
    std::string category;
    std::string name;
    std::string description;
    /// What the game draws: suit sheet (« givre »), trail / frame / victory style, title text.
    std::string look;
    int stars = 0;             // star milestone (0: from the start)
    bool allStars = false;     // every star of the catalogue (« Constellation »)
    std::string achievement;   // unlocked by this achievement instead of stars
    ChallengeUnlock challenge; // 2.35: or by verified challenge results
    bool isDefault = false;    // the category's starting item
    /// 2.43: a title created on Cosmic Online by an administrator (id "srv:<n>", `look` its
    /// text): its colour, "gold", "silver", "cyan" or "violet"; "" for the catalogue's items.
    std::string color;
    /// A suit created on Cosmic Online by an administrator (ADMINISTRATION › Créations: id and
    /// look "srvsuit:<n>"): the three colours Nova's sheet is recoloured with at run time.
    std::optional<SuitColors> suitColors;
    /// 2.59: a trail, an avatar frame or a victory effect created there too (id and look
    /// "srvtrail:<n>", "srvframe:<n>", "srvvictory:<n>"): its colours and style.
    std::optional<CreationLook> creationLook;
    bool challengeReward() const { return !challenge.kind.empty(); }
    bool serverTitle() const { return id.rfind("srv:", 0) == 0; }
    bool serverSuit() const { return id.rfind("srvsuit:", 0) == 0; }
    bool serverLook() const { return isServerLookRef(id); }
    /// Created on Cosmic Online (a title, a suit, a trail, a frame, a victory effect): never in the catalogue.
    bool serverMade() const { return serverTitle() || serverSuit() || serverLook(); }
};

/// 2.43: a reward won at one challenge, granted by Cosmic Online once the challenge was over
/// (GET /players/me/rewards, kept on this computer for offline play).
struct WonReward {
    std::string challengeId;   // "daily-2026-11-20"
    std::string challengeKind; // "daily", "weekly", "monthly"
    std::string period;        // "2026-11-20", "2026-W47", "2026-11"
    std::string date;          // its first day
    std::string kind;          // "item" (of the catalogue), "title", "suit", "trail", "frame" or "victory" (created on the server)
    std::string ref;           // the item's id, "srv:<n>", "srvsuit:<n>", "srvtrail:<n>", "srvframe:<n>" or "srvvictory:<n>"
    std::string grantedAt;
    /// 2.60: the rule of that challenge it was won by ("tier" value 1..3, "top" value N,
    /// "finished"; "" when the server did not say), for the tooltips of the exclusive items.
    std::string condition;
    int value = 0;
};

/// 2.43: what the player won at single challenges: catalogue items (unlocked whatever their
/// usual condition) and the server's titles, as items of the category « title »; and the suits
/// created on the server (ADMINISTRATION › Créations), as items of the category « suit »; 2.59,
/// the trails, frames and victory effects created there, as items of their category.
struct WonRewards {
    bool known = false;               // read from the server (else: nothing won here)
    std::vector<WonReward> list;      // the most recent first
    std::vector<CosmeticItem> titles; // the server titles won, once each
    std::vector<CosmeticItem> suits;  // the server suits won, once each (with their colours)
    std::vector<CosmeticItem> looks;  // 2.59: the server trails, frames, victory effects won, once each (with their look)
    bool has(const std::string& ref) const;
    bool wonAt(const std::string& challengeId, const std::string& ref) const;
    const CosmeticItem* title(const std::string& ref) const;
    const CosmeticItem* suit(const std::string& ref) const;
    const CosmeticItem* serverLook(const std::string& ref) const;
    /// A server title, suit, trail, frame or victory effect won (nullptr for anything else).
    const CosmeticItem* item(const std::string& ref) const;
    /// The first challenge (the oldest) where `ref` was won, nullptr when never.
    const WonReward* origin(const std::string& ref) const;
    /// `rewards` of GET /players/me/rewards; nullopt when malformed.
    static std::optional<WonRewards> fromJson(const Json& rewards);
    Json toJson() const;  // the same form (the cache)
    bool operator==(const WonRewards& o) const { return known == o.known && toJson() == o.toJson(); }
    bool operator!=(const WonRewards& o) const { return !(*this == o); }
};

/// What Cosmic Online verified of the player's challenge runs (GET /challenges/me, kept on this
/// computer for offline play): per kind (daily, weekly, monthly), the challenges with a
/// verified run at each tier or better and the longest series of periods in time.
struct ChallengeRewardProgress {
    bool known = false;               // read from the server (else: nothing counts)
    int atLeast[3][3] = {};           // [kind][tier - 1]
    int bestStreak[3] = {};           // [kind]
    /// Challenges of `kind` ("any": every kind) with a verified run at `tier` or better.
    int results(const std::string& kind, int tier) const;
    int streak(const std::string& kind) const;
    bool operator==(const ChallengeRewardProgress& o) const;
    bool operator!=(const ChallengeRewardProgress& o) const { return !(*this == o); }
};

/// data/cosmetics/cosmetics.json
class CosmeticCatalog {
public:
    bool load(const fs::path& file, std::string* error = nullptr);
    bool loadDefault(std::string* error = nullptr) { return load(Paths::dataDir() / "cosmetics" / "cosmetics.json", error); }
    const std::vector<CosmeticCategory>& categories() const { return categories_; }
    const std::vector<CosmeticItem>& items() const { return items_; }
    std::vector<const CosmeticItem*> items(const std::string& category) const;
    const CosmeticItem* find(const std::string& id) const;
    const CosmeticItem* defaultItem(const std::string& category) const;
    const CosmeticCategory* category(const std::string& id) const;
    /// `look` when an item of the category has it, else the category default's look: what
    /// another player's data (leaderboards, ghosts) may make this game draw (2.23). A suit
    /// created on the server ("srvsuit:<n>") passes as it is: drawn from its colours; 2.59, a
    /// trail, a frame or a victory effect created there too, in its own category only.
    std::string lookOr(const std::string& category, const std::string& look) const;
    /// Star milestones in order, each with its rewards (the route of the stars).
    struct Milestone {
        int stars = 0;
        std::vector<const CosmeticItem*> items;
    };
    std::vector<Milestone> milestones(int totalStars) const;

private:
    std::vector<CosmeticCategory> categories_;
    std::vector<CosmeticItem> items_;
};

/// What unlocks the rewards right now.
struct CosmeticProgress {
    int stars = 0;
    int totalStars = 0;
    const AchievementBook* book = nullptr;
    const ChallengeRewardProgress* challenges = nullptr;  // 2.35: nullptr / unknown: locked
    const WonRewards* won = nullptr;                      // 2.43: rewards won at single challenges
};

/// Stars needed for an item (its milestone; every star for « all »), -1 when an
/// achievement (or, 2.35, challenge results) unlocks it instead.
int cosmeticThreshold(const CosmeticItem& item, int totalStars);
bool cosmeticUnlocked(const CosmeticItem& item, const CosmeticProgress& p);
/// « Combinaison », « Traînée », « Cadre d'avatar », « Titre », « Effet de victoire ».
std::string cosmeticCategorySingular(const std::string& category);
/// A reward of the challenges: how far the verified results go ({have, need}, `have` never
/// above `need`); {0, 0} for any other item.
std::pair<int, int> cosmeticChallengeProgress(const CosmeticItem& item, const CosmeticProgress& p);
/// The rewards of the challenges a run of a challenge of `kind` ("daily", "weekly",
/// "monthly") can advance, in catalogue order: those of its kind or of any kind; the series
/// only while the challenge runs (`late` false: a run « hors délai » never counts for one).
std::vector<const CosmeticItem*> challengeRewardsFor(const CosmeticCatalog& catalog, const std::string& kind, bool late);

/// saves/collection.json: equipped rewards, rewards already announced, the jukebox and the
/// gallery's discoveries.
class CollectionBook {
public:
    static constexpr int kVersion = 1;
    explicit CollectionBook(fs::path file = {}) : file_(std::move(file)) {}
    void load();
    bool save() const;
    /// No collection file yet (first start of 2.19, or a new player).
    bool firstStart() const { return !existed_; }

    /// Equipped item of a category, or its default when the saved one is unknown or locked
    /// (2.43: a title may be a server title won, `p.won`).
    const CosmeticItem* equipped(const CosmeticCatalog& catalog, const std::string& category, const CosmeticProgress& p) const;
    std::string equippedId(const std::string& category) const;
    void equip(const std::string& category, const std::string& id);
    /// Look of the equipped item (« givre », « comet »…, a title's text).
    std::string look(const CosmeticCatalog& catalog, const std::string& category, const CosmeticProgress& p) const;
    /// Category → id of the item worn (the default when the saved one is unknown or locked),
    /// for every category of the catalogue: what the online profile receives (2.23).
    std::map<std::string, std::string> equippedIds(const CosmeticCatalog& catalog, const CosmeticProgress& p) const;

    bool announced(const std::string& id) const { return announced_.count(id) > 0; }
    void markAnnounced(const std::string& id) { announced_.insert(id); }
    /// Unlocked, not default, never announced (the end-of-level card), in catalogue order (2.43:
    /// the server titles won are announced by a toast and a notification instead).
    std::vector<const CosmeticItem*> fresh(const CosmeticCatalog& catalog, const CosmeticProgress& p) const;

    // Jukebox
    std::string menuMusic;               // asset path chosen for the main menu ("" = theme)
    std::set<std::string> heard;         // asset paths of the tracks already heard in the game
    // Gallery
    std::set<std::string> met;           // enemy ids met in a level
    int exported = 0;                    // planet pictures saved (the counter of the gallery)
    // Profile badges (2.41): achievement ids chosen for the online profile, in their order
    // (shownBadges() keeps those still unlocked, as many as the stars open). Kept here even
    // without an account: published once the player is connected.
    std::vector<std::string> badges;
    /// The player chose badges on this computer (even « none »): only then is the choice sent,
    /// so a computer where nothing was chosen never erases the badges chosen on another one.
    bool badgesChosen = false;

    Json toJson() const;
    void fromJson(const Json& j);

private:
    fs::path file_;
    bool existed_ = false;
    std::map<std::string, std::string> equipped_;
    std::set<std::string> announced_;
};

}  // namespace cosmic
