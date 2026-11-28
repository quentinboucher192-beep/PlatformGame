#pragma once

// Local trajectory optimizer (docs/TAS.md « Les trajectoires »): along the best run, at each place
// where Nova leaves the ground or loses speed (her horizontal speed drops, a wall, a turn), the
// savestate a little before, and from it, in the REAL game (savestates, deterministic), the
// variations a player has there:
//
// * the jump: its frame, how long it is held (its height, the jump being variable);
// * the second press after it is let go (the double jump in the air, the wall jump against a
//   wall: the game decides): its frame and how long it is held (its height too);
// * the run direction kept, or let go in the air while faster than a run (a slide's speed kept);
//   a slide just before the jump; the drop through a one-way platform; the fast fall;
// * the key of each power carried there (its frame, the direction held, how long), within the
//   energy she has.
//
// First a grid (a tree of savestates: the variations share their common beginning), then the best
// ones refined frame by frame on every parameter, then the extras on them. Pruned by time: a
// variation behind the best run at equal progress (its clock where the best run reached the same
// progress), or that can no longer reach the best run's path before it, is dropped. A variation that
// reaches a LATER point of the best run EARLIER (the same place, speed and state) re-joins it: the
// best run's rest played on from there; a faster finish, played again from the load, is offered
// like the other arms' runs (Slice::offer), then verified by the normal replay playback.
//
// Deterministic: the spots in a fixed order (the most time lost first), the variations of a spot
// shared among the threads by index and their results merged in a fixed order — the same runs for
// any number of threads. Spots already searched on the same run are not searched again (memo).

#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "engine/core/Math.h"
#include "tools/tas/Strategy.h"

namespace cosmic::tas {

class Sim;
class TrajectoryMemory;

/// A variation of what a player does from a savestate of the best run (frames counted from there).
struct Maneuver {
    static constexpr int16_t kHeld = 30000;  // a key never let go (until the variation ends)
    int8_t dir = 1;             // the run direction held: +1 right, -1 left
    bool momentum = false;      // the direction let go in the air while faster than a run
    bool fastFall = false;      // Down held while falling after the last jump is let go
    bool drop = false;          // the first press with Down: through a one-way platform
    int16_t slideAt = -1;       // Down pressed on the ground (a slide) at this frame
    int16_t pressAt = -1;       // the first jump press (-1: none)
    int16_t hold = kHeld;       // how long it is held (its height)
    int16_t gap = -1;           // frames between its release and the second press (-1: none)
    int16_t hold2 = kHeld;      // how long the second press is held (its height)
    int8_t key = -1;            // a power's key (an Action), pressed at keyAt for keyHold frames
    int8_t keyX = 0, keyY = 0;  // the direction held when it is pressed
    int16_t keyAt = -1, keyHold = 1;

    /// The frame of the second press (-1: none).
    int secondAt() const { return pressAt >= 0 && hold < kHeld && gap >= 0 ? pressAt + hold + gap : -1; }
    /// The last frame where a key of the variation changes (its decisions are all taken after it).
    int lastDecision() const;
    /// The first frame where it differs from running in its direction.
    int firstDecision() const;
    /// In French, for the log: « saut à +12 tenu 30 images, second appui 5 images après, tenu 18 ».
    std::string describe() const;
    auto tie() const { return std::tuple(dir, momentum, fastFall, drop, slideAt, pressAt, hold, gap, hold2, key, keyX, keyY, keyAt, keyHold); }
    bool operator<(const Maneuver& o) const { return tie() < o.tie(); }
    bool operator==(const Maneuver& o) const { return tie() == o.tie(); }
};

/// What the optimizer did at one place of the best run.
struct TrajectorySpot {
    size_t start = 0;            // the best run's frame where the variations start (from the load)
    float clock = 0.0f;          // its clock there
    Vec2 feet;                   // Nova there
    float loss = 0.0f;           // frames the best run loses after it to a run at full speed (priority)
    int64_t tried = 0;           // variations played
    bool joined = false;         // one reached a later point of the best run earlier
    Maneuver best;               // the variation with the largest lead (else the furthest ahead)
    int lead = 0;                // frames ahead of the best run where it re-joins it
    size_t joinFrame = 0, joinAt = 0;  // its frame (from the load), the best run's frame it joins
    float before = 0.0f, after = 0.0f;  // the whole run's clock before, after (after < before: faster)
    size_t leadThrough = 0;      // a join that does not carry to the exit: the sections it keeps a lead through
    float leadSeconds = 0.0f;    // ...and its lead at the end of the last of them
};

struct TrajectoryStats {
    int64_t tried = 0, steps = 0, rejoins = 0;
    int spots = 0, joins = 0, gains = 0, leads = 0, recalled = 0, learned = 0;
};

class TrajectoryOptimizer {
public:
    struct Options {
        int lookback = 48;      // frames before a takeoff (or a loss) where the variations start
        int pressRange = 72;    // the first press within this many frames of the start
        int horizon = 360;      // frames from the start in which the variations play
        float behind = 0.25f;   // s: further behind the best run at equal progress, a variation is dropped
        int seeds = 3;          // the best variations of the grid refined frame by frame
        int rejoins = 4;        // re-joins tried per spot (the largest leads first)
        float fromClock = 0.0f, toClock = 1.0e9f;  // only the spots whose start is within (the clock, s)
        TrajectoryMemory* memory = nullptr;  // the configurations already solved (Memory.h; none: not used)
        std::string level;                   // where a configuration is found (the memory's note)
    };
    TrajectoryOptimizer() = default;
    explicit TrajectoryOptimizer(const Options& o) : opt_(o) {}
    /// Searches the spots of the slice's window with one Sim per thread until the slice ends or
    /// every spot is searched; each faster run is offered through slice.offer.
    TrajectoryStats run(const std::vector<Sim*>& sims, const Slice& slice);
    const TrajectoryStats& total() const { return total_; }
    /// Every spot searched, in order (the CLI's report, the tests).
    const std::vector<TrajectorySpot>& spots() const { return spots_; }
    Options& options() { return opt_; }
    /// The last run() ended because every spot of its window had been searched on its best run.
    bool exhausted() const { return exhausted_; }

private:
    Options opt_;
    TrajectoryStats total_;
    std::unordered_set<uint64_t> done_;  // spots searched (the run's frames up to the spot's end)
    std::vector<TrajectorySpot> spots_;
    bool exhausted_ = false;
};

}  // namespace cosmic::tas
