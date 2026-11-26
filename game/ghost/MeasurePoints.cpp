#include "game/ghost/MeasurePoints.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"

namespace cosmic {

namespace {

bool validKey(const std::string& s) {
    if (s.empty() || s.size() > 96) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::abs(v.x) < 1e7f && std::abs(v.y) < 1e7f; }

bool validSeconds(float s) { return std::isfinite(s) && s > 0.0f && s <= 7200.0f; }

long long ms(float s) { return std::llround(static_cast<double>(s) * 1000.0); }

/// A player's name for a pair: one line, no control character, at most kNameMax bytes (cut on a
/// character boundary).
std::string cleanName(const std::string& in) {
    std::string out;
    for (unsigned char c : in)
        if (c >= 0x20 && c != 0x7f) out += static_cast<char>(c);
    if (out.size() > MeasureStore::kNameMax) {
        size_t n = MeasureStore::kNameMax;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;
        out.resize(n);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

Json gateJson(const MeasureGate& g) { return Json{g.at.x, g.at.y}; }

std::optional<MeasureGate> readGate(const Json& j) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return std::nullopt;
    MeasureGate g;
    g.at = {j[0].get<float>(), j[1].get<float>()};
    if (!finite(g.at)) return std::nullopt;
    return g;
}

}  // namespace

// ------------------------------------------------------------------ timing

std::optional<float> MeasureGate::crossing(Vec2 a, Vec2 b) const {
    const float x = at.x;
    // From one side to the other (a move ending on the line crosses it, one starting there does not).
    const bool right = a.x < x && b.x >= x, left = a.x > x && b.x <= x;
    if (!right && !left) return std::nullopt;
    const float f = (x - a.x) / (b.x - a.x);
    const float y = a.y + (b.y - a.y) * f;
    if (y < top() || y > bottom()) return std::nullopt;
    return std::clamp(f, 0.0f, 1.0f);
}

void MeasureTimer::reset() {
    hasPrev_ = false;
    started_ = -1.0f;
}

std::optional<float> MeasureTimer::feed(float t, Vec2 feet) {
    if (!std::isfinite(t) || !finite(feet)) return std::nullopt;
    if (!hasPrev_ || t < prevT_ || (feet - prev_).lengthSq() > kJump * kJump) {
        // The first position, or a jump: nothing crossed, the chronometer stopped.
        started_ = -1.0f;
        hasPrev_ = true;
        prevT_ = t;
        prev_ = feet;
        return std::nullopt;
    }
    std::optional<float> out;
    // The gates crossed by this move, in its order (B first on a tie: a run in progress ends
    // before a new one starts).
    struct Crossed {
        float f;
        bool isA;
    };
    Crossed ev[2];
    int n = 0;
    if (auto f = b_.crossing(prev_, feet)) ev[n++] = {*f, false};
    if (auto f = a_.crossing(prev_, feet)) ev[n++] = {*f, true};
    if (n == 2 && ev[1].f < ev[0].f) std::swap(ev[0], ev[1]);
    for (int i = 0; i < n; ++i) {
        const float when = prevT_ + (t - prevT_) * ev[i].f;
        if (ev[i].isA) {
            started_ = when;
        } else if (running()) {
            out = std::max(0.0f, when - started_);
            started_ = -1.0f;
        }
    }
    prevT_ = t;
    prev_ = feet;
    return out;
}

std::vector<float> measureTimes(const std::vector<std::pair<float, Vec2>>& samples, const MeasureGate& a, const MeasureGate& b) {
    std::vector<float> out;
    MeasureTimer timer(a, b);
    for (const auto& [t, p] : samples)
        if (auto d = timer.feed(t, p)) out.push_back(*d);
    return out;
}

// ------------------------------------------------------------------ the store

MeasureStore::MeasureStore(fs::path file) : file_(std::move(file)) {}

void MeasureStore::load() {
    levels_.clear();
    readOnly_ = false;
    if (file_.empty()) file_ = Paths::savesDir() / "measures.json";
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    auto j = FileUtil::loadJson(file_);
    if (!j || !j->is_object() || jstr(*j, "format") != "cosmic.measures" || !(*j)["levels"].is_object()) {
        fs::path bad = file_;
        bad += ".corrupt";
        fs::remove(bad, ec);
        fs::rename(file_, bad, ec);
        return;
    }
    readOnly_ = jget<int>(*j, "version", 0) > kVersion;
    showGates = jget<bool>(*j, "show_gates", true);
    inReplays = jget<bool>(*j, "in_replays", true);
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (levels_.size() >= kLevelsKept) break;
        if (!validKey(key) || !lj.is_object()) continue;
        Level l;
        l.touched = jstr(lj, "touched").substr(0, 32);
        for (const Json& pj : jfield(lj, "pairs", Json::array())) {
            if (l.pairs.size() >= kPerLevel || !pj.is_object()) break;
            MeasurePair p;
            p.id = jget<int>(pj, "id", 0);
            auto a = readGate(jfield(pj, "a"));
            if (!a || p.id <= 0 || p.id > 1000000 || std::any_of(l.pairs.begin(), l.pairs.end(), [&](const MeasurePair& o) { return o.id == p.id; }))
                continue;
            p.a = *a;
            if (pj.contains("b")) {
                p.b = readGate(pj["b"]);
                if (!p.b) continue;
            }
            p.name = cleanName(jstr(pj, "name"));
            p.date = jstr(pj, "date").substr(0, 32);
            if (p.b) {
                p.best = jget<float>(pj, "best", -1.0f);
                if (!validSeconds(p.best)) p.best = -1.0f;
                p.bestDate = jstr(pj, "best_date").substr(0, 32);
                p.attempts = std::clamp(jget<int>(pj, "attempts", 0), 0, 10000000);
                for (const Json& v : jfield(pj, "last", Json::array()))
                    if (v.is_number() && validSeconds(v.get<float>()) && p.last.size() < MeasurePair::kLastKept) p.last.push_back(v.get<float>());
            }
            l.nextId = std::max(l.nextId, p.id + 1);
            l.pairs.push_back(std::move(p));
        }
        l.nextId = std::max(l.nextId, std::clamp(jget<int>(lj, "next", 1), 1, 1000001));
        if (!l.pairs.empty()) levels_[key] = std::move(l);
    }
}

bool MeasureStore::save() {
    if (file_.empty() || readOnly_) return false;
    Json levels = Json::object();
    for (const auto& [key, l] : levels_) {
        Json pairs = Json::array();
        for (const MeasurePair& p : l.pairs) {
            Json pj{{"id", p.id}, {"name", p.name}, {"a", gateJson(p.a)}, {"date", p.date}};
            if (p.b) {
                pj["b"] = gateJson(*p.b);
                pj["best"] = p.best;
                pj["best_date"] = p.bestDate;
                pj["attempts"] = p.attempts;
                pj["last"] = p.last;
            }
            pairs.push_back(std::move(pj));
        }
        levels[key] = Json{{"touched", l.touched}, {"next", l.nextId}, {"pairs", pairs}};
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    Json root{{"format", "cosmic.measures"}, {"version", kVersion}, {"show_gates", showGates}, {"in_replays", inReplays}, {"levels", levels}};
    return FileUtil::writeTextAtomic(file_, root.dump(1, ' ', false, Json::error_handler_t::replace) + "\n");
}

std::vector<MeasurePair> MeasureStore::list(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return {};
    std::vector<MeasurePair> out = it->second.pairs;
    std::sort(out.begin(), out.end(), [](const MeasurePair& a, const MeasurePair& b) { return a.id < b.id; });
    return out;
}

const MeasurePair* MeasureStore::find(const std::string& levelKey, int id) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return nullptr;
    for (const MeasurePair& p : it->second.pairs)
        if (p.id == id) return &p;
    return nullptr;
}

MeasurePair* MeasureStore::get(const std::string& levelKey, int id) { return const_cast<MeasurePair*>(find(levelKey, id)); }

int MeasureStore::place(const std::string& levelKey, Vec2 feet, const std::string& date) {
    if (!validKey(levelKey) || !finite(feet)) return 0;
    auto it = levels_.find(levelKey);
    if (it != levels_.end()) {
        it->second.touched = date.substr(0, 32);
        for (MeasurePair& p : it->second.pairs)
            if (!p.b) {
                p.b = MeasureGate{feet};
                return p.id;
            }
    } else {
        while (levels_.size() >= kLevelsKept) {
            auto oldest = std::min_element(levels_.begin(), levels_.end(), [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
            levels_.erase(oldest);
        }
        it = levels_.emplace(levelKey, Level{}).first;
        it->second.touched = date.substr(0, 32);
    }
    Level& l = it->second;
    if (l.pairs.size() >= kPerLevel) return 0;
    MeasurePair p;
    p.id = l.nextId++;
    p.a = MeasureGate{feet};
    p.date = date.substr(0, 32);
    l.pairs.push_back(p);
    return p.id;
}

void MeasureStore::rename(const std::string& levelKey, int id, const std::string& name) {
    if (MeasurePair* p = get(levelKey, id)) p->name = cleanName(name);
}

void MeasureStore::move(const std::string& levelKey, int id, bool gateB, Vec2 feet) {
    MeasurePair* p = get(levelKey, id);
    if (!p || !finite(feet)) return;
    if (gateB) p->b = MeasureGate{feet};
    else p->a = MeasureGate{feet};
    // Another A → B: the times measured before do not compare any more.
    p->best = -1.0f;
    p->bestDate.clear();
    p->last.clear();
    p->attempts = 0;
}

bool MeasureStore::remove(const std::string& levelKey, int id) {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return false;
    auto& ps = it->second.pairs;
    auto p = std::find_if(ps.begin(), ps.end(), [&](const MeasurePair& x) { return x.id == id; });
    if (p == ps.end()) return false;
    ps.erase(p);
    if (ps.empty()) levels_.erase(it);
    return true;
}

void MeasureStore::clear(const std::string& levelKey) { levels_.erase(levelKey); }

bool MeasureStore::addTime(const std::string& levelKey, int id, float seconds, const std::string& date) {
    MeasurePair* p = get(levelKey, id);
    if (!p || !p->b || !validSeconds(seconds)) return false;
    p->attempts = std::min(p->attempts + 1, 10000000);
    p->last.push_back(seconds);
    if (p->last.size() > MeasurePair::kLastKept) p->last.erase(p->last.begin());
    if (p->best > 0.0f && ms(seconds) >= ms(p->best)) return false;
    p->best = seconds;
    p->bestDate = date.substr(0, 32);
    return true;
}

}  // namespace cosmic
