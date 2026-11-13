#include "engine/core/Easing.h"

#include <cmath>

#include "engine/core/Math.h"

namespace cosmic {

float applyEase(Ease ease, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    switch (ease) {
        case Ease::Linear: return t;
        case Ease::InQuad: return t * t;
        case Ease::OutQuad: return 1.0f - (1.0f - t) * (1.0f - t);
        case Ease::InOutQuad: return t < 0.5f ? 2 * t * t : 1.0f - std::pow(-2 * t + 2, 2.0f) / 2;
        case Ease::InCubic: return t * t * t;
        case Ease::OutCubic: return 1.0f - std::pow(1.0f - t, 3.0f);
        case Ease::InOutCubic: return t < 0.5f ? 4 * t * t * t : 1.0f - std::pow(-2 * t + 2, 3.0f) / 2;
        case Ease::OutBack: {
            const float c1 = 1.70158f, c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        }
        case Ease::InBack: {
            const float c1 = 1.70158f, c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }
        case Ease::OutElastic: {
            const float c4 = (2.0f * kPi) / 3.0f;
            return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        case Ease::OutBounce: {
            const float n1 = 7.5625f, d1 = 2.75f;
            if (t < 1 / d1) return n1 * t * t;
            if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
            if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
            t -= 2.625f / d1;
            return n1 * t * t + 0.984375f;
        }
        case Ease::InOutSine: return -(std::cos(kPi * t) - 1.0f) / 2.0f;
    }
    return t;
}

namespace {
struct EaseName {
    Ease ease;
    const char* name;
};
constexpr EaseName kNames[] = {
    {Ease::Linear, "linear"},       {Ease::InQuad, "inQuad"},       {Ease::OutQuad, "outQuad"},
    {Ease::InOutQuad, "inOutQuad"}, {Ease::InCubic, "inCubic"},     {Ease::OutCubic, "outCubic"},
    {Ease::InOutCubic, "inOutCubic"}, {Ease::OutBack, "outBack"},   {Ease::InBack, "inBack"},
    {Ease::OutElastic, "outElastic"}, {Ease::OutBounce, "outBounce"}, {Ease::InOutSine, "inOutSine"},
};
}  // namespace

Ease easeFromString(const std::string& name) {
    for (const auto& e : kNames)
        if (name == e.name) return e.ease;
    return Ease::InOutCubic;
}

const char* easeToString(Ease ease) {
    for (const auto& e : kNames)
        if (e.ease == ease) return e.name;
    return "inOutCubic";
}

}  // namespace cosmic
