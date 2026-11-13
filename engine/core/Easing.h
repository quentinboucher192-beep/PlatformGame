#pragma once

#include <string>

namespace cosmic {

/// Easing curves used by tweens, UI transitions and cinematics.
enum class Ease {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    OutBack,
    InBack,
    OutElastic,
    OutBounce,
    InOutSine,
};

float applyEase(Ease ease, float t);
Ease easeFromString(const std::string& name);
const char* easeToString(Ease ease);

/// A single float tween with an easing curve.
struct Tween {
    float from = 0.0f;
    float to = 0.0f;
    float duration = 0.0f;
    float elapsed = 0.0f;
    Ease ease = Ease::OutCubic;

    void start(float f, float t, float d, Ease e = Ease::OutCubic) {
        from = f;
        to = t;
        duration = d;
        elapsed = 0.0f;
        ease = e;
    }
    void update(float dt) { elapsed += dt; }
    bool done() const { return elapsed >= duration; }
    float progress() const { return duration <= 0.0f ? 1.0f : (elapsed >= duration ? 1.0f : elapsed / duration); }
    float value() const { return from + (to - from) * applyEase(ease, progress()); }
};

}  // namespace cosmic
