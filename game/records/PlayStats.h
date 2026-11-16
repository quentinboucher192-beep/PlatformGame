#pragma once

// 2.53 — « Statistiques » (main menu → Statistiques): what the player's own runs say about their
// play, kept on this computer only (saves/stats.json), never sent online.
//
//  * a summary per day (time played, deaths, attacks that threatened Nova and those she
//    dodged, her attacks and those that hit, per attack, where the blows came from, the stars
//    of the achievements at the end of the day), kept 365 days, the older days merged by month;
//  * per level, 40 slices of its route (the progress bar's route, game/gameplay/LevelProgress.h):
//    how many lives went through each and how many ended there, raw and weighted by recency
//    (half-life of 14 days);
//  * per boss, its last 200 attempts (share of its total health taken, phase reached, the attack
//    that ended it) and counters per phase;
//  * per classic level (not EXTRÊME) played from its start in the campaign, its segments between
//    checkpoints (the splits of the level timer, the same as Ghost Racing's): the last 50 times
//    of each, the best ones, the last full runs and the history of the PB and Sum of Best.
//
// The recorder (PlayStatsRecorder) is a WorldListener: it reads the world after each step and
// hears its informative hooks, it never changes the run (replays, records and rankings are the
// same with or without it). It counts the runs of the campaign, of the challenges and of the
// players' levels; training runs (boss, segment, practice) only for their time, apart; never a
// run with « Invincible » (administrators), an editor test, a replay nor a friend's run watched.
// The file is written on a worker thread (atomic: temporary file then rename), at the end of each
// attempt and when the game closes — never during a frame. A damaged file is kept as
// stats.json.bak and the statistics start again empty.

#include <array>
#include <bitset>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/gameplay/LevelProgress.h"
#include "game/gameplay/WorldListener.h"

namespace cosmic {

class ContentDatabase;
class GameWorld;
struct LevelData;

namespace playstats {
constexpr int kSlices = 40;               // a level's route cut in 40 slices
constexpr int kDaysKept = 365;            // days kept one by one (the older ones: by month)
constexpr int kMonthsKept = 120;          // months kept (ten years; the older ones dropped)
constexpr int kBossAttemptsKept = 200;    // attempts kept per boss
constexpr size_t kMaxLevels = 60;         // levels kept (the least recently played dropped)
constexpr size_t kMaxBosses = 40;         // bosses kept (the least recently fought dropped)
constexpr size_t kMaxFatal = 12;          // fatal attacks counted per boss (the rarest dropped)
constexpr size_t kMaxFileBytes = 300 * 1024;
constexpr float kHalfLifeDays = 14.0f;    // weight of a passage halves every 14 days
constexpr int kMinPassages = 5;           // a difficult passage needs at least 5 passages
constexpr float kThreatDistance = 24.0f;  // 1.5 tiles (16 px): an attack this close threatens
constexpr int kOrigins = static_cast<int>(HurtOrigin::Count);
// « Niveaux » (classic levels: the segments between checkpoints, Sum of Best, regularity).
constexpr size_t kMaxRunLevels = 24;      // levels kept (the least recently played dropped)
constexpr size_t kMaxSegments = 16;       // segments kept per level
constexpr size_t kSegmentTimesKept = 50;  // the last times of a segment
constexpr size_t kFinishesKept = 40;      // the last full runs
constexpr size_t kHistoryKept = 40;       // changes of the PB / Sum of Best
constexpr int kRecentFinishes = 20;       // regularity: the last 20 full runs
constexpr float kNearBest = 0.05f;        // « à moins de 5 % du meilleur »
}  // namespace playstats

/// Days since 1970-01-01 of a calendar date, and back (proleptic Gregorian).
int daysFromCivil(int y, int m, int d);
void civilFromDays(int days, int& y, int& m, int& d);
/// Today on this computer's calendar (local time), in days since 1970-01-01.
int localDay();

/// The counters of a day (or of a month).
struct StatsCounters {
    double playSec = 0.0;   // runs that count (campaign, challenges, players' levels)
    double trainSec = 0.0;  // training runs (boss, segment, practice): counted apart
    int deaths = 0;
    int threats = 0, dodged = 0;  // enemy attacks that came within 1.5 tiles; those that missed
    int attacks = 0, hits = 0;    // Nova's attacks (swings, shots, blades…); those that hurt an enemy
    std::map<std::string, std::array<int, 2>> powers;  // "attack" or a power id → {launched, hit}
    std::array<int, playstats::kOrigins> hurt{};      // blows taken (and falls) by origin
    int stars = -1;  // stars of the achievements at the end of the day (-1: unknown)

    void add(const StatsCounters& o);
    bool played() const { return playSec >= 1.0; }
};

struct StatsDay {
    int day = 0;  // days since 1970-01-01
    StatsCounters c;
};
struct StatsMonth {
    int month = 0;  // year * 12 + month - 1
    StatsCounters c;
};

/// A level's 40 slices.
struct LevelSlices {
    std::string name;
    std::array<int, playstats::kSlices> passes{}, deaths{};
    std::array<float, playstats::kSlices> wPasses{}, wDeaths{};  // weighted, as of the day `stamp`
    int stamp = 0;
    int lastDay = 0;
    /// The weights as of `day` (halved every 14 days).
    void decayTo(int day);
    bool any() const;
};

struct BossAttemptRec {
    int day = 0;
    float removed = 0.0f;  // share of its total health (every phase) taken, 0..1
    int phase = 0;         // the phase it was in at the end (0 = the first)
    bool won = false;
    std::string attack;    // what ended it (a defeat), "" otherwise
};

struct BossHistory {
    std::string name;
    int phases = 1;
    int attempts = 0, wins = 0;
    int firstWin = 0;          // the attempt number of the first victory (0: none yet)
    float bestBefore = 0.0f;   // the best share among the attempts no longer kept
    std::vector<BossAttemptRec> recent;  // the last 200, oldest first
    std::vector<int> reached, defeats;   // per phase: attempts that reached it, defeats in it
    std::map<std::string, int> fatal;    // attack → defeats it ended
    int lastSeq = 0;                     // order of the last fight (higher: more recent)

    /// The attempt number of recent[0].
    int firstKept() const { return attempts - static_cast<int>(recent.size()) + 1; }
    float best() const;
};

/// One segment of a classic level (from a checkpoint to the next one; "" = the start, or the exit).
struct SegmentRec {
    std::string from, to;
    std::vector<float> times;  // the last 50 (seconds), oldest first
    float best = -1.0f;        // the best ever (-1: none)
};
struct FinishRec {
    int day = 0;
    float time = 0.0f;
};
/// The PB and the Sum of Best after a change of either (-1: unknown).
struct SobPoint {
    int day = 0;
    float pb = -1.0f;
    float sob = -1.0f;
};
/// A classic level (not EXTRÊME) played from its start in the campaign: its segments.
struct LevelRuns {
    std::string name;
    std::string version;  // the level's version (its fingerprint): another one starts again
    int attempts = 0, finished = 0;
    float pb = -1.0f;
    std::vector<std::string> pbRoute;  // the checkpoints of the PB run, in order
    std::vector<SegmentRec> segments;
    std::vector<FinishRec> finishes;   // the last 40 full runs, oldest first
    std::vector<SobPoint> history;
    int lastDay = 0;

    const SegmentRec* segment(const std::string& from, const std::string& to) const;
    SegmentRec& segmentAt(const std::string& from, const std::string& to);
    /// The segments of the PB route, in order ("" at both ends).
    std::vector<std::pair<std::string, std::string>> route() const;
    /// Sum of Best: the best time of each segment of the PB route added up (-1 unknown).
    float sob() const;
};

struct PlayStatsData {
    std::vector<StatsDay> days;  // in order
    std::vector<StatsMonth> months;
    std::map<std::string, LevelSlices> levels;
    std::map<std::string, BossHistory> bosses;
    std::map<std::string, LevelRuns> runs;
    int seq = 0;

    bool empty() const;
    /// The counters of `day` (created in order when missing).
    StatsCounters& at(int day);
    const StatsDay* find(int day) const;
    /// Days older than 365 merged by month, and every cap applied (levels, bosses, attempts…).
    void compact(int today);
    Json toJson() const;
    /// Never trusts the file: wrong types are skipped, numbers clamped.
    static PlayStatsData fromJson(const Json& j);
};

// ------------------------------------------------------------------ calculations

/// The totals of the days `from`..`to` (inclusive).
struct PeriodTotals {
    int daysPlayed = 0;
    StatsCounters c;
    std::optional<double> dodgeRate() const;
    std::optional<double> accuracy() const;
    std::optional<double> deathsPerHour() const;
};
PeriodTotals periodTotals(const PlayStatsData& d, int from, int to);

/// The choice of period against the previous one of the same length.
struct Comparison {
    bool available = false;  // both periods have a value
    double delta = 0.0;      // current − previous
    bool progress = false;   // the change is for the better (`higherIsBetter` says which way)
    bool same = false;       // no change worth saying
};
Comparison comparePeriods(std::optional<double> current, std::optional<double> previous, bool higherIsBetter, double epsilon = 1e-9);

enum class DayMetric { Dodge, Accuracy, DeathsPerHour };
/// One played day of a metric: its own value and the average over the last 7 days played that
/// have a value (that day included; pooled: their totals divided, not their values averaged).
struct SeriesPoint {
    int day = 0;
    double value = 0.0;
    double rolling = 0.0;
};
std::vector<SeriesPoint> dailySeries(const PlayStatsData& d, DayMetric m, int from, int to, int window = 7);
/// The stars of the achievements at the end of each day of `from`..`to` that knows them.
std::vector<std::pair<int, int>> starsSeries(const PlayStatsData& d, int from, int to);

/// The best share taken so far, attempt after attempt (attempt number, best): a victory is 1.
std::vector<std::pair<int, float>> bossBestCurve(const BossHistory& b);
/// The phase with the most defeats (-1: none).
int hardestPhase(const BossHistory& b);
/// The attack that ended the most attempts ("" when none).
std::string commonFatal(const BossHistory& b);

/// Smoothed difficulty of a slice: (deaths + 1) / (passages + 2).
float sliceDifficulty(float deaths, float passes);
/// Weight of something `days` old (half-life 14 days).
float recencyWeight(int days);

/// A difficult passage: neighbouring slices of a level, or a phase of a boss.
struct HardPassage {
    enum class Kind { Level, BossPhase } kind = Kind::Level;
    std::string id;    // the level, or the boss
    std::string name;  // its name as kept
    int from = 0, to = 0;      // slices (level), inclusive
    int phase = 0, phases = 0; // boss phase (0 = the first)
    int passes = 0, deaths = 0;
    float difficulty = 0.0f;   // 0..1
    std::string attack;        // boss: its most frequent fatal attack
};
/// The difficulty of each slice of a level as of `today` (-1: no passage there).
std::array<float, playstats::kSlices> sliceDifficulties(const LevelSlices& l, int today);
/// The difficult passages of a level: slices above twice the level's median merged with their
/// neighbours, kept with at least 5 passages; difficulty = 1 − Π(1 − d).
std::vector<HardPassage> levelPassages(const std::string& id, const LevelSlices& l, int today);
/// A boss's phases as passages: defeats in the phase / attempts that reached it (at least 5).
std::vector<HardPassage> bossPassages(const std::string& id, const BossHistory& b);
/// The `count` most difficult passages, every level and boss phase together.
std::vector<HardPassage> hardestPassages(const PlayStatsData& d, int today, size_t count = 5);

/// « Niveaux »: a segment of a level's route, as the section shows it.
struct SegmentReport {
    std::string from, to;
    int index = 0;           // its place in the route (0 = from the start)
    int count = 0;           // times kept
    float best = -1.0f;
    float median = -1.0f;    // of the last times kept
    float lost = 0.0f;       // median − best: « temps perdu en moyenne »
    float spread = 0.0f;     // interquartile range / median (consistency: lower is steadier)
    float nearBest = 0.0f;   // share of the times within 5 % of the best
};
/// Every segment of the PB route that has times (without a PB: the segments in the order kept).
std::vector<SegmentReport> segmentReports(const LevelRuns& l);
/// Regularity of the full runs.
struct Regularity {
    int attempts = 0, finished = 0;
    std::vector<FinishRec> last;       // the last 20 full runs, oldest first
    std::vector<float> rollingMedian;  // per run of `last`: the median of it and the 4 before
    std::optional<float> sd;           // standard deviation of `last`
    std::optional<float> sdBefore;     // the same as of 30 days ago (the 20 runs before that day)
    int trend = 0;                     // 1 more regular than 30 days ago, -1 less, 0 the same / unknown
};
Regularity regularity(const LevelRuns& l, int today);
float medianOf(std::vector<float> v);
/// Interquartile range (quartiles linearly interpolated).
float iqrOf(std::vector<float> v);
std::optional<float> stddevOf(const std::vector<float>& v);

/// The bosses fought, the most recent first.
std::vector<std::string> bossesByRecency(const PlayStatsData& d);

/// Whether an attack of Nova counts for the precision: her attack ("attack"), and the powers
/// that hurt (their behaviour: wave, charge_shot, plasma_bomb, boomerang, boomerang_charge,
/// ground_pound).
bool countsAsAttack(const std::string& behavior);

// ------------------------------------------------------------------ the recorder

class PlayStatsStore;

/// What a run is, for the statistics (GameScene).
struct PlayStatsRun {
    std::string levelId;
    const LevelData* level = nullptr;
    bool editorTest = false;
    /// A section training (« Répéter un segment »): its time only, apart (boss trainings and
    /// practice runs are marked in the world itself).
    bool training = false;
    /// A classic campaign level (not EXTRÊME) played from its start, neither a challenge nor a
    /// training: its segments count (« Niveaux »).
    bool classic = false;
    std::string levelVersion;  // the level's fingerprint (its segments start again with another)
};

/// Follows one run (a GameScene): set as the world's listener, stepped after each step.
class PlayStatsRecorder : public WorldListener {
public:
    enum class Kind { Counted, Training };
    PlayStatsRecorder(PlayStatsStore& store, GameWorld& w, std::string levelId, std::string levelName, Kind kind);
    ~PlayStatsRecorder() override;

    /// Its segments count (« Niveaux »): a classic level from its start.
    void setClassic(std::string version) {
        classic_ = true;
        version_ = std::move(version);
    }
    /// The route of the level (computed off the main thread): until it comes, no slice.
    void setRouteFuture(std::future<LevelProgressTrack> f) { trackFuture_ = std::move(f); }
    void setRoute(LevelProgressTrack t);
    /// After each simulation step.
    void step(const GameWorld& w);
    /// The run ends (left, restarted, finished): what is pending is kept and written. The
    /// world's listener is cleared.
    void finish();

    void playerHurt(const GameWorld& w, const DamageInfo& d, const HurtSource& src) override;
    void playerKilled(const GameWorld& w, const HurtSource& src) override;
    void attackStarted(const GameWorld& w, uint32_t tag, const std::string& power) override;
    void enemyHit(const GameWorld& w, const Enemy& e, uint32_t tag) override;

    bool active() const { return active_; }

private:
    struct ShotWatch {
        bool threat = false, hit = false;
        uint64_t seen = 0;
    };
    struct BlowWatch {
        bool active = false, threat = false, hit = false;
        uint64_t seen = 0;
    };
    struct TagInfo {
        uint32_t tag = 0;
        std::string power;
        bool hit = false;
    };
    struct Fight {
        uint32_t uid = 0;
        std::string bossId, name;
        int phases = 1;
        int phase = 0;
        float removed = 0.0f;
    };
    struct LastBlow {
        HurtOrigin origin = HurtOrigin::Other;
        uint32_t enemy = 0;
        std::string attack;
    };
    void blow(const HurtSource& src, bool fall);
    void resolveShot(const ShotWatch& s);
    void resolveBlow(const BlowWatch& b);
    void trackThreats(const GameWorld& w);
    void trackPosition(const GameWorld& w);
    void trackBoss(const GameWorld& w, bool died);
    void trackSplits(const GameWorld& w);
    void endFight(bool won, bool defeat);
    void endLife(bool died);
    /// What is pending goes into the store and is written.
    void commit();
    std::string fatalName(const Fight& f) const;

    PlayStatsStore& store_;
    GameWorld* world_;
    std::string levelId_, levelName_;
    Kind kind_;
    bool active_ = true;
    bool finished_ = false;
    bool ended_ = false;  // the world completed or failed (the run is over, its results shown)
    uint64_t stepNo_ = 0;
    int lastDeaths_ = 0;
    // Pending (not yet in the store).
    StatsCounters run_;
    std::array<int, playstats::kSlices> slicePasses_{}, sliceDeaths_{};
    std::vector<std::pair<std::string, BossAttemptRec>> attempts_;
    std::vector<std::pair<std::string, std::pair<int, bool>>> phaseMarks_;  // boss, (phase, defeat) — reached when false
    std::map<std::string, std::string> bossNames_;
    std::map<std::string, int> bossPhases_;
    // Position along the route.
    std::future<LevelProgressTrack> trackFuture_;
    LevelProgressTrack track_;
    bool trackReady_ = false;
    size_t hint_ = 0;
    int lastSlice_ = -1;
    std::bitset<playstats::kSlices> life_;
    // Threats.
    std::unordered_map<uint32_t, ShotWatch> shots_;
    std::unordered_map<uint32_t, BlowWatch> blows_;
    // Nova's attacks (the latest tags).
    std::deque<TagInfo> tags_;
    // Segments (classic levels).
    bool classic_ = false;
    std::string version_;
    bool attemptNoted_ = false;
    bool attemptPending_ = false;
    size_t splitCount_ = 0;
    float lastSplit_ = 0.0f;
    std::string lastCp_;
    std::vector<std::string> cps_;
    std::vector<std::pair<std::pair<std::string, std::string>, float>> segTimes_;  // pending
    float finishTime_ = -1.0f;
    // Boss fight.
    std::optional<Fight> fight_;
    LastBlow lastBlow_;
};

// ------------------------------------------------------------------ the store

class PlayStatsStore {
public:
    static constexpr int kVersion = 1;
    PlayStatsStore() = default;
    ~PlayStatsStore();
    PlayStatsStore(const PlayStatsStore&) = delete;
    PlayStatsStore& operator=(const PlayStatsStore&) = delete;

    /// Reads the file (at startup). A damaged file is kept as `<file>.bak`, the statistics start empty.
    void open(const fs::path& file);
    const PlayStatsData& data() const { return data_; }
    bool damaged() const { return damaged_; }
    const fs::path& file() const { return file_; }

    /// « Enregistrer mes statistiques » (Paramètres › Jeu).
    bool enabled = true;
    /// Automated checks only (--stats-day YYYY-MM-DD): the day the runs are counted on.
    int dayOverride = 0;
    int today() const;
    /// The stars of the achievements now (set by GameServices).
    std::function<int()> currentStars;

    /// A run starts: its recorder (set as the world's listener), or nullptr when nothing is
    /// recorded (disabled, an editor test, « Invincible »). The level's route is computed off
    /// the main thread.
    std::unique_ptr<PlayStatsRecorder> beginRun(GameWorld& w, const PlayStatsRun& run, const ContentDatabase& content);

    /// From a recorder: what one attempt (or part of it) adds.
    struct Delta {
        StatsCounters counters;
        std::string levelId, levelName;
        std::array<int, playstats::kSlices> passes{}, deaths{};
        std::vector<std::pair<std::string, BossAttemptRec>> attempts;  // boss id, attempt
        std::map<std::string, std::string> bossNames;
        std::map<std::string, int> bossPhases;
        std::vector<std::pair<std::string, std::pair<int, bool>>> phaseMarks;  // boss, (phase, defeat) — reached when false
        // « Niveaux » (classic level): the segments done, an attempt started, a full run.
        bool classic = false;
        std::string version;
        bool attempt = false;
        std::vector<std::pair<std::pair<std::string, std::string>, float>> segments;
        float finish = -1.0f;
        std::vector<std::string> route;  // a full run: its checkpoints
    };
    void merge(const Delta& d);

    /// Writes on the worker thread (the latest state; several calls in a row write once).
    void saveAsync();
    /// Waits until what was asked is written (the game closing, tests).
    void flush();
    /// « Réinitialiser les statistiques »: everything forgotten, the empty file written.
    void reset();
    /// The file's text for `d` (tests: its size).
    static std::string serialize(const PlayStatsData& d);
    /// Tests: replaces the data.
    void setData(PlayStatsData d) { data_ = std::move(d); }

private:
    void worker();
    PlayStatsData data_;
    fs::path file_;
    bool damaged_ = false;
    bool readOnly_ = false;  // written by a newer game: never overwritten
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<PlayStatsData> pending_;
    bool writing_ = false;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace cosmic
