#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {

class Enemy;
class GameWorld;

/// Statistics of one boss (by enemy definition id), kept on this computer.
struct BossRecord {
    std::string name;
    int fights = 0;      // fights started (a fight resumed after a death counts again)
    int kills = 0;       // times beaten
    int defeats = 0;     // the player died during the fight
    int noHitKills = 0;  // beaten without being hit from its first blow to the last
    std::vector<std::string> phaseNames;
    std::vector<int> phaseClears;  // per phase: phases beaten
    std::vector<int> phaseNoHit;   // per phase: phases beaten without being hit
    float bestKill = 0.0f;         // seconds, fights won without dying (0: none yet)
    std::string lastKill;          // date of the last victory

    int phaseCount() const { return static_cast<int>(std::max(phaseNames.size(), phaseClears.size())); }
    int noHitPhases() const;
    Json toJson() const;
    static BossRecord fromJson(const Json& j);
};

/// Every boss's statistics (saves/boss_stats.json), and the giga bosses whose story the player
/// has seen (2.51, game/content/BossStories.h).
class BossStats {
public:
    static constexpr int kVersion = 1;
    void load();
    bool save();
    BossRecord& at(const std::string& bossId) { return bosses_[bossId]; }
    const BossRecord* find(const std::string& bossId) const;
    const std::map<std::string, BossRecord>& all() const { return bosses_; }
    Json toJson() const;
    void fromJson(const Json& j);
    bool dirty() const { return dirty_; }
    void touch() { dirty_ = true; }
    /// « Histoire des giga-boss »: the story of this boss was told (or skipped, which counts).
    bool storySeen(const std::string& bossId) const { return storiesSeen_.count(bossId) > 0; }
    void markStorySeen(const std::string& bossId) {
        if (storiesSeen_.insert(bossId).second) dirty_ = true;
    }
    const std::set<std::string>& storiesSeen() const { return storiesSeen_; }

private:
    std::map<std::string, BossRecord> bosses_;
    std::set<std::string> storiesSeen_;
    bool dirty_ = false;
};

/// Follows the boss fights of a run, step by step (statistics and feedback only: the
/// simulation is never touched). A phase or a fight counts as "sans dégât" when the
/// player took no damage and did not die between its start and its end.
class BossFightTracker {
public:
    struct Event {
        enum class Kind { PhaseNoHit, Kill };
        Kind kind = Kind::Kill;
        std::string bossId, bossName;
        int phase = 0;        // PhaseNoHit: the phase beaten without a hit
        std::string phaseName;
        bool noHit = false;   // Kill: the whole fight without a hit
        int kills = 0;        // Kill: victories over this boss so far
    };

    /// The world's events (`bossDefeated`).
    void onWorldEvent(const std::string& source, const std::string& trigger);
    /// After each simulation step.
    void step(GameWorld& w, BossStats& stats);
    /// Events since the last call (sound, message).
    std::vector<Event> takeEvents();
    /// Victories of this run (results screen).
    const std::vector<Event>& kills() const { return kills_; }

private:
    struct Fight {
        const Enemy* enemy = nullptr;
        std::string objectId, bossId, name;
        int phase = 0;
        int hitsAtPhase = 0;
        int hitsAtStart = 0;
        /// Fight resumed after a death or a hit against this boss: the phase it resumes in
        /// was started before, it cannot count « sans dégât » (-1: none).
        int taintedPhase = -1;
    };
    void begin(GameWorld& w, Enemy& boss, BossStats& stats, int hits);
    void phaseDone(BossStats& stats, int hits);
    std::optional<Fight> fight_;
    std::map<std::string, bool> hitBefore_;    // per boss object: hit or dead since its first fight
    std::map<std::string, bool> diedBefore_;   // per boss object: the player died against it
    std::map<std::string, float> firstStart_;  // per boss object: time of its first fight
    std::set<std::string> engaged_;            // per boss object: fought in this run, not beaten yet
    std::map<std::string, std::string> interrupted_;  // fights left unbeaten since the last death: key → boss id
    bool defeatedEvent_ = false;
    int lastDeaths_ = 0;
    std::vector<Event> events_, kills_;
};

}  // namespace cosmic
