#pragma once

// Timing polish (docs/TAS.md « Le polissage des timings »): a window of the best run seen as its
// input events — each press and each release of each key, at its frame — and those timings
// optimised against the real game, from savestates:
//
// 1. frame-perfect brute force: each event moved by ±1…±k frames, one at a time (as the brute
//    forcers of TAS do), the events near where the best run loses speed first (a drop of her
//    horizontal speed, a landing, a wall);
// 2. short presses inserted (a jump, a power's key, a direction) just before those drops, and
//    short presses deleted;
// 3. differential evolution over the timings of the events nearest the drops (a real vector,
//    rounded to frames): joint changes no single shift finds.
//
// Fitness: how far along the route Nova is at the window's end (the frame where the best run
// ends its last section searched); an edit that is further is played on to the exit with the
// best run's rest (re-joined, realigned), and a faster finish, played again from the load, is a
// faster run. The frames before the window, those of a cinematic, a skip or a power's answer
// are never changed.

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "tools/tas/Strategy.h"

namespace cosmic::tas {

class Sim;
struct RunStudy;

/// An input event of a run: key `bit` pressed (or released) at `frame`.
struct InputEvent {
    uint32_t bit = 0;
    size_t frame = 0;
    bool press = true;
    size_t lo = 0, hi = 0;  // it may move within [lo, hi] (the same key's events around it)
    float priority = 0.0f;  // the speed lost just after it
};

/// The events of `run` in [from, to) that may move (not in a cinematic, a skip or an answer).
std::vector<InputEvent> inputEvents(const std::vector<InputFrame>& run, const std::vector<uint8_t>& frozen, size_t from, size_t to);
/// `run` with `e` moved by `shift` frames (false: not possible there).
bool shiftEvent(std::vector<InputFrame>& run, const InputEvent& e, int shift, const std::vector<uint8_t>& frozen);
/// The speed the run loses at each frame (horizontal speed dropped, a landing, a wall), from its study.
std::vector<float> speedDrops(const RunStudy& g);

struct TimingStats {
    int64_t tried = 0;
    int shifts = 0, inserts = 0, deletes = 0, evolved = 0;  // gains kept by kind
};

class TimingPolish {
public:
    struct Options {
        int maxShift = 3;       // first pass ±1..±k (the next passes go to 2k)
        int population = 10;    // differential evolution
        int evolveEvents = 8;   // events evolved jointly
    };
    TimingPolish() = default;
    explicit TimingPolish(const Options& o) : opt_(o) {}
    /// Polishes the window of `slice` with one Sim per thread until the slice ends. Returns the
    /// gains kept (each offered through slice.offer).
    TimingStats run(const std::vector<Sim*>& sims, const Slice& slice);
    const TimingStats& total() const { return total_; }

private:
    Options opt_;
    TimingStats total_;
    std::unordered_set<uint64_t> tried_;  // (best run, edit) already tried without a gain
};

}  // namespace cosmic::tas
