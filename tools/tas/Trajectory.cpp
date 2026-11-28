#include "tools/tas/Trajectory.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>

#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "tools/tas/Memory.h"
#include "tools/tas/Sim.h"
#include "tools/tas/Timing.h"

namespace cosmic::tas {

namespace {

constexpr size_t kRung = 120;     // savestates of the best run every second
constexpr size_t kNone = static_cast<size_t>(-1);
constexpr int kSettle = 12;       // frames played on after a first join (a larger lead may come)
constexpr int kMerge = 2;         // a join needs at least this lead (frames)
constexpr int kWorthRemembering = 6;  // the memory keeps a configuration that gained this many frames at least
constexpr float kWorthSaying = 0.15f; // s lost to a run at full speed: a place worth a thought
constexpr float kStep = 1.0f / 120.0f;

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

/// The keys a variation must not cut short at its start (a jump, an attack, a power held).
uint32_t busyKeys() {
    uint32_t m = keyBit(Action::Jump) | keyBit(Action::Attack);
    for (int a = static_cast<int>(Action::Dash); a <= static_cast<int>(Action::PowerSlot8); ++a)
        if (a != static_cast<int>(Action::Interact)) m |= 1u << a;
    return m;
}

/// Nova's state, as a join compares it: on the ground (running, sliding, crouching), in the air,
/// along a wall, dashing, anything else.
uint8_t stateClass(PlayerState s) {
    switch (s) {
        case PlayerState::Idle:
        case PlayerState::Run: return 0;
        case PlayerState::Slide: return 1;
        case PlayerState::Crouch: return 2;
        case PlayerState::Jump:
        case PlayerState::Fall: return 3;
        case PlayerState::WallSlide: return 4;
        case PlayerState::Dash: return 5;
        default: return static_cast<uint8_t>(10 + static_cast<int>(s));
    }
}

/// What the best run's study does not keep, after each frame: on the ground, air jumps left,
/// energy, the next waypoint, the state.
struct Track {
    std::vector<uint8_t> grounded, state;
    std::vector<int8_t> airJumps;
    std::vector<float> energy;
    std::vector<int16_t> waypoint;
};

/// A place of the best run where variations are tried.
struct Spot {
    size_t start = 0;    // the variations replace the frames from here (from the load)...
    size_t end = 0;      // ...up to here at most
    size_t joinEnd = 0;  // the best run's frames a variation may re-join (up to this one)
    std::vector<int8_t> dirs;
    float loss = 0.0f;
    uint64_t key = 0;
};

/// One thread's world on the best run: its study, its track, its savestates.
struct Lane {
    Sim* sim = nullptr;
    uint64_t version = ~0ull;
    RunStudy g;
    Track t;
    size_t from = 0, endFrame = 0;  // the frames that may change (the window)
    std::vector<std::unique_ptr<ArenaWorld::State>> ladder;  // every kRung frames
    std::unique_ptr<ArenaWorld::State> s0;                   // the spot's start
    size_t s0At = kNone;
    int deaths = 0, waypoint = 0;
    uint32_t heldBefore = 0;
    int64_t steps = 0;
    void release() {
        for (auto& s : ladder) sim->give(std::move(s));
        ladder.clear();
        if (s0) sim->give(std::move(s0));
        s0At = kNone;
    }
};

/// A variation played: how far it went, and its best join with the best run.
struct Leaf {
    Maneuver m;
    bool joined = false, exit = false;
    int lead = INT_MIN;     // frames ahead of the best run where it joins it
    size_t f = 0, j = 0;    // its frame there (from the load), the best run's frame it joins
    uint32_t held = 0;      // the keys held at f
    float ahead = -1.0e9f;  // its largest lead at equal progress (frames)
    double score() const { return joined ? 1.0e6 + lead : static_cast<double>(ahead); }
};

bool better(const Leaf& a, const Leaf& b) {
    const double x = a.score(), y = b.score();
    if (x != y) return x > y;
    return a.m < b.m;
}

template <class F>
void parallel(int n, F&& f) {
    if (n <= 1) {
        f(0);
        return;
    }
    std::vector<std::thread> pool;
    for (int i = 0; i < n; ++i) pool.emplace_back([&f, i] { f(i); });
    for (auto& t : pool) t.join();
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

/// Plays variations from the spot's start in one lane: a whole tree of them (the grid), or one.
class Walker {
public:
    struct Cursor {
        int k = 0;            // frames played since the start
        uint32_t held = 0;    // the keys sent last
        float ahead = -1.0e9f;
        int stopAt = INT_MAX;
        Leaf join;            // the best join so far (joined, lead, f, j, held)
    };
    Walker(Lane& lane, const Spot& spot, const TrajectoryOptimizer::Options& o) : L_(lane), sp_(spot), behind_(o.behind * 120.0f) {}

    Maneuver m;
    Cursor c;
    std::vector<InputFrame>* record = nullptr;

    void begin() {
        L_.sim->arena().restore(*L_.s0);
        c = Cursor{};
        c.held = L_.heldBefore;
        // The fastest Nova goes sideways (the bound): what the best run did, a slide, a wall jump;
        // with a power that has a key, no bound (a dash may go faster).
        const Player& p = player();
        runSpeed_ = p.config().maxRunSpeed;
        vmax_ = std::max({L_.g.maxSpeedX, p.config().slideSpeed, p.config().wallJumpVelocity.x, runSpeed_}) * 1.25f;
        for (const auto& pw : p.powers())
            if (pw->trigger() != PowerTrigger::Passive) vmax_ = 1.0e6f;
    }
    /// The world and the cursor now (put back with back()).
    struct Mark {
        std::unique_ptr<ArenaWorld::State> state;
        Cursor c;
        Maneuver m;
    };
    Mark mark() {
        Mark k{L_.sim->take(), c, m};
        L_.sim->arena().save(*k.state);
        return k;
    }
    void back(Mark& k) {
        L_.sim->arena().restore(*k.state);
        c = k.c;
        m = k.m;
    }
    void forget(Mark& k) { L_.sim->give(std::move(k.state)); }
    const Player& player() const { return L_.sim->world().player(); }
    size_t now() const { return sp_.start + static_cast<size_t>(c.k); }

    /// The frame the variation sends now.
    InputFrame frame() const {
        const Player& p = player();
        const int k = c.k;
        int dx = m.dir;
        if (m.momentum && !p.grounded() && p.vel.x * static_cast<float>(m.dir) > runSpeed_ + 0.5f) dx = 0;
        bool jump = false, down = false, up = false;
        if (m.pressAt >= 0 && k >= m.pressAt && k - m.pressAt < m.hold) jump = true;
        const int t2 = m.secondAt();
        if (t2 >= 0 && k >= t2 && k - t2 < m.hold2) jump = true;
        if (m.slideAt >= 0 && k == m.slideAt) down = true;
        if (m.drop && m.pressAt >= 0 && k >= m.pressAt && k < m.pressAt + 2) down = true;
        if (m.fastFall && !p.grounded() && p.vel.y > 0.0f) {
            const int last = t2 >= 0 ? (m.hold2 < Maneuver::kHeld ? t2 + m.hold2 : INT_MAX) : (m.pressAt >= 0 ? (m.hold < Maneuver::kHeld ? m.pressAt + m.hold : INT_MAX) : 0);
            if (k >= last) down = true;
        }
        uint32_t h = 0;
        if (m.key >= 0 && k >= m.keyAt && k < m.keyAt + m.keyHold) {
            h |= 1u << m.key;
            if (k == m.keyAt) {
                dx = m.keyX;
                up = m.keyY < 0;
                down = m.keyY > 0;
            }
        }
        if (dx > 0) h |= keyBit(Action::MoveRight);
        if (dx < 0) h |= keyBit(Action::MoveLeft);
        if (down) h |= keyBit(Action::MoveDown);
        if (up) h |= keyBit(Action::MoveUp);
        if (jump) h |= keyBit(Action::Jump);
        InputFrame f;
        f.held = h;
        f.pressed = h & ~c.held;
        f.released = c.held & ~h;
        f.moveX = static_cast<int8_t>(dx > 0 ? 127 : (dx < 0 ? -127 : 0));
        f.moveY = static_cast<int8_t>(down == up ? 0 : (down ? 127 : -127));
        return f;
    }

    /// One frame, nothing checked (a variation played again up to its join).
    void stepRaw() {
        const InputFrame f = frame();
        L_.sim->arena().step(f);
        ++L_.steps;
        if (record) record->push_back(f);
        c.held = f.held;
        ++c.k;
    }

    /// One frame. False when the variation ends here: broken (a death, the category), dropped (behind
    /// the best run at equal progress, or unable to reach its path ahead of it), past the spot's end;
    /// and, when `mayStop` (no decision of its own left), back on the best run's path no earlier, or
    /// settled after a join.
    bool step(bool mayStop) {
        stepRaw();
        Sim& sim = *L_.sim;
        const size_t at = now();
        if (sim.broken(L_.deaths)) return false;
        if (sim.atExit()) {
            // The exit reached by the variation itself.
            const int lead = static_cast<int>(L_.g.exitAt) - static_cast<int>(at);
            if (lead >= kMerge && lead > c.join.lead) {
                c.join.joined = c.join.exit = true;
                c.join.lead = lead, c.join.f = at, c.join.j = L_.g.exitAt, c.join.held = c.held;
            }
            return false;
        }
        const Player& p = player();
        // Against the best run's clock at equal progress.
        const Progress pr = sim.measure(L_.waypoint);
        const auto& P = L_.g.potential;
        const auto F = static_cast<float>(std::lower_bound(P.begin(), P.end(), pr.potential) - P.begin());
        const float ahead = F - static_cast<float>(at);
        if (c.k > m.firstDecision()) c.ahead = std::max(c.ahead, ahead);
        if (ahead < -behind_ || !p.alive()) return false;
        // Admissible: still able to reach the best run's path ahead of it before the joins end.
        bool reachable = false;
        for (size_t j = at + 1; j <= sp_.joinEnd && !reachable; ++j)
            reachable = std::abs(p.feet().x - L_.g.feet[j].x) - 4.0f <= vmax_ * kStep * static_cast<float>(j - at);
        if (!reachable) return false;
        // The same place, speed and state as the best run at a frame j: ahead of it (j > at), a join.
        const size_t j = match(p);
        if (j != kNone) {
            const int lead = static_cast<int>(j) - static_cast<int>(at);
            if (lead >= kMerge) {
                if (lead > c.join.lead) {
                    c.join.joined = true;
                    c.join.exit = false;
                    c.join.lead = lead, c.join.f = at, c.join.j = j, c.join.held = c.held;
                }
                if (c.stopAt == INT_MAX) c.stopAt = c.k + kSettle;
            } else if (mayStop) {
                return false;  // on the best run's path again, no earlier
            }
        }
        if (mayStop && c.k >= c.stopAt) return false;
        return at + 1 < sp_.end;
    }

    /// The leaf of the variation as it is now.
    Leaf leaf() const {
        Leaf l = c.join;
        l.m = m;
        l.ahead = c.ahead;
        return l;
    }

    /// Walks the variation on from the cursor; at each frame base + v (v in `at`, ascending), its
    /// state is saved, `branch(v)` explores the deeper decision, and the state is put back. The leaf
    /// of the variation with this decision never taken ends the walk.
    template <class Branch>
    void level(int base, const std::vector<int>& at, Branch&& branch, std::vector<Leaf>& out) {
        size_t next = 0;
        for (;;) {
            while (next < at.size() && base + at[next] < c.k) ++next;
            if (next < at.size() && base + at[next] == c.k) {
                Mark k = mark();
                branch(at[next]);
                back(k);
                forget(k);
                ++next;
                continue;
            }
            if (!step(next >= at.size())) break;
        }
        out.push_back(leaf());
    }

    /// One variation, played from the start.
    Leaf eval(const Maneuver& mm) {
        begin();
        m = mm;
        const int last = m.lastDecision();
        while (step(c.k >= last)) {
        }
        return leaf();
    }

private:
    size_t match(const Player& p) const {
        const RunStudy& g = L_.g;
        const Track& t = L_.t;
        const Vec2 feet = p.feet();
        const bool grounded = p.grounded();
        const uint8_t cls = stateClass(p.state());
        size_t found = kNone;
        const size_t hi = std::min(sp_.joinEnd, t.grounded.size() - 1);
        for (size_t j = sp_.start; j <= hi; ++j) {
            if (std::abs(feet.x - g.feet[j].x) > 3.0f || std::abs(feet.y - g.feet[j].y) > 3.0f) continue;
            if (grounded != (t.grounded[j] != 0) || cls != t.state[j]) continue;
            if (std::abs(p.vel.x - g.vel[j].x) > 20.0f) continue;
            if (!grounded && (std::abs(p.vel.y - g.vel[j].y) > 40.0f || p.airJumpsLeft() != t.airJumps[j])) continue;
            if (p.energy + 1.0f < t.energy[j]) continue;
            found = j;
        }
        return found;
    }

    Lane& L_;
    const Spot& sp_;
    float behind_;
    float runSpeed_ = 150.0f, vmax_ = 1.0e6f;
};

/// The coarse grid of a spot: the first press, its hold, the second press, its hold.
struct Grid {
    std::vector<int> press, hold, gap, hold2;
};

Grid coarseGrid(const TrajectoryOptimizer::Options& o) {
    Grid G;
    for (int t = 0; t < o.pressRange; t += 4) G.press.push_back(t);
    G.hold = {3, 8, 14, 22, 32, 46};
    G.gap = {1, 5, 11, 20, 32};
    G.hold2 = {4, 10, 18, 28, 44};
    return G;
}

/// The grid's tree below a first press at `t` (or none: t < 0), in direction `dir`.
void gridRoot(Walker& w, int8_t dir, int t, const Grid& G, std::vector<Leaf>& out) {
    w.begin();
    w.m = Maneuver{};
    w.m.dir = dir;
    if (t < 0) {
        w.level(0, {}, [](int) {}, out);  // no press: running on
        return;
    }
    while (w.c.k < t)
        if (!w.step(false)) return;
    // The second press, then its hold, after the first is let go at frame `rel`.
    auto second = [&](int rel) {
        w.m.gap = -1;
        w.level(rel, G.gap, [&](int gp) {
            w.m.gap = static_cast<int16_t>(gp);
            w.m.hold2 = Maneuver::kHeld;
            w.level(rel + gp, G.hold2, [&](int h2) {
                w.m.hold2 = static_cast<int16_t>(h2);
                w.level(0, {}, [](int) {}, out);
            }, out);
        }, out);
    };
    Walker::Mark at = w.mark();
    // The jump at t, held as long as each value of the grid.
    w.m.pressAt = static_cast<int16_t>(t);
    w.m.hold = Maneuver::kHeld;
    w.level(t, G.hold, [&](int h) {
        w.m.hold = static_cast<int16_t>(h);
        second(t + h);
    }, out);
    // On the ground: the drop through a one-way platform (Down + Jump), when it is one.
    w.back(at);
    w.forget(at);
    if (!w.player().grounded()) return;
    w.m.pressAt = static_cast<int16_t>(t);
    w.m.drop = true;
    w.m.hold = 1;
    if (!w.step(false)) return;
    if (w.player().grounded() || w.player().vel.y < 0.0f) return;  // a jump (tried above), not a drop
    second(t + 1);
}

}  // namespace

int Maneuver::lastDecision() const {
    int last = 0;
    if (slideAt >= 0) last = std::max(last, slideAt + 1);
    if (pressAt >= 0) last = std::max(last, hold < kHeld ? pressAt + hold : pressAt + 1);
    if (const int t2 = secondAt(); t2 >= 0) last = std::max(last, hold2 < kHeld ? t2 + hold2 : t2 + 1);
    if (key >= 0) last = std::max(last, keyAt + keyHold);
    return last;
}

int Maneuver::firstDecision() const {
    int first = INT_MAX;
    if (slideAt >= 0) first = std::min<int>(first, slideAt);
    if (pressAt >= 0) first = std::min<int>(first, pressAt);
    if (key >= 0) first = std::min<int>(first, keyAt);
    return first == INT_MAX ? 0 : first;
}

std::string Maneuver::describe() const {
    std::string s = dir > 0 ? "vers la droite" : "vers la gauche";
    char b[200];
    if (slideAt >= 0) {
        std::snprintf(b, sizeof b, ", glissade à +%d", slideAt);
        s += b;
    }
    if (pressAt >= 0) {
        if (drop) std::snprintf(b, sizeof b, ", descente à travers la plateforme à +%d", pressAt);
        else if (hold >= kHeld) std::snprintf(b, sizeof b, ", saut à +%d tenu jusqu'au bout", pressAt);
        else std::snprintf(b, sizeof b, ", saut à +%d tenu %d images", pressAt, hold);
        s += b;
    } else {
        s += ", sans saut";
    }
    if (secondAt() >= 0) {
        if (hold2 >= kHeld) std::snprintf(b, sizeof b, ", second appui %d images après (+%d) tenu jusqu'au bout", gap, secondAt());
        else std::snprintf(b, sizeof b, ", second appui %d images après (+%d) tenu %d images", gap, secondAt(), hold2);
        s += b;
    }
    if (key >= 0) {
        std::snprintf(b, sizeof b, ", touche %d à +%d (direction %d, %d) tenue %d", key, keyAt, keyX, keyY, keyHold);
        s += b;
    }
    if (momentum) s += ", direction lâchée tant qu'elle va plus vite qu'en courant";
    if (fastFall) s += ", chute rapide";
    return s;
}

TrajectoryStats TrajectoryOptimizer::run(const std::vector<Sim*>& sims, const Slice& slice) {
    TrajectoryStats stats;
    exhausted_ = false;
    if (sims.empty() || slice.best.empty()) return stats;
    const int T = std::max(1, std::min(slice.threads, static_cast<int>(sims.size())));
    const Options o = opt_;
    std::vector<Lane> lanes(static_cast<size_t>(T));
    for (int i = 0; i < T; ++i) lanes[static_cast<size_t>(i)].sim = sims[static_cast<size_t>(i)];
    std::vector<InputFrame> best = slice.best;
    float bestClock = slice.bestClock;
    uint64_t version = 0;
    const uint32_t busy = busyKeys();
    auto log = [&](const std::string& s) {
        if (slice.log) slice.log(s);
    };

    // A lane on the best run: its study, its track and its savestates up to the joins' reach.
    auto refresh = [&](Lane& L) {
        if (L.version == version) return;
        L.release();
        L.version = version;
        Sim& sim = *L.sim;
        L.g = sim.study(best);
        if (!L.g.finishes()) return;
        const size_t sections = L.g.ends.size();
        L.from = std::max<size_t>(slice.keep, 1);
        const size_t last = slice.lastSection < 0 ? sections : static_cast<size_t>(slice.lastSection);
        L.endFrame = std::min(last < sections ? L.g.ends[last] : L.g.exitAt, L.g.exitAt);
        const size_t limit = std::min(L.g.exitAt, L.endFrame + static_cast<size_t>(o.horizon) + 240);
        Track& t = L.t;
        t = Track{};
        sim.arena().restore(sim.origin());
        int wp = 0;
        for (size_t i = 0; i <= limit; ++i) {
            if (i % kRung == 0) {
                auto s = sim.take();
                sim.arena().save(*s);
                L.ladder.push_back(std::move(s));
            }
            const Player& p = sim.world().player();
            t.grounded.push_back(p.grounded() ? 1 : 0);
            t.state.push_back(stateClass(p.state()));
            t.airJumps.push_back(static_cast<int8_t>(p.airJumpsLeft()));
            t.energy.push_back(p.energy);
            const Progress pr = sim.measure(wp);
            wp = pr.waypoint;
            t.waypoint.push_back(static_cast<int16_t>(wp));
            if (i < limit) sim.arena().step(best[i]);
        }
    };

    // The spots of the window: a takeoff, a jump pressed, a loss of speed (Timing's speedDrops: her
    // horizontal speed, a landing, a wall), a turn; the variations start `lookback` frames before,
    // where the best run holds no jump nor power key (none of its presses is cut short). The most
    // time lost first.
    auto findSpots = [&](const Lane& L) {
        const RunStudy& g = L.g;
        const Track& t = L.t;
        const std::vector<float> drops = speedDrops(g);
        std::vector<Spot> out;
        auto isBusy = [&](size_t i) { return i > 0 && i - 1 < best.size() && (best[i - 1].held & busy) != 0; };
        auto frozen = [&](size_t i) { return i < g.frozen.size() && g.frozen[i]; };
        const size_t end = std::min(L.endFrame, g.exitAt);
        for (size_t e = std::max<size_t>(L.from, 1); e < end; ++e) {
            const bool takeoff = t.grounded[e - 1] && !t.grounded[e];
            const bool press = (best[e].pressed & keyBit(Action::Jump)) != 0;
            const bool lost = drops[e] > 8.0f;
            const bool turn = g.vel[e - 1].x * g.vel[e].x < 0.0f && std::abs(g.vel[e - 1].x) > 30.0f && std::abs(g.vel[e].x) > 30.0f;
            if (!takeoff && !press && !lost && !turn) continue;
            size_t s = std::max(L.from, e > static_cast<size_t>(o.lookback) ? e - static_cast<size_t>(o.lookback) : size_t(0));
            size_t s2 = s;
            while (isBusy(s2) && s2 > L.from && s - s2 < 60) --s2;
            if (isBusy(s2))
                for (s2 = s; isBusy(s2) && s2 < e; ++s2) {
                }
            if (isBusy(s2) || s2 < 1) continue;
            s = s2;
            if (!out.empty() && s < out.back().start + 24) continue;  // the spot before covers it
            bool clear = true;
            for (size_t i = s; i <= e && clear; ++i) clear = !frozen(i);
            if (!clear) continue;
            const float clock = g.clockAt[s];
            if (clock < o.fromClock || clock > o.toClock) continue;
            Spot sp;
            sp.start = s;
            sp.end = std::min({s + static_cast<size_t>(o.horizon), g.exitAt, t.grounded.size() - 1});
            for (size_t i = s; i < sp.end; ++i)
                if (frozen(i)) {
                    sp.end = i;
                    break;
                }
            sp.joinEnd = std::min(sp.end + 240, t.grounded.size() - 1);
            const float dx = g.feet[std::min(s + 180, sp.joinEnd)].x - g.feet[s].x;
            if (std::abs(dx) >= 24.0f) sp.dirs = {static_cast<int8_t>(dx > 0.0f ? 1 : -1)};
            else sp.dirs = {1, -1};
            // The frames the best run loses after it to a run at full speed in its direction.
            const float run = L.sim->world().player().config().maxRunSpeed;
            for (size_t i = s; i < std::min(s + 240, sp.joinEnd); ++i)
                sp.loss += std::max(0.0f, 1.0f - std::abs(g.vel[i].x) / run) + (g.vel[i].x * dx < 0.0f ? 1.0f : 0.0f);
            uint64_t h = mix(0x7a1ec7u, s);
            for (size_t i = 0; i <= std::min(sp.joinEnd, best.size() - 1); ++i)
                h = mix(h, (static_cast<uint64_t>(best[i].held) << 16) ^ (static_cast<uint64_t>(best[i].flags) << 8) ^ best[i].choice);
            h = mix(mix(mix(h, static_cast<uint64_t>(o.lookback)), static_cast<uint64_t>(o.pressRange)), static_cast<uint64_t>(o.horizon));
            sp.key = h;
            out.push_back(std::move(sp));
        }
        std::stable_sort(out.begin(), out.end(), [](const Spot& a, const Spot& b) { return a.loss > b.loss; });
        return out;
    };

    // Every lane at the spot's start.
    auto prepare = [&](Lane& L, const Spot& sp) {
        Sim& sim = *L.sim;
        if (L.s0At != sp.start) {
            const size_t k = std::min(L.ladder.size() - 1, sp.start / kRung);
            sim.arena().restore(*L.ladder[k]);
            for (size_t i = k * kRung; i < sp.start; ++i) sim.arena().step(best[i]);
            if (!L.s0) L.s0 = sim.take();
            sim.arena().save(*L.s0);
            L.s0At = sp.start;
        }
        sim.arena().restore(*L.s0);
        L.deaths = sim.world().stats().deaths;
        L.waypoint = L.t.waypoint[sp.start];
        L.heldBefore = sp.start > 0 ? best[sp.start - 1].held : 0;
    };

    // Many variations played, shared among the lanes by index; the results in their order.
    auto evalAll = [&](const Spot& sp, const std::vector<Maneuver>& ms) {
        std::vector<Leaf> out(ms.size());
        parallel(T, [&](int i) {
            Walker w(lanes[static_cast<size_t>(i)], sp, o);
            for (size_t k = static_cast<size_t>(i); k < ms.size(); k += static_cast<size_t>(T)) out[k] = w.eval(ms[k]);
        });
        if (slice.count) slice.count(static_cast<int64_t>(ms.size()));
        stats.tried += static_cast<int64_t>(ms.size());
        return out;
    };

    struct Found {
        bool ok = false;
        float clock = 0.0f;
        std::vector<InputFrame> run;
        // Not carried to the exit: the sections it keeps a lead through on the best run's rest.
        size_t through = 0;
        float lead = 0.0f;
        std::vector<InputFrame> prefix;
    };
    // A join played on: the variation up to it, then the best run's rest from the frame it joins;
    // a faster finish played again from the load.
    auto rejoin = [&](Lane& L, const Spot& sp, const Leaf& lf) {
        Found r;
        Sim& sim = *L.sim;
        Walker w(L, sp, o);
        std::vector<InputFrame> rec;
        w.begin();
        w.m = lf.m;
        w.record = &rec;
        while (w.now() < lf.f) w.stepRaw();
        std::vector<InputFrame> whole(best.begin(), best.begin() + static_cast<std::ptrdiff_t>(sp.start));
        whole.insert(whole.end(), rec.begin(), rec.end());
        if (!lf.exit) {
            auto at = sim.take();
            sim.arena().save(*at);
            size_t played = 0;
            const float c = sim.playToExit(*at, L.g.frames, lf.j, bestClock - 1.0e-4f, &played, w.c.held);
            sim.give(std::move(at));
            L.steps += static_cast<int64_t>(played);
            if (c < 0.0f || c + 1.0e-6f >= bestClock) {
                // The lead does not carry to the exit on the best run's rest (a moving platform, an
                // enemy met at another moment...): how far it keeps it, section by section — a branch
                // may commit to that much and search the rest (Slice::onLead).
                std::vector<InputFrame> on = whole;
                appendSeamed(on, L.g.frames, lf.j, L.g.frames.size() - lf.j);
                const RunStudy s2 = sim.study(on);
                L.steps += static_cast<int64_t>(on.size());
                const size_t total = L.g.ends.size();
                for (size_t k = std::min(s2.ends.size(), total > 0 ? total - 1 : 0); k >= 1; --k) {
                    const float lead = L.g.splitAt(k - 1) - s2.splitAt(k - 1);
                    if (s2.splitAt(k - 1) < 0.0f || lead < 0.05f) continue;
                    r.through = k;
                    r.lead = lead;
                    r.prefix.assign(on.begin(), on.begin() + static_cast<std::ptrdiff_t>(std::min(s2.ends[k - 1], on.size())));
                    break;
                }
                return r;
            }
            appendSeamed(whole, L.g.frames, lf.j, played);
        }
        size_t played = 0;
        const float c = sim.playToExit(sim.origin(), whole, 0, bestClock, &played);
        L.steps += static_cast<int64_t>(played);
        if (c >= 0.0f && c + 1.0e-6f < bestClock) {
            whole.resize(played);
            r.ok = true;
            r.clock = c;
            r.run = std::move(whole);
        }
        return r;
    };

    while (!slice.over()) {
        parallel(T, [&](int i) { refresh(lanes[static_cast<size_t>(i)]); });
        Lane& L0 = lanes[0];
        if (!L0.g.finishes() || L0.ladder.empty()) break;
        const std::vector<Spot> spots = findSpots(L0);
        const Spot* sp = nullptr;
        for (const Spot& s : spots)
            if (!done_.count(s.key)) {
                sp = &s;
                break;
            }
        if (!sp) {
            exhausted_ = true;
            break;
        }
        parallel(T, [&](int i) { prepare(lanes[static_cast<size_t>(i)], *sp); });
        TrajectorySpot rep;
        rep.start = sp->start;
        rep.clock = L0.g.clockAt[sp->start];
        rep.feet = L0.g.feet[sp->start];
        rep.loss = sp->loss;
        rep.before = rep.after = bestClock;
        const int64_t triedBefore = stats.tried;
        auto think = [&](const char* kind, const char* code, std::vector<double> n, const std::string& s = {}) {
            if (slice.think) slice.think(Thought{kind, code, std::move(n), s});
        };
        const bool worthSaying = sp->loss / 120.0f >= kWorthSaying;
        if (worthSaying) think("observe", "spot", {static_cast<double>(rep.clock), static_cast<double>(rep.feet.x), static_cast<double>(sp->loss) / 120.0});

        // 0. The memory: this configuration already solved (here or in another level)? Its keys first;
        // when they join the best run earlier, the grid is not searched (« j'ai déjà fait cette
        // configuration de saut ») — only refined around them.
        prepare(L0, *sp);
        const Configuration cfg = Configuration::of(L0.sim->world(), sp->dirs.front());
        std::optional<Remembered> recalled = o.memory ? o.memory->find(cfg.key) : std::nullopt;
        bool fromMemory = false;
        std::vector<Leaf> all;
        if (recalled) {
            const Leaf lf = evalAll(*sp, {recalled->maneuver}).front();
            fromMemory = lf.joined && lf.lead >= kMerge;
            think("memory", fromMemory ? "recall" : "recall_miss", {static_cast<double>(rep.feet.x), static_cast<double>(cfg.rise), static_cast<double>(cfg.speed)}, recalled->level);
            if (fromMemory) {
                all.push_back(lf);
                ++stats.recalled;
            }
        }

        // 1. The grid: each root (a first press, or none) a tree of savestates, the roots shared by index.
        const Grid G = coarseGrid(o);
        std::vector<std::pair<int8_t, int>> roots;
        for (int8_t d : sp->dirs) {
            roots.push_back({d, -1});
            for (int t : G.press)
                if (sp->start + static_cast<size_t>(t) + 1 < sp->end) roots.push_back({d, t});
        }
        if (fromMemory) roots.clear();
        std::vector<std::vector<Leaf>> rootLeaves(roots.size());
        parallel(T, [&](int i) {
            Walker w(lanes[static_cast<size_t>(i)], *sp, o);
            for (size_t r = static_cast<size_t>(i); r < roots.size() && !slice.over(); r += static_cast<size_t>(T))
                gridRoot(w, roots[r].first, roots[r].second, G, rootLeaves[r]);
        });
        if (slice.over()) break;  // (searched again in the next slice)
        size_t grid = 0;
        for (auto& v : rootLeaves) all.insert(all.end(), v.begin(), v.end()), grid += v.size();
        if (slice.count) slice.count(static_cast<int64_t>(grid));
        stats.tried += static_cast<int64_t>(grid);

        // 2. The best ones refined frame by frame, on every parameter (the press, its hold — its
        // height —, the second press and its hold), one parameter at a time, until none improves.
        auto refine = [&](Leaf cur) {
            for (int pass = 0; pass < 3 && !slice.over(); ++pass) {
                bool improved = false;
                for (int dim = 0; dim < 7 && !slice.over(); ++dim) {
                    std::vector<Maneuver> ms;
                    const Maneuver& b = cur.m;
                    auto vary = [&](int16_t Maneuver::*field, int lo, int hi, int least) {
                        for (int v = lo; v <= hi; ++v) {
                            if (v < least || v == b.*field) continue;
                            Maneuver x = b;
                            x.*field = static_cast<int16_t>(v);
                            if (x.slideAt >= 0 && x.pressAt >= 0 && x.slideAt >= x.pressAt) continue;
                            if (sp->start + static_cast<size_t>(x.lastDecision()) + 1 >= sp->end) continue;
                            ms.push_back(x);
                        }
                    };
                    const int hold = b.hold >= Maneuver::kHeld ? 46 : b.hold, hold2 = b.hold2 >= Maneuver::kHeld ? 44 : b.hold2;
                    if (dim == 0 && b.pressAt >= 0) vary(&Maneuver::pressAt, b.pressAt - 6, b.pressAt + 6, 0);
                    if (dim == 1 && b.pressAt >= 0 && !b.drop) vary(&Maneuver::hold, hold - 8, hold + 8, 1);
                    if (dim == 2 && b.gap >= 0) vary(&Maneuver::gap, b.gap - 6, b.gap + 6, 1);
                    if (dim == 3 && b.gap >= 0) vary(&Maneuver::hold2, hold2 - 8, hold2 + 8, 1);
                    if (dim == 4 && b.slideAt >= 0) vary(&Maneuver::slideAt, b.slideAt - 4, b.slideAt + 4, 0);
                    if (dim == 5 && b.key >= 0) vary(&Maneuver::keyAt, b.keyAt - 4, b.keyAt + 4, 0);
                    if (dim == 6 && b.key >= 0) vary(&Maneuver::keyHold, b.keyHold - 4, b.keyHold + 6, 1);
                    if (ms.empty()) continue;
                    const std::vector<Leaf> res = evalAll(*sp, ms);
                    all.insert(all.end(), res.begin(), res.end());
                    for (const Leaf& l : res)
                        if (better(l, cur)) cur = l, improved = true;
                }
                if (!improved) break;
            }
            return cur;
        };
        std::sort(all.begin(), all.end(), better);
        std::vector<Leaf> seeds;
        for (const Leaf& l : all) {
            if (static_cast<int>(seeds.size()) >= o.seeds) break;
            if (l.m.pressAt < 0 && !l.joined) continue;
            bool close = false;
            for (const Leaf& s : seeds)
                close = close || (s.m.dir == l.m.dir && std::abs(s.m.pressAt - l.m.pressAt) <= 4 && std::abs(std::min<int>(s.m.hold, 60) - std::min<int>(l.m.hold, 60)) <= 6 &&
                                  (s.m.gap < 0) == (l.m.gap < 0));
            if (!close) seeds.push_back(l);
        }
        std::vector<Leaf> refined;
        for (const Leaf& s : seeds) refined.push_back(refine(evalAll(*sp, {s.m}).front()));
        if (slice.over()) break;

        // 3. The extras on the best of them: the fast fall, the direction let go while faster than a
        // run, a slide just before the jump, the drop, the key of each power carried that is ready
        // (its frame around the jumps, its direction, how long it is held); the "no press" run too.
        std::sort(refined.begin(), refined.end(), better);
        std::vector<Maneuver> extras;
        std::vector<Leaf> bases(refined.begin(), refined.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(2, refined.size())));
        for (const Leaf& l : all)
            if (l.m.pressAt < 0 && l.m.key < 0 && l.m.slideAt < 0) {
                bases.push_back(l);
                break;
            }
        std::vector<int8_t> powers;  // the keys of the powers carried, ready at the start (energy, cooldown)
        prepare(L0, *sp);
        for (const auto& pw : L0.sim->world().player().powers())
            if (pw->trigger() == PowerTrigger::SlotKey && pw->ready(L0.sim->world().player())) powers.push_back(static_cast<int8_t>(pw->key()));
        for (const Leaf& b : bases) {
            const Maneuver& m = b.m;
            if (m.pressAt >= 0) {
                Maneuver x = m;
                x.fastFall = true;
                extras.push_back(x);
                x = m;
                x.momentum = true;
                extras.push_back(x);
                for (int back : {2, 5, 9, 14, 20, 28, 38})
                    for (bool mo : {false, true})
                        if (m.pressAt - back >= 0 && m.slideAt < 0) {
                            x = m;
                            x.slideAt = static_cast<int16_t>(m.pressAt - back);
                            x.momentum = mo;
                            extras.push_back(x);
                        }
                if (!m.drop && m.slideAt < 0) {
                    x = m;
                    x.drop = true;
                    x.hold = 1;
                    extras.push_back(x);
                }
            }
            for (const int8_t key : powers) {
                std::vector<int> at;
                if (m.pressAt >= 0) {
                    const int rel = m.hold < Maneuver::kHeld ? m.pressAt + m.hold : m.pressAt + 46;
                    at = {m.pressAt - 6, m.pressAt, m.pressAt + 3, m.pressAt + 8, rel, rel + 4};
                    if (m.secondAt() >= 0) at.insert(at.end(), {m.secondAt(), m.secondAt() + 4, m.secondAt() + 12});
                } else {
                    for (int t = 0; t < o.pressRange; t += 6) at.push_back(t);
                }
                for (int t : at) {
                    if (t < 0) continue;
                    const int8_t d = m.dir;
                    for (const auto& [kx, ky] : std::vector<std::pair<int8_t, int8_t>>{{d, 0}, {d, -1}, {0, -1}, {d, 1}})
                        for (int hold : {1, 10}) {
                            Maneuver x = m;
                            x.key = key;
                            x.keyAt = static_cast<int16_t>(t);
                            x.keyX = kx, x.keyY = ky;
                            x.keyHold = static_cast<int16_t>(hold);
                            if (sp->start + static_cast<size_t>(x.lastDecision()) + 1 < sp->end) extras.push_back(x);
                        }
                }
            }
        }
        if (!extras.empty()) {
            const std::vector<Leaf> res = evalAll(*sp, extras);
            all.insert(all.end(), res.begin(), res.end());
            Leaf top = refined.empty() ? Leaf{} : refined.front();
            const Leaf* gainer = nullptr;
            for (const Leaf& l : res)
                if (better(l, top)) top = l, gainer = &l;
            if (gainer && !slice.over()) refined.push_back(refine(*gainer));
        }
        if (slice.over()) break;

        // 4. The joins played on to the exit, the largest leads first (distinct joins).
        std::sort(all.begin(), all.end(), better);
        std::vector<Leaf> joins;
        for (const Leaf& l : all) {
            if (!l.joined) break;
            bool dup = false;
            for (const Leaf& x : joins) dup = dup || (x.f == l.f && x.j == l.j);
            if (!dup) joins.push_back(l);
            if (static_cast<int>(joins.size()) >= o.rejoins) break;
        }
        std::vector<Found> found(joins.size());
        parallel(T, [&](int i) {
            for (size_t k = static_cast<size_t>(i); k < joins.size(); k += static_cast<size_t>(T)) found[k] = rejoin(lanes[static_cast<size_t>(i)], *sp, joins[k]);
        });
        stats.rejoins += static_cast<int64_t>(joins.size());
        int pick = -1;
        for (size_t k = 0; k < found.size(); ++k)
            if (found[k].ok && (pick < 0 || found[k].clock < found[static_cast<size_t>(pick)].clock)) pick = static_cast<int>(k);

        // The spot's report, the log; a faster run offered and kept.
        const Leaf& top = all.empty() ? Leaf{} : all.front();
        rep.tried = stats.tried - triedBefore;
        rep.joined = top.joined;
        rep.best = pick >= 0 ? joins[static_cast<size_t>(pick)].m : top.m;
        rep.lead = pick >= 0 ? joins[static_cast<size_t>(pick)].lead : (top.joined ? top.lead : 0);
        rep.joinFrame = pick >= 0 ? joins[static_cast<size_t>(pick)].f : top.f;
        rep.joinAt = pick >= 0 ? joins[static_cast<size_t>(pick)].j : top.j;
        ++stats.spots;
        if (top.joined) ++stats.joins;
        char line[600];
        std::snprintf(line, sizeof line, "  trajectoires : %.3f s (image %zu, x %.0f, y %.0f), %lld variations ; meilleure : %s", static_cast<double>(rep.clock),
                      rep.start, static_cast<double>(rep.feet.x), static_cast<double>(rep.feet.y), static_cast<long long>(rep.tried), rep.best.describe().c_str());
        std::string msg = line;
        if (top.joined) {
            std::snprintf(line, sizeof line, " ; rejoint la meilleure course %.3f s plus tôt (image %zu au lieu de %zu)", static_cast<double>(rep.lead) / 120.0,
                          rep.joinFrame, rep.joinAt);
            msg += line;
        } else {
            msg += " ; ne la rejoint pas plus tôt";
        }
        int leadPick = -1;  // no faster run: the join that keeps a lead the furthest (then the largest)
        if (pick < 0)
            for (size_t k = 0; k < found.size(); ++k)
                if (found[k].through > 0 &&
                    (leadPick < 0 || found[k].through > found[static_cast<size_t>(leadPick)].through ||
                     (found[k].through == found[static_cast<size_t>(leadPick)].through && found[k].lead > found[static_cast<size_t>(leadPick)].lead)))
                    leadPick = static_cast<int>(k);
        if (leadPick >= 0) {
            const Found& f = found[static_cast<size_t>(leadPick)];
            rep.leadThrough = f.through;
            rep.leadSeconds = f.lead;
            rep.best = joins[static_cast<size_t>(leadPick)].m;
            ++stats.leads;
            std::snprintf(line, sizeof line, " ; ne tient pas jusqu'à l'arrivée sur la suite de la meilleure course, mais garde %.3f s d'avance à la fin de la section %zu",
                          static_cast<double>(f.lead), f.through);
            msg += line;
            if (slice.onLead) slice.onLead(f.prefix, f.through, f.lead, "trajectoires (" + rep.best.describe() + ")");
        }
        if (pick >= 0) {
            const Found& f = found[static_cast<size_t>(pick)];
            rep.after = f.clock;
            std::snprintf(line, sizeof line, " ; course %.3f s -> %.3f s", static_cast<double>(bestClock), static_cast<double>(f.clock));
            msg += line;
        }
        log(msg);
        // The memory learns what joined the best run earlier (or better than it knew).
        if (o.memory && top.joined && top.lead >= kWorthRemembering && (!recalled || top.lead > recalled->leadFrames)) {
            o.memory->remember(cfg, top.m, top.lead, o.level);
            ++stats.learned;
            think("memory", "learn", {static_cast<double>(cfg.rise), static_cast<double>(cfg.riseAt), static_cast<double>(cfg.speed), top.lead / 120.0},
                  cfg.powers);
        }
        if (pick >= 0) {
            const float gain = bestClock - found[static_cast<size_t>(pick)].clock;
            think("gain", "trajectory", {static_cast<double>(gain), static_cast<double>(rep.feet.x), static_cast<double>(rep.best.pressAt),
                                         static_cast<double>(rep.best.hold >= Maneuver::kHeld ? -1 : rep.best.hold), static_cast<double>(rep.best.secondAt()),
                                         static_cast<double>(rep.best.hold2 >= Maneuver::kHeld ? -1 : rep.best.hold2)});
            if (fromMemory && o.memory) o.memory->used(cfg.key, static_cast<int64_t>(std::lround(gain * 1000.0f)));
        } else if (rep.leadThrough > 0) {
            think("decide", "lead", {static_cast<double>(rep.leadSeconds), static_cast<double>(rep.leadThrough), static_cast<double>(rep.feet.x)});
        } else if (!top.joined && worthSaying) {
            think("observe", "no_gain", {static_cast<double>(rep.feet.x), static_cast<double>(rep.tried)});
        }
        spots_.push_back(rep);
        done_.insert(sp->key);
        if (pick >= 0) {
            Found& f = found[static_cast<size_t>(pick)];
            ++stats.gains;
            best = f.run;
            bestClock = f.clock;
            ++version;
            if (slice.offer) slice.offer(best, bestClock, "trajectoires (" + rep.best.describe() + ")");
        }
    }
    for (Lane& L : lanes) {
        stats.steps += L.steps;
        L.release();
    }
    total_.tried += stats.tried;
    total_.steps += stats.steps;
    total_.rejoins += stats.rejoins;
    total_.spots += stats.spots;
    total_.joins += stats.joins;
    total_.gains += stats.gains;
    total_.leads += stats.leads;
    total_.recalled += stats.recalled;
    total_.learned += stats.learned;
    char line[240];
    std::snprintf(line, sizeof line, "  trajectoires : %d endroit(s), %lld variations, %lld images simulées, %d rejointe(s) plus tôt, %d course(s) plus rapide(s)%s", stats.spots,
                  static_cast<long long>(stats.tried), static_cast<long long>(stats.steps), stats.joins, stats.gains,
                  exhausted_ ? " ; tous les endroits de la fenêtre sont cherchés" : "");
    log(line);
    return stats;
}

}  // namespace cosmic::tas
