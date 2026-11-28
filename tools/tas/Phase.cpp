#include "tools/tas/Phase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "game/player/Player.h"

namespace cosmic::tas {

std::vector<PeriodicElement> periodicElements(const LevelData& level) {
    std::vector<PeriodicElement> out;
    for (const LevelObject& o : level.objects) {
        PeriodicElement e;
        e.id = o.id;
        if (o.type == "moving_platform") {
            // A platform that waits for a rider, or a bubble, keeps no time of its own.
            if (o.prop<bool>("triggered", false) || !o.prop<bool>("active", true)) continue;
            const float dx = o.prop<float>("dx", 128.0f), dy = o.prop<float>("dy", 0.0f);
            const float speed = std::max(1.0f, o.prop<float>("speed", 60.0f));
            if (o.prop<bool>("orbit", false)) {
                const float r = std::max(8.0f, (std::abs(dx) + std::abs(dy)) * 0.5f);
                e.period = 2.0f * kPi * r / speed;
                e.sweep = {o.rect.x - std::abs(dx), o.rect.y - std::abs(dy), o.rect.w + 2.0f * std::abs(dx), o.rect.h + 2.0f * std::abs(dy)};
            } else {
                // There and back along its route, waiting at each end.
                e.period = 2.0f * (std::sqrt(dx * dx + dy * dy) / speed + std::max(0.0f, o.prop<float>("wait", 0.6f)));
                e.sweep = {std::min(o.rect.x, o.rect.x + dx), std::min(o.rect.y, o.rect.y + dy), o.rect.w + std::abs(dx), o.rect.h + std::abs(dy)};
            }
        } else if (o.type == "laser") {
            if (!o.prop<bool>("active", true)) continue;
            e.period = std::max(0.0f, o.prop<float>("onTime", 1.5f)) + std::max(0.0f, o.prop<float>("offTime", 1.5f));
            // Its beam can be turned: everything around its rectangle, as far as its longest side.
            const float r = std::max(o.rect.w, o.rect.h);
            e.sweep = {o.rect.center().x - r, o.rect.center().y - r, 2.0f * r, 2.0f * r};
        } else {
            continue;
        }
        if (e.period > 0.0f && std::isfinite(e.period)) out.push_back(std::move(e));
    }
    return out;
}

float periodMet(const RunStudy& best, size_t s, const std::vector<PeriodicElement>& elements, float margin) {
    const size_t a = best.sectionStart(s);
    if (a == RunStudy::kNone) return 0.0f;
    const size_t b = std::min(best.feet.size(), (s < best.ends.size() ? best.ends[s] : best.exitAt) + 1);
    float out = 0.0f;
    for (const PeriodicElement& e : elements) {
        const Rect r{e.sweep.x - margin, e.sweep.y - margin, e.sweep.w + 2.0f * margin, e.sweep.h + 2.0f * margin};
        for (size_t i = a; i < b; i += 4)
            if (r.contains(best.feet[i])) {
                out = std::max(out, e.period);
                break;
            }
    }
    return out;
}

std::vector<InputFrame> throughSections(Sim& sim, const std::vector<InputFrame>& run, size_t& sections, size_t atMost) {
    const RunStudy g = sim.study(run);
    sections = std::min(g.ends.size(), atMost);
    if (sections == 0) return {};
    return std::vector<InputFrame>(run.begin(), run.begin() + static_cast<std::ptrdiff_t>(std::min(run.size(), g.ends[sections - 1])));
}

namespace {

/// A way through the sections so far: its inputs from the load, its world at the end.
struct Way {
    std::vector<InputFrame> run;
    std::unique_ptr<ArenaWorld::State> snap;
    float clock = 0.0f;
    int health = 0;
    float energy = 0.0f;
    uint32_t held = 0;
    float cost = 0.0f;  // the clock, a lost heart counting as time lost
    std::vector<PhaseSection> sections;
    int pure = -1;  // the source it has followed all along, without a wait (-1: none)
};

/// One try of a section: the frames played after the way's end (waits included), and how it ended.
struct Try {
    std::vector<InputFrame> frames;
    bool arrived = false;
    float clock = 0.0f;
    int health = 0;
    float energy = 0.0f;
    float cost = 0.0f;
    int wait = 0, waits = 0;
    // Out of phase: where it left the best run's path (the frame of the best run, and of the try).
    size_t leftAt = 0, leftTry = 0;
};

class Realigner {
public:
    Realigner(Sim& sim, const PhaseOptions& opt, const std::function<bool()>& over) : sim_(sim), opt_(opt), over_(over) {
        const auto& wps = sim.course().waypoints();
        for (int end : sim.course().sectionEnds()) {
            int cp = 0;
            for (int k = 0; k < end; ++k) cp += wps[static_cast<size_t>(k)].kind == Waypoint::Kind::Checkpoint ? 1 : 0;
            const bool exit = wps[static_cast<size_t>(end)].kind == Waypoint::Kind::Exit;
            endCheckpoint_.push_back(exit ? -1 : cp);
        }
        maxHealth_ = sim.world().player().maxHealth();
    }
    int64_t tried = 0;
    bool stopped() const { return over_ && over_(); }

    /// The tries of section `s` from the world now (saved in `at`, the way's keys `held`), waits up
    /// to `maxK` frames before the best run's frames from `starts`; `prefix`: frames already played
    /// in this section (an inner wait), counted in each try. Arrivals into `out`; the tries that
    /// left the path into `left` (the best ones).
    void sweep(const RunStudy& src, size_t s, const ArenaWorld::State& at, uint32_t held, float clock0, const std::vector<size_t>& startsIn, int maxK, int step,
               const std::vector<InputFrame>& prefix, int waitBefore, int waitsBefore, float& worst, std::vector<Try>& out, std::vector<Try>* left) {
        ArenaWorld& aw = sim_.arena();
        const size_t end = s < src.ends.size() ? src.ends[s] : src.exitAt;
        auto rungs = sim_.take();
        aw.restore(at);
        uint32_t h = held;
        for (int k = 0; k <= maxK && !stopped(); ++k) {
            if (k % step == 0) {
                // Hopeless: even with the best run's own pace from here it would arrive after the
                // arrivals kept (with a margin: a realigned run can gain on it).
                const float soonest = clock0 + static_cast<float>(k) / 120.0f + static_cast<float>(end - std::min(end, startsIn.front())) / 120.0f - 0.5f;
                if (worst > 0.0f && soonest > worst) break;
                aw.save(*rungs);
                const Player& p = sim_.world().player();
                std::vector<size_t> starts = startsIn;
                for (size_t j : src.nearest(p.feet(), p.vel, startsIn.front(), 120, 1))
                    if (std::none_of(starts.begin(), starts.end(), [&](size_t o) { return (o > j ? o - j : j - o) < 3; })) starts.push_back(j);
                for (size_t j : starts) {
                    Try t = play(src, s, *rungs, h, j, end);
                    if (t.frames.empty() && !t.arrived) continue;
                    std::vector<InputFrame> whole = prefix;
                    for (int i = 0; i < k; ++i) whole.push_back(idle(i == 0 ? held : 0u));
                    whole.insert(whole.end(), t.frames.begin(), t.frames.end());
                    t.frames = std::move(whole);
                    t.wait = waitBefore + k;
                    t.waits = waitsBefore + (k > 0 ? 1 : 0);
                    t.leftTry += prefix.size() + static_cast<size_t>(k);
                    if (t.arrived) {
                        out.push_back(std::move(t));
                        worst = worstKept(out);
                    } else if (left && t.leftAt > 0) {
                        left->push_back(std::move(t));
                    }
                }
                aw.restore(*rungs);
            }
            aw.step(idle(k == 0 ? h : 0u));
            h = 0;
        }
        sim_.give(std::move(rungs));
    }

    /// The best run's frames from `j`, played from `at` (keys `held` before) until the end of
    /// section `s` (the best run reached it after `end` frames), or out of phase, or too slow.
    Try play(const RunStudy& src, size_t s, const ArenaWorld::State& at, uint32_t held, size_t j, size_t end) {
        ArenaWorld& aw = sim_.arena();
        aw.restore(at);
        ++tried;
        Try t;
        const int deaths = sim_.world().stats().deaths;
        const size_t limit = (end > j ? end - j : 0) + static_cast<size_t>(opt_.slack * 120.0f);
        int off = 0;
        auto say = [&](const char* how) {
            if (!opt_.trace) return;
            char line[240];
            const Player& p = sim_.world().player();
            std::snprintf(line, sizeof line, "    essai : depuis l'image %zu de la source, %s après %zu images (%.3f s au chrono), pieds (%.0f, %.0f) / source (%.0f, %.0f)", j, how,
                          t.frames.size(), static_cast<double>(sim_.world().timer().elapsed()), static_cast<double>(p.feet().x), static_cast<double>(p.feet().y),
                          static_cast<double>(src.feet[std::min(j + t.frames.size(), src.feet.size() - 1)].x),
                          static_cast<double>(src.feet[std::min(j + t.frames.size(), src.feet.size() - 1)].y));
            opt_.trace(line);
        };
        uint32_t prev = held;
        for (size_t i = j; i < src.frames.size() && t.frames.size() < limit; ++i) {
            InputFrame f = src.frames[i];
            // (the source's power keys on the slots that hold the same powers here)
            if (i < src.layoutAt.size()) f = remapSlots(f, src.layouts[src.layoutAt[i]], slotLayout(sim_.world()), prev);
            if (i == j) {
                f.pressed = f.held & ~held;
                f.released = held & ~f.held;
            }
            aw.step(f);
            prev = f.held;
            t.frames.push_back(f);
            if (sim_.broken(deaths)) {
                say("mort");
                return Try{};
            }
            if (reached(s)) {
                say("arrivée");
                t.arrived = true;
                t.clock = sim_.world().timer().elapsed();
                t.health = sim_.world().player().health;
                t.energy = sim_.world().player().energy;
                t.cost = t.clock + 0.25f * static_cast<float>(std::max(0, maxHealth_ - t.health));
                return t;
            }
            // Out of phase: far from the best run's path for a while.
            const size_t bi = std::min(i + 1, src.feet.size() - 1);
            if ((sim_.world().player().feet() - src.feet[bi]).length() > opt_.leave) {
                if (++off >= opt_.leaveFrames) {
                    say("hors du chemin");
                    t.leftAt = i + 1 - static_cast<size_t>(off);
                    t.leftTry = t.frames.size() - static_cast<size_t>(off);
                    return t;
                }
            } else {
                off = 0;
            }
        }
        say("trop lent");
        return t;
    }

    bool reached(size_t s) {
        if (s >= endCheckpoint_.size()) return false;
        const int cp = endCheckpoint_[s];
        bool ok = cp < 0 ? sim_.atExit() : ((sim_.litCheckpoints() >> cp) & 1u) != 0;
        if (ok && sim_.category() != RunCategory::AnyPercent && cp >= 0) ok = sim_.measure(0).waypoint > sim_.course().sectionEnds()[s];
        return ok;
    }

    float worstKept(std::vector<Try>& out) const {
        if (static_cast<int>(out.size()) < opt_.keep) return -1.0f;
        std::vector<float> c;
        for (const Try& t : out) c.push_back(t.cost);
        std::nth_element(c.begin(), c.begin() + (opt_.keep - 1), c.end());
        return c[static_cast<size_t>(opt_.keep - 1)];
    }

    static InputFrame idle(uint32_t heldBefore) {
        InputFrame f;
        f.released = heldBefore;
        return f;
    }

    Sim& sim_;
    const PhaseOptions& opt_;
    const std::function<bool()>& over_;
    std::vector<int> endCheckpoint_;  // per section: the checkpoint that ends it (CP index), or -1: the exit
    int maxHealth_ = 1;
};

}  // namespace

PhaseRun realign(Sim& sim, const std::vector<const RunStudy*>& sources, const std::vector<PeriodicElement>& elements, const std::vector<InputFrame>& prefix,
                 size_t from, const PhaseOptions& opt, const std::function<bool()>& over, int prefixSource) {
    PhaseRun out;
    const size_t sections = sim.course().sectionEnds().size();
    if (from >= sections) return out;
    Realigner R(sim, opt, over);
    ArenaWorld& aw = sim.arena();
    std::vector<Way> ways;
    {
        Way w;
        w.run = prefix;
        aw.restore(sim.origin());
        for (const InputFrame& f : prefix) aw.step(f);
        w.snap = sim.take();
        aw.save(*w.snap);
        w.clock = sim.world().timer().elapsed();
        w.health = sim.world().player().health;
        w.energy = sim.world().player().energy;
        w.held = prefix.empty() ? 0u : prefix.back().held;
        w.pure = prefixSource;
        ways.push_back(std::move(w));
    }
    // The span of the waits tried: up to the lead on the source (+ a margin), at most one period of
    // the elements it met in the section (+ a margin), at most opt.maxWait.
    auto spanOf = [&](float lead, float period) {
        const float span = std::min({opt.maxWait, std::max(opt.minWait, lead + opt.extra), std::max(opt.minWait, period + opt.extra)});
        return std::max(0, static_cast<int>(std::lround(span * 120.0f)));
    };
    for (size_t s = from; s < sections && !R.stopped(); ++s) {
        struct Found {
            size_t way = 0, src = 0;
            Try t;
        };
        std::vector<Found> found;
        std::vector<float> periods(sources.size(), 0.0f);
        for (size_t wi = 0; wi < ways.size() && !R.stopped(); ++wi) {
            Way& w = ways[wi];
            for (size_t si = 0; si < sources.size() && !R.stopped(); ++si) {
                const RunStudy& src = *sources[si];
                const size_t start = src.sectionStart(s);
                if (start == RunStudy::kNone || start >= src.exitAt || (s >= src.ends.size() && !src.finishes())) continue;
                const float srcStart = src.clockAt[std::min(start, src.clockAt.size() - 1)];
                const float period = periods[si] = periodMet(src, s, elements);
                const float lead = srcStart - w.clock;
                const int maxK = spanOf(lead, period);
                if (opt.trace) {
                    char line[240];
                    aw.restore(*w.snap);
                    const Player& p = sim.world().player();
                    std::snprintf(line, sizeof line, "  section %zu, source %zu : départ %.3f s (la source : %.3f s), attentes 0 à %d images ; santé %d, énergie %.1f, pieds (%.0f, %.0f)",
                                  s + 1, si, static_cast<double>(w.clock), static_cast<double>(srcStart), maxK, p.health, static_cast<double>(p.energy),
                                  static_cast<double>(p.feet().x), static_cast<double>(p.feet().y));
                    opt.trace(line);
                }
                float worst = -1.0f;
                std::vector<Try> got, left;
                R.sweep(src, s, *w.snap, w.held, w.clock, {start}, maxK, std::max(1, opt.coarse), {}, 0, 0, worst, got, &left);
                // Finer just below the best waits found (an earlier one that still works gains).
                if (opt.fine < opt.coarse && !got.empty() && !R.stopped()) {
                    std::vector<int> ks;
                    std::vector<Try> sorted = got;
                    std::sort(sorted.begin(), sorted.end(), [](const Try& a, const Try& b) { return a.cost < b.cost; });
                    for (size_t i = 0; i < sorted.size() && ks.size() < 2; ++i)
                        if (sorted[i].waits <= 1 && std::find(ks.begin(), ks.end(), sorted[i].wait) == ks.end()) ks.push_back(sorted[i].wait);
                    for (int k0 : ks) {
                        const int lo = std::max(0, k0 - opt.coarse + opt.fine);
                        if (lo >= k0) continue;
                        auto at = sim.take();
                        aw.restore(*w.snap);
                        std::vector<InputFrame> pre;
                        for (int i = 0; i < lo; ++i) pre.push_back(Realigner::idle(i == 0 ? w.held : 0u));
                        for (const InputFrame& f : pre) aw.step(f);
                        aw.save(*at);
                        float wf = -1.0f;
                        std::vector<Try> fine;
                        R.sweep(src, s, *at, lo == 0 ? w.held : 0u, w.clock + static_cast<float>(lo) / 120.0f, {start}, k0 - 1 - lo, opt.fine, pre, lo, 0, wf, fine,
                                nullptr);
                        for (Try& t : fine) {
                            t.waits = t.wait > 0 ? 1 : 0;  // (one wait, at the section's start)
                            got.push_back(std::move(t));
                        }
                        sim.give(std::move(at));
                    }
                }
                // Out of phase further on: a second wait where the try left the source's path, at the
                // last place before it where the source had its feet on the ground (a clean stop).
                if (opt.inner > 0 && !left.empty() && !R.stopped()) {
                    std::sort(left.begin(), left.end(), [](const Try& a, const Try& b) { return a.leftAt != b.leftAt ? a.leftAt > b.leftAt : a.wait < b.wait; });
                    auto grounded = [&](size_t i) {
                        return i > 0 && i < src.feet.size() && std::abs(src.feet[i].y - src.feet[i - 1].y) < 0.01f && std::abs(src.vel[i].y) < 0.01f;
                    };
                    int done = 0;
                    std::vector<size_t> anchors;
                    for (const Try& t : left) {
                        if (done >= opt.inner || R.stopped()) break;
                        size_t a = t.leftAt > 12 ? t.leftAt - 12 : 0;
                        const size_t lowest = std::max(start, t.leftAt > 240 ? t.leftAt - 240 : size_t(0));
                        while (a > lowest && !grounded(a)) --a;
                        if (a <= start || a >= t.leftAt || !grounded(a) || std::find(anchors.begin(), anchors.end(), a) != anchors.end()) continue;
                        const size_t into = t.leftTry - (t.leftAt - a);  // the try's frames before the anchor
                        if (into > t.frames.size()) continue;
                        ++done;
                        anchors.push_back(a);
                        std::vector<InputFrame> pre(t.frames.begin(), t.frames.begin() + static_cast<std::ptrdiff_t>(into));
                        auto at = sim.take();
                        aw.restore(*w.snap);
                        for (const InputFrame& f : pre) aw.step(f);
                        aw.save(*at);
                        const float c0 = sim.world().timer().elapsed();
                        const int maxK2 = std::max(0, maxK - t.wait);
                        float wf = worst;
                        R.sweep(src, s, *at, pre.empty() ? w.held : pre.back().held, c0, {a}, maxK2, std::max(1, opt.coarse), pre, t.wait, t.waits, wf, got, nullptr);
                        sim.give(std::move(at));
                    }
                }
                for (Try& t : got) found.push_back({wi, si, std::move(t)});
            }
        }
        if (found.empty()) break;
        std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.t.cost < b.t.cost; });
        // A pure way's own continuation first (its source on, no wait): never worse than that source.
        std::stable_partition(found.begin(), found.end(), [&](const Found& f) {
            return ways[f.way].pure >= 0 && f.src == static_cast<size_t>(ways[f.way].pure) && f.t.wait == 0;
        });
        std::vector<Way> next;
        // A Pareto set at the section's end (docs/TAS.md): the earliest arrivals, and a later one
        // only when it keeps more energy or health than every earlier one kept (what the next
        // sections may need: a Dash, a shield), `keep` x 2 at most.
        for (Found& f : found) {
            if (static_cast<int>(next.size()) >= 2 * std::max(1, opt.keep)) break;
            bool dominated = false;
            for (const Way& o : next)
                dominated = dominated || (o.cost <= f.t.cost + 1e-4f && o.energy + 1.0f >= f.t.energy && o.health >= f.t.health);
            if (dominated && static_cast<int>(next.size()) >= std::max(1, opt.keep)) continue;
            bool same = false;
            for (const Way& o : next) same = same || (std::abs(o.clock - f.t.clock) < 1e-4f && o.health == f.t.health && std::abs(o.energy - f.t.energy) < 1.0f);
            if (same) continue;
            const Way& w = ways[f.way];
            const RunStudy& src = *sources[f.src];
            Way n;
            n.run = w.run;
            n.run.insert(n.run.end(), f.t.frames.begin(), f.t.frames.end());
            n.snap = sim.take();
            aw.restore(*w.snap);
            for (const InputFrame& x : f.t.frames) aw.step(x);
            aw.save(*n.snap);
            n.clock = f.t.clock;
            n.health = f.t.health;
            n.energy = f.t.energy;
            n.held = n.run.empty() ? 0u : n.run.back().held;
            n.cost = f.t.cost;
            n.sections = w.sections;
            n.pure = w.pure >= 0 && f.src == static_cast<size_t>(w.pure) && f.t.wait == 0 ? w.pure : -1;
            PhaseSection ps;
            ps.source = static_cast<int>(f.src);
            ps.wait = f.t.wait;
            ps.waits = f.t.waits;
            ps.arrival = f.t.clock;
            ps.sourceArrival = src.clockAt[std::min(s < src.ends.size() ? src.ends[s] : src.exitAt, src.clockAt.size() - 1)];
            ps.periodMet = periods[f.src];
            ps.tried = static_cast<int>(R.tried);
            n.sections.push_back(ps);
            next.push_back(std::move(n));
        }
        for (Way& w : ways) sim.give(std::move(w.snap));
        ways = std::move(next);
        if (!ways.empty()) {
            out.partial = ways.front().run;
            out.partialSections = s + 1;
            out.partialClock = ways.front().clock;
        }
        if (s + 1 == sections) {
            // The exit: the fastest way, played again from the load.
            for (const Way& w : ways) {
                const float c = sim.playToExit(sim.origin(), w.run, 0, 1e9f);
                if (c >= 0.0f && (!out.found || c < out.clock)) {
                    out.found = true;
                    out.clock = c;
                    out.run = w.run;
                    out.sections = w.sections;
                }
            }
        }
    }
    for (Way& w : ways) sim.give(std::move(w.snap));
    out.tried = R.tried;
    return out;
}

PhaseRun realign(Sim& sim, const RunStudy& best, const std::vector<PeriodicElement>& elements, const std::vector<InputFrame>& prefix, size_t from,
                 const PhaseOptions& opt, const std::function<bool()>& over) {
    if (!best.finishes()) return {};
    return realign(sim, std::vector<const RunStudy*>{&best}, elements, prefix, from, opt, over);
}

std::vector<PhaseLead> phaseLeads(const std::vector<const RunStudy*>& sources, size_t sections, float minLead) {
    std::vector<PhaseLead> out;
    // The fastest source, and the fastest time of each section among the sources that finish.
    float best = -1.0f;
    std::vector<float> segment(sections, -1.0f);
    for (const RunStudy* g : sources) {
        if (!g->finishes()) continue;
        if (best < 0.0f || g->clock < best) best = g->clock;
        for (size_t s = 0; s < sections && s < g->ends.size(); ++s) {
            const float d = g->splitAt(s) - (s == 0 ? 0.0f : g->splitAt(s - 1));
            if (segment[s] < 0.0f || d < segment[s]) segment[s] = d;
        }
    }
    if (best < 0.0f) return out;
    std::vector<float> estimate;
    for (size_t i = 0; i < sources.size(); ++i) {
        const RunStudy& a = *sources[i];
        for (size_t n = 1; n < sections && n <= a.ends.size(); ++n) {
            // Its own split, then the best segments of the sources (as if they followed with no
            // loss): a prefix worth realigning when that could beat the fastest source.
            float e = a.splitAt(n - 1);
            for (size_t s = n; s < sections; ++s) e += segment[s] < 0.0f ? 1.0e6f : segment[s];
            if (e + minLead > best) continue;
            float lead = 0.0f;  // (for the log: its advance on the sources that finish)
            for (const RunStudy* g : sources)
                if (g != &a && g->finishes() && n <= g->ends.size()) lead = std::max(lead, g->splitAt(n - 1) - a.splitAt(n - 1));
            out.push_back({i, n, lead});
            estimate.push_back(e);
        }
    }
    // The most promising first.
    std::vector<size_t> order(out.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return estimate[x] < estimate[y]; });
    std::vector<PhaseLead> sorted;
    for (size_t k : order) sorted.push_back(out[k]);
    return sorted;
}

PhaseSplice phaseSplice(Sim& sim, const std::vector<const RunStudy*>& sources, const std::vector<PeriodicElement>& elements, const std::vector<PhaseLead>& leads,
                        const PhaseOptions& opt, const std::function<bool()>& over, const std::function<void(const PhaseLead&, const PhaseRun&)>& each) {
    PhaseSplice out;
    for (const PhaseLead& l : leads) {
        if (over && over()) break;
        const RunStudy& a = *sources[l.source];
        if (l.through == 0 || l.through > a.ends.size()) continue;
        const std::vector<InputFrame> prefix(a.frames.begin(), a.frames.begin() + static_cast<std::ptrdiff_t>(std::min(a.frames.size(), a.ends[l.through - 1])));
        PhaseRun r = realign(sim, sources, elements, prefix, l.through, opt, over, static_cast<int>(l.source));
        ++out.leads;
        out.tried += r.tried;
        if (each) each(l, r);
        if (r.partialSections > out.furthest.partialSections ||
            (r.partialSections == out.furthest.partialSections && r.partialSections > 0 && r.partialClock < out.furthest.partialClock))
            out.furthest = r;
        if (r.found && (!out.run.found || r.clock < out.run.clock)) {
            out.run = std::move(r);
            out.from = l;
        }
    }
    return out;
}

}  // namespace cosmic::tas
