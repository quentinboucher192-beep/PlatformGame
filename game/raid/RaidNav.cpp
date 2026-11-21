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
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/PowerDefinition.h"

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
// The most a build simulates (a bound on its cost whatever the zone).
constexpr int kMaxSimulations = 60000;

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

}  // namespace

struct NavGraph::Snapshot {
    std::string level;
    PhysicsWorld physics;  // the moving platforms and the players' bodies off, the crumbling platforms on
    Rect bounds;
    float killY = 0.0f;
    std::vector<Rect> hazards;  // the lasting dangers (spikes, deadly liquids, damage zones), a little wider
    struct Env {
        Rect box;
        int kind = 0;  // 0 ladder, 1 water, 2 updraft
        float strength = 0.0f;
    };
    std::vector<Env> env;
    std::vector<Mover> movers;
    std::vector<Rect> keepOut;
    PlayerConfig cfg;
    nav::Kit kit;
    uint64_t key = 0;
};

namespace {

struct Builder {
    const NavGraph::Snapshot& s;
    const PhysicsWorld& ph;
    // The walls that block a body (static solids only), in columns of 64 px.
    std::vector<Rect> solids;
    std::vector<std::vector<int>> cols;
    float x0 = 0.0f;

    void index() {
        for (const Collider& c : ph.colliders())
            if (blocking(c)) solids.push_back(c.box);
        const Rect b = s.bounds;
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
        for (const Rect& h : s.hazards)
            if (h.intersects(box)) return true;
        return false;
    }
    bool free(Vec2 feet) const {
        const Rect b = bodyAt(feet);
        return !blocked(b) && !hazard(b);
    }
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
    bool lineFree(Vec2 a, Vec2 b) const {
        const float len = std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y));
        const int n = std::max(1, static_cast<int>(len / 8.0f));
        for (int k = 1; k <= n; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(n);
            if (!free({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})) return false;
        }
        return true;
    }
};

/// The build's arena: the snapshot's physics (one moving platform put where its place is), the body's
/// surroundings, the lasting dangers and the void.
class BuildArena : public nav::Arena {
public:
    BuildArena(PhysicsWorld& ph, const NavGraph::Snapshot& s) : ph_(ph), s_(s) {}
    const PhysicsWorld& physics() const override { return ph_; }
    void begin() override {}
    void end() override {}
    void advance(nav::Body& b, const nav::BodySim& /*sim*/, int /*k*/) override {
        b.onLadder = false;
        b.inWater = false;
        b.updraft = 0.0f;
        b.speedMultiplier = 1.0f;
        for (const auto& e : s_.env) {
            if (!e.box.intersects(b.box)) continue;
            if (e.kind == 0) {
                b.onLadder = true;
                b.ladderRect = e.box;
            } else if (e.kind == 1) {
                b.inWater = true;
            } else {
                b.updraft = std::max(b.updraft, e.strength);
            }
        }
        ++steps;
    }
    nav::Harm harm(const Rect& box, int /*k*/) const override {
        if (box.y > s_.killY || box.y > floorY) return nav::Harm::Void;
        for (size_t k = 0; k < s_.keepOut.size(); ++k)
            if (static_cast<int>(k) + 1 != allowed && s_.keepOut[k].intersects(box)) return nav::Harm::Void;  // another zone
        for (const Rect& h : s_.hazards)
            if (h.intersects(box)) return nav::Harm::Hazard;
        return nav::Harm::None;
    }
    float floorY = 1e9f;  // below this the move is lost (far under both ends)
    int allowed = 0;      // the place to keep out of it may enter (its target is in it), 1-based
    int64_t steps = 0;

private:
    PhysicsWorld& ph_;
    const NavGraph::Snapshot& s_;
};

struct CacheEntry {
    uint64_t key = 0;
    std::shared_future<std::shared_ptr<const NavGraph>> graph;
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

// ----------------------------------------------------------------------------- the snapshot

std::shared_ptr<NavGraph::Snapshot> NavGraph::snapshot(const GameWorld& w, const std::string& periodic, const std::vector<Rect>& keepOut) {
    auto s = std::make_shared<Snapshot>();
    s->level = w.level().id;
    s->physics = w.physics();
    // The moving platforms move (their routes are nodes of their own), the players' bodies too; a crumbling
    // platform always counts as a floor (it comes back).
    for (const Collider& c : w.physics().colliders()) {
        Collider* mine = s->physics.get(c.id);
        if (!mine) continue;
        if (c.tags & (ColliderTag::Moving | ColliderTag::Teammate)) mine->enabled = false;
        if (c.tags & ColliderTag::Crumble) mine->enabled = true;
    }
    s->bounds = w.level().bounds;
    s->killY = w.level().effectiveKillY();
    for (const auto& e : w.entities()) {
        if (e->removed) continue;
        if (auto* sp = dynamic_cast<const SpikesEntity*>(e.get())) s->hazards.push_back(sp->overlapBox().expanded(2.0f));
        else if (auto* l = dynamic_cast<const LiquidEntity*>(e.get()); l && !l->swimmable()) s->hazards.push_back(l->overlapBox().expanded(1.0f));
        else if (auto* d = dynamic_cast<const DamageZoneEntity*>(e.get()); d && e->id() != periodic && e->active) {
            // A damage zone that is on (a curtain of magma switched off is no danger; a pool, its body now).
            const Rect r = d->overlapBox();
            if (r.h > 0.5f) s->hazards.push_back(r.expanded(2.0f));
        }
        if (!e->active) continue;
        if (dynamic_cast<const LadderEntity*>(e.get())) s->env.push_back({e->overlapBox(), 0, 0.0f});
        else if (auto* lq = dynamic_cast<const LiquidEntity*>(e.get()); lq && lq->swimmable()) s->env.push_back({e->overlapBox(), 1, 0.0f});
        else if (auto* u = dynamic_cast<const UpdraftEntity*>(e.get()); u && u->blowing())
            s->env.push_back({e->overlapBox(), 2, e->object().prop<float>("strength", 230.0f)});
    }
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
        m.oneway = o.prop<bool>("oneway", false) || o.type == "bubble";
        s->movers.push_back(m);
    }
    // The player's physics and the kit of the zone (its start powers: the dash, the impulse; the graph
    // also tries a third jump, kept on the edges that need it).
    s->cfg = w.teamSize() > 0 ? w.playerAt(0).config() : PlayerConfig{};
    s->kit.airJumps = s->cfg.airJumps + 1;
    s->kit.dashKey = static_cast<int>(Action::PowerSlot1);
    s->kit.impulseKey = static_cast<int>(Action::PowerSlot2);
    if (const PowerDefinition* d = w.content().power("dash")) {
        s->kit.dashCost = d->energyCost;
        s->kit.dashCooldown = d->cooldown;
        s->kit.dashSpeed = d->param("speed", 430.0f);
        s->kit.dashDuration = d->param("duration", 0.16f);
        s->kit.dashAirUses = static_cast<int>(d->param("airUses", 1));
    }
    if (const PowerDefinition* d = w.content().power("impulse")) {
        s->kit.impulseCost = d->energyCost;
        s->kit.impulseCooldown = d->cooldown;
        s->kit.impulseVelocity = d->param("velocity", 380.0f);
        s->kit.impulseAirUses = static_cast<int>(d->param("airUses", 1));
    }
    s->keepOut.assign(keepOut.begin(), keepOut.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(keepOut.size(), 250)));
    s->key = signature(w, periodic, keepOut);
    return s;
}

uint64_t NavGraph::signature(const GameWorld& w, const std::string& periodic, const std::vector<Rect>& keepOut) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : w.level().id) h = mix(h, c);
    for (unsigned char c : periodic) h = mix(h, c);
    for (const Rect& r : keepOut)
        h = mix(mix(h, static_cast<uint64_t>(std::lround(r.x)) | (static_cast<uint64_t>(std::lround(r.y)) << 32)),
                static_cast<uint64_t>(std::lround(r.w)) | (static_cast<uint64_t>(std::lround(r.h)) << 32));
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
    // The damage zones switched on or off (a curtain of magma that seals an arena).
    for (const auto& e : w.entities())
        if (!e->removed && e->id() != periodic && dynamic_cast<const DamageZoneEntity*>(e.get())) h = mix(h, e->active ? 1u : 2u);
    return mix(h, statics);
}

int NavGraph::delayFor(const GameWorld& w) {
    // A zone's width says how long its graph may take: 0.2 s, plus 1 step per 120 px (the longest
    // routes, 16 832 px: 1.3 s). The same on every computer, cached or not.
    return std::clamp(24 + static_cast<int>(w.level().bounds.w / 120.0f), 24, 160);
}

bool NavGraph::Ticket::finished() const {
    return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

namespace {

/// 2.84: everything a build reads (the snapshot itself), hashed — a graph is taken from the cache only when it was built
/// from the very same world: the simulated players' choices never depend on what an earlier fight of the process left in
/// it (a graph built at another moment, when a liquid, an updraft or a hazard was not the same), so that every computer
/// of a relayed test, cache or not, has the same graph.
uint64_t contentKey(const NavGraph::Snapshot& s);

}  // namespace

NavGraph::Ticket NavGraph::request(const GameWorld& w, const std::string& periodic, const std::vector<Rect>& keepOut) {
    Ticket t;
    std::shared_ptr<Snapshot> snap = snapshot(w, periodic, keepOut);
    t.key = signature(w, periodic, keepOut);
    const uint64_t cacheKey = mix(t.key, contentKey(*snap));  // the cache's own key: the very same world
    t.delay = delayFor(w);
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto& c = cache();
        for (auto it = c.begin(); it != c.end(); ++it)
            if (it->key == cacheKey) {
                c.splice(c.begin(), c, it);  // most recent first
                t.future = c.front().graph;
                return t;
            }
    }
    t.future = std::async(std::launch::async, [snap]() -> std::shared_ptr<const NavGraph> {
                   auto g = std::make_shared<NavGraph>();
                   g->build(*snap);
                   return g;
               }).share();
    std::lock_guard<std::mutex> lock(cacheMutex());
    auto& c = cache();
    c.push_front({cacheKey, t.future});
    while (c.size() > 48) c.pop_back();
    return t;
}

void NavGraph::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex());
    cache().clear();
}

std::shared_ptr<const NavGraph> NavGraph::of(const GameWorld& w, const std::string& periodic, const std::vector<Rect>& keepOut) {
    return request(w, periodic, keepOut).get();
}

void NavGraph::build(const GameWorld& w, const std::string& periodic, const std::vector<Rect>& keepOut) { build(*snapshot(w, periodic, keepOut)); }

// ----------------------------------------------------------------------------- the build

void NavGraph::addEdge(int a, int b, Move m, float cost, uint8_t needs, int32_t program) {
    if (a == b) return;
    for (Edge& e : edges_[static_cast<size_t>(a)])
        if (e.to == b && e.needs == needs) {
            if (cost < e.cost) {
                e.cost = cost;
                e.move = m;
                e.program = program;
            }
            return;
        }
    edges_[static_cast<size_t>(a)].push_back({b, cost, m, needs, program});
}

void NavGraph::build(const Snapshot& s) {
    const auto t0 = std::chrono::steady_clock::now();
    nodes_.clear();
    edges_.clear();
    movers_ = s.movers;
    programs_.clear();
    segment_.clear();
    buckets_.clear();
    stats_ = {};
    PhysicsWorld ph = s.physics;  // its own copy: a move onto a moving platform puts it there
    Builder B{s, ph, {}, {}, 0.0f};
    B.index();
    const float killY = s.killY;
    const Rect bounds = s.bounds;
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
    // 1. Every top of a floor or a one-way platform (by collider id: the same order every time).
    for (const Collider& c : ph.colliders()) {
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
    for (const auto& e : s.env) {
        if (e.kind != 0) continue;
        const Rect r = e.box;
        const int first = static_cast<int>(nodes_.size());
        ++ladderSurface;
        for (float y = r.y + 6.0f; y <= r.bottom() + 0.5f; y += kStep) {
            const Vec2 feet{r.center().x, std::min(y, r.bottom())};
            if (B.free(feet)) addNode(feet, ladderSurface, -1, true);
        }
        const int last = static_cast<int>(nodes_.size()) - 1;
        if (last >= first) ladders.push_back({first, last});
    }
    edges_.resize(nodes_.size());
    // 3. The route of each moving platform, every 32 px.
    for (size_t mi = 0; mi < movers_.size(); ++mi) {
        const Mover& m = movers_[mi];
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
            const int id = addNode(feet, 0x20000000u + static_cast<uint32_t>(mi), static_cast<int>(mi), false);
            if (edges_.size() < nodes_.size()) edges_.resize(nodes_.size());
            if (prev >= 0) {
                const float d = (nodes_[static_cast<size_t>(prev)].feet - feet).length();
                addEdge(prev, id, Move::Ride, d * 1.5f + 4.0f);
                addEdge(id, prev, Move::Ride, d * 1.5f + 4.0f);
            }
            prev = id;
        }
    }
    edges_.resize(nodes_.size());
    keepOut_ = s.keepOut;
    for (Node& n : nodes_) {
        const Rect body = bodyAt(n.feet);
        for (size_t k = 0; k < keepOut_.size(); ++k)
            if (keepOut_[k].intersects(body)) {
                n.out = static_cast<uint8_t>(k + 1);
                break;
            }
    }
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
    // 6. Segments: the places joined by walking or climbing (a moving platform's places: each its own).
    segment_.assign(nodes_.size(), -1);
    {
        int next = 0;
        std::vector<int> stack;
        for (int i = 0; i < size(); ++i) {
            if (segment_[static_cast<size_t>(i)] >= 0) continue;
            const int id = next++;
            segment_[static_cast<size_t>(i)] = id;
            if (nodes_[static_cast<size_t>(i)].mover >= 0) continue;
            stack.assign(1, i);
            while (!stack.empty()) {
                const int u = stack.back();
                stack.pop_back();
                for (const Edge& e : edges_[static_cast<size_t>(u)]) {
                    if (e.move != Move::Walk && e.move != Move::Climb) continue;
                    if (segment_[static_cast<size_t>(e.to)] >= 0) continue;
                    // A passage cuts its floor: the places in it are a segment of their own.
                    if (nodes_[static_cast<size_t>(e.to)].out != nodes_[static_cast<size_t>(u)].out) continue;
                    segment_[static_cast<size_t>(e.to)] = id;
                    stack.push_back(e.to);
                }
            }
        }
    }
    // 7. The moves through the air, found by simulating the player's physics: for each pair of segments
    // within reach, the two best places to take off (the nearest to the other one), the simplest move
    // that lands on it.
    struct Cand {
        int from, to;
        float d;
    };
    std::map<std::pair<int, int>, std::vector<Cand>> pairs;  // (segment from, segment to) → the best takeoffs
    {
        std::map<int, Cand> best;  // per target segment, for one node
        for (int i = 0; i < size(); ++i) {
            const Node& a = nodes_[static_cast<size_t>(i)];
            if (a.ladder) continue;
            best.clear();
            const int bi = std::clamp(static_cast<int>((a.feet.x - bucketX0_) / kBucket), 0, columns - 1);
            const int reachCols = static_cast<int>(kReachAcross / kBucket) + 1;
            for (int bb = std::max(0, bi - reachCols); bb <= std::min(columns - 1, bi + reachCols); ++bb)
                for (int j : buckets_[static_cast<size_t>(bb)]) {
                    const Node& b = nodes_[static_cast<size_t>(j)];
                    if (b.ladder || j == i || segment_[static_cast<size_t>(j)] == segment_[static_cast<size_t>(i)]) continue;
                    if (a.mover >= 0 && b.mover == a.mover) continue;
                    const float up = a.feet.y - b.feet.y, dx = std::fabs(b.feet.x - a.feet.x);
                    if (up > kReachUp || up < -kReachDown || dx > kReachAcross) continue;
                    const float d = dx + std::max(0.0f, up) * 1.5f + std::max(0.0f, -up) * 0.25f;
                    const int seg = segment_[static_cast<size_t>(j)];
                    auto it = best.find(seg);
                    if (it == best.end() || d < it->second.d || (d == it->second.d && j < it->second.to)) best[seg] = {i, j, d};
                }
            for (const auto& [seg, c] : best) {
                auto& list = pairs[{segment_[static_cast<size_t>(i)], seg}];
                list.push_back(c);
            }
        }
    }
    {
        BuildArena arena(ph, s);
        nav::BodySim sim(s.cfg, s.kit);
        std::vector<nav::Program> lib;
        MoveOptions opt;
        opt.ignoreTags = ColliderTag::Teammate;
        // The reach of each move in the open (no wall, nothing but the ledge it leaves): a move that cannot get
        // there even in the open is never simulated toward a place (most pairs out of reach cost nothing).
        struct Envelope {
            nav::Program p;
            std::vector<Vec2> at;
            std::vector<uint8_t> down;
        };
        std::vector<Envelope> envelopes;
        PhysicsWorld open;
        const uint32_t ledge = open.add({-40.0f, 0.0f, 46.0f, 16.0f}, ColliderKind::OneWay);
        auto envelope = [&](const nav::Program& p0) -> const Envelope& {
            nav::Program p = p0;
            p.dir = p.dir < 0 ? 1 : p.dir;
            p.steer = false;
            for (const Envelope& e : envelopes)
                if (e.p == p) return e;
            Envelope e;
            e.p = p;
            nav::Body b;
            b.box = Rect::fromBottomCenter({0.0f, 0.0f}, s.cfg.size);
            b.grounded = b.wasGrounded = true;
            b.groundId = ledge;
            b.airJumpsLeft = s.kit.airJumps;
            b.energy = s.cfg.maxEnergy;
            b.dashAir = s.kit.dashAirUses;
            b.impulseUses = s.kit.impulseAirUses;
            nav::Exec ex;
            bool left = false;
            for (int k = 1; k <= 180; ++k) {
                const nav::Pad pad = nav::padAt(p, k - 1, b, 1e9f, ex, s.kit, s.cfg);
                sim.step(b, open, pad);
                ++stats_.bodySteps;
                e.at.push_back(b.feet());
                e.down.push_back(b.vel.y > 0.0f ? 1 : 0);
                if (!b.grounded) left = true;
                if (left && b.grounded) break;
            }
            envelopes.push_back(std::move(e));
            return envelopes.back();
        };
        auto plausible = [&](const nav::Program& p, float dx, float up) {
            if (p.wall) return true;
            const Envelope& e = envelope(p);
            const float need = std::fabs(dx) - 14.0f, ty = -up;
            float prevY = 0.0f;
            for (size_t k = 0; k < e.at.size(); ++k) {
                const Vec2 q = e.at[k];
                if (e.down[k] && prevY <= ty + 2.0f && q.y >= ty - 2.0f && q.x >= need) return true;
                prevY = q.y;
            }
            return false;
        };
        for (auto& [key, list] : pairs) {
            std::sort(list.begin(), list.end(), [](const Cand& x, const Cand& y) {
                if (x.d != y.d) return x.d < y.d;
                return x.from < y.from;
            });
            int tried = 0;
            bool found = false;
            for (const Cand& c : list) {
                if (tried >= 2 || found || stats_.simulations >= kMaxSimulations) break;
                // Two takeoffs side by side are the same move: the next one (only when the first found nothing)
                // at least 32 px away.
                if (tried == 1 && std::fabs(nodes_[static_cast<size_t>(c.from)].feet.x - nodes_[static_cast<size_t>(list.front().from)].feet.x) < 32.0f)
                    continue;
                const Node& A = nodes_[static_cast<size_t>(c.from)];
                const Node& T = nodes_[static_cast<size_t>(c.to)];
                // A moving platform at either end: put where that place of its route is.
                auto place = [&](const Node& n, bool on) {
                    if (n.mover < 0) return;
                    const Mover& m = movers_[static_cast<size_t>(n.mover)];
                    if (Collider* col = ph.get(m.collider)) {
                        col->enabled = on;
                        const Vec2 tl{n.feet.x - m.w * 0.5f, n.feet.y};
                        col->box = m.oneway ? Rect{tl.x, tl.y, m.w, 4.0f} : Rect{tl.x, tl.y, m.w, m.h};
                        col->velocity = {};
                    }
                };
                place(A, true);
                place(T, true);
                nav::Body body;
                body.box = Rect::fromBottomCenter(A.feet, s.cfg.size);
                if (ph.overlapsSolid(body.box, opt)) {
                    // A place the platform covers when it is there (a goods lift at rest on the floor): not a takeoff.
                    place(A, false);
                    place(T, false);
                    continue;
                }
                ++tried;
                ++stats_.pairs;
                const Collider* ground = ph.groundBelow(body.box, opt);
                if (ground) {
                    body.grounded = body.wasGrounded = true;
                    body.groundId = ground->id;
                    const float dxT = T.feet.x - A.feet.x;
                    body.facing = dxT < 0.0f ? -1.0f : 1.0f;
                    body.aim = {body.facing, 0.0f};
                    body.airJumpsLeft = s.kit.airJumps;
                    body.energy = s.cfg.maxEnergy;
                    body.dashAir = s.kit.dashAirUses;
                    body.impulseUses = s.kit.impulseAirUses;
                    const float up = A.feet.y - T.feet.y;
                    int wallSide = 0;
                    for (int side : {-1, 1}) {
                        const Rect probe{A.feet.x + static_cast<float>(side) * 30.0f - 8.0f, A.feet.y - 60.0f, 16.0f, 50.0f};
                        if (B.blocked(probe) && static_cast<float>(side) * dxT <= 0.0f && up > 40.0f) wallSide = side;
                    }
                    nav::library(lib, dxT, up, s.kit, ground->kind == ColliderKind::OneWay, wallSide);
                    arena.floorY = std::max(A.feet.y, T.feet.y) + 200.0f;
                    arena.allowed = T.out != 0 ? T.out : A.out;
                    const int targetSeg = segment_[static_cast<size_t>(c.to)];
                    const uint32_t moverCol = T.mover >= 0 ? movers_[static_cast<size_t>(T.mover)].collider : 0;
                    int landNode = -1;
                    auto accept = [&](const nav::Body& b, int) {
                        landNode = -1;
                        if (!b.grounded) return false;
                        if (moverCol) {
                            if (b.groundId != moverCol) return false;
                            landNode = c.to;
                            return true;
                        }
                        const Vec2 f = b.feet();
                        const int n = nearest(f, 12.0f, [&](const Node& nd) { return nd.mover < 0 && !nd.ladder && std::fabs(nd.feet.y - f.y) < 1.5f; });
                        if (n < 0 || segment_[static_cast<size_t>(n)] != targetSeg) return false;
                        landNode = n;
                        return true;
                    };
                    const float tx = T.feet.x;
                    // The simplest move that lands there, and for the players who lack what it needs (the
                    // triple jump, the impulse, the dash), the simplest they can do: one edge per need.
                    std::vector<uint8_t> got;
                    for (const nav::Program& p : lib) {
                        if (stats_.simulations >= kMaxSimulations) break;
                        const uint8_t needs = p.needs();
                        bool covered = false;
                        for (uint8_t g : got) covered = covered || (g & ~needs) == 0;
                        if (covered || !plausible(p, dxT, up)) continue;
                        ++stats_.simulations;
                        const nav::Outcome o = nav::simulate(p, body, 0, sim, arena, [&](int) { return tx; }, accept, p.wall > 0 ? 240 : 180);
                        if (!o.accepted || landNode < 0) continue;
                        programs_.push_back(p);
                        const bool drop = T.feet.y > A.feet.y + 4.0f;
                        addEdge(c.from, landNode, drop ? Move::Drop : Move::Jump, static_cast<float>(o.steps) * 1.25f + 8.0f, needs,
                                static_cast<int32_t>(programs_.size()) - 1);
                        ++stats_.byStyle[static_cast<int>(p.style())];
                        found = true;
                        got.push_back(needs);
                        if (needs == 0) break;
                    }
                }
                place(A, false);
                place(T, false);
            }
        }
        stats_.bodySteps += arena.steps;
    }
    stats_.bytes = sizeof(NavGraph) + nodes_.capacity() * sizeof(Node) + programs_.capacity() * sizeof(nav::Program) +
                   segment_.capacity() * sizeof(int) + movers_.capacity() * sizeof(Mover);
    for (const auto& e : edges_) stats_.bytes += e.capacity() * sizeof(Edge) + sizeof(e);
    for (const auto& b : buckets_) stats_.bytes += b.capacity() * sizeof(int) + sizeof(b);
    buildMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ----------------------------------------------------------------------------- queries

size_t NavGraph::edgeCount() const {
    size_t n = 0;
    for (const auto& e : edges_) n += e.size();
    return n;
}

NavGraph::Move NavGraph::moveBetween(int a, int b) const {
    const Edge* e = edgeBetween(a, b);
    return e ? e->move : Move::Walk;
}

const NavGraph::Edge* NavGraph::edgeBetween(int a, int b, uint8_t can) const {
    if (a < 0 || a >= size()) return nullptr;
    const Edge* best = nullptr;
    for (const Edge& e : edges_[static_cast<size_t>(a)])
        if (e.to == b && (e.needs & ~can) == 0 && (!best || e.cost < best->cost)) best = &e;
    return best;
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

bool NavGraph::path(int from, int to, std::vector<int>& out, int budget, uint8_t can, const std::function<bool(int, int)>& avoid) const {
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
    open.push({(goal - nodes_[static_cast<size_t>(from)].feet).length() * 0.8f, from});
    int expanded = 0;
    while (!open.empty()) {
        const int u = open.top().second;
        open.pop();
        if (closed[static_cast<size_t>(u)]) continue;
        closed[static_cast<size_t>(u)] = 1;
        if (u == to) break;
        if (++expanded > budget) return false;
        for (const Edge& e : edges_[static_cast<size_t>(u)]) {
            if (e.needs & ~can) continue;
            if (avoid && avoid(u, e.to)) continue;
            const float ng = g[static_cast<size_t>(u)] + e.cost;
            if (ng >= g[static_cast<size_t>(e.to)]) continue;
            g[static_cast<size_t>(e.to)] = ng;
            parent[static_cast<size_t>(e.to)] = u;
            open.push({ng + (goal - nodes_[static_cast<size_t>(e.to)].feet).length() * 0.8f, e.to});
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

int NavGraph::search(int from, const std::function<bool(int)>& ok, float maxCost, std::vector<int>* out, uint8_t can) const {
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
            if (e.needs & ~can) continue;
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

int NavGraph::closest(int from, Vec2 goal, float maxCost, std::vector<int>* out, uint8_t can, const std::function<bool(int, int)>& avoid) const {
    if (out) out->clear();
    if (from < 0 || from >= size()) return -1;
    const size_t n = nodes_.size();
    std::vector<float> g(n, 1e30f);
    std::vector<int> parent(n, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[static_cast<size_t>(from)] = 0.0f;
    open.push({0.0f, from});
    int best = from;
    float bestD = (nodes_[static_cast<size_t>(from)].feet - goal).lengthSq();
    while (!open.empty()) {
        const auto [d, u] = open.top();
        open.pop();
        if (d > g[static_cast<size_t>(u)]) continue;
        if (d > maxCost) break;
        const float gd = (nodes_[static_cast<size_t>(u)].feet - goal).lengthSq();
        if (gd < bestD || (gd == bestD && u < best)) {
            bestD = gd;
            best = u;
        }
        for (const Edge& e : edges_[static_cast<size_t>(u)]) {
            if (e.needs & ~can) continue;
            if (avoid && avoid(u, e.to)) continue;
            const float ng = d + e.cost;
            if (ng >= g[static_cast<size_t>(e.to)]) continue;
            g[static_cast<size_t>(e.to)] = ng;
            parent[static_cast<size_t>(e.to)] = u;
            open.push({ng, e.to});
        }
    }
    if (out) {
        for (int v = best; v >= 0; v = parent[static_cast<size_t>(v)]) {
            out->push_back(v);
            if (v == from) break;
        }
        std::reverse(out->begin(), out->end());
    }
    return best;
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
        for (const Edge& e : edges_[i]) h = mix(h, (static_cast<uint64_t>(e.to) << 16) | (static_cast<uint64_t>(e.move) << 8) | e.needs);
    }
    for (const nav::Program& p : programs_)
        h = mix(h, static_cast<uint64_t>(p.jump) | (static_cast<uint64_t>(p.air1) << 8) | (static_cast<uint64_t>(p.dash) << 16) |
                       (static_cast<uint64_t>(p.impulse) << 24) | (static_cast<uint64_t>(static_cast<uint8_t>(p.dir)) << 32));
    return h;
}

}  // namespace cosmic::raid

namespace cosmic::raid {
namespace {

uint64_t contentKey(const NavGraph::Snapshot& s) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    const auto f = [&](float v) { h = mix(h, static_cast<uint64_t>(static_cast<int64_t>(std::lround(v * 8.0f)))); };
    const auto r = [&](const Rect& q) {
        f(q.x);
        f(q.y);
        f(q.w);
        f(q.h);
    };
    for (unsigned char c : s.level) h = mix(h, c);
    r(s.bounds);
    f(s.killY);
    for (const Collider& c : s.physics.colliders()) {
        h = mix(h, (static_cast<uint64_t>(c.id) << 8) | (c.enabled ? 1u : 0u) | (c.dynamic ? 2u : 0u) | (static_cast<uint64_t>(c.kind) << 2));
        h = mix(h, static_cast<uint64_t>(c.tags));
        r(c.box);
    }
    for (const Rect& q : s.hazards) r(q);
    for (const auto& e : s.env) {
        r(e.box);
        h = mix(h, static_cast<uint64_t>(e.kind));
        f(e.strength);
    }
    for (const auto& m : s.movers) {
        h = mix(h, (static_cast<uint64_t>(m.collider) << 1) | (m.oneway ? 1u : 0u));
        f(m.a.x);
        f(m.a.y);
        f(m.b.x);
        f(m.b.y);
        f(m.w);
        f(m.h);
    }
    for (const Rect& q : s.keepOut) r(q);
    f(s.cfg.maxRunSpeed);
    f(s.cfg.jumpHeight);
    f(s.cfg.doubleJumpHeight);
    h = mix(h, static_cast<uint64_t>(s.cfg.airJumps));
    h = mix(h, static_cast<uint64_t>(s.kit.airJumps) | (static_cast<uint64_t>(s.kit.dashKey + 1) << 8) | (static_cast<uint64_t>(s.kit.impulseKey + 1) << 16));
    f(s.kit.dashCost);
    f(s.kit.dashSpeed);
    f(s.kit.impulseVelocity);
    return h;
}

}  // namespace
}  // namespace cosmic::raid
