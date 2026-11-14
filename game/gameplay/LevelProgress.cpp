#include "game/gameplay/LevelProgress.h"

#include <algorithm>
#include <set>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/levels/LevelData.h"

namespace cosmic {

namespace {
size_t nearestIndex(const std::vector<Vec2>& route, Vec2 p, size_t from, size_t to) {
    size_t best = from;
    float bestD = 1e30f;
    for (size_t i = from; i < to; ++i) {
        float d = (route[i] - p).lengthSq();
        if (d < bestD) bestD = d, best = i;
    }
    return best;
}
}  // namespace

float LevelProgressTrack::fractionAt(Vec2 p) const {
    if (!valid()) return 0.0f;
    size_t i = nearestIndex(route, p, 0, route.size());
    return cumulative[i] / length;
}

float LevelProgressTrack::project(Vec2 feet, size_t& hint) const {
    if (!valid()) return 0.0f;
    hint = std::min(hint, route.size() - 1);
    // Local search first (about 1500 px of route around the last position).
    size_t lo = hint > 190 ? hint - 190 : 0, hi = std::min(route.size(), hint + 190);
    size_t i = nearestIndex(route, feet, lo, hi);
    if ((route[i] - feet).lengthSq() > 160.0f * 160.0f) {
        // Far from the route here (respawn, teleport, detour): search everywhere.
        size_t g = nearestIndex(route, feet, 0, route.size());
        if ((route[g] - feet).lengthSq() < (route[i] - feet).lengthSq()) i = g;
    }
    hint = i;
    return cumulative[i] / length;
}

LevelProgressTrack buildProgressTrack(const LevelData& level, const ContentDatabase& content) {
    LevelProgressTrack t;
    t.route = intendedRoute(level, content);
    if (t.route.size() < 2) return t;
    t.cumulative.resize(t.route.size());
    t.cumulative[0] = 0.0f;
    for (size_t i = 1; i < t.route.size(); ++i) t.cumulative[i] = t.cumulative[i - 1] + (t.route[i] - t.route[i - 1]).length();
    t.length = t.cumulative.back();
    if (!t.valid()) return t;
    using Kind = LevelProgressTrack::Marker::Kind;
    // A boss's own arena is shown by the boss's badge only (not by a skull as well).
    auto isBoss = [&](const LevelObject& e) {
        if (e.type != "enemy") return false;
        const EnemyDefinition* d = content.enemy(e.propStr("enemy"));
        return d && d->isBoss;
    };
    auto bossArena = [&](const LevelObject& zone) {
        std::string waves = zone.propStr("waves");
        std::set<std::string> ids;
        std::string cur;
        for (char c : waves + ",") {
            if (c == ',' || c == ';') {
                if (!cur.empty()) ids.insert(cur);
                cur.clear();
            } else if (c != ' ') {
                cur += c;
            }
        }
        for (const auto& e : level.objects)
            if (isBoss(e) && (ids.empty() ? zone.rect.contains(e.rect.center()) : ids.count(e.id) > 0)) return true;
        return false;
    };
    for (const auto& o : level.objects) {
        LevelProgressTrack::Marker m;
        if (o.type == "checkpoint" && !o.prop<bool>("optional", false)) {
            m.kind = Kind::Checkpoint;
            m.label = o.propStr("label");
            m.at = t.fractionAt(o.rect.bottomCenter());
        } else if (o.type == "combat_zone") {
            if (bossArena(o)) continue;
            m.kind = Kind::Ambush;
            std::string waves = o.propStr("waves");
            int n = waves.empty() ? 1 : 1 + static_cast<int>(std::count(waves.begin(), waves.end(), ';'));
            m.label = n > 1 ? std::to_string(n) + " vagues" : "embuscade";
            const PlanetData* planet = content.planet(level.planet);
            Color fallback = planet ? planet->color : Color{255, 70, 96, 255};
            std::string hex = o.propStr("skullColor");
            m.color = hex.empty() ? fallback : Color::fromHex(hex, fallback);
            m.color.a = 255;
            m.at = t.fractionAt(o.rect.center());
        } else if (o.type == "enemy") {
            const EnemyDefinition* d = content.enemy(o.propStr("enemy"));
            if (!d || !d->isBoss) continue;
            m.kind = Kind::Boss;
            m.label = d->name;
            m.enemyId = d->id;
            m.at = t.fractionAt(o.rect.bottomCenter());
        } else {
            continue;
        }
        m.objectId = o.id;
        t.markers.push_back(m);
    }
    std::stable_sort(t.markers.begin(), t.markers.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    return t;
}

}  // namespace cosmic
