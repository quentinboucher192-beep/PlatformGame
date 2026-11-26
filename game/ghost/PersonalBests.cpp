#include "game/ghost/PersonalBests.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"
#include "engine/core/Log.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/KeptReplays.h"
#include "game/records/PlayStats.h"

namespace cosmic {

namespace {

using Range = PersonalBests::Range;

/// A checkpoint id or a level key as read back: short, no control character.
bool validId(const std::string& s) {
    if (s.size() > 96) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool validSeconds(float s) { return std::isfinite(s) && s > 0.0f && s <= 7200.0f; }

/// Compared to the millisecond (what the chronometer shows): a tie is not a new best.
long long ms(float s) { return std::llround(static_cast<double>(s) * 1000.0); }

/// A replay matches its range to the half millisecond (the chronometer's own values).
constexpr float kSameTime = 0.0005f;

std::string dayOf(const std::string& date) { return date.size() >= 10 ? date.substr(0, 10) : date; }

std::vector<float> readFloats(const Json& j, size_t max) {
    std::vector<float> out;
    if (!j.is_array()) return out;
    for (const Json& v : j) {
        if (!v.is_number() || out.size() >= max) break;
        out.push_back(v.get<float>());
    }
    return out;
}

/// Ids as read back: as many as `count` (any when `count` is npos), valid, never empty; else none.
std::optional<std::vector<std::string>> readIds(const Json& j, size_t count = std::string::npos, size_t max = 256) {
    if (!j.is_array() || (count != std::string::npos && j.size() != count) || j.size() > max) return std::nullopt;
    std::vector<std::string> out;
    for (const Json& v : j) {
        if (!v.is_string()) return std::nullopt;
        std::string s = v.get<std::string>();
        if (s.empty() || !validId(s)) return std::nullopt;
        out.push_back(std::move(s));
    }
    return out;
}

bool validTimes(const std::vector<float>& v, float limit) {
    float prev = 0.0f;
    for (float s : v) {
        if (!std::isfinite(s) || s < prev || s > limit) return false;
        prev = s;
    }
    return true;
}

Json replayJson(const BestReplay& r) {
    Json j{{"file", r.file}, {"start", r.start}, {"end", r.end}};
    if (!r.fromLevelStart) j["setup"] = r.setup.toJson();
    return j;
}

/// A best's replay as read back: dropped (none) when it does not describe its range exactly.
BestReplay readReplay(const Json& j, float best) {
    BestReplay r;
    if (!j.is_object()) return r;
    std::string file = jstr(j, "file");
    float a = jget<float>(j, "start", -1.0f), b = jget<float>(j, "end", -1.0f);
    if (!keptreplays::validName(file) || !std::isfinite(a) || !std::isfinite(b) || a < 0.0f || b > 7200.0f || b < a || best < 0.0f ||
        std::abs((b - a) - best) > kSameTime)
        return r;
    if (j.contains("setup")) {
        auto s = SectionTrainingStart::fromJson(j["setup"]);
        if (!s) return r;
        r.fromLevelStart = false;
        r.setup = std::move(*s);
    }
    r.file = std::move(file);
    r.start = a;
    r.end = b;
    return r;
}

Json runJson(const PersonalBests::Run& r) {
    Json j{{"date", r.date}, {"time", r.time}, {"splits", r.splits}, {"source", bestSourceId(r.source)}};
    if (!r.checkpoints.empty()) j["checkpoints"] = r.checkpoints;
    return j;
}

std::optional<PersonalBests::Run> readRun(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    PersonalBests::Run r;
    r.date = jstr(j, "date").substr(0, 32);
    r.time = jget<float>(j, "time", 0.0f);
    if (j.contains("splits")) r.splits = readFloats(j["splits"], 255);
    if (!validSeconds(r.time) || !validTimes(r.splits, r.time)) return std::nullopt;
    if (j.contains("checkpoints"))
        if (auto ids = readIds(j["checkpoints"], r.splits.size())) r.checkpoints = std::move(*ids);
    auto src = bestSourceFromId(jstr(j, "source", "run"));
    r.source = src && (*src == BestSource::Online) ? BestSource::Online : BestSource::Run;
    return r;
}

/// The boundaries of the route: 0 = the start, k = its k-th checkpoint, n + 1 = the exit.
Range segmentOf(const std::vector<std::string>& route, size_t k) {
    return {k == 0 ? std::string{} : route[k - 1], k >= route.size() ? std::string{} : route[k]};
}

}  // namespace

// ------------------------------------------------------------------ sources

const char* bestSourceLabel(BestSource s) {
    switch (s) {
        case BestSource::Run: return "\x1F" "source|Partie";  // « Partie » (a run), not « In game »
        case BestSource::Training: return "Entraînement";
        case BestSource::Chain: return "Enchaînement";
        case BestSource::StartPos: return "StartPos";
        case BestSource::Online: return "En ligne";
    }
    return "Partie";
}

const char* bestSourceId(BestSource s) {
    switch (s) {
        case BestSource::Run: return "run";
        case BestSource::Training: return "practice";
        case BestSource::Chain: return "chain";
        case BestSource::StartPos: return "startpos";
        case BestSource::Online: return "online";
    }
    return "run";
}

std::optional<BestSource> bestSourceFromId(const std::string& id) {
    for (BestSource s : {BestSource::Run, BestSource::Training, BestSource::Chain, BestSource::StartPos, BestSource::Online})
        if (id == bestSourceId(s)) return s;
    return std::nullopt;
}

BestPassage BestPassage::ofSplits(const std::vector<float>& splits, const std::vector<std::string>& ids, float exitTime, From from,
                          const std::string& startCheckpoint) {
    BestPassage p;
    p.from = from;
    p.startCheckpoint = startCheckpoint;
    p.exitTime = exitTime > 0.0f ? exitTime : -1.0f;
    if (ids.size() == splits.size()) {
        for (size_t i = 0; i < splits.size(); ++i) p.checkpoints.push_back({ids[i], splits[i]});
    } else {
        p.checkpointsKnown = false;  // which checkpoint each split is: unknown
        p.unknownSplits = splits;
    }
    return p;
}

// ------------------------------------------------------------------ the store

PersonalBests::PersonalBests(fs::path file, fs::path replayDir, Legacy legacy)
    : file_(std::move(file)), replayDir_(std::move(replayDir)), legacy_(std::move(legacy)) {}

PersonalBests::Level& PersonalBests::level(const std::string& key, const std::string& date) {
    auto it = levels_.find(key);
    if (it == levels_.end()) {
        // Bounded file: the least recently changed levels go first.
        while (levels_.size() >= kLevelsKept) {
            auto oldest = std::min_element(levels_.begin(), levels_.end(), [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
            levels_.erase(oldest);
        }
        it = levels_.emplace(key, Level{}).first;
    }
    if (!date.empty() && date > it->second.touched) it->second.touched = date.substr(0, 32);
    return it->second;
}

bool PersonalBests::offer(Level& l, const Range& r, float seconds, BestSource source, const std::string& date, const BestReplay& replay,
                          bool passage) {
    if (!validSeconds(seconds) || !validId(r.first) || !validId(r.second)) return false;
    auto it = l.ranges.find(r);
    if (it == l.ranges.end()) {
        if (l.ranges.size() >= kRangesPerLevel) return false;
        it = l.ranges.emplace(r, Record{}).first;
    }
    Record& rec = it->second;
    if (passage) rec.passages = std::min(rec.passages + 1, 10000000);
    const bool better = rec.best < 0.0f || ms(seconds) < ms(rec.best);
    if (better) {
        rec.best = seconds;
        rec.source = source;
        rec.date = date.substr(0, 32);
        rec.replay = replay;
    } else if (replay.has() && !rec.replay.has() && ms(seconds) == ms(rec.best)) {
        rec.replay = replay;  // the same time, with a replay this best did not have
    }
    rec.passages = std::max(rec.passages, 1);
    return better;
}

float PersonalBests::sobOf(const Level& l) const {
    if (!l.routeKnown) return -1.0f;
    double sum = 0.0;
    for (size_t k = 0; k <= l.route.size(); ++k) {
        auto it = l.ranges.find(segmentOf(l.route, k));
        if (it == l.ranges.end() || !validSeconds(it->second.best)) return -1.0f;
        sum += it->second.best;
    }
    return static_cast<float>(sum);
}

void PersonalBests::noteHistory(Level& l, const std::string& date, float sobBefore, float pbBefore) {
    const float sob = sobOf(l), pb = l.pb ? l.pb->time : -1.0f;
    auto changed = [](float a, float b) { return (a > 0.0f) != (b > 0.0f) || (a > 0.0f && ms(a) != ms(b)); };
    if (!changed(sob, sobBefore) && !changed(pb, pbBefore)) return;
    const std::string day = dayOf(date);
    if (!l.history.empty() && l.history.back().day == day) l.history.back() = {day, sob, pb};
    else l.history.push_back({day, sob, pb});
    if (l.history.size() > kHistoryKept) l.history.erase(l.history.begin(), l.history.end() - kHistoryKept);
}

void PersonalBests::resolveLegacy(Level& l) {
    if (!l.legacy || !l.routeKnown || l.legacy->best.size() != l.route.size() + 1) return;
    const Legacy0 g = std::move(*l.legacy);
    l.legacy.reset();
    for (size_t k = 0; k < g.best.size(); ++k)
        offer(l, segmentOf(l.route, k), g.best[k], BestSource::Run, g.date, k < g.replays.size() ? g.replays[k] : BestReplay{}, false);
}

std::string PersonalBests::keepReplay(const std::string& key, const std::vector<uint8_t>& bytes, const std::string& date) const {
    return keptreplays::write(replayDir_, key, bytes, date);
}

PersonalBests::Added PersonalBests::add(const std::string& levelKey, const BestAttempt& a) {
    Added out;
    if (levelKey.empty() || !validId(levelKey)) return out;
    const BestPassage& p = a.passage;
    // The boundaries crossed, in order (a damaged entry ends the list there).
    struct B {
        std::string id;
        float t = 0.0f;
        int kind = 1;  // 0 the start, 1 a checkpoint, 2 the exit
    };
    std::vector<B> bs;
    if (p.from == BestPassage::From::LevelStart) bs.push_back({{}, 0.0f, 0});
    else if (p.from == BestPassage::From::Checkpoint) {
        if (p.startCheckpoint.empty() || !validId(p.startCheckpoint)) return out;
        bs.push_back({p.startCheckpoint, 0.0f, 1});
    }
    float prev = 0.0f;
    bool whole = true;
    for (const auto& [id, t] : p.checkpoints) {
        if (id.empty() || !validId(id) || !std::isfinite(t) || t < prev || t > 7200.0f) {
            whole = false;
            break;
        }
        if (bs.size() == 1 && bs[0].kind == 1 && bs[0].id == id) continue;  // the start checkpoint itself
        if (std::any_of(bs.begin(), bs.end(), [&](const B& b) { return b.kind == 1 && b.id == id; })) {
            whole = false;  // reached twice: not a run through the route
            break;
        }
        bs.push_back({id, t, 1});
        prev = t;
    }
    if (whole && validSeconds(p.exitTime) && p.exitTime >= prev) bs.push_back({{}, p.exitTime, 2});
    if (bs.size() < 2) return out;

    Level& l = level(levelKey, a.date);
    const float sobBefore = sobOf(l), pbBefore = l.pb ? l.pb->time : -1.0f;
    const bool fullRun = bs.front().kind == 0 && bs.back().kind == 2 && (a.source == BestSource::Run || a.source == BestSource::Online);
    // A verified online run already known (or one of this computer's runs, the same time): its
    // passages are not counted twice.
    bool count = true;
    if (a.source == BestSource::Online) {
        if (!a.onlineId.empty()) {
            if (std::find(l.onlineIds.begin(), l.onlineIds.end(), a.onlineId) != l.onlineIds.end()) count = false;
            else {
                l.onlineIds.push_back(a.onlineId.substr(0, 96));
                if (l.onlineIds.size() > 64) l.onlineIds.erase(l.onlineIds.begin());
            }
        }
        if (fullRun)
            for (const Run& r : l.runs)
                if (ms(r.time) == ms(p.exitTime)) count = false;
    }
    std::vector<std::string> ids;
    for (const B& b : bs)
        if (b.kind == 1) ids.push_back(b.id);
    // The route, learned from a full run while the level has not given it (the one through the
    // most checkpoints).
    if (fullRun && p.checkpointsKnown && !l.routeGiven && (!l.routeKnown || ids.size() > l.route.size())) {
        l.route = ids;
        l.routeKnown = true;
        resolveLegacy(l);
    }
    // Where each boundary is on the route (-1: not on it).
    auto pos = [&](const B& b) -> int {
        if (b.kind == 0) return 0;
        if (b.kind == 2) return static_cast<int>(l.route.size()) + 1;
        auto it = std::find(l.route.begin(), l.route.end(), b.id);
        return it == l.route.end() ? -1 : static_cast<int>(it - l.route.begin()) + 1;
    };
    const bool onRoute = l.routeKnown && std::all_of(bs.begin(), bs.end(), [&](const B& b) { return pos(b) >= 0; });
    std::string replayName;
    bool replayWritten = false;
    for (size_t i = 0; i + 1 < bs.size(); ++i)
        for (size_t j = i + 1; j < bs.size(); ++j) {
            // Unknown checkpoints: only the whole run is known (from the start to the exit).
            if (!p.checkpointsKnown && !(bs[i].kind == 0 && bs[j].kind == 2)) continue;
            // Segments that follow each other on the route (none skipped, none in another order).
            if (onRoute && p.checkpointsKnown) {
                bool straight = true;
                for (size_t k = i; k <= j && straight; ++k) straight = pos(bs[k]) == pos(bs[i]) + static_cast<int>(k - i);
                if (!straight) continue;
            }
            const float secs = bs[j].t - bs[i].t;
            const Range r{bs[i].kind == 0 ? std::string{} : bs[i].id, bs[j].kind == 2 ? std::string{} : bs[j].id};
            const Record* before = nullptr;
            if (auto it = l.ranges.find(r); it != l.ranges.end()) before = &it->second;
            const bool better = validSeconds(secs) && (!before || before->best < 0.0f || ms(secs) < ms(before->best));
            BestReplay rep;
            if (better && a.replay && !replayWritten) {
                replayWritten = true;  // one copy for every best this attempt sets
                replayName = keepReplay(levelKey, *a.replay, a.date);
            }
            if (better && !replayName.empty()) {
                rep.file = replayName;
                rep.start = bs[i].t;
                rep.end = bs[j].t;
                rep.fromLevelStart = p.from == BestPassage::From::LevelStart;
                if (!rep.fromLevelStart) rep.setup = a.setup;
            }
            if (offer(l, r, secs, a.source, a.date, rep, count) && better) out.newBests.push_back(r);
        }
    if (fullRun) {
        Run run;
        run.date = a.date.substr(0, 32);
        for (const B& b : bs)
            if (b.kind == 1) run.splits.push_back(b.t);
        run.time = p.exitTime;
        if (p.checkpointsKnown) run.checkpoints = ids;
        else if (validTimes(p.unknownSplits, run.time) && p.unknownSplits.size() <= 255) run.splits = p.unknownSplits;
        run.source = a.source;
        if (count) {
            l.runs.insert(l.runs.begin(), run);
            if (l.runs.size() > kRunsKept) l.runs.resize(kRunsKept);
        }
        if (!l.pb || ms(run.time) < ms(l.pb->time)) {
            l.pb = run;
            out.newPb = true;
        }
    }
    out.sobBefore = sobBefore;
    out.sobAfter = sobOf(l);
    noteHistory(l, a.date, sobBefore, pbBefore);
    return out;
}

void PersonalBests::setRoute(const std::string& levelKey, std::vector<std::string> checkpoints) {
    if (levelKey.empty() || !validId(levelKey) || checkpoints.size() > 256) return;
    std::set<std::string> seen;
    for (const std::string& id : checkpoints)
        if (id.empty() || !validId(id) || !seen.insert(id).second) return;
    auto it = levels_.find(levelKey);
    if (it != levels_.end() && it->second.routeGiven && it->second.route == checkpoints) return;  // nothing new
    Level& l = level(levelKey, {});
    l.route = std::move(checkpoints);
    l.routeKnown = true;
    l.routeGiven = true;
    resolveLegacy(l);
}

void PersonalBests::setPowers(const std::string& levelKey, const Range& r, const std::vector<std::string>* powers, const std::string& date) {
    if (levelKey.empty() || !validId(levelKey) || !validId(r.first) || !validId(r.second)) return;
    if (!powers) {
        if (auto it = levels_.find(levelKey); it != levels_.end()) it->second.powers.erase(r);
        return;
    }
    std::vector<std::string> kept;
    for (const std::string& id : *powers)
        if (!id.empty() && validId(id) && std::find(kept.begin(), kept.end(), id) == kept.end() && kept.size() < 8) kept.push_back(id);
    Level& l = level(levelKey, date);
    if (!l.powers.count(r) && l.powers.size() >= kRangesPerLevel) return;
    l.powers[r] = std::move(kept);
}

bool PersonalBests::adoptReplay(const std::string& levelKey, const fs::path& replayFile, const std::string& date, const std::vector<Adoption>& list) {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return false;
    Level& l = it->second;
    std::vector<std::pair<Record*, Adoption>> ok;
    for (const Adoption& a : list) {
        auto r = l.ranges.find(a.range);
        if (r == l.ranges.end() || r->second.replay.has() || r->second.best < 0.0f || !std::isfinite(a.start) || !std::isfinite(a.end) ||
            a.start < 0.0f || std::abs((a.end - a.start) - r->second.best) > kSameTime)
            continue;
        ok.push_back({&r->second, a});
    }
    if (ok.empty()) return false;
    const uint64_t n = keptreplays::fileSize(replayFile);
    if (n == 0 || n > keptreplays::kMaxFileBytes) return false;
    auto bytes = FileUtil::readBinary(replayFile);
    if (!bytes) return false;
    const std::string name = keepReplay(levelKey, *bytes, date);
    if (name.empty()) return false;
    for (auto& [rec, a] : ok) rec->replay = BestReplay{name, a.start, a.end, true, {}};
    return true;
}

void PersonalBests::importStats(const PlayStatsData& stats) {
    for (const auto& [levelId, r] : stats.runs) {
        if (r.version.empty()) continue;
        const std::string key = GhostStore::levelKey(levelId, r.version);
        bool any = validSeconds(r.pb);
        for (const SegmentRec& s : r.segments) any = any || validSeconds(s.best) || validSeconds(s.trainBest);
        if (!any) continue;
        Level& l = level(key, {});
        const float sobBefore = sobOf(l), pbBefore = l.pb ? l.pb->time : -1.0f;
        if (!l.routeKnown && !r.pbRoute.empty()) {
            std::set<std::string> seen;
            bool ok = r.pbRoute.size() <= 256;
            for (const std::string& id : r.pbRoute) ok = ok && !id.empty() && validId(id) && seen.insert(id).second;
            if (ok) {
                l.route = r.pbRoute;
                l.routeKnown = true;
                resolveLegacy(l);
            }
        }
        for (const SegmentRec& s : r.segments) {
            const Range range{s.from, s.to};
            if (validSeconds(s.best)) offer(l, range, s.best, BestSource::Run, {}, {}, false);
            if (validSeconds(s.trainBest)) offer(l, range, s.trainBest, BestSource::Training, {}, {}, false);
            if (auto it = l.ranges.find(range); it != l.ranges.end())
                it->second.passages = std::max(it->second.passages, static_cast<int>(s.times.size()));
        }
        if (validSeconds(r.pb)) offer(l, {std::string{}, std::string{}}, r.pb, BestSource::Run, {}, {}, false);
        noteHistory(l, {}, sobBefore, pbBefore);
    }
    if (std::find(migrated_.begin(), migrated_.end(), "stats.json") == migrated_.end()) migrated_.push_back("stats.json");
}

bool PersonalBests::statsImported() const { return std::find(migrated_.begin(), migrated_.end(), "stats.json") != migrated_.end(); }

// ------------------------------------------------------------------ reading

const PersonalBests::Record* PersonalBests::record(const std::string& levelKey, const Range& r) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return nullptr;
    auto rt = it->second.ranges.find(r);
    return rt == it->second.ranges.end() ? nullptr : &rt->second;
}

std::optional<std::vector<std::string>> PersonalBests::route(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || !it->second.routeKnown) return std::nullopt;
    return it->second.route;
}

std::vector<PersonalBests::Segment> PersonalBests::segments(const std::string& levelKey) const {
    std::vector<Segment> out;
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || !it->second.routeKnown) return out;
    const Level& l = it->second;
    for (size_t k = 0; k <= l.route.size(); ++k) {
        Range r = segmentOf(l.route, k);
        auto rt = l.ranges.find(r);
        out.push_back({r.first, r.second, rt == l.ranges.end() ? nullptr : &rt->second});
    }
    return out;
}

float PersonalBests::sob(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() ? -1.0f : sobOf(it->second);
}

float PersonalBests::sumOfBests(const std::string& levelKey, int from, int to) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || !it->second.routeKnown) return -1.0f;
    const Level& l = it->second;
    const int n = static_cast<int>(l.route.size());
    if (from < 0 || to > n + 1 || from >= to) return -1.0f;
    double sum = 0.0;
    for (int k = from; k < to; ++k) {
        auto rt = l.ranges.find(segmentOf(l.route, static_cast<size_t>(k)));
        if (rt == l.ranges.end() || !validSeconds(rt->second.best)) return -1.0f;
        sum += rt->second.best;
    }
    return static_cast<float>(sum);
}

const PersonalBests::Run* PersonalBests::pb(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() || !it->second.pb ? nullptr : &*it->second.pb;
}

const std::vector<PersonalBests::Run>* PersonalBests::runs(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() || it->second.runs.empty() ? nullptr : &it->second.runs;
}

std::vector<PersonalBests::SobPoint> PersonalBests::sobHistory(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() ? std::vector<SobPoint>{} : it->second.history;
}

bool PersonalBests::knowsOnline(const std::string& levelKey, const std::string& onlineId) const {
    auto it = levels_.find(levelKey);
    return it != levels_.end() && std::find(it->second.onlineIds.begin(), it->second.onlineIds.end(), onlineId) != it->second.onlineIds.end();
}

const std::vector<std::string>* PersonalBests::powers(const std::string& levelKey, const Range& r) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return nullptr;
    auto p = it->second.powers.find(r);
    return p == it->second.powers.end() ? nullptr : &p->second;
}

fs::path PersonalBests::replayPath(const std::string& levelKey, const std::string& file) const {
    fs::path dir = keptreplays::levelDir(replayDir_, levelKey);
    if (dir.empty() || !keptreplays::validName(file)) return {};
    return dir / file;
}

std::vector<std::string> PersonalBests::levels() const {
    std::vector<std::string> out;
    for (const auto& [key, l] : levels_) out.push_back(key);
    return out;
}

// ------------------------------------------------------------------ the file

void PersonalBests::load() {
    levels_.clear();
    migrated_.clear();
    legacyDirs_.clear();
    recovered_ = false;
    readOnly_ = false;
    if (file_.empty()) {
        file_ = Paths::savesDir() / "bests.json";
        if (replayDir_.empty()) replayDir_ = Paths::userDir() / "ghosts" / "bests";
        if (legacy_.segments.empty()) {
            legacy_.segments = Paths::savesDir() / "segments.json";
            legacy_.segmentReplays = Paths::userDir() / "ghosts" / "golds";
        }
        if (legacy_.training.empty()) {
            legacy_.training = Paths::savesDir() / "training.json";
            legacy_.trainingReplays = Paths::userDir() / "ghosts" / "training";
        }
    }
    std::error_code ec;
    if (fs::exists(file_, ec)) {
        std::optional<Json> j;
        if (auto text = FileUtil::readText(file_)) {
            try {
                j = Json::parse(*text);
            } catch (const Json::exception&) {
            }
        }
        const int version = j && j->is_object() ? jget<int>(*j, "version", 0) : 0;
        if (!j || !j->is_object() || jstr(*j, "format") != "cosmic.bests" || version < 1 || !j->contains("levels") || !(*j)["levels"].is_object()) {
            // Unreadable: set aside (never lost); the old files are read again into a new one.
            fs::path bad = file_;
            bad += ".corrupt";
            fs::remove(bad, ec);
            fs::rename(file_, bad, ec);
            recovered_ = true;
        } else {
            readOnly_ = version > kVersion;  // a newer game's file: read what is known, never written
            if (auto m = readIds(jfield(*j, "migrated", Json::array()), std::string::npos, 16)) migrated_ = *m;
            for (const auto& [key, lj] : (*j)["levels"].items()) {
                if (levels_.size() >= kLevelsKept) break;
                if (key.empty() || !validId(key) || !lj.is_object()) continue;
                Level l;
                l.touched = jstr(lj, "touched").substr(0, 32);
                if (lj.contains("route"))
                    if (auto r = readIds(lj["route"])) {
                        std::set<std::string> seen(r->begin(), r->end());
                        if (seen.size() == r->size()) {
                            l.route = std::move(*r);
                            l.routeKnown = true;
                            l.routeGiven = jget<bool>(lj, "route_given", false);
                        }
                    }
                for (const Json& rj : jfield(lj, "ranges", Json::array())) {
                    if (!rj.is_object() || l.ranges.size() >= kRangesPerLevel) continue;
                    if (!rj.contains("from") || !rj.contains("to") || !rj["from"].is_string() || !rj["to"].is_string()) continue;
                    Range r{rj["from"].get<std::string>(), rj["to"].get<std::string>()};
                    if (!validId(r.first) || !validId(r.second)) continue;
                    Record rec;
                    rec.best = jget<float>(rj, "best", -1.0f);
                    if (!validSeconds(rec.best)) rec.best = -1.0f;
                    rec.passages = std::clamp(jget<int>(rj, "count", 0), 0, 10000000);
                    rec.source = bestSourceFromId(jstr(rj, "source", "run")).value_or(BestSource::Run);
                    rec.date = jstr(rj, "date").substr(0, 32);
                    if (rec.best > 0.0f) {
                        rec.passages = std::max(rec.passages, 1);
                        rec.replay = readReplay(jfield(rj, "replay"), rec.best);
                    } else if (rec.passages == 0) {
                        continue;
                    }
                    l.ranges[r] = std::move(rec);
                }
                if (auto pb = readRun(jfield(lj, "pb"))) l.pb = std::move(*pb);
                for (const Json& rj : jfield(lj, "runs", Json::array())) {
                    if (l.runs.size() >= kRunsKept) break;
                    if (auto run = readRun(rj)) l.runs.push_back(std::move(*run));
                }
                for (const Json& hj : jfield(lj, "history", Json::array())) {
                    if (!hj.is_array() || hj.size() != 3 || !hj[0].is_string() || !hj[1].is_number() || !hj[2].is_number()) continue;
                    SobPoint pt{hj[0].get<std::string>().substr(0, 10), hj[1].get<float>(), hj[2].get<float>()};
                    if (!validSeconds(pt.sob)) pt.sob = -1.0f;
                    if (!validSeconds(pt.pb)) pt.pb = -1.0f;
                    if (l.history.size() < kHistoryKept) l.history.push_back(std::move(pt));
                }
                for (const Json& pj : jfield(lj, "powers", Json::array())) {
                    if (!pj.is_object() || !pj.contains("from") || !pj.contains("to") || !pj["from"].is_string() || !pj["to"].is_string()) continue;
                    Range r{pj["from"].get<std::string>(), pj["to"].get<std::string>()};
                    auto ids = readIds(jfield(pj, "powers", Json::array()), std::string::npos, 8);
                    if (validId(r.first) && validId(r.second) && ids && l.powers.size() < kRangesPerLevel) l.powers[r] = std::move(*ids);
                }
                if (auto ids = readIds(jfield(lj, "online", Json::array()), std::string::npos, 64)) l.onlineIds = std::move(*ids);
                if (const Json& gj = jfield(lj, "legacy"); gj.is_object()) {
                    Legacy0 g;
                    g.best = readFloats(jfield(gj, "best", Json::array()), 256);
                    g.date = jstr(gj, "date").substr(0, 32);
                    const Json& rs = jfield(gj, "replays", Json::array());
                    for (size_t k = 0; k < g.best.size(); ++k) {
                        if (!validSeconds(g.best[k])) g.best[k] = -1.0f;
                        g.replays.push_back(rs.is_array() && k < rs.size() ? readReplay(rs[k], g.best[k]) : BestReplay{});
                    }
                    if (!g.best.empty()) l.legacy = std::move(g);
                }
                levels_[key] = std::move(l);
            }
        }
    }
    if (readOnly_) return;
    // The old files, merged once (their replays copied here; their folders removed once this
    // file names the copies).
    const size_t before = migrated_.size();
    auto done = [&](const char* what) { return std::find(migrated_.begin(), migrated_.end(), what) != migrated_.end(); };
    if (!legacy_.segments.empty() && !done("segments.json")) migrateSegments();
    if (!legacy_.training.empty() && !done("training.json")) migrateTraining();
    if (migrated_.size() != before && save()) {
        for (const fs::path& d : legacyDirs_) fs::remove_all(d, ec);
        legacyDirs_.clear();
    }
}

void PersonalBests::migrateSegments() {
    migrated_.push_back("segments.json");
    std::error_code ec;
    if (!fs::exists(legacy_.segments, ec)) return;
    auto j = FileUtil::loadJson(legacy_.segments);
    if (!j || !j->is_object() || !(*j)["levels"].is_object()) return;
    if (!legacy_.segmentReplays.empty()) legacyDirs_.push_back(legacy_.segmentReplays);
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (key.empty() || key.size() > 96 || !validId(key) || !lj.is_object()) continue;
        // The runs kept, oldest first (the most recent ends first in the list).
        std::vector<Run> runs;
        for (const Json& rj : jfield(lj, "runs", Json::array()))
            if (auto r = readRun(rj); r && runs.size() < kRunsKept) runs.push_back(std::move(*r));
        for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
            BestAttempt a;
            a.passage = BestPassage::ofSplits(it->splits, it->checkpoints, it->time);
            a.source = BestSource::Run;
            a.date = it->date;
            add(key, a);
        }
        // The gold segments, with their replays (copied once each).
        const size_t splitCount = static_cast<size_t>(std::clamp(jget<int>(lj, "best_split_count", 0), 0, 255));
        std::vector<float> best = readFloats(jfield(lj, "best", Json::array()), 256);
        if (best.empty()) continue;
        best.resize(splitCount + 1, -1.0f);
        for (float& b : best)
            if (!validSeconds(b)) b = -1.0f;
        std::optional<std::vector<std::string>> ids;
        if (lj.contains("best_checkpoints")) ids = readIds(lj["best_checkpoints"], splitCount);
        if (!ids && splitCount == 0) ids = std::vector<std::string>{};
        const std::string touched = jstr(lj, "touched").substr(0, 32);
        std::map<std::string, std::string> copied;  // old name → new name
        std::vector<BestReplay> replays(best.size());
        const Json& sj = jfield(lj, "best_sources", Json::array());
        for (size_t i = 0; sj.is_array() && i < sj.size() && i < best.size(); ++i) {
            BestReplay r = readReplay(Json{{"file", jstr(sj[i], "replay")}, {"start", jget<float>(sj[i], "start", -1.0f)}, {"end", jget<float>(sj[i], "end", -1.0f)}},
                                      best[i]);
            if (!r.has()) continue;
            auto c = copied.find(r.file);
            if (c == copied.end()) {
                std::string name;
                const fs::path from = keptreplays::levelDir(legacy_.segmentReplays, key) / r.file;
                const uint64_t n = keptreplays::fileSize(from);
                if (n > 0 && n <= keptreplays::kMaxFileBytes)
                    if (auto bytes = FileUtil::readBinary(from)) name = keepReplay(key, *bytes, jstr(sj[i], "date", touched));
                c = copied.emplace(r.file, name).first;
            }
            if (c->second.empty()) continue;
            r.file = c->second;
            replays[i] = std::move(r);
        }
        Level& l = level(key, touched);
        const float sobBefore = sobOf(l), pbBefore = l.pb ? l.pb->time : -1.0f;
        if (ids) {
            // Its checkpoints known: each gold becomes the best of its segment when it is better.
            std::vector<std::string> route = *ids;
            for (size_t k = 0; k < best.size(); ++k) {
                const std::string date = sj.is_array() && k < sj.size() && sj[k].is_object() ? jstr(sj[k], "date", touched) : touched;
                offer(l, segmentOf(route, k), best[k], BestSource::Run, date, replays[k], false);
            }
            if (!l.routeKnown) {
                l.route = route;
                l.routeKnown = true;
            }
        } else {
            // Not known yet: kept until the route tells which segment is which (setRoute).
            l.legacy = Legacy0{best, replays, touched};
            resolveLegacy(l);
        }
        noteHistory(l, touched, sobBefore, pbBefore);
    }
}

void PersonalBests::migrateTraining() {
    migrated_.push_back("training.json");
    std::error_code ec;
    if (!fs::exists(legacy_.training, ec)) return;
    auto j = FileUtil::loadJson(legacy_.training);
    if (!j || !j->is_object() || jstr(*j, "format") != "cosmic.training" || !(*j)["levels"].is_object()) return;
    if (!legacy_.trainingReplays.empty()) legacyDirs_.push_back(legacy_.trainingReplays);
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (key.empty() || !validId(key) || !lj.is_object()) continue;
        const std::string touched = jstr(lj, "touched").substr(0, 32);
        for (const Json& sj : jfield(lj, "segments", Json::array())) {
            if (!sj.is_object() || !sj.contains("from") || !sj.contains("to") || !sj["from"].is_string() || !sj["to"].is_string()) continue;
            const Range r{sj["from"].get<std::string>(), sj["to"].get<std::string>()};
            if (!validId(r.first) || !validId(r.second)) continue;
            Level& l = level(key, touched);
            const float sobBefore = sobOf(l), pbBefore = l.pb ? l.pb->time : -1.0f;
            const float best = jget<float>(sj, "best", -1.0f);
            const std::string date = jstr(sj, "date", touched).substr(0, 32);
            if (validSeconds(best)) {
                BestReplay rep;
                const std::string name = jstr(sj, "replay");
                auto start = SectionTrainingStart::fromJson(jfield(sj, "start", Json()));
                if (keptreplays::validName(name) && start && start->checkpointId == r.first) {
                    const fs::path from = keptreplays::levelDir(legacy_.trainingReplays, key) / name;
                    const uint64_t n = keptreplays::fileSize(from);
                    if (n > 0 && n <= keptreplays::kMaxFileBytes)
                        if (auto bytes = FileUtil::readBinary(from)) rep.file = keepReplay(key, *bytes, date);
                    if (rep.has()) {
                        // The attempt's chronometer started at its checkpoint: the segment is all of it.
                        rep.start = 0.0f;
                        rep.end = best;
                        rep.fromLevelStart = r.first.empty() && !start->hasSpawn && start->powers.empty() && !start->powersChosen;
                        if (!rep.fromLevelStart) rep.setup = *start;
                    }
                }
                offer(l, r, best, BestSource::Training, date, rep, false);
                if (auto it = l.ranges.find(r); it != l.ranges.end())
                    it->second.passages = std::min(it->second.passages + std::clamp(jget<int>(sj, "attempts", 1), 1, 1000000) - 1, 10000000);
            }
            if (sj.contains("powers"))
                if (auto ids = readIds(sj["powers"], std::string::npos, 8)) l.powers[r] = std::move(*ids);
            noteHistory(l, date, sobBefore, pbBefore);
        }
    }
}

bool PersonalBests::save() {
    if (file_.empty() || readOnly_) return false;
    Json levels = Json::object();
    for (const auto& [key, l] : levels_) {
        Json lj{{"touched", l.touched}};
        if (l.routeKnown) {
            lj["route"] = l.route;
            lj["route_given"] = l.routeGiven;
        }
        Json ranges = Json::array();
        for (const auto& [r, rec] : l.ranges) {
            Json rj{{"from", r.first}, {"to", r.second}, {"best", rec.best}, {"count", rec.passages}, {"source", bestSourceId(rec.source)},
                    {"date", rec.date}};
            if (rec.replay.has()) rj["replay"] = replayJson(rec.replay);
            ranges.push_back(std::move(rj));
        }
        lj["ranges"] = std::move(ranges);
        if (l.pb) lj["pb"] = runJson(*l.pb);
        Json runs = Json::array();
        for (const Run& r : l.runs) runs.push_back(runJson(r));
        lj["runs"] = std::move(runs);
        Json hist = Json::array();
        for (const SobPoint& p : l.history) hist.push_back(Json{p.day, p.sob, p.pb});
        lj["history"] = std::move(hist);
        if (!l.powers.empty()) {
            Json pw = Json::array();
            for (const auto& [r, ids] : l.powers) pw.push_back(Json{{"from", r.first}, {"to", r.second}, {"powers", ids}});
            lj["powers"] = std::move(pw);
        }
        if (!l.onlineIds.empty()) lj["online"] = l.onlineIds;
        if (l.legacy) {
            Json rs = Json::array();
            for (const BestReplay& r : l.legacy->replays) rs.push_back(r.has() ? replayJson(r) : Json(nullptr));
            lj["legacy"] = Json{{"best", l.legacy->best}, {"date", l.legacy->date}, {"replays", rs}};
        }
        levels[key] = std::move(lj);
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    Json root{{"format", "cosmic.bests"}, {"version", kVersion}, {"migrated", migrated_}, {"levels", levels}};
    const bool ok = FileUtil::writeTextAtomic(file_, root.dump(1, ' ', false, Json::error_handler_t::replace) + "\n");
    // Only once the file that names them is written: a replay is never deleted while it is named.
    if (ok) collectReplays();
    return ok;
}

void PersonalBests::collectReplays() {
    if (replayDir_.empty()) return;
    std::map<std::string, std::set<std::string>> used;
    std::map<std::string, std::string> keyOf;  // folder → level key
    for (const auto& [key, l] : levels_) {
        const std::string dir = keptreplays::safeName(key, 96);
        if (dir.empty()) continue;
        keyOf[dir] = key;
        for (const auto& [r, rec] : l.ranges)
            if (rec.replay.has()) used[dir].insert(rec.replay.file);
        if (l.legacy)
            for (const BestReplay& b : l.legacy->replays)
                if (b.has()) used[dir].insert(b.file);
    }
    auto sizes = keptreplays::collect(replayDir_, used);
    // Bounded: beyond kMaxReplayBytes, the least recently changed levels lose their replays.
    uint64_t total = 0;
    for (const auto& s : sizes) total += s.second;
    if (total <= kMaxReplayBytes) return;
    auto touched = [&](const std::string& dir) {
        auto k = keyOf.find(dir);
        auto it = k == keyOf.end() ? levels_.end() : levels_.find(k->second);
        return it == levels_.end() ? std::string{} : it->second.touched;
    };
    std::sort(sizes.begin(), sizes.end(), [&](const auto& a, const auto& b) { return touched(a.first) < touched(b.first); });
    bool dropped = false;
    for (const auto& [dir, bytes] : sizes) {
        if (total <= kMaxReplayBytes) break;
        if (auto k = keyOf.find(dir); k != keyOf.end())
            if (auto it = levels_.find(k->second); it != levels_.end()) {
                for (auto& [r, rec] : it->second.ranges) rec.replay = BestReplay{};
                if (it->second.legacy)
                    for (BestReplay& b : it->second.legacy->replays) b = BestReplay{};
            }
        std::error_code ec;
        fs::remove_all(replayDir_ / Paths::fromUtf8(dir), ec);
        total -= std::min(total, bytes);
        dropped = true;
    }
    if (dropped) save();  // the file must stop naming them
}

}  // namespace cosmic
