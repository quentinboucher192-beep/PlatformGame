// 2.55 — the statistics of another computer added to this one's (a copy of the progression
// restored after « Migrer vers un nouveau compte »), without counting anything twice. The rules
// are in PlayStats.h (mergePlayStats); each one gives back the same result when the same copy is
// merged again (tests/test_play_stats.cpp).

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include "game/records/PlayStats.h"

namespace cosmic {

using namespace playstats;

namespace {

int monthOfDay(int day) {
    int y, m, d;
    civilFromDays(day, y, m, d);
    return y * 12 + m - 1;
}

int centis(float s) { return static_cast<int>(std::lround(static_cast<double>(s) * 100.0)); }

// ------------------------------------------------------------------ counters: the highest of both

StatsCounters highest(const StatsCounters& a, const StatsCounters& b) {
    StatsCounters c = a;
    c.playSec = std::max(a.playSec, b.playSec);
    c.trainSec = std::max(a.trainSec, b.trainSec);
    c.deaths = std::max(a.deaths, b.deaths);
    c.threats = std::max(a.threats, b.threats);
    c.dodged = std::max(a.dodged, b.dodged);
    c.attacks = std::max(a.attacks, b.attacks);
    c.hits = std::max(a.hits, b.hits);
    for (const auto& [id, v] : b.powers) {
        auto& m = c.powers[id];
        m[0] = std::max(m[0], v[0]);
        m[1] = std::max(m[1], v[1]);
    }
    for (size_t i = 0; i < c.hurt.size(); ++i) c.hurt[i] = std::max(a.hurt[i], b.hurt[i]);
    c.stars = std::max(a.stars, b.stars);
    return c;
}

LevelSlices mergeSlices(LevelSlices a, LevelSlices b) {
    const int stamp = std::max(a.stamp, b.stamp);
    a.decayTo(stamp);
    b.decayTo(stamp);
    for (size_t k = 0; k < static_cast<size_t>(kSlices); ++k) {
        a.passes[k] = std::max(a.passes[k], b.passes[k]);
        a.deaths[k] = std::max(a.deaths[k], b.deaths[k]);
        a.wPasses[k] = std::max(a.wPasses[k], b.wPasses[k]);
        a.wDeaths[k] = std::max(a.wDeaths[k], b.wDeaths[k]);
    }
    a.stamp = stamp;
    a.lastDay = std::max(a.lastDay, b.lastDay);
    if (a.name.empty()) a.name = b.name;
    return a;
}

// ------------------------------------------------------------------ lists with an identity

/// Two lists, oldest first, each with the total it counted (its items and the older ones it no
/// longer keeps). What can be told apart: the items of a side, unless it is older than the other
/// side's list (when that side no longer keeps everything) and not in it — an older item of the
/// other side might be one it counted. United by identifier, ordered by day (on the same day,
/// `mine` first, each side's order kept).
template <class T>
struct Union {
    std::vector<T> items;
    std::vector<T> setAside;  // items of either side that could not be told apart
    int restMine = 0, restTheirs = 0;  // each side's total minus what it shows in `items`
};

template <class T, class Id>
Union<T> unite(const std::vector<T>& a, int totalA, const std::vector<T>& b, int totalB, Id idOf) {
    Union<T> u;
    const bool completeA = totalA <= static_cast<int>(a.size()), completeB = totalB <= static_cast<int>(b.size());
    // Without any item, a side that counted some keeps no list: nothing of the other side older
    // than now can be told apart from them.
    const int cutA = completeA ? INT_MIN : (a.empty() ? INT_MAX : a.front().day);
    const int cutB = completeB ? INT_MIN : (b.empty() ? INT_MAX : b.front().day);
    std::set<decltype(idOf(a.front(), 0))> idsA, idsB;
    std::vector<decltype(idOf(a.front(), 0))> keyA, keyB;
    for (size_t i = 0; i < a.size(); ++i) keyA.push_back(idOf(a[i], i)), idsA.insert(keyA.back());
    for (size_t i = 0; i < b.size(); ++i) keyB.push_back(idOf(b[i], i)), idsB.insert(keyB.back());
    std::vector<T> ka, kb;
    int shownA = 0, shownB = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const bool inB = idsB.count(keyA[i]) > 0;
        if (inB || a[i].day > cutB) {
            ka.push_back(a[i]);
            ++shownA;
        } else {
            u.setAside.push_back(a[i]);
        }
    }
    for (size_t i = 0; i < b.size(); ++i) {
        if (idsA.count(keyB[i])) {
            ++shownB;  // the same item: shown by mine's copy (always kept, being in both lists)
            continue;
        }
        if (b[i].day > cutA) {
            kb.push_back(b[i]);
            ++shownB;
        } else {
            u.setAside.push_back(b[i]);
        }
    }
    // Ordered by day (stable: on the same day, mine first, each side in its own order).
    size_t i = 0, j = 0;
    while (i < ka.size() || j < kb.size()) {
        const bool takeA = j >= kb.size() || (i < ka.size() && ka[i].day <= kb[j].day);
        u.items.push_back(takeA ? ka[i++] : kb[j++]);
    }
    u.restMine = std::max(0, totalA - shownA);
    u.restTheirs = std::max(0, totalB - shownB);
    return u;
}

// ------------------------------------------------------------------ bosses

BossHistory mergeBoss(const BossHistory& a, const BossHistory& b) {
    BossHistory m;
    m.name = a.name.empty() ? b.name : a.name;
    m.phases = std::max({1, a.phases, b.phases});
    const auto idOf = [](const BossAttemptRec& r, size_t) { return r.key(); };
    Union<BossAttemptRec> u = unite(a.recent, a.attempts, b.recent, b.attempts, idOf);
    m.recent = u.items;
    const int rest = std::max(u.restMine, u.restTheirs);
    m.attempts = rest + static_cast<int>(m.recent.size());

    // What a list says, and what a side counted beyond what it shows in the merged list.
    const size_t P = static_cast<size_t>(m.phases);
    struct Counts {
        int wins = 0;
        std::vector<int> reached, defeats;
        std::map<std::string, int> fatal;
    };
    auto countOf = [&](const std::vector<BossAttemptRec>& list, auto keep) {
        Counts c;
        c.reached.assign(P, 0);
        c.defeats.assign(P, 0);
        for (size_t i = 0; i < list.size(); ++i) {
            if (!keep(i)) continue;
            const BossAttemptRec& r = list[i];
            const size_t ph = static_cast<size_t>(std::clamp(r.phase, 0, m.phases - 1));
            if (r.won) c.wins++;
            for (size_t p = 0; p <= ph; ++p) c.reached[p]++;  // phases are reached in order
            if (!r.won && !r.attack.empty()) {
                c.defeats[ph]++;
                c.fatal[r.attack]++;
            }
        }
        return c;
    };
    // The items of each side that the merged list shows (by identifier).
    std::set<uint64_t> shown;
    for (const auto& r : m.recent) shown.insert(r.key());
    const Counts all = countOf(m.recent, [](size_t) { return true; });
    const Counts inA = countOf(a.recent, [&](size_t i) { return shown.count(a.recent[i].key()) > 0; });
    const Counts inB = countOf(b.recent, [&](size_t i) { return shown.count(b.recent[i].key()) > 0; });
    auto restOf = [](int total, int shownCount) { return std::max(0, total - shownCount); };
    m.wins = std::max(restOf(a.wins, inA.wins), restOf(b.wins, inB.wins)) + all.wins;
    m.reached.assign(P, 0);
    m.defeats.assign(P, 0);
    for (size_t p = 0; p < P; ++p) {
        const int ra = p < a.reached.size() ? a.reached[p] : 0, rb = p < b.reached.size() ? b.reached[p] : 0;
        const int da = p < a.defeats.size() ? a.defeats[p] : 0, db = p < b.defeats.size() ? b.defeats[p] : 0;
        m.defeats[p] = std::max(restOf(da, inA.defeats[p]), restOf(db, inB.defeats[p])) + all.defeats[p];
        m.reached[p] = std::max(std::max(restOf(ra, inA.reached[p]), restOf(rb, inB.reached[p])) + all.reached[p], m.defeats[p]);
    }
    std::set<std::string> attacks;
    for (const auto& [k, v] : a.fatal) attacks.insert(k);
    for (const auto& [k, v] : b.fatal) attacks.insert(k);
    for (const auto& [k, v] : all.fatal) attacks.insert(k);
    for (const std::string& k : attacks) {
        auto get = [&](const std::map<std::string, int>& f) {
            auto it = f.find(k);
            return it == f.end() ? 0 : it->second;
        };
        const int n = std::max(restOf(get(a.fatal), get(inA.fatal)), restOf(get(b.fatal), get(inB.fatal))) + get(all.fatal);
        if (n > 0) m.fatal[k] = n;
    }
    m.wins = std::min(m.wins, m.attempts);
    // The best share among what the list no longer shows.
    m.bestBefore = std::max(a.bestBefore, b.bestBefore);
    for (const auto& r : u.setAside) m.bestBefore = std::max(m.bestBefore, r.won ? 1.0f : r.removed);
    // The first victory: in the part a side counted before its list (its own number), or the
    // first one of the merged list.
    int first = 0;
    auto candidate = [&](int n) {
        if (n > 0 && (first == 0 || n < first)) first = n;
    };
    for (const BossHistory* s : {&a, &b})
        if (s->firstWin > 0 && s->firstWin < s->firstKept()) candidate(s->firstWin);
    for (size_t i = 0; i < m.recent.size(); ++i)
        if (m.recent[i].won) {
            candidate(rest + static_cast<int>(i) + 1);
            break;
        }
    if (first == 0 && m.wins > 0) {
        candidate(a.firstWin);
        candidate(b.firstWin);
    }
    m.firstWin = m.wins > 0 ? std::clamp(first, 0, m.attempts) : 0;
    m.lastSeq = std::max(a.lastSeq, b.lastSeq);
    return m;
}

// ------------------------------------------------------------------ classic levels

/// Whether every time of `part` is in `whole` (to the hundredth, as many times).
bool containsTimes(const std::vector<float>& whole, const std::vector<float>& part) {
    std::map<int, int> left;
    for (float t : whole) left[centis(t)]++;
    for (float t : part)
        if (left[centis(t)]-- <= 0) return false;
    return true;
}

/// The times of a segment on both sides (a time has no date): a side that has all the other's
/// times as it is (merged before); otherwise the older side's, then those of the more recent side
/// that it does not have (an equal time, to the hundredth, counts once per occurrence).
std::vector<float> uniteTimes(const std::vector<float>& older, const std::vector<float>& newer) {
    if (containsTimes(newer, older)) return newer;
    if (containsTimes(older, newer)) return older;
    std::map<int, int> left;
    for (float t : older) left[centis(t)]++;
    std::vector<float> out = older;
    for (float t : newer) {
        int& n = left[centis(t)];
        if (n > 0) --n;
        else out.push_back(t);
    }
    return out;
}

/// A full run and its identity: its day, its time to the hundredth, and how many equal ones come
/// before it in its own list.
struct Run {
    int day = 0;
    FinishRec f;
    int64_t id = 0;
};
std::vector<Run> runsOf(const std::vector<FinishRec>& list) {
    std::vector<Run> out;
    std::map<std::pair<int, int>, int> seen;
    for (const FinishRec& f : list) {
        const int k = seen[{f.day, centis(f.time)}]++;
        out.push_back({f.day, f, (static_cast<int64_t>(f.day) << 40) | (static_cast<int64_t>(centis(f.time) & 0xFFFFFFF) << 8) | std::min(k, 255)});
    }
    return out;
}

LevelRuns mergeRuns(const LevelRuns& a, const LevelRuns& b) {
    // Another version of the level: its segments are not the same; the side played the most
    // recently is kept (on the same day: this computer's).
    if (a.version != b.version) return b.lastDay > a.lastDay ? b : a;
    // The side played the most recently is the newer one (on the same day: this computer).
    const bool mineNewer = a.lastDay >= b.lastDay;
    const LevelRuns& older = mineNewer ? b : a;
    const LevelRuns& newer = mineNewer ? a : b;
    LevelRuns m = a;
    m.name = a.name.empty() ? b.name : a.name;
    m.lastDay = std::max(a.lastDay, b.lastDay);
    // 2.95: the bests of a side timed by older rules are not taken (its times are).
    m.sobRules = std::max(a.sobRules, b.sobRules);
    const bool olderBests = older.sobRules >= m.sobRules, newerBests = newer.sobRules >= m.sobRules;
    // Segments: the times united, the best the better one.
    m.segments.clear();
    auto add = [&](const SegmentRec& s) {
        for (auto& x : m.segments)
            if (x.from == s.from && x.to == s.to) return;
        const SegmentRec* o = older.segment(s.from, s.to);
        const SegmentRec* n = newer.segment(s.from, s.to);
        SegmentRec r;
        r.from = s.from;
        r.to = s.to;
        r.times = uniteTimes(o ? o->times : std::vector<float>{}, n ? n->times : std::vector<float>{});
        const SegmentRec* withBests[2] = {olderBests ? o : nullptr, newerBests ? n : nullptr};
        for (const SegmentRec* x : withBests)
            if (x && x->best > 0.0f && (r.best <= 0.0f || x->best < r.best)) r.best = x->best;
        // 2.60.3: the best section-training time, the better one (the Sum of Best only).
        for (const SegmentRec* x : withBests)
            if (x && x->trainBest > 0.0f && (r.trainBest <= 0.0f || x->trainBest < r.trainBest)) r.trainBest = x->trainBest;
        m.segments.push_back(std::move(r));
    };
    for (const auto& s : a.segments) add(s);
    for (const auto& s : b.segments) add(s);
    // Full runs: united by (day, time), the same run once.
    Union<Run> u = unite(runsOf(a.finishes), a.finished, runsOf(b.finishes), b.finished, [](const Run& r, size_t) { return r.id; });
    m.finishes.clear();
    for (const Run& r : u.items) m.finishes.push_back(r.f);
    m.finished = std::max(u.restMine, u.restTheirs) + static_cast<int>(m.finishes.size());
    // The attempts not finished: the highest of both sides.
    m.attempts = m.finished + std::max(std::max(0, a.attempts - a.finished), std::max(0, b.attempts - b.finished));
    // The PB: the better one, with its route.
    if (b.pb > 0.0f && (a.pb <= 0.0f || b.pb < a.pb)) {
        m.pb = b.pb;
        m.pbRoute = b.pbRoute;
    }
    // The history of the PB and the Sum of Best: united, sorted by day (on a day, the better
    // last), then the merged PB / SOB when it is new.
    std::vector<SobPoint> h;
    std::set<std::tuple<int, int, int>> seen;
    for (const LevelRuns* s : {&a, &b})
        for (const auto& p : s->history)
            if (seen.insert({p.day, p.pb > 0.0f ? centis(p.pb) : -1, p.sob > 0.0f ? centis(p.sob) : -1}).second) h.push_back(p);
    std::stable_sort(h.begin(), h.end(), [](const SobPoint& x, const SobPoint& y) {
        if (x.day != y.day) return x.day < y.day;
        const float px = x.pb > 0.0f ? x.pb : 1e9f, py = y.pb > 0.0f ? y.pb : 1e9f;
        if (px != py) return px > py;
        const float sx = x.sob > 0.0f ? x.sob : 1e9f, sy = y.sob > 0.0f ? y.sob : 1e9f;
        return sx > sy;
    });
    m.history = std::move(h);
    const float sob = m.sob();
    auto changed = [](float x, float y) { return (x > 0.0f) != (y > 0.0f) || std::abs(x - y) > 0.005f; };
    if (m.pb > 0.0f && (m.history.empty() || changed(m.history.back().pb, m.pb) || changed(m.history.back().sob, sob))) {
        const int day = std::max(m.lastDay, m.history.empty() ? 0 : m.history.back().day);
        m.history.push_back({day, m.pb, sob});
    }
    return m;
}

/// Whatever the copy holds (very long names…), the merged file stays under its cap: what was
/// played the least recently goes first (the oldest month, classic level, level, boss, day).
void shrinkToFit(PlayStatsData& m) {
    auto leastRecent = [](auto& map, auto recency) {
        auto worst = map.begin();
        for (auto it = map.begin(); it != map.end(); ++it)
            if (recency(it->second) < recency(worst->second)) worst = it;
        map.erase(worst);
    };
    while (PlayStatsStore::serialize(m).size() >= kMaxFileBytes) {
        if (!m.months.empty()) m.months.erase(m.months.begin());
        else if (!m.runs.empty()) leastRecent(m.runs, [](const LevelRuns& r) { return r.lastDay; });
        else if (!m.levels.empty()) leastRecent(m.levels, [](const LevelSlices& l) { return l.lastDay; });
        else if (!m.bosses.empty()) leastRecent(m.bosses, [](const BossHistory& b) { return b.lastSeq; });
        else if (!m.days.empty()) m.days.erase(m.days.begin());
        else break;
    }
}

}  // namespace

PlayStatsData mergePlayStats(PlayStatsData mine, PlayStatsData theirs, int today) {
    // Never trusted: a day (or month) of the other computer far in the future is left out.
    const int lastDay = today + 31, lastMonth = monthOfDay(lastDay);
    theirs.days.erase(std::remove_if(theirs.days.begin(), theirs.days.end(), [&](const StatsDay& d) { return d.day > lastDay; }), theirs.days.end());
    theirs.months.erase(std::remove_if(theirs.months.begin(), theirs.months.end(), [&](const StatsMonth& m) { return m.month > lastMonth; }),
                        theirs.months.end());
    // Both sides as of the same day: the same days kept one by one, the older ones by month; the
    // attempts of each boss in the order of their days.
    for (PlayStatsData* side : {&mine, &theirs}) {
        for (auto& [id, b] : side->bosses)
            std::stable_sort(b.recent.begin(), b.recent.end(), [](const BossAttemptRec& x, const BossAttemptRec& y) { return x.day < y.day; });
        side->assignIds();
        side->compact(today);
    }
    PlayStatsData m;
    // Days and months: the highest of both sides.
    {
        std::map<int, StatsCounters> days;
        for (const auto& d : mine.days) days[d.day] = d.c;
        for (const auto& d : theirs.days) {
            auto it = days.find(d.day);
            days[d.day] = it == days.end() ? d.c : highest(it->second, d.c);
        }
        for (const auto& [day, c] : days) m.days.push_back({day, c});
        std::map<int, StatsCounters> months;
        for (const auto& x : mine.months) months[x.month] = x.c;
        for (const auto& x : theirs.months) {
            auto it = months.find(x.month);
            months[x.month] = it == months.end() ? x.c : highest(it->second, x.c);
        }
        for (const auto& [month, c] : months) m.months.push_back({month, c});
    }
    // Slices of the levels.
    m.levels = mine.levels;
    for (const auto& [id, l] : theirs.levels) {
        auto it = m.levels.find(id);
        m.levels[id] = it == m.levels.end() ? l : mergeSlices(it->second, l);
    }
    // Bosses: their attempts united; then the order of the last fights (by the day of the last
    // attempt, then each side's own order).
    m.bosses = mine.bosses;
    for (const auto& [id, b] : theirs.bosses) {
        auto it = m.bosses.find(id);
        m.bosses[id] = it == m.bosses.end() ? b : mergeBoss(it->second, b);
    }
    if (!theirs.bosses.empty()) {
        std::vector<std::pair<std::pair<int, int>, std::string>> order;
        for (const auto& [id, b] : m.bosses) {
            int last = 0;
            for (const auto& r : b.recent) last = std::max(last, r.day);
            order.push_back({{last, b.lastSeq}, id});
        }
        std::sort(order.begin(), order.end());
        int seq = 0;
        for (const auto& [key, id] : order) m.bosses[id].lastSeq = ++seq;
        m.seq = std::max({mine.seq, theirs.seq, seq});
    } else {
        m.seq = mine.seq;
    }
    // Classic levels.
    m.runs = mine.runs;
    for (const auto& [id, r] : theirs.runs) {
        auto it = m.runs.find(id);
        m.runs[id] = it == m.runs.end() ? r : mergeRuns(it->second, r);
    }
    m.compact(today);
    shrinkToFit(m);
    return m;
}

}  // namespace cosmic
