#include "game/ghost/SobPath.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/ghost/KeptReplays.h"
#include "game/ghost/StartPosStore.h"
#include "game/player/Player.h"

namespace cosmic {

namespace {

/// A sample where Nova can be given to the player: in play (not dead, appearing, hidden, at the exit).
bool playable(const ReplayFrame& f) {
    const auto s = static_cast<PlayerState>(f.state);
    return s != PlayerState::Dead && s != PlayerState::Respawn && s != PlayerState::Hidden && s != PlayerState::Victory;
}

float sampleTime(const ReplayData& r, size_t k) { return static_cast<float>(k) / static_cast<float>(std::max<uint16_t>(1, r.sampleHz)); }

/// A replay that can start an attempt now: exact, of this game's simulation, one player.
bool usable(const ReplayData& r) { return r.exact() && r.team.empty() && r.simVersion == static_cast<uint16_t>(kSimulationVersion); }

}  // namespace

SobPathRequest sobPathRequest(const PersonalBests& bests, const std::string& levelKey) {
    SobPathRequest req;
    int k = 0;
    for (const PersonalBests::Segment& s : bests.segments(levelKey)) {
        SobPathRequest::Part p;
        p.range = {s.from, s.to};
        p.route = k++;
        if (s.record && s.record->replay.has()) {
            p.file = bests.replayPath(levelKey, s.record->replay.file);
            p.replay = s.record->replay;
            p.source = s.record->source;
        }
        req.signature += s.from + ">" + s.to + "=" + (p.file.empty() ? std::string("-") : s.record->replay.file) + "@" +
                         std::to_string(std::lround(p.replay.start * 1000.0f)) + ";";
        req.parts.push_back(std::move(p));
    }
    return req;
}

SobPath buildSobPath(const SobPathRequest& req) {
    SobPath path;
    path.signature = req.signature;
    for (const SobPathRequest::Part& p : req.parts) {
        std::optional<ReplayData> rd;
        if (!p.file.empty() && keptreplays::fileSize(p.file) > 0 && keptreplays::fileSize(p.file) <= keptreplays::kMaxFileBytes)
            if (auto bytes = FileUtil::readBinary(p.file)) rd = ReplayData::decode(*bytes);
        if (!rd || !usable(*rd)) {
            path.missing.push_back(p.range);
            continue;
        }
        SobPathSegment s;
        s.range = p.range;
        s.route = p.route;
        s.file = p.file;
        s.replay = p.replay;
        s.source = p.source;
        for (size_t k = 0; k < rd->frames.size(); ++k) {
            const float t = sampleTime(*rd, k);
            if (t + 1e-4f < p.replay.start || t - 1e-4f > p.replay.end || !playable(rd->frames[k])) continue;
            s.points.push_back({rd->frames[k].x, rd->frames[k].y});
            s.times.push_back(t);
        }
        if (s.points.empty()) path.missing.push_back(p.range);
        else path.segments.push_back(std::move(s));
    }
    path.ready = true;
    return path;
}

SobPathCache::~SobPathCache() {
    if (future_.valid()) future_.wait();  // its replays are read in a moment
}

void SobPathCache::update(const PersonalBests& bests, const std::string& levelKey) {
    changed_ = false;
    if (future_.valid() && future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        path_ = std::make_unique<SobPath>(future_.get());
        changed_ = true;
    }
    if (levelKey.empty() || future_.valid()) return;
    SobPathRequest req = sobPathRequest(bests, levelKey);
    if (levelKey == key_ && req.signature == wanted_) return;  // the same bests: the same path
    key_ = levelKey;
    wanted_ = req.signature;
    future_ = std::async(std::launch::async, [r = std::move(req)] { return buildSobPath(r); });
}

int reanchorStartPos(StartPosStore& store, const std::string& levelKey, const SobPath& path) {
    int n = 0;
    for (const StartPosEntry& e : store.list(levelKey)) {
        if (e.onPath) continue;
        const auto pt = path.nearest(e.state.feet, kSobPathSnap);
        if (!pt) continue;  // far from it: « à replacer »
        const SobPathSegment& s = path.segments[static_cast<size_t>(pt->segment)];
        if (store.placeOnPath(levelKey, e.id, pt->at, s.range.first, s.range.second, e.at, kSobPathSame)) ++n;
    }
    return n;
}

std::optional<SobPathPoint> SobPath::nearest(Vec2 p, float radius) const {
    std::optional<SobPathPoint> out;
    float best = radius * radius;
    for (size_t s = 0; s < segments.size(); ++s)
        for (size_t i = 0; i < segments[s].points.size(); ++i)
            if (const float d = (segments[s].points[i] - p).lengthSq(); d < best || (!out && d <= best)) {
                best = d;
                out = SobPathPoint{static_cast<int>(s), static_cast<int>(i), segments[s].points[i]};
            }
    return out;
}

std::optional<SobPathPoint> SobPath::nearestIn(const PersonalBests::Range& r, Vec2 p, float radius) const {
    std::optional<SobPathPoint> out;
    float best = radius * radius;
    for (size_t s = 0; s < segments.size(); ++s) {
        if (segments[s].range != r) continue;
        for (size_t i = 0; i < segments[s].points.size(); ++i)
            if (const float d = (segments[s].points[i] - p).lengthSq(); d < best || (!out && d <= best)) {
                best = d;
                out = SobPathPoint{static_cast<int>(s), static_cast<int>(i), segments[s].points[i]};
            }
    }
    return out;
}

std::optional<SobPathPoint> SobPath::step(const SobPathPoint& p, int delta) const {
    if (p.segment < 0 || p.segment >= static_cast<int>(segments.size()) || delta == 0) return std::nullopt;
    int s = p.segment, i = p.index + (delta > 0 ? 1 : -1);
    if (i < 0) {
        if (--s < 0) return std::nullopt;
        i = static_cast<int>(segments[static_cast<size_t>(s)].points.size()) - 1;
    } else if (i >= static_cast<int>(segments[static_cast<size_t>(s)].points.size())) {
        if (++s >= static_cast<int>(segments.size())) return std::nullopt;
        i = 0;
    }
    if (i < 0) return std::nullopt;
    return SobPathPoint{s, i, segments[static_cast<size_t>(s)].points[static_cast<size_t>(i)]};
}

const SobPathSegment* SobPath::segment(const PersonalBests::Range& r) const {
    for (const SobPathSegment& s : segments)
        if (s.range == r) return &s;
    return nullptr;
}

std::optional<SobAnchor> sobAnchor(const ReplayData& replay, const BestReplay& best, Vec2 feet) {
    if (!usable(replay)) return std::nullopt;
    std::optional<SobAnchor> out;
    float bd = 0.0f;
    for (size_t k = 0; k < replay.frames.size(); ++k) {
        const float t = sampleTime(replay, k);
        if (t + 1e-4f < best.start || t - 1e-4f > best.end || !playable(replay.frames[k])) continue;
        const Vec2 at{replay.frames[k].x, replay.frames[k].y};
        const float d = (at - feet).lengthSq();
        if (!out || d < bd) {  // a tie keeps the earliest
            bd = d;
            out = SobAnchor{t, at, static_cast<int>(k)};
        }
    }
    return out;
}

// ------------------------------------------------------------------ the preroll

SobPreroll::SobPreroll(std::shared_ptr<const ReplayData> replay, GameWorld& world, const std::function<void(GameWorld&)>& setup, float chrono)
    : replay_(std::move(replay)), world_(world), chrono_(chrono) {
    // The replay's order (ReplayPlayback): its simulator (its seed, keys, rules), then its start.
    sim_ = std::make_unique<ReplaySimulator>(*replay_, world_);
    if (setup) setup(world_);
}

SobPreroll::~SobPreroll() = default;

bool SobPreroll::advance(int budget, const std::function<void(const InputFrame&)>& each) {
    for (int n = 0; n < budget && !arrived_ && !failed_; ++n) {
        if (!sim_->step()) {
            failed_ = true;
            break;
        }
        if (each)
            if (const InputFrame* f = sim_->currentInput()) each(*f);
        // The step its trace was sampled at (ReplayRecorder: the first step, the chronometer
        // running, whose time reaches the sample's).
        const LevelTimer& t = world_.timer();
        if ((t.running() || t.elapsed() > 0.0f) && t.elapsed() + 1e-4f >= chrono_) arrived_ = true;
    }
    return arrived_ || failed_;
}

float SobPreroll::progress() const {
    if (arrived_) return 1.0f;
    return chrono_ > 0.0f ? std::clamp(world_.timer().elapsed() / chrono_, 0.0f, 1.0f) : 0.0f;
}

}  // namespace cosmic
