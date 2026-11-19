#include "game/raid/RaidNav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <map>
#include <mutex>
#include <queue>
#include <utility>

#include "engine/physics/PhysicsWorld.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic::raid {

namespace {

constexpr float kHalfW = 5.0f;    // data/player.json: 10 × 22
constexpr float kHeight = 22.0f;
constexpr float kBucket = 64.0f;
// What never blocks a body on the graph: the moving platforms (they move), the players' bodies, the
// crumbling platforms (they come back), the enemies' own walls.
constexpr uint32_t kIgnore = ColliderTag::Moving | ColliderTag::Teammate | ColliderTag::Crumble | ColliderTag::EnemyOnly;
// What opens or breaks during a zone: the graph is built again when one of them changes.
constexpr uint32_t kChanging = ColliderTag::Door | ColliderTag::Breakable | ColliderTag::Gate | ColliderTag::ShieldPass;

uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

Rect bodyAt(Vec2 feet) { return {feet.x - kHalfW, feet.y - kHeight, kHalfW * 2.0f, kHeight - 0.5f}; }

bool blocking(const Collider& c) {
    return c.enabled && !c.dynamic && c.kind == ColliderKind::Solid && (c.tags & kIgnore) == 0 && (c.tags & ColliderTag::Teammate) == 0;
}

struct Builder {
    const GameWorld& w;
    const PhysicsWorld& ph;
    std::vector<Rect> hazards;
    // The walls that block a body (static solids only: the graph is a pure function of the level and
    // its doors), in columns of 64 px (no allocation per query: a graph checks many thousands of bodies).
    std::vector<Rect> solids;
    std::vector<std::vector<int>> cols;
    float x0 = 0.0f;

    void index() {
        for (const Collider& c : ph.colliders())
            if (blocking(c)) solids.push_back(c.box);
        const Rect b = w.level().bounds;
        x0 = b.x - 256.0f;
        cols.assign(static_cast<size_t>(std::max(1.0f, (b.w + 512.0f) / 64.0f) + 1), {});
        for (size_t i = 0; i < solids.size(); ++i) {
            const int c0 = std::clamp(static_cast<int>((solids[i].x - x0) / 64.0f), 0, static_cast<int>(cols.size()) - 1);
            const int c1 = std::clamp(static_cast<int>((solids[i].right() - x0) / 64.0f), 0, static_cast<int>(cols.size()) - 1);
            for (int c = c0; c <= c1; ++c) cols[static_cast<size_t>(c)].push_back(static_cast<int>(i));
        }
    }
    bool blocked(const Rect& box) const {
        const int c0 = std::clamp(static_cast<int>((box.x - x0) / 64.0f), 0, static_cast<int>(cols.size()) - 1);
        const int c1 = std::clamp(static_cast<int>((box.right() - x0) / 64.0f), 0, static_cast<int>(cols.size()) - 1);
        for (int c = c0; c <= c1; ++c)
            for (int i : cols[static_cast<size_t>(c)])
                if (solids[static_cast<size_t>(i)].intersects(box)) return true;
        return false;
    }
    bool hazard(const Rect& box) const {
        for (const Rect& h : hazards)
            if (h.intersects(box)) return true;
        return false;
    }
    /// A body can be at `feet` (no wall, no lasting danger).
    bool free(Vec2 feet) const {
        const Rect b = bodyAt(feet);
        return !blocked(b) && !hazard(b);
    }
    /// Something to stand on right under `feet` (a floor, a one-way platform), within 3 px.
    bool supported(Vec2 feet) const {
        const Rect probe{feet.x - kHalfW + 1.0f, feet.y - 1.0f, kHalfW * 2.0f - 2.0f, 4.0f};
        bool any = false;
        ph.forEachIn(probe, [&](const Collider& c) {
            if (any || c.dynamic || (c.tags & (ColliderTag::Teammate | ColliderTag::EnemyOnly))) return;
            if (!c.enabled && !(c.tags & ColliderTag::Crumble)) return;
            if (c.box.intersects(probe) && c.box.y >= feet.y - 1.0f && c.box.y <= feet.y + 3.0f) any = true;
        });
        return any;
    }
    /// A body can follow the quadratic curve a → b over control point c (12 bodies along it).
    bool curveFree(Vec2 a, Vec2 c, Vec2 b) const {
        for (int k = 1; k < 12; ++k) {
            const float t = static_cast<float>(k) / 12.0f, u = 1.0f - t;
            const Vec2 p{u * u * a.x + 2.0f * u * t * c.x + t * t * b.x, u * u * a.y + 2.0f * u * t * c.y + t * t * b.y};
            if (!free(p)) return false;
        }
        return true;
    }
    bool lineFree(Vec2 a, Vec2 b) const {
        const float len = std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y));
        const int n = std::max(1, static_cast<int>(len / 8.0f));
        for (int k = 1; k <= n; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(n);
            if (!free({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})) return false;
        }
        return true;
    }
    /// Whether a jump (or a drop) from a to b is possible: within reach, nothing on its way.
    bool jump(Vec2 a, Vec2 b) const {
        const float dx = b.x - a.x, up = a.y - b.y, adx = std::fabs(dx);
        if (up > NavGraph::kMaxUp) return false;
        const float reach = up > 0.0f ? 140.0f - up * 0.6f : 150.0f + std::min(120.0f, -up * 0.4f);
        if (adx > reach) return false;
        const float top = std::min(a.y, b.y) - (up > 0.0f ? 18.0f : 14.0f);
        // A curve over both ends (its middle at `top`).
        const Vec2 c{(a.x + b.x) * 0.5f, 2.0f * top - 0.5f * (a.y + b.y)};
        if (curveFree(a, c, b)) return true;
        if (up > 0.0f && adx < 48.0f) {
            // Straight up, then across (a platform just above).
            const Vec2 m{a.x, b.y - 4.0f};
            return lineFree(a, m) && lineFree(m, b);
        }
        if (up <= 0.0f) {
            // Off the ledge, then down.
            const float side = dx >= 0.0f ? 1.0f : -1.0f;
            const Vec2 m{a.x + side * std::min(adx, 14.0f), a.y};
            return lineFree(a, m) && lineFree(m, b);
        }
        return false;
    }
};

struct CacheEntry {
    uint64_t key = 0;
    std::shared_ptr<const NavGraph> graph;
};
std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}
std::list<CacheEntry>& cache() {
    static std::list<CacheEntry> c;
    return c;
}

}  // namespace

uint64_t NavGraph::signature(const GameWorld& w, const std::string& periodic) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : w.level().id) h = mix(h, c);
    for (unsigned char c : periodic) h = mix(h, c);
    h = mix(h, static_cast<uint64_t>(w.level().objects.size()));
    uint64_t statics = 0;
    for (const Collider& c : w.physics().colliders()) {
        if (c.dynamic || (c.tags & ColliderTag::Teammate)) continue;
        ++statics;
        // Every wall that can block a body, open or closed (a door, a barrier switched off by a link of the
        // titan…): the graph is then a pure function of its signature.
        if ((c.tags & kChanging) || (c.kind == ColliderKind::Solid && !(c.tags & kIgnore) && !c.enabled))
            h = mix(h, (static_cast<uint64_t>(c.id) << 1) | (c.enabled ? 1u : 0u));
    }
    return mix(h, statics);
}

std::shared_ptr<const NavGraph> NavGraph::of(const GameWorld& w, const std::string& periodic) {
    const uint64_t key = signature(w, periodic);
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto& c = cache();
        for (auto it = c.begin(); it != c.end(); ++it)
            if (it->key == key) {
                c.splice(c.begin(), c, it);  // most recent first
                return c.front().graph;
            }
    }
    auto g = std::make_shared<NavGraph>();
    g->build(w, periodic);
    std::lock_guard<std::mutex> lock(cacheMutex());
    auto& c = cache();
    c.push_front({key, g});
    while (c.size() > 48) c.pop_back();
    return g;
}

void NavGraph::addEdge(int a, int b, Move m, float cost) {
    if (a == b) return;
    for (Edge& e : edges_[static_cast<size_t>(a)])
        if (e.to == b) {
            if (cost < e.cost) {
                e.cost = cost;
                e.move = m;
            }
            return;
        }
    edges_[static_cast<size_t>(a)].push_back({b, cost, m});
}

void NavGraph::build(const GameWorld& w, const std::string& periodic) {
    const auto t0 = std::chrono::steady_clock::now();
    nodes_.clear();
    edges_.clear();
    movers_.clear();
    buckets_.clear();
    Builder B{w, w.physics(), {}, {}, {}, 0.0f};
    B.index();
    // The lasting dangers: spikes, a liquid one cannot swim in, a damage zone that is not the zone's
    // periodic liquid (the rising magma comes and goes: the players flee it when it is announced).
    for (const auto& e : w.entities()) {
        if (e->removed) continue;
        if (auto* s = dynamic_cast<const SpikesEntity*>(e.get())) B.hazards.push_back(s->overlapBox().expanded(2.0f));
        else if (auto* l = dynamic_cast<const LiquidEntity*>(e.get()); l && !l->swimmable()) B.hazards.push_back(l->overlapBox().expanded(1.0f));
        else if (auto* d = dynamic_cast<const DamageZoneEntity*>(e.get()); d && e->id() != periodic) B.hazards.push_back(e->box.expanded(2.0f));
    }
    const float killY = w.level().effectiveKillY();
    const Rect bounds = w.level().bounds;
    std::map<std::pair<int, int>, int> seen;  // (x / 8, y) → node: two tiles side by side give one place
    auto addNode = [&](Vec2 feet, uint32_t surface, int mover, bool ladder) -> int {
        const std::pair<int, int> key{static_cast<int>(std::lround(feet.x / 8.0f)), static_cast<int>(std::lround(feet.y))};
        if (mover < 0 && !ladder) {
            if (auto it = seen.find(key); it != seen.end()) return it->second;
        }
        Node n;
        n.feet = feet;
        n.surface = surface;
        n.mover = mover;
        n.ladder = ladder;
        nodes_.push_back(n);
        const int id = static_cast<int>(nodes_.size()) - 1;
        if (mover < 0 && !ladder) seen[key] = id;
        return id;
    };
    // 1. Every top of a floor or a one-way platform (sorted by collider id: the same order every time).
    for (const Collider& c : w.physics().colliders()) {
        if (c.dynamic || (c.tags & (ColliderTag::Teammate | ColliderTag::EnemyOnly | ColliderTag::Moving))) continue;
        if (!c.enabled && !(c.tags & ColliderTag::Crumble)) continue;
        const float top = c.box.y;
        if (top <= bounds.y + kHeight || top >= killY - 4.0f) continue;
        const float x0 = c.box.x + kHalfW + 1.0f, x1 = c.box.right() - kHalfW - 1.0f;
        if (x1 < x0 && c.box.w < 6.0f) continue;
        const int n = x1 <= x0 ? 0 : static_cast<int>(std::ceil((x1 - x0) / kStep));
        for (int k = 0; k <= n; ++k) {
            const float x = n == 0 ? c.box.center().x : x0 + (x1 - x0) * static_cast<float>(k) / static_cast<float>(n);
            const Vec2 feet{x, top};
            if (B.free(feet)) addNode(feet, c.id, -1, false);
        }
    }
    // 2. The ladders, every 16 px.
    uint32_t ladderSurface = 0x40000000u;
    std::vector<std::pair<int, int>> ladders;  // first, last node of each
    for (const auto& e : w.entities()) {
        auto* l = dynamic_cast<const LadderEntity*>(e.get());
        if (!l || e->removed) continue;
        const Rect r = e->box;
        const int first = static_cast<int>(nodes_.size());
        ++ladderSurface;
        for (float y = r.y + 6.0f; y <= r.bottom() + 0.5f; y += kStep) {
            const Vec2 feet{r.center().x, std::min(y, r.bottom())};
            if (B.free(feet)) addNode(feet, ladderSurface, -1, true);
        }
        const int last = static_cast<int>(nodes_.size()) - 1;
        if (last >= first) ladders.push_back({first, last});
    }
    // 3. The route of each moving platform (from the level: its box and its offset), every 32 px.
    for (const LevelObject& o : w.level().objects) {
        if ((o.type != "moving_platform" && o.type != "bubble") || o.prop<bool>("orbit", false)) continue;
        auto* mp = dynamic_cast<const MovingPlatformEntity*>(const_cast<GameWorld&>(w).entity(o.id));
        if (!mp) continue;
        Mover m;
        m.collider = mp->colliderId();
        m.a = o.rect.pos();
        m.b = m.a + Vec2{o.prop<float>("dx", 128.0f), o.prop<float>("dy", 0.0f)};
        m.w = o.rect.w;
        m.h = o.rect.h;
        movers_.push_back(m);
        const int mi = static_cast<int>(movers_.size()) - 1;
        const float len = std::max(1.0f, (m.b - m.a).length());
        const int steps = std::max(1, static_cast<int>(len / 32.0f));
        int prev = -1;
        for (int k = 0; k <= steps; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            const Vec2 feet{m.a.x + (m.b.x - m.a.x) * t + m.w * 0.5f, m.a.y + (m.b.y - m.a.y) * t};
            if (B.blocked(bodyAt(feet))) {
                prev = -1;
                continue;
            }
            const int id = addNode(feet, 0x20000000u + static_cast<uint32_t>(mi), mi, false);
            if (edges_.size() < nodes_.size()) edges_.resize(nodes_.size());
            if (prev >= 0) {
                const float d = (nodes_[static_cast<size_t>(prev)].feet - feet).length();
                addEdge(prev, id, Move::Ride, d * 1.5f + 4.0f);
                if (o.type != "bubble") addEdge(id, prev, Move::Ride, d * 1.5f + 4.0f);
            }
            prev = id;
        }
    }
    edges_.resize(nodes_.size());
    // The buckets (columns of 64 px), each sorted by node index.
    bucketX0_ = bounds.x - kBucket;
    const int columns = std::max(1, static_cast<int>((bounds.w + 2.0f * kBucket) / kBucket) + 1);
    buckets_.assign(static_cast<size_t>(columns), {});
    for (int i = 0; i < size(); ++i) {
        const int b = std::clamp(static_cast<int>((nodes_[static_cast<size_t>(i)].feet.x - bucketX0_) / kBucket), 0, columns - 1);
        buckets_[static_cast<size_t>(b)].push_back(i);
    }
    // 4. Walking: two places side by side on the same level, the floor between them.
    for (int i = 0; i < size(); ++i) {
        const Node& a = nodes_[static_cast<size_t>(i)];
        if (a.mover >= 0 || a.ladder) continue;
        const int bi = std::clamp(static_cast<int>((a.feet.x - bucketX0_) / kBucket), 0, columns - 1);
        for (int bb = std::max(0, bi - 1); bb <= std::min(columns - 1, bi + 1); ++bb)
            for (int j : buckets_[static_cast<size_t>(bb)]) {
                if (j <= i) continue;
                const Node& b = nodes_[static_cast<size_t>(j)];
                if (b.mover >= 0 || b.ladder || std::fabs(b.feet.y - a.feet.y) > 1.5f) continue;
                const float dx = std::fabs(b.feet.x - a.feet.x);
                if (dx > 20.0f || dx < 0.5f) continue;
                const Vec2 mid = (a.feet + b.feet) * 0.5f;
                if (!B.free(mid) || !B.supported(mid)) continue;
                addEdge(i, j, Move::Walk, dx);
                addEdge(j, i, Move::Walk, dx);
            }
    }
    // 5. Ladders: up and down their rungs, on and off at both ends.
    for (const auto& [first, last] : ladders) {
        for (int k = first; k < last; ++k) {
            const float d = std::fabs(nodes_[static_cast<size_t>(k + 1)].feet.y - nodes_[static_cast<size_t>(k)].feet.y);
            addEdge(k, k + 1, Move::Climb, d * 1.4f);
            addEdge(k + 1, k, Move::Climb, d * 1.4f);
        }
        for (int k : {first, last}) {
            const Node& l = nodes_[static_cast<size_t>(k)];
            const int bi = std::clamp(static_cast<int>((l.feet.x - bucketX0_) / kBucket), 0, columns - 1);
            for (int bb = std::max(0, bi - 1); bb <= std::min(columns - 1, bi + 1); ++bb)
                for (int j : buckets_[static_cast<size_t>(bb)]) {
                    const Node& n = nodes_[static_cast<size_t>(j)];
                    if (n.ladder || n.mover >= 0) continue;
                    if (std::fabs(n.feet.x - l.feet.x) > 30.0f || std::fabs(n.feet.y - l.feet.y) > 26.0f) continue;
                    if (!B.lineFree(l.feet, n.feet)) continue;
                    const float d = (n.feet - l.feet).length();
                    addEdge(k, j, Move::Climb, d * 1.2f + 2.0f);
                    addEdge(j, k, Move::Climb, d * 1.2f + 2.0f);
                }
        }
    }
    // 6. Jumps and drops: to each surface within reach, its two nearest places only (the graph stays
    // small), each checked on its way.
    struct Cand {
        uint32_t surface;
        float d;
        int j;
    };
    std::vector<Cand> cands;
    for (int i = 0; i < size(); ++i) {
        const Node& a = nodes_[static_cast<size_t>(i)];
        cands.clear();
        const int bi = std::clamp(static_cast<int>((a.feet.x - bucketX0_) / kBucket), 0, columns - 1);
        for (int bb = std::max(0, bi - 5); bb <= std::min(columns - 1, bi + 5); ++bb)
            for (int j : buckets_[static_cast<size_t>(bb)]) {
                if (j == i) continue;
                const Node& b = nodes_[static_cast<size_t>(j)];
                if (b.surface == a.surface && (a.mover >= 0 || std::fabs(b.feet.y - a.feet.y) < 1.5f)) continue;
                if (a.mover >= 0 && b.mover == a.mover) continue;
                const float up = a.feet.y - b.feet.y, dx = std::fabs(b.feet.x - a.feet.x);
                if (up > kMaxUp || up < -420.0f || dx > 280.0f) continue;
                if (std::fabs(up) < 1.5f && dx <= 20.0f) continue;  // walking does it
                cands.push_back({b.surface, dx + std::fabs(up) * 0.25f, j});
            }
        std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
            if (x.surface != y.surface) return x.surface < y.surface;
            if (x.d != y.d) return x.d < y.d;
            return x.j < y.j;
        });
        uint32_t cur = 0xFFFFFFFFu;
        int taken = 0;
        for (const Cand& c : cands) {
            if (c.surface != cur) {
                cur = c.surface;
                taken = 0;
            }
            if (taken >= 2) continue;
            const Node& b = nodes_[static_cast<size_t>(c.j)];
            if (!B.jump(a.feet, b.feet)) continue;
            ++taken;
            const float d = (b.feet - a.feet).length();
            const bool drop = b.feet.y > a.feet.y + 4.0f;
            addEdge(i, c.j, drop ? Move::Drop : Move::Jump, drop ? d * 1.1f + 6.0f : d * 1.3f + 12.0f);
        }
    }
    buildMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

size_t NavGraph::edgeCount() const {
    size_t n = 0;
    for (const auto& e : edges_) n += e.size();
    return n;
}

NavGraph::Move NavGraph::moveBetween(int a, int b) const {
    if (a < 0 || a >= size()) return Move::Walk;
    for (const Edge& e : edges_[static_cast<size_t>(a)])
        if (e.to == b) return e.move;
    return Move::Walk;
}

int NavGraph::nearest(Vec2 p, float reach, const std::function<bool(const Node&)>& ok) const {
    if (buckets_.empty()) return -1;
    const int columns = static_cast<int>(buckets_.size());
    const int b0 = std::clamp(static_cast<int>((p.x - reach - bucketX0_) / kBucket), 0, columns - 1);
    const int b1 = std::clamp(static_cast<int>((p.x + reach - bucketX0_) / kBucket), 0, columns - 1);
    int best = -1;
    float bestD = reach * reach;
    for (int bb = b0; bb <= b1; ++bb)
        for (int j : buckets_[static_cast<size_t>(bb)]) {
            const Node& n = nodes_[static_cast<size_t>(j)];
            const float d = (n.feet - p).lengthSq();
            if (d > bestD || (d == bestD && j > best)) continue;
            if (ok && !ok(n)) continue;
            best = j;
            bestD = d;
        }
    return best;
}

int NavGraph::locate(Vec2 feet, float reach) const {
    // The place it stands on (within a few pixels), else the nearest one, those under it preferred.
    const int on = nearest(feet, 14.0f, [&](const Node& n) { return n.mover < 0 && std::fabs(n.feet.y - feet.y) <= 6.0f; });
    if (on >= 0) return on;
    const int below = nearest(feet + Vec2{0.0f, 24.0f}, reach, [&](const Node& n) { return n.mover < 0 && n.feet.y >= feet.y - 8.0f; });
    if (below >= 0) return below;
    return nearest(feet, reach, [](const Node& n) { return n.mover < 0; });
}

bool NavGraph::path(int from, int to, std::vector<int>& out, int budget) const {
    out.clear();
    if (from < 0 || to < 0 || from >= size() || to >= size()) return false;
    if (from == to) {
        out.push_back(from);
        return true;
    }
    const size_t n = nodes_.size();
    std::vector<float> g(n, 1e30f);
    std::vector<int> parent(n, -1);
    std::vector<uint8_t> closed(n, 0);
    using Item = std::pair<float, int>;  // f, node: the lowest f first, then the lowest node
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const Vec2 goal = nodes_[static_cast<size_t>(to)].feet;
    g[static_cast<size_t>(from)] = 0.0f;
    open.push({(goal - nodes_[static_cast<size_t>(from)].feet).length(), from});
    int expanded = 0;
    while (!open.empty()) {
        const int u = open.top().second;
        open.pop();
        if (closed[static_cast<size_t>(u)]) continue;
        closed[static_cast<size_t>(u)] = 1;
        if (u == to) break;
        if (++expanded > budget) return false;
        for (const Edge& e : edges_[static_cast<size_t>(u)]) {
            const float ng = g[static_cast<size_t>(u)] + e.cost;
            if (ng >= g[static_cast<size_t>(e.to)]) continue;
            g[static_cast<size_t>(e.to)] = ng;
            parent[static_cast<size_t>(e.to)] = u;
            open.push({ng + (goal - nodes_[static_cast<size_t>(e.to)].feet).length(), e.to});
        }
    }
    if (parent[static_cast<size_t>(to)] < 0) return false;
    for (int v = to; v >= 0; v = parent[static_cast<size_t>(v)]) {
        out.push_back(v);
        if (v == from) break;
    }
    std::reverse(out.begin(), out.end());
    return !out.empty() && out.front() == from;
}

int NavGraph::search(int from, const std::function<bool(int)>& ok, float maxCost, std::vector<int>* out) const {
    if (out) out->clear();
    if (from < 0 || from >= size()) return -1;
    const size_t n = nodes_.size();
    std::vector<float> g(n, 1e30f);
    std::vector<int> parent(n, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[static_cast<size_t>(from)] = 0.0f;
    open.push({0.0f, from});
    int found = -1;
    while (!open.empty()) {
        const auto [d, u] = open.top();
        open.pop();
        if (d > g[static_cast<size_t>(u)]) continue;
        if (d > maxCost) break;
        if (ok(u)) {
            found = u;
            break;
        }
        for (const Edge& e : edges_[static_cast<size_t>(u)]) {
            const float ng = d + e.cost;
            if (ng >= g[static_cast<size_t>(e.to)]) continue;
            g[static_cast<size_t>(e.to)] = ng;
            parent[static_cast<size_t>(e.to)] = u;
            open.push({ng, e.to});
        }
    }
    if (found >= 0 && out) {
        for (int v = found; v >= 0; v = parent[static_cast<size_t>(v)]) {
            out->push_back(v);
            if (v == from) break;
        }
        std::reverse(out->begin(), out->end());
    }
    return found;
}

Vec2 NavGraph::moverFeet(const GameWorld& w, int m) const {
    if (m < 0 || m >= static_cast<int>(movers_.size())) return {};
    const Collider* c = w.physics().get(movers_[static_cast<size_t>(m)].collider);
    if (!c) return {};
    return {c->box.center().x, c->box.y};
}

uint64_t NavGraph::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        h = mix(h, static_cast<uint64_t>(std::lround(nodes_[i].feet.x * 4.0f)) ^ (static_cast<uint64_t>(std::lround(nodes_[i].feet.y * 4.0f)) << 32));
        for (const Edge& e : edges_[i]) h = mix(h, (static_cast<uint64_t>(e.to) << 8) | static_cast<uint64_t>(e.move));
    }
    return h;
}

}  // namespace cosmic::raid
