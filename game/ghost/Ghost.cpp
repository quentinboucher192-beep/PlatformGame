#include "game/ghost/Ghost.h"

#include <algorithm>
#include <cmath>

#include "game/gameplay/LevelProgress.h"
#include "game/player/Player.h"

namespace cosmic {

// ---------------------------------------------------------------- meta

Json GhostMeta::toJson() const {
    return Json{{"format", "cosmic.ghost"},
                {"source", source},
                {"label", label},
                {"player_id", playerId},
                {"level_id", levelId},
                {"level_hash", levelHash},
                {"record_id", recordId},
                {"category", category},
                {"level_version", levelVersion},
                {"time_ms", timeMs},
                {"date", date},
                {"splits", splits},
                {"validated", validated},
                {"community", community},
                {"game_version", gameVersion}};
}

GhostMeta GhostMeta::fromJson(const Json& j) {
    GhostMeta m;
    // Every field is checked: a sidecar file is user data (it may have been edited).
    auto clip = [](std::string s, size_t n) {
        if (s.size() > n) s.resize(n);
        return s;
    };
    m.source = clip(jstr(j, "source"), 16);
    m.label = clip(jstr(j, "label"), 48);
    m.playerId = clip(jstr(j, "player_id"), 64);
    m.levelId = clip(jstr(j, "level_id"), 128);
    m.levelHash = clip(jstr(j, "level_hash"), 64);
    m.recordId = clip(jstr(j, "record_id"), 64);
    m.category = clip(jstr(j, "category"), 32);
    m.levelVersion = std::clamp(jget<int>(j, "level_version", 0), 0, 1000000);
    m.timeMs = std::clamp<int64_t>(jget<int64_t>(j, "time_ms", 0), 0, 7200000);
    m.date = clip(jstr(j, "date"), 32);
    if (j.is_object() && j.contains("splits") && j["splits"].is_array()) {
        for (const Json& s : j["splits"]) {
            if (!s.is_number() || m.splits.size() >= 255) break;
            float v = s.get<float>();
            if (!std::isfinite(v) || v < 0.0f || v > 7200.0f) break;
            m.splits.push_back(v);
        }
    }
    m.validated = jget<bool>(j, "validated", false);
    m.community = jget<bool>(j, "community", false);
    m.gameVersion = clip(jstr(j, "game_version"), 24);
    return m;
}

std::string GhostMeta::sourceLabel() const {
    if (source == "pb") return "Mon meilleur temps";
    if (source == "prev") return "Mon record précédent";
    if (source == "last") return "Ma dernière course";
    if (source == "world") return "Record mondial";
    if (source == "friend") return "Record d'un ami";
    if (source == "record") return "Record du classement";
    if (source == "favorite") return "Replay favori";
    if (source == "file") return "Replay choisi";
    return "Fantôme";
}

// ---------------------------------------------------------------- checks

namespace {

/// Frames where the player may move instantly: a death, a respawn or a teleport (within a
/// sample or two). Sorted.
std::vector<uint32_t> cutFrames(const ReplayData& r) {
    std::vector<uint32_t> cuts;
    for (const ReplayEvent& e : r.events)
        if (e.type == ReplayEventType::Death || e.type == ReplayEventType::Respawn || e.type == ReplayEventType::Teleport)
            cuts.push_back(e.frame);
    std::sort(cuts.begin(), cuts.end());
    return cuts;
}

bool nearCut(const std::vector<uint32_t>& cuts, uint32_t frame) {
    auto it = std::lower_bound(cuts.begin(), cuts.end(), frame > 2 ? frame - 2 : 0u);
    return it != cuts.end() && *it <= frame + 2;
}

}  // namespace

GhostCheck checkGhostReplay(const ReplayData& r, const std::string& expectedLevelHash, const Rect& b) {
    GhostCheck c;
    if (r.frames.size() < 2 || r.sampleHz < 5 || r.sampleHz > 240) {
        c.reason = "Trajectoire absente ou illisible.";
        return c;
    }
    if (r.durationMs == 0 || r.durationMs > ReplayData::kMaxTicks / 120u * 1000u) {
        c.reason = "Durée impossible.";
        return c;
    }
    if (!expectedLevelHash.empty() && r.levelHash != expectedLevelHash) {
        c.reason = "Autre version du niveau.";
        return c;
    }
    // Frames are sampled on the level timer: their number follows the duration.
    double expected = static_cast<double>(r.durationMs) / 1000.0 * r.sampleHz;
    double n = static_cast<double>(r.frames.size());
    if (n < expected * 0.97 - 3.0 || n > expected * 1.03 + 3.0) {
        c.reason = "Durée et trajectoire incohérentes.";
        return c;
    }
    bool finished = false;
    for (const ReplayEvent& e : r.events) {
        if (e.frame >= r.frames.size()) {
            c.reason = "Événement hors de la trajectoire.";
            return c;
        }
        if (e.type == ReplayEventType::Finish) finished = true;
    }
    if (!finished) {
        c.reason = "Course non terminée.";
        return c;
    }
    // Positions: inside the level (with a margin for falls), no jump faster than any move
    // of the game except deaths, respawns and teleports.
    Rect area{b.x - 600.0f, b.y - 1200.0f, b.w + 1200.0f, b.h + 2400.0f};
    const float maxStep = 4200.0f / static_cast<float>(r.sampleHz);
    const std::vector<uint32_t> cuts = cutFrames(r);
    int unexplained = 0;
    for (size_t i = 0; i < r.frames.size(); ++i) {
        const ReplayFrame& f = r.frames[i];
        if (!std::isfinite(f.x) || !std::isfinite(f.y) || !area.contains({f.x, f.y})) {
            c.reason = "Position hors du niveau.";
            return c;
        }
        if (i == 0) continue;
        const ReplayFrame& p = r.frames[i - 1];
        if (std::hypot(f.x - p.x, f.y - p.y) > maxStep && !nearCut(cuts, static_cast<uint32_t>(i))) {
            // A few are tolerated (level teleporters, the last sample taken off the grid).
            if (++unexplained > 3) {
                c.reason = "Déplacements impossibles.";
                return c;
            }
        }
    }
    // Splits: in order, inside the run.
    int64_t last = -1;
    for (const ReplayEvent& e : r.events) {
        if (e.type != ReplayEventType::Split) continue;
        int64_t t = r.splitTimeMs(e);
        if (t < last || t > static_cast<int64_t>(r.durationMs) + 50) {
            c.reason = "Temps de passage incohérents.";
            return c;
        }
        last = t;
    }
    c.ok = true;
    return c;
}

const char* ghostClipFor(const ReplayFrame& f, Vec2 vel) {
    switch (static_cast<PlayerState>(f.state)) {
        case PlayerState::Dead: return "death";
        case PlayerState::Respawn: return "respawn";
        case PlayerState::Victory: return "victory";
        case PlayerState::Hurt: return "hurt";
        case PlayerState::Dash: return "dash";
        case PlayerState::Slide: return "slide";
        case PlayerState::Climb: return "climb";
        case PlayerState::WallSlide: return "wall_slide";
        case PlayerState::Crouch: return "crouch";
        default: break;
    }
    if (f.flags & ReplayFrame::Grounded) {
        float s = std::abs(vel.x);
        return s > 93.0f ? "run" : s > 8.0f ? "walk" : "idle";
    }
    if (f.flags & ReplayFrame::Gliding) return "glide";
    return vel.y < -30.0f ? "jump" : "fall";
}

// ---------------------------------------------------------------- track

void GhostTrack::load(const ReplayData& r) {
    frames_ = r.frames;
    hz_ = static_cast<float>(std::max<uint16_t>(1, r.sampleHz));
    duration_ = static_cast<float>(r.durationMs) / 1000.0f;
    cuts_ = cutFrames(r);
    splits_.clear();
    splitsExact_ = false;
    for (const ReplayEvent& e : r.events)
        if (e.type == ReplayEventType::Split) splits_.push_back(static_cast<float>(r.splitTimeMs(e)) / 1000.0f);
    if (!splits_.empty()) {
        splitsExact_ = true;
    } else {
        // Older replays: the checkpoint event belongs to the sample after it (about 1/60 s
        // late on average).
        for (const ReplayEvent& e : r.events)
            if (e.type == ReplayEventType::Checkpoint)
                splits_.push_back(std::max(0.0f, (static_cast<float>(e.frame) - 0.5f) / hz_));
    }
    progressTimes_.clear();
}

void GhostTrack::mapProgress(const LevelProgressTrack& track) {
    progressTimes_.clear();
    if (!track.valid() || frames_.empty()) return;
    progressTimes_.assign(kProgressSteps + 1, -1.0f);
    size_t hint = 0;
    int reached = -1;
    for (size_t i = 0; i < frames_.size(); ++i) {
        Vec2 feet{frames_[i].x, frames_[i].y};
        float p = track.project(feet, hint);
        // Only points on the route count (a fall below it would match a far part).
        if ((track.route[hint] - feet).lengthSq() > 200.0f * 200.0f) continue;
        int k = std::clamp(static_cast<int>(p * kProgressSteps), 0, kProgressSteps);
        float t = std::min(duration_, static_cast<float>(i) / hz_);
        for (; reached < k; ++reached) progressTimes_[static_cast<size_t>(reached + 1)] = t;
    }
    // The exit is reached at the end of the run.
    for (; reached < kProgressSteps; ++reached) progressTimes_[static_cast<size_t>(reached + 1)] = duration_;
}

float GhostTrack::timeAtProgress(float progress) const {
    if (progressTimes_.empty()) return -1.0f;
    float f = std::clamp(progress, 0.0f, 1.0f) * kProgressSteps;
    int i = std::min(static_cast<int>(f), kProgressSteps - 1);
    float a = progressTimes_[static_cast<size_t>(i)], b = progressTimes_[static_cast<size_t>(i + 1)];
    if (a < 0.0f || b < 0.0f) return a;
    return a + (b - a) * (f - static_cast<float>(i));
}

float GhostTrack::progressAtTime(float t) const {
    if (progressTimes_.empty()) return -1.0f;
    // First times are in order along the route (filled while the run goes on): the last
    // step reached by `t` is found by bisection.
    auto it = std::upper_bound(progressTimes_.begin(), progressTimes_.end(), t);
    if (it == progressTimes_.begin()) return 0.0f;
    auto k = static_cast<int>(it - progressTimes_.begin()) - 1;
    return static_cast<float>(k) / kProgressSteps;
}

float GhostTrack::nearestTime(Vec2 p) const {
    float best = 1e30f, t = 0.0f;
    for (size_t i = 0; i < frames_.size(); ++i) {
        float d = (Vec2{frames_[i].x, frames_[i].y} - p).lengthSq();
        if (d < best) {
            best = d;
            t = static_cast<float>(i) / hz_;
        }
    }
    return t;
}

GhostTrack::Pose GhostTrack::poseAt(float t) const {
    Pose p;
    if (frames_.empty()) return p;
    float f = std::max(0.0f, t) * hz_;
    size_t i = std::min(static_cast<size_t>(f), frames_.size() - 1);
    const ReplayFrame& a = frames_[i];
    p.state = a.state;
    p.flags = a.flags;
    p.feet = {a.x, a.y};
    p.visible = true;
    if (i + 1 >= frames_.size()) return p;
    const ReplayFrame& b = frames_[i + 1];
    bool cut = std::binary_search(cuts_.begin(), cuts_.end(), static_cast<uint32_t>(i + 1)) ||
               std::binary_search(cuts_.begin(), cuts_.end(), static_cast<uint32_t>(i));
    Vec2 d{b.x - a.x, b.y - a.y};
    if (cut || d.lengthSq() > 120.0f * 120.0f) return p;
    float u = f - static_cast<float>(i);
    p.feet = {a.x + d.x * u, a.y + d.y * u};
    p.vel = d * hz_;
    return p;
}

}  // namespace cosmic
