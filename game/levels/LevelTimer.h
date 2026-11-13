#pragma once

#include <string>
#include <vector>

#include "game/levels/LevelData.h"

namespace cosmic {

/// Level chronometer. Advanced only by fixed simulation steps, so the measured time is
/// independent of the frame rate and of pauses / cinematics / loading.
class LevelTimer {
public:
    void configure(TimerMode mode, float limit, ExpireAction onExpire);
    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool running() const { return running_; }
    void reset();

    void fixedUpdate(float dt);

    TimerMode mode() const { return mode_; }
    bool hasLimit() const { return mode_ == TimerMode::Limited || mode_ == TimerMode::Challenge; }
    float elapsed() const { return elapsed_; }
    float remaining() const { return hasLimit() ? std::max(0.0f, limit_ - elapsed_) : 0.0f; }
    float limit() const { return limit_; }
    bool expired() const { return hasLimit() && elapsed_ >= limit_; }
    ExpireAction onExpire() const { return onExpire_; }
    void addTime(float seconds) { limit_ = std::max(1.0f, limit_ + seconds); }

    /// Records a split (checkpoint) and returns its index.
    int split();
    const std::vector<float>& splits() const { return splits_; }

    /// "m:ss.cc" (or "h:mm:ss" beyond an hour).
    static std::string format(float seconds, bool centiseconds = true);
    /// "+1.23" / "-0.50" for split comparisons.
    static std::string formatDelta(float delta);

private:
    TimerMode mode_ = TimerMode::Limited;
    ExpireAction onExpire_ = ExpireAction::GameOver;
    float limit_ = 600.0f;
    double elapsedAccum_ = 0.0;  // double accumulation avoids drift on long runs
    float elapsed_ = 0.0f;
    bool running_ = false;
    std::vector<float> splits_;
};

}  // namespace cosmic
