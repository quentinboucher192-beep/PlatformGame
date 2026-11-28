#pragma once

// The mind of the TAS (docs/TAS.md « L'IA du TAS »): it watches what the search does and decides,
// slice after slice, what each essayeur (a core) works on — no more one strategy on every core
// at a time: several strategies side by side, each on its own cores. It observes (the places where
// the best run loses speed, what each strategy gained for the time it was given), remembers (the
// configurations already solved: Memory.h), decides, and says why: each decision is a thought
// (Strategy.h Thought), worded by the game in the player's language. Deterministic for a given
// seed and number of cores. Generic: nothing here knows a level.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "tools/tas/Strategy.h"

namespace cosmic::tas {

class Portfolio;

/// What an essayeur does now (the progress file's « workers »).
struct WorkerState {
    std::string arm;       // the strategy (Portfolio ids)
    int first = 0, last = -1;  // the sections it searches (0-based; last < 0: to the exit)
    int64_t attempts = 0;  // attempts it simulated since the training started
};

class Director {
public:
    /// One slice's plan: strategies side by side, each on `threads` cores.
    struct Part {
        int arm = -1;
        int threads = 1;
    };
    struct Plan {
        std::vector<Part> parts;
        bool exclusive() const { return parts.size() == 1; }
    };

    Director(Portfolio& portfolio, int threads);

    /// The plan of the next slice after the beam's part of a round (the refinement, the
    /// exploration, the polish, the trajectories, the strategies registered). `bestClock`: the
    /// best run now; `window`: the sections the round searches.
    Plan decide(float bestClock, int first, int last);
    /// What a slice of a part did (the director learns, the essayeurs' counts).
    void done(const Part& part, float before, float after, double seconds);
    /// The trajectories searched every place of the window on this best run (none left).
    void trajectoriesExhausted(float bestClock);
    /// Attempts simulated by the essayeurs from `firstWorker` (`count` of them).
    void counted(int firstWorker, int count, int64_t n);

    /// A thought (from the director itself or a strategy's slice): to the sink, in order.
    void think(const Thought& t);
    std::function<void(const Thought&)> sink;
    /// The essayeurs now (one per core).
    std::vector<WorkerState> workers() const;
    int threads() const { return threads_; }

private:
    Portfolio& P_;
    int threads_ = 1;
    float trajectoryDoneAt_ = -1.0f;  // the best clock when the trajectories had nothing left
    float lastRefined_ = -1.0f;       // the best clock the refinement last ran on
    int decisions_ = 0;
    std::string lastPlan_;
    mutable std::mutex m_;
    std::vector<WorkerState> workers_;
};

}  // namespace cosmic::tas
