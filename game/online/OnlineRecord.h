#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

/// One category in which a finished run is (or will be) ranked.
struct OnlineSubmission {
    RunCategory category = RunCategory::AnyPercent;
    RecordStatus status = RecordStatus::Local;
    std::string recordId;    // server id once submitted
    std::string note;        // server note / rejection reason / why it is blocked
    bool blocked = false;    // the server refused it for good (version, category...): not retried
    int rank = 0;            // world rank after verification (0 = unknown)
    int previousRank = 0;
};

/// A finished run kept locally until the server has decided. Every attempt carries a
/// unique id: submitting it twice never creates two records.
struct OnlineAttempt {
    std::string attemptId;
    std::string levelId;
    int levelVersion = 1;
    std::string levelHash;
    std::string gameVersion;
    int64_t timeMs = 0;
    int deaths = 0;
    int damageTaken = 0;
    int collectibles = 0;
    int totalCollectibles = 0;
    int kills = 0;
    int score = 0;
    std::string createdAt;   // local "YYYY-MM-DD HH:MM"
    std::string ownerId;     // account logged in when the run ended ("" = none)
    bool hasReplay = false;
    bool replayUploaded = false;
    std::vector<OnlineSubmission> submissions;

    Json toJson() const;
    static std::optional<OnlineAttempt> fromJson(const Json& j);
    /// Body of POST /records/submit for one category.
    Json submitBody(RunCategory c) const;
    OnlineSubmission* submission(RunCategory c);
    const OnlineSubmission* submission(RunCategory c) const;
    bool needsSync() const;
};

/// What the game knows about a run when the level ends.
struct RunCandidate {
    std::string levelId;
    int levelVersion = 1;
    std::string levelHash;
    int64_t timeMs = 0;
    int deaths = 0;
    int damageTaken = 0;
    int collectibles = 0;
    int totalCollectibles = 0;
    int kills = 0;
    int score = 0;
    bool fromStart = true;  // not started at a custom spawn point
    bool hasReplay = false;
};

/// Categories a run qualifies for (precise rules: docs/online/RULES.md).
std::vector<RunCategory> eligibleCategories(const RunCandidate& run);

/// Local performances waiting for / tracked by the server (saves/online_attempts.json,
/// versioned) and their replay files (online/replays/<attempt>.cprz).
class AttemptStore {
public:
    static constexpr int kVersion = 1;
    static constexpr size_t kMaxAttempts = 300;

    explicit AttemptStore(fs::path file = {}, fs::path replayDir = {}) : file_(std::move(file)), replayDir_(std::move(replayDir)) {}
    void load();
    bool save() const;

    /// Adds a finished run if it improves the player's best local time in at least one
    /// category (others are not worth sending). Returns the stored attempt or nullptr.
    OnlineAttempt* add(const RunCandidate& run, const std::string& attemptId, const std::string& ownerId);
    OnlineAttempt* find(const std::string& attemptId);
    std::vector<OnlineAttempt>& all() { return attempts_; }
    const std::vector<OnlineAttempt>& all() const { return attempts_; }
    /// Best local time (any status but REJECTED) for a level revision and category.
    std::optional<int64_t> bestTime(const std::string& levelHash, RunCategory c) const;
    /// Submissions not sent yet (status LOCAL, not blocked).
    int unsentCount(const std::string& ownerId) const;
    fs::path replayPath(const std::string& attemptId) const;
    void removeOwner(const std::string& ownerId);

private:
    void trim();
    fs::path file_;
    fs::path replayDir_;
    std::vector<OnlineAttempt> attempts_;
};

}  // namespace cosmic
