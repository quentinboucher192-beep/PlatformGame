#pragma once

// Collection (2.19): cosmetic rewards unlocked by the achievements' stars, and what the
// player has discovered (tracks heard, creatures met). Display only: nothing here ever
// changes a run, a replay or a record.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

class AchievementBook;

struct CosmeticCategory {
    std::string id;    // suit, trail, frame, title, victory
    std::string name;  // « Combinaisons »...
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
    bool isDefault = false;    // the category's starting item
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
};

/// Stars needed for an item (its milestone; every star for « all »), -1 when an
/// achievement unlocks it instead.
int cosmeticThreshold(const CosmeticItem& item, int totalStars);
bool cosmeticUnlocked(const CosmeticItem& item, const CosmeticProgress& p);

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

    /// Equipped item of a category, or its default when the saved one is unknown or locked.
    const CosmeticItem* equipped(const CosmeticCatalog& catalog, const std::string& category, const CosmeticProgress& p) const;
    std::string equippedId(const std::string& category) const;
    void equip(const std::string& category, const std::string& id);
    /// Look of the equipped item (« givre », « comet »…, a title's text).
    std::string look(const CosmeticCatalog& catalog, const std::string& category, const CosmeticProgress& p) const;

    bool announced(const std::string& id) const { return announced_.count(id) > 0; }
    void markAnnounced(const std::string& id) { announced_.insert(id); }
    /// Unlocked, not default, never announced (the end-of-level card), in catalogue order.
    std::vector<const CosmeticItem*> fresh(const CosmeticCatalog& catalog, const CosmeticProgress& p) const;

    // Jukebox
    std::string menuMusic;               // asset path chosen for the main menu ("" = theme)
    std::set<std::string> heard;         // asset paths of the tracks already heard in the game
    // Gallery
    std::set<std::string> met;           // enemy ids met in a level
    int exported = 0;                    // planet pictures saved (the counter of the gallery)

    Json toJson() const;
    void fromJson(const Json& j);

private:
    fs::path file_;
    bool existed_ = false;
    std::map<std::string, std::string> equipped_;
    std::set<std::string> announced_;
};

}  // namespace cosmic
