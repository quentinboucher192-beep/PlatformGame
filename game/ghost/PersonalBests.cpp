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

// The statistics' segment bests follow the same rules (game/records/PlayStats.h).
static_assert(PersonalBests::kRules == kSegmentRules, "the rules of a segment: one generation for the bests and the statistics");

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

/// A boundary an attempt crossed, with its chronometer there.
struct Boundary {
    std::string id;
    float t = 0.0f;
    int kind = 1;  // 0 the level's start, 1 a checkpoint, 2 the exit
};

/// The boundaries a passage crossed, in order: its start (the level's start, a training's
/// checkpoint; none after a StartPos), its checkpoints, the exit. A damaged entry (a time going
/// back, a checkpoint met twice) ends the list there.
std::vector<Boundary> boundariesOf(const BestPassage& p) {
    std::vector<Boundary> bs;
    if (p.from == BestPassage::From::LevelStart) bs.push_back({{}, 0.0f, 0});
    else if (p.from == BestPassage::From::Checkpoint) {
        if (p.startCheckpoint.empty() || !validId(p.startCheckpoint)) return {};
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
        if (std::any_of(bs.begin(), bs.end(), [&](const Boundary& b) { return b.kind == 1 && b.id == id; })) {
            whole = false;  // reached twice: not a run through the route
            break;
        }
        bs.push_back({id, t, 1});
        prev = t;
    }
    if (whole && validSeconds(p.exitTime) && p.exitTime >= prev) bs.push_back({{}, p.exitTime, 2});
    return bs;
}

/// 2.95 — the ranges (i, j) of `bs` an attempt counts (docs/GHOST_RACING.md § « Règles d'un
/// segment »):
///  * boundaries that follow each other on the route, none skipped, in the route's order (on
///    the order crossed while the route is not known);
///  * entered by playing the whole segment before it in this attempt (its start boundary
///    crossed right after the one before it on the route), or from the level's start when the
///    attempt began there, or first entered after a StartPos (an attempt from one plays on from
///    the real state of the SOB's replay there: the segment it lies in never counts, the next
///    one entered does) — never from where a training appeared (its checkpoint);
///  * never in a training's run-up (before `countFrom`);
///  * checkpoints unknown (an old run): only the whole run, from the start to the exit.
std::vector<std::pair<size_t, size_t>> countedSpans(const std::vector<Boundary>& bs, const BestPassage& p, const std::vector<std::string>* route) {
    std::vector<std::pair<size_t, size_t>> out;
    if (bs.size() < 2) return out;
    if (!p.checkpointsKnown) {
        if (bs.front().kind == 0 && bs.back().kind == 2) out.push_back({0, bs.size() - 1});
        return out;
    }
    // Where each boundary lies: on the route (0 the start, k the k-th checkpoint, n + 1 the exit,
    // -1 off it); without it, in the order crossed.
    std::vector<int> pos(bs.size(), -1);
    int first = 0;  // the first place a counted range may start from
    if (route) {
        const int n = static_cast<int>(route->size());
        auto at = [&](const std::string& id) {
            auto it = std::find(route->begin(), route->end(), id);
            return it == route->end() ? -1 : static_cast<int>(it - route->begin()) + 1;
        };
        for (size_t k = 0; k < bs.size(); ++k) pos[k] = bs[k].kind == 0 ? 0 : bs[k].kind == 2 ? n + 1 : at(bs[k].id);
        if (p.countFrom) first = p.countFrom->empty() ? 0 : at(*p.countFrom);
    } else {
        const int off = bs.front().kind == 0 ? 0 : 1;
        for (size_t k = 0; k < bs.size(); ++k) pos[k] = static_cast<int>(k) + off;
        if (p.countFrom) {
            first = p.countFrom->empty() ? 0 : -1;
            for (size_t k = 0; k < bs.size(); ++k)
                if (bs[k].kind == 1 && bs[k].id == *p.countFrom) first = pos[k];
        }
    }
    if (first < 0) return out;  // its run-up ends off the route: nothing to count
    for (size_t i = 0; i + 1 < bs.size(); ++i) {
        if (pos[i] < first) continue;
        // Entered whole: from the level's start, right after the boundary before it, or the first
        // one crossed after a StartPos (on the SOB path: the real state of a run there).
        const bool entered = bs[i].kind == 0 || (i == 0 && p.from == BestPassage::From::Elsewhere) ||
                             (i >= 1 && pos[i - 1] >= 0 && pos[i - 1] == pos[i] - 1);
        if (!entered) continue;
        for (size_t j = i + 1; j < bs.size(); ++j) {
            if (pos[j] != pos[i] + static_cast<int>(j - i)) break;  // a boundary skipped (or off the route): none further
            out.push_back({i, j});
        }
    }
    return out;
}

/// The attempt started as the level does (its replay plays from the level's start as it is).
bool levelOwnStart(const SectionTrainingStart& s) {
    return !s.startPos && !s.hasSpawn && s.checkpointId.empty() && s.powers.empty() && !s.powersChosen;
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

std::string PersonalBests::keepReplay(const std::string& key, const std::vector<uint8_t>& bytes, const std::string& date) const {
    return keptreplays::write(replayDir_, key, bytes, date);
}

PersonalBests::Added PersonalBests::add(const std::string& levelKey, const BestAttempt& a) {
    Added out;
    if (levelKey.empty() || !validId(levelKey)) return out;
    const BestPassage& p = a.passage;
    const std::vector<Boundary> bs = boundariesOf(p);
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
    for (const Boundary& b : bs)
        if (b.kind == 1) ids.push_back(b.id);
    // The route, learned from a full run while the level has not given it (the one through the
    // most checkpoints).
    if (fullRun && p.checkpointsKnown && !l.routeGiven && (!l.routeKnown || ids.size() > l.route.size())) {
        l.route = ids;
        l.routeKnown = true;
    }
    // Its replay starts as the attempt did: the level's start as it is, else its setup.
    const bool ownStart = p.from == BestPassage::From::LevelStart && levelOwnStart(a.setup);
    std::string replayName;
    bool replayWritten = false;
    for (const auto& [i, j] : countedSpans(bs, p, l.routeKnown ? &l.route : nullptr)) {
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
            rep.fromLevelStart = ownStart;
            if (!rep.fromLevelStart) rep.setup = a.setup;
        }
        if (offer(l, r, secs, a.source, a.date, rep, count) && better) out.newBests.push_back(r);
    }
    if (fullRun) {
        Run run;
        run.date = a.date.substr(0, 32);
        for (const Boundary& b : bs)
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

std::vector<PersonalBests::Span> PersonalBests::counted(const std::string& levelKey, const BestPassage& p) const {
    std::vector<Span> out;
    const std::vector<Boundary> bs = boundariesOf(p);
    auto it = levels_.find(levelKey);
    const std::vector<std::string>* route = it != levels_.end() && it->second.routeKnown ? &it->second.route : nullptr;
    for (const auto& [i, j] : countedSpans(bs, p, route))
        out.push_back({{bs[i].kind == 0 ? std::string{} : bs[i].id, bs[j].kind == 2 ? std::string{} : bs[j].id}, bs[i].t, bs[j].t});
    return out;
}

void PersonalBests::resetRules() {
    for (auto& [key, l] : levels_) {
        const std::map<Range, Record> old = std::move(l.ranges);
        l.ranges.clear();
        // The Sum of Best of before is forgotten (its points; the PB's stay).
        for (SobPoint& pt : l.history) pt.sob = -1.0f;
        l.history.erase(std::remove_if(l.history.begin(), l.history.end(), [](const SobPoint& pt) { return pt.sob < 0.0f && pt.pb < 0.0f; }),
                        l.history.end());
        // The full runs kept (from the level's start: they follow the rules) give their ranges
        // again, the oldest first (a tie keeps the older one), each run once.
        std::vector<const Run*> list;
        for (const Run& r : l.runs) list.push_back(&r);
        if (l.pb && std::none_of(l.runs.begin(), l.runs.end(), [&](const Run& r) { return r.date == l.pb->date && ms(r.time) == ms(l.pb->time); }))
            list.push_back(&*l.pb);
        std::stable_sort(list.begin(), list.end(), [](const Run* x, const Run* y) { return x->date < y->date; });
        for (const Run* r : list) {
            const BestPassage p = BestPassage::ofSplits(r->splits, r->checkpoints, r->time);
            const std::vector<Boundary> bs = boundariesOf(p);
            for (const auto& [i, j] : countedSpans(bs, p, l.routeKnown ? &l.route : nullptr)) {
                const Range range{bs[i].kind == 0 ? std::string{} : bs[i].id, bs[j].kind == 2 ? std::string{} : bs[j].id};
                offer(l, range, bs[j].t - bs[i].t, r->source, r->date, {}, true);
            }
        }
        // A best given again exactly as it was by a full run: its replay stays with it.
        for (auto& [range, rec] : l.ranges) {
            auto o = old.find(range);
            if (o == old.end() || !o->second.replay.has() || ms(o->second.best) != ms(rec.best)) continue;
            if (o->second.source == BestSource::Run || o->second.source == BestSource::Online) rec.replay = o->second.replay;
        }
        // The whole run's record is a full run's: never removed (a better one than the runs kept
        // give, with its replay), its arrivals never fewer.
        const Range whole{std::string{}, std::string{}};
        if (auto o = old.find(whole); o != old.end() && validSeconds(o->second.best) &&
                                      (o->second.source == BestSource::Run || o->second.source == BestSource::Online)) {
            auto n = l.ranges.find(whole);
            if (n == l.ranges.end() || ms(o->second.best) < ms(n->second.best)) {
                const int passages = n == l.ranges.end() ? 0 : n->second.passages;
                Record kept = o->second;
                kept.passages = std::max(kept.passages, passages);
                l.ranges[whole] = std::move(kept);
            } else {
                n->second.passages = std::max(n->second.passages, o->second.passages);
                if (!n->second.replay.has() && ms(o->second.best) == ms(n->second.best)) n->second.replay = o->second.replay;
            }
        }
    }
    rulesReset_ = true;
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
        // 2.95: segments timed by the rules of before (a respawn could start one) are never taken;
        // the PB (a full run's time) is.
        const bool rules = r.sobRules >= kRules;
        bool any = validSeconds(r.pb);
        if (rules)
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
            }
        }
        for (const SegmentRec& s : rules ? r.segments : std::vector<SegmentRec>{}) {
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
    rulesReset_ = false;
    bool oldRules = false;
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
            // 2.95: the generation of the rules its ranges were taken with (none: before 2.95).
            oldRules = jget<int>(*j, "rules", 1) < kRules && !readOnly_;
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
                // (2.91 – 2.94 « legacy »: gold segments of segments.json waiting for their route,
                // of the rules before: never read again.)
                levels_[key] = std::move(l);
            }
        }
    }
    if (readOnly_) return;
    // 2.95: the rules changed: every range of before goes, the full runs kept give theirs again
    // (written at once: the replays no best refers to any more are then deleted).
    if (oldRules) resetRules();
    // The old files, merged once (their full runs; their replay folders removed once this file
    // is written).
    const size_t before = migrated_.size();
    auto done = [&](const char* what) { return std::find(migrated_.begin(), migrated_.end(), what) != migrated_.end(); };
    if (!legacy_.segments.empty() && !done("segments.json")) migrateSegments();
    if (!legacy_.training.empty() && !done("training.json")) migrateTraining();
    if ((migrated_.size() != before || rulesReset_) && save()) {
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
        // The full runs kept, oldest first (the most recent ends first in the list): from the
        // level's start, they give their segments by the rules of now. 2.95: the gold segments of
        // that file (« best », timed by the rules of before) are not read.
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
    }
}

void PersonalBests::migrateTraining() {
    migrated_.push_back("training.json");
    std::error_code ec;
    if (!fs::exists(legacy_.training, ec)) return;
    auto j = FileUtil::loadJson(legacy_.training);
    if (!j || !j->is_object() || jstr(*j, "format") != "cosmic.training" || !(*j)["levels"].is_object()) return;
    if (!legacy_.trainingReplays.empty()) legacyDirs_.push_back(legacy_.trainingReplays);
    // 2.95: its best times (trained from a standstill at the segment's own checkpoint, by the
    // rules of before) are not read; only the powers chosen for a segment (a preference).
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (key.empty() || !validId(key) || !lj.is_object()) continue;
        const std::string touched = jstr(lj, "touched").substr(0, 32);
        for (const Json& sj : jfield(lj, "segments", Json::array())) {
            if (!sj.is_object() || !sj.contains("from") || !sj.contains("to") || !sj["from"].is_string() || !sj["to"].is_string()) continue;
            const Range r{sj["from"].get<std::string>(), sj["to"].get<std::string>()};
            if (!validId(r.first) || !validId(r.second) || !sj.contains("powers")) continue;
            if (auto ids = readIds(sj["powers"], std::string::npos, 8)) {
                Level& l = level(key, touched);
                if (l.powers.count(r) || l.powers.size() < kRangesPerLevel) l.powers[r] = std::move(*ids);
            }
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
        levels[key] = std::move(lj);
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    Json root{{"format", "cosmic.bests"}, {"version", kVersion}, {"rules", kRules}, {"migrated", migrated_}, {"levels", levels}};
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
            if (auto it = levels_.find(k->second); it != levels_.end())
                for (auto& [r, rec] : it->second.ranges) rec.replay = BestReplay{};
        std::error_code ec;
        fs::remove_all(replayDir_ / Paths::fromUtf8(dir), ec);
        total -= std::min(total, bytes);
        dropped = true;
    }
    if (dropped) save();  // the file must stop naming them
}

}  // namespace cosmic
