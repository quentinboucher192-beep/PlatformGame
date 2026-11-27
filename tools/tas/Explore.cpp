#include "tools/tas/Explore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "engine/core/Random.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "tools/tas/Bound.h"
#include "tools/tas/Search.h"
#include "tools/tas/Sim.h"

namespace cosmic::tas {

namespace {

constexpr double kHintHalfLife = 1500.0;  // visits

uint64_t mixKey(uint64_t h, int64_t v) {
    h ^= static_cast<uint64_t>(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xbf58476d1ce4e5b9ull;
}

/// A move of a player: a direction, a jump held a while (and a second one), a key pressed, Down / Up.
struct Move {
    int8_t dir = 0, vert = 0;
    uint8_t len = 16;
    uint8_t jumpAt = 255, jumpHold = 0, jump2At = 255, jump2Hold = 0;
    int8_t action = -1;
    uint8_t actionAt = 0, actionHold = 1, vertSteps = 255, choice = 0;
    InputFrame want(int t) const {
        InputFrame f;
        if (dir > 0) f.held |= keyBit(Action::MoveRight), f.moveX = 127;
        if (dir < 0) f.held |= keyBit(Action::MoveLeft), f.moveX = -127;
        if (vert != 0 && t < vertSteps) {
            f.held |= vert > 0 ? keyBit(Action::MoveDown) : keyBit(Action::MoveUp);
            f.moveY = static_cast<int8_t>(vert > 0 ? 127 : -127);
        }
        if (t >= jumpAt && t < jumpAt + jumpHold) f.held |= keyBit(Action::Jump);
        if (t >= jump2At && t < jump2At + jump2Hold) f.held |= keyBit(Action::Jump);
        if (action >= 0 && t >= actionAt && t < actionAt + actionHold) f.held |= 1u << action;
        if (t == 0) f.choice = choice;
        return f;
    }
};

}  // namespace

struct GoExplore::Archive {
    struct Node {
        int parent = -1;  // -1: from the load
        std::vector<InputFrame> frames;
        std::unique_ptr<ArenaWorld::State> snap;
        uint64_t used = 0;
        bool live = true;
    };
    struct Cell {
        int node = -1;
        float clock = 0.0f;
        float potential = 0.0f;
        int waypoint = 0;
        int chosen = 0, seen = 0;
        int64_t improved = -1000;  // the visit that last improved it
        float lead = 0.0f;         // ahead of the best run at the same progress (s)
        bool anchor = false;       // a state of the best run
        float hint = 1.0f;         // the route hints' favour (1: none near)
    };
    std::vector<Node> nodes;
    std::vector<Cell> cells;
    std::unordered_map<uint64_t, int> index;
    size_t snaps = 0;
    uint64_t tick = 0;
    int64_t visits = 0;
    uint64_t bestSeen = 0;  // the best run whose states are anchors (its hash)
    uint64_t hintsSeen = 0;     // the route hints in use (their hash), and the visit they came
    int64_t hintsSince = 0;
    RunStudy g;
    float bestClock = 0.0f;
    std::vector<InputFrame> best;

    uint32_t heldAfter(int n) const {
        for (int k = n; k >= 0; k = nodes[static_cast<size_t>(k)].parent)
            if (!nodes[static_cast<size_t>(k)].frames.empty()) return nodes[static_cast<size_t>(k)].frames.back().held;
        return 0;
    }
    std::vector<InputFrame> path(int n) const {
        std::vector<int> chain;
        for (int k = n; k >= 0; k = nodes[static_cast<size_t>(k)].parent) chain.push_back(k);
        std::vector<InputFrame> out;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto& f = nodes[static_cast<size_t>(*it)].frames;
            out.insert(out.end(), f.begin(), f.end());
        }
        return out;
    }
    void evictOne() {
        size_t victim = nodes.size();
        uint64_t oldest = ~0ull;
        for (size_t k = 0; k < nodes.size(); ++k)
            if (nodes[k].snap && nodes[k].used < oldest) oldest = nodes[k].used, victim = k;
        if (victim < nodes.size()) {
            nodes[victim].snap.reset();
            --snaps;
        }
    }
    void keepState(Sim& sim, int n, size_t maxStates) {
        Node& node = nodes[static_cast<size_t>(n)];
        node.used = ++tick;
        if (node.snap) return;
        while (snaps >= maxStates && snaps > 0) evictOne();
        node.snap = std::make_unique<ArenaWorld::State>();
        sim.arena().save(*node.snap);
        ++snaps;
    }
    /// The world at the end of node `n`: its savestate, or its nearest ancestor's and the frames after.
    void goTo(Sim& sim, int n, size_t maxStates) {
        std::vector<int> chain;
        int k = n;
        while (k >= 0 && !nodes[static_cast<size_t>(k)].snap) {
            chain.push_back(k);
            k = nodes[static_cast<size_t>(k)].parent;
        }
        if (k >= 0) {
            nodes[static_cast<size_t>(k)].used = ++tick;
            sim.arena().restore(*nodes[static_cast<size_t>(k)].snap);
        } else {
            sim.arena().restore(sim.origin());
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            for (const InputFrame& f : nodes[static_cast<size_t>(*it)].frames) sim.arena().step(f);
        if (!chain.empty()) keepState(sim, n, maxStates);
    }
    int addNode(int parent, std::vector<InputFrame> frames) {
        Node nd;
        nd.parent = parent;
        nd.frames = std::move(frames);
        nodes.push_back(std::move(nd));
        return static_cast<int>(nodes.size()) - 1;
    }
    /// Bounded memory: the least promising cells dropped, then the nodes no cell needs.
    void trim(size_t maxCells) {
        if (cells.size() <= maxCells) return;
        std::vector<int> order(cells.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        auto worth = [&](const Cell& c) { return (c.anchor ? 1.0e6f : 0.0f) + 3.0f * c.lead - 0.05f * static_cast<float>(c.chosen) + 0.0001f * c.potential; };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return worth(cells[static_cast<size_t>(a)]) > worth(cells[static_cast<size_t>(b)]); });
        const size_t keepN = maxCells * 4 / 5;
        std::vector<char> keepCell(cells.size(), 0);
        for (size_t i = 0; i < keepN && i < order.size(); ++i) keepCell[static_cast<size_t>(order[i])] = 1;
        std::vector<Cell> kept;
        std::unordered_map<uint64_t, int> idx;
        for (const auto& [key, ci] : index)
            if (keepCell[static_cast<size_t>(ci)]) {
                idx[key] = static_cast<int>(kept.size());
                kept.push_back(cells[static_cast<size_t>(ci)]);
            }
        cells = std::move(kept);
        index = std::move(idx);
        // The nodes still needed (the cells' ways), renumbered.
        std::vector<char> need(nodes.size(), 0);
        for (const Cell& c : cells)
            for (int k = c.node; k >= 0 && !need[static_cast<size_t>(k)]; k = nodes[static_cast<size_t>(k)].parent) need[static_cast<size_t>(k)] = 1;
        std::vector<int> remap(nodes.size(), -1);
        std::vector<Node> kn;
        for (size_t k = 0; k < nodes.size(); ++k)
            if (need[k]) {
                remap[k] = static_cast<int>(kn.size());
                kn.push_back(std::move(nodes[k]));
            }
        snaps = 0;
        for (Node& nd : kn) {
            nd.parent = nd.parent >= 0 ? remap[static_cast<size_t>(nd.parent)] : -1;
            if (nd.snap) ++snaps;
        }
        nodes = std::move(kn);
        for (Cell& c : cells) c.node = remap[static_cast<size_t>(c.node)];
    }
};

GoExplore::GoExplore() : GoExplore(Options{}) {}
GoExplore::GoExplore(const Options& o) : opt_(o) {}
GoExplore::~GoExplore() = default;

size_t GoExplore::cells() const {
    size_t n = 0;
    for (const auto& a : archives_) n += a->cells.size();
    return n;
}

ExploreStats GoExplore::run(const std::vector<Sim*>& sims, const Slice& slice) {
    ExploreStats stats;
    if (sims.empty() || slice.best.empty()) return stats;
    const int threads = std::max(1, std::min(slice.threads, static_cast<int>(sims.size())));
    while (archives_.size() < static_cast<size_t>(threads)) archives_.push_back(std::make_unique<Archive>());
    std::mutex m;
    std::vector<InputFrame> shared = slice.best;
    float sharedClock = slice.bestClock;
    std::atomic<uint64_t> version{0};
    const Options o = opt_;
    // The route hints in buckets of hintRadius px (the nearest one within the radius).
    const float hr = std::max(8.0f, hintRadius);
    std::unordered_map<uint64_t, std::vector<size_t>> hintGrid;
    auto bucket = [hr](float x, float y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(std::floor(x / hr)))) << 32) |
               static_cast<uint32_t>(static_cast<int32_t>(std::floor(y / hr)));
    };
    for (size_t i = 0; i < routeHints.size(); ++i) hintGrid[bucket(routeHints[i].feet.x, routeHints[i].feet.y)].push_back(i);
    // Near the hinted route: 1.5x, up to 3.5x when there as early as the hint (or earlier).
    auto hintFavour = [&](Vec2 feet, float clock) {
        if (hintGrid.empty()) return 1.0f;
        float best = hr * hr, lead = 0.0f;
        bool near = false;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = hintGrid.find(bucket(feet.x + static_cast<float>(dx) * hr, feet.y + static_cast<float>(dy) * hr));
                if (it == hintGrid.end()) continue;
                for (size_t i : it->second) {
                    const float d2 = (routeHints[i].feet - feet).lengthSq();
                    if (d2 <= best) best = d2, lead = routeHints[i].clock - clock, near = true;
                }
            }
        return near ? 1.5f + 2.0f * std::clamp(lead + 0.25f, 0.0f, 1.0f) : 1.0f;
    };

    auto work = [&](int wi) {
        Sim& sim = *sims[static_cast<size_t>(wi)];
        Archive& A = *archives_[static_cast<size_t>(wi)];
        Random rng(slice.seed * 2654435761u + static_cast<uint64_t>(wi) * 40503u + 7u + static_cast<uint64_t>(A.visits));
        ExploreStats mine;
        const Course& course = sim.course();
        const auto& sectionEnds = course.sectionEnds();
        const int nSections = static_cast<int>(sectionEnds.size());
        auto sectionOf = [&](int wp) {
            int s = 0;
            for (int e : sectionEnds)
                if (wp > e) ++s;
            return std::min(s, std::max(0, nSections - 1));
        };
        uint64_t myVersion = ~0ull;
        double hintFade = 1.0;
        const auto began = std::chrono::steady_clock::now();
        double rejoinSeconds = 0.0;
        int firstS = 0, lastS = nSections - 1;
        size_t windowEnd = 0;
        // The best run now: studied, its states along the window added as anchors, the cells' leads.
        auto refresh = [&] {
            uint64_t v = version.load();
            if (v == myVersion) return true;
            myVersion = v;
            {
                std::lock_guard<std::mutex> l(m);
                A.best = shared;
                A.bestClock = sharedClock;
            }
            A.g = sim.study(A.best);
            if (!A.g.finishes()) return false;
            firstS = std::clamp(slice.firstSection, 0, std::max(0, nSections - 1));
            // One section past the window too: an earlier arrival that cannot re-join at once (it
            // left something the best run's rest needs) may still find its own way through the next.
            lastS = slice.lastSection < 0 ? nSections - 1 : std::min(nSections - 1, std::clamp(slice.lastSection, firstS, nSections - 1) + 1);
            windowEnd = static_cast<size_t>(lastS) < A.g.ends.size() ? A.g.ends[static_cast<size_t>(lastS)] : A.g.exitAt;
            uint64_t h = 0x51ed27ull;
            for (const InputFrame& f : A.best) h = mixKey(h, static_cast<int64_t>(f.held) * 31 + f.flags * 7 + f.choice);
            if (h != A.bestSeen) {
                A.bestSeen = h;
                const size_t from = std::min(slice.keep, windowEnd);
                int node = A.addNode(-1, std::vector<InputFrame>(A.best.begin(), A.best.begin() + static_cast<std::ptrdiff_t>(from)));
                sim.arena().restore(sim.origin());
                for (size_t i = 0; i < from; ++i) sim.arena().step(A.best[i]);
                int wp = 0;
                for (size_t i = from;;) {
                    // The state after i frames of the best run: an anchor cell.
                    const Progress p = sim.measure(wp);
                    wp = p.waypoint;
                    A.keepState(sim, node, o.maxStates);
                    const Player& pl = sim.world().player();
                    uint64_t key = 0xa11ull;
                    key = mixKey(key, static_cast<int64_t>(h & 0xffffffu));
                    key = mixKey(key, static_cast<int64_t>(std::floor(pl.feet().x / o.cellPx)));
                    key = mixKey(key, static_cast<int64_t>(std::floor(pl.feet().y / o.cellPx)));
                    key = mixKey(key, static_cast<int64_t>(i));
                    Archive::Cell c;
                    c.node = node;
                    c.clock = p.clock;
                    c.potential = p.potential;
                    c.waypoint = p.waypoint;
                    c.anchor = true;
                    A.index[key] = static_cast<int>(A.cells.size());
                    A.cells.push_back(c);
                    if (i >= windowEnd) break;
                    const size_t to = std::min(windowEnd, i + 60);
                    for (size_t k = i; k < to; ++k) sim.arena().step(A.best[k]);
                    node = A.addNode(node, std::vector<InputFrame>(A.best.begin() + static_cast<std::ptrdiff_t>(i), A.best.begin() + static_cast<std::ptrdiff_t>(to)));
                    i = to;
                }
            }
            // Leads against this best run: its clock when it got as far.
            for (Archive::Cell& c : A.cells) {
                const size_t f = A.g.frameAtPotential(c.potential);
                c.lead = (f < A.g.clockAt.size() ? A.g.clockAt[f] : A.bestClock) - c.clock;
            }
            return true;
        };
        auto cellKey = [&](const Progress& p) {
            const Player& pl = sim.world().player();
            uint64_t h = 0xce11ull;
            h = mixKey(h, static_cast<int64_t>(std::floor(pl.feet().x / o.cellPx)));
            h = mixKey(h, static_cast<int64_t>(std::floor(pl.feet().y / o.cellPx)));
            h = mixKey(h, std::clamp(static_cast<int>(std::lround(pl.vel.x / 50.0f)), -9, 9));
            h = mixKey(h, pl.vel.y < -60.0f ? -1 : (pl.vel.y > 60.0f ? 1 : 0));
            h = mixKey(h, pl.grounded() ? 1 : 0);
            h = mixKey(h, pl.airJumpsLeft());
            h = mixKey(h, pl.dashing() ? 1 : 0);
            h = mixKey(h, static_cast<int64_t>(pl.energy / 25.0f));
            h = mixKey(h, p.health);
            h = mixKey(h, p.waypoint);
            h = mixKey(h, p.milestones);
            h = mixKey(h, static_cast<int64_t>(p.powerSet));
            h = mixKey(h, p.offer ? 1 : 0);
            h = mixKey(h, p.enemiesLeft);
            int ready = 0, k = 0;
            for (const auto& pw : pl.powers()) ready |= (pw->cooldownRemaining() <= 0.0f ? 1 : 0) << (k++ & 15);
            h = mixKey(h, ready);
            return h;
        };
        auto randomMove = [&](uint32_t held, bool offer) {
            Move mv;
            mv.len = static_cast<uint8_t>(rng.rangeInt(6, 24));
            const uint8_t j0 = (held & keyBit(Action::Jump)) ? 1 : 0;
            if (offer) {
                const int slots = std::max(1, sim.world().player().powerSlots());
                const int c = rng.rangeInt(0, slots);
                mv.choice = c == 0 ? InputFrame::kChoiceLeave : static_cast<uint8_t>(c);
            }
            const float d = rng.unit();
            mv.dir = d < 0.55f ? 1 : (d < 0.85f ? -1 : 0);
            if (rng.chance(0.6f)) {
                mv.jumpAt = static_cast<uint8_t>(j0 + (rng.chance(0.35f) ? rng.rangeInt(0, mv.len / 2) : 0));
                mv.jumpHold = static_cast<uint8_t>(rng.chance(0.4f) ? mv.len : rng.rangeInt(1, mv.len));
                if (rng.chance(0.35f)) {
                    const int at = mv.jumpAt + mv.jumpHold + 1 + rng.rangeInt(0, 8);
                    if (at < mv.len) mv.jump2At = static_cast<uint8_t>(at), mv.jump2Hold = static_cast<uint8_t>(rng.rangeInt(1, std::max(2, mv.len - at)));
                }
            }
            if (rng.chance(0.45f)) {
                std::vector<int8_t> acts = {static_cast<int8_t>(Action::Attack), static_cast<int8_t>(Action::Interact)};
                for (const auto& pw : sim.world().player().powers())
                    if (pw->trigger() != PowerTrigger::Passive) acts.push_back(static_cast<int8_t>(pw->key()));
                mv.action = acts[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(acts.size()) - 1))];
                const bool down = (held & (1u << mv.action)) != 0;
                mv.actionAt = static_cast<uint8_t>((down ? 1 : 0) + (rng.chance(0.5f) ? 0 : rng.rangeInt(0, mv.len - 2)));
                mv.actionHold = static_cast<uint8_t>(rng.chance(0.6f) ? 1 : rng.rangeInt(2, mv.len));
            }
            if (rng.chance(0.15f)) {
                mv.vert = rng.chance(0.6f) ? 1 : -1;
                mv.vertSteps = static_cast<uint8_t>(rng.chance(0.5f) ? 255 : rng.rangeInt(1, mv.len));
            }
            return mv;
        };
        // The cell to explore from: weighted by its promise.
        auto choose = [&]() -> int {
            double total = 0.0;
            std::vector<double> w(A.cells.size(), 0.0);
            for (size_t i = 0; i < A.cells.size(); ++i) {
                const Archive::Cell& c = A.cells[i];
                const int s = sectionOf(c.waypoint);
                if (s < firstS || s > lastS) continue;
                const double lead = std::clamp(static_cast<double>(c.lead), -1.5, 1.0);
                double x = std::exp(lead / 0.35) / std::sqrt(1.0 + c.chosen);
                if (A.visits - c.improved < 64) x *= 2.0;
                if (c.anchor) x *= 0.6;
                // The route hints only seed the first exploration: their favour fades (halved every
                // kHintHalfLife visits), novelty and promise stay what chooses.
                x *= 1.0 + (static_cast<double>(c.hint) - 1.0) * hintFade;
                w[i] = x;
                total += x;
            }
            if (total <= 0.0) return -1;
            double r = rng.unit() * total;
            for (size_t i = 0; i < w.size(); ++i) {
                r -= w[i];
                if (r <= 0.0 && w[i] > 0.0) return static_cast<int>(i);
            }
            for (size_t i = w.size(); i-- > 0;)
                if (w[i] > 0.0) return static_cast<int>(i);
            return -1;
        };

        while (!slice.over()) {
            if (!refresh()) return;
            {
                uint64_t hh = routeHints.size();
                if (!routeHints.empty()) hh = mixKey(mixKey(hh, static_cast<int64_t>(routeHints.front().feet.x)), static_cast<int64_t>(routeHints.back().clock * 1000.0f));
                if (hh != A.hintsSeen) A.hintsSeen = hh, A.hintsSince = A.visits;
                hintFade = std::exp2(-static_cast<double>(A.visits - A.hintsSince) / kHintHalfLife);
            }
            const int ci = choose();
            if (ci < 0) return;
            ++A.visits;
            ++mine.visits;
            int parentNode = A.cells[static_cast<size_t>(ci)].node;
            ++A.cells[static_cast<size_t>(ci)].chosen;
            int wp = A.cells[static_cast<size_t>(ci)].waypoint;
            A.goTo(sim, parentNode, o.maxStates);
            const int deaths = sim.world().stats().deaths;
            uint32_t held = A.heldAfter(parentNode);
            int cinematic = 0;
            std::vector<InputFrame> pending;
            // A move offered from elsewhere first, sometimes.
            std::vector<InputFrame> offered;
            if (proposals && rng.chance(0.3f)) {
                auto ps = proposals(sim.world(), sectionEnds[static_cast<size_t>(std::min(sectionOf(wp), nSections - 1))]);
                if (!ps.empty()) offered = ps[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(ps.size()) - 1))];
            }
            for (int mvI = 0; mvI < o.movesPerVisit && !slice.over(); ++mvI) {
                const int startSection = sectionOf(wp);
                const bool offer = sim.world().powerOffer().has_value();
                const Move mv = randomMove(held, offer);
                const size_t len = mvI == 0 && !offered.empty() ? offered.size() : mv.len;
                bool dead = false, exitReached = false;
                for (size_t t = 0; t < len; ++t) {
                    InputFrame f = mvI == 0 && !offered.empty() ? offered[t] : playerFrame(sim.world(), mv.want(static_cast<int>(t)), cinematic);
                    f.pressed = f.held & ~held;
                    f.released = held & ~f.held;
                    held = f.held;
                    sim.arena().step(f);
                    pending.push_back(f);
                    if (sim.broken(deaths)) {
                        dead = true;
                        break;
                    }
                    if (sim.atExit()) {
                        exitReached = true;
                        break;
                    }
                    if (sim.world().powerOffer()) break;
                }
                ++mine.moves;
                if (slice.count) slice.count(1);
                if (dead) break;
                const Progress p = sim.measure(wp);
                wp = p.waypoint;
                if (p.rejected) break;
                const int s = sectionOf(wp);
                // A section's end reached earlier than the best run: the rest of the best run from there.
                const bool crossed = p.finished || s > startSection;
                if (crossed) {
                    const size_t endS = static_cast<size_t>(startSection);
                    const float split = A.g.splitAt(endS);
                    // The re-joins (played to the exit) take at most a third of the thread's time.
                    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
                    if (p.finished || (split >= 0.0f && p.clock + 1.0e-4f < split && rejoinSeconds < 0.35 * spent + 2.0)) {
                        ++mine.ahead;
                        const auto r0 = std::chrono::steady_clock::now();
                        const size_t center = endS < A.g.ends.size() ? A.g.ends[endS] : A.g.exitAt;
                        const JoinTry j = exitReached ? JoinTry{true, p.clock, A.g.frames.size(), 0, 0} : sim.rejoin(A.g, center, held, A.bestClock - 1.0e-4f);
                        if (slice.count) slice.count(j.tried);
                        rejoinSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - r0).count();
                        if (j.ok && j.finish + 1.0e-6f < A.bestClock) {
                            ++mine.rejoined;
                            std::vector<InputFrame> whole = A.path(parentNode);
                            whole.insert(whole.end(), pending.begin(), pending.end());
                            if (j.played > 0) {
                                const size_t at = whole.size();
                                for (size_t i = j.seedAt; i < j.seedAt + j.played && i < A.g.frames.size(); ++i) whole.push_back(A.g.frames[i]);
                                if (whole.size() > at) {
                                    whole[at].pressed = whole[at].held & ~held;
                                    whole[at].released = held & ~whole[at].held;
                                }
                            }
                            size_t played = 0;
                            const float c = sim.playToExit(sim.origin(), whole, 0, A.bestClock, &played);
                            if (c >= 0.0f && c + 1.0e-6f < A.bestClock) {
                                whole.resize(played);
                                std::lock_guard<std::mutex> l(m);
                                if (c + 1.0e-6f < sharedClock) {
                                    char line[200];
                                    std::snprintf(line, sizeof line, "  exploration : %.3f s -> %.3f s (section %zu finie %.3f s plus tôt)", static_cast<double>(sharedClock),
                                                  static_cast<double>(c), endS + 1, static_cast<double>(split - p.clock));
                                    if (slice.log) slice.log(line);
                                    shared = whole;
                                    sharedClock = c;
                                    version.fetch_add(1);
                                    ++mine.gains;
                                    if (slice.offer) slice.offer(whole, c, "exploration");
                                }
                            }
                        }
                    }
                    if (p.finished || s > lastS) break;  // past the horizon: the run re-joined (or not)
                }
                if (exitReached) break;
                // Branch-and-bound: a state that cannot reach its section's end before the best run's split.
                if (slice.pruner && slice.pruner->prune(sim.world(), static_cast<size_t>(s), sectionEnds[static_cast<size_t>(s)], p.clock,
                                                         slice.pruner->startSplit.size() > static_cast<size_t>(s) ? slice.pruner->startSplit[static_cast<size_t>(s)] : 0.0f)) {
                    ++mine.pruned;
                    break;
                }
                const uint64_t key = cellKey(p);
                auto it = A.index.find(key);
                const bool fresh = it == A.index.end();
                if (fresh || p.clock + 1.0e-4f < A.cells[static_cast<size_t>(it->second)].clock) {
                    const int node = A.addNode(parentNode, std::move(pending));
                    pending.clear();
                    parentNode = node;
                    A.keepState(sim, node, o.maxStates);
                    Archive::Cell c;
                    if (!fresh) c = A.cells[static_cast<size_t>(it->second)];
                    c.node = node;
                    c.clock = p.clock;
                    c.potential = p.potential;
                    c.waypoint = p.waypoint;
                    c.anchor = false;
                    c.improved = A.visits;
                    const size_t f = A.g.frameAtPotential(p.potential);
                    c.lead = (f < A.g.clockAt.size() ? A.g.clockAt[f] : A.bestClock) - p.clock;
                    c.hint = hintFavour(sim.world().player().feet(), p.clock);
                    if (fresh) {
                        A.index[key] = static_cast<int>(A.cells.size());
                        A.cells.push_back(c);
                        ++mine.cellsNew;
                    } else {
                        A.cells[static_cast<size_t>(it->second)] = c;
                        ++mine.cellsFaster;
                    }
                } else {
                    ++A.cells[static_cast<size_t>(it->second)].seen;
                }
            }
            if (A.cells.size() > o.maxCells) A.trim(o.maxCells);
        }
        std::lock_guard<std::mutex> l(m);
        stats.visits += mine.visits;
        stats.moves += mine.moves;
        stats.cellsNew += mine.cellsNew;
        stats.cellsFaster += mine.cellsFaster;
        stats.pruned += mine.pruned;
        stats.ahead += mine.ahead;
        stats.rejoined += mine.rejoined;
        stats.gains += mine.gains;
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back(work, i);
    for (auto& t : pool) t.join();
    total_.visits += stats.visits;
    total_.moves += stats.moves;
    total_.cellsNew += stats.cellsNew;
    total_.cellsFaster += stats.cellsFaster;
    total_.pruned += stats.pruned;
    total_.ahead += stats.ahead;
    total_.rejoined += stats.rejoined;
    total_.gains += stats.gains;
    if (slice.log) {
        char line[240];
        std::snprintf(line, sizeof line, "  exploration : %zu cellules, %lld visites, %lld nouvelles, %lld plus tôt, %lld coupées, %d fins de section en avance, %d raccords plus rapides",
                      cells(), static_cast<long long>(stats.visits), static_cast<long long>(stats.cellsNew), static_cast<long long>(stats.cellsFaster),
                      static_cast<long long>(stats.pruned), stats.ahead, stats.rejoined);
        slice.log(line);
    }
    return stats;
}

}  // namespace cosmic::tas
