#include "tools/tas/Sim.h"

#include <algorithm>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelData.h"
#include "game/player/Player.h"

namespace cosmic::tas {

std::vector<size_t> RunStudy::nearest(Vec2 f, Vec2 v, size_t center, size_t radius, int k) const {
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

size_t RunStudy::frameAtPotential(float p) const {
    for (size_t i = 0; i < potential.size() && i <= exitAt; ++i)
        if (potential[i] >= p) return i;
    return exitAt;
}

void repressRun(std::vector<InputFrame>& in, size_t lo, size_t hi) {
    uint32_t prev = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        InputFrame& f = in[i];
        f.pressed = f.held & ~prev;
        f.released = prev & ~f.held;
        prev = f.held;
        if (i < lo || i > hi) continue;
        const bool r = f.held & keyBit(Action::MoveRight), l = f.held & keyBit(Action::MoveLeft);
        const bool d = f.held & keyBit(Action::MoveDown), u = f.held & keyBit(Action::MoveUp);
        f.moveX = static_cast<int8_t>(r == l ? 0 : (r ? 127 : -127));
        f.moveY = static_cast<int8_t>(d == u ? 0 : (d ? 127 : -127));
    }
}

Sim::Sim(const ContentDatabase& content, const LevelData& level, const Course& course, RunCategory category)
    : course_(course), category_(category), world_(content, level) {
    bound_ = course.bind(const_cast<GameWorld&>(world_.world()));
    const auto& wps = course.waypoints();
    for (size_t k = 0; k < wps.size(); ++k)
        if (wps[k].kind == Waypoint::Kind::Checkpoint && cps_.size() < 32) cps_.push_back(dynamic_cast<const CheckpointEntity*>(bound_.waypointEntity[k]));
    world_.save(origin_);
}

Sim::~Sim() = default;

Progress Sim::measure(int fromWaypoint) { return course_.measure(const_cast<GameWorld&>(world_.world()), bound_, fromWaypoint); }

bool Sim::broken(int deaths) const {
    const GameWorld& w = world_.world();
    const RunStats& s = w.stats();
    return s.deaths > deaths || (category_ == RunCategory::NoDeath && s.deaths > 0) ||
           (category_ == RunCategory::NoDamage && (s.deaths > 0 || s.damageTaken > 0)) || w.state() == WorldState::Failed;
}

bool Sim::atExit(bool* refused) const {
    const GameWorld& w = world_.world();
    if (refused) *refused = false;
    if (w.state() != WorldState::Outro && w.state() != WorldState::Completed) return false;
    if (category_ == RunCategory::HundredPercent && w.stats().collectibles() < w.stats().totalCollectibles()) {
        if (refused) *refused = true;
        return false;
    }
    return true;
}

uint32_t Sim::litCheckpoints() const {
    uint32_t lit = 0;
    for (size_t k = 0; k < cps_.size(); ++k)
        if (cps_[k] && cps_[k]->activated()) lit |= 1u << k;
    return lit;
}

RunStudy Sim::study(const std::vector<InputFrame>& frames) {
    RunStudy g;
    g.frames = frames;
    world_.restore(origin_);
    const auto& ends = course_.sectionEnds();
    int wp = 0;
    auto keep = [&] {
        const Player& p = world_.world().player();
        g.feet.push_back(p.feet());
        g.vel.push_back(p.vel);
        g.clockAt.push_back(world_.world().timer().elapsed());
        const Progress pr = measure(wp);
        wp = pr.waypoint;
        g.potential.push_back(g.potential.empty() ? pr.potential : std::max(g.potential.back(), pr.potential));
        if (p.alive()) {
            g.maxSpeedX = std::max(g.maxSpeedX, std::abs(p.vel.x));
            g.maxSpeed = std::max(g.maxSpeed, p.vel.length());
        }
        while (g.ends.size() < ends.size() && pr.waypoint > ends[g.ends.size()]) g.ends.push_back(g.feet.size() - 1);
    };
    keep();
    g.exitAt = frames.size();
    g.frozen.assign(frames.size(), 0);
    // The category's rules as the records read them (a death is allowed in ANY%).
    constexpr int kAnyDeaths = 1 << 30;
    bool broke = false;
    for (size_t i = 0; i < frames.size(); ++i) {
        g.frozen[i] = world_.world().cinematic().active() || frames[i].flags != 0 || frames[i].choice != 0;
        world_.step(frames[i]);
        keep();
        broke = broke || broken(kAnyDeaths);
        const WorldState st = world_.world().state();
        if (st == WorldState::Outro || st == WorldState::Completed) {
            g.exitAt = i + 1;
            break;
        }
    }
    if (!broke && atExit()) g.clock = world_.world().timer().elapsed();
    world_.restore(origin_);
    return g;
}

float Sim::playToExit(const ArenaWorld::State& at, const std::vector<InputFrame>& frames, size_t from, float limit, size_t* played, uint32_t held) {
    world_.restore(at);
    const int deaths = world_.world().stats().deaths;
    size_t n = 0;
    float out = -1.0f;
    for (size_t i = from; i < frames.size(); ++i) {
        InputFrame f = frames[i];
        if (i == from && held != kSame) {
            f.pressed = f.held & ~held;
            f.released = held & ~f.held;
        }
        world_.step(f);
        ++n;
        if (broken(deaths)) break;
        bool refused = false;
        if (atExit(&refused)) {
            out = world_.world().timer().elapsed();
            break;
        }
        if (refused || world_.world().timer().elapsed() > limit) break;
    }
    if (played) *played = n;
    return out;
}

JoinTry Sim::rejoin(const RunStudy& g, size_t center, uint32_t held, float limit) {
    JoinTry r;
    if (!g.finishes()) return r;
    if (!tmp_) tmp_ = std::make_unique<ArenaWorld::State>();
    world_.save(*tmp_);
    if (atExit()) {
        r.ok = true;
        r.finish = world_.world().timer().elapsed();
        r.seedAt = g.frames.size();
        return r;
    }
    const Player& p = world_.world().player();
    std::vector<size_t> js = g.nearest(p.feet(), p.vel, center, 120, 3);
    for (int d : {0, -2, 2, -1, 1}) {
        const auto j = static_cast<std::ptrdiff_t>(center) + d;
        if (j > 0 && static_cast<size_t>(j) < g.exitAt && std::find(js.begin(), js.end(), static_cast<size_t>(j)) == js.end()) js.push_back(static_cast<size_t>(j));
    }
    for (size_t j : js) {
        size_t played = 0;
        const float c = playToExit(*tmp_, g.frames, j, r.ok ? std::min(limit, r.finish) : limit, &played, held);
        ++r.tried;
        if (c >= 0.0f && (!r.ok || c < r.finish)) {
            r.ok = true;
            r.finish = c;
            r.seedAt = j;
            r.played = played;
        }
    }
    world_.restore(*tmp_);
    return r;
}

void Sim::buildLadder(const std::vector<InputFrame>& run, size_t rung) {
    rung_ = std::max<size_t>(1, rung);
    for (auto& s : ladder_) give(std::move(s));
    ladder_.clear();
    world_.restore(origin_);
    for (size_t i = 0; i <= run.size(); ++i) {
        if (i % rung_ == 0) {
            auto s = take();
            world_.save(*s);
            ladder_.push_back(std::move(s));
        }
        if (i < run.size()) world_.step(run[i]);
    }
}

const ArenaWorld::State& Sim::rungBefore(size_t frame, size_t& at) const {
    if (ladder_.empty()) {
        at = 0;
        return origin_;
    }
    const size_t k = std::min(ladder_.size() - 1, frame / rung_);
    at = k * rung_;
    return *ladder_[k];
}

std::unique_ptr<ArenaWorld::State> Sim::take() {
    if (pool_.empty()) return std::make_unique<ArenaWorld::State>();
    auto s = std::move(pool_.back());
    pool_.pop_back();
    return s;
}

void Sim::give(std::unique_ptr<ArenaWorld::State> s) {
    if (s && pool_.size() < 64) pool_.push_back(std::move(s));
}

}  // namespace cosmic::tas
