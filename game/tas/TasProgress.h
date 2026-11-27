#pragma once

// The progress of a training of the machine's runs (docs/TAS.md « Entraînement depuis
// ADMINISTRATION »): `cosmic_tas --progress <file>` rewrites this small JSON file (atomically)
// about every half second; the game, which launched it as a child process on the computer of an
// administrator, reads it to show the training live. Nothing in it is trusted beyond the screen
// of that administrator: what is published is checked again by the server and by every game.
//
//   {
//     "format": "cosmic.tas.progress",
//     "phase": "searching",          // starting, searching, verifying, stopped, failed
//     "level": "sylvanox_01", "category": "ANY_PERCENT",
//     "elapsedMs": 61250,            // this session: since the process started
//     "explored": 30144,             // attempts simulated this session
//     "seedMs": 86692,               // the run it started from, verified (0: none)
//     "bestMs": 86650,               // the best run verified this session: the seed's or a faster one written (0: none)
//     "candidateMs": 86601,          // a faster run the search holds, not verified yet (0: none)
//     "improvements": 1,             // runs faster than the seed, verified and written
//     "bestElapsedMs": 58000,        // elapsedMs when bestMs was verified (where its point goes on the curve)
//     "bestExplored": 29000,         // explored then
//     "round": 2, "threads": 4,
//     "section": 2, "sections": 4,   // the section searched now (1-based), 0: whole runs refined
//     "focusFirst": 4, "focusLast": 5, // the round's window (absent or 0: the whole level)
//     "checkpoints": [{"id": "o63", "label": "Seuil des Anciens"}, ...],  // CP1 first
//     "sequence": 123,               // +1 at every write
//     "message": "...",              // the last line of the search's log (French, as the tool writes it)
//     "windows": [ { ... } ]         // the last TasAttemptWindow's (at most kMaxWindows, oldest first)
//   }
//
// The attempts of the search are never listed one by one: they are counted in WINDOWS of a few
// seconds (TasAttemptStats, closed every kWindowMs and whenever the section or the round
// changes): how many, how many reached the exit, their best / median / worst clock, and for the
// others the section where they stopped (how many of those broke the category: a death, or a
// blow in « Sans dégât ») and how far along the route they got. The game keeps them with the
// training curve (game/tas/TasCurve.h).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

inline constexpr const char* kTasProgressFormat = "cosmic.tas.progress";

enum class TasPhase { Starting, Searching, Verifying, Stopped, Failed };

const char* tasPhaseId(TasPhase p);
bool tasPhaseFromId(const std::string& id, TasPhase& out);

/// The attempts of a few seconds of a search, counted (never listed one by one, except those that
/// finished: each one a point of the curve's cloud, up to kMaxFinishPoints a window).
struct TasAttemptWindow {
    static constexpr int kMaxSections = 32;  // more sections: the last ones counted with the 32nd
    /// The points of a window: an attempt's time (at the exit, or at a checkpoint) and when it was
    /// counted (ms from the window's start).
    struct Point {
        int64_t atMs = 0;
        int64_t timeMs = 0;
        bool operator==(const Point&) const = default;
    };
    /// Every attempt that finished is a point up to this many a window; beyond, a uniform sample
    /// of that many (its best always in it), said as such (« N points sur M »).
    static constexpr size_t kMaxFinishPoints = 400;
    /// The checkpoints reached: their times sampled, this many a window and a checkpoint (the best always in it).
    static constexpr size_t kCheckpointSample = 32;
    /// The attempts that reached a checkpoint of the level (CP1 first): how many, their times
    /// (the median from the sample: approximate), a sample of them.
    struct Checkpoint {
        int64_t reached = 0;
        int64_t bestMs = 0, medianMs = 0, worstMs = 0;
        std::vector<Point> sample;
        bool operator==(const Checkpoint&) const = default;
    };
    int64_t id = 0;                          // its number in the session (progress file)
    int64_t startMs = 0, endMs = 0;          // the session's elapsed time it covers
    int round = 0;
    int section = 0;   // the section searched (1-based); 0: whole runs refined
    int sections = 0;  // the sections of the level for the category (the size of stopped / broken)
    /// The round's window (docs/TAS.md « Les tours »): the sections it searches (1-based), the
    /// rest following the best run; 0: the whole level (and the windows written before 2.98).
    int focusFirst = 0, focusLast = 0;
    int64_t attempts = 0;
    int64_t finished = 0;                           // the exit reached, the category kept
    int64_t bestMs = 0, medianMs = 0, worstMs = 0;  // their clocks (0: none finished)
    int routeMedian = 0, routeMax = 0;              // ‰ of the route reached by the others
    /// Per section (index 0: section 1): the attempts that did not finish whose furthest point
    /// lies in it, and those of them that broke the category (a death, a blow in « Sans dégât »).
    std::vector<int64_t> stopped, broken;
    std::vector<Point> finishes;            // the finished attempts (all of them up to kMaxFinishPoints)
    std::vector<Checkpoint> checkpoints;    // per checkpoint of the level (empty: none counted)

    int64_t unfinished() const { return attempts - finished; }
    /// `pts` cut to `cap` points: the best (lowest time) kept, the others evenly spread.
    static void thin(std::vector<Point>& pts, size_t cap);
    int64_t brokenTotal() const;
    /// The attempts that reached section k (0-based: k = 0 all of them; k = sections: the exit).
    int64_t reached(int k) const;
    /// Both windows as one (counts added, best and worst kept, the medians weighted: approximate);
    /// its round, section and id are b's.
    static TasAttemptWindow merged(const TasAttemptWindow& a, const TasAttemptWindow& b);

    Json toJson() const;
    /// Nullopt for anything inconsistent: negative counts, more finished than attempts, the
    /// sections' counts not adding up, times out of order, too many sections.
    static std::optional<TasAttemptWindow> fromJson(const Json& j);
};

/// The counter of the attempts of a search (cosmic_tas): add...() from the search's threads (under
/// the caller's lock), take() the window since the last one. The medians come from a sample of
/// kSample values at most (reservoir sampling), the rest is exact.
class TasAttemptStats {
public:
    static constexpr size_t kSample = 256;
    void setSections(int sections);
    int sections() const { return sections_; }
    /// The checkpoints of the level the crossings are counted for (CP1 first), at most kMaxSections.
    void setCheckpoints(int checkpoints);
    /// An attempt that finished: its clock, when it was counted (`atMs`, the session's elapsed time).
    void addFinished(int64_t clockMs, int64_t atMs = 0);
    /// An attempt that did not finish: the section (0-based) of its furthest point, how far along
    /// the route (‰), whether it broke the category.
    void addStopped(int section, int routePermille, bool broke);
    /// An attempt reached the checkpoint `checkpoint` (0-based) with the level's clock at `clockMs`.
    void addCrossing(int checkpoint, int64_t clockMs, int64_t atMs = 0);
    bool empty() const { return attempts_ == 0 && crossings_ == 0; }
    /// What was added since the last take() (its times, round and section left to the caller);
    /// the points placed from `startMs` (the window's start) and within `endMs`.
    TasAttemptWindow take(int64_t startMs = 0, int64_t endMs = INT64_MAX);

private:
    uint64_t next();
    struct Cp {
        int64_t reached = 0, best = 0, worst = 0;
        TasAttemptWindow::Point bestAt;
        std::vector<TasAttemptWindow::Point> sample;
    };
    int sections_ = 0;
    int64_t attempts_ = 0, finished_ = 0, best_ = 0, worst_ = 0, timesSeen_ = 0, routesSeen_ = 0, crossings_ = 0;
    int routeMax_ = 0;
    std::vector<int64_t> stopped_, broken_;
    std::vector<TasAttemptWindow::Point> times_;  // a sample of the finished attempts (all up to kMaxFinishPoints)
    TasAttemptWindow::Point bestAt_;
    std::vector<int> routes_;
    std::vector<Cp> cps_;
    uint64_t rng_ = 0x9E3779B97F4A7C15ull;
};

/// A checkpoint of the level as the trainings count it (the course's order: CP1 first; its id
/// and its label in the level, as the splits name it).
struct TasCheckpoint {
    std::string id, label;
    bool operator==(const TasCheckpoint&) const = default;
};

/// The splits of a run at the level's checkpoints (CP1 first, as TasProgress::checkpoints numbers
/// them): the level's clock in ms at each one, 0 where it was not reached. Read from JSON:
/// nullopt for anything else than such a list (at most kMaxSections, each within 0..timeMs when
/// timeMs > 0, the times reached never going back).
std::optional<std::vector<int64_t>> tasSplitsFromJson(const Json& j, int64_t timeMs = 0);
/// Where two runs differ, segment by segment (docs/TAS.md « Où est le gain »): for each segment
/// (start → CP1, CP1 → CP2… CPn → exit: n + 1 of them), the new run's time in it minus the old
/// one's (ms; negative: faster). Empty when they cannot be compared (not the same checkpoints,
/// one not reached, a time missing).
std::vector<int64_t> tasSegmentDeltas(const std::vector<int64_t>& oldSplits, int64_t oldMs, const std::vector<int64_t>& newSplits, int64_t newMs);
/// The segment's name: « Départ → CP1 », « CP2 → CP3 », « CP7 → Arrivée » (k of `checkpoints`).
std::string tasSegmentName(size_t k, size_t checkpoints);

struct TasProgress {
    static constexpr size_t kMaxWindows = 24;  // the last two minutes
    static constexpr int64_t kWindowMs = 5000;
    TasPhase phase = TasPhase::Starting;
    std::string levelId;
    RunCategory category = RunCategory::AnyPercent;
    int64_t elapsedMs = 0;
    int64_t explored = 0;
    int64_t seedMs = 0;
    int64_t bestMs = 0;
    int64_t candidateMs = 0;
    int improvements = 0;
    int64_t bestElapsedMs = 0;  // when bestMs was verified (elapsedMs then)
    int64_t bestExplored = 0;   // explored then
    int round = 0;
    int threads = 0;
    int section = 0, sections = 0;
    /// The round's window (TasAttemptWindow::focusFirst): 0 the whole level (and older files).
    int focusFirst = 0, focusLast = 0;
    std::vector<TasCheckpoint> checkpoints;  // the level's checkpoints the windows count (CP1 first)
    /// 2.99: the splits of bestMs (tasSplitsFromJson), where its gain came from ("beam", "guide",
    /// "splice", "polish"; empty: the seed run, or before 2.99) and whether a candidate
    /// (candidateMs) is being verified now (cosmic_tas verifies each one at once, on a thread).
    std::vector<int64_t> bestSplits;
    std::string bestSource;
    bool verifying = false;
    /// The players' runs that guide the search (--guide): whose, their time when they finish the
    /// category (0: not), their splits — the references of the curve's checkpoints.
    struct Reference {
        std::string player;
        int64_t timeMs = 0;
        std::vector<int64_t> splits;
        bool operator==(const Reference&) const = default;
    };
    static constexpr size_t kMaxReferences = 8;
    std::vector<Reference> guides;
    int64_t sequence = 0;
    std::string message;
    std::vector<TasAttemptWindow> windows;  // the last ones, oldest first

    /// The process is done (stopped or failed): nothing more will be written.
    bool finished() const { return phase == TasPhase::Stopped || phase == TasPhase::Failed; }
    /// The best time known this session (a run not verified yet counts only once verified).
    int64_t bestKnownMs() const { return bestMs; }

    Json toJson() const;
    /// Nullopt (and why) for anything but a progress file of this format: numbers are clamped to
    /// be non-negative, an unknown phase or category refuses it.
    static std::optional<TasProgress> fromJson(const Json& j, std::string* error = nullptr);
    static std::optional<TasProgress> parse(const std::string& text, std::string* error = nullptr);
};

}  // namespace cosmic
