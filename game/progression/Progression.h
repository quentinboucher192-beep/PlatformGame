#pragma once

#include <map>
#include <set>
#include <string>

#include "engine/core/Json.h"

namespace cosmic {

class ContentDatabase;
struct PlanetData;
struct LevelEntry;

/// "0 min", "45 min", "3 h 05 min": time played, in hours and minutes.
std::string playTimeText(double seconds);

/// Campaign progression: completed levels, unlocked planets, last played content
/// (saves/progress.json, versioned).
class Progression {
public:
    static constexpr int kVersion = 1;

    void load();
    bool save() const;

    bool levelCompleted(const std::string& levelId) const { return completed_.count(levelId) > 0; }
    void markCompleted(const std::string& levelId) { completed_.insert(levelId); }
    bool planetUnlocked(const PlanetData& p, const ContentDatabase& content) const;
    bool planetCompleted(const PlanetData& p, const ContentDatabase& content) const;
    bool levelUnlocked(const LevelEntry& level, const ContentDatabase& content) const;
    int completedCount() const { return static_cast<int>(completed_.size()); }

    std::string lastPlanet = "sylvanox";
    std::string lastLevel;
    double playTimeSeconds = 0.0;
    /// Time played in one level (seconds, every session, finished or not).
    double levelPlayTime(const std::string& levelId) const {
        auto it = levelPlayTime_.find(levelId);
        return it == levelPlayTime_.end() ? 0.0 : it->second;
    }
    /// Adds a session to the total and to the level.
    void addPlayTime(const std::string& levelId, double seconds);
    bool introSeen = false;
    bool unlockAll = false;  // from the settings: every level is playable (testing)

    Json toJson() const;
    void fromJson(const Json& j);
    void resetAll() { completed_.clear(); }

private:
    std::set<std::string> completed_;
    std::map<std::string, double> levelPlayTime_;
};

}  // namespace cosmic
