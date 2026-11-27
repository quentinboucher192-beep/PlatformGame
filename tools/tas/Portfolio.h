#pragma once

// The portfolio of the search's strategies (docs/TAS.md « Le portefeuille »): the beam, Go-Explore,
// the timing polish, the refinement, and those registered from elsewhere (the planner's proposals,
// the guides' splices) are the ARMS of a bandit. A round is cut into slices; each slice goes, with
// all the threads, to the arm the bandit draws: Thompson sampling of each arm's rate of gain
// (milliseconds gained per CPU-second, recent slices weighing more), with a FLOOR (no arm gets
// less than its share of the time), so that the time follows what improves without starving the
// others. Lives as long as the process (round after round), with what each arm found.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/core/Random.h"
#include "game/online/OnlineTypes.h"
#include "tools/tas/Strategy.h"

namespace cosmic {
class ContentDatabase;
class GameWorld;
struct LevelData;
}  // namespace cosmic

namespace cosmic::tas {

class Course;
class Sim;
class GoExplore;
class TimingPolish;

/// The bandit alone (the tests drive it with arms of their own).
class Bandit {
public:
    struct Arm {
        std::string id;
        bool enabled = true;
        double cpu = 0.0;     // CPU-seconds given (wall time x threads)
        double gainMs = 0.0;  // milliseconds gained
        int pulls = 0, wins = 0;
        double recentCpu = 0.0, recentGain = 0.0;  // discounted at each slice
        double floor = -1.0;                        // its own floor (negative: the bandit's)
    };
    explicit Bandit(uint64_t seed = 1, double floor = 0.08, double discount = 0.9);
    int add(const std::string& id);
    void enable(int arm, bool on) { arms_[static_cast<size_t>(arm)].enabled = on; }
    /// A floor of its own for `arm` (the exploration keeps a larger one: its gains come rarely).
    void setFloor(int arm, double floor) { arms_[static_cast<size_t>(arm)].floor = floor; }
    /// The arm of the next slice: one under its floor first (the least served), else the
    /// largest rate drawn from each arm's posterior (gamma: gains as events over the CPU time).
    int pick();
    /// What a slice of `arm` cost and gained.
    void record(int arm, double cpuSeconds, double gainMs);
    /// Its share of all the CPU time given.
    double share(int arm) const;
    const std::vector<Arm>& arms() const { return arms_; }
    int find(const std::string& id) const;

private:
    double gamma(double shape);  // a Gamma(shape, 1) variate
    std::vector<Arm> arms_;
    Random rng_;
    double floor_, discount_;
};

/// One faster run found: when, by which arm, from what to what.
struct FoundGain {
    int64_t atMs = 0;  // since the portfolio started
    std::string arm;
    float from = 0.0f, to = 0.0f;
};

class Portfolio {
public:
    /// The built-in arms; others are registered with addArm().
    static constexpr const char* kBeam = "beam";
    static constexpr const char* kExplore = "explore";
    static constexpr const char* kTiming = "timing";
    static constexpr const char* kRefine = "refine";

    Portfolio(const ContentDatabase& content, const LevelData& level, RunCategory category, int threads, uint64_t seed);
    ~Portfolio();
    Portfolio(const Portfolio&) = delete;
    Portfolio& operator=(const Portfolio&) = delete;

    /// An arm from elsewhere (the planner, the guides): it runs a slice; its gains through slice.offer.
    int addArm(const std::string& id, std::function<void(const Slice&)> run);
    /// Only these arms (ids; empty: all). The others are never drawn.
    void only(const std::vector<std::string>& ids);
    Bandit& bandit() { return bandit_; }
    /// The arm of the next slice: during the beam's part of a round (`beamPart`: every arm but the
    /// refinement), or after it (every arm but the beam). -1: none allowed.
    int pick(bool beamPart);
    const std::string& id(int arm) const { return bandit_.arms()[static_cast<size_t>(arm)].id; }
    double sliceSeconds = 45.0;
    /// Branch-and-bound (Bound.h) in the beam and the exploration, and its slack (s).
    bool branchAndBound = true;
    float boundSlack = 0.0f;
    /// A tighter admissible lower bound from elsewhere (the planner): seconds from the world to a waypoint.
    std::function<float(const GameWorld&, int wp)> lowerBound;

    /// Runs a slice of a non-beam arm that the portfolio itself plays (explore, timing, or one
    /// registered): false when the arm is the search's own (beam, refine).
    bool runSlice(int arm, const Slice& slice);
    /// What the slice of `arm` gained (ms) and cost (s x threads): the bandit learns it.
    void record(int arm, double cpuSeconds, float before, float after);
    /// A faster run found by `arm` (the progress file shows who found each gain).
    void credit(const std::string& arm, float from, float to);
    std::vector<FoundGain> gains() const;
    /// The log's summary: each arm's share of the time and its gains.
    std::string summary() const;

    GoExplore& explore();
    TimingPolish& timing();
    const Course& course() const { return *course_; }

private:
    std::vector<Sim*> sims(int n);
    const ContentDatabase& content_;
    const LevelData& level_;
    RunCategory category_;
    std::unique_ptr<Course> course_;
    std::vector<std::unique_ptr<Sim>> sims_;
    std::unique_ptr<GoExplore> explore_;
    std::unique_ptr<TimingPolish> timing_;
    Bandit bandit_;
    std::vector<std::function<void(const Slice&)>> extra_;  // per arm (empty for the built-in ones)
    std::vector<std::string> only_;                         // the arms allowed (empty: all)
    mutable std::mutex m_;
    std::vector<FoundGain> gains_;
    int64_t began_ = 0;
};

/// The French name of an arm (tasStrategyLabel: the log, the progress file's readers).
std::string strategyLabel(const std::string& id);

}  // namespace cosmic::tas
