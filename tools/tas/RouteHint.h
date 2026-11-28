#pragma once

// A point of a human route (a ghost's recorded trajectory): where Nova's feet were, at which clock. Shared by
// the planner (tools/tas/Planner.h) and Go-Explore (tools/tas/Explore.h); only a hint, never a target to stop at.

#include "engine/core/Math.h"

namespace cosmic::tas {

struct RouteHint {
    Vec2 feet;
    float clock = 0.0f;
};

}  // namespace cosmic::tas
