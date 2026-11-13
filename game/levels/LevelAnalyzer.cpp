#include "game/levels/LevelAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>
#include <set>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {

constexpr float kCell = 8.0f;
constexpr float kInf = std::numeric_limits<float>::infinity();

struct Grid {
    int w = 0, h = 0;
    float ox = 0.0f, oy = 0.0f;
    std::vector<uint8_t> solid, deadly, platform, climb, water, updraft, bounce;
    std::vector<int> sat;  // summed-area table of solid (w+1) x (h+1)

    int idx(int x, int y) const { return y * w + x; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    void init(const Rect& b) {
        // The grid starts at the level ceiling (kCeilingMargin above the bounds).
        ox = b.x;
        oy = b.y - kCeilingMargin;
        w = std::max(1, static_cast<int>(std::ceil(b.w / kCell)));
        h = std::max(1, static_cast<int>(std::ceil((b.h + kCeilingMargin) / kCell)));
        size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (auto* v : {&solid, &deadly, &platform, &climb, &water, &updraft, &bounce}) v->assign(n, 0);
    }

    template <typename F>
    void forCells(const Rect& r, F f) const {
        int x0 = std::max(0, static_cast<int>(std::floor((r.x - ox) / kCell)));
        int y0 = std::max(0, static_cast<int>(std::floor((r.y - oy) / kCell)));
        int x1 = std::min(w - 1, static_cast<int>(std::ceil((r.right() - ox) / kCell)) - 1);
        int y1 = std::min(h - 1, static_cast<int>(std::ceil((r.bottom() - oy) / kCell)) - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) f(x, y);
    }
    void mark(std::vector<uint8_t>& layer, const Rect& r, uint8_t v = 1) {
        forCells(r, [&](int x, int y) { layer[static_cast<size_t>(idx(x, y))] = v; });
    }

    void buildSat() {
        sat.assign(static_cast<size_t>(w + 1) * static_cast<size_t>(h + 1), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                sat[static_cast<size_t>((y + 1) * (w + 1) + x + 1)] = solid[static_cast<size_t>(idx(x, y))] + sat[static_cast<size_t>(y * (w + 1) + x + 1)] +
                                                                      sat[static_cast<size_t>((y + 1) * (w + 1) + x)] - sat[static_cast<size_t>(y * (w + 1) + x)];
    }
    /// Solid cells in [x0, x1] x [y0, y1] (outside the grid counts as solid at the top and
    /// the sides, open at the bottom).
    int solidIn(int x0, int y0, int x1, int y1) const {
        if (x0 < 0 || x1 >= w || y0 < 0) return 1;
        y1 = std::min(y1, h - 1);
        if (y1 < y0) return 0;
        auto at = [&](int x, int y) { return sat[static_cast<size_t>(y * (w + 1) + x)]; };
        return at(x1 + 1, y1 + 1) - at(x0, y1 + 1) - at(x1 + 1, y0) + at(x0, y0);
    }
};

struct Model {
    Grid g;
    int bw = 2, bh = 3;  // player box in cells
    std::vector<uint8_t> free, support, reach;
    std::vector<float> riseBonus;

    bool isFree(int x, int y) const { return g.in(x, y) && free[static_cast<size_t>(g.idx(x, y))]; }
    bool canEnter(int x, int y) const { return isFree(x, y) && !g.deadly[static_cast<size_t>(g.idx(x, y))]; }
};

bool hasPower(const std::set<std::string>& powers, const char* id) { return powers.count(id) > 0; }

/// Free cell nearest to `p` (feet position) within `radius` cells.
int nearestFree(const Model& m, Vec2 p, int radius) {
    int cx = static_cast<int>(std::floor((p.x - m.g.ox) / kCell));
    int cy = static_cast<int>(std::floor((p.y - m.g.oy - 0.01f) / kCell));
    int best = -1;
    float bestD = kInf;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            int x = cx + dx, y = cy + dy;
            if (!m.canEnter(x, y)) continue;
            float d = static_cast<float>(dx * dx + dy * dy);
            if (d < bestD) {
                bestD = d;
                best = m.g.idx(x, y);
            }
        }
    return best;
}

/// Shortest route between two cells (px), with the path when requested. `to` < 0: every
/// distance from `from` (returned through `all`).
float shortest(const Model& m, int from, int to, std::vector<int>* path, std::vector<float>* all = nullptr) {
    const Grid& g = m.g;
    size_t n = static_cast<size_t>(g.w) * static_cast<size_t>(g.h);
    std::vector<float> dist(n, kInf);
    std::vector<int> parent(n, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> q;
    dist[static_cast<size_t>(from)] = 0.0f;
    q.push({0.0f, from});
    static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    while (!q.empty()) {
        auto [d, c] = q.top();
        q.pop();
        if (d > dist[static_cast<size_t>(c)]) continue;
        if (c == to) break;
        int cx = c % g.w, cy = c / g.w;
        for (const auto& dv : dirs) {
            int nx = cx + dv[0], ny = cy + dv[1];
            if (!m.canEnter(nx, ny)) continue;
            int ni = g.idx(nx, ny);
            // Rising or moving sideways needs the reachable band; falling is always possible.
            if (!m.reach[static_cast<size_t>(ni)] && ny <= cy) continue;
            // No corner cutting through solids.
            if (dv[0] != 0 && dv[1] != 0 && (!m.isFree(cx + dv[0], cy) || !m.isFree(cx, cy + dv[1]))) continue;
            float nd = d + ((dv[0] != 0 && dv[1] != 0) ? 1.41421356f : 1.0f) * kCell;
            if (nd < dist[static_cast<size_t>(ni)]) {
                dist[static_cast<size_t>(ni)] = nd;
                parent[static_cast<size_t>(ni)] = c;
                q.push({nd, ni});
            }
        }
    }
    if (all) *all = dist;
    if (to < 0) return 0.0f;
    float r = dist[static_cast<size_t>(to)];
    if (path && std::isfinite(r)) {
        path->clear();
        for (int c = to; c != -1; c = parent[static_cast<size_t>(c)]) path->push_back(c);
        std::reverse(path->begin(), path->end());
    }
    return r;
}

/// `closedDoors`: doors closed at the start (opened by levers, receptors or by winning an
/// arena) are walls. The shortest route is computed with them closed (nobody opens them
/// early), the intended route with them open (progression opens them).
Model buildModel(const LevelData& level, const ContentDatabase& content, const std::set<std::string>& powers, float& maxRise, float& airReach,
                 bool closedDoors = false) {
    Model m;
    Grid& g = m.g;
    g.init(level.bounds);
    const Json& pc = content.playerConfig();
    Vec2 size{10.0f, 22.0f};
    if (pc.contains("size") && pc["size"].is_array() && pc["size"].size() == 2) size = {pc["size"][0].get<float>(), pc["size"][1].get<float>()};
    m.bw = std::max(1, static_cast<int>(std::ceil(size.x / kCell)));
    m.bh = std::max(1, static_cast<int>(std::ceil(size.y / kCell)));

    // Abilities.
    float jump = jget<float>(pc, "jumpHeight", 58.0f);
    float airJump = jget<float>(pc, "doubleJumpHeight", 44.0f);
    int airJumps = jget<int>(pc, "airJumps", 1);
    float timeToApex = jget<float>(pc, "timeToApex", 0.38f);
    float gravity = 2.0f * jump / (timeToApex * timeToApex);
    float run = jget<float>(pc, "maxRunSpeed", 150.0f);
    auto param = [&](const char* id, const char* key, float def) {
        const PowerDefinition* d = content.power(id);
        return d ? d->param(key, def) : def;
    };
    if (hasPower(powers, "triple_jump")) airJumps += static_cast<int>(param("triple_jump", "jumps", 1.0f));
    maxRise = jump + airJump * static_cast<float>(airJumps);
    if (hasPower(powers, "impulse")) {
        float v = param("impulse", "velocity", 380.0f);
        maxRise += v * v / (2.0f * gravity);
    }
    if (hasPower(powers, "teleport")) maxRise += param("teleport", "distance", 80.0f);
    float airTime = 2.0f * timeToApex * static_cast<float>(1 + airJumps);
    airReach = run * airTime;
    if (hasPower(powers, "dash")) airReach += param("dash", "speed", 430.0f) * param("dash", "duration", 0.16f);
    if (hasPower(powers, "teleport")) airReach += param("teleport", "distance", 80.0f);
    if (hasPower(powers, "glide")) airReach += maxRise / std::max(10.0f, param("glide", "fallSpeed", 52.0f)) * param("glide", "airSpeed", 175.0f);
    float grappleRange = hasPower(powers, "grapple") ? param("grapple", "range", 170.0f) : 0.0f;

    // Rasterisation.
    std::vector<Vec2> grapplePoints;
    for (const auto& o : level.objects) {
        const std::string& t = o.type;
        if (t == "solid") {
            Rect r = o.rect;
            if (r.y <= level.bounds.y + 0.5f) {  // up to the ceiling, as the engine does
                r.h += r.y - (level.bounds.y - kCeilingMargin);
                r.y = level.bounds.y - kCeilingMargin;
            }
            if (o.prop<bool>("collision", true)) g.mark(g.solid, r);
        } else if (t == "door") {
            if (closedDoors && !o.prop<bool>("open", false)) g.mark(g.solid, o.rect);
        }
        else if (t == "power_gate") {
            if (!powers.count(o.propStr("power"))) g.mark(g.solid, o.rect);
        } else if (t == "oneway" || t == "crumble_platform" || t == "moving_platform") g.mark(g.platform, o.rect);
        else if (t == "ladder") g.mark(g.climb, o.rect);
        else if (t == "updraft") g.mark(g.updraft, o.rect);
        else if (t == "bounce_pad") g.mark(g.bounce, o.rect);
        else if (t == "grapple_point") grapplePoints.push_back(o.rect.center());
        else if (t == "liquid") {
            std::string kind = o.propStr("kind", "water");
            if (kind == "acid" || kind == "lava") g.mark(g.deadly, o.rect);
            else g.mark(g.water, o.rect);
        }
    }
    float killY = level.killY > 0.0f ? level.killY : level.bounds.bottom() + 64.0f;
    g.mark(g.deadly, {level.bounds.x, killY - 4.0f, level.bounds.w, level.bounds.bottom() + 64.0f - killY + 4.0f});
    g.buildSat();

    size_t n = static_cast<size_t>(g.w) * static_cast<size_t>(g.h);
    m.free.assign(n, 0);
    m.support.assign(n, 0);
    m.reach.assign(n, 0);
    m.riseBonus.assign(n, 0.0f);
    // Free: the player's box (bw x bh cells, feet on row y) holds no solid.
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) m.free[static_cast<size_t>(g.idx(x, y))] = g.solidIn(x, y - m.bh + 1, x + m.bw - 1, y) == 0;

    auto anyIn = [&](const std::vector<uint8_t>& layer, int x0, int y0, int x1, int y1) {
        for (int y = std::max(0, y0); y <= std::min(g.h - 1, y1); ++y)
            for (int x = std::max(0, x0); x <= std::min(g.w - 1, x1); ++x)
                if (layer[static_cast<size_t>(g.idx(x, y))]) return true;
        return false;
    };
    float bounceBonus = 0.0f;
    for (const auto& o : level.objects)
        if (o.type == "bounce_pad") bounceBonus = std::max(bounceBonus, o.prop<float>("power", 600.0f));
    bounceBonus = bounceBonus * bounceBonus / (2.0f * gravity);

    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            size_t i = static_cast<size_t>(g.idx(x, y));
            if (!m.free[i] || g.deadly[i]) continue;
            int top = y - m.bh + 1, right = x + m.bw - 1;
            bool floor = y + 1 >= g.h ? false : (g.solidIn(x, y + 1, right, y + 1) > 0 || anyIn(g.platform, x, y + 1, right, y + 1));
            bool wall = g.solidIn(x - 1, top, x - 1, y) > 0 || g.solidIn(right + 1, top, right + 1, y) > 0;
            bool special = anyIn(g.climb, x, top, right, y) || anyIn(g.water, x, top, right, y) || anyIn(g.updraft, x, top, right, y);
            bool grapple = false;
            if (grappleRange > 0.0f) {
                Vec2 c{g.ox + (static_cast<float>(x) + m.bw * 0.5f) * kCell, g.oy + (static_cast<float>(y) + 1.0f - m.bh * 0.5f) * kCell};
                for (const auto& gp : grapplePoints)
                    if ((gp - c).length() <= grappleRange) {
                        grapple = true;
                        break;
                    }
            }
            if (floor || wall || special || grapple) m.support[i] = 1;
            if (floor && anyIn(g.bounce, x, y + 1, right, y + 1)) m.riseBonus[i] = bounceBonus;
        }

    // Vertical band: free cells up to maxRise above a support in the same column.
    std::vector<uint8_t> band(n, 0);
    for (int x = 0; x < g.w; ++x) {
        float above = kInf;
        for (int y = g.h - 1; y >= 0; --y) {
            size_t i = static_cast<size_t>(g.idx(x, y));
            if (!m.free[i]) {
                above = kInf;
                continue;
            }
            if (m.support[i]) above = -m.riseBonus[i];
            else if (std::isfinite(above)) above += kCell;
            if (std::isfinite(above) && above <= maxRise) band[i] = 1;
            else if (std::isfinite(above) && above > maxRise) above = kInf;
        }
    }
    // Horizontal air travel from the band, through free cells only.
    for (int y = 0; y < g.h; ++y) {
        for (int pass = 0; pass < 2; ++pass) {
            float since = kInf;
            for (int k = 0; k < g.w; ++k) {
                int x = pass == 0 ? k : g.w - 1 - k;
                size_t i = static_cast<size_t>(g.idx(x, y));
                if (!m.free[i]) {
                    since = kInf;
                    continue;
                }
                if (band[i]) since = 0.0f;
                else if (std::isfinite(since)) since += kCell;
                if (std::isfinite(since) && since <= airReach) m.reach[i] = 1;
            }
        }
    }
    return m;
}

std::set<std::string> powersFor(const LevelData& level, const LevelAnalyzerOptions& opt) {
    std::set<std::string> p(level.startPowers.begin(), level.startPowers.end());
    for (const auto& o : level.objects)
        if (o.type == "power_unlock") p.insert(o.propStr("power"));
    p.insert(opt.extraPowers.begin(), opt.extraPowers.end());
    return p;
}

Vec2 cellFeet(const Model& m, int c) {
    int x = c % m.g.w, y = c / m.g.w;
    return {m.g.ox + (static_cast<float>(x) + m.bw * 0.5f) * kCell, m.g.oy + (static_cast<float>(y) + 1.0f) * kCell};
}

}  // namespace

LevelAnalysis analyzeLevel(const LevelData& level, const ContentDatabase& content, const LevelAnalyzerOptions& opt) {
    LevelAnalysis a;
    auto powers = powersFor(level, opt);
    Model m = buildModel(level, content, powers, a.maxRise, a.airReach);         // progression: doors open
    Model strict = buildModel(level, content, powers, a.maxRise, a.airReach, true);  // doors closed
    const Grid& g = m.g;
    for (int x = 0; x < g.w && !a.reachesTop; ++x)
        for (int y = 0; y < m.bh + 1 && !a.reachesTop; ++y) a.reachesTop = m.reach[static_cast<size_t>(g.idx(x, y))] != 0;

    // Start, checkpoints (level order), exit.
    int start = -1, exitCell = -1;
    std::vector<int> cps;
    for (const auto& o : level.objects) {
        if (o.type == "player_start") start = nearestFree(m, o.rect.bottomCenter(), 4);
        else if (o.type == "exit") exitCell = nearestFree(m, o.rect.bottomCenter(), 6);
        else if (o.type == "checkpoint") {
            // Checkpoints of an optional detour or of an alternative route are not required.
            if (o.prop<bool>("optional", false)) continue;
            LevelAnalysis::Checkpoint cp;
            cp.label = o.propStr("label", o.id);
            cp.pos = o.rect.bottomCenter();
            int c = nearestFree(m, cp.pos, 6);
            if (c < 0) {
                a.problems.push_back("point de contrôle « " + cp.label + " » dans le décor");
                continue;
            }
            a.checkpoints.push_back(cp);
            cps.push_back(c);
        }
    }
    if (start < 0 || exitCell < 0) {
        a.problems.push_back(start < 0 ? "départ introuvable" : "sortie introuvable");
        return a;
    }
    std::vector<int> path;
    a.shortestRoute = shortest(m, start, exitCell, &path);
    a.reachable = std::isfinite(a.shortestRoute);
    if (!a.reachable) {
        a.problems.push_back("sortie inaccessible selon le modèle");
        return a;
    }
    for (int c : path) a.route.push_back(cellFeet(m, c));

    // Progress order of the checkpoints: their distance from the start (files do not
    // always list them in the order they are reached).
    std::vector<float> fromStart;
    shortest(m, start, -1, nullptr, &fromStart);
    std::vector<size_t> order(cps.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t l, size_t r) { return fromStart[static_cast<size_t>(cps[l])] < fromStart[static_cast<size_t>(cps[r])]; });
    {
        std::vector<int> c2;
        std::vector<LevelAnalysis::Checkpoint> p2;
        for (size_t k : order) {
            c2.push_back(cps[k]);
            p2.push_back(a.checkpoints[k]);
        }
        cps = std::move(c2);
        a.checkpoints = std::move(p2);
    }
    // Intended route: through every checkpoint in that order.
    int prev = start;
    a.intendedRoute = 0.0f;
    for (size_t k = 0; k <= cps.size(); ++k) {
        int target = k < cps.size() ? cps[k] : exitCell;
        std::vector<int> seg;
        float d = shortest(m, prev, target, &seg);
        for (int c : seg) a.intendedPath.push_back(cellFeet(m, c));
        if (!std::isfinite(d)) {
            a.problems.push_back(k < cps.size() ? "point de contrôle « " + a.checkpoints[k].label + " » inaccessible selon le modèle"
                                                : "sortie inaccessible après le dernier point de contrôle");
            break;
        }
        a.intendedRoute += d;
        prev = target;
    }
    a.ratio = a.intendedRoute > 0.0f ? a.shortestRoute / a.intendedRoute : 1.0f;
    if (a.ratio < opt.minRatio) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "raccourci : la route la plus courte fait %.0f %% de la route prévue (%.0f px au lieu de %.0f)",
                      a.ratio * 100.0f, a.shortestRoute, a.intendedRoute);
        a.problems.push_back(buf);
    }
    // Doors closed at the start (arenas, levers, receptors): a local way around one makes it
    // useless (an arena skipped over the top...).
    for (const auto& o : level.objects) {
        if (o.type != "door" || o.prop<bool>("open", false)) continue;
        Vec2 feet = o.rect.bottomCenter();
        int before = nearestFree(strict, feet - Vec2{o.rect.w * 0.5f + 20.0f, 0.0f}, 3);
        int after = nearestFree(strict, feet + Vec2{o.rect.w * 0.5f + 20.0f, 0.0f}, 3);
        if (before < 0 || after < 0) continue;
        std::vector<int> around;
        float d = shortest(strict, before, after, &around);
        if (std::isfinite(d) && d < opt.doorDetour) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "porte fermée %s (x=%.0f) contournable par un détour de %.0f px", o.id.c_str(), o.rect.x, d);
            a.problems.push_back(buf);
            LevelAnalysis::Bypass b;
            b.door = o.rect;
            for (int c : around) b.route.push_back(cellFeet(strict, c));
            a.doorBypasses.push_back(std::move(b));
        }
    }
    for (auto& cp : a.checkpoints) {
        float best = kInf;
        for (const auto& p : a.route) best = std::min(best, (p - cp.pos).length());
        cp.distanceToRoute = best;
        cp.skipped = best > opt.skipDistance;
        if (cp.skipped) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "point de contrôle « %s » contourné (à %.0f px de la route la plus courte)", cp.label.c_str(), best);
            a.problems.push_back(buf);
        }
    }
    return a;
}

std::vector<Rect> suggestGates(const LevelData& level, const LevelAnalysis& a, float clearance, float width, bool deviationGates) {
    constexpr float kTile = 16.0f;
    constexpr float kPlayerHeight = 24.0f;
    // Optional high places (secrets, relics, hearts) must stay reachable: a gate never cuts
    // them; it is moved sideways instead (up to 128 px), or not placed.
    std::vector<Rect> protectedAreas;
    for (const auto& o : level.objects) {
        if (o.type == "secret_area" || o.type == "relic" || o.type == "heart_container") protectedAreas.push_back(o.rect.expanded(48.0f));
        // Designed vertical routes: vines / ladders, updrafts, grapple anchors.
        if (o.type == "ladder" || o.type == "updraft") protectedAreas.push_back(o.rect.expanded(24.0f));
        if (o.type == "grapple_point") protectedAreas.push_back(o.rect.expanded(64.0f));
    }
    auto clashes = [&](const Rect& r) {
        return std::any_of(protectedAreas.begin(), protectedAreas.end(), [&](const Rect& p) { return p.intersects(r); });
    };
    const float top = level.bounds.y - kCeilingMargin;  // gates start at the level ceiling
    std::vector<Rect> out;
    // Last resort: the column is cut around the protected areas it crosses (the hole is the
    // protected place itself, the analysis then checks that it is not a way through).
    auto split = [&](const Rect& r) {
        std::vector<std::pair<float, float>> holes;
        for (const auto& p : protectedAreas)
            if (p.intersects(r)) holes.push_back({p.y, p.bottom()});
        std::sort(holes.begin(), holes.end());
        float y = r.y;
        for (const auto& [h0, h1] : holes) {
            float end = std::floor(h0 / kTile) * kTile;
            if (end - y >= 2.0f * kTile) out.push_back({r.x, y, r.w, end - y});
            y = std::max(y, std::ceil(h1 / kTile) * kTile);
        }
        if (r.bottom() - y >= 2.0f * kTile) out.push_back({r.x, y, r.w, r.bottom() - y});
    };
    auto place = [&](Rect r) {
        if (!clashes(r)) {
            out.push_back(r);
            return;
        }
        for (float shift = kTile; shift <= 128.0f; shift += kTile)
            for (float dir : {-1.0f, 1.0f}) {
                Rect m = r.translated({dir * shift, 0.0f});
                // The bottom is recomputed for the new place (never across the intended route).
                float highest = kInf;
                for (const auto& p : a.intendedPath)
                    if (p.x >= m.x - kTile && p.x <= m.right() + kTile) highest = std::min(highest, p.y);
                if (!std::isfinite(highest)) continue;
                float bottom = std::floor((highest - kPlayerHeight - clearance) / kTile) * kTile;
                if (bottom - level.bounds.y < 2.0f * kTile) continue;
                m.h = std::min(m.h, bottom - top);
                if (!clashes(m)) {
                    out.push_back(m);
                    return;
                }
            }
        split(r);
    };
    for (const auto& cp : a.checkpoints) {
        if (!cp.skipped) continue;
        float x0 = std::floor((cp.pos.x - width * 0.5f) / kTile) * kTile;
        float x1 = x0 + width;
        float highest = kInf;
        for (const auto& p : a.intendedPath)
            if (p.x >= x0 - kTile && p.x <= x1 + kTile) highest = std::min(highest, p.y);
        if (!std::isfinite(highest)) continue;
        float bottom = std::floor((highest - kPlayerHeight - clearance) / kTile) * kTile;
        if (bottom - level.bounds.y < 2.0f * kTile) continue;
        place({x0, top, x1 - x0, bottom - top});
    }
    // Where the shortest route leaves the intended one to go over something (a hood, a wall):
    // a column at the start of each deviation, above the intended route.
    // (Disabled by default: the intended route is the shortest one through the checkpoints,
    // not always the designer's path; designed high routes would be cut.)
    if (deviationGates && std::any_of(a.checkpoints.begin(), a.checkpoints.end(), [](const auto& c) { return c.skipped; })) {
        auto distToIntended = [&](Vec2 p) {
            float best = kInf;
            for (const auto& q : a.intendedPath) best = std::min(best, (p - q).length());
            return best;
        };
        bool inside = false;
        for (size_t i = 0; i < a.route.size(); ++i) {
            const Vec2& p = a.route[i];
            bool away = distToIntended(p) > 128.0f && p.y < a.route[i > 0 ? i - 1 : 0].y + 1.0f;
            if (away && !inside) {
                // Deviation starts: gate just before this point, above the intended route there.
                float x0 = std::floor((p.x - width * 0.5f) / kTile) * kTile;
                float highest = kInf;
                for (const auto& q : a.intendedPath)
                    if (q.x >= x0 - kTile && q.x <= x0 + width + kTile) highest = std::min(highest, q.y);
                if (std::isfinite(highest)) {
                    float bottom = std::floor((highest - kPlayerHeight - clearance) / kTile) * kTile;
                    if (bottom - level.bounds.y >= 2.0f * kTile && bottom < p.y) place({x0, top, width, bottom - top});
                }
            }
            inside = away;
        }
    }
    // Above a door that can be walked around: a column from the top down to the door, as wide
    // as the detour's highest point needs (the wall around the door is extended upwards).
    for (const auto& b : a.doorBypasses) {
        float x0 = std::floor(b.door.x / kTile) * kTile;
        float x1 = std::max(x0 + kTile, std::ceil(b.door.right() / kTile) * kTile);
        float bottom = std::floor(b.door.y / kTile) * kTile;
        if (bottom - level.bounds.y < 2.0f * kTile) continue;
        place({x0, top, x1 - x0, bottom - top});
    }
    return out;
}

bool writeAnalysisImage(const LevelData& level, const ContentDatabase& content, const LevelAnalysis& a, const std::string& path,
                        const LevelAnalyzerOptions& opt) {
    float rise = 0.0f, reach = 0.0f;
    Model m = buildModel(level, content, powersFor(level, opt), rise, reach);
    const Grid& g = m.g;
    std::vector<uint8_t> px(static_cast<size_t>(g.w) * static_cast<size_t>(g.h) * 3, 0);
    auto put = [&](int x, int y, int r, int gg, int b) {
        if (!g.in(x, y)) return;
        size_t i = static_cast<size_t>(g.idx(x, y)) * 3;
        px[i] = static_cast<uint8_t>(r);
        px[i + 1] = static_cast<uint8_t>(gg);
        px[i + 2] = static_cast<uint8_t>(b);
    };
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            size_t i = static_cast<size_t>(g.idx(x, y));
            if (g.solid[i]) put(x, y, 96, 100, 116);
            else if (g.deadly[i]) put(x, y, 150, 30, 50);
            else if (g.platform[i]) put(x, y, 170, 150, 70);
            else if (m.reach[i]) put(x, y, 18, 64, 52);
            else put(x, y, 10, 12, 24);
        }
    auto toCell = [&](Vec2 p, int& x, int& y) {
        x = static_cast<int>(std::floor((p.x - g.ox) / kCell));
        y = static_cast<int>(std::floor((p.y - g.oy - 0.01f) / kCell));
    };
    for (const auto& p : a.intendedPath) {
        int x, y;
        toCell(p, x, y);
        put(x, y, 70, 200, 255);
    }
    for (const auto& b : a.doorBypasses)
        for (const auto& p : b.route) {
            int x, y;
            toCell(p, x, y);
            put(x, y, 255, 70, 230);
        }
    for (const auto& p : a.route) {
        int x, y;
        toCell(p, x, y);
        put(x, y, 255, 214, 60);
    }
    auto square = [&](Vec2 p, int r, int cr, int cg, int cb) {
        int x, y;
        toCell(p, x, y);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) put(x + dx, y + dy, cr, cg, cb);
    };
    for (const auto& cp : a.checkpoints) square(cp.pos, 3, cp.skipped ? 255 : 240, cp.skipped ? 50 : 240, cp.skipped ? 60 : 255);
    for (const auto& o : level.objects) {
        if (o.type == "player_start") square(o.rect.bottomCenter(), 3, 60, 255, 90);
        if (o.type == "exit") square(o.rect.bottomCenter(), 3, 255, 110, 255);
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", g.w, g.h);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace cosmic
