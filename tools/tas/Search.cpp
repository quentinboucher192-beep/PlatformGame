#include "tools/tas/Search.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
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
    int parent = 0;
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
};

struct Shared {
    const ContentDatabase& content;
    const LevelData& level;
    const Course& course;
    const SearchOptions& opt;
    std::atomic<int64_t> explored{0};
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
        base_ = std::make_unique<ArenaWorld::State>();
        world_.save(*base_);
        world_.save(origin_);
        baseProgress_ = measure(0);
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

    /// Plays `frames` from the base: the new base.
    void advanceBase(const std::vector<InputFrame>& frames, const Pad& pad) {
        world_.restore(*base_);
        for (const InputFrame& f : frames) world_.step(f);
        world_.save(*base_);
        basePad_ = pad;
        baseProgress_ = measure(baseProgress_.waypoint);
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

    /// The base: `frames` played from the load (going back to an earlier section).
    void resetBase(const std::vector<InputFrame>& frames, const Pad& pad) {
        world_.restore(origin_);
        for (const InputFrame& f : frames) world_.step(f);
        world_.save(*base_);
        basePad_ = pad;
        baseProgress_ = measure(0);
    }

    Progress measure(int fromWaypoint) { return sh_.course.measure(const_cast<GameWorld&>(world_.world()), bound_, fromWaypoint); }

    /// The beam from the base to the waypoint `target` (reached: arrivals), until `deadline`.
    /// `seedFrames`: the seed run's frames for this section (it joins the beam), or empty.
    std::vector<Arrival> section(int target, Clock::time_point deadline, int maxSteps, const std::vector<InputFrame>& seedFrames);

    /// Plays `frames` from the base until the exit (or `limit` steps): the clock at the exit, or a
    /// negative value when the run does not finish the category. `note` (an attempt of the
    /// refinement): where it stopped, measured, when it does not finish.
    float evaluate(const std::vector<InputFrame>& frames, size_t from, const ArenaWorld::State& at, int fromWaypoint, uint32_t limit,
                   AttemptNote* note = nullptr);
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

    Shared& sh_;
    int index_;
    ArenaWorld world_;
    Course::Bound bound_;
    Random rng_;
    std::unique_ptr<ArenaWorld::State> base_;
    ArenaWorld::State origin_;
    Pad basePad_;
    Progress baseProgress_;
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

std::vector<Arrival> Worker::section(int target, Clock::time_point deadline, int maxSteps, const std::vector<InputFrame>& seedFrames) {
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
    const Tuning base{sh_.opt.beam, 10, 16};
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
    // The seed run joins the beam while it has not arrived (its state at every generation).
    Node seed;
    size_t seedAt = 0;
    if (!seedFrames.empty()) {
        seed.snap = takeSnap();
        world_.restore(*base_);
        world_.save(*seed.snap);
        seed.pad = basePad_;
        seed.p = baseProgress_;
    }
    float bestScore = -1e30f;
    int stalled = 0, firstArrivalGen = -1, gen = 0;
    const int window = 4;  // generations after the first arrival (later ones may arrive in a better state)
    uint32_t steps = 0;
    auto restart = [&] {
        for (Node& n : beam) giveSnap(std::move(n.snap));
        beam.clear();
        makeRoot();
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
        if (firstArrivalGen >= 0 && gen - firstArrivalGen >= window) break;
        // Expansion: every move from every state.
        std::vector<Candidate> cands;
        std::vector<AttemptNote> notes;  // the attempts of this generation (a training's counts)
        const bool noting = static_cast<bool>(sh_.opt.onAttempts);
        for (size_t b = 0; b < beam.size(); ++b) {
            const Node& n = beam[b];
            const std::vector<Move> moves = movesFor(n, tune);
            for (const Move& m : moves) {
                world_.restore(*n.snap);
                Candidate c;
                c.parent = static_cast<int>(b);
                c.pad = n.pad;
                c.frames.reserve(m.len);
                bool broken = false;
                for (int t = 0; t < m.len; ++t) {
                    const InputFrame f = send(world_.world(), m.want(t), c.pad);
                    world_.step(f);
                    c.frames.push_back(f);
                    const RunStats& s = world_.world().stats();
                    if (s.deaths > baseDeaths || (sh_.opt.category == RunCategory::NoDeath && s.deaths > 0) ||
                        (sh_.opt.category == RunCategory::NoDamage && (s.deaths > 0 || s.damageTaken > 0)) || world_.world().state() == WorldState::Failed) {
                        broken = true;
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
                if (broken) {
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
        if (noting && !notes.empty()) sh_.opt.onAttempts(notes.data(), notes.size());
        // The seed run's state at this generation.
        int seedCand = -1;
        if (seed.snap && seedAt < seedFrames.size()) {
            world_.restore(*seed.snap);
            Candidate c;
            c.parent = -1;
            c.pad = seed.pad;
            const size_t end = std::min(seedFrames.size(), seedAt + static_cast<size_t>(tune.len));
            for (size_t i = seedAt; i < end; ++i) {
                world_.step(seedFrames[i]);
                c.frames.push_back(seedFrames[i]);
                c.pad.held = seedFrames[i].held;
                if (reachedTarget(target)) {
                    c.arrived = true;
                    break;
                }
            }
            seedAt += c.frames.size();
            c.p = measure(seed.p.waypoint);
            if (c.p.waypoint > target) c.arrived = true;
            world_.save(*seed.snap);
            seed.pad = c.pad;
            seed.p = c.p;
            seedCand = static_cast<int>(cands.size());
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
            a.frames = c.parent >= 0 ? pathOf(beam[static_cast<size_t>(c.parent)].tree, &c.frames) : std::vector<InputFrame>{};
            if (c.parent < 0) a.frames.assign(seedFrames.begin(), seedFrames.begin() + static_cast<std::ptrdiff_t>(seedAt));
            const int maxHealth = world_.world().player().maxHealth();
            a.cost = static_cast<float>(a.frames.size()) + 30.0f * static_cast<float>(std::max(0, maxHealth - c.p.health));
            a.p = c.p;
            a.pad = c.pad;
            arrivals.push_back(std::move(a));
            if (firstArrivalGen < 0) firstArrivalGen = gen;
        }
        // Selection: the best of each place and situation, a few others for diversity.
        std::vector<int> order(cands.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cands[static_cast<size_t>(a)].p.score > cands[static_cast<size_t>(b)].p.score; });
        std::vector<int> keep;
        std::unordered_set<uint64_t> cells;
        std::vector<int> rest;
        for (int i : order) {
            const Candidate& c = cands[static_cast<size_t>(i)];
            if (c.arrived || i == seedCand) continue;
            if (static_cast<int>(keep.size()) < tune.beam * 9 / 10 && cells.insert(c.p.cell).second) keep.push_back(i);
            else rest.push_back(i);
        }
        while (static_cast<int>(keep.size()) < tune.beam && !rest.empty()) {
            const size_t k = static_cast<size_t>(rng_.rangeInt(0, static_cast<int>(rest.size()) - 1));
            if (cells.insert(cands[static_cast<size_t>(rest[k])].p.cell).second || rng_.chance(0.3f)) keep.push_back(rest[k]);
            rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(k));
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
            const Node& parent = beam[static_cast<size_t>(c.parent)];
            world_.restore(*parent.snap);
            for (const InputFrame& f : c.frames) world_.step(f);
            Node n;
            n.snap = takeSnap();
            world_.save(*n.snap);
            n.pad = c.pad;
            n.p = c.p;
            n.actions = actionsOf(world_.world());
            n.slots = world_.world().player().powerSlots();
            n.steps = parent.steps + static_cast<uint32_t>(c.frames.size());
            treeParent.push_back(parent.tree);
            treeFrames.push_back(std::move(c.frames));
            n.tree = static_cast<int>(treeParent.size()) - 1;
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
    giveSnap(std::move(seed.snap));
    std::sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) { return a.cost < b.cost; });
    if (arrivals.size() > 4) arrivals.resize(4);
    return arrivals;
}

float Worker::evaluate(const std::vector<InputFrame>& frames, size_t from, const ArenaWorld::State& at, int fromWaypoint, uint32_t limit, AttemptNote* note) {
    world_.restore(at);
    // Where a run that does not finish stopped (measured once, from the start of the route).
    auto stopped = [&](bool broke) {
        if (note) *note = stoppedAt(measure(0), broke);
        return -1.0f;
    };
    for (size_t i = from; i < frames.size() && i < limit; ++i) {
        world_.step(frames[i]);
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

    // The seed run: where it arrives at each section's end (when it finishes the category).
    std::vector<size_t> seedEnds;  // frame index after the section's end, per section
    float seedClock = -1.0f;
    if (!opt.seedRun.empty()) {
        Worker& w0 = *workers[0];
        ArenaWorld::State s0;
        w0.world().save(s0);
        size_t sec = 0;
        int wp = 0;
        for (size_t i = 0; i < opt.seedRun.size(); ++i) {
            w0.world().step(opt.seedRun[i]);
            if (sec < course.sectionEnds().size() && (i % 8 == 7 || i + 1 == opt.seedRun.size() || w0.world().world().state() == WorldState::Outro)) {
                Progress p = w0.measure(wp);
                wp = p.waypoint;
                while (sec < course.sectionEnds().size() && p.waypoint > course.sectionEnds()[sec]) seedEnds.push_back(i + 1), ++sec;
            }
            const WorldState st = w0.world().world().state();
            if (st == WorldState::Outro || st == WorldState::Completed) break;
        }
        seedClock = w0.evaluate(opt.seedRun, 0, s0, 0, static_cast<uint32_t>(opt.seedRun.size()));
        w0.world().restore(s0);
        char line[160];
        if (seedClock > 0.0f) std::snprintf(line, sizeof line, "Course de départ : termine la catégorie en %.3f s", static_cast<double>(seedClock));
        else std::snprintf(line, sizeof line, "Course de départ : ne termine pas la catégorie (utilisée jusqu'où elle va)");
        log(line);
        if (seedClock > 0.0f && opt.onBest) opt.onBest(seedClock);
    }

    // Section by section. Each section keeps its best arrival for every set of powers carried
    // (the answers to a power offer differ there): when a later section cannot be finished, the
    // search goes back to the section before and takes its next arrival (a few times at most).
    struct Decision {
        size_t runSize = 0;
        Pad pad;
        std::vector<Arrival> options;
        size_t used = 0;
    };
    std::vector<Decision> taken;
    std::vector<InputFrame> run;
    bool onSeed = !opt.seedRun.empty();
    const size_t sections = course.sectionEnds().size();
    int backtracks = 0;
    auto adopt = [&](const Arrival& a) {
        std::vector<std::thread> pool;
        for (auto& w : workers) pool.emplace_back([&w, &a] { w->advanceBase(a.frames, a.pad); });
        for (auto& t : pool) t.join();
        run.insert(run.end(), a.frames.begin(), a.frames.end());
    };
    for (size_t s = 0; s < sections;) {
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
        std::vector<InputFrame> seedFrames;
        if (onSeed && s < seedEnds.size()) {
            const size_t from = run.size(), to = seedEnds[s];
            if (to > from) seedFrames.assign(opt.seedRun.begin() + static_cast<std::ptrdiff_t>(from), opt.seedRun.begin() + static_cast<std::ptrdiff_t>(to));
        }
        if (opt.onStage) opt.onStage(static_cast<int>(s) + 1, static_cast<int>(sections));
        std::vector<std::vector<Arrival>> found(workers.size());
        if (static_cast<int>(s) < opt.keepSeedSections && !seedFrames.empty()) {
            // The seed run's section taken as it is (--keep-seed-sections).
            found[0].push_back(workers[0]->arrivalOf(seedFrames));
        } else {
            const auto until = std::min(deadline, now + share * 3);
            std::vector<std::thread> pool;
            for (size_t i = 0; i < workers.size(); ++i)
                pool.emplace_back([&, i] { found[i] = workers[i]->section(course.sectionEnds()[s], until, 120 * 600, seedFrames); });
            for (auto& t : pool) t.join();
        }
        std::vector<Arrival> all;
        for (auto& f : found)
            for (Arrival& a : f) all.push_back(std::move(a));
        std::stable_sort(all.begin(), all.end(), [](const Arrival& a, const Arrival& b) { return a.cost < b.cost; });
        Decision d;
        d.runSize = run.size();
        d.pad = workers[0]->basePad();
        for (Arrival& a : all) {
            bool seen = false;
            for (const Arrival& o : d.options) seen = seen || o.p.powerSet == a.p.powerSet;
            if (!seen && d.options.size() < 3) d.options.push_back(std::move(a));
        }
        if (d.options.empty()) {
            // Back to the section before, with its next arrival.
            if (s > 0 && backtracks < 3 && taken.back().used + 1 < taken.back().options.size() && Clock::now() < deadline && !sh.stopped()) {
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
                onSeed = false;
                adopt(back.options[back.used]);
                log("Retour à la section " + std::to_string(s + 1) + " : son arrivée suivante (autres pouvoirs)");
                ++s;
                continue;
            }
            out.why = "section " + std::to_string(s + 1) + " non terminée dans le budget";
            break;
        }
        if (onSeed && (seedFrames.empty() || d.options.front().frames != seedFrames)) onSeed = false;
        taken.push_back(std::move(d));
        const Arrival& winner = taken.back().options.front();
        adopt(winner);
        // Every arena must be the same world now (determinism, checked as the search goes).
        const uint64_t d0 = workers[0]->baseDigest();
        for (auto& w : workers)
            if (w->baseDigest() != d0) {
                out.why = "les mondes des fils de la recherche divergent (non-déterminisme)";
                return out;
            }
        char line[200];
        std::snprintf(line, sizeof line, "Section %zu/%zu : %.3f s au chrono (%zu pas), santé %d, %lld essais", s + 1, sections,
                      static_cast<double>(winner.p.clock), run.size(), winner.p.health, static_cast<long long>(sh.explored.load()));
        log(line);
        if (opt.onSection) opt.onSection(run);
        if (winner.p.finished) break;
        ++s;
    }
    // The run found (else the seed run, when it finishes the category), played again from the load.
    std::vector<InputFrame> current;
    float currentClock = -1.0f;
    if (out.why.empty()) {
        currentClock = workers[0]->evaluate(run, 0, workers[0]->origin(), 0, static_cast<uint32_t>(run.size()));
        if (currentClock >= 0.0f) current = run;
    }
    if (seedClock >= 0.0f && (currentClock < 0.0f || seedClock < currentClock)) {
        current = opt.seedRun;
        currentClock = seedClock;
    }
    if (currentClock < 0.0f) {
        if (out.why.empty()) out.why = "la course trouvée ne termine pas la catégorie";
        out.explored = sh.explored.load();
        return out;
    }
    out.why.clear();
    {
        char line[160];
        std::snprintf(line, sizeof line, "Course trouvée : %.3f s ; affinage jusqu'à la fin du budget", static_cast<double>(currentClock));
        log(line);
        if (opt.onBest) opt.onBest(currentClock);
        if (opt.onStage) opt.onStage(0, static_cast<int>(sections));
    }

    // Refinement: a change of input (a press or a release of a key) moved by one or two steps, or
    // a press dropped; kept when the run still finishes the category earlier (or as early with
    // fewer changes of input). The frames of a cinematic stay as they were.
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
            if (mine.size() < 4) break;
            const size_t i = static_cast<size_t>(rng.rangeInt(1, static_cast<int>(mine.size()) - 1));
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
            bool frozen = false;
            for (size_t k = lo > 0 ? lo - 1 : 0; k <= std::min(mine.size() - 1, hi); ++k) frozen = frozen || cinematic[k];
            if (frozen) continue;
            repress(edited, lo, hi);
            const size_t rung = std::min(ladder.size() - 1, (lo > 0 ? lo - 1 : 0) / kRung);
            AttemptNote note;
            const float clock = w.evaluate(edited, rung * kRung, *ladder[rung], 0, static_cast<uint32_t>(edited.size()), opt.onAttempts ? &note : nullptr);
            sh.count();
            if (opt.onAttempts) opt.onAttempts(&note, 1);
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
            currentClock = clock;
            currentChanges = changes(current);
            version.fetch_add(1);
        }
    };
    {
        std::vector<std::thread> pool;
        for (int i = 0; i < threads; ++i) pool.emplace_back(refine, i);
        for (auto& t : pool) t.join();
    }
    out.found = true;
    out.inputs = std::move(current);
    out.clock = currentClock;
    out.explored = sh.explored.load();
    return out;
}

}  // namespace cosmic::tas
