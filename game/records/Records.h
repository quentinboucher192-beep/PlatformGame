#pragma once

#include <map>
#include <memory>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/gameplay/RunStats.h"
#include "game/levels/LevelData.h"

namespace cosmic {

/// Result of one completed (or failed) attempt.
struct RunResult {
    std::string levelId;
    bool completed = false;
    float time = 0.0f;
    int deaths = 0;
    int collectibles = 0;
    int totalCollectibles = 0;
    int relics = 0;
    int secrets = 0;
    int totalSecrets = 0;
    int kills = 0;
    int score = 0;
    Medal medal = Medal::None;
    std::vector<std::string> powersUsed;
    std::vector<float> splits;
    std::string date;
    std::string player = "Nova";
    /// Gameplay fingerprint of the level played (campaign): a record is only valid for it.
    std::string levelHash;

    Json toJson() const;
    static RunResult fromJson(const Json& j);
};

struct LevelRecord {
    std::string levelId;
    int attempts = 0;
    int completions = 0;
    float bestTime = 0.0f;  // 0 = never completed
    std::string bestTimeDate;
    std::vector<float> bestSplits;
    std::vector<std::string> bestTimePowers;
    float firstCompletionTime = 0.0f;
    int bestScore = 0;
    int fewestDeaths = -1;
    int mostCollectibles = 0;
    int totalCollectibles = 0;
    int mostKills = 0;
    int mostSecrets = 0;
    Medal bestMedal = Medal::None;
    std::vector<RunResult> leaderboard;  // local top runs (by time)
    /// Gameplay fingerprint of the level when the best time was set ("" = before 2.4.0).
    std::string levelHash;

    Json toJson() const;
    static LevelRecord fromJson(const Json& j);
};

/// What changed with a submitted run (for the results screen).
struct RecordUpdate {
    bool firstCompletion = false;
    bool newBestTime = false;
    bool newBestScore = false;
    bool newMedal = false;
    float previousBestTime = 0.0f;
    int previousBestScore = 0;
    Medal previousMedal = Medal::None;
    int leaderboardRank = -1;  // 0-based, -1 if not ranked
};

struct LeaderboardEntry {
    std::string player;
    float time = 0.0f;
    int score = 0;
    Medal medal = Medal::None;
    std::string date;
};

/// Leaderboard back-end. The game ships with a local provider; an online provider can
/// be added later by implementing this interface (no change to the game code).
class ILeaderboardProvider {
public:
    virtual ~ILeaderboardProvider() = default;
    virtual std::string name() const = 0;
    virtual void submit(const RunResult& run) = 0;
    virtual std::vector<LeaderboardEntry> top(const std::string& levelId, size_t count) const = 0;
};

class RecordsManager;

class LocalLeaderboard : public ILeaderboardProvider {
public:
    explicit LocalLeaderboard(RecordsManager& owner) : owner_(owner) {}
    std::string name() const override { return "Local"; }
    void submit(const RunResult& run) override;
    std::vector<LeaderboardEntry> top(const std::string& levelId, size_t count) const override;

private:
    RecordsManager& owner_;
};

/// Persistent per-level records, medals and local leaderboards (saves/records.json).
class RecordsManager {
public:
    static constexpr int kVersion = 2;
    static constexpr size_t kLeaderboardSize = 10;

    RecordsManager();
    void load();
    bool save() const;

    const LevelRecord* get(const std::string& levelId) const;
    LevelRecord& getOrCreate(const std::string& levelId);
    void registerAttempt(const std::string& levelId);
    RecordUpdate submit(const RunResult& run);
    void reset(const std::string& levelId);
    /// The level changed since its records were set: best time, medal, score and local
    /// ranking are dropped (attempts and completions stay). Returns the record as it was.
    std::optional<LevelRecord> invalidateBest(const std::string& levelId);

    const std::vector<std::unique_ptr<ILeaderboardProvider>>& providers() const { return providers_; }
    void addProvider(std::unique_ptr<ILeaderboardProvider> p) { providers_.push_back(std::move(p)); }

    static Medal evaluateMedal(const LevelData& level, const RunResult& run);
    static int computeScore(const LevelData& level, const RunStats& stats, float time, bool completed);
    /// Human readable condition of a medal tier ("< 3:00, 0 mort").
    static std::string medalCondition(const LevelData& level, Medal m);

    /// v1 -> v2: v1 stored times as integer milliseconds ("bestTimeMs").
    static Json migrateV1ToV2(const Json& v1);
    Json toJson() const;
    void fromJson(const Json& j);

private:
    std::map<std::string, LevelRecord> records_;
    std::vector<std::unique_ptr<ILeaderboardProvider>> providers_;
};

}  // namespace cosmic
