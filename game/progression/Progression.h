#pragma once

#include <map>
#include <set>
#include <string>

#include "engine/core/Json.h"

namespace cosmic {

class ContentDatabase;
struct PlanetData;
struct LevelEntry;
struct RunResult;

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
    /// A finished run completes its level, never a training run (RunResult::training).
    /// Returns true when the level was not completed before.
    bool noteRun(const RunResult& run);
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
    /// 2.64: the tutorial of the first launch (« Le premier envol de Nova »): "" not seen yet (it is
    /// due, whatever the progression: 2.62.2), "started", "skipped" or "finished" (seen). Never
    /// played twice, whatever its end.
    std::string tutorial;
    bool tutorialFinished() const { return tutorial == "finished"; }
    /// "finished" > "skipped" > "started" > "" (not seen): the furthest of two (copies, migration).
    static std::string furthestTutorial(const std::string& a, const std::string& b);
    /// 2.68: Asterion's film (ui/screens/AsterionFilm) played to its end: its first viewing (which
    /// nothing skips) is not due any more. Written only at its end; never lost by a copy or a
    /// migration (ProgressMerge: either side's).
    bool asterionFilmSeen = false;
    bool unlockAll = false;  // from the settings: every level is playable (testing)

    Json toJson() const;
    void fromJson(const Json& j);
    void resetAll() { completed_.clear(); }

private:
    std::set<std::string> completed_;
    std::map<std::string, double> levelPlayTime_;
};

}  // namespace cosmic
