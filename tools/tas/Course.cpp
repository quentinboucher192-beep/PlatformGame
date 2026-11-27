#include "tools/tas/Course.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "engine/physics/PhysicsWorld.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelData.h"
#include "game/powers/Power.h"

namespace cosmic::tas {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

/// A distance that is never infinite (an unknown way: the straight line, made longer).
float finiteOr(float d, Vec2 a, Vec2 b) { return std::isfinite(d) ? d : 1.5f * (a - b).length() + 400.0f; }

uint64_t mix(uint64_t h, int64_t v) {
    h ^= static_cast<uint64_t>(v) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

bool isCollectible(const LevelObject& o) { return o.type == "shard" || o.type == "relic"; }

bool keptKind(const std::string& type) {
    return type == "door" || type == "lever" || type == "receptor" || type == "trigger" || type == "combat_zone" || type == "enemy";
}

}  // namespace

Course::Course(const LevelData& level, const ContentDatabase& content, RunCategory category)
    : category_(category), nav_(std::make_unique<LevelNavigator>(level, content)) {
    Vec2 start;
    Vec2 exitFeet;
    bool hasExit = false;
    std::string exitId;
    struct Cp {
        std::string id;
        Vec2 feet;
        LevelNavigator::Field field;
        float fromStart = 0.0f;
    };
    std::vector<Cp> cps;
    std::vector<Waypoint> items;
    for (const LevelObject& o : level.objects) {
        if (o.type == "player_start") start = o.rect.bottomCenter();
        else if (o.type == "exit") exitFeet = o.rect.bottomCenter(), hasExit = true, exitId = o.id;
        else if (o.type == "checkpoint" && !o.prop<bool>("optional", false)) cps.push_back({o.id, o.rect.bottomCenter(), {}, 0.0f});
        else if (category == RunCategory::HundredPercent && isCollectible(o)) {
            Waypoint w;
            w.kind = Waypoint::Kind::Collectible;
            w.objectId = o.id;
            w.feet = nav_->snap(o.rect.bottomCenter());
            items.push_back(std::move(w));
        }
        if (isCollectible(o)) ++totalCollectibles_;
    }
    // The checkpoints in the order of the route: by their distance from the start.
    for (Cp& c : cps) {
        c.field = nav_->fieldTo(c.feet);
        c.fromStart = finiteOr(nav_->distance(c.field, start), start, c.feet);
    }
    std::stable_sort(cps.begin(), cps.end(), [](const Cp& a, const Cp& b) { return a.fromStart < b.fromStart; });
    std::vector<Waypoint> anchors;  // the checkpoints, then the exit
    for (Cp& c : cps) {
        Waypoint w;
        w.kind = Waypoint::Kind::Checkpoint;
        w.objectId = c.id;
        w.feet = c.feet;
        w.field = std::move(c.field);
        anchors.push_back(std::move(w));
    }
    if (hasExit) {
        Waypoint w;
        w.kind = Waypoint::Kind::Exit;
        w.objectId = exitId;
        w.feet = exitFeet;
        w.field = nav_->fieldTo(exitFeet);
        anchors.push_back(std::move(w));
    }
    auto dist = [&](const Waypoint& to, Vec2 from) { return finiteOr(nav_->distance(to.field, from), from, to.feet); };

    // 100 %: each collectible in the section where it makes the shortest detour, then the order
    // within each section searched (nearest neighbour, then 2-opt on the level's own distances).
    std::vector<std::vector<Waypoint>> bySection(anchors.size());
    for (Waypoint& it : items) {
        it.field = nav_->fieldTo(it.feet);
        size_t best = 0;
        float bestDetour = kInf;
        Vec2 from = start;
        for (size_t s = 0; s < anchors.size(); ++s) {
            const float detour = dist(it, from) + dist(anchors[s], it.feet) - dist(anchors[s], from);
            if (detour < bestDetour) bestDetour = detour, best = s;
            from = anchors[s].feet;
        }
        bySection[best].push_back(std::move(it));
    }
    Vec2 from = start;
    for (size_t s = 0; s < anchors.size(); ++s) {
        std::vector<Waypoint>& list = bySection[s];
        std::vector<Waypoint> order;
        Vec2 cur = from;
        while (!list.empty()) {
            size_t pick = 0;
            float bestD = kInf;
            for (size_t i = 0; i < list.size(); ++i) {
                const float d = dist(list[i], cur);
                if (d < bestD) bestD = d, pick = i;
            }
            cur = list[pick].feet;
            order.push_back(std::move(list[pick]));
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(pick));
        }
        // 2-opt on the directed distances: from the section's start, through the order, to its end.
        auto cost = [&](const std::vector<Waypoint>& o) {
            float c = 0.0f;
            Vec2 p = from;
            for (const Waypoint& w : o) {
                c += dist(w, p);
                p = w.feet;
            }
            return c + dist(anchors[s], p);
        };
        float best = cost(order);
        for (bool improved = order.size() > 2; improved;) {
            improved = false;
            for (size_t i = 0; i + 1 < order.size(); ++i)
                for (size_t j = i + 1; j < order.size(); ++j) {
                    std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    const float c = cost(order);
                    if (c + 0.5f < best) {
                        best = c;
                        improved = true;
                    } else {
                        std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    }
                }
        }
        for (Waypoint& w : order) waypoints_.push_back(std::move(w));
        waypoints_.push_back(std::move(anchors[s]));
        sectionEnds_.push_back(static_cast<int>(waypoints_.size()) - 1);
        from = waypoints_.back().feet;
    }
    Vec2 prev = start;
    float total = 0.0f;
    for (Waypoint& w : waypoints_) {
        w.fromPrev = dist(w, prev);
        total += w.fromPrev;
        w.cumulative = total;
        prev = w.feet;
    }
}

Course::Bound Course::bind(GameWorld& w) const {
    Bound b;
    for (const Waypoint& wp : waypoints_) b.waypointEntity.push_back(wp.kind == Waypoint::Kind::Exit ? nullptr : w.entity(wp.objectId));
    for (auto& e : w.entities())
        if (keptKind(e->type())) b.kept.push_back(e.get());
    return b;
}

Progress Course::measure(GameWorld& w, const Bound& b, int fromWaypoint) const {
    Progress p;
    const Player& player = w.player();
    const Vec2 feet = player.feet();
    const RunStats& s = w.stats();
    p.finished = w.state() == WorldState::Outro || w.state() == WorldState::Completed;
    p.clock = w.timer().elapsed();
    p.feet = feet;
    p.state = static_cast<int>(player.state());
    p.deaths = s.deaths;
    p.damage = s.damageTaken;
    p.kills = s.kills;
    p.collectibles = s.collectibles();
    p.health = player.alive() ? player.health : 0;
    p.offer = w.powerOffer().has_value();
    p.powers = static_cast<int>(player.powers().size());
    p.rejected = w.state() == WorldState::Failed || (category_ == RunCategory::NoDeath && s.deaths > 0) ||
                 (category_ == RunCategory::NoDamage && (s.deaths > 0 || s.damageTaken > 0));

    // The waypoints reached: touched (a checkpoint lit, a collectible taken, the exit), or a
    // checkpoint passed by (closer to the next waypoint than it is itself).
    const int n = static_cast<int>(waypoints_.size());
    int k = std::clamp(fromWaypoint, 0, n);
    while (k < n) {
        const Waypoint& wp = waypoints_[static_cast<size_t>(k)];
        const LevelEntity* e = b.waypointEntity[static_cast<size_t>(k)];
        bool reached = false;
        if (wp.kind == Waypoint::Kind::Exit) reached = p.finished;
        else if (wp.kind == Waypoint::Kind::Collectible) reached = !e || e->removed;
        else {
            const auto* cp = dynamic_cast<const CheckpointEntity*>(e);
            reached = !cp || cp->activated();
            if (!reached && k + 1 < n && player.alive()) {
                const Waypoint& next = waypoints_[static_cast<size_t>(k + 1)];
                const float d = nav_->distance(next.field, feet);
                reached = std::isfinite(d) && d + 24.0f < next.fromPrev;
            }
        }
        if (!reached) break;
        ++k;
    }
    p.waypoint = k;
    if (k >= n) {
        p.potential = length();
    } else {
        const Waypoint& wp = waypoints_[static_cast<size_t>(k)];
        const float d = player.alive() ? nav_->distance(wp.field, feet) : kInf;
        const float base = wp.cumulative - wp.fromPrev;
        p.potential = std::isfinite(d) ? std::max(base - 200.0f, wp.cumulative - d) : base - 300.0f;
    }

    // An arena being fought holds the way on (its doors are closed until its waves are beaten):
    // there, the progress is measured from the arena's centre, and what counts is beating its
    // enemies (those left, their wounds) and going to them.
    // Every enemy an arena still asks for counts against a state the same way, whether its fight
    // has started or not (entering an arena costs nothing; beating its enemies is what counts), and
    // starting an arena is a step on the way.
    float seek = kInf;
    int left = 0, started = 0;
    for (LevelEntity* e : b.kept) {
        auto* zone = dynamic_cast<CombatZoneEntity*>(e);
        if (!zone || zone->cleared()) continue;
        if (!zone->started()) {
            left += static_cast<int>(zone->enemyCount());
            continue;
        }
        ++started;
        left += zone->enemiesLeft(w);
        if (!zone->fighting()) continue;
        p.fighting = true;
        if (k < n) {
            const Waypoint& wp = waypoints_[static_cast<size_t>(k)];
            const float d = nav_->distance(wp.field, nav_->snap(zone->box.center()));
            if (std::isfinite(d)) p.potential = std::min(p.potential, wp.cumulative - d);
        }
        float nearest = kInf;
        for (const Enemy* en : zone->fightingEnemies(w)) nearest = std::min(nearest, (en->box.center() - player.box.center()).length());
        seek = std::min(seek, nearest);
    }
    p.enemiesLeft = left;

    // What the world did on the way.
    for (LevelEntity* e : b.kept)
        if (!e->keepState(w).is_null()) ++p.milestones;
    for (const Collider& c : w.physics().colliders())
        if ((c.tags & ColliderTag::Gate) && !c.enabled && (c.tags & ColliderTag::Breakable || !(c.tags & (ColliderTag::Phase | ColliderTag::ShieldPass))))
            ++p.milestones;
    for (const auto& pw : player.powers()) {
        uint64_t ph = 1469598103934665603ull;
        for (char ch : pw->id()) ph = (ph ^ static_cast<uint8_t>(ch)) * 1099511628211ull;
        p.powerSet = mix(p.powerSet, static_cast<int64_t>(ph));
    }
    for (const auto& e : w.enemies()) {
        if (!e->def().isBoss) {
            if (e->alive() && !e->dormant()) p.wounds += 1.0f - e->hpRatio();
            continue;
        }
        if (!e->alive()) p.bosses += static_cast<float>(e->def().phases.size()) + 1.0f;
        else if (!e->dormant()) p.bosses += static_cast<float>(e->phaseIndex()) + (1.0f - e->hpRatio());
    }

    p.score = p.potential + 250.0f * static_cast<float>(p.milestones) + 120.0f * static_cast<float>(p.powers) + 400.0f * p.bosses + 100.0f * p.wounds +
              20.0f * static_cast<float>(p.kills) + 30.0f * static_cast<float>(p.health);
    p.score += 250.0f * static_cast<float>(started) - 300.0f * static_cast<float>(left);
    if (p.fighting && std::isfinite(seek)) p.score += 0.5f * std::max(0.0f, 400.0f - seek);
    if (category_ == RunCategory::HundredPercent) p.score += 150.0f * static_cast<float>(p.collectibles);
    if (p.finished) p.score += 1.0e7f - p.clock * 1000.0f;

    uint64_t h = 0x1234567ull;
    h = mix(h, static_cast<int64_t>(std::floor(feet.x / 12.0f)));
    h = mix(h, static_cast<int64_t>(std::floor(feet.y / 12.0f)));
    h = mix(h, k);
    h = mix(h, p.milestones);
    h = mix(h, p.powers);
    h = mix(h, p.health);
    h = mix(h, static_cast<int64_t>(p.bosses * 8.0f));
    h = mix(h, p.offer ? 1 : 0);
    h = mix(h, p.collectibles);
    h = mix(h, static_cast<int64_t>(p.powerSet));
    p.cell = h;
    return p;
}

}  // namespace cosmic::tas
