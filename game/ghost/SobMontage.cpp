#include "game/ghost/SobMontage.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "game/ghost/GhostRace.h"
#include "game/ghost/TrainingSegments.h"

namespace cosmic {

namespace {

/// The same time to the half millisecond (the chronometer's own values, read back from a file).
constexpr float kSameTime = 0.0005f;

bool validSeconds(float s) { return std::isfinite(s) && s > 0.0f && s <= 7200.0f; }

/// Segment `i` of a run on its chronometer.
std::pair<float, float> bounds(const MontageRun& r, size_t i) {
    float a = i == 0 ? 0.0f : r.splits[i - 1];
    float b = i < r.splits.size() ? r.splits[i] : r.time;
    return {a, b};
}

}  // namespace

float MontagePlan::possibleGain() const { return pbOnRoute && total > 0.0f && pbTime > 0.0f ? std::max(0.0f, pbTime - total) : -1.0f; }

MontagePlan buildMontagePlan(const std::vector<float>& gold, const std::vector<std::string>& goldCheckpoints,
                             const std::vector<SegmentHistory::GoldSource>& sources, const MontageRun& pb, const std::vector<MontageRun>& others,
                             const std::vector<std::string>& route, const MontageTrainings& trainings) {
    MontagePlan p;
    const bool hasPb = validSeconds(pb.time);
    std::vector<float> pbSegs;
    std::optional<std::vector<std::string>> pbIds;
    if (hasPb) {
        p.pbTime = pb.time;
        pbSegs = segmentDurations(pb.splits, pb.time);
        pbIds = resolveRunCheckpoints(pb.checkpoints, pb.splits.size(), route);
    }
    // The Sum of Best exactly as the training's references count it (buildSegmentReferences):
    // the gold segments, completed with the record's when it went through the same checkpoints,
    // then the trainings' best times.
    std::vector<float> sob;
    std::vector<bool> fromPb;
    std::optional<std::vector<std::string>> ids;
    if (!gold.empty()) {
        const size_t splits = gold.size() - 1;
        ids = resolveRunCheckpoints(goldCheckpoints, splits, route);
        sob = gold;
        fromPb.assign(sob.size(), false);
        if (hasPb && pb.splits.size() == splits && (!ids || !pbIds || *ids == *pbIds)) {
            p.pbOnRoute = true;
            if (!ids) ids = pbIds;
            for (size_t i = 0; i < sob.size() && i < pbSegs.size(); ++i)
                if (sob[i] < 0.0f || pbSegs[i] < sob[i]) {
                    sob[i] = pbSegs[i];
                    fromPb[i] = true;
                }
        }
    } else if (hasPb) {
        sob = pbSegs;  // one run known: its segments are the best ones
        fromPb.assign(sob.size(), true);
        ids = pbIds;
        p.pbOnRoute = true;
    } else if (!trainings.empty()) {
        sob.assign(route.size() + 1, -1.0f);  // no full run yet: the route, from the trainings only
        fromPb.assign(sob.size(), false);
        ids = route;
    }
    std::vector<bool> fromTraining(sob.size(), false);
    if (ids) {
        TrainingBests bests;
        for (const auto& [ends, t] : trainings) bests[ends] = t.best;
        sob = withTrainingBests(std::move(sob), *ids, bests, &fromTraining);
    }
    p.total = sumOfBest(sob);
    const size_t splitCount = sob.empty() ? 0 : sob.size() - 1;
    // A kept run that went the same way (same checkpoints, when both are known).
    auto sameRoute = [&](const MontageRun& r) {
        if (r.file.empty() || !validSeconds(r.time) || r.splits.size() != splitCount) return false;
        auto rIds = resolveRunCheckpoints(r.checkpoints, r.splits.size(), route);
        return !ids || !rIds || *ids == *rIds;
    };
    std::vector<const MontageRun*> kept;
    if (hasPb) kept.push_back(&pb);
    for (const MontageRun& r : others) kept.push_back(&r);
    for (size_t i = 0; i < sob.size(); ++i) {
        MontageSegment s;
        s.index = static_cast<int>(i);
        if (ids && ids->size() == splitCount) {
            s.from = i == 0 ? std::string{} : (*ids)[i - 1];
            s.to = i == splitCount ? std::string{} : (*ids)[i];
        }
        s.duration = sob[i] >= 0.0f && std::isfinite(sob[i]) ? sob[i] : -1.0f;
        auto use = [&](const MontageRun& r, bool adopt) {
            auto [a, b] = bounds(r, i);
            s.file = r.file;
            s.date = r.date;
            s.start = a;
            s.end = b;
            s.adopt = adopt;
        };
        if (s.duration < 0.0f) {
            // Unknown: nothing to show (the total is unknown too).
        } else if (fromTraining[i]) {
            // A section training did better: its best attempt (from its start to its end).
            s.training = true;
            auto it = trainings.find({s.from, s.to});
            if (it != trainings.end()) {
                s.date = it->second.date;
                if (!it->second.file.empty()) {
                    s.file = it->second.file;
                    s.start = 0.0f;
                    s.end = it->second.best;
                    s.trainingStart = it->second.start;
                }
            }
        } else if (fromPb[i]) {
            // The record did better than the golds kept: its own replay.
            s.date = pb.date;
            if (!pb.file.empty()) use(pb, false);
        } else {
            if (i < sources.size()) s.date = sources[i].date;
            if (i < sources.size() && sources[i].has() && std::abs((sources[i].end - sources[i].start) - s.duration) <= kSameTime) {
                s.file = sources[i].replay;
                s.start = sources[i].start;
                s.end = sources[i].end;
            } else {
                // A gold of before 2.60.3: one of my kept runs may have done exactly that time.
                for (const MontageRun* r : kept) {
                    if (!sameRoute(*r)) continue;
                    auto [a, b] = bounds(*r, i);
                    if (std::abs((b - a) - s.duration) > kSameTime) continue;
                    use(*r, true);
                    break;
                }
            }
        }
        if (s.hasReplay()) ++p.withReplay;
        p.segments.push_back(std::move(s));
    }
    return p;
}

}  // namespace cosmic
