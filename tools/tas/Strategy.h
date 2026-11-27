#pragma once

// A slice of a round given to one strategy of the portfolio (docs/TAS.md « Le portefeuille »):
// the best run now, the frames it must keep, until when, and how to give back a faster run.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"

namespace cosmic::tas {

struct Pruner;

struct Slice {
    std::vector<InputFrame> best;  // the best run now (finishes the category)
    float bestClock = 0.0f;
    size_t keep = 0;               // its first frames stay exactly as they are (a window round)
    int firstSection = 0, lastSection = -1;  // the sections searched (0-based); lastSection < 0: to the exit
    std::chrono::steady_clock::time_point until;
    int threads = 1;
    uint64_t seed = 1;
    const Pruner* pruner = nullptr;  // branch-and-bound (none: nothing pruned)
    std::function<bool()> stopped;   // the search asked to end
    std::function<void(int64_t)> count;  // attempts simulated
    std::function<void(const std::string&)> log;
    /// A faster whole run (from the load, played again by the strategy from the load before):
    /// the search keeps it when it beats its best. Called from the strategy's threads.
    std::function<void(const std::vector<InputFrame>& run, float clock, const std::string& how)> offer;
    bool over() const { return (stopped && stopped()) || std::chrono::steady_clock::now() >= until; }
};

}  // namespace cosmic::tas
