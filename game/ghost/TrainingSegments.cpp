#include "game/ghost/TrainingSegments.h"

#include <algorithm>
#include <cmath>

#include "game/ghost/Ghost.h"

namespace cosmic {


// ------------------------------------------------------------------ matching

int segmentIndexIn(const std::vector<std::string>& checkpoints, const std::string& from, const std::string& to) {
    const int n = static_cast<int>(checkpoints.size());
    // Segment i goes from boundary i to boundary i + 1: boundary 0 is the start, boundary
    // k (1..n) the k-th checkpoint reached, boundary n + 1 the exit.
    int i = -1;
    if (from.empty()) {
        i = 0;
    } else {
        for (int k = 0; k < n; ++k)
            if (checkpoints[static_cast<size_t>(k)] == from) {
                i = k + 1;
                break;
            }
        if (i < 0) return -1;
    }
    const bool toExit = i == n;
    if (to.empty()) return toExit ? i : -1;
    if (toExit || checkpoints[static_cast<size_t>(i)] != to) return -1;
    return i;
}

std::optional<std::vector<std::string>> resolveRunCheckpoints(const std::vector<std::string>& recorded, size_t splitCount,
                                                              const std::vector<std::string>& route) {
    if (recorded.size() == splitCount) return recorded;  // recorded with it (or no split at all)
    if (splitCount == route.size()) return route;        // one split per checkpoint: the route's order
    return std::nullopt;
}

std::vector<std::string> checkpointsOfTrack(const GhostTrack& track, const std::vector<std::pair<std::string, Vec2>>& checkpoints) {
    std::vector<std::string> out;
    for (float s : track.splits()) {
        Vec2 at = track.poseAt(s).feet;
        const std::string* id = nullptr;
        float bd = 320.0f * 320.0f;
        for (const auto& [cid, feet] : checkpoints) {
            float d = (feet - at).lengthSq();
            if (d < bd) {
                bd = d;
                id = &cid;
            }
        }
        if (!id || std::find(out.begin(), out.end(), *id) != out.end()) return {};
        out.push_back(*id);
    }
    return out;
}

float sumOfBest(const std::vector<float>& bestSegments) {
    if (bestSegments.empty()) return -1.0f;
    double sum = 0.0;
    for (float b : bestSegments) {
        if (!(b >= 0.0f) || !std::isfinite(b)) return -1.0f;
        sum += b;
    }
    return static_cast<float>(sum);
}

}  // namespace cosmic
