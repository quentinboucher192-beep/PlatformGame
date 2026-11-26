#include "game/ghost/GhostRace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cosmic {

std::vector<float> segmentDurations(const std::vector<float>& splits, float finalTime) {
    std::vector<float> out;
    out.reserve(splits.size() + 1);
    float prev = 0.0f;
    for (float s : splits) {
        out.push_back(std::max(0.0f, s - prev));
        prev = s;
    }
    if (finalTime > 0.0f) out.push_back(std::max(0.0f, finalTime - prev));
    return out;
}

std::string formatGhostDelta(float seconds, bool unit) {
    long ms = std::lround(static_cast<double>(seconds) * 1000.0);
    char buf[32];
    long a = std::labs(ms);
    if (ms == 0) return unit ? "0,000 s" : "0,000";
    if (a >= 60000) {
        std::snprintf(buf, sizeof buf, "%c%ld:%02ld,%03ld", ms > 0 ? '+' : '-', a / 60000, (a / 1000) % 60, a % 1000);
        return buf;
    }
    std::snprintf(buf, sizeof buf, unit ? "%c%ld,%03ld s" : "%c%ld,%03ld", ms > 0 ? '+' : '-', a / 1000, a % 1000);
    return buf;
}

std::string formatGhostSeconds(float seconds) {
    const long ms = std::lround(static_cast<double>(std::max(0.0f, seconds)) * 1000.0);
    char buf[32];
    if (ms >= 60000) std::snprintf(buf, sizeof buf, "%ld:%02ld,%03ld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    else std::snprintf(buf, sizeof buf, "%ld,%03ld s", ms / 1000, ms % 1000);
    return buf;
}

std::string formatGhostTime(float seconds) {
    long cs = std::lround(static_cast<double>(std::max(0.0f, seconds)) * 100.0);
    char buf[32];
    long s = cs / 100;
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld,%02ld", s / 3600, (s / 60) % 60, s % 60, cs % 100);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld,%02ld", s / 60, s % 60, cs % 100);
    return buf;
}

namespace {
/// Gaps compared to the millisecond (what the timer shows): a tie is a tie.
float msGap(float a, float b) { return static_cast<float>(std::llround(static_cast<double>(a) * 1000.0) - std::llround(static_cast<double>(b) * 1000.0)) / 1000.0f; }
}  // namespace

void GhostRace::clear() {
    racers_.clear();
    reference_ = 0;
    offset_ = 0.0f;
    splitBase_ = 0;
    track_ = {};
    hint_ = 0;
    bestProgress_ = 0.0f;
    runTime_ = 0.0f;
    sinceSplit_ = 1e9f;
    finished_ = false;
    finalTime_ = 0.0f;
    results_.clear();
}

int GhostRace::add(GhostMeta meta, const ReplayData& replay, bool sameVersion, Color color) {
    GhostTrack t;
    t.load(replay);
    return add(std::move(meta), std::move(t), sameVersion, color);
}

int GhostRace::add(GhostMeta meta, GhostTrack track, bool sameVersion, Color color) {
    if (size() >= kMaxGhosts) return -1;
    Racer r;
    r.meta = std::move(meta);
    r.track = std::move(track);
    r.sameVersion = sameVersion;
    r.color = color;
    if (track_.valid() && !r.track.mapped()) r.track.mapProgress(track_);
    racers_.push_back(std::move(r));
    results_.reserve(64);
    return size() - 1;
}

void GhostRace::setReference(int i) {
    if (i >= 0 && i < size()) reference_ = i;
}

void GhostRace::setRoute(const LevelProgressTrack& track) {
    track_ = track;
    hint_ = 0;
    for (Racer& r : racers_)
        if (!r.track.mapped()) r.track.mapProgress(track_);
}

void GhostRace::setPersonal(std::vector<float> bestSegments, std::vector<float> pbSplits, float pbTime) {
    bestSegments_ = std::move(bestSegments);
    pbSplits_ = std::move(pbSplits);
    pbTime_ = pbTime;
}

void GhostRace::update(float runTime, Vec2 feet, float dt) {
    if (finished_) {
        // The ghosts finish their own runs.
        runTime_ += dt;
    } else {
        runTime_ = runTime;
    }
    sinceSplit_ += dt;
    if (!finished_ && track_.valid()) {
        float p = track_.project(feet, hint_);
        if ((track_.route[hint_] - feet).lengthSq() < 200.0f * 200.0f) bestProgress_ = std::max(bestProgress_, p);
    }
    float t = runTime_ + offset_;
    for (Racer& r : racers_) {
        uint8_t before = r.pose.state;
        r.pose = r.track.poseAt(t);
        r.animTime = r.pose.state == before ? r.animTime + dt : 0.0f;
        if (finished_) continue;
        r.timeHere = r.track.timeAtProgress(bestProgress_);
        r.hasLiveDelta = r.timeHere >= 0.0f && bestProgress_ > 0.002f;
        r.liveDelta = r.hasLiveDelta ? t - r.timeHere : 0.0f;
    }
}

void GhostRace::onSplit(int index, float runTime, Vec2 feet) {
    if (finished_) return;
    SplitResult s;
    s.index = index;
    s.player = runTime + offset_;
    float prevPlayer = results_.empty() ? offset_ : results_.back().player;
    s.segment = std::max(0.0f, s.player - prevPlayer);
    if (!racers_.empty()) {
        const Racer& ref = racers_[static_cast<size_t>(reference_)];
        const std::vector<float>& gs = ref.track.splits();
        // The ghost's split at the same checkpoint: the next one passed near the player's
        // feet (a skipped or revisited checkpoint does not shift the comparison).
        int from = results_.empty() ? 0 : results_.back().ghostIndex + 1;
        for (int j = std::max(0, from); j < static_cast<int>(gs.size()); ++j) {
            Vec2 at = ref.track.poseAt(gs[static_cast<size_t>(j)]).feet;
            if ((at - feet).lengthSq() < 320.0f * 320.0f) {
                s.ghostIndex = j;
                break;
            }
        }
        if (s.ghostIndex < 0 && results_.empty() && offset_ > 0.0f) {
            // Training: the ghost's splits before the start are skipped by time.
            for (int j = 0; j < static_cast<int>(gs.size()); ++j) {
                if (gs[static_cast<size_t>(j)] <= offset_ + 0.05f) continue;
                Vec2 at = ref.track.poseAt(gs[static_cast<size_t>(j)]).feet;
                if ((at - feet).lengthSq() < 320.0f * 320.0f) s.ghostIndex = j;
                break;
            }
        }
        if (s.ghostIndex >= 0) {
            s.comparable = true;
            s.ghost = gs[static_cast<size_t>(s.ghostIndex)];
            s.delta = msGap(s.player, s.ghost);
            float prevGhost = -1.0f;
            if (results_.empty()) prevGhost = offset_;
            else if (results_.back().comparable) prevGhost = gs[static_cast<size_t>(results_.back().ghostIndex)];
            if (prevGhost >= 0.0f) s.ghostSegment = std::max(0.0f, s.ghost - prevGhost);
        }
    }
    // Personal references (full runs: the timer's split index; training: from its base).
    size_t pi = static_cast<size_t>(std::max(0, splitBase_ + index));
    bool fullRun = offset_ <= 0.0f;
    if (pi < bestSegments_.size()) {
        s.bestSegment = bestSegments_[pi];
        s.goldSegment = s.bestSegment > 0.0f && s.segment < s.bestSegment - 0.0005f;
    }
    if (fullRun && pi < pbSplits_.size()) s.aheadOfRecord = s.player < pbSplits_[pi];
    results_.push_back(s);
    sinceSplit_ = 0.0f;
}

void GhostRace::finish(float runTime) {
    if (finished_) return;
    SplitResult s;
    s.index = -1;  // the exit
    s.player = runTime + offset_;
    float prevPlayer = results_.empty() ? offset_ : results_.back().player;
    s.segment = std::max(0.0f, s.player - prevPlayer);
    if (!racers_.empty()) {
        const Racer& ref = racers_[static_cast<size_t>(reference_)];
        s.comparable = true;
        s.ghost = ref.track.duration();
        s.delta = msGap(s.player, s.ghost);
        float prevGhost = -1.0f;
        if (results_.empty()) prevGhost = offset_;
        else if (results_.back().comparable) prevGhost = ref.track.splits()[static_cast<size_t>(results_.back().ghostIndex)];
        if (prevGhost >= 0.0f) s.ghostSegment = std::max(0.0f, s.ghost - prevGhost);
        for (Racer& r : racers_) {
            r.timeHere = r.track.duration();
            r.hasLiveDelta = true;
            r.liveDelta = msGap(s.player, r.timeHere);
        }
    }
    if (offset_ <= 0.0f) {
        size_t pi = results_.size();  // the exit's segment comes after every split
        if (pi < bestSegments_.size()) {
            s.bestSegment = bestSegments_[pi];
            s.goldSegment = s.bestSegment > 0.0f && s.segment < s.bestSegment - 0.0005f;
        }
        s.aheadOfRecord = pbTime_ > 0.0f && s.player < pbTime_;
    }
    results_.push_back(s);
    finished_ = true;
    finalTime_ = s.player;
    runTime_ = runTime;
    sinceSplit_ = 0.0f;
}

float GhostRace::estimatedRemaining() const {
    if (racers_.empty()) return -1.0f;
    const Racer& ref = racers_[static_cast<size_t>(reference_)];
    if (ref.timeHere < 0.0f || !ref.track.mapped()) return -1.0f;
    return std::max(0.0f, ref.track.duration() - ref.timeHere);
}

float GhostRace::estimatedFinish() const {
    float rem = estimatedRemaining();
    return rem < 0.0f ? -1.0f : runTime_ + offset_ + rem;
}

}  // namespace cosmic
