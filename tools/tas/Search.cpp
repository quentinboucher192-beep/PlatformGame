#include "tools/tas/Search.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

#include "engine/core/Random.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelData.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "game/tas/ArenaWorld.h"
#include "tools/tas/Course.h"

namespace cosmic::tas {

using Clock = std::chrono::steady_clock;

namespace {

constexpr uint32_t bit(Action a) { return 1u << static_cast<int>(a); }

/// What the beam keeps of a run besides the world: the last frame sent (for the presses of the
/// next one) and how long a cinematic has been « held » to be skipped.
struct Pad {
    uint32_t held = 0;
    int cinematic = 0;
};

/// The frame actually sent at a step: the player's rules for cinematics, presses and releases
/// from the frame before.
InputFrame send(const GameWorld& w, InputFrame want, Pad& pad) {
    InputFrame f = playerFrame(w, want, pad.cinematic);
    f.pressed = f.held & ~pad.held;
    f.released = pad.held & ~f.held;
    pad.held = f.held;
    return f;
}

/// A move: a few steps of a player's inputs.
struct Move {
    int8_t dir = 0;    // -1 left, 0, +1 right
    int8_t vert = 0;   // -1 up, 0, +1 down
    uint8_t len = 16;
    uint8_t jumpAt = 255, jumpHold = 0;
    uint8_t jump2At = 255, jump2Hold = 0;
    int8_t action = -1;  // an Action pressed (attack, interact, a power's key)
    uint8_t actionAt = 0, actionHold = 1;
    uint8_t choice = 0;  // the answer to a pending power offer (first step)
    uint8_t vertSteps = 255;  // Down / Up held only for these first steps

    InputFrame want(int t) const {
        InputFrame f;
        if (dir > 0) f.held |= bit(Action::MoveRight), f.moveX = 127;
        if (dir < 0) f.held |= bit(Action::MoveLeft), f.moveX = -127;
        if (vert != 0 && t < vertSteps) {
            f.held |= vert > 0 ? bit(Action::MoveDown) : bit(Action::MoveUp);
            f.moveY = static_cast<int8_t>(vert > 0 ? 127 : -127);
        }
        if (t >= jumpAt && t < jumpAt + jumpHold) f.held |= bit(Action::Jump);
        if (t >= jump2At && t < jump2At + jump2Hold) f.held |= bit(Action::Jump);
        if (action >= 0 && t >= actionAt && t < actionAt + actionHold) f.held |= 1u << action;
        if (t == 0) f.choice = choice;
        return f;
    }
};

/// The actions worth pressing now: attack, interact, the key of each power carried that has one.
std::vector<int8_t> actionsOf(const GameWorld& w) {
    std::vector<int8_t> a = {static_cast<int8_t>(Action::Attack), static_cast<int8_t>(Action::Interact)};
    for (const auto& p : w.player().powers()) {
        if (p->trigger() == PowerTrigger::Passive) continue;
        const auto k = static_cast<int8_t>(p->key());
        if (std::find(a.begin(), a.end(), k) == a.end()) a.push_back(k);
    }
    return a;
}

struct Tuning {
    int beam = 40;
    int moves = 14;  // moves tried per state
    int len = 16;    // steps per move
};

/// How a section is searched: the beam's tuning, the generations kept after the first arrival.
struct Effort {
    int beam = 24, moves = 10, len = 16;
    int settle = 4;
    bool route = true;  // the route hints followed (SearchOptions::routeHints): not once the best run is as fast there
};

/// A state of the beam (its world in a savestate of the worker's arena).
struct Node {
    int tree = -1;
    uint32_t steps = 0;  // since the section's start
    Pad pad;
    Progress p;
    std::vector<int8_t> actions;  // what can be pressed there (actionsOf)
    int slots = 1;                // power slots (the answers to an offer)
    std::unique_ptr<ArenaWorld::State> snap;
};

struct Candidate {
    int parent = 0;  // the beam's state it comes from; negative: the guide -1 - parent
    std::vector<InputFrame> frames;
    Pad pad;
    Progress p;
    bool arrived = false;
};

/// The way to a section's end found by one worker.
struct Arrival {
    std::vector<InputFrame> frames;  // from the section's start
    float cost = 0.0f;               // steps, a lost heart counting as time lost
    Progress p;
    Pad pad;
    int guide = -1;  // the lead that arrived (its index in the section's leads), or -1: the beam's own
    std::string source = "beam";  // where its way comes from: "beam", or its lead's source
};

/// A way through a section offered to the beam besides its own moves (Worker::section): played on
/// from the section's start while it has not arrived, its state joining the beam at every
/// generation (the beam branches from it), its arrival an arrival like the others. The best run's
/// frames, a guide's (a player's run), or any other source that proposes inputs — each named
/// (`source`: "best", "guide", ...), so that a gain says where it came from.
struct Lead {
    std::string source;
    std::vector<InputFrame> frames;
};

/// The best run (the seed run): what the search keeps of it to be guided by it everywhere
/// (docs/TAS.md « La meilleure course guide la recherche »).
struct Guide {
    std::vector<InputFrame> frames;  // from the load
    std::vector<size_t> ends;        // per section reached: the frame index just after its end
    std::vector<Vec2> feet, vel;     // Nova after i frames (i = 0..exitAt)
    std::vector<float> clockAt;      // the level's clock after i frames
    size_t exitAt = 0;               // the frames up to the exit (all of them when it does not finish)
    float clock = -1.0f;             // its time when it finishes the category, else negative
    bool finishes() const { return clock >= 0.0f; }
    static constexpr size_t kNone = static_cast<size_t>(-1);
    /// The frame index where section `s` starts (kNone: the run does not get there).
    size_t sectionStart(size_t s) const { return s == 0 ? 0 : s - 1 < ends.size() ? ends[s - 1] : kNone; }
    /// The `k` frame indices around `center` (± `radius`) where Nova was nearest to `f` (her feet)
    /// moving as `v`, at least 3 steps apart, the nearest first.
    std::vector<size_t> nearest(Vec2 f, Vec2 v, size_t center, size_t radius, int k) const {
        std::vector<std::pair<float, size_t>> d;
        const size_t lo = center > radius ? center - radius : 0, hi = std::min(exitAt, center + radius);
        for (size_t i = lo; i < hi && i < feet.size(); ++i) d.push_back({(feet[i] - f).length() + 0.02f * (vel[i] - v).length(), i});
        std::sort(d.begin(), d.end());
        std::vector<size_t> out;
        for (const auto& [dist, i] : d) {
            bool close = false;
            for (size_t o : out) close = close || (o > i ? o - i : i - o) < 3;
            if (!close) out.push_back(i);
            if (static_cast<int>(out.size()) >= k) break;
        }
        return out;
    }
};

/// The frames of `src` from `from` (at most `count`), the first one's presses and releases
/// recomputed from the keys `held` before it (the seam of two runs, as a player would press).
std::vector<InputFrame> seamed(const std::vector<InputFrame>& src, size_t from, size_t count, uint32_t held) {
    std::vector<InputFrame> out;
    if (from >= src.size() || count == 0) return out;
    out.assign(src.begin() + static_cast<std::ptrdiff_t>(from), src.begin() + static_cast<std::ptrdiff_t>(std::min(src.size(), from + count)));
    out.front().pressed = out.front().held & ~held;
    out.front().released = held & ~out.front().held;
    return out;
}

/// What an arrival gives at the exit when the rest of the best run follows it (measured), or not.
struct JoinResult {
    bool measured = false;
    float finish = 0.0f;
    size_t seedAt = 0, played = 0;  // the best run's frames from seedAt, `played` of them
    int tried = 0;
    size_t src = 0;  // the run re-joined: 0 the best run, k the guide k (SearchOptions::guides[k - 1])
};

/// An arrival with the finish it gives: measured (played on with the rest of the best run,
/// realigned), or estimated (as if the rest went as the best run's from its own arrival there).
struct Ranked {
    Arrival a;
    JoinResult j;
    float finish() const { return j.finish; }
};

struct Shared {
    const ContentDatabase& content;
    const LevelData& level;
    const Course& course;
    const SearchOptions& opt;
    std::atomic<int64_t> explored{0};
    std::atomic<int64_t> routeKept{0};
    std::atomic<bool> stop{false};
    std::mutex logMutex;
    /// The search ends as soon as it can: asked from outside (SearchOptions::stopRequested), or by itself.
    bool stopped() const { return stop.load() || (opt.stopRequested && opt.stopRequested->load()); }
    /// One more attempt simulated (and in SearchOptions::exploredTotal, read by the progress of a training).
    void count() {
        explored.fetch_add(1, std::memory_order_relaxed);
        if (opt.exploredTotal) opt.exploredTotal->fetch_add(1, std::memory_order_relaxed);
    }
    /// The section (0-based) a state is in: the section ends it has passed.
    int sectionOf(int waypoint) const {
        int s = 0;
        for (int end : course.sectionEnds())
            if (waypoint > end) ++s;
        return std::min(s, std::max(0, static_cast<int>(course.sectionEnds().size()) - 1));
    }
    /// A batch of attempts and the checkpoints they reached, for a training's counts.
    void attempts(const std::vector<AttemptNote>& notes, const std::vector<Crossing>& crossings) const {
        if (opt.onAttempts && (!notes.empty() || !crossings.empty())) opt.onAttempts(notes.data(), notes.size(), crossings.data(), crossings.size());
    }
    Clock::time_point began = Clock::now();
    void log(const std::string& s) {
        std::lock_guard<std::mutex> l(logMutex);
        char at[32];
        std::snprintf(at, sizeof at, "[%5.1f min] ", std::chrono::duration<double>(Clock::now() - began).count() / 60.0);
        if (opt.log) opt.log(at + s);
    }
};

class Worker {
public:
    Worker(Shared& sh, int index)
        : sh_(sh), index_(index), world_(sh.content, sh.level), rng_(sh.opt.seed * 7919u + static_cast<uint64_t>(index) * 104729u + 17u) {
        bound_ = sh.course.bind(const_cast<GameWorld&>(world_.world()));
        const auto& wps = sh.course.waypoints();
        for (size_t k = 0; k < wps.size(); ++k)
            if (wps[k].kind == Waypoint::Kind::Checkpoint && cps_.size() < 32) cps_.push_back(dynamic_cast<const CheckpointEntity*>(bound_.waypointEntity[k]));
        base_ = std::make_unique<ArenaWorld::State>();
        world_.save(*base_);
        world_.save(origin_);
        baseProgress_ = measure(0);
        baseNova_ = nova();
    }

    ArenaWorld& world() { return world_; }
    /// The level just loaded.
    const ArenaWorld::State& origin() const { return origin_; }
    const Progress& baseProgress() const { return baseProgress_; }
    const Pad& basePad() const { return basePad_; }
    uint64_t baseDigest() {
        world_.restore(*base_);
        return world_.digest();
    }
    struct Nova {
        Vec2 feet, vel;
    };
    Nova nova() const { return {world_.world().player().feet(), world_.world().player().vel}; }
    const Nova& baseNova() const { return baseNova_; }

    /// Plays `frames` from the base: the new base.
    void advanceBase(const std::vector<InputFrame>& frames, const Pad& pad) {
        world_.restore(*base_);
        for (const InputFrame& f : frames) world_.step(f);
        world_.save(*base_);
        basePad_ = pad;
        baseProgress_ = measure(baseProgress_.waypoint);
        baseNova_ = nova();
    }

    /// `frames` played from the base, as an arrival (the seed run's section kept as it is).
    Arrival arrivalOf(const std::vector<InputFrame>& frames) {
        world_.restore(*base_);
        for (const InputFrame& f : frames) world_.step(f);
        Arrival a;
        a.frames = frames;
        a.p = measure(baseProgress_.waypoint);
        a.pad.held = frames.empty() ? basePad_.held : frames.back().held;
        a.cost = static_cast<float>(frames.size());
        return a;
    }

    /// `frames` played from the base until the waypoint `target` is reached (a source's way through
    /// a section, for the splice): the arrival, or nothing (the rules broken, or out of frames).
    std::optional<Arrival> playTo(const std::vector<InputFrame>& frames, int target) {
        world_.restore(*base_);
        Arrival a;
        bool reached = false;
        for (const InputFrame& f : frames) {
            world_.step(f);
            a.frames.push_back(f);
            if (broken(baseProgress_.deaths)) return std::nullopt;
            if (reachedTarget(target)) {
                reached = true;
                break;
            }
        }
        a.p = measure(baseProgress_.waypoint);
        if (a.p.rejected || !(reached || a.p.waypoint > target)) return std::nullopt;
        a.pad.held = a.frames.empty() ? basePad_.held : a.frames.back().held;
        const int maxHealth = world_.world().player().maxHealth();
        a.cost = static_cast<float>(a.frames.size()) + 30.0f * static_cast<float>(std::max(0, maxHealth - a.p.health));
        return a;
    }

    /// The base: `frames` played from the load (going back to an earlier section).
    void resetBase(const std::vector<InputFrame>& frames, const Pad& pad) {
        world_.restore(origin_);
        for (const InputFrame& f : frames) world_.step(f);
        world_.save(*base_);
        basePad_ = pad;
        baseProgress_ = measure(0);
        baseNova_ = nova();
    }
    /// The base kept as the start of a window's passes (markStart), and put back (backToStart).
    void markStart() {
        if (!start_) start_ = std::make_unique<ArenaWorld::State>();
        world_.restore(*base_);
        world_.save(*start_);
        startPad_ = basePad_;
        startProgress_ = baseProgress_;
        startNova_ = baseNova_;
    }
    void backToStart() {
        world_.restore(*start_);
        world_.save(*base_);
        basePad_ = startPad_;
        baseProgress_ = startProgress_;
        baseNova_ = startNova_;
    }

    Progress measure(int fromWaypoint) { return sh_.course.measure(const_cast<GameWorld&>(world_.world()), bound_, fromWaypoint); }

    /// The best run played from the load, measured at every step (where it reaches each section's end).
    Guide study(const std::vector<InputFrame>& frames);

    /// The beam from the base to the waypoint `target` (reached: arrivals), until `deadline`.
    /// `guides`: the best run's frames for this section (each is played on and joins the beam).
    std::vector<Arrival> section(int target, Clock::time_point deadline, int maxSteps, const std::vector<Lead>& guides, const Effort& effort);

    /// `a` (from the base) played on with the rest of the best run after its section `s`, realigned
    /// by a few steps: the fastest finish under `clockLimit` (none: it does not re-join). An arrival
    /// at the exit is its own finish. The tries are attempts (`notes`, `crossings`).
    JoinResult rejoin(const Guide& g, size_t s, const Arrival& a, float clockLimit, std::vector<AttemptNote>* notes, std::vector<Crossing>* crossings);

    /// Plays `frames` from `at` until the exit (or `limit` steps): the clock at the exit, or a
    /// negative value when the run does not finish the category. `note` (an attempt of the
    /// refinement): where it stopped, measured, when it does not finish; `crossings`: the
    /// checkpoints it reached.
    float evaluate(const std::vector<InputFrame>& frames, size_t from, const ArenaWorld::State& at, int fromWaypoint, uint32_t limit,
                   AttemptNote* note = nullptr, std::vector<Crossing>* crossings = nullptr);
    /// The note of an attempt that did not finish, from where its furthest point is.
    AttemptNote stoppedAt(const Progress& p, bool broke) const {
        AttemptNote n;
        n.section = sh_.sectionOf(p.waypoint);
        const float len = sh_.course.length();
        n.route = len > 0.0f ? std::clamp(p.potential / len, 0.0f, 1.0f) : 0.0f;
        n.broke = broke;
        return n;
    }

private:
    std::unique_ptr<ArenaWorld::State> takeSnap() {
        if (pool_.empty()) return std::make_unique<ArenaWorld::State>();
        auto s = std::move(pool_.back());
        pool_.pop_back();
        return s;
    }
    void giveSnap(std::unique_ptr<ArenaWorld::State> s) {
        if (s) pool_.push_back(std::move(s));
    }
    std::vector<Move> movesFor(const Node& n, const Tuning& t);
    bool reachedTarget(int target);
    /// The rules broken now: a death beyond `deaths`, the category, the level failed.
    bool broken(int deaths) const {
        const GameWorld& w = world_.world();
        const RunStats& s = w.stats();
        return s.deaths > deaths || (sh_.opt.category == RunCategory::NoDeath && s.deaths > 0) ||
               (sh_.opt.category == RunCategory::NoDamage && (s.deaths > 0 || s.damageTaken > 0)) || w.state() == WorldState::Failed;
    }
    // The checkpoints an attempt reaches: those lit since it started (looked at only on a step
    // where the level's timer took a split).
    struct Watch {
        uint32_t lit = 0;
        size_t splits = 0;
    };
    Watch watch() const {
        Watch w;
        for (size_t k = 0; k < cps_.size(); ++k)
            if (cps_[k] && cps_[k]->activated()) w.lit |= 1u << k;
        w.splits = world_.world().timer().splits().size();
        return w;
    }
    void crossed(Watch& w, std::vector<Crossing>* out) const {
        if (!out) return;
        const auto& sp = world_.world().timer().splits();
        if (sp.size() == w.splits) return;
        w.splits = sp.size();
        for (size_t k = 0; k < cps_.size(); ++k)
            if (!((w.lit >> k) & 1u) && cps_[k] && cps_[k]->activated()) {
                w.lit |= 1u << k;
                out->push_back({static_cast<int>(k), sp.empty() ? world_.world().timer().elapsed() : sp.back()});
            }
    }
    /// The best run played on from `at` (its frame `j` first, pressed from `held`) to the exit:
    /// the clock there, or negative (broken, a death, past `clockLimit`, out of frames).
    float playOn(const ArenaWorld::State& at, const Guide& g, size_t j, uint32_t held, float clockLimit, size_t& played, AttemptNote* note,
                 std::vector<Crossing>* crossings);

    Shared& sh_;
    int index_;
    ArenaWorld world_;
    Course::Bound bound_;
    std::vector<const CheckpointEntity*> cps_;  // the course's checkpoints, CP1 first
    Random rng_;
    std::unique_ptr<ArenaWorld::State> base_, start_, tmp_;
    ArenaWorld::State origin_;
    Pad basePad_, startPad_;
    Progress baseProgress_, startProgress_;
    Nova baseNova_, startNova_;
    std::vector<std::unique_ptr<ArenaWorld::State>> pool_;
};

bool Worker::reachedTarget(int target) {
    const auto& wps = sh_.course.waypoints();
    if (target >= static_cast<int>(wps.size())) return false;
    const Waypoint& wp = wps[static_cast<size_t>(target)];
    const GameWorld& w = world_.world();
    if (wp.kind == Waypoint::Kind::Exit) return w.state() == WorldState::Outro || w.state() == WorldState::Completed;
    const LevelEntity* e = bound_.waypointEntity[static_cast<size_t>(target)];
    if (wp.kind == Waypoint::Kind::Collectible) return !e || e->removed;
    const auto* cp = dynamic_cast<const CheckpointEntity*>(e);
    return !cp || cp->activated();
}

std::vector<Move> Worker::movesFor(const Node& n, const Tuning& t) {
    std::vector<Move> out;
    const bool jumpHeld = (n.pad.held & bit(Action::Jump)) != 0;
    const uint8_t j0 = jumpHeld ? 1 : 0;  // a new press needs the key released first
    const auto len = static_cast<uint8_t>(t.len);
    // A pending power offer: every answer (leave it, or put it in each slot).
    if (n.p.offer) {
        const int slots = std::max(1, n.slots);
        for (int c = 0; c <= slots; ++c) {
            Move m;
            m.len = len;
            m.dir = 1;
            m.choice = c == 0 ? InputFrame::kChoiceLeave : static_cast<uint8_t>(c);
            out.push_back(m);
            m.dir = -1;
            out.push_back(m);
        }
        return out;
    }
    auto basic = [&](int8_t dir, uint8_t hold) {
        Move m;
        m.len = len;
        m.dir = dir;
        if (hold) m.jumpAt = j0, m.jumpHold = hold;
        return m;
    };
    out.push_back(basic(1, 0));
    out.push_back(basic(1, len));
    out.push_back(basic(1, 8));
    out.push_back(basic(-1, 0));
    out.push_back(basic(-1, len));
    out.push_back(basic(0, 0));
    out.push_back(basic(0, len));
    const std::vector<int8_t>& actions = n.actions;
    while (static_cast<int>(out.size()) < t.moves) {
        Move m;
        m.len = len;
        const float d = rng_.unit();
        m.dir = d < 0.55f ? 1 : (d < 0.85f ? -1 : 0);
        const float j = rng_.unit();
        if (j < 0.6f) {
            m.jumpAt = static_cast<uint8_t>(j0 + (rng_.chance(0.3f) ? rng_.rangeInt(0, std::max(0, t.len / 2)) : 0));
            m.jumpHold = static_cast<uint8_t>(rng_.chance(0.4f) ? len : rng_.rangeInt(1, std::max(2, t.len)));
            if (rng_.chance(0.35f)) {
                const int at = m.jumpAt + m.jumpHold + 1 + rng_.rangeInt(0, 10);
                if (at < t.len) {
                    m.jump2At = static_cast<uint8_t>(at);
                    m.jump2Hold = static_cast<uint8_t>(rng_.rangeInt(1, std::max(2, t.len - at)));
                }
            }
        }
        if (rng_.chance(0.5f)) {
            const auto a = actions[static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(actions.size()) - 1))];
            const bool held = (n.pad.held & (1u << a)) != 0;
            m.action = a;
            m.actionAt = static_cast<uint8_t>((held ? 1 : 0) + (rng_.chance(0.5f) ? 0 : rng_.rangeInt(0, std::max(0, t.len - 2))));
            m.actionHold = static_cast<uint8_t>(rng_.chance(0.6f) ? 1 : rng_.rangeInt(2, std::max(3, t.len)));
        }
        if (rng_.chance(0.18f)) {
            m.vert = rng_.chance(0.6f) ? 1 : -1;
            m.vertSteps = static_cast<uint8_t>(rng_.chance(0.5f) ? 255 : rng_.rangeInt(1, std::max(2, t.len)));
        }
        out.push_back(m);
    }
    return out;
}

Guide Worker::study(const std::vector<InputFrame>& frames) {
    Guide g;
    g.frames = frames;
    world_.restore(origin_);
    const auto& ends = sh_.course.sectionEnds();
    auto keep = [&] {
        const Nova n = nova();
        g.feet.push_back(n.feet);
        g.vel.push_back(n.vel);
        g.clockAt.push_back(world_.world().timer().elapsed());
    };
    keep();
    int wp = 0;
    g.exitAt = frames.size();
    for (size_t i = 0; i < frames.size(); ++i) {
        world_.step(frames[i]);
        keep();
        if (g.ends.size() < ends.size()) {
            const Progress p = measure(wp);
            wp = p.waypoint;
            while (g.ends.size() < ends.size() && p.waypoint > ends[g.ends.size()]) g.ends.push_back(i + 1);
        }
        const WorldState st = world_.world().state();
        if (st == WorldState::Outro || st == WorldState::Completed) {
            g.exitAt = i + 1;
            break;
        }
    }
    g.clock = evaluate(frames, 0, origin_, 0, static_cast<uint32_t>(frames.size()));
    world_.restore(origin_);
    return g;
}

std::vector<Arrival> Worker::section(int target, Clock::time_point deadline, int maxSteps, const std::vector<Lead>& guides, const Effort& effort) {
    std::vector<Arrival> arrivals;
    // The path of every state kept, as a tree (parent, frames of its move).
    std::vector<int> treeParent;
    std::vector<std::vector<InputFrame>> treeFrames;
    auto pathOf = [&](int t, const std::vector<InputFrame>* last) {
        std::vector<int> chain;
        for (int k = t; k >= 0; k = treeParent[static_cast<size_t>(k)]) chain.push_back(k);
        std::vector<InputFrame> out;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) out.insert(out.end(), treeFrames[static_cast<size_t>(*it)].begin(), treeFrames[static_cast<size_t>(*it)].end());
        if (last) out.insert(out.end(), last->begin(), last->end());
        return out;
    };
    const Tuning base{effort.beam, effort.moves, effort.len};
    Tuning tune = base;
    // A death is never kept, in any category: the beam goes on with the states still alive, and
    // when none is left it starts the section again, wider (a dead end found too greedily).
    const int baseDeaths = baseProgress_.deaths;
    int restarts = 0;
    std::vector<Node> beam;
    auto makeRoot = [&] {
        Node root;
        root.snap = takeSnap();
        world_.restore(*base_);
        world_.save(*root.snap);
        root.pad = basePad_;
        root.p = baseProgress_;
        root.actions = actionsOf(world_.world());
        root.slots = world_.world().player().powerSlots();
        beam.push_back(std::move(root));
    };
    makeRoot();
    // The guides: the best run's frames for this section, each played on from the base while it
    // has not arrived; its state at every generation joins the beam (the beam branches from it).
    struct GuideRun {
        std::unique_ptr<ArenaWorld::State> snap;
        size_t at = 0;  // its frames played
        Pad pad;
        Progress p;
        int tree = -1;  // its last state kept in the beam's tree
        bool done = false;
    };
    std::vector<GuideRun> gr(guides.size());
    auto startGuides = [&] {
        for (size_t g = 0; g < guides.size(); ++g) {
            GuideRun& G = gr[g];
            G.at = 0;
            G.tree = -1;
            G.done = guides[g].frames.empty();
            G.pad = basePad_;
            G.p = baseProgress_;
            if (G.done) continue;
            if (!G.snap) G.snap = takeSnap();
            world_.restore(*base_);
            world_.save(*G.snap);
        }
    };
    startGuides();
    float bestScore = -1e30f;
    int stalled = 0, firstArrivalGen = -1, gen = 0;
    uint32_t steps = 0;
    auto restart = [&] {
        for (Node& n : beam) giveSnap(std::move(n.snap));
        beam.clear();
        makeRoot();
        startGuides();
        ++restarts;
        tune.beam = std::min(base.beam * 4, std::max(tune.beam, base.beam) * 3 / 2);
        tune.moves = std::min(32, tune.moves + 4);
        tune.len = base.len;
        bestScore = -1e30f;
        stalled = 0;
        steps = 0;
        if (index_ == 0) sh_.log("    plus aucun état en vie : la section recommence, faisceau " + std::to_string(tune.beam));
    };
    while (!sh_.stopped() && Clock::now() < deadline && static_cast<int>(steps) < maxSteps) {
        if (firstArrivalGen >= 0 && gen - firstArrivalGen >= effort.settle) break;
        // Expansion: every move from every state.
        std::vector<Candidate> cands;
        std::vector<AttemptNote> notes;  // the attempts of this generation (a training's counts)
        std::vector<Crossing> crossings;
        const bool noting = static_cast<bool>(sh_.opt.onAttempts);
        for (size_t b = 0; b < beam.size(); ++b) {
            const Node& n = beam[b];
            const std::vector<Move> moves = movesFor(n, tune);
            for (const Move& m : moves) {
                world_.restore(*n.snap);
                Watch wt;
                if (noting) wt = watch();
                Candidate c;
                c.parent = static_cast<int>(b);
                c.pad = n.pad;
                c.frames.reserve(m.len);
                bool brokenMove = false;
                for (int t = 0; t < m.len; ++t) {
                    const InputFrame f = send(world_.world(), m.want(t), c.pad);
                    world_.step(f);
                    c.frames.push_back(f);
                    if (noting) crossed(wt, &crossings);
                    if (broken(baseDeaths)) {
                        brokenMove = true;
                        break;
                    }
                    if (reachedTarget(target)) {
                        c.arrived = true;
                        break;
                    }
                    if (world_.world().powerOffer()) break;  // the next move answers it
                }
                sh_.count();
                // A broken attempt counts where its move started (its last state kept).
                if (brokenMove) {
                    if (noting) notes.push_back(stoppedAt(n.p, true));
                    continue;
                }
                c.p = measure(n.p.waypoint);
                if (c.p.rejected) {
                    if (noting) notes.push_back(stoppedAt(c.p, true));
                    continue;
                }
                if (c.p.waypoint > target) c.arrived = true;
                if (noting) {
                    if (c.p.finished) {
                        AttemptNote f;
                        f.finished = true;
                        f.clock = c.p.clock;
                        notes.push_back(f);
                    } else {
                        notes.push_back(stoppedAt(c.p, false));
                    }
                }
                cands.push_back(std::move(c));
            }
        }
        sh_.attempts(notes, crossings);
        // The guides' states at this generation (as many steps as the beam's moves).
        std::vector<int> guideCand(guides.size(), -1);
        for (size_t g = 0; g < guides.size(); ++g) {
            GuideRun& G = gr[g];
            if (G.done || !G.snap) continue;
            if (G.at >= guides[g].frames.size()) {
                G.done = true;
                continue;
            }
            world_.restore(*G.snap);
            Candidate c;
            c.parent = -1 - static_cast<int>(g);
            c.pad = G.pad;
            const size_t end = std::min(guides[g].frames.size(), G.at + static_cast<size_t>(tune.len));
            bool brokenRun = false;
            for (size_t i = G.at; i < end; ++i) {
                const InputFrame& f = guides[g].frames[i];
                world_.step(f);
                c.frames.push_back(f);
                c.pad.held = f.held;
                if (broken(baseDeaths)) {
                    brokenRun = true;
                    break;
                }
                if (reachedTarget(target)) {
                    c.arrived = true;
                    break;
                }
            }
            G.at += c.frames.size();
            if (brokenRun) {
                G.done = true;
                continue;
            }
            c.p = measure(G.p.waypoint);
            if (c.p.rejected) {
                G.done = true;
                continue;
            }
            if (c.p.waypoint > target) c.arrived = true;
            world_.save(*G.snap);
            G.pad = c.pad;
            G.p = c.p;
            if (c.arrived) G.done = true;
            guideCand[g] = static_cast<int>(cands.size());
            cands.push_back(std::move(c));
        }
        if (cands.empty()) {
            if (arrivals.empty()) restart();
            else break;
            continue;
        }
        // Arrivals.
        for (const Candidate& c : cands) {
            if (!c.arrived) continue;
            Arrival a;
            if (c.parent >= 0) {
                a.frames = pathOf(beam[static_cast<size_t>(c.parent)].tree, &c.frames);
            } else {
                const size_t g = static_cast<size_t>(-1 - c.parent);
                a.frames.assign(guides[g].frames.begin(), guides[g].frames.begin() + static_cast<std::ptrdiff_t>(gr[g].at));
                a.guide = static_cast<int>(g);
                a.source = guides[g].source;
            }
            const int maxHealth = world_.world().player().maxHealth();
            a.cost = static_cast<float>(a.frames.size()) + 30.0f * static_cast<float>(std::max(0, maxHealth - c.p.health));
            a.p = c.p;
            a.pad = c.pad;
            arrivals.push_back(std::move(a));
            if (firstArrivalGen < 0) firstArrivalGen = gen;
        }
        // Selection: the best of each place and situation, a few others for diversity; the
        // guides' states always.
        std::vector<int> order(cands.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cands[static_cast<size_t>(a)].p.score > cands[static_cast<size_t>(b)].p.score; });
        std::vector<int> keep;
        std::unordered_set<uint64_t> cells;
        std::vector<int> rest;
        for (int i : order) {
            const Candidate& c = cands[static_cast<size_t>(i)];
            if (c.arrived || c.parent < 0) continue;
            if (static_cast<int>(keep.size()) < tune.beam * 9 / 10 && cells.insert(c.p.cell).second) keep.push_back(i);
            else rest.push_back(i);
        }
        while (static_cast<int>(keep.size()) < tune.beam && !rest.empty()) {
            const size_t k = static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(rest.size()) - 1));
            if (cells.insert(cands[static_cast<size_t>(rest[k])].p.cell).second || rng_.chance(0.3f)) keep.push_back(rest[k]);
            rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(k));
        }
        for (int gi : guideCand)
            if (gi >= 0 && !cands[static_cast<size_t>(gi)].arrived) keep.push_back(gi);
        // The routes of older runs (SearchOptions::routeHints): a few more states, those that follow
        // a route the furthest (the latest point of it within kRouteRadius px of Nova's feet).
        if (const auto& hints = sh_.opt.routeHints; !hints.empty() && effort.route) {
            const float r2 = SearchOptions::kRouteRadius * SearchOptions::kRouteRadius;
            std::vector<std::pair<size_t, int>> along;
            for (int i : rest) {
                const Candidate& c = cands[static_cast<size_t>(i)];
                if (c.arrived || c.parent < 0 || std::find(keep.begin(), keep.end(), i) != keep.end()) continue;
                size_t far = 0;
                bool near = false;
                for (size_t h = 0; h < hints.size(); ++h)
                    if ((hints[h].feet - c.p.feet).lengthSq() <= r2) far = h, near = true;
                if (near) along.push_back({far, i});
            }
            std::sort(along.begin(), along.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            const size_t extra = static_cast<size_t>(std::max(1, tune.beam / 6));
            std::unordered_set<size_t> spots;
            for (const auto& [h, i] : along) {
                if (spots.size() >= extra) break;
                if (!spots.insert(h / 8).second) continue;  // not all on the same stretch of it
                keep.push_back(i);
                sh_.routeKept.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Progress and stall: the beam widens and the moves lengthen while nothing gets better.
        const float top = cands[static_cast<size_t>(order.front())].p.score;
        if (top > bestScore + 8.0f) {
            bestScore = top;
            stalled = 0;
            tune.beam = std::max(base.beam, tune.beam * 3 / 4);
            tune.moves = std::max(base.moves, tune.moves - 2);
            tune.len = std::max(base.len, tune.len - 4);
        } else if (++stalled >= std::max(6, 180 / tune.len)) {
            stalled = 0;
            tune.beam = std::min(base.beam * 4, tune.beam * 3 / 2);
            tune.moves = std::min(32, tune.moves + 4);
            tune.len = std::min(48, tune.len + 8);
        }
        // The new generation (each state played again from its parent: one savestate per state kept).
        std::vector<Node> next;
        for (int i : keep) {
            Candidate& c = cands[static_cast<size_t>(i)];
            Node n;
            int parentTree = -1;
            if (c.parent >= 0) {
                const Node& parent = beam[static_cast<size_t>(c.parent)];
                world_.restore(*parent.snap);
                for (const InputFrame& f : c.frames) world_.step(f);
                parentTree = parent.tree;
                n.steps = parent.steps + static_cast<uint32_t>(c.frames.size());
            } else {
                const GuideRun& G = gr[static_cast<size_t>(-1 - c.parent)];
                world_.restore(*G.snap);
                parentTree = G.tree;
                n.steps = static_cast<uint32_t>(G.at);
            }
            n.snap = takeSnap();
            world_.save(*n.snap);
            n.pad = c.pad;
            n.p = c.p;
            n.actions = actionsOf(world_.world());
            n.slots = world_.world().player().powerSlots();
            treeParent.push_back(parentTree);
            treeFrames.push_back(std::move(c.frames));
            n.tree = static_cast<int>(treeParent.size()) - 1;
            if (c.parent < 0) gr[static_cast<size_t>(-1 - c.parent)].tree = n.tree;
            next.push_back(std::move(n));
        }
        for (Node& n : beam) giveSnap(std::move(n.snap));
        beam = std::move(next);
        if (beam.empty()) {
            if (!arrivals.empty()) break;
            restart();
            continue;
        }
        steps += static_cast<uint32_t>(tune.len);
        ++gen;
        if (gen % 25 == 0 && index_ == 0) {
            const Progress& p = cands[static_cast<size_t>(order.front())].p;
            char line[512];
            std::snprintf(line, sizeof line, "    génération %d : %.1f s, repère %d, potentiel %.0f px, jalons %d, pieds (%.0f, %.0f), état %d, santé %d, morts %d, arène %d (%d), faisceau %d, coups %d x %d pas", gen,
                          static_cast<double>(p.clock), p.waypoint, static_cast<double>(p.potential), p.milestones, static_cast<double>(p.feet.x),
                          static_cast<double>(p.feet.y), p.state, p.health, p.deaths, p.fighting ? 1 : 0, p.enemiesLeft, tune.beam, tune.moves, tune.len);
            sh_.log(line);
        }
    }
    for (Node& n : beam) giveSnap(std::move(n.snap));
    for (GuideRun& G : gr) giveSnap(std::move(G.snap));
    std::stable_sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) { return a.cost < b.cost; });
    // The six earliest, and the guides' arrivals whatever their rank (the best run's own way).
    std::vector<Arrival> out;
    for (size_t i = 0; i < arrivals.size(); ++i)
        if (i < 6 || arrivals[i].guide >= 0) out.push_back(std::move(arrivals[i]));
    return out;
}

float Worker::playOn(const ArenaWorld::State& at, const Guide& g, size_t j, uint32_t held, float clockLimit, size_t& played, AttemptNote* note,
                     std::vector<Crossing>* crossings) {
    world_.restore(at);
    const int deaths = world_.world().stats().deaths;
    Watch wt;
    if (crossings) wt = watch();
    played = 0;
    auto stopped = [&](bool broke) {
        if (note) *note = stoppedAt(measure(0), broke);
        return -1.0f;
    };
    for (size_t i = j; i < g.frames.size(); ++i) {
        InputFrame f = g.frames[i];
        if (i == j) {
            f.pressed = f.held & ~held;
            f.released = held & ~f.held;
        }
        world_.step(f);
        ++played;
        crossed(wt, crossings);
        if (broken(deaths)) return stopped(true);
        const GameWorld& w = world_.world();
        if (w.state() == WorldState::Outro || w.state() == WorldState::Completed) {
            if (sh_.opt.category == RunCategory::HundredPercent && w.stats().collectibles() < w.stats().totalCollectibles()) return stopped(false);
            if (note) {
                *note = AttemptNote{};
                note->finished = true;
                note->clock = w.timer().elapsed();
            }
            return w.timer().elapsed();
        }
        if (w.timer().elapsed() > clockLimit) return stopped(false);
    }
    return stopped(false);
}

JoinResult Worker::rejoin(const Guide& g, size_t s, const Arrival& a, float clockLimit, std::vector<AttemptNote>* notes, std::vector<Crossing>* crossings) {
    JoinResult r;
    world_.restore(*base_);
    for (const InputFrame& f : a.frames) world_.step(f);
    if (a.p.finished) {
        const GameWorld& w = world_.world();
        if (sh_.opt.category == RunCategory::HundredPercent && w.stats().collectibles() < w.stats().totalCollectibles()) return r;
        r.measured = true;
        r.finish = w.timer().elapsed();
        return r;
    }
    if (!g.finishes() || s >= g.ends.size()) return r;
    if (!tmp_) tmp_ = std::make_unique<ArenaWorld::State>();
    world_.save(*tmp_);
    // Where to take the best run up: where Nova was nearest to her state now, around its own
    // arrival there; and its arrival itself, a step or two either way.
    const Nova n = nova();
    std::vector<size_t> js = g.nearest(n.feet, n.vel, g.ends[s], 120, 3);
    for (int d : {0, -2, 2}) {
        const auto j = static_cast<ptrdiff_t>(g.ends[s]) + d;
        if (j > 0 && static_cast<size_t>(j) < g.exitAt && std::find(js.begin(), js.end(), static_cast<size_t>(j)) == js.end()) js.push_back(static_cast<size_t>(j));
    }
    for (size_t j : js) {
        size_t played = 0;
        AttemptNote note;
        const float c = playOn(*tmp_, g, j, a.pad.held, r.measured ? std::min(clockLimit, r.finish) : clockLimit, played, notes ? &note : nullptr, crossings);
        sh_.count();
        ++r.tried;
        if (notes) notes->push_back(note);
        if (c >= 0.0f && (!r.measured || c < r.finish)) {
            r.measured = true;
            r.finish = c;
            r.seedAt = j;
            r.played = played;
        }
    }
    return r;
}

float Worker::evaluate(const std::vector<InputFrame>& frames, size_t from, const ArenaWorld::State& at, int fromWaypoint, uint32_t limit, AttemptNote* note,
                       std::vector<Crossing>* crossings) {
    world_.restore(at);
    Watch wt;
    if (crossings) wt = watch();
    // Where a run that does not finish stopped (measured once, from the start of the route).
    auto stopped = [&](bool broke) {
        if (note) *note = stoppedAt(measure(0), broke);
        return -1.0f;
    };
    for (size_t i = from; i < frames.size() && i < limit; ++i) {
        world_.step(frames[i]);
        crossed(wt, crossings);
        const GameWorld& w = world_.world();
        const RunStats& s = w.stats();
        if (w.state() == WorldState::Failed || (sh_.opt.category == RunCategory::NoDeath && s.deaths > 0) ||
            (sh_.opt.category == RunCategory::NoDamage && (s.deaths > 0 || s.damageTaken > 0)))
            return stopped(true);
        if (w.state() == WorldState::Outro || w.state() == WorldState::Completed) {
            if (sh_.opt.category == RunCategory::HundredPercent && s.collectibles() < s.totalCollectibles()) return stopped(false);
            if (note) {
                *note = AttemptNote{};
                note->finished = true;
                note->clock = w.timer().elapsed();
            }
            return w.timer().elapsed();
        }
    }
    (void)fromWaypoint;
    return stopped(false);
}

/// Counts the input changes (presses and releases) of a run: fewer is a cleaner run.
int changes(const std::vector<InputFrame>& in) {
    int n = 0;
    for (const InputFrame& f : in) n += std::popcount(f.pressed) + std::popcount(f.released);
    return n;
}

/// The presses and releases recomputed after an edit of the held keys.
void repress(std::vector<InputFrame>& in, size_t lo, size_t hi) {
    uint32_t prev = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        InputFrame& f = in[i];
        f.pressed = f.held & ~prev;
        f.released = prev & ~f.held;
        prev = f.held;
        if (i < lo || i > hi) continue;
        // In the frames edited, the stick follows the direction keys held (fully, as a key does).
        const bool r = f.held & bit(Action::MoveRight), l = f.held & bit(Action::MoveLeft);
        const bool d = f.held & bit(Action::MoveDown), u = f.held & bit(Action::MoveUp);
        f.moveX = static_cast<int8_t>(r == l ? 0 : (r ? 127 : -127));
        f.moveY = static_cast<int8_t>(d == u ? 0 : (d ? 127 : -127));
    }
}

}  // namespace

InputFrame playerFrame(const GameWorld& w, InputFrame want, int& cinematicSteps) {
    if (w.cinematic().active() && w.cinematic().skippable()) {
        InputFrame f;
        f.choice = want.choice;
        if (++cinematicSteps >= kSkipHoldSteps) {
            f.flags = InputFrame::SkipCinematic;
            cinematicSteps = 0;
        }
        return f;
    }
    cinematicSteps = 0;
    return want;
}

// ------------------------------------------------------------------ the windows of the rounds

WindowPlanner::WindowPlanner(int sections) : last_(static_cast<size_t>(std::max(0, sections)), 0), gain_(static_cast<size_t>(std::max(0, sections)), 0.0) {}

std::pair<int, int> WindowPlanner::next(int round) const {
    const int n = sections();
    if (n <= 0) return {0, 0};
    const int w = std::min(width(), n);
    // Each window's score: how long ago its sections were searched (in rounds), their past gains,
    // and its stalest section once more (a section left out the longest is taken first).
    int bestStart = 0, bestStalest = -1;
    double bestScore = -1.0;
    for (int s = 0; s + w <= n; ++s) {
        double score = 0.0;
        int stalest = 0;
        for (int k = s; k < s + w; ++k) {
            const int age = std::max(0, round - last_[static_cast<size_t>(k)]);
            score += static_cast<double>(age) + 2.0 * gain_[static_cast<size_t>(k)];
            stalest = std::max(stalest, age);
        }
        score += stalest;
        if (score > bestScore + 1e-9 || (std::abs(score - bestScore) <= 1e-9 && stalest > bestStalest)) {
            bestScore = score;
            bestStart = s;
            bestStalest = stalest;
        }
    }
    return {bestStart + 1, bestStart + w};
}

void WindowPlanner::done(int round, int first, int last, int64_t gainMs) {
    const int n = sections();
    if (n <= 0) return;
    if (first <= 0) first = 1, last = n;  // the whole level
    first = std::clamp(first, 1, n);
    last = std::clamp(last, first, n);
    for (int k = first - 1; k < last; ++k) {
        last_[static_cast<size_t>(k)] = std::max(last_[static_cast<size_t>(k)], round);
        double& g = gain_[static_cast<size_t>(k)];
        g = gainMs > 0 ? std::min(3.0, g + 1.0) : g * 0.5;
    }
}

// ------------------------------------------------------------------ the search

SearchOutcome search(const ContentDatabase& content, const LevelData& level, const SearchOptions& opt) {
    SearchOutcome out;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(static_cast<int64_t>(opt.budgetSeconds * 1000.0));
    Course course(level, content, opt.category);
    Shared sh{content, level, course, opt};
    auto log = [&](const std::string& s) { sh.log(s); };
    {
        char line[200];
        std::snprintf(line, sizeof line, "Parcours : %zu repères, %zu sections, %.0f px", course.waypoints().size(), course.sectionEnds().size(),
                      static_cast<double>(course.length()));
        log(line);
    }
    const int threads = std::max(1, opt.threads);
    std::vector<std::unique_ptr<Worker>> workers;
    for (int i = 0; i < threads; ++i) workers.push_back(std::make_unique<Worker>(sh, i));
    {
        // What a step and a savestate cost here (the log of the search).
        ArenaWorld& aw = workers[0]->world();
        ArenaWorld::State s;
        const auto t0 = Clock::now();
        InputFrame right;
        right.held = bit(Action::MoveRight);
        right.moveX = 127;
        int cine = 0;
        for (int i = 0; i < 1200; ++i) aw.step(playerFrame(aw.world(), right, cine));
        const auto t1 = Clock::now();
        aw.save(s);
        for (int i = 0; i < 50; ++i) aw.restore(s);
        const auto t2 = Clock::now();
        aw.restore(workers[0]->origin());
        char line[200];
        std::snprintf(line, sizeof line, "Coûts : un pas %.1f µs, un état sauvegardé %.2f Mo restauré en %.0f µs",
                      std::chrono::duration<double, std::micro>(t1 - t0).count() / 1200.0, static_cast<double>(s.size) / (1024.0 * 1024.0),
                      std::chrono::duration<double, std::micro>(t2 - t1).count() / 50.0);
        log(line);
    }
    const size_t sections = course.sectionEnds().size();

    // The best run (the seed run): where it reaches each section's end, step by step.
    Guide guide;
    if (!opt.seedRun.empty()) {
        guide = workers[0]->study(opt.seedRun);
        char line[160];
        if (guide.finishes()) std::snprintf(line, sizeof line, "Course de départ : termine la catégorie en %.3f s", static_cast<double>(guide.clock));
        else std::snprintf(line, sizeof line, "Course de départ : ne termine pas la catégorie (utilisée jusqu'où elle va)");
        log(line);
        if (guide.finishes() && opt.onBest) opt.onBest(guide.clock);
    }
    // The guides (SearchOptions::guides: a player's runs), studied the same way: the sources of the
    // search besides the best run (sourceOf(0) is the best run, sourceOf(k) the guide k).
    std::vector<Guide> extra;
    for (const std::vector<InputFrame>& g : opt.guides) {
        if (g.empty()) continue;
        extra.push_back(workers[0]->study(g));
        const Guide& x = extra.back();
        char line[200];
        if (x.finishes())
            std::snprintf(line, sizeof line, "Guide %zu : termine la catégorie en %.3f s, %zu section(s) atteinte(s)", extra.size(), static_cast<double>(x.clock),
                          x.ends.size());
        else std::snprintf(line, sizeof line, "Guide %zu : ne termine pas la catégorie (utilisé jusqu'où il va : %zu section(s))", extra.size(), x.ends.size());
        log(line);
    }
    out.guideSources = static_cast<int>(extra.size());
    const size_t nsrc = 1 + extra.size();
    auto sourceOf = [&](size_t k) -> const Guide& { return k == 0 ? guide : extra[k - 1]; };
    // The best whole run of this search: the seed run, then each faster one found (verified from
    // the load before it is kept).
    std::vector<InputFrame> best;
    float bestClock = -1.0f;
    if (guide.finishes()) best = opt.seedRun, bestClock = guide.clock;
    bool bestJoined = false;
    size_t bestJoinAt = 0, bestJoinSeedAt = 0;
    auto offer = [&](std::vector<InputFrame> run, float clock, bool joined, size_t joinAt, size_t joinSeedAt, const std::string& how, const std::string& source) {
        if (bestClock >= 0.0f && clock + 1e-6f >= bestClock) return;
        char line[200];
        if (bestClock >= 0.0f) std::snprintf(line, sizeof line, "%s : %.3f s -> %.3f s", how.c_str(), static_cast<double>(bestClock), static_cast<double>(clock));
        else std::snprintf(line, sizeof line, "%s : %.3f s", how.c_str(), static_cast<double>(clock));
        log(line);
        best = std::move(run);
        bestClock = clock;
        bestJoined = joined;
        bestJoinAt = joinAt;
        bestJoinSeedAt = joinSeedAt;
        if (opt.onBest) opt.onBest(clock);
        out.bestSource = source;
        if (opt.onRun) opt.onRun(best, clock, source);
    };

    std::vector<InputFrame> run;  // the run so far
    bool splicing = false;        // the splice under way (its gains are the splice's)
    auto adopt = [&](const Arrival& a) {
        std::vector<std::thread> pool;
        for (auto& w : workers) pool.emplace_back([&w, &a] { w->advanceBase(a.frames, a.pad); });
        for (auto& t : pool) t.join();
        run.insert(run.end(), a.frames.begin(), a.frames.end());
    };
    // Every arena must be the same world now (determinism, checked as the search goes).
    auto sameWorlds = [&] {
        const uint64_t d0 = workers[0]->baseDigest();
        for (auto& w : workers)
            if (w->baseDigest() != d0) return false;
        return true;
    };
    // The run so far is the best run's own beginning.
    auto onGuide = [&] { return !guide.frames.empty() && run.size() <= guide.frames.size() && std::equal(run.begin(), run.end(), guide.frames.begin()); };
    // The best run's frames for section `s`, from the base: while the run is the best run's own,
    // its next frames; else its frames of that section by the section's index, and from where
    // Nova was nearest to her state now (realigned).
    auto guidesFor = [&](size_t s) {
        std::vector<Lead> gs;
        const uint32_t held = workers[0]->basePad().held;
        const Worker::Nova n = workers[0]->baseNova();
        for (size_t k = 0; k < nsrc; ++k) {
            const std::string name = k == 0 ? "best" : "guide";
            const Guide& g = sourceOf(k);
            const size_t from = g.sectionStart(s);
            if (g.frames.empty() || from == Guide::kNone || from >= g.exitAt) continue;
            const size_t to = std::min(g.frames.size(), (s < g.ends.size() ? g.ends[s] : g.exitAt) + 240);
            if (k == 0 && onGuide()) {
                if (run.size() < to)
                    gs.push_back({name, std::vector<InputFrame>(g.frames.begin() + static_cast<std::ptrdiff_t>(run.size()), g.frames.begin() + static_cast<std::ptrdiff_t>(to))});
                continue;
            }
            gs.push_back({name, seamed(g.frames, from, to - from, held)});
            for (size_t j : g.nearest(n.feet, n.vel, from, 120, 1))
                if (j != from) gs.push_back({name, seamed(g.frames, j, to - from, held)});
        }
        return gs;
    };
    // A section searched by every thread from the base: the arrivals, each way once.
    // A route hint is a start, never a ceiling: once the best run reaches the section's end no later
    // than the route did (its point nearest to the section's checkpoint), the section is searched
    // without it (its pull stops, the search goes on beyond the player's way).
    auto routeWanted = [&](size_t s) {
        const auto& hints = opt.routeHints;
        if (hints.empty()) return false;
        if (!guide.finishes() || s >= guide.ends.size()) return true;
        const Vec2 at = course.waypoints()[static_cast<size_t>(course.sectionEnds()[s])].feet;
        size_t h = 0;
        for (size_t k = 1; k < hints.size(); ++k)
            if ((hints[k].feet - at).lengthSq() < (hints[h].feet - at).lengthSq()) h = k;
        return guide.clockAt[std::min(guide.ends[s], guide.clockAt.size() - 1)] > hints[h].clock;
    };
    auto searchSection = [&](size_t s, Clock::time_point until, const Effort& effort, const std::vector<Lead>& gs) {
        Effort e = effort;
        e.route = routeWanted(s);
        std::vector<std::vector<Arrival>> found(workers.size());
        {
            std::vector<std::thread> pool;
            for (size_t i = 0; i < workers.size(); ++i) pool.emplace_back([&, i] { found[i] = workers[i]->section(course.sectionEnds()[s], until, 120 * 600, gs, e); });
            for (auto& t : pool) t.join();
        }
        std::vector<Arrival> all;
        for (auto& f : found)
            for (Arrival& a : f) {
                bool seen = false;
                for (const Arrival& o : all) seen = seen || (o.frames.size() == a.frames.size() && o.frames == a.frames);
                if (!seen) all.push_back(std::move(a));
            }
        std::stable_sort(all.begin(), all.end(), [](const Arrival& a, const Arrival& b) { return a.cost < b.cost; });
        return all;
    };
    // The arrivals of section `s` ranked by the finish they give: the first ones (and the
    // guides') played on with the rest of the best run (measured), the others estimated.
    auto rank = [&](size_t s, std::vector<Arrival> all) {
        std::vector<Ranked> r(all.size());
        for (size_t i = 0; i < all.size(); ++i) r[i].a = std::move(all[i]);
        // The sources an arrival can re-join here: the best run and every guide that finishes.
        std::vector<size_t> joinable;
        for (size_t k = 0; k < nsrc; ++k)
            if (sourceOf(k).finishes() && s < sourceOf(k).ends.size()) joinable.push_back(k);
        const bool canJoin = !joinable.empty();
        std::vector<size_t> tried;
        const size_t first = std::max<size_t>(8, 2 * workers.size());
        for (size_t i = 0; i < r.size(); ++i)
            if (r[i].a.p.finished || (canJoin && (i < first || r[i].a.guide >= 0))) tried.push_back(i);
        float refClock = bestClock;
        for (size_t k : joinable)
            if (refClock < 0.0f || sourceOf(k).clock < refClock) refClock = sourceOf(k).clock;
        const float limit = refClock + 5.0f;
        {
            std::vector<std::thread> pool;
            for (size_t wi = 0; wi < workers.size(); ++wi)
                pool.emplace_back([&, wi] {
                    std::vector<AttemptNote> notes;
                    std::vector<Crossing> crossings;
                    const bool noting = static_cast<bool>(opt.onAttempts);
                    for (size_t k = wi; k < tried.size(); k += workers.size()) {
                        Ranked& x = r[tried[k]];
                        // Into each source: the fastest finish (an arrival at the exit is its own).
                        for (size_t src : joinable) {
                            JoinResult j = workers[wi]->rejoin(sourceOf(src), s, x.a, x.j.measured ? std::min(limit, x.j.finish) : limit,
                                                               noting ? &notes : nullptr, noting ? &crossings : nullptr);
                            j.src = src;
                            j.tried += x.j.tried;
                            if (j.measured && (!x.j.measured || j.finish < x.j.finish)) x.j = j;
                            else x.j.tried = j.tried;
                            if (x.a.p.finished) break;
                        }
                    }
                    sh.attempts(notes, crossings);
                });
            for (auto& t : pool) t.join();
        }
        float rest = 0.0f;
        for (size_t k : joinable) {
            const Guide& g = sourceOf(k);
            const float left = g.clock - g.clockAt[std::min(g.ends[s], g.clockAt.size() - 1)];
            if (k == joinable.front() || left < rest) rest = left;
        }
        for (Ranked& x : r) {
            if (x.j.measured) continue;
            // As if the best run's rest followed from here (a heart lost counted as time), with a
            // margin: an arrival that cannot re-join is a guess. Without a best run: by cost.
            const float hurt = (x.a.cost - static_cast<float>(x.a.frames.size())) / 120.0f;
            x.j.finish = canJoin ? x.a.p.clock + rest + hurt + 0.25f : 1.0e6f + x.a.cost;
        }
        // Measured first (by their finish): an arrival that cannot re-join often got there by leaving
        // something the rest needs behind (a power, a lever); then the others, by their estimate.
        std::stable_sort(r.begin(), r.end(), [](const Ranked& a, const Ranked& b) {
            return a.j.measured != b.j.measured ? a.j.measured : a.finish() < b.finish();
        });
        // The fastest measured finish: a whole run, verified from the load, kept when faster.
        const Ranked* top = nullptr;
        for (const Ranked& x : r)
            if (x.j.measured) {
                ++out.rejoins;
                if (!top || x.j.finish < top->j.finish) top = &x;
            }
        if (top && (bestClock < 0.0f || top->j.finish + 1e-6f < bestClock)) {
            std::vector<InputFrame> whole = run;
            whole.insert(whole.end(), top->a.frames.begin(), top->a.frames.end());
            const size_t joinAt = whole.size();
            const std::vector<InputFrame> tail = seamed(sourceOf(top->j.src).frames, top->j.seedAt, top->j.played, top->a.pad.held);
            whole.insert(whole.end(), tail.begin(), tail.end());
            const float c = workers[0]->evaluate(whole, 0, workers[0]->origin(), 0, static_cast<uint32_t>(whole.size()));
            const std::string into = top->j.src == 0 ? std::string() : " (dans le guide " + std::to_string(top->j.src) + ")";
            if (c >= 0.0f)
                offer(std::move(whole), c, top->j.played > 0 && top->j.src == 0, joinAt, top->j.seedAt, "Raccord après la section " + std::to_string(s + 1) + into,
                      splicing ? std::string("splice") : top->a.source == "best" ? std::string("beam") : top->a.source);
            else log("Raccord après la section " + std::to_string(s + 1) + " non reproduit depuis le départ : ignoré");
        }
        return r;
    };
    auto sectionLine = [&](size_t s, const Arrival& w, const Ranked* how) {
        char line[260];
        std::snprintf(line, sizeof line, "Section %zu/%zu : %.3f s au chrono (%zu pas), santé %d, fin %s %.3f s, %lld essais", s + 1, sections,
                      static_cast<double>(w.p.clock), run.size(), w.p.health, how && how->j.measured ? "mesurée" : "estimée",
                      static_cast<double>(how ? how->finish() : 0.0f), static_cast<long long>(sh.explored.load()));
        log(line);
    };

    // The splice (docs/TAS.md « Les guides »): from the load, section by section, every source's
    // way through the section (the best run's and each guide's, by the section's index and
    // realigned on Nova's position) is played from where the splice is; the arrivals are ranked by
    // the finish they give (re-joined into every source, measured) and the best one is taken — a
    // sum of the best segments that really plays. A faster whole run is offered at once (played
    // from the load, then verified like any run); the round then builds on the best run so far.
    auto resetAll = [&](const std::vector<InputFrame>& frames, Pad pad) {
        std::vector<std::thread> pool;
        for (auto& w : workers) pool.emplace_back([&w, &frames, pad] { w->resetBase(frames, pad); });
        for (auto& t : pool) t.join();
    };
    if (opt.splice && !extra.empty() && sections > 0 && opt.startRun.empty() && !sh.stopped() && Clock::now() < deadline) {
        splicing = true;
        log("Assemblage : la meilleure course et " + std::to_string(extra.size()) + " guide(s), section par section");
        run.clear();
        resetAll(run, Pad{});
        for (size_t s = 0; s < sections && !sh.stopped() && Clock::now() < deadline; ++s) {
            if (opt.onStage) opt.onStage(static_cast<int>(s) + 1, static_cast<int>(sections));
            const uint32_t held = workers[0]->basePad().held;
            const Worker::Nova n = workers[0]->baseNova();
            std::vector<std::pair<size_t, std::vector<InputFrame>>> ways;  // (source, its frames from the base)
            for (size_t k = 0; k < nsrc; ++k) {
                const Guide& g = sourceOf(k);
                const size_t from = g.sectionStart(s);
                if (g.frames.empty() || from == Guide::kNone || from >= g.exitAt) continue;
                const size_t to = std::min(g.frames.size(), (s < g.ends.size() ? g.ends[s] : g.exitAt) + 240);
                std::vector<size_t> starts = {from};
                if (s > 0)
                    for (size_t j : g.nearest(n.feet, n.vel, from, 120, 2))
                        if (std::find(starts.begin(), starts.end(), j) == starts.end()) starts.push_back(j);
                for (size_t j : starts)
                    if (j < to) ways.emplace_back(k, seamed(g.frames, j, to - j, held));
            }
            std::vector<std::optional<Arrival>> got(ways.size());
            {
                std::vector<std::thread> pool;
                for (size_t wi = 0; wi < workers.size(); ++wi)
                    pool.emplace_back([&, wi] {
                        for (size_t k = wi; k < ways.size(); k += workers.size()) {
                            got[k] = workers[wi]->playTo(ways[k].second, course.sectionEnds()[s]);
                            sh.count();
                        }
                    });
                for (auto& t : pool) t.join();
            }
            std::vector<Arrival> all;
            for (size_t k = 0; k < got.size(); ++k)
                if (got[k]) {
                    got[k]->guide = static_cast<int>(k);
                    got[k]->source = ways[k].first == 0 ? "best" : "guide";
                    all.push_back(std::move(*got[k]));
                }
            out.spliceWays += static_cast<int>(ways.size());
            out.spliceArrivals += static_cast<int>(all.size());
            if (all.empty()) {
                log("Assemblage : aucune source ne passe la section " + std::to_string(s + 1) + " depuis là");
                break;
            }
            std::vector<Ranked> ranked = rank(s, std::move(all));
            for (const Ranked& x : ranked) out.spliceJoined += x.j.measured ? 1 : 0;
            const Ranked top = ranked.front();
            const size_t src = ways[static_cast<size_t>(top.a.guide)].first;
            adopt(top.a);
            if (!sameWorlds()) {
                out.why = "les mondes des fils de la recherche divergent (non-déterminisme)";
                return out;
            }
            if (src != 0) ++out.splicedSections;
            const std::string from = src == 0 ? std::string("la meilleure course") : "le guide " + std::to_string(src);
            char line[240];
            std::snprintf(line, sizeof line, "Assemblage, section %zu/%zu : %s, %.3f s au chrono, fin %s %.3f s", s + 1, sections, from.c_str(),
                          static_cast<double>(top.a.p.clock), top.j.measured ? "mesurée" : "estimée", static_cast<double>(top.finish()));
            log(line);
            if (top.a.p.finished) break;
        }
        if (!run.empty()) {
            const float c = workers[0]->evaluate(run, 0, workers[0]->origin(), 0, static_cast<uint32_t>(run.size()));
            out.spliceClock = c;
            if (c >= 0.0f) offer(run, c, false, 0, 0, "Assemblage des guides", "splice");
        }
        // The round builds on the best run so far (the splice's, or a re-join's, when faster).
        if (bestClock >= 0.0f && (!guide.finishes() || bestClock + 1e-6f < guide.clock)) {
            guide = workers[0]->study(best);
            char line[160];
            std::snprintf(line, sizeof line, "La recherche part de la meilleure course assemblée : %.3f s", static_cast<double>(guide.clock));
            log(line);
        }
        run.clear();
        resetAll(run, Pad{});
        splicing = false;
    }

    const Effort normal{opt.beam, 10, 16, 4};
    const bool windowed = opt.windowFirst > 0 && guide.finishes() && sections > 0 && guide.ends.size() >= sections;
    size_t kept = 0;
    if (!windowed) {
        // The whole level, section by section. Each section keeps its best arrival for every set of
        // powers carried (the answers to a power offer differ there): when a later section cannot
        // be finished, the search goes back to the section before and takes its next arrival (a
        // few times at most).
        struct Decision {
            size_t runSize = 0;
            Pad pad;
            std::vector<Arrival> options;
            size_t used = 0;
        };
        std::vector<Decision> taken;
        int backtracks = 0;
        size_t s0 = 0;
        if (!opt.startRun.empty()) {
            // A run already under way: its frames kept, the search goes on from the section they end in.
            run = opt.startRun;
            Pad pad;
            pad.held = run.back().held;
            std::vector<std::thread> pool;
            for (auto& w : workers) pool.emplace_back([&w, &run, pad] { w->resetBase(run, pad); });
            for (auto& t : pool) t.join();
            s0 = static_cast<size_t>(sh.sectionOf(workers[0]->baseProgress().waypoint));
        }
        for (size_t s = s0; s < sections;) {
            const auto now = Clock::now();
            if (now >= deadline || sh.stopped()) {
                out.why = sh.stopped() ? "recherche arrêtée avant la fin du niveau" : "budget épuisé avant la fin du niveau";
                break;
            }
            // An even share of what is left (the refinement keeps a sixth of the budget), up to three
            // shares when the section needs them: a section ends as soon as it has its arrivals.
            const auto reserve = (deadline - start) / 6;
            const auto left = deadline - now > reserve * 2 ? deadline - now - reserve : (deadline - now) / 2;
            const auto share = left / static_cast<int64_t>(sections - s);
            if (opt.onStage) opt.onStage(static_cast<int>(s) + 1, static_cast<int>(sections));
            std::vector<Ranked> ranked;
            if (static_cast<int>(s) < opt.keepSeedSections && s < guide.ends.size() && guide.ends[s] > run.size() && onGuide()) {
                // The seed run's section taken as it is (--keep-seed-sections).
                Ranked k;
                k.a = workers[0]->arrivalOf(std::vector<InputFrame>(guide.frames.begin() + static_cast<std::ptrdiff_t>(run.size()),
                                                                    guide.frames.begin() + static_cast<std::ptrdiff_t>(guide.ends[s])));
                ranked.push_back(std::move(k));
            } else {
                const bool following = onGuide();
                const auto gs = guidesFor(s);
                std::vector<Arrival> all = searchSection(s, std::min(deadline, now + share * 3), normal, gs);
                if (!gs.empty() && !following) {
                    ++out.guidedSections;
                    if (std::any_of(all.begin(), all.end(), [](const Arrival& a) { return a.guide >= 0; })) ++out.guideArrivals;
                }
                ranked = rank(s, std::move(all));
            }
            Decision d;
            d.runSize = run.size();
            d.pad = workers[0]->basePad();
            const Ranked* how = ranked.empty() ? nullptr : &ranked.front();
            float howFinish = how ? how->finish() : 0.0f;
            bool howMeasured = how && how->j.measured;
            for (Ranked& x : ranked) {
                bool seen = false;
                for (const Arrival& o : d.options) seen = seen || o.p.powerSet == x.a.p.powerSet;
                if (!seen && d.options.size() < 3) d.options.push_back(std::move(x.a));
            }
            if (d.options.empty()) {
                // Back to the section before, with its next arrival.
                if (s > s0 && !taken.empty() && backtracks < 3 && taken.back().used + 1 < taken.back().options.size() && Clock::now() < deadline && !sh.stopped()) {
                    ++backtracks;
                    --s;
                    Decision& back = taken.back();
                    ++back.used;
                    run.resize(back.runSize);
                    {
                        std::vector<std::thread> pool;
                        for (auto& w : workers) pool.emplace_back([&w, &run, &back] { w->resetBase(run, back.pad); });
                        for (auto& t : pool) t.join();
                    }
                    adopt(back.options[back.used]);
                    log("Retour à la section " + std::to_string(s + 1) + " : son arrivée suivante (autres pouvoirs)");
                    ++s;
                    continue;
                }
                out.why = "section " + std::to_string(s + 1) + " non terminée dans le budget";
                break;
            }
            taken.push_back(std::move(d));
            const Arrival& winner = taken.back().options.front();
            adopt(winner);
            if (!sameWorlds()) {
                out.why = "les mondes des fils de la recherche divergent (non-déterminisme)";
                return out;
            }
            Ranked shown;
            shown.j.measured = howMeasured;
            shown.j.finish = howFinish;
            sectionLine(s, winner, how ? &shown : nullptr);
            if (opt.onSection) opt.onSection(run);
            if (winner.p.finished) break;
            ++s;
        }
        // The run found, played again from the load.
        if (out.why.empty() && !run.empty()) {
            const float c = workers[0]->evaluate(run, 0, workers[0]->origin(), 0, static_cast<uint32_t>(run.size()));
            if (c >= 0.0f) offer(run, c, false, 0, 0, "Course entière trouvée", "beam");
            else if (bestClock < 0.0f) out.why = "la course trouvée ne termine pas la catégorie";
        }
    } else {
        // A window round: the seed run's sections before the window kept exactly, the window
        // searched again and again (deeper each pass), the run re-joining the seed run after it.
        const size_t wa = static_cast<size_t>(std::clamp(opt.windowFirst - 1, 0, static_cast<int>(sections) - 1));
        const size_t wb = static_cast<size_t>(std::clamp(std::max(opt.windowLast, opt.windowFirst) - 1, static_cast<int>(wa), static_cast<int>(sections) - 1));
        out.windowFirst = static_cast<int>(wa) + 1;
        out.windowLast = static_cast<int>(wb) + 1;
        kept = guide.sectionStart(wa);
        out.keptFrames = kept;
        run.assign(guide.frames.begin(), guide.frames.begin() + static_cast<std::ptrdiff_t>(kept));
        {
            Pad pad;
            pad.held = run.empty() ? 0 : run.back().held;
            std::vector<std::thread> pool;
            for (auto& w : workers)
                pool.emplace_back([&w, &run, pad] {
                    w->resetBase(run, pad);
                    w->markStart();
                });
            for (auto& t : pool) t.join();
        }
        {
            char line[200];
            std::snprintf(line, sizeof line, "Tour en fenêtre : sections %zu à %zu ; la course de départ gardée avant (%zu pas), suivie après", wa + 1, wb + 1, kept);
            log(line);
        }
        const auto windowEnd = opt.refine ? deadline - (deadline - start) / 6 : deadline;
        for (int pass = 0; Clock::now() < windowEnd && !sh.stopped(); ++pass) {
            if (pass > 0) {
                std::vector<std::thread> pool;
                for (auto& w : workers) pool.emplace_back([&w] { w->backToStart(); });
                for (auto& t : pool) t.join();
                run.resize(kept);
            }
            const int k = pass % 4;
            const Effort e{opt.beam * (1 + k), 10 + 2 * k, 16, 4 + 2 * k};
            const float before = bestClock;
            std::string end = "interrompue";
            for (size_t s = wa; s < sections; ++s) {
                const auto now = Clock::now();
                if (now >= windowEnd || sh.stopped()) break;
                if (opt.onStage) opt.onStage(static_cast<int>(s) + 1, static_cast<int>(sections));
                const size_t left = s <= wb ? wb - s + 1 : sections - s;
                const auto share = (windowEnd - now) / static_cast<int64_t>(left);
                const bool following = onGuide();
                const auto gs = guidesFor(s);
                std::vector<Arrival> all = searchSection(s, std::min(windowEnd, now + share * 3), e, gs);
                if (!gs.empty() && !following) {
                    ++out.guidedSections;
                    if (std::any_of(all.begin(), all.end(), [](const Arrival& a) { return a.guide >= 0; })) ++out.guideArrivals;
                }
                if (all.empty()) {
                    end = "section " + std::to_string(s + 1) + " sans arrivée";
                    break;
                }
                std::vector<Ranked> ranked = rank(s, std::move(all));
                // After the window, a measured re-join ends the pass (its run was offered).
                if (s >= wb && std::any_of(ranked.begin(), ranked.end(), [](const Ranked& x) { return x.j.measured; })) {
                    end = "raccord après la section " + std::to_string(s + 1);
                    break;
                }
                // The winner: the best finish; on odd passes inside a window of two sections, the
                // earliest arrival (its gain may need the next section searched to show).
                size_t pick = 0;
                if (s < wb && pass % 2 == 1)
                    for (size_t i = 1; i < ranked.size(); ++i)
                        if (ranked[i].a.cost < ranked[pick].a.cost) pick = i;
                const Ranked chosen = ranked[pick];
                adopt(chosen.a);
                if (!sameWorlds()) {
                    out.why = "les mondes des fils de la recherche divergent (non-déterminisme)";
                    return out;
                }
                sectionLine(s, chosen.a, &chosen);
                if (chosen.a.p.finished) {
                    end = "sortie atteinte";
                    break;
                }
            }
            ++out.passes;
            char line[200];
            std::snprintf(line, sizeof line, "Passe %d (faisceau %d, coups %d) : %s ; meilleure course %.3f s%s", pass + 1, e.beam, e.moves, end.c_str(),
                          static_cast<double>(bestClock), bestClock + 1e-6f < before ? " (améliorée)" : "");
            log(line);
        }
    }

    if (bestClock < 0.0f) {
        if (out.why.empty()) out.why = "la course trouvée ne termine pas la catégorie";
        out.explored = sh.explored.load();
        return out;
    }
    out.why.clear();
    std::vector<InputFrame> current = best;
    float currentClock = bestClock;
    out.beforeRefinement = best;
    out.rejoined = bestJoined;
    out.rejoinAt = bestJoinAt;
    out.rejoinSeedAt = bestJoinSeedAt;
    {
        char line[160];
        std::snprintf(line, sizeof line, "Course trouvée : %.3f s ; affinage jusqu'à la fin du budget", static_cast<double>(currentClock));
        log(line);
        if (opt.onBest) opt.onBest(currentClock);
        if (opt.onStage) opt.onStage(0, static_cast<int>(sections));
    }

    // Refinement: a change of input (a press or a release of a key) moved by one or two steps, or
    // a press dropped; kept when the run still finishes the category earlier (or as early with
    // fewer changes of input). The frames of a cinematic stay as they were; in a window round,
    // the frames before the window too.
    const size_t minFrame = std::max<size_t>(1, kept);
    std::mutex mutex;
    int currentChanges = changes(current);
    std::atomic<uint64_t> version{0};
    constexpr size_t kRung = 240;
    auto refine = [&](int wi) {
        Worker& w = *workers[static_cast<size_t>(wi)];
        ArenaWorld& aw = w.world();
        Random rng(opt.seed * 31u + static_cast<uint64_t>(wi) * 977u + 5u);
        std::vector<std::unique_ptr<ArenaWorld::State>> ladder;  // savestates every kRung steps
        std::vector<uint8_t> cinematic;                           // a cinematic plays at that frame
        std::vector<InputFrame> mine;
        uint64_t mineVersion = ~0ull;
        auto rebuild = [&] {
            {
                std::lock_guard<std::mutex> l(mutex);
                mine = current;
                mineVersion = version.load();
            }
            ladder.clear();
            cinematic.assign(mine.size(), 0);
            aw.restore(w.origin());
            for (size_t i = 0; i < mine.size(); ++i) {
                if (i % kRung == 0) {
                    auto s = std::make_unique<ArenaWorld::State>();
                    aw.save(*s);
                    ladder.push_back(std::move(s));
                }
                cinematic[i] = aw.world().cinematic().active() || mine[i].flags != 0 || mine[i].choice != 0;
                aw.step(mine[i]);
            }
        };
        while (!sh.stopped() && Clock::now() < deadline) {
            if (mineVersion != version.load()) rebuild();
            if (mine.size() < minFrame + 4) break;
            const size_t i = static_cast<size_t>(rng.rangeInt(static_cast<int>(minFrame), static_cast<int>(mine.size()) - 1));
            const uint32_t changed = (mine[i].held ^ mine[i - 1].held);
            if (!changed) continue;
            uint32_t b = 0;
            for (int pick = rng.rangeInt(0, std::popcount(changed) - 1), k = 0; k < 32; ++k)
                if (((changed >> k) & 1u) && pick-- == 0) {
                    b = 1u << k;
                    break;
                }
            const bool press = (mine[i].held & b) != 0;
            std::vector<InputFrame> edited = mine;
            size_t lo = i, hi = i;
            const int kind = rng.rangeInt(0, 4);
            if (kind < 4) {
                const int shift = kind < 2 ? kind - 2 : kind - 1;  // -2, -1, +1, +2
                if (shift < 0) {
                    lo = i - std::min<size_t>(i, static_cast<size_t>(-shift));
                    for (size_t k = lo; k < i; ++k) edited[k].held = press ? (edited[k].held | b) : (edited[k].held & ~b);
                } else {
                    hi = std::min(mine.size(), i + static_cast<size_t>(shift));
                    for (size_t k = i; k < hi; ++k) edited[k].held = press ? (edited[k].held & ~b) : (edited[k].held | b);
                }
            } else {
                if (!press) continue;
                // The press dropped: the key is not held until its release.
                for (hi = i; hi < mine.size() && (mine[hi].held & b); ++hi) edited[hi].held &= ~b;
            }
            if (lo < minFrame) continue;  // the window round's kept sections stay exactly as they are
            bool frozen = false;
            for (size_t k = lo > 0 ? lo - 1 : 0; k <= std::min(mine.size() - 1, hi); ++k) frozen = frozen || cinematic[k];
            if (frozen) continue;
            repress(edited, lo, hi);
            const size_t rung = std::min(ladder.size() - 1, (lo > 0 ? lo - 1 : 0) / kRung);
            AttemptNote note;
            std::vector<Crossing> crossings;
            const bool noting = static_cast<bool>(opt.onAttempts);
            const float clock = w.evaluate(edited, rung * kRung, *ladder[rung], 0, static_cast<uint32_t>(edited.size()), noting ? &note : nullptr,
                                           noting ? &crossings : nullptr);
            sh.count();
            if (noting) opt.onAttempts(&note, 1, crossings.data(), crossings.size());
            if (clock < 0.0f) continue;
            const int ch = changes(edited);
            std::lock_guard<std::mutex> l(mutex);
            if (version.load() != mineVersion) continue;  // another thread changed the run meanwhile
            if (!(clock + 1e-6f < currentClock || (clock <= currentClock + 1e-6f && ch < currentChanges))) continue;
            // The run ends at the exit: what the edit made useless after it is cut.
            aw.restore(*ladder[rung]);
            size_t end = rung * kRung;
            while (end < edited.size()) {
                aw.step(edited[end++]);
                const WorldState st = aw.world().state();
                if (st == WorldState::Outro || st == WorldState::Completed) break;
            }
            edited.resize(end);
            if (clock + 1e-6f < currentClock) {
                char line[160];
                std::snprintf(line, sizeof line, "  affinage : %.3f s -> %.3f s", static_cast<double>(currentClock), static_cast<double>(clock));
                sh.log(line);
            }
            current = std::move(edited);
            if (clock + 1e-6f < currentClock && opt.onBest) opt.onBest(clock);
            if (clock + 1e-6f < currentClock) out.bestSource = "polish";
            if (clock + 1e-6f < currentClock && opt.onRun) opt.onRun(current, clock, "polish");
            currentClock = clock;
            currentChanges = changes(current);
            version.fetch_add(1);
        }
    };
    if (opt.refine) {
        std::vector<std::thread> pool;
        for (int i = 0; i < threads; ++i) pool.emplace_back(refine, i);
        for (auto& t : pool) t.join();
    }
    out.routeKept = sh.routeKept.load();
    out.found = true;
    out.inputs = std::move(current);
    out.clock = currentClock;
    out.explored = sh.explored.load();
    return out;
}

// ------------------------------------------------------------------ for the tests

Rejoin rejoinBest(const ContentDatabase& content, const LevelData& level, RunCategory category, const std::vector<InputFrame>& prefix,
                  const std::vector<InputFrame>& best, int section) {
    Rejoin out;
    SearchOptions opt;
    opt.category = category;
    opt.threads = 1;
    Course course(level, content, category);
    Shared sh{content, level, course, opt};
    Worker w(sh, 0);
    const Guide g = w.study(best);
    if (!g.finishes() || section < 0 || static_cast<size_t>(section) >= g.ends.size()) return out;
    Pad pad;
    pad.held = prefix.empty() ? 0 : prefix.back().held;
    w.resetBase(prefix, pad);
    Arrival a;
    a.pad = pad;
    a.p = w.baseProgress();
    a.p.finished = false;
    const JoinResult r = w.rejoin(g, static_cast<size_t>(section), a, g.clock + 5.0f, nullptr, nullptr);
    out.tried = r.tried;
    if (!r.measured) return out;
    out.found = true;
    out.clock = r.finish;
    out.seedAt = r.seedAt;
    out.inputs = prefix;
    const std::vector<InputFrame> tail = seamed(g.frames, r.seedAt, r.played, pad.held);
    out.inputs.insert(out.inputs.end(), tail.begin(), tail.end());
    return out;
}

std::vector<size_t> sectionEndsOf(const ContentDatabase& content, const LevelData& level, RunCategory category, const std::vector<InputFrame>& run) {
    SearchOptions opt;
    opt.category = category;
    Course course(level, content, category);
    Shared sh{content, level, course, opt};
    Worker w(sh, 0);
    return w.study(run).ends;
}

}  // namespace cosmic::tas
