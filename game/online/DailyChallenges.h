#pragma once

// « Défis du jour » (2.32), the game's side of docs/online/DAILY_CHALLENGES.md: the challenge
// as the server describes it, the cache that keeps it playable offline until its end
// (saves/challenges/), the player's local results and streak, and the runs waiting to be sent
// (the same offline-queue pattern as the records: kept on disk, sent when the connection and
// the session are there, never blocking the main thread).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/gameplay/ChallengeRules.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class OnlineService;
struct ReplayData;

/// Tier of a finished run: Bronze (finished), Argent (time ≤ silver), Or (time ≤ gold and,
/// when the challenge says so, no damage).
enum class ChallengeTier : int { None = 0, Bronze = 1, Silver = 2, Gold = 3 };
const char* challengeTierLabel(ChallengeTier t);  // "Bronze", "Argent", "Or", "—"
/// "BRONZE" / "silver" / 3… as the server writes it (None when unknown).
ChallengeTier challengeTierFromJson(const Json& v);

/// A challenge as the server describes it (§1 of the contract), checked on reading: dates,
/// level, rules (and their hash, recomputed), tiers. Never trusted blindly.
struct DailyChallenge {
    std::string id;           // "daily-2026-11-16"
    std::string kind;         // "daily"
    std::string date;         // "2026-11-16" (UTC day)
    std::string source;       // "auto" or "admin"
    int64_t startsAtMs = 0;   // UTC, milliseconds since 1970
    int64_t endsAtMs = 0;
    struct Level {
        std::string source;    // "campaign" or "community"
        std::string levelId;   // campaign level id, or the community level's online id
        int version = 0;       // revision (campaign) or published version, frozen (community)
        std::string hash;      // fingerprint of that version
        std::string title;
        std::string authorId;  // community levels
        std::string authorName;
    } level;
    ChallengeRules rules;
    std::string rulesHash;
    int64_t silverMs = 0;
    int64_t goldMs = 0;
    bool goldNoDamage = true;
    Json raw;  // as received (cache)

    bool community() const { return level.source == "community"; }
    ChallengeTier tierFor(int64_t timeMs, int damageTaken) const;
    static std::optional<DailyChallenge> fromJson(const Json& j, std::string* error = nullptr);
};

/// Where a challenge run stands, as the player sees it.
enum class ChallengeRunState {
    LocalOnly,  // no account (or a practice run): kept on this computer only
    Waiting,    // « hors ligne : à l'envoi » — waits for the connection
    Sent,       // « envoyé » — declared, its replay on the way
    Pending,    // « en attente de contrôle »
    Verified,   // « vérifié » — the server checked its replay
    Rejected,   // « refusé »
};
const char* challengeRunStateLabel(ChallengeRunState s);
ChallengeRunState challengeRunStateFromServer(const std::string& status);

/// A challenge run kept until the server has decided (saves/challenges/pending.json).
struct ChallengeSubmission {
    std::string attemptId;
    std::string challengeId;
    std::string ownerId;  // account logged in when the run ended
    int64_t timeMs = 0;
    int deaths = 0;
    int damageTaken = 0;
    std::string levelHash, rulesHash, gameVersion;
    bool hasReplay = false;
    bool replayUploaded = false;
    ChallengeRunState state = ChallengeRunState::Waiting;
    ChallengeTier serverTier = ChallengeTier::None;  // the tier the server computed
    std::string runId;
    std::string uploadPath;  // where the server takes its replay (/api/v1/challenges/runs/…)
    std::string note;      // why it was refused (French)
    std::string createdAt;

    Json toJson() const;
    static std::optional<ChallengeSubmission> fromJson(const Json& j);
    /// Body of POST /challenges/{id}/runs.
    Json body() const;
};

/// The player's best run of a challenge on this computer (saves/challenges/results.json).
struct ChallengeResult {
    std::string challengeId;
    std::string date;
    int64_t bestMs = 0;  // 0: never finished
    ChallengeTier tier = ChallengeTier::None;
    int deaths = 0;
    int damageTaken = 0;
    int finishes = 0;
    std::string bestAttemptId;
    bool hasReplay = false;  // replays/<challenge>.cprz is that run
};

class ChallengeService {
public:
    static constexpr int kVersion = 1;
    /// Archives listed at most (cache and server pages).
    static constexpr int kArchivePage = 20;

    /// `dir`: saves/challenges of the user folder.
    void init(OnlineService& online, fs::path dir);
    /// Main thread, every frame: sends what waits, follows the statuses.
    void update(float dt);
    const fs::path& dir() const { return dir_; }

    // ------------------------------------------------------------------ time
    /// Now, on the server's clock when it is known (offset measured at the last reading of
    /// the current challenge, kept on disk), else on this computer's clock.
    int64_t nowMs() const;
    bool serverTimeKnown() const { return offsetKnown_; }
    static int64_t localNowMs();
    /// "2026-11-16" of a UTC time.
    static std::string utcDate(int64_t ms);

    // ------------------------------------------------------------ challenges
    /// The challenge running now (cached; playable offline until its end), nullptr if none.
    const DailyChallenge* current() const;
    const DailyChallenge* find(const std::string& id) const;
    /// The challenges already over (cache, and the pages read from the server), newest first.
    std::vector<const DailyChallenge*> past() const;
    enum class Fetch { Never, Loading, Ok, Offline, Failed };
    Fetch fetchState() const { return fetch_; }
    const std::string& fetchMessage() const { return fetchMessage_; }
    /// Reads GET /challenges/daily/current (at most every few minutes unless `force`).
    void refresh(bool force = false);
    /// Reads a page of GET /challenges/daily (older than the oldest known). `cb(ok, message)`.
    void loadArchives(std::function<void(bool, const std::string&)> cb);
    bool archivesExhausted() const { return archivesDone_; }
    bool archivesLoading() const { return archivesBusy_; }
    /// A challenge read elsewhere (a screen): validated and cached.
    const DailyChallenge* note(const Json& challenge);

    // --------------------------------------------------------------- results
    const ChallengeResult* result(const std::string& challengeId) const;
    fs::path bestReplayPath(const std::string& challengeId) const;
    std::optional<ReplayData> bestReplay(const std::string& challengeId) const;
    struct Streak {
        int current = 0;
        int best = 0;
        std::string lastDate;
        bool fromServer = false;
    };
    /// The server's streak when it was read (/challenges/me), else computed from the local
    /// results (UTC days in a row, up to today or yesterday, with a run of that day's challenge).
    Streak streak() const;
    Streak localStreak() const;
    /// Reads GET /challenges/me (logged in only).
    void refreshMine(ApiCallback cb = {});
    /// The server's status of my best run of a challenge (/challenges/me), if read.
    std::optional<ChallengeRunState> serverStatus(const std::string& challengeId) const;

    // ------------------------------------------------------------------- runs
    struct RunReport {
        bool newBest = false;
        ChallengeTier tier = ChallengeTier::None;
        int64_t previousBestMs = 0;
        ChallengeRunState state = ChallengeRunState::LocalOnly;
        std::string note;
    };
    /// A finished run of the challenge of the day (not a practice run): kept as the local best
    /// when it is (with its replay), and queued for the server when an account is logged in
    /// and `sendable` (the level played is the challenge's version).
    RunReport recordRun(const DailyChallenge& c, int64_t timeMs, int deaths, int damageTaken, const std::string& levelHash,
                        const std::string& attemptId, const ReplayData* replay, bool sendable, const std::string& whyNot = {});
    const ChallengeSubmission* submission(const std::string& attemptId) const;
    /// The submission of my best local run of a challenge (nullptr when none).
    const ChallengeSubmission* bestSubmission(const std::string& challengeId) const;
    /// Runs of the logged-in player waiting to be sent.
    int waitingCount() const;
    /// Tries to send what waits now.
    void flush();

    // ----------------------------------------------------- leaderboard, ghost
    void leaderboard(const std::string& challengeId, const std::string& scope, ApiCallback cb);
    /// Downloads the replay of a run (the first of the day, as a ghost), kept in ghosts/.
    void downloadRunReplay(const std::string& challengeId, const std::string& runId,
                           std::function<void(std::optional<fs::path>, const std::string&)> cb);
    fs::path runReplayPath(const std::string& challengeId, const std::string& runId) const;

    /// Tests: the clock offset (server − local), as measured from `server_time`.
    void setServerOffsetForTests(int64_t offsetMs) {
        offsetMs_ = offsetMs;
        offsetKnown_ = true;
    }

private:
    void loadAll();
    void saveState() const;
    void saveResults() const;
    void saveSubmissions() const;
    void cache(const DailyChallenge& c);
    void sendNext();
    void upload(const std::string& attemptId);
    void applyRun(ChallengeSubmission& s, const Json& run);
    fs::path uploadPath(const std::string& attemptId) const;

    OnlineService* online_ = nullptr;
    fs::path dir_;
    std::map<std::string, DailyChallenge> challenges_;
    std::map<std::string, ChallengeResult> results_;
    std::vector<ChallengeSubmission> submissions_;
    int64_t offsetMs_ = 0;
    bool offsetKnown_ = false;
    Fetch fetch_ = Fetch::Never;
    std::string fetchMessage_;
    double lastFetch_ = -1e9;  // steady seconds
    double clock_ = 0.0;
    bool archivesBusy_ = false;
    bool archivesDone_ = false;
    bool sending_ = false;
    bool wasOffline_ = false;
    float sendTimer_ = 2.0f;
    float mineTimer_ = 0.0f;
    bool mineBusy_ = false;
    std::optional<Streak> serverStreak_;
    std::string serverStreakFor_;
    std::map<std::string, ChallengeRunState> serverStatus_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
