#pragma once

// « Défis » (2.32 to 2.35), the game's side of docs/online/DAILY_CHALLENGES.md and
// docs/online/CHALLENGES_PERIODS.md: the challenges of the day, of the week and of the month as
// the server describes them, the cache that keeps each playable offline until its end and the
// past ones playable « hors délai » (saves/challenges/), the calendar of a month, the player's
// local results and streaks (one per kind), and the runs (and, from 2.33, the unfinished
// attempts that went further than my best one: « Plus loin ») waiting to be sent (the same
// offline-queue pattern as the records: kept on disk, sent when the connection and the
// session are there, never blocking the main thread).

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/cosmetics/Collection.h"
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

/// The three kinds of challenges (2.33): a UTC day, an ISO 8601 week (Monday to Monday), a
/// month. Their periods are written "2026-11-16", "2026-W47" and "2026-11".
enum class ChallengeKind : int { Daily = 0, Weekly = 1, Monthly = 2 };
constexpr int kChallengeKinds = 3;
const char* challengeKindId(ChallengeKind k);  // "daily", "weekly", "monthly"
std::optional<ChallengeKind> challengeKindFromId(const std::string& id);

namespace challenge_period {
/// Days since 1970-01-01 of a "YYYY-MM-DD" date (nullopt when it is not a real date).
std::optional<int64_t> dayNumber(const std::string& date);
/// "2026-11-16" of a day number.
std::string dateOfDay(int64_t day);
/// 0 = Monday … 6 = Sunday.
int weekday(int64_t day);
/// The period of `kind` that contains the UTC day `day`: "2026-11-16", "2026-W47", "2026-11".
std::string ofDay(ChallengeKind kind, int64_t day);
/// Whether `period` is a well-formed period of `kind` (a real day, week 01..53 of its year, month).
bool valid(ChallengeKind kind, const std::string& period);
/// Its first day (days since 1970-01-01), nullopt when invalid.
std::optional<int64_t> firstDay(ChallengeKind kind, const std::string& period);
/// A number that grows by one from a period to the next (streaks), nullopt when invalid.
std::optional<int64_t> index(ChallengeKind kind, const std::string& period);
/// "2026-11" → "2026-12" (`delta` months later, may be negative); "" when malformed.
std::string addMonths(const std::string& month, int delta);
/// Days in a "YYYY-MM" month (0 when malformed).
int daysInMonth(const std::string& month);
}  // namespace challenge_period

/// A challenge as the server describes it (§1 of the contracts), checked on reading: kind and
/// period, dates, level, rules (and their hash, recomputed), tiers. Never trusted blindly.
struct DailyChallenge {
    std::string id;           // "daily-2026-11-16", "weekly-2026-W47", "monthly-2026-11"
    ChallengeKind kind = ChallengeKind::Daily;
    std::string period;       // "2026-11-16", "2026-W47", "2026-11"
    std::string date;         // first UTC day ("2026-11-16")
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
    bool progressAvailable = false;  // « Plus loin » (2.33): its level has a route layout
    Json raw;  // as received (cache)

    bool community() const { return level.source == "community"; }
    const char* kindId() const { return challengeKindId(kind); }
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

/// A challenge run (or, from 2.33, an unfinished attempt that went further than my best one)
/// kept until the server has decided (saves/challenges/pending.json).
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
    bool late = false;  // played after the end: « Depuis » only (the server's word once sent)
    // « Plus loin » (2.33): an unfinished attempt, POST /challenges/{id}/attempts with its claim,
    // then its replay to PUT /challenges/attempts/{attempt_id}/replay (the game's own id).
    bool attempt = false;
    std::vector<int> bosses;       // per mille of health removed, per boss of the route layout
    std::vector<int> waves;        // waves beaten, per ambush of the route layout
    float progress = 0.0f;         // this computer's estimate (only to decide to send it)
    float serverProgress = -1.0f;  // the server's score, computed from the replay
    ChallengeRunState state = ChallengeRunState::Waiting;
    ChallengeTier serverTier = ChallengeTier::None;  // the tier the server computed
    std::string runId;
    std::string uploadPath;  // where the server takes its replay (/api/v1/challenges/…)
    std::string note;      // why it was refused (French)
    std::string createdAt;

    Json toJson() const;
    static std::optional<ChallengeSubmission> fromJson(const Json& j);
    /// Body of POST /challenges/{id}/runs, or of POST /challenges/{id}/attempts (an attempt:
    /// { attempt_id, time_ms, level_hash, rules_hash, game_version, claim }).
    Json body() const;
    /// Where it is declared: "/api/v1/challenges/{id}/runs" or "…/attempts".
    std::string postPath() const;
};

/// The player's best run of a challenge on this computer (saves/challenges/results.json).
struct ChallengeResult {
    std::string challengeId;
    ChallengeKind kind = ChallengeKind::Daily;
    std::string period;
    std::string date;
    int64_t bestMs = 0;  // 0: never finished
    ChallengeTier tier = ChallengeTier::None;
    int deaths = 0;
    int damageTaken = 0;
    int finishes = 0;
    int liveFinishes = 0;      // finished while the challenge ran (the streak)
    bool bestLate = false;     // the best time was made « hors délai »
    std::string bestAttemptId;
    bool hasReplay = false;  // replays/<challenge>.cprz is that run
    float bestProgress = 0.0f;  // « Plus loin »: my furthest unfinished attempt here (0..99.9)
    std::string bestProgressAttemptId;
};

class ChallengeService {
public:
    static constexpr int kVersion = 1;
    /// A run received this long after the end is « hors délai » (contract §2).
    static constexpr int64_t kLateGraceMs = 10 * 60 * 1000;

    /// `dir`: saves/challenges of the user folder.
    void init(OnlineService& online, fs::path dir);
    /// Main thread, every frame: sends what waits, follows the statuses.
    void update(float dt);
    const fs::path& dir() const { return dir_; }

    // ------------------------------------------------------------------ time
    /// Now, on the server's clock when it is known (offset measured at the last reading of
    /// the current challenges, kept on disk), else on this computer's clock.
    int64_t nowMs() const;
    bool serverTimeKnown() const { return offsetKnown_; }
    static int64_t localNowMs();
    /// "2026-11-16" of a UTC time.
    static std::string utcDate(int64_t ms);
    /// "2026-11" of a UTC time.
    static std::string utcMonth(int64_t ms) { return utcDate(ms).substr(0, 7); }

    // ------------------------------------------------------------ challenges
    /// The challenge of `kind` running now (cached; playable offline until its end), nullptr if none.
    const DailyChallenge* current(ChallengeKind kind = ChallengeKind::Daily) const;
    const DailyChallenge* find(const std::string& id) const;
    /// The challenges already over (cache, and the months read from the server), newest first.
    std::vector<const DailyChallenge*> past() const;
    bool ended(const DailyChallenge& c) const { return nowMs() >= c.endsAtMs; }
    enum class Fetch { Never, Loading, Ok, Offline, Failed };
    Fetch fetchState() const { return fetch_; }
    const std::string& fetchMessage() const { return fetchMessage_; }
    /// The server only knows the day's challenge (2.32: /challenges/current answered 404).
    bool legacyServer() const { return legacy_; }
    /// Reads GET /challenges/current (2.32 server: /challenges/daily/current) at most every few
    /// minutes unless `force`.
    void refresh(bool force = false);
    /// A challenge read elsewhere (a screen): validated and cached.
    const DailyChallenge* note(const Json& challenge);

    // --------------------------------------------------------------- calendar
    struct CalendarMonth {
        enum class State { Never, Loading, Ok, Cached, Offline, Failed };
        State state = State::Never;
        std::string message;  // why it is not the server's answer of now (French)
    };
    /// Reads GET /challenges/calendar?month=YYYY-MM (2.32 server: its days from
    /// /challenges/daily). Its challenges join the cache, my result of each one the server's.
    void loadCalendar(const std::string& month, bool force = false);
    const CalendarMonth& calendar(const std::string& month) const;
    /// The oldest month with a challenge ("" while unknown): the calendar never goes before it.
    const std::string& firstMonth() const { return firstMonth_; }
    /// The challenges known here that have begun and touch `month` (its days, the weeks that
    /// overlap it, the month itself), by start.
    std::vector<const DailyChallenge*> challengesOfMonth(const std::string& month) const;

    // --------------------------------------------------------------- results
    const ChallengeResult* result(const std::string& challengeId) const;
    fs::path bestReplayPath(const std::string& challengeId) const;
    std::optional<ReplayData> bestReplay(const std::string& challengeId) const;
    struct Streak {
        int current = 0;
        int best = 0;
        std::string lastPeriod;
        bool fromServer = false;
    };
    /// The server's streak of a kind when it was read (/challenges/me), else computed from the
    /// local results (periods in a row, up to the current one or the one before, with a run
    /// of that period's challenge finished while it ran).
    Streak streak(ChallengeKind kind = ChallengeKind::Daily) const;
    Streak localStreak(ChallengeKind kind = ChallengeKind::Daily) const;
    /// Reads GET /challenges/me (logged in only).
    void refreshMine(ApiCallback cb = {});
    /// The server's status of my best run of a challenge (/challenges/me, calendar), if read.
    std::optional<ChallengeRunState> serverStatus(const std::string& challengeId) const;
    /// My best run of a challenge as the server knows it (/challenges/me and the calendar:
    /// also the runs made on another computer), if read.
    struct ServerResult {
        int64_t bestMs = 0;
        ChallengeTier tier = ChallengeTier::None;
        ChallengeRunState status = ChallengeRunState::Pending;
        bool late = false;
        float bestProgress = -1.0f;  // « Plus loin » (100: finished), -1 unknown
    };
    std::optional<ServerResult> serverResult(const std::string& challengeId) const;

    // ------------------------------------------ rewards of the challenges (2.35)
    /// What Cosmic Online verified of my challenge runs, for the rewards of the Collection
    /// (`verified` of /challenges/me; a 2.33 server: counted from its results whose status is
    /// VERIFIED), kept on this computer (saves/challenges/state.json) for offline play. It is
    /// the reading of one account: used while that account is logged in, or nobody is; never a
    /// run kept on this computer only. Not `known` before a first reading.
    const ChallengeRewardProgress& rewardProgress() const;
    /// Grows each time a reading changes it (a screen waiting for a reward to be unlocked).
    int rewardsRevision() const { return rewardsRevision_; }
    /// Main thread, after a reading changed it (the Collection, the announcement of a reward).
    std::function<void()> onRewardsChanged;
    /// `verified` of /challenges/me (nullopt when absent or malformed: a 2.33 server).
    static std::optional<ChallengeRewardProgress> rewardProgressFromJson(const Json& v);
    /// A 2.33 server: the results whose status is VERIFIED (their tier, in time or late; the
    /// series: periods with such a result in time).
    static ChallengeRewardProgress rewardProgressFromResults(const Json& results);

    // ------------------------------------------------------------------- runs
    struct RunReport {
        bool newBest = false;
        bool late = false;  // finished after the end of the challenge: « Depuis » only
        ChallengeTier tier = ChallengeTier::None;
        int64_t previousBestMs = 0;
        ChallengeRunState state = ChallengeRunState::LocalOnly;
        std::string note;
    };
    /// Whether a run finished now would be « hors délai » (after the end and its grace).
    bool lateNow(const DailyChallenge& c) const { return nowMs() > c.endsAtMs + kLateGraceMs; }
    /// A finished run of a challenge (not a practice run; « hors délai » once it is over): kept
    /// as the local best when it is (with its replay), and queued for the server when an
    /// account is logged in and `sendable` (the level played is the challenge's version).
    RunReport recordRun(const DailyChallenge& c, int64_t timeMs, int deaths, int damageTaken, const std::string& levelHash,
                        const std::string& attemptId, const ReplayData* replay, bool sendable, const std::string& whyNot = {});
    /// A run (or, `attempt`, an unfinished attempt) kept for the server, by its attempt id.
    const ChallengeSubmission* submission(const std::string& attemptId, bool attempt = false) const;
    /// The submission of my best local run of a challenge (nullptr when none).
    const ChallengeSubmission* bestSubmission(const std::string& challengeId) const;
    /// Runs of the logged-in player waiting to be sent.
    int waitingCount() const;
    /// Tries to send what waits now.
    void flush();

    // ------------------------------------------------------- « Plus loin » (2.33)
    /// How far I went on a challenge: 100 once finished (here or, as the server knows it, on
    /// another computer), else the best unfinished attempt kept here or known by the server;
    /// 0 when none.
    float bestProgress(const std::string& challengeId) const;
    /// Whether an attempt of `progress` goes further than `best` (in tenths, strictly; never a
    /// finish nor nothing).
    static bool beatsBestAttempt(float progress, float best);
    struct AttemptReport {
        bool newBest = false;  // « Nouveau record de progression »
        bool late = false;
        float progress = 0.0f;      // this attempt (one decimal)
        float previousBest = 0.0f;  // my best before it
        ChallengeRunState state = ChallengeRunState::LocalOnly;
        std::string note;
    };
    /// An unfinished attempt of a challenge with « Plus loin » (a fall, a restart, a return to
    /// the menu; without checkpoints, its first life): its progress (raceScore) only decides
    /// whether it beats my best attempt; if so it is kept as such and, logged in and
    /// `sendable`, queued with its claim and its format 4 replay (the server computes the
    /// score). `bosses`: per mille of health removed per boss; `waves`: per ambush.
    AttemptReport recordAttempt(const DailyChallenge& c, float progress, int64_t timeMs, std::vector<int> bosses, std::vector<int> waves,
                                const std::string& levelHash, const std::string& attemptId, const ReplayData* replay, bool sendable,
                                const std::string& whyNot = {});

    // ----------------------------------------------------- leaderboard, ghost
    /// GET /challenges/{id}/leaderboard: `scope` world|country|friends, `allPeriod` (« Depuis »:
    /// period=all, the runs « hors délai » too), `progressBoard` (« Plus loin »: board=progress).
    void leaderboard(const std::string& challengeId, const std::string& scope, ApiCallback cb, bool allPeriod = false,
                     bool progressBoard = false);
    static std::string leaderboardPath(const std::string& challengeId, const std::string& scope, bool allPeriod, bool progressBoard = false);
    /// Downloads the replay of a run (the first of the challenge, as a ghost; a leaderboard's
    /// « Voir »), or with `attempt` of an attempt of the « Plus loin » board (its server id),
    /// kept in ghosts/.
    void downloadRunReplay(const std::string& challengeId, const std::string& runId,
                           std::function<void(std::optional<fs::path>, const std::string&)> cb, bool attempt = false);
    fs::path runReplayPath(const std::string& challengeId, const std::string& runId, bool attempt = false) const;

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
    void fetchCurrent(bool legacy);
    void applyCurrent(const ApiResult& r, bool legacy);
    void loadCalendarLegacy(const std::string& month, const std::string& before, int page);
    void noteServerResult(const std::string& id, const Json& x);
    bool resultsMine();
    void sendNext();
    void upload(const std::string& attemptId, bool attempt);
    void applyRun(ChallengeSubmission& s, const Json& run);
    ChallengeSubmission* findSubmission(const std::string& attemptId, bool attempt);
    void trimSubmissions();
    fs::path uploadPath(const std::string& attemptId, bool attempt = false) const;

    OnlineService* online_ = nullptr;
    fs::path dir_;
    std::map<std::string, DailyChallenge> challenges_;
    std::map<std::string, ChallengeResult> results_;
    std::vector<ChallengeSubmission> submissions_;
    int64_t offsetMs_ = 0;
    bool offsetKnown_ = false;
    Fetch fetch_ = Fetch::Never;
    std::string fetchMessage_;
    bool legacy_ = false;
    int64_t knownEndMs_ = 0;   // the first end among the challenges running at the last reading
    double lastFetch_ = -1e9;  // steady seconds
    double clock_ = 0.0;
    std::map<std::string, CalendarMonth> calendar_;
    std::string firstMonth_;
    bool sending_ = false;
    bool wasOffline_ = false;
    float sendTimer_ = 2.0f;
    float mineTimer_ = 0.0f;
    bool mineBusy_ = false;
    std::vector<ApiCallback> mineWaiters_;  // refreshMine() called while a reading was on its way
    std::array<std::optional<Streak>, kChallengeKinds> serverStreaks_;
    std::string serverStreakFor_;  // the account whose streaks, results and rewards these are
    std::map<std::string, ServerResult> serverResults_;
    void setRewardProgress(const ChallengeRewardProgress& p);
    ChallengeRewardProgress rewards_;
    int rewardsRevision_ = 0;
    bool rewardsStale_ = false;  // a run was just verified: /challenges/me again for its rewards
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
