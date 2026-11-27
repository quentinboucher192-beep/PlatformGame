#pragma once

// Go-Explore (docs/TAS.md « L'exploration (Go-Explore) »): an archive of the places and
// situations Nova has reached, each with the fastest way known to reach it, explored again and
// again from the most promising ones. It does not prune on « the distance to the next waypoint »
// as the beam does: a cell is kept for being NEW (another place, speed, power or world change) or
// for being reached EARLIER, so other routes and skips survive until they pay.
//
// * A cell: Nova's place (a grid of `cellPx`), her speed (coarse), on the ground, the jumps and
//   dash left, her energy (a band), her hearts, the powers carried and which are ready, the
//   waypoint, what the world did (milestones). It keeps the fastest way known to reach it: its
//   inputs from the load (a tree shared by the cells) and, while the memory allows, its savestate.
// * The choice of a cell, weighted by its promise: ahead of the best run at the same progress
//   (its clock against the best run's when it got that far), rarely chosen, recently improved.
// * Going back: its savestate (or its nearest ancestor's and the frames after it); exploring:
//   a few random moves of a player (and moves offered from elsewhere: `proposals`, the planner).
// * A section's end reached EARLIER than the best run: the rest of the best run played on from
//   there (re-joined, realigned); a faster finish, played again from the load, is a faster run.
// * The archive lives as long as the process (round after round), one per thread (savestates
//   belong to their arena), bounded: `maxCells` cells and `maxStates` savestates per thread.
// * Branch-and-bound (Bound.h): a state that cannot reach the section's end before the best
//   run's split is not kept.
// * Route hints (optional, `routeHints`): places with times from elsewhere (a player's ghost, the
//   guides) — the cells near that path are chosen more often, the more so when they are there
//   as early as the hint or earlier (its checkpoint times are soft targets). They only SEED the
//   exploration: their favour is halved every 1 500 visits, while novelty (cells rarely chosen,
//   new or improved) and the lead on the best run keep choosing, off the hinted path too — a
//   player's route is a start, not the limit. Without them, the archive finds its routes alone.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"
#include "tools/tas/Strategy.h"
#include "tools/tas/RouteHint.h"

namespace cosmic {
class GameWorld;
}

namespace cosmic::tas {

class Sim;

/// A place of a route from elsewhere and the level's clock there (a ghost's trajectory).
struct ExploreStats {
    int64_t visits = 0, moves = 0;
    int64_t cellsNew = 0, cellsFaster = 0, pruned = 0;
    int ahead = 0;     // section ends reached earlier than the best run
    int rejoined = 0;  // ... that finished with the best run's rest
    int gains = 0;     // faster runs found
};

class GoExplore {
public:
    struct Options {
        float cellPx = 24.0f;
        size_t maxCells = 6000;   // per thread
        size_t maxStates = 140;   // savestates per thread (1.3 MB each on sylvanox_01)
        int movesPerVisit = 8;
    };
    GoExplore();
    explicit GoExplore(const Options& o);
    ~GoExplore();
    GoExplore(const GoExplore&) = delete;
    GoExplore& operator=(const GoExplore&) = delete;

    /// Explores the window of `slice` with one Sim per thread (the same Sims every call: the
    /// archives keep their savestates) until the slice ends.
    ExploreStats run(const std::vector<Sim*>& sims, const Slice& slice);
    /// Moves offered from elsewhere (the planner): input sequences from the world now toward the
    /// waypoint `wp`. Generic hook; empty: random moves only.
    std::function<std::vector<std::vector<InputFrame>>(const GameWorld&, int wp)> proposals;
    /// A route from elsewhere (optional): favoured cells near it (within `hintRadius` px).
    std::vector<RouteHint> routeHints;
    float hintRadius = 64.0f;

    size_t cells() const;
    const ExploreStats& total() const { return total_; }

    struct Archive;

private:
    Options opt_;
    std::vector<std::unique_ptr<Archive>> archives_;
    ExploreStats total_;
};

}  // namespace cosmic::tas
