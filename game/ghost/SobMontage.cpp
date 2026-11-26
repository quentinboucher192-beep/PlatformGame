#include "game/ghost/SobMontage.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <system_error>

namespace cosmic {

namespace {

/// The same time to the half millisecond (the chronometer's own values, read back from a file).
constexpr float kSameTime = 0.0005f;

bool validSeconds(float s) { return std::isfinite(s) && s > 0.0f && s <= 7200.0f; }

/// Where the segment (from, to) lies on a kept run's chronometer, nullopt when it did not go
/// straight from one to the other.
std::optional<std::pair<float, float>> boundsIn(const MontageRun& r, const std::vector<std::string>& route, const std::string& from,
                                                const std::string& to) {
    if (r.file.empty() || !validSeconds(r.time)) return std::nullopt;
    const std::vector<std::string>& ids = r.checkpoints.size() == r.splits.size() ? r.checkpoints : route;
    if (ids.size() != r.splits.size()) return std::nullopt;
    // Boundary k of the run: 0 its start, 1..n its checkpoints, n + 1 its exit.
    const int n = static_cast<int>(ids.size());
    int a = -1;
    if (from.empty()) a = 0;
    else
        for (int k = 0; k < n; ++k)
            if (ids[static_cast<size_t>(k)] == from) a = k + 1;
    if (a < 0) return std::nullopt;
    const int b = a + 1;
    if (to.empty() ? b != n + 1 : (b > n || ids[static_cast<size_t>(b - 1)] != to)) return std::nullopt;
    const float ta = a == 0 ? 0.0f : r.splits[static_cast<size_t>(a - 1)];
    const float tb = b == n + 1 ? r.time : r.splits[static_cast<size_t>(b - 1)];
    return std::make_pair(ta, tb);
}

}  // namespace

float MontagePlan::possibleGain() const { return pbOnRoute && total > 0.0f && pbTime > 0.0f ? std::max(0.0f, pbTime - total) : -1.0f; }

MontagePlan buildMontagePlan(const PersonalBests& bests, const std::string& levelKey, const std::vector<MontageRun>& kept, int from, int to) {
    MontagePlan p;
    const auto route = bests.route(levelKey);
    if (!route) return p;
    const int n = static_cast<int>(route->size());
    if (to < 0 || to > n + 1) to = n + 1;
    from = std::clamp(from, 0, n);
    if (to <= from) return p;
    auto boundary = [&](int k) { return k <= 0 || k > n ? std::string{} : (*route)[static_cast<size_t>(k - 1)]; };
    p.total = bests.sumOfBests(levelKey, from, to);
    // The record of the range: the PB for the whole route, else that enchaînement's.
    if (from == 0 && to == n + 1) {
        if (const PersonalBests::Run* pb = bests.pb(levelKey)) {
            p.pbTime = pb->time;
            p.pbOnRoute = pb->checkpoints.empty() ? pb->splits.size() == route->size() : pb->checkpoints == *route;
        }
    } else if (const PersonalBests::Record* r = bests.record(levelKey, boundary(from), boundary(to)); r && r->best > 0.0f) {
        p.pbTime = r->best;
        p.pbOnRoute = true;  // a record of that range always went through its checkpoints
    }
    for (int k = from; k < to; ++k) {
        MontageSegment s;
        s.index = k;
        s.from = boundary(k);
        s.to = boundary(k + 1);
        if (const PersonalBests::Record* r = bests.record(levelKey, s.from, s.to); r && validSeconds(r->best)) {
            s.duration = r->best;
            s.source = r->source;
            s.date = r->date;
            if (r->replay.has()) {
                const fs::path file = bests.replayPath(levelKey, r->replay.file);
                std::error_code ec;
                if (!file.empty() && fs::is_regular_file(file, ec)) {
                    s.file = Paths::toUtf8(file);
                    s.start = r->replay.start;
                    s.end = r->replay.end;
                    s.training = !r->replay.fromLevelStart;
                    s.trainingStart = r->replay.setup;
                }
            }
            if (!s.hasReplay())
                // A best of before 2.60.3: one of my kept runs may have done exactly that time.
                for (const MontageRun& run : kept) {
                    auto b = boundsIn(run, *route, s.from, s.to);
                    if (!b || std::abs((b->second - b->first) - s.duration) > kSameTime) continue;
                    s.file = run.file;
                    s.start = b->first;
                    s.end = b->second;
                    s.adopt = true;
                    break;
                }
        }
        if (s.hasReplay()) ++p.withReplay;
        p.segments.push_back(std::move(s));
    }
    return p;
}

}  // namespace cosmic
