#include "game/ghost/ReplayCompare.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cosmic {

std::vector<int64_t> replaySplitsMs(const ReplayData& r, bool* exact) {
    std::vector<int64_t> out;
    for (const ReplayEvent& e : r.events)
        if (e.type == ReplayEventType::Split) out.push_back(r.splitTimeMs(e));
    if (exact) *exact = !out.empty();
    if (!out.empty()) return out;
    // Older replays: the checkpoint event belongs to the sample after it (as GhostTrack).
    const double hz = std::max<uint16_t>(1, r.sampleHz);
    for (const ReplayEvent& e : r.events)
        if (e.type == ReplayEventType::Checkpoint)
            out.push_back(std::max<int64_t>(0, std::llround((static_cast<double>(e.frame) - 0.5) * 1000.0 / hz)));
    return out;
}

std::string formatRunMs(int64_t ms) {
    char buf[40];
    const char* sign = ms < 0 ? "-" : "";
    int64_t a = std::llabs(ms);
    long long s = a / 1000;
    long long frac = a % 1000;
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%s%lld:%02lld:%02lld,%03lld", sign, s / 3600, (s / 60) % 60, s % 60, frac);
    else std::snprintf(buf, sizeof buf, "%s%lld:%02lld,%03lld", sign, s / 60, s % 60, frac);
    return buf;
}

std::string formatDeltaMs(int64_t ms, bool unit) {
    if (ms == 0) return unit ? "0,000 s" : "0,000";
    char buf[40];
    long long a = std::llabs(ms);
    char sign = ms > 0 ? '+' : '-';
    if (a >= 60000) {
        std::snprintf(buf, sizeof buf, "%c%lld:%02lld,%03lld", sign, a / 60000, (a / 1000) % 60, a % 1000);
        return buf;
    }
    std::snprintf(buf, sizeof buf, unit ? "%c%lld,%03lld s" : "%c%lld,%03lld", sign, a / 1000, a % 1000);
    return buf;
}

std::string formatCompareOffset(int64_t ms) { return ms == 0 ? "sans décalage" : "décalage " + formatDeltaMs(ms); }

std::optional<int64_t> parseCompareOffset(const std::string& text) {
    // Spaces (also the French non-breaking ones) do not count; the minus sign may be "−".
    std::string s;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\t') continue;
        if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            ++i;
            continue;
        }
        if (c == 0xE2 && i + 2 < text.size()) {
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]), c2 = static_cast<unsigned char>(text[i + 2]);
            if (c1 == 0x80 && c2 == 0xAF) {  // narrow no-break space
                i += 2;
                continue;
            }
            if (c1 == 0x88 && c2 == 0x92) {  // minus sign
                s += '-';
                i += 2;
                continue;
            }
        }
        s += static_cast<char>(std::tolower(c));
    }
    bool millis = false;
    if (s.size() >= 2 && s.compare(s.size() - 2, 2, "ms") == 0) {
        millis = true;
        s.resize(s.size() - 2);
    } else if (!s.empty() && s.back() == 's') {
        s.pop_back();
    }
    if (s.empty()) return std::nullopt;
    int sign = 1;
    if (s[0] == '+' || s[0] == '-') {
        sign = s[0] == '-' ? -1 : 1;
        s.erase(0, 1);
    }
    if (s.empty()) return std::nullopt;
    int64_t minutes = 0;
    if (auto colon = s.find(':'); colon != std::string::npos) {
        if (millis || colon == 0 || colon > 3) return std::nullopt;
        for (size_t i = 0; i < colon; ++i) {
            if (s[i] < '0' || s[i] > '9') return std::nullopt;
            minutes = minutes * 10 + (s[i] - '0');
        }
        s.erase(0, colon + 1);
        if (s.empty()) return std::nullopt;
    }
    // Whole part, then at most one decimal separator.
    int64_t whole = 0;
    size_t i = 0;
    int digits = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++digits) {
        if (digits >= 9) return std::nullopt;
        whole = whole * 10 + (s[i] - '0');
    }
    int64_t frac = 0;  // in thousandths of the unit
    if (i < s.size() && (s[i] == ',' || s[i] == '.')) {
        ++i;
        int64_t scale = 100;
        int fracDigits = 0;
        bool roundUp = false;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++fracDigits) {
            if (fracDigits < 3) frac += (s[i] - '0') * scale, scale /= 10;
            else if (fracDigits == 3) roundUp = s[i] >= '5';
        }
        if (fracDigits == 0 && digits == 0) return std::nullopt;
        if (roundUp) ++frac;
    } else if (digits == 0) {
        return std::nullopt;
    }
    if (i != s.size()) return std::nullopt;
    if (minutes > 0 && whole >= 60) return std::nullopt;
    int64_t ms = millis ? whole + (frac >= 500 ? 1 : 0) : minutes * 60000 + whole * 1000 + frac;
    if (ms > 7200000) return std::nullopt;
    return sign * ms;
}

// ---------------------------------------------------------------- runs

CompareRun CompareRun::fromReplay(const ReplayData& r, GhostMeta meta) {
    CompareRun c;
    c.meta = std::move(meta);
    c.track.load(r);
    c.splitsMs = replaySplitsMs(r, &c.splitsExact);
    c.durationMs = r.durationMs;
    c.levelHash = r.levelHash;
    if (c.meta.timeMs <= 0) c.meta.timeMs = r.durationMs;
    return c;
}

// ---------------------------------------------------------- comparison

void ReplayComparison::set(CompareRun a, CompareRun b) {
    a_ = std::move(a);
    b_ = std::move(b);
    ready_ = true;
    offsetMs_ = 0;
    alignedRow_ = -1;
    rebuild();
}

void ReplayComparison::setRoute(const LevelProgressTrack& track) {
    a_.track.mapProgress(track);
    b_.track.mapProgress(track);
}

int64_t ReplayComparison::maxOffsetMs() const { return std::max<int64_t>(1000, a_.durationMs + b_.durationMs); }

void ReplayComparison::setOffsetMs(int64_t ms) {
    int64_t m = maxOffsetMs();
    ms = std::clamp(ms, -m, m);
    if (ms != offsetMs_) alignedRow_ = -1;
    offsetMs_ = ms;
    rebuild();
}

bool ReplayComparison::alignOn(int row) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return false;
    const Row& r = rows_[static_cast<size_t>(row)];
    int64_t aAt = r.exit ? a_.durationMs : r.aCumMs;
    int64_t bAt = r.exit ? b_.durationMs : (r.bSplit >= 0 ? b_.splitsMs[static_cast<size_t>(r.bSplit)] : -1);
    if (aAt < 0 || bAt < 0) return false;
    setOffsetMs(aAt - bAt);
    alignedRow_ = offsetMs_ == aAt - bAt ? row : -1;
    return alignedRow_ == row;
}

double ReplayComparison::endTime() const {
    return static_cast<double>(std::max(a_.durationMs, b_.durationMs + offsetMs_)) / 1000.0;
}

void ReplayComparison::rebuild() {
    rows_.clear();
    totals_ = {};
    if (!ready_) return;
    // Checkpoints matched by position, in order: B's next split passed near A's.
    std::vector<Vec2> pb;
    pb.reserve(b_.splitsMs.size());
    for (int64_t ms : b_.splitsMs) pb.push_back(b_.track.poseAt(static_cast<float>(ms) / 1000.0f).feet);
    int last = -1;
    int64_t prevA = 0;
    for (size_t i = 0; i < a_.splitsMs.size(); ++i) {
        Row r;
        r.aSplit = static_cast<int>(i);
        r.aCumMs = a_.splitsMs[i];
        r.aSegMs = std::max<int64_t>(0, r.aCumMs - prevA);
        prevA = r.aCumMs;
        Vec2 pa = a_.track.poseAt(static_cast<float>(r.aCumMs) / 1000.0f).feet;
        for (int j = last + 1; j < static_cast<int>(pb.size()); ++j)
            if ((pb[static_cast<size_t>(j)] - pa).lengthSq() < 320.0f * 320.0f) {
                r.bSplit = j;
                last = j;
                break;
            }
        rows_.push_back(r);
    }
    Row exitRow;
    exitRow.exit = true;
    exitRow.aCumMs = a_.durationMs;
    exitRow.aSegMs = std::max<int64_t>(0, a_.durationMs - prevA);
    rows_.push_back(exitRow);
    // B: its times at the same places; a segment is known when B passed both of its ends.
    bool prevKnown = true;
    int64_t prevB = 0;
    for (Row& r : rows_) {
        int64_t bAt = r.exit ? b_.durationMs : (r.bSplit >= 0 ? b_.splitsMs[static_cast<size_t>(r.bSplit)] : -1);
        if (bAt >= 0) {
            r.bCumMs = bAt + offsetMs_;
            r.hasGap = true;
            r.gapMs = r.aCumMs - r.bCumMs;
            if (prevKnown) r.bSegMs = std::max<int64_t>(0, bAt - prevB);
            prevB = bAt;
            prevKnown = true;
        } else {
            prevKnown = false;
        }
        if (r.aSegMs >= 0 && r.bSegMs >= 0) {
            r.hasSegDiff = true;
            r.segDiffMs = r.aSegMs - r.bSegMs;
            r.faster = r.segDiffMs < 0 ? 1 : r.segDiffMs > 0 ? 2 : 0;
        }
    }
    totals_.aMs = a_.durationMs;
    totals_.bMs = b_.durationMs;
    totals_.diffMs = a_.durationMs - b_.durationMs;
    totals_.gapMs = a_.durationMs - (b_.durationMs + offsetMs_);
    totals_.faster = totals_.diffMs < 0 ? 1 : totals_.diffMs > 0 ? 2 : 0;
}

std::optional<int64_t> ReplayComparison::liveGapMs(double aTime) const {
    if (!ready_) return std::nullopt;
    if (aTime * 1000.0 >= static_cast<double>(a_.durationMs)) return totals_.gapMs;  // A has finished: exact
    if (aTime <= 0.002 || !a_.track.mapped() || !b_.track.mapped()) return std::nullopt;
    float p = a_.track.progressAtTime(static_cast<float>(aTime));
    if (p <= 0.002f) return std::nullopt;
    float tb = b_.track.timeAtProgress(p);
    if (tb < 0.0f) return std::nullopt;
    return std::llround(aTime * 1000.0) - (std::llround(static_cast<double>(tb) * 1000.0) + offsetMs_);
}

int ReplayComparison::rowAt(double aTime) const {
    int64_t t = std::llround(aTime * 1000.0);
    int found = -1;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].aCumMs >= 0 && rows_[i].aCumMs <= t) found = static_cast<int>(i);
    return found;
}

}  // namespace cosmic
