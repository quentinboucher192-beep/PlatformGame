#pragma once

// Branch-and-bound of the search (docs/TAS.md « Séparation et évaluation »): a state whose clock
// plus a LOWER BOUND of the time still needed to reach the next checkpoint cannot beat the best
// known split there is dropped. The bound must stay admissible (never more than the truth):
//
// * horizontally: the gap to the checkpoint's box (or the exit's), minus what a teleport carried
//   can jump at once, over the fastest horizontal speed of the current kit — running, sliding,
//   a wall jump, a knockback, every speed of each power carried (dash, impulse, grapple...), her
//   speed now and the fastest the best run ever went, all with a margin of 25 %;
// * along the route: the distance of the level's geometry (LevelNavigator's field) to the region
//   where the checkpoint counts as passed, scaled down (the model's grid and its reach are coarse),
//   over the fastest speed in any direction (falls and jumps included);
// * a tighter bound from elsewhere (the planner, tools/tas/Planner.*) through `extra`: the larger
//   of two admissible bounds is admissible.

#include <functional>
#include <vector>

namespace cosmic {
class GameWorld;
}

namespace cosmic::tas {

class Course;
struct RunStudy;

class TimeBound {
public:
    TimeBound(const Course& course, const GameWorld& w);
    /// The fastest the best run went (it raises the speeds the bound allows: launchers, winds...).
    void observe(float maxSpeedX, float maxSpeed);
    /// Seconds at least from the world now to the waypoint `wp` (a checkpoint, the exit).
    float toWaypoint(const GameWorld& w, int wp) const;
    /// A tighter admissible bound from elsewhere (seconds; the larger one is used). Generic hook.
    std::function<float(const GameWorld&, int wp)> extra;

    static constexpr float kMargin = 1.25f;   // on every speed
    static constexpr float kFieldScale = 0.7f;  // on the field's distances

private:
    const Course& course_;
    struct Box {
        bool known = false;
        float left = 0.0f, right = 0.0f;
    };
    std::vector<Box> boxes_;  // per waypoint: the x extent where it counts as reached
    float seenX_ = 0.0f, seen_ = 0.0f;
};

/// What may be pruned: the split to beat at each section's end. A state of section `s` is dropped
/// when clock + bound > limit(s), limit(s) = the best run's split there, later by as much as the
/// section started later than the best run's (a run already behind is not condemned), plus `slack`.
struct Pruner {
    const TimeBound* bound = nullptr;
    std::vector<float> split;       // per section: the best run's clock at its end (negative: none)
    std::vector<float> startSplit;  // per section: the best run's clock at its start
    float slack = 0.0f;
    bool active() const { return bound != nullptr && !split.empty(); }
    /// The clock limit of section `s` for a search that started it at `startClock`.
    float limit(size_t s, float startClock) const;
    /// True: the state now (in section `s`, toward waypoint `wp`) cannot beat the limit.
    bool prune(const GameWorld& w, size_t s, int wp, float clock, float startClock) const;
    static Pruner from(const TimeBound& b, const RunStudy& best, float slack);
};

}  // namespace cosmic::tas
