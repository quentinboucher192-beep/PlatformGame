#pragma once

// 2.91 — ONE personal Sum of Best per level version (docs/GHOST_RACING.md § « Une seule SOB »).
//
// Every attempt of the player — a full run, a section training, an enchaînement (several
// segments in a row), the segments crossed whole after a StartPos, and the player's own runs
// verified online — gives the time it took between each two boundaries it crossed (the level's
// start, its checkpoints, its exit). This store keeps, per level version (GhostStore::levelKey)
// and per range (from, to) of boundaries:
//   * the best time, where it was done (its source), when, and the exact replay that holds it
//     (a copy, user folder « ghosts/bests/<level version>/ », kept while a best refers to it);
//   * the number of attempts that crossed it (« passages »).
// A range of one segment of the route is that segment's best: its part of the Sum of Best (the
// diagonal of the enchaînements' grid). A longer range is an enchaînement's record (the cells
// above the diagonal); (start, exit) is the full run. Every screen reads this store: the in-race
// gold splits, « Segments », the training screen, « Regarder », the statistics.
//
// Personal only (this computer): nothing here is ever sent, and the official online records and
// world records never count anything of it (they stay the full verified runs of the server).
//
// The file (saves/bests.json) replaces saves/segments.json (gold segments of the full runs) and
// saves/training.json (best section-training times): both are read once and merged (load()),
// their replays copied here; then the statistics' segment bests (stats.json, which travels with
// the copy of the progression) are merged too (importStats, again after a restoration). A
// damaged file is set aside (".corrupt"); a file written by a newer game is read, never written.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Paths.h"
#include "game/gameplay/BossTraining.h"

namespace cosmic {

struct PlayStatsData;

/// Where a personal best was done.
enum class BestSource : uint8_t { Run, Training, Chain, StartPos, Online };
/// « Partie », « Entraînement », « Enchaînement », « StartPos », « En ligne ».
const char* bestSourceLabel(BestSource s);
/// "run", "practice", "chain", "startpos", "online" (the file).
const char* bestSourceId(BestSource s);
std::optional<BestSource> bestSourceFromId(const std::string& id);

/// The exact replay that holds a best: a file of the store's folder for the level version, the
/// range on its chronometer, and how that attempt started (a section training's checkpoint and
/// powers, a StartPos; the default: the level's own start). Played with ReplayPlayback and
/// `setup` (applySectionTraining) when it is not the level's start.
struct BestReplay {
    std::string file;   // "<name>.cprz" (PersonalBests::replayPath), empty: none
    float start = 0.0f;  // the range on the replay's chronometer
    float end = 0.0f;
    bool fromLevelStart = true;  // the replay starts at the level's start; else with `setup`
    SectionTrainingStart setup;
    bool has() const { return !file.empty(); }
};

/// What an attempt crossed: where it started, the checkpoints it reached (with its chronometer
/// at each, in order) and the exit if it reached it.
struct BestPassage {
    /// LevelStart: the level's start (chronometer 0). Checkpoint: `startCheckpoint` (chronometer 0
    /// there: a section training, an enchaînement). Elsewhere: a StartPos — only what lies between
    /// two boundaries crossed after it counts.
    enum class From : uint8_t { LevelStart, Checkpoint, Elsewhere } from = From::LevelStart;
    std::string startCheckpoint;
    std::vector<std::pair<std::string, float>> checkpoints;  // (id, chronometer), in order
    float exitTime = -1.0f;                                  // the exit reached then (-1: not)
    /// False when the checkpoints of the splits are not known (an old run): only the whole run,
    /// from the start to the exit, is matched.
    bool checkpointsKnown = true;
    std::vector<float> unknownSplits;  // then the run's splits (kept with it as they are)

    /// A run's cumulative splits with the ids of their checkpoints (as many; else none is
    /// matched) and its final time (<= 0: not finished), from `from`.
    static BestPassage ofSplits(const std::vector<float>& splits, const std::vector<std::string>& ids, float exitTime,
                            From from = From::LevelStart, const std::string& startCheckpoint = {});
};

/// One attempt given to the store.
struct BestAttempt {
    BestPassage passage;
    BestSource source = BestSource::Run;
    std::string date;  // FileUtil::nowString()
    /// Its exact replay (encoded), kept as the source of the bests it sets; nullptr: none.
    const std::vector<uint8_t>* replay = nullptr;
    /// How it started when not at the level's start (its replay starts the same way).
    SectionTrainingStart setup;
    /// A verified online run: its record id (never counted twice).
    std::string onlineId;
};

class PersonalBests {
public:
    using Range = std::pair<std::string, std::string>;  // (from, to): "" = the start / the exit
    struct Record {
        float best = -1.0f;   // seconds (-1: none yet)
        int passages = 0;     // attempts that crossed it, every source
        BestSource source = BestSource::Run;
        std::string date;     // of the best
        BestReplay replay;    // of the best
    };
    /// A finished full run (a run of this computer or a verified online run of the player).
    struct Run {
        std::string date;
        std::vector<float> splits;             // cumulative seconds at each checkpoint
        float time = 0.0f;
        std::vector<std::string> checkpoints;  // ids of the splits (empty: unknown)
        BestSource source = BestSource::Run;
    };
    /// A segment of the route: its ends and its record (nullptr: never crossed).
    struct Segment {
        std::string from, to;
        const Record* record = nullptr;
        float best() const { return record ? record->best : -1.0f; }
    };
    /// The Sum of Best after a change of it, or of the PB (YYYY-MM-DD; -1: unknown).
    struct SobPoint {
        std::string day;
        float sob = -1.0f;
        float pb = -1.0f;
    };
    /// What add() changed.
    struct Added {
        std::vector<Range> newBests;  // the ranges whose best this attempt now holds
        float sobBefore = -1.0f, sobAfter = -1.0f;
        bool newPb = false;
    };

    static constexpr int kVersion = 1;
    static constexpr size_t kLevelsKept = 400;
    static constexpr size_t kRangesPerLevel = 2048;  // 62 checkpoints in a row
    static constexpr size_t kRunsKept = 20;
    static constexpr size_t kHistoryKept = 60;
    static constexpr uint64_t kMaxReplayBytes = 256ull << 20;

    /// `file` empty: saves/bests.json, the replays in « ghosts/bests » and the old files
    /// (saves/segments.json with « ghosts/golds », saves/training.json with « ghosts/training »)
    /// merged once. Given (tests): only what is given; `legacy` names the old files.
    struct Legacy {
        fs::path segments, segmentReplays, training, trainingReplays;
    };
    explicit PersonalBests(fs::path file = {}, fs::path replayDir = {}, Legacy legacy = {});
    /// Reads the file, then merges the old files not merged yet (and saves at once when it did:
    /// the old replays are copied, then their folders removed once this file names the copies).
    void load();
    /// Writes the file, then deletes the replays no best refers to any more.
    bool save();

    // ------------------------------------------------------------ writing
    /// Every range the attempt crossed whole (a passage of each; between two checkpoints that
    /// follow each other on the route, when the route is known) — a new best when faster, to the
    /// millisecond (a tie keeps the older one). A full run (from the start to the exit, source Run
    /// or Online) is also kept in the runs and may be the new PB.
    Added add(const std::string& levelKey, const BestAttempt& a);
    /// The route of the level version: its checkpoints in the order a run reaches them (from the
    /// level itself). Learned from the first full runs while it is not given.
    void setRoute(const std::string& levelKey, std::vector<std::string> checkpoints);
    /// The powers chosen to train a range (nullptr: back to the route's ones).
    void setPowers(const std::string& levelKey, const Range& r, const std::vector<std::string>* powers, const std::string& date);
    /// A best without a replay whose time one of the player's kept runs did exactly (to the half
    /// millisecond) between `start` and `end`: that run's replay (`replayFile`, copied once)
    /// becomes its source. True when one took it (save() to keep it).
    struct Adoption {
        Range range;
        float start = 0.0f, end = 0.0f;
    };
    bool adoptReplay(const std::string& levelKey, const fs::path& replayFile, const std::string& date, const std::vector<Adoption>& list);
    /// The statistics' bests (stats.json: full runs and section trainings of the classic levels),
    /// merged: a better time becomes the best (without a replay), the passages never fewer.
    void importStats(const PlayStatsData& stats);
    /// importStats was done once already (the first launch merges the statistics; a restored copy
    /// of the progression merges them again).
    bool statsImported() const;

    // ------------------------------------------------------------ reading
    const Record* record(const std::string& levelKey, const Range& r) const;
    const Record* record(const std::string& levelKey, const std::string& from, const std::string& to) const { return record(levelKey, {from, to}); }
    /// The route (its checkpoints, in order); nullopt when not known yet.
    std::optional<std::vector<std::string>> route(const std::string& levelKey) const;
    /// The segments of the route, in order (start → first checkpoint … last one → exit); empty
    /// when the route is not known.
    std::vector<Segment> segments(const std::string& levelKey) const;
    /// The Sum of Best: the best of every segment of the route added up; -1 when one is unknown.
    float sob(const std::string& levelKey) const;
    /// The sum of the bests of the route's segments between boundaries `from` and `to` (0 = the
    /// start, k = the route's k-th checkpoint, n + 1 = the exit); -1 when one is unknown.
    float sumOfBests(const std::string& levelKey, int from, int to) const;
    /// The best full run (the PB), nullptr when none.
    const Run* pb(const std::string& levelKey) const;
    /// The last full runs, most recent first (nullptr: none).
    const std::vector<Run>* runs(const std::string& levelKey) const;
    std::vector<SobPoint> sobHistory(const std::string& levelKey) const;
    /// The powers chosen to train a range (nullptr: the route's ones).
    const std::vector<std::string>* powers(const std::string& levelKey, const Range& r) const;
    /// The file of a best's replay (empty when none is kept).
    fs::path replayPath(const std::string& levelKey, const std::string& file) const;
    /// Every level version known.
    std::vector<std::string> levels() const;
    bool recovered() const { return recovered_; }
    bool readOnly() const { return readOnly_; }
    const fs::path& file() const { return file_; }

private:
    struct Legacy0 {  // a gold route of saves/segments.json whose checkpoints are not known yet
        std::vector<float> best;
        std::vector<BestReplay> replays;
        std::string date;
    };
    struct Level {
        std::string touched;  // date of the last change (the oldest levels go first)
        bool routeKnown = false;
        bool routeGiven = false;  // by setRoute (else learned from a run)
        std::vector<std::string> route;
        std::map<Range, Record> ranges;
        std::optional<Run> pb;
        std::vector<Run> runs;
        std::vector<SobPoint> history;
        std::map<Range, std::vector<std::string>> powers;
        std::vector<std::string> onlineIds;
        std::optional<Legacy0> legacy;
    };
    Level& level(const std::string& key, const std::string& date);
    /// Offers a time for a range: true when it is the new best (with `replay`).
    bool offer(Level& l, const Range& r, float seconds, BestSource source, const std::string& date, const BestReplay& replay, bool passage);
    void resolveLegacy(Level& l);
    float sobOf(const Level& l) const;
    void noteHistory(Level& l, const std::string& date, float sobBefore, float pbBefore);
    void migrateSegments();
    void migrateTraining();
    void collectReplays();
    std::string keepReplay(const std::string& key, const std::vector<uint8_t>& bytes, const std::string& date) const;

    fs::path file_, replayDir_;
    Legacy legacy_;
    std::map<std::string, Level> levels_;
    std::vector<std::string> migrated_;  // "segments.json", "training.json", "stats.json"
    std::vector<fs::path> legacyDirs_;   // old replay folders to remove once this file is written
    bool recovered_ = false;
    bool readOnly_ = false;
};

}  // namespace cosmic
