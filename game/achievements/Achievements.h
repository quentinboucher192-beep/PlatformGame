#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;
class Progression;
class RecordsManager;
class BossStats;

/// A family of achievements (a sector of the web).
struct AchievementCategory {
    std::string id;
    std::string name;
    std::string description;
    Color color{57, 240, 216, 255};
};

/// What has to be done (data/achievements/achievements.json, "condition").
struct AchievementCondition {
    std::string type;     // levels_completed, planet, medal_count, stat, boss, power_uses...
    std::string key;      // stat / set / max name
    std::string level, planet, boss, power, variant;
    std::vector<std::string> planets;
    int64_t count = 1;
    int medal = 0;
    double seconds = 0.0;
    bool noHit = false;  // "boss": beaten without being hit (2.19)
};

struct AchievementDef {
    std::string id;
    std::string category;
    std::string name;
    std::string description;
    int stars = 1;  // difficulty, 1 to 5
    AchievementCondition condition;
    std::vector<std::string> after;  // achievements before this one (threads of the web)
};

/// Every achievement of the game (read from data/achievements/achievements.json).
class AchievementCatalog {
public:
    bool load(const fs::path& file, std::string* error = nullptr);
    bool loadDefault(std::string* error = nullptr) { return load(Paths::dataDir() / "achievements" / "achievements.json", error); }
    const std::vector<AchievementCategory>& categories() const { return categories_; }
    const std::vector<AchievementDef>& achievements() const { return achievements_; }
    const AchievementDef* find(const std::string& id) const;
    const AchievementCategory* category(const std::string& id) const;
    int totalStars() const;

private:
    std::vector<AchievementCategory> categories_;
    std::vector<AchievementDef> achievements_;
    std::map<std::string, size_t> index_;
};

/// Lifetime counters kept for the achievements (local only, never sent).
struct AchievementStats {
    std::map<std::string, int64_t> counters;               // kills, jumps, deaths, power_uses...
    std::map<std::string, int64_t> maxima;                 // run_kills...
    std::map<std::string, std::set<std::string>> sets;     // cleared_levels, community_levels...
    std::map<std::string, int64_t> powerUses;              // per power id

    void add(const std::string& key, int64_t n = 1);
    void raise(const std::string& key, int64_t value);
    void insert(const std::string& set, const std::string& value);
    int64_t counter(const std::string& key) const;
    int64_t maximum(const std::string& key) const;
    size_t setSize(const std::string& key) const;
    bool setHas(const std::string& key, const std::string& value) const;
};

/// What the conditions read: the game's own saves (progression, records, bosses) and the
/// counters above. Everything is optional (tests build only what they need).
struct AchievementInputs {
    const ContentDatabase* content = nullptr;
    const Progression* progression = nullptr;
    const RecordsManager* records = nullptr;
    const BossStats* bosses = nullptr;
    const AchievementStats* stats = nullptr;
    double playTimeSeconds = 0.0;
    int userLevels = 0;
    int userEnemies = 0;
    int completionProofs = 0;
    int communityLevelsFinished = 0;  // from the records of installed community levels
    bool onlineAccount = false;
    int friends = 0;
    std::vector<std::string> campaignLevels;  // the 14 levels of the campaign
};

struct AchievementProgress {
    int64_t value = 0;
    int64_t target = 1;
    bool done() const { return value >= target; }
};

/// Unlocked achievements and counters (saves/achievements.json).
class AchievementBook {
public:
    static constexpr int kVersion = 1;
    explicit AchievementBook(fs::path file = {}) : file_(std::move(file)) {}
    void load();
    bool save() const;

    AchievementStats& stats() { return stats_; }
    const AchievementStats& stats() const { return stats_; }
    bool unlocked(const std::string& id) const { return unlocked_.count(id) > 0; }
    /// Date of the unlock ("2026-11-14 21:39"), empty if locked.
    std::string unlockDate(const std::string& id) const;
    int unlockedCount() const { return static_cast<int>(unlocked_.size()); }
    int stars(const AchievementCatalog& catalog) const;
    int stars(const AchievementCatalog& catalog, const std::string& category) const;

    /// Where the condition stands now (value / target).
    static AchievementProgress progress(const AchievementDef& def, const AchievementInputs& in);
    /// Every achievement it requires is unlocked.
    bool available(const AchievementDef& def) const;
    /// Unlocks what is now done (and whose requirements are unlocked), in order; returns the
    /// new ones. `date`: when (FileUtil::nowString()).
    std::vector<const AchievementDef*> evaluate(const AchievementCatalog& catalog, const AchievementInputs& in, const std::string& date);
    /// New unlocks not shown yet (toasts, results screen), oldest first.
    std::vector<std::string> takeAnnouncements();

    Json toJson() const;
    void fromJson(const Json& j);

private:
    fs::path file_;
    std::map<std::string, std::string> unlocked_;  // id -> date
    std::vector<std::string> announce_;
    AchievementStats stats_;
};

}  // namespace cosmic
