#include "game/ghost/ReplayGap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cosmic {

// ------------------------------------------------------------------ route

float routeDistanceAt(const LevelProgressTrack& route, Vec2 feet, size_t& hint, float maxOff) {
    if (!route.valid()) return -1.0f;
    route.project(feet, hint);  // the nearest point of the route, searched near the last one first
    const size_t i = std::min(hint, route.route.size() - 1);
    float bestD = (route.route[i] - feet).lengthSq();
    if (bestD > maxOff * maxOff) return -1.0f;
    float best = route.cumulative[i];
    // On the segments around that point: a distance that does not jump from one point to the next.
    auto onSegment = [&](size_t a) {
        const Vec2 p = route.route[a], q = route.route[a + 1];
        const Vec2 d = q - p;
        const float len2 = d.x * d.x + d.y * d.y;
        if (len2 < 1e-6f) return;
        const float u = std::clamp(((feet.x - p.x) * d.x + (feet.y - p.y) * d.y) / len2, 0.0f, 1.0f);
        const Vec2 c = p + d * u;
        const float dd = (c - feet).lengthSq();
        if (dd < bestD) {
            bestD = dd;
            best = route.cumulative[a] + u * std::sqrt(len2);
        }
    };
    if (i > 0) onSegment(i - 1);
    if (i + 1 < route.route.size()) onSegment(i);
    return best;
}

RouteRun RouteRun::fromReplay(const ReplayData& r, const LevelProgressTrack& route) {
    if (!route.valid() || r.frames.empty()) return {};
    const double hz = std::max<uint16_t>(1, r.sampleHz);
    const double duration = static_cast<double>(r.durationMs) / 1000.0;
    std::vector<double> times;
    std::vector<float> reach;
    times.reserve(r.frames.size() + 1);
    reach.reserve(r.frames.size() + 1);
    size_t hint = 0;
    float best = 0.0f;
    bool any = false;
    for (size_t i = 0; i < r.frames.size(); ++i) {
        const ReplayFrame& f = r.frames[i];
        const float d = routeDistanceAt(route, {f.x, f.y}, hint);
        if (d >= 0.0f) {
            best = any ? std::max(best, d) : d;
            any = true;
        }
        times.push_back(std::min(duration, static_cast<double>(i) / hz));
        reach.push_back(any ? best : 0.0f);
    }
    // A finished run reached the exit (the route's end) at its final time.
    if (r.countEvents(ReplayEventType::Finish) > 0) {
        times.push_back(duration);
        reach.push_back(route.length);
    }
    return fromSamples(std::move(times), std::move(reach), duration, route.length);
}

RouteRun RouteRun::fromSamples(std::vector<double> times, std::vector<float> reach, double duration, float routeLength) {
    RouteRun out;
    const size_t n = std::min(times.size(), reach.size());
    if (n < 2 || !(routeLength > 1.0f)) return out;
    times.resize(n);
    reach.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(times[i])) times[i] = i > 0 ? times[i - 1] : 0.0;
        if (!std::isfinite(reach[i])) reach[i] = i > 0 ? reach[i - 1] : 0.0f;
        reach[i] = std::clamp(reach[i], 0.0f, routeLength);
        if (i > 0) {
            times[i] = std::max(times[i], times[i - 1]);
            reach[i] = std::max(reach[i], reach[i - 1]);
        }
    }
    out.times_ = std::move(times);
    out.reach_ = std::move(reach);
    out.duration_ = std::max(duration, out.times_.back());
    out.length_ = routeLength;
    return out;
}

float RouteRun::reachAt(double t) const {
    if (times_.empty()) return 0.0f;
    if (t <= times_.front()) return reach_.front();
    if (t >= times_.back()) return reach_.back();
    const size_t k = static_cast<size_t>(std::upper_bound(times_.begin(), times_.end(), t) - times_.begin());  // times_[k] > t, k >= 1
    const double t0 = times_[k - 1], t1 = times_[k];
    if (t1 - t0 < 1e-12) return reach_[k];
    const double u = (t - t0) / (t1 - t0);
    return static_cast<float>(reach_[k - 1] + (reach_[k] - reach_[k - 1]) * u);
}

double RouteRun::timeAt(float d) const {
    if (reach_.empty() || d > reach_.back()) return -1.0;
    if (d <= reach_.front()) return times_.front();
    const size_t k = static_cast<size_t>(std::lower_bound(reach_.begin(), reach_.end(), d) - reach_.begin());  // reach_[k] >= d > reach_[k-1]
    const double r0 = reach_[k - 1], r1 = reach_[k];
    const double u = r1 > r0 ? (static_cast<double>(d) - r0) / (r1 - r0) : 1.0;
    return times_[k - 1] + (times_[k] - times_[k - 1]) * u;
}

// -------------------------------------------------------------------- gap

const GapSample* GapCurve::atSplit(int split) const {
    for (const GapSample& s : samples)
        if (s.exact && s.split == split) return &s;
    return nullptr;
}

namespace {

/// Half a simulation step (1/240 s, in ms): a jump to a checkpoint lands on its step.
constexpr int64_t kSplitWindowMs = 4;

bool continuous(const GapRun& run, const GapReference& ref) { return ref.run.valid() && run.run && run.run->valid(); }

/// The exact gap at the run's checkpoint `k` (the exit: the split count). False when the
/// reference did not pass the same checkpoint (or its final time is unknown).
bool exactAt(const GapRun& run, const GapReference& ref, int k, GapSample& s) {
    const int n = static_cast<int>(run.splitsMs.size());
    int64_t mine = -1, theirs = -1;
    if (k >= 0 && k < n) {
        mine = run.splitsMs[static_cast<size_t>(k)];
        if (static_cast<size_t>(k) < ref.atSplitsMs.size()) theirs = ref.atSplitsMs[static_cast<size_t>(k)];
    } else if (k == n && run.finished) {
        mine = run.durationMs;
        theirs = ref.finishMs;
    }
    if (mine < 0 || theirs < 0) return false;
    s.hasTime = true;
    s.timeMs = mine - (theirs + ref.offsetMs);
    s.exact = true;
    s.split = k;
    return true;
}

/// The route estimate at `t` (both ways along the route), without the checkpoints.
GapSample estimateAt(const GapRun& run, const GapReference& ref, double t) {
    GapSample s;
    s.t = t;
    if (!continuous(run, ref) || t <= 0.002) return s;
    const RouteRun& a = *run.run;
    const double off = static_cast<double>(ref.offsetMs) / 1000.0;
    const double tMs = t * 1000.0;
    const bool finished = run.finished && tMs >= static_cast<double>(run.durationMs);
    const float da = a.reachAt(t);  // a finished run's trajectory ends at the exit
    // Distance: where each one stands on the route at this instant (B on its own clock).
    s.hasDistance = true;
    s.metres = metersOf(da - ref.run.reachAt(t - off));
    if (finished) {
        exactAt(run, ref, static_cast<int>(run.splitsMs.size()), s);
        return s;
    }
    // Time: how much longer it took to get there (the Ghost Racing estimate).
    if (da <= a.length() * 0.002f) return s;
    const double tr = ref.run.timeAt(da);
    if (tr < 0.0) return s;
    s.hasTime = true;
    s.timeMs = std::llround(tMs) - (std::llround(tr * 1000.0) + ref.offsetMs);
    return s;
}

}  // namespace

GapSample gapAt(const GapRun& run, const GapReference& ref, double t) {
    const int64_t tMs = std::llround(t * 1000.0);
    const int n = static_cast<int>(run.splitsMs.size());
    if (!continuous(run, ref)) {
        // Checkpoints only: the last one passed (or the exit).
        GapSample s;
        s.t = t;
        if (tMs >= run.durationMs - kSplitWindowMs && exactAt(run, ref, n, s)) {
            s.t = static_cast<double>(run.durationMs) / 1000.0;
            return s;
        }
        for (int k = n - 1; k >= 0; --k) {
            if (run.splitsMs[static_cast<size_t>(k)] > tMs + kSplitWindowMs) continue;
            if (exactAt(run, ref, k, s)) {
                s.t = static_cast<double>(run.splitsMs[static_cast<size_t>(k)]) / 1000.0;
                return s;
            }
        }
        return GapSample{t};
    }
    GapSample s = estimateAt(run, ref, t);
    for (int k = 0; k < n; ++k)
        if (std::llabs(tMs - run.splitsMs[static_cast<size_t>(k)]) <= kSplitWindowMs) {
            GapSample e = s;
            if (exactAt(run, ref, k, e)) return e;
        }
    return s;
}

GapCurve gapCurve(const GapRun& run, const GapReference& ref, double end, int maxSamples) {
    GapCurve c;
    c.end = std::max(0.0, end);
    c.continuous = continuous(run, ref);
    const int n = static_cast<int>(run.splitsMs.size());
    struct At {
        double t;
        int split;  // -1: a regular sample
    };
    std::vector<At> at;
    if (c.continuous) {
        const double step = std::max(1.0 / 30.0, c.end / std::max(1, maxSamples));
        const auto count = static_cast<int>(std::floor(c.end / step));
        at.reserve(static_cast<size_t>(count) + static_cast<size_t>(n) + 3);
        for (int j = 0; j <= count; ++j) at.push_back({j * step, -1});
        at.push_back({c.end, -1});
    }
    const double endMs = c.end * 1000.0 + 0.5;
    for (int k = 0; k < n; ++k)
        if (static_cast<double>(run.splitsMs[static_cast<size_t>(k)]) <= endMs) at.push_back({static_cast<double>(run.splitsMs[static_cast<size_t>(k)]) / 1000.0, k});
    if (static_cast<double>(run.durationMs) <= endMs) at.push_back({static_cast<double>(run.durationMs) / 1000.0, n});
    std::stable_sort(at.begin(), at.end(), [](const At& a, const At& b) { return a.t < b.t; });
    c.samples.reserve(at.size());
    for (const At& a : at) {
        GapSample s;
        if (a.split >= 0) {
            s = c.continuous ? estimateAt(run, ref, a.t) : GapSample{};
            s.t = a.t;
            if (!exactAt(run, ref, a.split, s) && !c.continuous) continue;
        } else {
            s = estimateAt(run, ref, a.t);
        }
        // Two samples at the same instant: the exact one stays.
        if (!c.samples.empty() && std::abs(c.samples.back().t - s.t) < 0.0005) {
            if (c.samples.back().exact && !s.exact) continue;
            c.samples.back() = s;
        } else {
            c.samples.push_back(s);
        }
    }
    for (const GapSample& s : c.samples) {
        if (s.hasTime) c.maxTimeMs = std::max<int64_t>(c.maxTimeMs, std::llabs(s.timeMs));
        if (s.hasDistance) c.maxMetres = std::max(c.maxMetres, std::abs(s.metres));
    }
    return c;
}

// ------------------------------------------------------------ Sum of Best

SobRun buildSobRun(const std::vector<SobPiece>& pieces, const std::vector<float>& boundaryReach, float routeLength) {
    SobRun out;
    const size_t n = pieces.size();
    out.boundaryMs.assign(n + 1, -1);
    if (n == 0) return out;
    out.boundaryMs[0] = 0;
    double sum = 0.0;
    bool known = true;
    for (size_t i = 0; i < n; ++i) {
        const double d = pieces[i].duration;
        if (!(d >= 0.0) || !std::isfinite(d)) known = false;
        if (!known) continue;
        sum += d;
        out.boundaryMs[i + 1] = std::llround(sum * 1000.0);
    }
    bool all = known && boundaryReach.size() == n + 1 && routeLength > 1.0f;
    for (const SobPiece& p : pieces) {
        if (p.run && p.run->valid()) ++out.withReplay;
        else all = false;
    }
    if (!all) return out;
    // Each best segment from its own run, put after the previous ones on the montage's clock
    // (its duration exactly its best), held between the route distances of its two ends.
    std::vector<double> times;
    std::vector<float> reach;
    double at = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const SobPiece& p = pieces[i];
        const float lo = std::clamp(boundaryReach[i], 0.0f, routeLength);
        const float hi = std::clamp(std::max(lo, boundaryReach[i + 1]), 0.0f, routeLength);
        const double span = p.end - p.start;
        times.push_back(at);
        reach.push_back(lo);
        const auto& rt = p.run->times();
        const auto& rr = p.run->reach();
        for (auto it = std::upper_bound(rt.begin(), rt.end(), p.start); it != rt.end() && *it < p.end; ++it) {
            const size_t k = static_cast<size_t>(it - rt.begin());
            const double u = span > 1e-9 ? (*it - p.start) / span : 1.0;
            times.push_back(at + u * p.duration);
            reach.push_back(std::clamp(rr[k], lo, hi));
        }
        at += p.duration;
        times.push_back(at);
        reach.push_back(hi);
    }
    out.run = RouteRun::fromSamples(std::move(times), std::move(reach), at, routeLength);
    return out;
}

std::vector<int64_t> sobAtSplits(const SobRun& sob, size_t runSplits, const std::vector<std::string>& runCheckpoints,
                                 const std::vector<std::string>& sobCheckpoints) {
    std::vector<int64_t> out(runSplits, -1);
    if (sob.boundaryMs.size() < 2) return out;
    const size_t inner = sob.boundaryMs.size() - 2;  // checkpoints between the segments
    if (runCheckpoints.size() == runSplits && sobCheckpoints.size() == inner && inner > 0) {
        for (size_t k = 0; k < runSplits; ++k)
            for (size_t j = 0; j < inner; ++j)
                if (!runCheckpoints[k].empty() && runCheckpoints[k] == sobCheckpoints[j]) {
                    out[k] = sob.boundaryMs[j + 1];
                    break;
                }
        return out;
    }
    if (runSplits == inner)
        for (size_t k = 0; k < runSplits; ++k) out[k] = sob.boundaryMs[k + 1];
    return out;
}

GapReference gapReferenceOf(const ReplayComparison& cmp, RouteRun bRun) {
    GapReference ref;
    ref.run = std::move(bRun);
    ref.atSplitsMs.assign(cmp.a().splitsMs.size(), -1);
    for (const ReplayComparison::Row& r : cmp.rows())
        if (!r.exit && r.aSplit >= 0 && r.bSplit >= 0 && static_cast<size_t>(r.aSplit) < ref.atSplitsMs.size() &&
            static_cast<size_t>(r.bSplit) < cmp.b().splitsMs.size())
            ref.atSplitsMs[static_cast<size_t>(r.aSplit)] = cmp.b().splitsMs[static_cast<size_t>(r.bSplit)];
    ref.finishMs = cmp.b().durationMs;
    ref.offsetMs = cmp.offsetMs();
    return ref;
}

GapRun gapRunOf(const CompareRun& a, const RouteRun* aRun) {
    GapRun g;
    g.run = aRun;
    g.splitsMs = a.splitsMs;
    g.durationMs = a.durationMs;
    return g;
}

bool MeasurePassTimer::feed(double t, Vec2 feet, MeasurePass& out) {
    const auto ft = static_cast<float>(t);
    const std::optional<float> d = timer_.feed(ft, feet);
    if (d) {
        const double a = start_ >= 0.0 ? start_ : t - *d;  // A and B in the same move: its end
        out = {a, a + *d};
    }
    if (timer_.running()) {
        const double s = t - timer_.elapsed(ft);
        if (d || start_ < 0.0 || std::abs(s - start_) > 1e-4) start_ = s;  // A crossed (again)
    } else {
        start_ = -1.0;  // B crossed, or a jump stopped the chronometer
    }
    return d.has_value();
}

void MeasurePassTimer::reset() {
    timer_.reset();
    start_ = -1.0;
}

std::vector<MeasurePass> measurePasses(const ReplayData& r, const MeasureGate& a, const MeasureGate& b) {
    std::vector<MeasurePass> out;
    MeasurePassTimer timer(a, b);
    const double hz = std::max<uint16_t>(1, r.sampleHz);
    const double duration = static_cast<double>(r.durationMs) / 1000.0;
    MeasurePass p;
    for (size_t i = 0; i < r.frames.size(); ++i) {
        const ReplayFrame& f = r.frames[i];
        if (timer.feed(std::min(duration, static_cast<double>(i) / hz), {f.x, f.y}, p)) out.push_back(p);
    }
    if (timer.openSince() >= 0.0) out.push_back({timer.openSince(), -1.0});
    return out;
}

std::string formatGapMetres(float metres) {
    long tenths = std::lround(static_cast<double>(std::abs(metres)) * 10.0);
    char buf[32];
    const char* sign = metres < 0.0f && tenths > 0 ? "-" : "";
    std::snprintf(buf, sizeof buf, "%s%ld,%ld m", sign, tenths / 10, tenths % 10);
    return buf;
}

}  // namespace cosmic
