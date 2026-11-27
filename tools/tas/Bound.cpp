#include "tools/tas/Bound.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "tools/tas/Course.h"
#include "tools/tas/Sim.h"

namespace cosmic::tas {

namespace {

bool contains(const std::string& key, const char* word) {
    std::string k = key;
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return k.find(word) != std::string::npos;
}

}  // namespace

TimeBound::TimeBound(const Course& course, const GameWorld& w) : course_(course) {
    const auto& wps = course.waypoints();
    boxes_.resize(wps.size());
    for (size_t k = 0; k < wps.size(); ++k) {
        Box& b = boxes_[k];
        const LevelEntity* e = wps[k].objectId.empty() ? nullptr : const_cast<GameWorld&>(w).entity(wps[k].objectId);
        // Her box touches the waypoint's: her half width and a few pixels more either side.
        const float half = 0.5f * w.player().config().size.x + 4.0f;
        if (e) {
            b.known = true;
            b.left = e->box.x - half;
            b.right = e->box.right() + half;
        } else {
            b.known = true;
            b.left = wps[k].feet.x - 64.0f;
            b.right = wps[k].feet.x + 64.0f;
        }
    }
}

void TimeBound::observe(float maxSpeedX, float maxSpeed) {
    seenX_ = std::max(seenX_, maxSpeedX);
    seen_ = std::max(seen_, maxSpeed);
}

float TimeBound::toWaypoint(const GameWorld& w, int wp) const {
    const auto& wps = course_.waypoints();
    if (wp < 0 || wp >= static_cast<int>(wps.size())) return 0.0f;
    const Player& p = w.player();
    const PlayerConfig& c = p.config();
    // The kit's speeds: hers, and each speed of each power carried; what a teleport jumps at once.
    float vx = std::max({c.maxRunSpeed, c.slideSpeed, std::abs(c.wallJumpVelocity.x), std::abs(c.knockback.x), c.swimSpeed, std::abs(p.vel.x), seenX_});
    float vAny = std::max({c.fastFallSpeed, c.maxFallSpeed, c.jumpVelocity(), c.doubleJumpVelocity(), std::abs(c.wallJumpVelocity.y), std::abs(c.knockback.y),
                           p.vel.length(), seen_});
    float reach = 0.0f;
    for (const auto& pw : p.powers()) {
        const Json& params = pw->def().params;
        if (!params.is_object()) continue;
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (!it.value().is_number()) continue;
            const float v = std::abs(it.value().get<float>());
            if (contains(it.key(), "speed") || contains(it.key(), "velocity")) vx = std::max(vx, v);
            if (contains(it.key(), "distance") || contains(it.key(), "range")) reach += 2.0f * v;
        }
    }
    vAny = std::max(vAny, vx);
    vx *= kMargin;
    vAny *= kMargin;
    const Vec2 feet = p.feet();
    // Horizontally, to the waypoint's box.
    const Box& b = boxes_[static_cast<size_t>(wp)];
    const float gapX = feet.x < b.left ? b.left - feet.x : (feet.x > b.right ? feet.x - b.right : 0.0f);
    float touch = std::max(0.0f, gapX - reach) / vx;
    // Along the route, to the waypoint (its box a few cells around its feet).
    const float d = course_.distanceTo(wp, feet);
    if (std::isfinite(d)) touch = std::max(touch, std::max(0.0f, kFieldScale * d - 64.0f - reach) / vAny);
    float lb = touch;
    // A checkpoint also counts as passed when she is nearer to the next waypoint than it is.
    if (wps[static_cast<size_t>(wp)].kind == Waypoint::Kind::Checkpoint && wp + 1 < static_cast<int>(wps.size())) {
        const float dn = course_.distanceTo(wp + 1, feet);
        const float pass = std::isfinite(dn) ? std::max(0.0f, kFieldScale * (dn - (wps[static_cast<size_t>(wp) + 1].fromPrev - 24.0f)) - reach) / vAny : 0.0f;
        lb = std::min(lb, pass);
    }
    if (extra) lb = std::max(lb, extra(w, wp));
    return lb;
}

float Pruner::limit(size_t s, float startClock) const {
    if (s >= split.size() || split[s] < 0.0f) return 1.0e9f;
    const float late = s < startSplit.size() && startSplit[s] >= 0.0f ? std::max(0.0f, startClock - startSplit[s]) : 0.0f;
    return split[s] + late + slack;
}

bool Pruner::prune(const GameWorld& w, size_t s, int wp, float clock, float startClock) const {
    if (!active()) return false;
    const float lim = limit(s, startClock);
    if (lim >= 1.0e8f) return false;
    if (clock > lim) return true;
    return clock + bound->toWaypoint(w, wp) > lim;
}

Pruner Pruner::from(const TimeBound& b, const RunStudy& best, float slack) {
    Pruner p;
    p.bound = &b;
    p.slack = slack;
    if (!best.finishes()) return p;
    for (size_t s = 0; s < best.ends.size(); ++s) {
        p.split.push_back(best.splitAt(s));
        p.startSplit.push_back(s == 0 ? (best.clockAt.empty() ? 0.0f : best.clockAt[0]) : best.splitAt(s - 1));
    }
    return p;
}

}  // namespace cosmic::tas
