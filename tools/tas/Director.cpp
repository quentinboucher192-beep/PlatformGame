#include "tools/tas/Director.h"

#include <algorithm>
#include <cmath>

#include "tools/tas/Portfolio.h"

namespace cosmic::tas {

Director::Director(Portfolio& portfolio, int threads) : P_(portfolio), threads_(std::max(1, threads)) {
    workers_.resize(static_cast<size_t>(threads_));
}

Director::Plan Director::decide(float bestClock, int first, int last) {
    Plan plan;
    const int T = threads_;
    const int traj = P_.bandit().find(Portfolio::kTrajectory);
    const int expl = P_.bandit().find(Portfolio::kExplore);
    const int refine = P_.bandit().find(Portfolio::kRefine);
    const bool trajOk = traj >= 0 && P_.allowed(traj);
    const bool explOk = expl >= 0 && P_.allowed(expl);
    const bool refineOk = refine >= 0 && P_.allowed(refine);
    // The trajectories have places left unless they searched every one of this very best run.
    const bool trajLeft = trajOk && !(trajectoryDoneAt_ > 0.0f && std::abs(trajectoryDoneAt_ - bestClock) < 1.0e-6f);
    const bool gained = lastRefined_ > 0.0f && bestClock + 1.0e-6f < lastRefined_;
    std::string reason;
    std::vector<double> n;
    if (lastRefined_ < 0.0f) lastRefined_ = bestClock;
    if (gained && refineOk) {
        // A faster run: refined once on every core (its neighbours often polish it further).
        plan.parts.push_back({refine, T});
        lastRefined_ = bestClock;
        reason = "plan_after_gain";
        n = {static_cast<double>(T)};
    } else if (trajLeft) {
        // Places of the best run not searched yet: the trajectories take the cores, but one for the
        // exploration (the routes nothing points to).
        if (T >= 2 && explOk) {
            plan.parts.push_back({traj, T - 1});
            plan.parts.push_back({expl, 1});
        } else {
            plan.parts.push_back({traj, T});
        }
        reason = "plan_places_left";
        n = {static_cast<double>(plan.parts[0].threads), plan.parts.size() > 1 ? 1.0 : 0.0};
    } else {
        // Otherwise the strategy that gained the most for its time lately (the bandit), the
        // exploration keeping a core when the other one can share.
        const int a = P_.pick(false);
        if (a < 0) return plan;
        const std::string& id = P_.id(a);
        const bool shares = id == Portfolio::kTiming || id == Portfolio::kTrajectory;
        if (shares && T >= 2 && explOk && a != expl) {
            plan.parts.push_back({a, T - 1});
            plan.parts.push_back({expl, 1});
        } else {
            plan.parts.push_back({a, T});
        }
        reason = "plan_bandit";
        n = {100.0 * P_.bandit().share(a), static_cast<double>(plan.parts[0].threads)};
    }
    std::string desc;
    for (const Part& p : plan.parts) desc += (desc.empty() ? "" : ",") + P_.id(p.arm) + ":" + std::to_string(p.threads);
    {
        std::lock_guard<std::mutex> l(m_);
        int w = 0;
        for (const Part& p : plan.parts)
            for (int k = 0; k < p.threads && w < T; ++k, ++w) {
                workers_[static_cast<size_t>(w)].arm = P_.id(p.arm);
                workers_[static_cast<size_t>(w)].first = first;
                workers_[static_cast<size_t>(w)].last = last;
            }
    }
    ++decisions_;
    if (desc != lastPlan_ || reason == "plan_after_gain") {
        lastPlan_ = desc;
        n.push_back(static_cast<double>(first + 1));
        n.push_back(static_cast<double>(last < 0 ? -1 : last + 1));
        think(Thought{"decide", reason, n, desc});
    }
    return plan;
}

void Director::done(const Part& part, float before, float after, double seconds) {
    P_.record(part.arm, seconds * part.threads, before, after);
}

void Director::trajectoriesExhausted(float bestClock) {
    if (std::abs(trajectoryDoneAt_ - bestClock) < 1.0e-6f) return;
    trajectoryDoneAt_ = bestClock;
    think(Thought{"observe", "places_done", {static_cast<double>(bestClock)}, {}});
}

void Director::counted(int firstWorker, int count, int64_t n) {
    std::lock_guard<std::mutex> l(m_);
    if (count <= 0 || firstWorker < 0) return;
    const int64_t each = n / count;
    int64_t rest = n - each * count;
    for (int k = 0; k < count; ++k) {
        const size_t w = static_cast<size_t>(firstWorker + k);
        if (w >= workers_.size()) break;
        workers_[w].attempts += each + (rest > 0 ? 1 : 0);
        if (rest > 0) --rest;
    }
}

void Director::think(const Thought& t) {
    std::function<void(const Thought&)> s;
    {
        std::lock_guard<std::mutex> l(m_);
        s = sink;
    }
    if (s) s(t);
}

std::vector<WorkerState> Director::workers() const {
    std::lock_guard<std::mutex> l(m_);
    return workers_;
}

}  // namespace cosmic::tas
