#include "tools/tas/Timing.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "engine/core/Random.h"
#include "game/gameplay/GameWorld.h"
#include "tools/tas/Sim.h"

namespace cosmic::tas {

namespace {

constexpr size_t kRung = 120;  // savestates of the best run every second

uint64_t mixHash(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

uint32_t clockBits(float c) {
    uint32_t b = 0;
    std::memcpy(&b, &c, sizeof b);
    return b;
}

/// The frames of `src` from `from` (`count` of them) after `out`, the first one pressed from what `out` held last.
void appendSeamed(std::vector<InputFrame>& out, const std::vector<InputFrame>& src, size_t from, size_t count) {
    const uint32_t held = out.empty() ? 0 : out.back().held;
    const size_t at = out.size();
    for (size_t i = from; i < src.size() && i < from + count; ++i) out.push_back(src[i]);
    if (out.size() > at) {
        out[at].pressed = out[at].held & ~held;
        out[at].released = held & ~out[at].held;
    }
}

/// One edit of the work list: an event moved, a short press inserted, a press deleted.
struct Edit {
    enum Kind : uint8_t { Shift, Insert, Release, Delete } kind = Shift;
    size_t event = 0;  // Shift, Delete
    int shift = 0;     // Shift
    uint32_t bit = 0;  // Insert, Release
    size_t frame = 0;  // Insert, Release
    int len = 1;       // Insert, Release
    uint64_t key(uint32_t best, const std::vector<InputEvent>& ev) const {
        uint64_t h = mixHash(best, kind);
        if (kind == Shift || kind == Delete) h = mixHash(mixHash(mixHash(h, ev[event].frame), ev[event].bit), ev[event].press);
        return mixHash(mixHash(mixHash(mixHash(h, static_cast<uint64_t>(shift + 64)), bit), frame), static_cast<uint64_t>(len));
    }
};

struct Outcome {
    bool ok = false;       // played to the window's end without breaking the category
    float ahead = -1.0e9f; // its progress there over the best run's (score)
    bool faster = false;   // a faster whole run (played again from the load)
    float clock = 0.0f;
    std::vector<InputFrame> whole;
};

/// The state of one thread on the current best run.
struct Bench {
    uint64_t version = ~0ull;
    std::vector<InputFrame> best;
    float bestClock = 0.0f;
    RunStudy g;
    std::vector<InputEvent> events;
    std::vector<float> drops;
    size_t from = 0, endFrame = 0;  // the frames that may change, up to the window's end
    float endScore = 0.0f;          // the best run's score at endFrame
    int endWaypoint = 0;
};

}  // namespace

std::vector<InputEvent> inputEvents(const std::vector<InputFrame>& run, const std::vector<uint8_t>& frozen, size_t from, size_t to) {
    std::vector<InputEvent> out;
    to = std::min(to, run.size());
    auto isFrozen = [&](size_t i) { return i < frozen.size() && frozen[i]; };
    for (size_t i = std::max<size_t>(1, from); i < to; ++i) {
        const uint32_t changed = run[i].held ^ run[i - 1].held;
        if (!changed || isFrozen(i) || isFrozen(i - 1)) continue;
        for (int k = 0; k < 32; ++k) {
            const uint32_t b = 1u << k;
            if (!(changed & b)) continue;
            InputEvent e;
            e.bit = b;
            e.frame = i;
            e.press = (run[i].held & b) != 0;
            // Within the same key's events around it, never before `from`, never into a frozen frame.
            size_t lo = i;
            while (lo > std::max<size_t>(1, from) && !isFrozen(lo - 1) && !isFrozen(lo - 2 < lo ? lo - 2 : 0) && ((run[lo - 1].held ^ run[lo - 2].held) & b) == 0)
                --lo;
            size_t hi = i;
            while (hi + 1 < to && !isFrozen(hi) && ((run[hi + 1].held ^ run[hi].held) & b) == 0) ++hi;
            e.lo = lo;
            e.hi = hi;
            out.push_back(e);
        }
    }
    return out;
}

bool shiftEvent(std::vector<InputFrame>& run, const InputEvent& e, int shift, const std::vector<uint8_t>& frozen) {
    if (shift == 0) return true;
    const long t = static_cast<long>(e.frame) + shift;
    if (t < static_cast<long>(e.lo) || t > static_cast<long>(e.hi) || t < 1 || static_cast<size_t>(t) >= run.size()) return false;
    const size_t a = std::min(e.frame, static_cast<size_t>(t)), b = std::max(e.frame, static_cast<size_t>(t));
    for (size_t i = a; i < b; ++i)
        if (i < frozen.size() && frozen[i]) return false;
    // Earlier: the frames before it take the key's state after it; later: those after it keep the state before.
    const bool after = e.press;
    const bool value = shift < 0 ? after : !after;
    for (size_t i = a; i < b; ++i) run[i].held = value ? (run[i].held | e.bit) : (run[i].held & ~e.bit);
    return true;
}

std::vector<float> speedDrops(const RunStudy& g) {
    std::vector<float> d(g.vel.size(), 0.0f);
    for (size_t i = 1; i < g.vel.size(); ++i) {
        const float vx0 = std::abs(g.vel[i - 1].x), vx1 = std::abs(g.vel[i].x);
        float lost = std::max(0.0f, vx0 - vx1);
        if (g.vel[i - 1].y > 60.0f && std::abs(g.vel[i].y) < 5.0f) lost += 20.0f;  // a landing
        if (vx0 > 40.0f && vx1 < 5.0f) lost += 40.0f;                                // a wall
        d[i] = lost;
    }
    return d;
}

TimingStats TimingPolish::run(const std::vector<Sim*>& sims, const Slice& slice) {
    TimingStats stats;
    if (sims.empty() || slice.best.empty()) return stats;
    std::mutex m;
    std::vector<InputFrame> shared = slice.best;
    float sharedClock = slice.bestClock;
    uint64_t version = 0;
    const int threads = std::max(1, std::min(slice.threads, static_cast<int>(sims.size())));
    const Options o = opt_;

    auto work = [&](int wi) {
        Sim& sim = *sims[static_cast<size_t>(wi)];
        Random rng(slice.seed * 6151u + static_cast<uint64_t>(wi) * 337u + 11u);
        Bench B;
        auto refresh = [&]() -> bool {
            {
                std::lock_guard<std::mutex> l(m);
                if (B.version == version) return true;
                B.version = version;
                B.best = shared;
                B.bestClock = sharedClock;
            }
            B.g = sim.study(B.best);
            if (!B.g.finishes()) return false;
            sim.buildLadder(B.best, kRung);
            const size_t sections = B.g.ends.size();
            B.from = std::max<size_t>(slice.keep, 1);
            const size_t last = slice.lastSection < 0 ? sections : static_cast<size_t>(slice.lastSection);
            B.endFrame = last < sections ? B.g.ends[last] : B.g.exitAt;
            B.endFrame = std::min(B.endFrame, B.g.exitAt);
            if (B.endFrame <= B.from + 2) return false;
            B.events = inputEvents(B.best, B.g.frozen, B.from, B.endFrame);
            B.drops = speedDrops(B.g);
            for (InputEvent& e : B.events) {
                float p = 1.0f;
                for (size_t i = e.frame > 4 ? e.frame - 4 : 0; i < std::min(B.drops.size(), e.frame + 24); ++i) p += B.drops[i];
                e.priority = p;
            }
            // The best run's score at the window's end (the fitness compares to it).
            size_t at = 0;
            sim.arena().restore(sim.rungBefore(B.endFrame, at));
            for (size_t i = at; i < B.endFrame; ++i) sim.arena().step(B.best[i]);
            const Progress p = sim.measure(0);
            B.endScore = p.score;
            B.endWaypoint = p.waypoint;
            return true;
        };
        // An edited run (changed from frame `lo` on) against the best run.
        auto evaluate = [&](const std::vector<InputFrame>& edited, size_t lo) {
            Outcome r;
            size_t at = 0;
            sim.arena().restore(sim.rungBefore(lo > 0 ? lo - 1 : 0, at));
            const int deaths = sim.world().stats().deaths;
            if (slice.count) slice.count(1);
            for (size_t i = at; i < B.endFrame && i < edited.size(); ++i) {
                sim.arena().step(edited[i]);
                if (sim.broken(deaths)) return r;
                if (sim.atExit()) {
                    r.ok = true;
                    r.ahead = 1.0e6f;
                    r.whole.assign(edited.begin(), edited.begin() + static_cast<std::ptrdiff_t>(i + 1));
                    break;
                }
            }
            if (r.whole.empty()) {
                r.ok = true;
                r.ahead = sim.measure(0).score - B.endScore;
                if (r.ahead <= 0.25f) return r;
                // Further along at the window's end: the best run's rest played on (realigned).
                const JoinTry j = sim.rejoin(B.g, B.endFrame, edited[B.endFrame - 1].held, B.bestClock - 1.0e-4f);
                if (slice.count) slice.count(j.tried);
                if (!j.ok || j.finish + 1.0e-6f >= B.bestClock) return r;
                r.whole.assign(edited.begin(), edited.begin() + static_cast<std::ptrdiff_t>(B.endFrame));
                appendSeamed(r.whole, B.g.frames, j.seedAt, j.played);
            }
            // A faster run: played again from the load (and cut at the exit).
            size_t played = 0;
            const float c = sim.playToExit(sim.origin(), r.whole, 0, B.bestClock, &played);
            if (c >= 0.0f && c + 1.0e-6f < B.bestClock) {
                r.whole.resize(played);
                r.faster = true;
                r.clock = c;
            }
            return r;
        };
        // A faster run kept: shared with the other threads, offered to the search.
        auto keep = [&](const Outcome& r, const char* kind, int TimingStats::*counter) {
            std::lock_guard<std::mutex> l(m);
            if (!(r.clock + 1.0e-6f < sharedClock)) return;
            char line[160];
            std::snprintf(line, sizeof line, "  polissage (%s) : %.3f s -> %.3f s", kind, static_cast<double>(sharedClock), static_cast<double>(r.clock));
            if (slice.log) slice.log(line);
            shared = r.whole;
            sharedClock = r.clock;
            ++version;
            ++(stats.*counter);
            if (slice.offer) slice.offer(r.whole, r.clock, std::string("polissage (") + kind + ")");
        };
        auto changed = [&] {
            std::lock_guard<std::mutex> l(m);
            return B.version != version;
        };

        while (!slice.over()) {
            if (!refresh()) return;
            const uint32_t bestKey = clockBits(B.bestClock) ^ static_cast<uint32_t>(B.best.size() * 2654435761u);
            // The work list: shifts (the events nearest the speed lost first), short presses
            // inserted before the drops, short presses deleted.
            std::vector<size_t> order(B.events.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return B.events[a].priority > B.events[b].priority; });
            std::vector<Edit> edits;
            for (int k = 1; k <= 2 * o.maxShift; ++k) {
                if (k == o.maxShift + 1) {
                    // After the first ±k: the insertions and deletions, then the wider shifts.
                    std::vector<size_t> dropFrames;
                    for (size_t i = B.from; i < B.endFrame && i < B.drops.size(); ++i)
                        if (B.drops[i] > 8.0f && !B.g.frozen[std::min(i, B.g.frozen.size() - 1)]) dropFrames.push_back(i);
                    std::stable_sort(dropFrames.begin(), dropFrames.end(), [&](size_t a, size_t b) { return B.drops[a] > B.drops[b]; });
                    if (dropFrames.size() > 12) dropFrames.resize(12);
                    uint32_t keys = keyBit(Action::Jump) | keyBit(Action::MoveLeft) | keyBit(Action::MoveRight);
                    for (const InputEvent& e : B.events)
                        if (e.press) keys |= e.bit;
                    keys &= ~(keyBit(Action::MoveUp) | keyBit(Action::Interact));
                    for (size_t f : dropFrames)
                        for (int off : {-8, -4, -1})
                            for (int len : {1, 6, 12})
                                for (int k2 = 0; k2 < 32; ++k2) {
                                    if (!((keys >> k2) & 1u) || static_cast<long>(f) + off < static_cast<long>(B.from)) continue;
                                    Edit e;
                                    e.bit = 1u << k2;
                                    e.frame = static_cast<size_t>(static_cast<long>(f) + off);
                                    e.len = len;
                                    e.kind = (B.best[e.frame].held & e.bit) ? Edit::Release : Edit::Insert;
                                    edits.push_back(e);
                                }
                    for (size_t i = 0; i < B.events.size(); ++i) {
                        const InputEvent& e = B.events[i];
                        if (!e.press) continue;
                        size_t r = e.frame;
                        while (r < B.best.size() && (B.best[r].held & e.bit)) ++r;
                        if (r - e.frame <= 12) {
                            Edit d;
                            d.kind = Edit::Delete;
                            d.event = i;
                            edits.push_back(d);
                        }
                    }
                }
                for (size_t i : order)
                    for (int s : {-k, k}) {
                        Edit e;
                        e.kind = Edit::Shift;
                        e.event = i;
                        e.shift = s;
                        edits.push_back(e);
                    }
            }
            bool restarted = false;
            for (size_t n = static_cast<size_t>(wi); n < edits.size() && !slice.over(); n += static_cast<size_t>(threads)) {
                if (changed()) {
                    restarted = true;
                    break;
                }
                const Edit& e = edits[n];
                const uint64_t key = e.key(bestKey, B.events);
                {
                    std::lock_guard<std::mutex> l(m);
                    if (tried_.count(key)) continue;
                }
                std::vector<InputFrame> ed = B.best;
                size_t lo = 0, hi = 0;
                const char* kind = "décalage";
                int TimingStats::*counter = &TimingStats::shifts;
                if (e.kind == Edit::Shift) {
                    const InputEvent& ev = B.events[e.event];
                    if (!shiftEvent(ed, ev, e.shift, B.g.frozen)) continue;
                    lo = std::min(ev.frame, static_cast<size_t>(static_cast<long>(ev.frame) + e.shift));
                    hi = std::max(ev.frame, static_cast<size_t>(static_cast<long>(ev.frame) + e.shift));
                } else if (e.kind == Edit::Delete) {
                    const InputEvent& ev = B.events[e.event];
                    lo = ev.frame;
                    for (hi = ev.frame; hi < ed.size() && (ed[hi].held & ev.bit); ++hi) ed[hi].held &= ~ev.bit;
                    kind = "appui supprimé";
                    counter = &TimingStats::deletes;
                } else {
                    lo = e.frame;
                    hi = std::min(ed.size(), e.frame + static_cast<size_t>(e.len));
                    bool clash = false;
                    for (size_t i = lo > 0 ? lo - 1 : 0; i <= std::min(ed.size() - 1, hi); ++i) {
                        clash = clash || B.g.frozen[std::min(i, B.g.frozen.size() - 1)];
                        if (e.kind == Edit::Insert) clash = clash || (ed[i].held & e.bit);
                    }
                    if (clash) continue;
                    for (size_t i = lo; i < hi; ++i) ed[i].held = e.kind == Edit::Insert ? (ed[i].held | e.bit) : (ed[i].held & ~e.bit);
                    kind = e.kind == Edit::Insert ? "appui ajouté" : "relâché un instant";
                    counter = &TimingStats::inserts;
                }
                if (lo < B.from) continue;
                repressRun(ed, lo, hi);
                const Outcome r = evaluate(ed, lo);
                {
                    std::lock_guard<std::mutex> l(m);
                    ++stats.tried;
                    if (!r.faster) tried_.insert(key);
                }
                if (r.faster) {
                    keep(r, kind, counter);
                    restarted = true;
                    break;
                }
            }
            if (restarted || slice.over()) continue;
            // Differential evolution over the timings of the events nearest the speed lost.
            const size_t n = std::min(order.size(), static_cast<size_t>(std::max(2, o.evolveEvents)));
            if (n < 2) {
                // Nothing to evolve: wait for another thread's gain or the slice's end.
                while (!slice.over() && !changed()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            const int P = std::max(4, o.population);
            const float K = static_cast<float>(2 * o.maxShift);
            std::vector<std::vector<float>> pop(static_cast<size_t>(P), std::vector<float>(n, 0.0f));
            std::vector<float> fit(static_cast<size_t>(P), 0.0f);
            auto realise = [&](const std::vector<float>& x, std::vector<InputFrame>& ed, size_t& lo) {
                ed = B.best;
                lo = ed.size();
                size_t hi = 0;
                bool any = false;
                for (size_t j = 0; j < n; ++j) {
                    const int d = static_cast<int>(std::lround(x[j]));
                    const InputEvent& ev = B.events[order[j]];
                    if (d == 0 || !shiftEvent(ed, ev, d, B.g.frozen)) continue;
                    any = true;
                    lo = std::min(lo, std::min(ev.frame, static_cast<size_t>(static_cast<long>(ev.frame) + d)));
                    hi = std::max(hi, std::max(ev.frame, static_cast<size_t>(static_cast<long>(ev.frame) + d)));
                }
                if (any) repressRun(ed, lo, hi);
                return any && lo >= B.from;
            };
            for (int i = 1; i < P; ++i)
                for (size_t j = 0; j < n; ++j) pop[static_cast<size_t>(i)][j] = rng.range(-K, K);
            bool gained = false;
            for (int i = 1; i < P && !slice.over() && !gained; ++i) {
                std::vector<InputFrame> ed;
                size_t lo = 0;
                if (!realise(pop[static_cast<size_t>(i)], ed, lo)) {
                    fit[static_cast<size_t>(i)] = -1.0e9f;
                    continue;
                }
                const Outcome r = evaluate(ed, lo);
                std::lock_guard<std::mutex> l(m);
                ++stats.tried;
                fit[static_cast<size_t>(i)] = r.ok ? r.ahead : -1.0e9f;
                gained = r.faster;
                if (gained) {
                    m.unlock();
                    keep(r, "évolution", &TimingStats::evolved);
                    m.lock();
                }
            }
            while (!gained && !slice.over() && !changed()) {
                for (int i = 0; i < P && !gained && !slice.over(); ++i) {
                    int a, b, c;
                    do a = rng.rangeInt(0, P - 1); while (a == i);
                    do b = rng.rangeInt(0, P - 1); while (b == i || b == a);
                    do c = rng.rangeInt(0, P - 1); while (c == i || c == a || c == b);
                    std::vector<float> t = pop[static_cast<size_t>(i)];
                    const size_t forced = static_cast<size_t>(rng.rangeInt(0, static_cast<int>(n) - 1));
                    bool same = true;
                    for (size_t j = 0; j < n; ++j) {
                        if (j == forced || rng.chance(0.7f)) {
                            const float v = pop[static_cast<size_t>(a)][j] + 0.6f * (pop[static_cast<size_t>(b)][j] - pop[static_cast<size_t>(c)][j]);
                            t[j] = std::clamp(v, -K, K);
                        }
                        same = same && std::lround(t[j]) == std::lround(pop[static_cast<size_t>(i)][j]);
                    }
                    if (same) continue;
                    std::vector<InputFrame> ed;
                    size_t lo = 0;
                    if (!realise(t, ed, lo)) continue;
                    const Outcome r = evaluate(ed, lo);
                    {
                        std::lock_guard<std::mutex> l(m);
                        ++stats.tried;
                    }
                    if (r.faster) {
                        keep(r, "évolution", &TimingStats::evolved);
                        gained = true;
                        break;
                    }
                    const float f = r.ok ? r.ahead : -1.0e9f;
                    if (f >= fit[static_cast<size_t>(i)]) {
                        pop[static_cast<size_t>(i)] = std::move(t);
                        fit[static_cast<size_t>(i)] = f;
                    }
                }
            }
        }
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back(work, i);
    for (auto& t : pool) t.join();
    total_.tried += stats.tried;
    total_.shifts += stats.shifts;
    total_.inserts += stats.inserts;
    total_.deletes += stats.deletes;
    total_.evolved += stats.evolved;
    if (slice.log) {
        char line[200];
        std::snprintf(line, sizeof line, "  polissage : %lld essais (gains : %d décalages, %d appuis ajoutés, %d supprimés, %d par évolution)", static_cast<long long>(stats.tried),
                      stats.shifts, stats.inserts, stats.deletes, stats.evolved);
        slice.log(line);
    }
    return stats;
}

}  // namespace cosmic::tas
