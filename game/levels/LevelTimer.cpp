#include "game/levels/LevelTimer.h"

#include <cmath>
#include <cstdio>

namespace cosmic {

void LevelTimer::configure(TimerMode mode, float limit, ExpireAction onExpire) {
    mode_ = mode;
    limit_ = limit;
    onExpire_ = onExpire;
    reset();
}

void LevelTimer::reset() {
    elapsedAccum_ = 0.0;
    elapsed_ = 0.0f;
    running_ = false;
    splits_.clear();
}

void LevelTimer::fixedUpdate(float dt) {
    if (!running_) return;
    elapsedAccum_ += static_cast<double>(dt);
    elapsed_ = static_cast<float>(elapsedAccum_);
}

int LevelTimer::split() {
    splits_.push_back(elapsed_);
    return static_cast<int>(splits_.size()) - 1;
}

std::string LevelTimer::format(float seconds, bool centiseconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    int cs = static_cast<int>((seconds - total) * 100.0f);
    int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else if (centiseconds)
        std::snprintf(buf, sizeof(buf), "%d:%02d.%02d", m, s, cs);
    else
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string LevelTimer::formatDelta(float delta) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c%.2f", delta < 0 ? '-' : '+', std::abs(delta));
    return buf;
}

}  // namespace cosmic
