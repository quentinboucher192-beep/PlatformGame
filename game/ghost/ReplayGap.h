#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/MeasurePoints.h"
#include "game/ghost/ReplayCompare.h"
#include "game/online/Replay.h"

namespace cosmic {

// The gap of a replay to a reference, all along the run (replay viewer and « Comparer deux
// replays », 2.91): in time (the Ghost Racing estimate, exact at the checkpoints) and in
// distance along the level's route (1 m = 1 tile, 16 px: a convention, the game has no unit of
// length). Everything comes from what the replays recorded (their trajectories at 30 Hz and
// their splits to the millisecond), never from an estimate made up for the display. Display
// only: nothing here touches a simulation, a record or the statistics.

// 1 m = 1 tile = 16 px: kPixelsPerMeter, metersOf (game/ghost/MeasurePoints.h).

/// Where along the level's route `feet` stand: px from the start, projected on the route's
/// segments (not only its points). -1 when off the route (more than `maxOff` px away: a fall,
/// a detour), as the Ghost Racing maps a run. `hint` keeps the search local (LevelProgressTrack::project).
float routeDistanceAt(const LevelProgressTrack& route, Vec2 feet, size_t& hint, float maxOff = 200.0f);

/// A run along the level's route: at each sample of its recorded trajectory, the furthest
/// distance along the route it had reached (px), and when. Monotonic both ways, so that « when
/// did it first get there » and « how far had it got by then » answer each other.
class RouteRun {
public:
    /// From a replay's recorded trajectory (feet sampled on the level timer). A finished run
    /// reaches the exit (the route's end) at its final time.
    static RouteRun fromReplay(const ReplayData& r, const LevelProgressTrack& route);
    /// From samples already measured: `times` increasing, `reach` (px) of the same size; made
    /// monotonic here.
    static RouteRun fromSamples(std::vector<double> times, std::vector<float> reach, double duration, float routeLength);

    bool valid() const { return times_.size() >= 2 && length_ > 1.0f; }
    double duration() const { return duration_; }
    float length() const { return length_; }
    /// The furthest distance (px) reached `t` seconds after its start (its first sample's before).
    float reachAt(double t) const;
    /// When it first reached `d` px along the route (seconds; -1: never).
    double timeAt(float d) const;
    const std::vector<double>& times() const { return times_; }
    const std::vector<float>& reach() const { return reach_; }

private:
    std::vector<double> times_;
    std::vector<float> reach_;
    double duration_ = 0.0;
    float length_ = 0.0f;
};

/// What a run is compared with: a whole run (the record, another replay, B in a comparison)
/// or the Sum of Best (its best segments end to end).
struct GapReference {
    /// Its way along the route. Invalid: the gap is known at the checkpoints only (the Sum of
    /// Best when a best segment has no replay on this computer).
    RouteRun run;
    /// Its own time at each checkpoint of the compared run (ms, exact), -1 where it did not pass
    /// the same checkpoint (matched by position or by id, never by order alone).
    std::vector<int64_t> atSplitsMs;
    /// Its time at the exit (ms), -1 unknown.
    int64_t finishMs = -1;
    /// Shown that much later than it ran (B's offset in a comparison): every gap includes it.
    int64_t offsetMs = 0;
};

/// The gap at one instant of the compared run.
struct GapSample {
    double t = 0.0;            // the compared run's time (s)
    bool hasTime = false;
    int64_t timeMs = 0;        // its time minus the reference's to the same place (+: behind)
    bool hasDistance = false;
    float metres = 0.0f;       // its distance along the route minus the reference's at that instant (-: behind)
    bool exact = false;        // at a checkpoint or at the exit: both times to the millisecond
    int split = -1;            // exact: the run's checkpoint (its split index; the exit: the split count)
};

/// The gap all along the run.
struct GapCurve {
    std::vector<GapSample> samples;  // in time order
    bool continuous = false;         // false: known at the checkpoints only
    double end = 0.0;                // the last instant compared (s)
    int64_t maxTimeMs = 0;           // largest |time gap| (scale)
    float maxMetres = 0.0f;          // largest |distance gap|
    bool empty() const { return samples.empty(); }
    /// The exact sample at the run's checkpoint `split` (the exit: the split count), nullptr if none.
    const GapSample* atSplit(int split) const;
};

/// The compared run: its way along the route, its splits (ms) and its final time (ms).
struct GapRun {
    const RouteRun* run = nullptr;
    std::vector<int64_t> splitsMs;
    int64_t durationMs = 0;
    /// It reached the exit (its final time compares exactly); else it stopped where it was (an
    /// attempt of an extreme level's race).
    bool finished = true;
};

/// The gap at the run's time `t` (the live counter): the route estimate between the
/// checkpoints, exact at a checkpoint (within half a simulation step) and once the run has
/// finished. Checkpoints only (no way along the route for the reference): the gap at the last
/// checkpoint passed by `t` (its `t` is that checkpoint's time).
GapSample gapAt(const GapRun& run, const GapReference& ref, double t);
/// The whole curve from 0 to `end` (the run's time; a comparison goes on while B runs): one
/// sample every 1/30 s at most (`maxSamples` in all), plus every checkpoint and the exit, exact.
GapCurve gapCurve(const GapRun& run, const GapReference& ref, double end, int maxSamples = 3000);

/// One segment of the Sum of Best.
struct SobPiece {
    double duration = -1.0;          // its best duration (s), -1 unknown
    const RouteRun* run = nullptr;   // the run (or training attempt) that did it, nullptr: no replay here
    double start = 0.0, end = 0.0;   // the segment on that run's clock (s)
};
/// The Sum of Best as one run: its best segments end to end.
struct SobRun {
    /// Valid only when every segment has its replay: then the gap is continuous.
    RouteRun run;
    /// The sum at each boundary (ms): 0 at the start, then each checkpoint, then the exit; -1
    /// from an unknown segment on.
    std::vector<int64_t> boundaryMs;
    int withReplay = 0;
};
/// `boundaryReach`: the route distance (px) of each boundary (pieces + 1 values: the start, the
/// checkpoints in order, the exit).
SobRun buildSobRun(const std::vector<SobPiece>& pieces, const std::vector<float>& boundaryReach, float routeLength);

/// The Sum of Best's time (ms) at each checkpoint of a run that passed `runSplits` of them:
/// matched by the checkpoints' ids when both are known (`runCheckpoints`: one per split;
/// `sobCheckpoints`: the Sum of Best's route, one per boundary between two segments), else by
/// order when the run went through exactly as many checkpoints as that route; -1 elsewhere.
std::vector<int64_t> sobAtSplits(const SobRun& sob, size_t runSplits, const std::vector<std::string>& runCheckpoints,
                                 const std::vector<std::string>& sobCheckpoints);

/// B of a comparison as A's reference: its checkpoints matched by position (the comparison's
/// rows), its final time and its offset.
GapReference gapReferenceOf(const ReplayComparison& cmp, RouteRun bRun);
/// A of a comparison as the compared run (`aRun`: its way along the route, kept by the caller).
GapRun gapRunOf(const CompareRun& a, const RouteRun* aRun);

/// One pass from a measure point A to its B (2.91, game/ghost/MeasurePoints.h) on a replay's own
/// clock: when it crossed A and B (interpolated on its recorded trajectory, about 1/30 s).
struct MeasurePass {
    double a = 0.0;
    double b = -1.0;  // -1: still between the two at the end of the replay
    bool done() const { return b >= a; }
    double time() const { return b - a; }
};
/// Every pass of a replay's recorded trajectory through A then B (MeasureTimer: either way, a
/// jump of the trajectory — a respawn, a teleport — stops the chronometer). Read only: a time
/// measured in a replay never goes to the measure store nor to the statistics.
std::vector<MeasurePass> measurePasses(const ReplayData& r, const MeasureGate& a, const MeasureGate& b);

/// The same one position at a time (an exact replay played step by step: A's passes timed on
/// the simulation itself, 1/120 s, not on its 30 Hz trajectory).
class MeasurePassTimer {
public:
    MeasurePassTimer() = default;
    MeasurePassTimer(const MeasureGate& a, const MeasureGate& b) : timer_(a, b) {}
    /// The next position (on a clock that never goes back); true with `out` when a pass from A
    /// to B ends with this move.
    bool feed(double t, Vec2 feet, MeasurePass& out);
    /// A jump in the stream (a seek): nothing is crossed across it.
    void reset();
    /// When A was crossed for the pass going on (-1: none).
    double openSince() const { return start_; }

private:
    MeasureTimer timer_;
    double start_ = -1.0;
};

/// "6,8 m" (one decimal, French comma, never "-0,0").
std::string formatGapMetres(float metres);

}  // namespace cosmic
