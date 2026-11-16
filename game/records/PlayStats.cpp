// 2.53 — the player's statistics: the data kept (saves/stats.json) and what the « Statistiques »
// screen computes from it. See PlayStats.h; the recorder and the store: PlayStatsRecorder.cpp.

#include "game/records/PlayStats.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>

namespace cosmic {

using namespace playstats;

// ------------------------------------------------------------------ calendar

int daysFromCivil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void civilFromDays(int z, int& y, int& m, int& d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    d = static_cast<int>(doy - (153u * mp + 2u) / 5u + 1u);
    m = static_cast<int>(mp < 10u ? mp + 3u : mp - 9u);
    y += m <= 2 ? 1 : 0;
}

int localDay() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return daysFromCivil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

namespace {

int monthOf(int day) {
    int y, m, d;
    civilFromDays(day, y, m, d);
    return y * 12 + m - 1;
}

// ------------------------------------------------------------------ reading safely

int num(const Json& a, size_t i, int def = 0) {
    if (!a.is_array() || i >= a.size() || !a[i].is_number()) return def;
    double v = a[i].get<double>();
    if (!std::isfinite(v)) return def;
    return static_cast<int>(std::clamp(v, -2.0e9, 2.0e9));
}
double real(const Json& a, size_t i, double def = 0.0) {
    if (!a.is_array() || i >= a.size() || !a[i].is_number()) return def;
    double v = a[i].get<double>();
    return std::isfinite(v) ? v : def;
}
int field(const Json& o, const char* k, int def = 0) {
    if (!o.is_object()) return def;
    auto it = o.find(k);
    if (it == o.end() || !it->is_number()) return def;
    double v = it->get<double>();
    if (!std::isfinite(v)) return def;
    return static_cast<int>(std::clamp(v, -2.0e9, 2.0e9));
}
std::string text(const Json& o, const char* k, size_t maxLen = 120) {
    if (!o.is_object()) return {};
    auto it = o.find(k);
    if (it == o.end() || !it->is_string()) return {};
    std::string s = it->get<std::string>();
    if (s.size() > maxLen) s.resize(maxLen);
    return s;
}
const Json& member(const Json& o, const char* k) {
    static const Json none;
    if (!o.is_object()) return none;
    auto it = o.find(k);
    return it == o.end() ? none : *it;
}
std::string str(const Json& a, size_t i, size_t maxLen = 120) {
    if (!a.is_array() || i >= a.size() || !a[i].is_string()) return {};
    std::string s = a[i].get<std::string>();
    if (s.size() > maxLen) s.resize(maxLen);
    return s;
}

double round1(double v) { return std::round(v * 10.0) / 10.0; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }
int centis(float s) { return static_cast<int>(std::lround(static_cast<double>(s) * 100.0)); }
float fromCentis(int c) { return static_cast<float>(c) / 100.0f; }

// A compact text for long lists (the file stays small): base64url digits, `chars` per value.
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr unsigned kNoTime = 262143;  // 3 digits, all set: no time
void putB64(std::string& out, unsigned v, int chars) {
    for (int i = chars - 1; i >= 0; --i) out += kB64[(v >> (6 * i)) & 63u];
}
bool getB64(const std::string& in, size_t& pos, int chars, unsigned& v) {
    if (pos + static_cast<size_t>(chars) > in.size()) return false;
    v = 0;
    for (int i = 0; i < chars; ++i) {
        char c = in[pos++];
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : c == '_' ? 63 : -1;
        if (d < 0) return false;
        v = (v << 6) | static_cast<unsigned>(d);
    }
    return true;
}
unsigned centisCode(float s) { return static_cast<unsigned>(std::clamp(std::lround(static_cast<double>(s) * 100.0), 1L, static_cast<long>(kNoTime) - 1L)); }

Json countersJson(const StatsCounters& c, int key, std::map<std::string, int>& names) {
    Json hurt = Json::array();
    for (int h : c.hurt) hurt.push_back(h);
    Json pw = Json::array();
    for (const auto& [id, v] : c.powers) {
        auto it = names.find(id);
        if (it == names.end()) it = names.emplace(id, static_cast<int>(names.size())).first;
        pw.push_back(Json::array({it->second, v[0], v[1]}));
    }
    return Json::array({key, round1(c.playSec), round1(c.trainSec), c.deaths, c.threats, c.dodged, c.attacks, c.hits, c.stars, hurt, pw});
}

StatsCounters countersFrom(const Json& a, const std::vector<std::string>& names) {
    StatsCounters c;
    c.playSec = std::clamp(real(a, 1), 0.0, 86400.0 * 31);
    c.trainSec = std::clamp(real(a, 2), 0.0, 86400.0 * 31);
    c.deaths = std::max(0, num(a, 3));
    c.threats = std::max(0, num(a, 4));
    c.dodged = std::clamp(num(a, 5), 0, c.threats);
    c.attacks = std::max(0, num(a, 6));
    c.hits = std::clamp(num(a, 7), 0, c.attacks);
    c.stars = std::max(-1, num(a, 8, -1));
    const Json& hurt = a.is_array() && a.size() > 9 ? a[9] : Json();
    for (int i = 0; i < kOrigins; ++i) c.hurt[static_cast<size_t>(i)] = std::max(0, num(hurt, static_cast<size_t>(i)));
    const Json& pw = a.is_array() && a.size() > 10 ? a[10] : Json();
    if (pw.is_array())
        for (const auto& e : pw) {
            int idx = num(e, 0, -1);
            if (idx < 0 || static_cast<size_t>(idx) >= names.size() || names[static_cast<size_t>(idx)].empty()) continue;
            if (c.powers.size() >= 32) break;
            auto& v = c.powers[names[static_cast<size_t>(idx)]];
            v[0] = std::max(0, num(e, 1));
            v[1] = std::clamp(num(e, 2), 0, v[0]);
        }
    return c;
}

}  // namespace

// ------------------------------------------------------------------ counters

void StatsCounters::add(const StatsCounters& o) {
    playSec += o.playSec;
    trainSec += o.trainSec;
    deaths += o.deaths;
    threats += o.threats;
    dodged += o.dodged;
    attacks += o.attacks;
    hits += o.hits;
    for (const auto& [id, v] : o.powers) {
        auto& m = powers[id];
        m[0] += v[0];
        m[1] += v[1];
    }
    for (size_t i = 0; i < hurt.size(); ++i) hurt[i] += o.hurt[i];
    stars = std::max(stars, o.stars);
}

void LevelSlices::decayTo(int day) {
    if (day <= stamp) return;
    const float f = recencyWeight(day - stamp);
    for (int i = 0; i < kSlices; ++i) {
        wPasses[static_cast<size_t>(i)] *= f;
        wDeaths[static_cast<size_t>(i)] *= f;
    }
    stamp = day;
}

bool LevelSlices::any() const {
    for (int p : passes)
        if (p > 0) return true;
    return false;
}

float BossHistory::best() const {
    float b = bestBefore;
    for (const auto& r : recent) b = std::max(b, r.won ? 1.0f : r.removed);
    return b;
}

const SegmentRec* LevelRuns::segment(const std::string& from, const std::string& to) const {
    for (const auto& s : segments)
        if (s.from == from && s.to == to) return &s;
    return nullptr;
}

SegmentRec& LevelRuns::segmentAt(const std::string& from, const std::string& to) {
    for (auto& s : segments)
        if (s.from == from && s.to == to) return s;
    segments.push_back({from, to, {}, -1.0f});
    return segments.back();
}

std::vector<std::pair<std::string, std::string>> LevelRuns::route() const {
    std::vector<std::pair<std::string, std::string>> r;
    if (pb <= 0.0f) return r;
    std::string prev;
    for (const auto& cp : pbRoute) {
        r.push_back({prev, cp});
        prev = cp;
    }
    r.push_back({prev, std::string()});
    return r;
}

float LevelRuns::sob() const {
    auto r = route();
    if (r.empty()) return -1.0f;
    float sum = 0.0f;
    for (const auto& [from, to] : r) {
        const SegmentRec* s = segment(from, to);
        if (!s || s->best <= 0.0f) return -1.0f;
        sum += s->best;
    }
    return sum;
}

// ------------------------------------------------------------------ the data

bool PlayStatsData::empty() const {
    for (const auto& d : days)
        if (d.c.playSec > 0.0 || d.c.trainSec > 0.0) return false;
    return months.empty() && levels.empty() && bosses.empty() && runs.empty();
}

StatsCounters& PlayStatsData::at(int day) {
    auto it = std::lower_bound(days.begin(), days.end(), day, [](const StatsDay& a, int d) { return a.day < d; });
    if (it == days.end() || it->day != day) it = days.insert(it, StatsDay{day, {}});
    return it->c;
}

const StatsDay* PlayStatsData::find(int day) const {
    auto it = std::lower_bound(days.begin(), days.end(), day, [](const StatsDay& a, int d) { return a.day < d; });
    return it != days.end() && it->day == day ? &*it : nullptr;
}

void PlayStatsData::compact(int today) {
    // Days: one by one for 365 days, then by month.
    std::sort(days.begin(), days.end(), [](const StatsDay& a, const StatsDay& b) { return a.day < b.day; });
    const int oldest = today - kDaysKept + 1;
    size_t old = 0;
    while (old < days.size() && days[old].day < oldest) ++old;
    for (size_t i = 0; i < old; ++i) {
        int m = monthOf(days[i].day);
        auto it = std::find_if(months.begin(), months.end(), [m](const StatsMonth& x) { return x.month == m; });
        if (it == months.end()) months.push_back({m, days[i].c});
        else it->c.add(days[i].c);
    }
    days.erase(days.begin(), days.begin() + static_cast<std::ptrdiff_t>(old));
    std::sort(months.begin(), months.end(), [](const StatsMonth& a, const StatsMonth& b) { return a.month < b.month; });
    if (months.size() > static_cast<size_t>(kMonthsKept)) months.erase(months.begin(), months.end() - kMonthsKept);
    // Levels: the least recently played go first.
    while (levels.size() > kMaxLevels) {
        auto worst = levels.begin();
        for (auto it = levels.begin(); it != levels.end(); ++it)
            if (it->second.lastDay < worst->second.lastDay) worst = it;
        levels.erase(worst);
    }
    // Bosses: their last 200 attempts, their 12 most frequent fatal attacks.
    for (auto& [id, b] : bosses) {
        if (b.recent.size() > static_cast<size_t>(kBossAttemptsKept)) {
            size_t drop = b.recent.size() - kBossAttemptsKept;
            for (size_t i = 0; i < drop; ++i) b.bestBefore = std::max(b.bestBefore, b.recent[i].won ? 1.0f : b.recent[i].removed);
            b.recent.erase(b.recent.begin(), b.recent.begin() + static_cast<std::ptrdiff_t>(drop));
        }
        while (b.fatal.size() > kMaxFatal) {
            auto rare = b.fatal.begin();
            for (auto it = b.fatal.begin(); it != b.fatal.end(); ++it)
                if (it->second < rare->second) rare = it;
            b.fatal.erase(rare);
        }
        b.attempts = std::max(b.attempts, static_cast<int>(b.recent.size()));
    }
    while (bosses.size() > kMaxBosses) {
        auto worst = bosses.begin();
        for (auto it = bosses.begin(); it != bosses.end(); ++it)
            if (it->second.lastSeq < worst->second.lastSeq) worst = it;
        bosses.erase(worst);
    }
    // Classic levels: their segments, last runs and history.
    for (auto& [id, r] : runs) {
        for (auto& s : r.segments)
            if (s.times.size() > kSegmentTimesKept) s.times.erase(s.times.begin(), s.times.end() - static_cast<std::ptrdiff_t>(kSegmentTimesKept));
        if (r.segments.size() > kMaxSegments) {
            // The segments of the PB route first, then the most recent ones.
            auto route = r.route();
            std::stable_partition(r.segments.begin(), r.segments.end(), [&](const SegmentRec& s) {
                return std::find(route.begin(), route.end(), std::make_pair(s.from, s.to)) != route.end();
            });
            r.segments.resize(kMaxSegments);
        }
        if (r.finishes.size() > kFinishesKept) r.finishes.erase(r.finishes.begin(), r.finishes.end() - static_cast<std::ptrdiff_t>(kFinishesKept));
        if (r.history.size() > kHistoryKept) r.history.erase(r.history.begin(), r.history.end() - static_cast<std::ptrdiff_t>(kHistoryKept));
        if (r.pbRoute.size() > 64) r.pbRoute.resize(64);
    }
    while (runs.size() > kMaxRunLevels) {
        auto worst = runs.begin();
        for (auto it = runs.begin(); it != runs.end(); ++it)
            if (it->second.lastDay < worst->second.lastDay) worst = it;
        runs.erase(worst);
    }
}

Json PlayStatsData::toJson() const {
    std::map<std::string, int> names;
    Json jd = Json::array(), jm = Json::array();
    for (const auto& d : days) jd.push_back(countersJson(d.c, d.day, names));
    for (const auto& m : months) jm.push_back(countersJson(m.c, m.month, names));
    Json table = Json::array();
    std::vector<std::string> ordered(names.size());
    for (const auto& [id, i] : names) ordered[static_cast<size_t>(i)] = id;
    for (const auto& n : ordered) table.push_back(n);

    Json jl = Json::object();
    for (const auto& [id, l] : levels) {
        Json p = Json::array(), dd = Json::array(), wp = Json::array(), wd = Json::array();
        for (int i = 0; i < kSlices; ++i) {
            p.push_back(l.passes[static_cast<size_t>(i)]);
            dd.push_back(l.deaths[static_cast<size_t>(i)]);
            wp.push_back(round2(l.wPasses[static_cast<size_t>(i)]));
            wd.push_back(round2(l.wDeaths[static_cast<size_t>(i)]));
        }
        jl[id] = {{"n", l.name}, {"s", l.stamp}, {"l", l.lastDay}, {"p", p}, {"d", dd}, {"wp", wp}, {"wd", wd}};
    }

    Json jb = Json::object();
    for (const auto& [id, b] : bosses) {
        // Its attempts, 4 characters each: its share taken (or a victory), its phase, its fatal
        // attack (an index in `an`, the names shared with the counts of `fc`).
        std::map<std::string, int> index;
        Json names = Json::array();
        auto nameOf = [&](const std::string& n) {
            auto it = index.find(n);
            if (it != index.end()) return it->second;
            if (index.size() >= 62) return -1;
            names.push_back(n);
            return index.emplace(n, static_cast<int>(index.size())).first->second;
        };
        std::string attempts;
        for (const auto& r : b.recent) {
            putB64(attempts, r.won ? 1001u : static_cast<unsigned>(std::clamp(std::lround(r.removed * 1000.0f), 0L, 1000L)), 2);
            putB64(attempts, static_cast<unsigned>(std::clamp(r.phase, 0, 63)), 1);
            putB64(attempts, r.attack.empty() ? 0u : static_cast<unsigned>(nameOf(r.attack) + 1), 1);
        }
        Json reached = Json::array(), defeats = Json::array(), fatal = Json::array();
        for (int v : b.reached) reached.push_back(v);
        for (int v : b.defeats) defeats.push_back(v);
        for (const auto& [a, n] : b.fatal)
            if (int i = nameOf(a); i >= 0) fatal.push_back(Json::array({i, n}));
        jb[id] = {{"n", b.name},   {"ph", b.phases}, {"a", b.attempts}, {"w", b.wins}, {"fw", b.firstWin},
                  {"bb", static_cast<int>(std::lround(b.bestBefore * 1000.0f))}, {"q", b.lastSeq}, {"re", reached},
                  {"de", defeats}, {"an", names}, {"fc", fatal}, {"r", attempts}};
    }

    Json jr = Json::object();
    for (const auto& [id, r] : runs) {
        // Times in hundredths of a second, 3 characters each; days as gaps from the first one.
        Json segs = Json::array();
        for (const auto& s : r.segments) {
            std::string times;
            for (float t : s.times) putB64(times, centisCode(t), 3);
            segs.push_back({{"f", s.from}, {"t", s.to}, {"b", s.best > 0.0f ? centis(s.best) : -1}, {"x", times}});
        }
        std::string fin, hist;
        int prevDay = r.finishes.empty() ? 0 : r.finishes.front().day;
        for (const auto& f : r.finishes) {
            putB64(fin, static_cast<unsigned>(std::clamp(f.day - prevDay, 0, 4095)), 2);
            putB64(fin, centisCode(f.time), 3);
            prevDay = f.day;
        }
        const int h0 = r.history.empty() ? 0 : r.history.front().day;
        prevDay = h0;
        for (const auto& h : r.history) {
            putB64(hist, static_cast<unsigned>(std::clamp(h.day - prevDay, 0, 4095)), 2);
            putB64(hist, h.pb > 0.0f ? centisCode(h.pb) : kNoTime, 3);
            putB64(hist, h.sob > 0.0f ? centisCode(h.sob) : kNoTime, 3);
            prevDay = h.day;
        }
        Json route = Json::array();
        for (const auto& c : r.pbRoute) route.push_back(c);
        jr[id] = {{"n", r.name}, {"v", r.version}, {"a", r.attempts}, {"f", r.finished}, {"pb", r.pb > 0.0f ? centis(r.pb) : -1},
                  {"route", route}, {"seg", segs}, {"fin0", r.finishes.empty() ? 0 : r.finishes.front().day}, {"fin", fin},
                  {"h0", h0}, {"h", hist}, {"l", r.lastDay}};
    }
    return {{"names", table}, {"days", jd}, {"months", jm}, {"levels", jl}, {"bosses", jb}, {"runs", jr}, {"seq", seq}};
}

PlayStatsData PlayStatsData::fromJson(const Json& j) {
    PlayStatsData out;
    if (!j.is_object()) return out;
    try {
        std::vector<std::string> names;
        const Json& table = member(j, "names");
        if (table.is_array())
            for (size_t i = 0; i < table.size() && i < 64; ++i) names.push_back(str(table, i, 40));
        const Json& jd = member(j, "days");
        if (jd.is_array())
            for (const auto& a : jd) {
                if (!a.is_array() || a.empty()) continue;
                int day = num(a, 0, -1);
                if (day <= 0) continue;
                out.at(day).add(countersFrom(a, names));
            }
        const Json& jm = member(j, "months");
        if (jm.is_array())
            for (const auto& a : jm) {
                int m = num(a, 0, -1);
                if (m <= 0) continue;
                auto it = std::find_if(out.months.begin(), out.months.end(), [m](const StatsMonth& x) { return x.month == m; });
                if (it == out.months.end()) out.months.push_back({m, countersFrom(a, names)});
                else it->c.add(countersFrom(a, names));
            }
        const Json& jl = member(j, "levels");
        if (jl.is_object())
            for (const auto& [id, o] : jl.items()) {
                if (id.empty() || id.size() > 120 || !o.is_object()) continue;
                LevelSlices l;
                l.name = text(o, "n");
                l.stamp = std::max(0, field(o, "s"));
                l.lastDay = std::max(0, field(o, "l"));
                const Json &p = member(o, "p"), &d = member(o, "d"), &wp = member(o, "wp"), &wd = member(o, "wd");
                for (int i = 0; i < kSlices; ++i) {
                    size_t k = static_cast<size_t>(i);
                    l.passes[k] = std::max(0, num(p, k));
                    l.deaths[k] = std::clamp(num(d, k), 0, 1000000);
                    l.wPasses[k] = static_cast<float>(std::clamp(real(wp, k), 0.0, 1.0e7));
                    l.wDeaths[k] = static_cast<float>(std::clamp(real(wd, k), 0.0, 1.0e7));
                }
                out.levels[id] = l;
            }
        const Json& jb = member(j, "bosses");
        if (jb.is_object())
            for (const auto& [id, o] : jb.items()) {
                if (id.empty() || id.size() > 120 || !o.is_object()) continue;
                BossHistory b;
                b.name = text(o, "n");
                b.phases = std::clamp(field(o, "ph", 1), 1, 64);
                b.attempts = std::max(0, field(o, "a"));
                b.wins = std::clamp(field(o, "w"), 0, b.attempts);
                b.firstWin = std::clamp(field(o, "fw"), 0, b.attempts);
                b.bestBefore = std::clamp(static_cast<float>(field(o, "bb")) / 1000.0f, 0.0f, 1.0f);
                b.lastSeq = std::max(0, field(o, "q"));
                const Json &re = member(o, "re"), &de = member(o, "de");
                for (int i = 0; i < b.phases; ++i) {
                    b.reached.push_back(std::max(0, num(re, static_cast<size_t>(i))));
                    b.defeats.push_back(std::max(0, num(de, static_cast<size_t>(i))));
                }
                std::vector<std::string> names;
                const Json& an = member(o, "an");
                if (an.is_array())
                    for (size_t i = 0; i < an.size() && i < 64; ++i) names.push_back(str(an, i));
                const Json& fc = member(o, "fc");
                if (fc.is_array())
                    for (const auto& e : fc) {
                        int i = num(e, 0, -1);
                        if (i >= 0 && static_cast<size_t>(i) < names.size() && !names[static_cast<size_t>(i)].empty())
                            b.fatal[names[static_cast<size_t>(i)]] = std::max(0, num(e, 1));
                    }
                const std::string r = text(o, "r", 4 * (kBossAttemptsKept + 50));
                size_t pos = 0;
                unsigned code = 0, phase = 0, attack = 0;
                while (getB64(r, pos, 2, code) && getB64(r, pos, 1, phase) && getB64(r, pos, 1, attack)) {
                    BossAttemptRec rec;
                    rec.won = code > 1000;
                    rec.removed = rec.won ? 1.0f : static_cast<float>(code) / 1000.0f;
                    rec.phase = std::clamp(static_cast<int>(phase), 0, b.phases - 1);
                    if (attack > 0 && attack <= names.size()) rec.attack = names[attack - 1];
                    b.recent.push_back(rec);
                }
                b.attempts = std::max(b.attempts, static_cast<int>(b.recent.size()));
                out.bosses[id] = b;
            }
        const Json& jr = member(j, "runs");
        if (jr.is_object())
            for (const auto& [id, o] : jr.items()) {
                if (id.empty() || id.size() > 120 || !o.is_object()) continue;
                LevelRuns r;
                r.name = text(o, "n");
                r.version = text(o, "v", 128);
                r.attempts = std::max(0, field(o, "a"));
                r.finished = std::clamp(field(o, "f"), 0, r.attempts);
                int pb = field(o, "pb", -1);
                r.pb = pb > 0 ? fromCentis(pb) : -1.0f;
                const Json& route = member(o, "route");
                if (route.is_array())
                    for (size_t i = 0; i < route.size() && i < 64; ++i) r.pbRoute.push_back(str(route, i));
                const Json& segs = member(o, "seg");
                if (segs.is_array())
                    for (const auto& s : segs) {
                        if (!s.is_object() || r.segments.size() >= kMaxSegments) continue;
                        SegmentRec sr;
                        sr.from = text(s, "f");
                        sr.to = text(s, "t");
                        int b = field(s, "b", -1);
                        sr.best = b > 0 ? fromCentis(b) : -1.0f;
                        const std::string x = text(s, "x", 3 * (kSegmentTimesKept + 10));
                        size_t pos = 0;
                        for (unsigned c = 0; getB64(x, pos, 3, c);)
                            if (c > 0 && c != kNoTime) sr.times.push_back(fromCentis(static_cast<int>(c)));
                        if (sr.times.size() > kSegmentTimesKept) sr.times.erase(sr.times.begin(), sr.times.end() - static_cast<std::ptrdiff_t>(kSegmentTimesKept));
                        r.segments.push_back(std::move(sr));
                    }
                int day = std::max(0, field(o, "fin0"));
                const std::string fin = text(o, "fin", 5 * (kFinishesKept + 10));
                size_t pos = 0;
                for (unsigned gap = 0, c = 0; getB64(fin, pos, 2, gap) && getB64(fin, pos, 3, c);) {
                    day += static_cast<int>(gap);
                    if (c > 0 && c != kNoTime) r.finishes.push_back({day, fromCentis(static_cast<int>(c))});
                }
                day = std::max(0, field(o, "h0"));
                const std::string h = text(o, "h", 8 * (kHistoryKept + 10));
                pos = 0;
                for (unsigned gap = 0, p = 0, so = 0; getB64(h, pos, 2, gap) && getB64(h, pos, 3, p) && getB64(h, pos, 3, so);) {
                    day += static_cast<int>(gap);
                    r.history.push_back({day, p != kNoTime && p > 0 ? fromCentis(static_cast<int>(p)) : -1.0f,
                                         so != kNoTime && so > 0 ? fromCentis(static_cast<int>(so)) : -1.0f});
                }
                r.lastDay = std::max(0, field(o, "l"));
                out.runs[id] = std::move(r);
            }
        out.seq = std::max(0, field(j, "seq"));
    } catch (const Json::exception&) {
        // Something unexpected in a part of the file: what was read is kept.
    }
    return out;
}

// ------------------------------------------------------------------ calculations

std::optional<double> PeriodTotals::dodgeRate() const {
    if (c.threats <= 0) return std::nullopt;
    return static_cast<double>(c.dodged) / c.threats;
}
std::optional<double> PeriodTotals::accuracy() const {
    if (c.attacks <= 0) return std::nullopt;
    return static_cast<double>(c.hits) / c.attacks;
}
std::optional<double> PeriodTotals::deathsPerHour() const {
    if (c.playSec < 60.0) return std::nullopt;  // under a minute played: not a rate worth showing
    return c.deaths / (c.playSec / 3600.0);
}

PeriodTotals periodTotals(const PlayStatsData& d, int from, int to) {
    PeriodTotals t;
    for (const auto& day : d.days) {
        if (day.day < from || day.day > to) continue;
        t.c.add(day.c);
        if (day.c.played()) ++t.daysPlayed;
    }
    return t;
}

Comparison comparePeriods(std::optional<double> current, std::optional<double> previous, bool higherIsBetter, double epsilon) {
    Comparison c;
    if (!current || !previous) return c;
    c.available = true;
    c.delta = *current - *previous;
    c.same = std::abs(c.delta) < epsilon;
    c.progress = !c.same && (higherIsBetter ? c.delta > 0.0 : c.delta < 0.0);
    return c;
}

namespace {
/// The numerator and denominator of a metric for some counters (denominator 0: no value).
std::pair<double, double> ratioParts(const StatsCounters& c, DayMetric m) {
    switch (m) {
        case DayMetric::Dodge: return {static_cast<double>(c.dodged), static_cast<double>(c.threats)};
        case DayMetric::Accuracy: return {static_cast<double>(c.hits), static_cast<double>(c.attacks)};
        case DayMetric::DeathsPerHour: return {static_cast<double>(c.deaths), c.playSec >= 60.0 ? c.playSec / 3600.0 : 0.0};
    }
    return {0.0, 0.0};
}
}  // namespace

std::vector<SeriesPoint> dailySeries(const PlayStatsData& d, DayMetric m, int from, int to, int window) {
    std::vector<SeriesPoint> out;
    std::vector<std::pair<double, double>> seen;  // days with a value, up to the one at hand
    for (const auto& day : d.days) {
        if (day.day > to) break;
        auto [nu, den] = ratioParts(day.c, m);
        if (den <= 0.0) continue;
        seen.push_back({nu, den});
        if (day.day < from) continue;
        double n = 0.0, dd = 0.0;
        size_t first = seen.size() > static_cast<size_t>(std::max(1, window)) ? seen.size() - static_cast<size_t>(std::max(1, window)) : 0;
        for (size_t i = first; i < seen.size(); ++i) {
            n += seen[i].first;
            dd += seen[i].second;
        }
        out.push_back({day.day, nu / den, dd > 0.0 ? n / dd : nu / den});
    }
    return out;
}

std::vector<std::pair<int, int>> starsSeries(const PlayStatsData& d, int from, int to) {
    std::vector<std::pair<int, int>> out;
    for (const auto& day : d.days)
        if (day.day >= from && day.day <= to && day.c.stars >= 0) out.push_back({day.day, day.c.stars});
    return out;
}

std::vector<std::pair<int, float>> bossBestCurve(const BossHistory& b) {
    std::vector<std::pair<int, float>> out;
    float best = b.bestBefore;
    int n = b.firstKept();
    for (const auto& r : b.recent) {
        best = std::max(best, r.won ? 1.0f : r.removed);
        out.push_back({n++, best});
    }
    return out;
}

int hardestPhase(const BossHistory& b) {
    int best = -1, most = 0;
    for (size_t i = 0; i < b.defeats.size(); ++i)
        if (b.defeats[i] > most) {
            most = b.defeats[i];
            best = static_cast<int>(i);
        }
    return best;
}

std::string commonFatal(const BossHistory& b) {
    std::string best;
    int most = 0;
    for (const auto& [a, n] : b.fatal)
        if (n > most) {
            most = n;
            best = a;
        }
    return best;
}

float sliceDifficulty(float deaths, float passes) { return (deaths + 1.0f) / (passes + 2.0f); }

float recencyWeight(int days) { return days <= 0 ? 1.0f : std::pow(0.5f, static_cast<float>(days) / kHalfLifeDays); }

std::array<float, kSlices> sliceDifficulties(const LevelSlices& l, int today) {
    std::array<float, kSlices> out;
    const float f = recencyWeight(today - l.stamp);
    for (int i = 0; i < kSlices; ++i) {
        size_t k = static_cast<size_t>(i);
        out[k] = l.passes[k] > 0 ? sliceDifficulty(l.wDeaths[k] * f, l.wPasses[k] * f) : -1.0f;
    }
    return out;
}

std::vector<HardPassage> levelPassages(const std::string& id, const LevelSlices& l, int today) {
    std::vector<HardPassage> out;
    auto ds = sliceDifficulties(l, today);
    std::vector<float> values;
    for (float v : ds)
        if (v >= 0.0f) values.push_back(v);
    if (values.empty()) return out;
    const float threshold = 2.0f * medianOf(values);
    int i = 0;
    while (i < kSlices) {
        if (ds[static_cast<size_t>(i)] < 0.0f || ds[static_cast<size_t>(i)] <= threshold) {
            ++i;
            continue;
        }
        HardPassage h;
        h.kind = HardPassage::Kind::Level;
        h.id = id;
        h.name = l.name;
        h.from = i;
        float survive = 1.0f;
        while (i < kSlices && ds[static_cast<size_t>(i)] >= 0.0f && ds[static_cast<size_t>(i)] > threshold) {
            size_t k = static_cast<size_t>(i);
            survive *= 1.0f - ds[k];
            h.passes = std::max(h.passes, l.passes[k]);
            h.deaths += l.deaths[k];
            h.to = i++;
        }
        h.difficulty = 1.0f - survive;
        if (h.passes >= kMinPassages) out.push_back(h);
    }
    return out;
}

std::vector<HardPassage> bossPassages(const std::string& id, const BossHistory& b) {
    std::vector<HardPassage> out;
    const std::string attack = commonFatal(b);
    for (size_t p = 0; p < b.reached.size() && p < b.defeats.size(); ++p) {
        if (b.reached[p] < kMinPassages || b.defeats[p] <= 0) continue;
        HardPassage h;
        h.kind = HardPassage::Kind::BossPhase;
        h.id = id;
        h.name = b.name;
        h.phase = static_cast<int>(p);
        h.phases = b.phases;
        h.passes = b.reached[p];
        h.deaths = std::min(b.defeats[p], b.reached[p]);
        h.difficulty = static_cast<float>(h.deaths) / static_cast<float>(h.passes);
        h.attack = attack;
        out.push_back(h);
    }
    return out;
}

std::vector<HardPassage> hardestPassages(const PlayStatsData& d, int today, size_t count) {
    std::vector<HardPassage> all;
    for (const auto& [id, l] : d.levels)
        for (auto& h : levelPassages(id, l, today)) all.push_back(std::move(h));
    for (const auto& [id, b] : d.bosses)
        for (auto& h : bossPassages(id, b)) all.push_back(std::move(h));
    std::stable_sort(all.begin(), all.end(), [](const HardPassage& a, const HardPassage& b) {
        if (a.difficulty != b.difficulty) return a.difficulty > b.difficulty;
        return a.passes > b.passes;
    });
    if (all.size() > count) all.resize(count);
    return all;
}

float medianOf(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

namespace {
float quantile(const std::vector<float>& sorted, float q) {
    if (sorted.empty()) return 0.0f;
    float pos = q * static_cast<float>(sorted.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(pos));
    size_t hi = std::min(sorted.size() - 1, lo + 1);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (pos - static_cast<float>(lo));
}
}  // namespace

float iqrOf(std::vector<float> v) {
    if (v.size() < 2) return 0.0f;
    std::sort(v.begin(), v.end());
    return quantile(v, 0.75f) - quantile(v, 0.25f);
}

std::optional<float> stddevOf(const std::vector<float>& v) {
    if (v.size() < 2) return std::nullopt;
    double mean = 0.0;
    for (float x : v) mean += x;
    mean /= static_cast<double>(v.size());
    double s = 0.0;
    for (float x : v) s += (x - mean) * (x - mean);
    return static_cast<float>(std::sqrt(s / static_cast<double>(v.size() - 1)));
}

std::vector<SegmentReport> segmentReports(const LevelRuns& l) {
    std::vector<std::pair<std::string, std::string>> route = l.route();
    if (route.empty())
        for (const auto& s : l.segments) route.push_back({s.from, s.to});
    std::vector<SegmentReport> out;
    int index = 0;
    for (const auto& [from, to] : route) {
        const SegmentRec* s = l.segment(from, to);
        int at = index++;
        if (!s || s->times.empty()) continue;
        SegmentReport r;
        r.from = from;
        r.to = to;
        r.index = at;
        r.count = static_cast<int>(s->times.size());
        r.best = s->best;
        r.median = medianOf(s->times);
        r.lost = r.best > 0.0f ? std::max(0.0f, r.median - r.best) : 0.0f;
        r.spread = r.median > 0.0f ? iqrOf(s->times) / r.median : 0.0f;
        int near = 0;
        for (float t : s->times) near += r.best > 0.0f && t <= r.best * (1.0f + kNearBest) + 1e-4f ? 1 : 0;
        r.nearBest = static_cast<float>(near) / static_cast<float>(s->times.size());
        out.push_back(r);
    }
    return out;
}

Regularity regularity(const LevelRuns& l, int today) {
    Regularity g;
    g.attempts = l.attempts;
    g.finished = l.finished;
    size_t n = l.finishes.size();
    size_t first = n > static_cast<size_t>(kRecentFinishes) ? n - kRecentFinishes : 0;
    g.last.assign(l.finishes.begin() + static_cast<std::ptrdiff_t>(first), l.finishes.end());
    std::vector<float> times;
    for (size_t i = 0; i < g.last.size(); ++i) {
        times.push_back(g.last[i].time);
        size_t lo = i >= 4 ? i - 4 : 0;
        std::vector<float> win(times.begin() + static_cast<std::ptrdiff_t>(lo), times.end());
        g.rollingMedian.push_back(medianOf(win));
    }
    if (times.size() >= 3) g.sd = stddevOf(times);
    // As of 30 days ago: the last 20 full runs played before that day.
    std::vector<float> before;
    for (const auto& f : l.finishes)
        if (f.day <= today - 30) before.push_back(f.time);
    if (before.size() > static_cast<size_t>(kRecentFinishes)) before.erase(before.begin(), before.end() - kRecentFinishes);
    if (before.size() >= 3) g.sdBefore = stddevOf(before);
    if (g.sd && g.sdBefore) {
        if (*g.sd < *g.sdBefore * 0.9f) g.trend = 1;
        else if (*g.sd > *g.sdBefore * 1.1f) g.trend = -1;
    }
    return g;
}

std::vector<std::string> bossesByRecency(const PlayStatsData& d) {
    std::vector<std::pair<int, std::string>> v;
    for (const auto& [id, b] : d.bosses)
        if (b.attempts > 0) v.push_back({b.lastSeq, id});
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (auto& [s, id] : v) out.push_back(id);
    return out;
}

bool countsAsAttack(const std::string& behavior) {
    static const std::set<std::string> kAttacks = {"attack", "wave", "charge_shot", "plasma_bomb", "boomerang", "boomerang_charge", "ground_pound"};
    return kAttacks.count(behavior) > 0;
}

}  // namespace cosmic
