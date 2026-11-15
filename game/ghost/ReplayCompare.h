#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/gameplay/LevelProgress.h"
#include "game/ghost/Ghost.h"

namespace cosmic {

/// Checkpoint splits of a replay in milliseconds: exact with the Split events (2.18+),
/// else estimated from the checkpoint events (older replays, about 1/30 s).
std::vector<int64_t> replaySplitsMs(const ReplayData& r, bool* exact = nullptr);

/// "1:02,358" (milliseconds; "1:02:03,040" beyond an hour). Negative values: "-0:01,250".
std::string formatRunMs(int64_t ms);
/// "+1,250 s" (unit) or "+1,250"; "0,000 s" when zero; "+1:15,500" beyond a minute.
std::string formatDeltaMs(int64_t ms, bool unit = true);
/// What the comparison says of B's time shift: "décalage +1,250 s" or "sans décalage".
std::string formatCompareOffset(int64_t ms);
/// Reads an offset typed by the player: "+1,25", "-0.5 s", "250 ms", "1:02,5" (seconds
/// by default, comma or point). nullopt when it is not a time.
std::optional<int64_t> parseCompareOffset(const std::string& text);

/// One of the two replays compared: its trajectory (recorded positions, interpolated) and
/// its splits to the millisecond. Never a re-simulation by itself (see ReplayPlayback).
struct CompareRun {
    GhostMeta meta;
    GhostTrack track;
    std::vector<int64_t> splitsMs;
    bool splitsExact = false;
    int64_t durationMs = 0;
    std::string levelHash;

    static CompareRun fromReplay(const ReplayData& r, GhostMeta meta);
    float duration() const { return static_cast<float>(durationMs) / 1000.0f; }
};

/// Two replays of the same level side by side, B shifted in time on A's clock.
///
/// The offset (`offsetMs`, B's time shift) is what the player sets to make the ghosts
/// meet again: with +1 250 ms, B is shown 1,25 s later than it ran, so at A's time t, B is
/// where it was at t - 1,25 s. Every gap shown takes it into account and says so. Splits
/// are matched by position (a checkpoint one of them skipped does not shift the others);
/// durations, splits and gaps are computed in whole milliseconds, so an alignment on a
/// checkpoint gives exactly 0 there.
///
/// Display only: nothing here touches a simulation, a record or the server.
class ReplayComparison {
public:
    /// One checkpoint of A (in A's order) and the exit, with B's matching split.
    struct Row {
        int aSplit = -1;    // A's split index (-1: the exit row)
        int bSplit = -1;    // B's matching split (-1: B did not pass this checkpoint)
        bool exit = false;
        int64_t aSegMs = -1, bSegMs = -1;  // segment durations (-1: unknown)
        int64_t aCumMs = -1;               // A's split time
        int64_t bCumMs = -1;               // B's split time on A's clock (offset included)
        bool hasSegDiff = false;
        int64_t segDiffMs = 0;  // A - B on this segment (positive: A slower)
        bool hasGap = false;
        int64_t gapMs = 0;      // A - (B + offset) at this checkpoint
        int faster = 0;         // 1: A faster on this segment, 2: B, 0: tie or unknown
    };
    struct Totals {
        int64_t aMs = 0, bMs = 0;
        int64_t diffMs = 0;  // A - B (durations, no offset)
        int64_t gapMs = 0;   // A - (B + offset) at the exit
        int faster = 0;
    };

    void set(CompareRun a, CompareRun b);
    /// Maps both runs on the level's route (live gap). Without a route, only the checkpoint
    /// gaps compare.
    void setRoute(const LevelProgressTrack& track);
    bool ready() const { return ready_; }
    const CompareRun& a() const { return a_; }
    const CompareRun& b() const { return b_; }
    /// Same version of the level (an official comparison); else "autre version".
    bool sameVersion() const { return a_.levelHash == b_.levelHash; }

    // ------------------------------------------------------------ offset
    int64_t offsetMs() const { return offsetMs_; }
    /// Clamped so that the two runs still meet (± both durations).
    void setOffsetMs(int64_t ms);
    void nudge(int64_t ms) { setOffsetMs(offsetMs_ + ms); }
    void resetOffset() { setOffsetMs(0); }
    int64_t maxOffsetMs() const;
    /// B passes row `row`'s checkpoint (or the exit) at the same moment as A. False when
    /// B did not pass it (nothing changes).
    bool alignOn(int row);
    /// The row the offset was last aligned on (-1 once changed by hand).
    int alignedRow() const { return alignedRow_; }

    // ------------------------------------------------------------- clock
    /// B's own run time when A's clock reads `aTime` seconds.
    double bTime(double aTime) const { return aTime - static_cast<double>(offsetMs_) / 1000.0; }
    /// Until both have finished, on A's clock.
    double endTime() const;

    // -------------------------------------------------------------- gaps
    const std::vector<Row>& rows() const { return rows_; }
    const Totals& totals() const { return totals_; }
    /// Live gap at A's time `aTime`, in ms: A's time minus B's (offset included) to reach
    /// the furthest point of the route A had reached (the Ghost Racing gap). Exact once
    /// both have finished. nullopt before the start or without a route.
    std::optional<int64_t> liveGapMs(double aTime) const;
    /// Row whose checkpoint A passed last at `aTime` (-1 before the first).
    int rowAt(double aTime) const;

private:
    void rebuild();

    CompareRun a_, b_;
    bool ready_ = false;
    int64_t offsetMs_ = 0;
    int alignedRow_ = -1;
    std::vector<Row> rows_;
    Totals totals_;
};

}  // namespace cosmic
