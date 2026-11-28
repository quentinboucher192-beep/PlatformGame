#include "game/tas/TasProgress.h"

#include <algorithm>

namespace cosmic {

namespace {

struct PhaseName {
    TasPhase phase;
    const char* id;
};
constexpr PhaseName kPhases[] = {{TasPhase::Starting, "starting"},
                                 {TasPhase::Searching, "searching"},
                                 {TasPhase::Verifying, "verifying"},
                                 {TasPhase::Stopped, "stopped"},
                                 {TasPhase::Failed, "failed"}};

using Point = TasAttemptWindow::Point;

/// [[atMs, timeMs], ...]
Json pointsJson(const std::vector<Point>& pts) {
    Json a = Json::array();
    for (const Point& p : pts) a.push_back(Json::array({p.atMs, p.timeMs}));
    return a;
}

Json checkpointsJson(const std::vector<TasAttemptWindow::Checkpoint>& cps) {
    Json a = Json::array();
    for (const auto& c : cps)
        a.push_back({{"reached", c.reached}, {"bestMs", c.bestMs}, {"medianMs", c.medianMs}, {"worstMs", c.worstMs}, {"sample", pointsJson(c.sample)}});
    return a;
}

/// Points read: each at most `span` ms into the window, its time within [lo, hi]; at most `cap`.
bool pointsFrom(const Json& a, int64_t span, int64_t lo, int64_t hi, size_t cap, std::vector<Point>& out) {
    if (!a.is_array() || a.size() > cap) return false;
    for (const Json& x : a) {
        if (!x.is_array() || x.size() != 2 || !x[0].is_number_integer() || !x[1].is_number_integer()) return false;
        const Point p{x[0].get<int64_t>(), x[1].get<int64_t>()};
        if (p.atMs < 0 || p.atMs > span || p.timeMs < lo || p.timeMs > hi) return false;
        out.push_back(p);
    }
    return true;
}

}  // namespace

const char* tasPhaseId(TasPhase p) {
    for (const PhaseName& n : kPhases)
        if (n.phase == p) return n.id;
    return "starting";
}

bool tasPhaseFromId(const std::string& id, TasPhase& out) {
    for (const PhaseName& n : kPhases)
        if (id == n.id) {
            out = n.phase;
            return true;
        }
    return false;
}

// ------------------------------------------------------------------ attempt windows

int64_t TasAttemptWindow::brokenTotal() const {
    int64_t n = 0;
    for (int64_t b : broken) n += b;
    return n;
}

int64_t TasAttemptWindow::reached(int k) const {
    if (k <= 0) return attempts;
    int64_t n = finished;
    for (size_t j = static_cast<size_t>(k); j < stopped.size(); ++j) n += stopped[j];
    return n;
}

TasAttemptWindow TasAttemptWindow::merged(const TasAttemptWindow& a, const TasAttemptWindow& b) {
    TasAttemptWindow m = b;
    m.startMs = std::min(a.startMs, b.startMs);
    m.endMs = std::max(a.endMs, b.endMs);
    m.attempts = a.attempts + b.attempts;
    m.finished = a.finished + b.finished;
    auto lowest = [](int64_t x, int64_t y) { return x <= 0 ? y : y <= 0 ? x : std::min(x, y); };
    m.bestMs = lowest(a.bestMs, b.bestMs);
    m.worstMs = std::max(a.worstMs, b.worstMs);
    // The medians weighted by their counts (the values are gone: an approximation, said in docs/TAS.md).
    if (m.finished > 0)
        m.medianMs = static_cast<int64_t>((static_cast<double>(a.medianMs) * static_cast<double>(a.finished) +
                                           static_cast<double>(b.medianMs) * static_cast<double>(b.finished)) /
                                              static_cast<double>(m.finished) +
                                          0.5);
    const int64_t ua = a.unfinished(), ub = b.unfinished();
    if (ua + ub > 0)
        m.routeMedian = static_cast<int>((static_cast<double>(a.routeMedian) * static_cast<double>(ua) + static_cast<double>(b.routeMedian) * static_cast<double>(ub)) /
                                             static_cast<double>(ua + ub) +
                                         0.5);
    m.routeMax = std::max(a.routeMax, b.routeMax);
    m.sections = std::max(a.sections, b.sections);
    m.stopped.assign(static_cast<size_t>(m.sections), 0);
    m.broken.assign(static_cast<size_t>(m.sections), 0);
    for (const TasAttemptWindow* w : {&a, &b})
        for (size_t k = 0; k < w->stopped.size() && k < m.stopped.size(); ++k) {
            m.stopped[k] += w->stopped[k];
            m.broken[k] += k < w->broken.size() ? w->broken[k] : 0;
        }
    // The points, placed from the merged window's start; at most as many as one window keeps.
    auto rebased = [&](const std::vector<Point>& pts, int64_t from, std::vector<Point>& out) {
        for (const Point& p : pts) out.push_back({std::clamp<int64_t>(p.atMs + from - m.startMs, 0, std::max<int64_t>(0, m.endMs - m.startMs)), p.timeMs});
    };
    m.finishes.clear();
    rebased(a.finishes, a.startMs, m.finishes);
    rebased(b.finishes, b.startMs, m.finishes);
    thin(m.finishes, kMaxFinishPoints);
    m.checkpoints.assign(std::max(a.checkpoints.size(), b.checkpoints.size()), Checkpoint{});
    for (size_t k = 0; k < m.checkpoints.size(); ++k) {
        static const Checkpoint none;
        const Checkpoint& x = k < a.checkpoints.size() ? a.checkpoints[k] : none;
        const Checkpoint& y = k < b.checkpoints.size() ? b.checkpoints[k] : none;
        Checkpoint& c = m.checkpoints[k];
        c.reached = x.reached + y.reached;
        c.bestMs = lowest(x.bestMs, y.bestMs);
        c.worstMs = std::max(x.worstMs, y.worstMs);
        if (c.reached > 0)
            c.medianMs = std::clamp<int64_t>(static_cast<int64_t>((static_cast<double>(x.medianMs) * static_cast<double>(x.reached) +
                                                                   static_cast<double>(y.medianMs) * static_cast<double>(y.reached)) /
                                                                      static_cast<double>(c.reached) +
                                                                  0.5),
                                             c.bestMs, c.worstMs);
        rebased(x.sample, a.startMs, c.sample);
        rebased(y.sample, b.startMs, c.sample);
        thin(c.sample, kCheckpointSample);
    }
    return m;
}

void TasAttemptWindow::thin(std::vector<Point>& pts, size_t cap) {
    if (pts.size() <= cap) return;
    if (cap == 0) {
        pts.clear();
        return;
    }
    size_t best = 0;
    for (size_t i = 1; i < pts.size(); ++i)
        if (pts[i].timeMs < pts[best].timeMs) best = i;
    std::vector<Point> out;
    out.reserve(cap);
    out.push_back(pts[best]);
    // The others evenly spread (in their order: along the window's time).
    const size_t others = cap - 1;
    for (size_t k = 0; k < others; ++k) {
        size_t i = static_cast<size_t>((static_cast<double>(k) + 0.5) * static_cast<double>(pts.size()) / static_cast<double>(others));
        i = std::min(i, pts.size() - 1);
        if (i == best) i = i + 1 < pts.size() ? i + 1 : i - 1;
        out.push_back(pts[i]);
    }
    std::stable_sort(out.begin(), out.end(), [](const Point& p, const Point& q) { return p.atMs < q.atMs; });
    pts = std::move(out);
}

Json TasAttemptWindow::toJson() const {
    return {{"id", id},
            {"startMs", startMs},
            {"endMs", endMs},
            {"round", round},
            {"section", section},
            {"sections", sections},
            {"attempts", attempts},
            {"finished", finished},
            {"bestMs", bestMs},
            {"medianMs", medianMs},
            {"worstMs", worstMs},
            {"routeMedian", routeMedian},
            {"routeMax", routeMax},
            {"stopped", stopped},
            {"broken", broken},
            {"focusFirst", focusFirst},
            {"focusLast", focusLast},
            {"finishes", pointsJson(finishes)},
            {"checkpoints", checkpointsJson(checkpoints)}};
}

std::optional<TasAttemptWindow> TasAttemptWindow::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    TasAttemptWindow w;
    w.id = jget<int64_t>(j, "id", 0);
    w.startMs = jget<int64_t>(j, "startMs", -1);
    w.endMs = jget<int64_t>(j, "endMs", -1);
    w.round = jget<int>(j, "round", -1);
    w.section = jget<int>(j, "section", -1);
    w.sections = jget<int>(j, "sections", -1);
    w.attempts = jget<int64_t>(j, "attempts", -1);
    w.finished = jget<int64_t>(j, "finished", -1);
    w.bestMs = jget<int64_t>(j, "bestMs", -1);
    w.medianMs = jget<int64_t>(j, "medianMs", -1);
    w.worstMs = jget<int64_t>(j, "worstMs", -1);
    w.routeMedian = jget<int>(j, "routeMedian", -1);
    w.routeMax = jget<int>(j, "routeMax", -1);
    if (w.startMs < 0 || w.endMs < w.startMs || w.round < 0 || w.sections < 0 || w.sections > kMaxSections || w.section < 0 || w.section > w.sections ||
        w.attempts < 0 || w.finished < 0 || w.finished > w.attempts || w.routeMedian < 0 || w.routeMedian > 1000 || w.routeMax < w.routeMedian ||
        w.routeMax > 1000)
        return std::nullopt;
    if (w.finished == 0 ? (w.bestMs != 0 || w.medianMs != 0 || w.worstMs != 0) : (w.bestMs <= 0 || w.medianMs < w.bestMs || w.worstMs < w.medianMs))
        return std::nullopt;
    auto counts = [&](const char* key, std::vector<int64_t>& out) {
        const Json a = jfield(j, key, Json::array());
        if (!a.is_array() || a.size() != static_cast<size_t>(w.sections)) return false;
        for (const Json& x : a) {
            if (!x.is_number_integer() || x.get<int64_t>() < 0) return false;
            out.push_back(x.get<int64_t>());
        }
        return true;
    };
    if (!counts("stopped", w.stopped) || !counts("broken", w.broken)) return std::nullopt;
    int64_t sum = 0;
    for (size_t k = 0; k < w.stopped.size(); ++k) {
        if (w.broken[k] > w.stopped[k]) return std::nullopt;
        sum += w.stopped[k];
    }
    if (w.sections > 0 && sum != w.unfinished()) return std::nullopt;
    // 2.98 (absent before: the whole level, no point): the round's window, the points.
    w.focusFirst = jget<int>(j, "focusFirst", 0);
    w.focusLast = jget<int>(j, "focusLast", 0);
    if (w.focusFirst < 0 || w.focusLast < w.focusFirst || w.focusLast > kMaxSections || (w.focusFirst == 0) != (w.focusLast == 0)) return std::nullopt;
    const int64_t span = w.endMs - w.startMs;
    if (const Json f = jfield(j, "finishes", Json::array());
        !pointsFrom(f, span, w.bestMs, w.worstMs, static_cast<size_t>(std::min<int64_t>(w.finished, static_cast<int64_t>(kMaxFinishPoints))), w.finishes))
        return std::nullopt;
    const Json cps = jfield(j, "checkpoints", Json::array());
    if (!cps.is_array() || cps.size() > static_cast<size_t>(kMaxSections)) return std::nullopt;
    for (const Json& x : cps) {
        if (!x.is_object()) return std::nullopt;
        Checkpoint c;
        c.reached = jget<int64_t>(x, "reached", -1);
        c.bestMs = jget<int64_t>(x, "bestMs", -1);
        c.medianMs = jget<int64_t>(x, "medianMs", -1);
        c.worstMs = jget<int64_t>(x, "worstMs", -1);
        if (c.reached < 0 || c.reached > w.attempts) return std::nullopt;
        if (c.reached == 0 ? (c.bestMs != 0 || c.medianMs != 0 || c.worstMs != 0) : (c.bestMs <= 0 || c.medianMs < c.bestMs || c.worstMs < c.medianMs))
            return std::nullopt;
        if (!pointsFrom(jfield(x, "sample", Json::array()), span, c.bestMs, c.worstMs,
                        static_cast<size_t>(std::min<int64_t>(c.reached, static_cast<int64_t>(kCheckpointSample))), c.sample))
            return std::nullopt;
        w.checkpoints.push_back(std::move(c));
    }
    return w;
}

void TasAttemptStats::setSections(int sections) {
    sections_ = std::clamp(sections, 0, TasAttemptWindow::kMaxSections);
    stopped_.resize(static_cast<size_t>(sections_), 0);
    broken_.resize(static_cast<size_t>(sections_), 0);
}

uint64_t TasAttemptStats::next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return rng_;
}

void TasAttemptStats::setCheckpoints(int checkpoints) { cps_.resize(static_cast<size_t>(std::clamp(checkpoints, 0, TasAttemptWindow::kMaxSections))); }

void TasAttemptStats::addFinished(int64_t clockMs, int64_t atMs) {
    clockMs = std::max<int64_t>(1, clockMs);
    ++attempts_;
    ++finished_;
    if (best_ == 0 || clockMs < best_) bestAt_ = {atMs, clockMs};
    best_ = best_ == 0 ? clockMs : std::min(best_, clockMs);
    worst_ = std::max(worst_, clockMs);
    // Every one kept up to kMaxFinishPoints, then reservoir sampling: every time has the same
    // chance to be in the sample (the best is put back in at take()).
    ++timesSeen_;
    constexpr size_t cap = TasAttemptWindow::kMaxFinishPoints;
    if (times_.size() < cap) times_.push_back({atMs, clockMs});
    else if (const uint64_t k = next() % static_cast<uint64_t>(timesSeen_); k < cap) times_[static_cast<size_t>(k)] = {atMs, clockMs};
}

void TasAttemptStats::addCrossing(int checkpoint, int64_t clockMs, int64_t atMs) {
    if (checkpoint < 0 || checkpoint >= static_cast<int>(cps_.size())) return;
    clockMs = std::max<int64_t>(1, clockMs);
    Cp& c = cps_[static_cast<size_t>(checkpoint)];
    ++crossings_;
    ++c.reached;
    if (c.best == 0 || clockMs < c.best) c.bestAt = {atMs, clockMs};
    c.best = c.best == 0 ? clockMs : std::min(c.best, clockMs);
    c.worst = std::max(c.worst, clockMs);
    constexpr size_t cap = TasAttemptWindow::kCheckpointSample;
    if (c.sample.size() < cap) c.sample.push_back({atMs, clockMs});
    else if (const uint64_t k = next() % static_cast<uint64_t>(c.reached); k < cap) c.sample[static_cast<size_t>(k)] = {atMs, clockMs};
}

void TasAttemptStats::addStopped(int section, int routePermille, bool broke) {
    ++attempts_;
    routePermille = std::clamp(routePermille, 0, 1000);
    routeMax_ = std::max(routeMax_, routePermille);
    ++routesSeen_;
    if (routes_.size() < kSample) routes_.push_back(routePermille);
    else if (const uint64_t k = next() % static_cast<uint64_t>(routesSeen_); k < kSample) routes_[static_cast<size_t>(k)] = routePermille;
    if (sections_ <= 0) return;
    const size_t s = static_cast<size_t>(std::clamp(section, 0, sections_ - 1));
    ++stopped_[s];
    if (broke) ++broken_[s];
}

TasAttemptWindow TasAttemptStats::take(int64_t startMs, int64_t endMs) {
    TasAttemptWindow w;
    w.sections = sections_;
    w.attempts = attempts_;
    w.finished = finished_;
    const int64_t span = std::max<int64_t>(0, endMs - startMs);
    auto placed = [&](TasAttemptWindow::Point p) { return TasAttemptWindow::Point{std::clamp<int64_t>(p.atMs - startMs, 0, span), p.timeMs}; };
    // A sample with its best: the best put back in place of its worst point when it was left out.
    auto withBest = [&](std::vector<TasAttemptWindow::Point> pts, TasAttemptWindow::Point best) {
        if (std::find(pts.begin(), pts.end(), best) == pts.end()) {
            auto worst = std::max_element(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.timeMs < b.timeMs; });
            if (worst != pts.end()) *worst = best;
            else pts.push_back(best);
        }
        for (auto& p : pts) p = placed(p);
        std::stable_sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.atMs < b.atMs; });
        return pts;
    };
    if (finished_ > 0) {
        std::vector<int64_t> t;
        for (const auto& p : times_) t.push_back(p.timeMs);
        std::sort(t.begin(), t.end());
        w.bestMs = best_;
        w.worstMs = worst_;
        w.medianMs = std::clamp(t[t.size() / 2], best_, worst_);
        w.finishes = withBest(times_, bestAt_);
    }
    for (Cp& c : cps_) {
        TasAttemptWindow::Checkpoint k;
        k.reached = std::min(c.reached, attempts_);
        if (k.reached > 0) {
            std::vector<int64_t> t;
            for (const auto& p : c.sample) t.push_back(p.timeMs);
            std::sort(t.begin(), t.end());
            k.bestMs = c.best;
            k.worstMs = c.worst;
            k.medianMs = std::clamp(t[t.size() / 2], c.best, c.worst);
            k.sample = withBest(c.sample, c.bestAt);
            if (static_cast<int64_t>(k.sample.size()) > k.reached) k.sample.resize(static_cast<size_t>(k.reached));
        }
        w.checkpoints.push_back(std::move(k));
        c = Cp{};
    }
    crossings_ = 0;
    if (std::all_of(w.checkpoints.begin(), w.checkpoints.end(), [](const auto& k) { return k.reached == 0; })) w.checkpoints.clear();
    if (!routes_.empty()) {
        std::vector<int> r = routes_;
        std::sort(r.begin(), r.end());
        w.routeMedian = r[r.size() / 2];
        w.routeMax = std::max(routeMax_, w.routeMedian);
    }
    w.stopped = stopped_;
    w.broken = broken_;
    // Without sections (a level the search could not cut): every attempt that did not finish is
    // nowhere in particular; the window still adds up.
    attempts_ = finished_ = best_ = worst_ = timesSeen_ = routesSeen_ = 0;
    routeMax_ = 0;
    times_.clear();
    routes_.clear();
    std::fill(stopped_.begin(), stopped_.end(), 0);
    std::fill(broken_.begin(), broken_.end(), 0);
    return w;
}

// ------------------------------------------------------------------ progress

std::optional<std::vector<int64_t>> tasSplitsFromJson(const Json& j, int64_t timeMs) {
    if (!j.is_array() || j.size() > static_cast<size_t>(TasAttemptWindow::kMaxSections)) return std::nullopt;
    std::vector<int64_t> out;
    int64_t last = 0;
    for (const Json& x : j) {
        if (!x.is_number_integer()) return std::nullopt;
        const int64_t v = x.get<int64_t>();
        if (v < 0 || v > 24 * 3600 * 1000LL || (timeMs > 0 && v > timeMs) || (v > 0 && v < last)) return std::nullopt;
        if (v > 0) last = v;
        out.push_back(v);
    }
    return out;
}

std::vector<int64_t> tasSegmentDeltas(const std::vector<int64_t>& oldSplits, int64_t oldMs, const std::vector<int64_t>& newSplits, int64_t newMs) {
    if (oldSplits.empty() || oldSplits.size() != newSplits.size() || oldMs <= 0 || newMs <= 0) return {};
    for (size_t k = 0; k < oldSplits.size(); ++k)
        if (oldSplits[k] <= 0 || newSplits[k] <= 0) return {};
    std::vector<int64_t> d;
    int64_t po = 0, pn = 0;
    for (size_t k = 0; k <= oldSplits.size(); ++k) {
        const int64_t o = k < oldSplits.size() ? oldSplits[k] : oldMs, n = k < newSplits.size() ? newSplits[k] : newMs;
        d.push_back((n - pn) - (o - po));
        po = o;
        pn = n;
    }
    return d;
}

std::string tasSegmentName(size_t k, size_t checkpoints) {
    const std::string from = k == 0 ? std::string("Départ") : "CP" + std::to_string(k);
    const std::string to = k >= checkpoints ? std::string("Arrivée") : "CP" + std::to_string(k + 1);
    return from + " → " + to;
}

Json TasProgress::toJson() const {
    Json strategiesJson = Json::array();
    for (const TasStrategy& s : strategies) strategiesJson.push_back({{"id", s.id}, {"timeMs", s.timeMs}, {"gainMs", s.gainMs}, {"wins", s.wins}});
    Json ws = Json::array();
    for (const TasAttemptWindow& w : windows) ws.push_back(w.toJson());
    Json cps = Json::array();
    for (const TasCheckpoint& c : checkpoints) cps.push_back({{"id", c.id}, {"label", c.label}});
    Json refs = Json::array();
    for (const Reference& r : guides) {
        refs.push_back({{"player", r.player}, {"timeMs", r.timeMs}, {"splits", r.splits}});
        if (r.oldVersion) refs.back()["oldVersion"] = true;
    }
    return {{"format", kTasProgressFormat},
            {"phase", tasPhaseId(phase)},
            {"level", levelId},
            {"category", categoryId(category)},
            {"elapsedMs", elapsedMs},
            {"explored", explored},
            {"seedMs", seedMs},
            {"bestMs", bestMs},
            {"candidateMs", candidateMs},
            {"improvements", improvements},
            {"bestElapsedMs", bestElapsedMs},
            {"bestExplored", bestExplored},
            {"round", round},
            {"threads", threads},
            {"section", section},
            {"sections", sections},
            {"focusFirst", focusFirst},
            {"focusLast", focusLast},
            {"checkpoints", cps},
            {"bestSplits", bestSplits},
            {"bestSource", bestSource},
            {"verifying", verifying},
            {"guides", refs},
            {"sequence", sequence},
            {"message", message},
            {"windows", ws},
            {"strategies", strategiesJson}};
}

std::optional<TasProgress> TasProgress::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<TasProgress> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("pas un objet JSON");
    if (jstr(j, "format") != kTasProgressFormat) return fail("format inconnu");
    TasProgress p;
    if (!tasPhaseFromId(jstr(j, "phase"), p.phase)) return fail("phase inconnue");
    p.levelId = jstr(j, "level");
    if (p.levelId.empty()) return fail("niveau absent");
    if (!categoryFromId(jstr(j, "category"), p.category)) return fail("catégorie inconnue");
    auto count = [&](const char* k) { return std::max<int64_t>(0, jget<int64_t>(j, k, 0)); };
    p.elapsedMs = count("elapsedMs");
    p.explored = count("explored");
    p.seedMs = count("seedMs");
    p.bestMs = count("bestMs");
    p.candidateMs = count("candidateMs");
    p.improvements = std::max(0, jget<int>(j, "improvements", 0));
    p.bestElapsedMs = std::min(count("bestElapsedMs"), p.elapsedMs);
    p.bestExplored = std::min(count("bestExplored"), p.explored);
    p.round = std::max(0, jget<int>(j, "round", 0));
    p.threads = std::max(0, jget<int>(j, "threads", 0));
    p.sections = std::clamp(jget<int>(j, "sections", 0), 0, TasAttemptWindow::kMaxSections);
    p.section = std::clamp(jget<int>(j, "section", 0), 0, p.sections);
    // 2.98 (absent before: the whole level, no checkpoint named).
    p.focusFirst = std::clamp(jget<int>(j, "focusFirst", 0), 0, TasAttemptWindow::kMaxSections);
    p.focusLast = std::clamp(jget<int>(j, "focusLast", 0), p.focusFirst, TasAttemptWindow::kMaxSections);
    if (p.focusFirst == 0) p.focusLast = 0;
    if (const Json cps = jfield(j, "checkpoints", Json::array()); cps.is_array())
        for (const Json& c : cps) {
            if (p.checkpoints.size() >= static_cast<size_t>(TasAttemptWindow::kMaxSections) || !c.is_object()) break;
            TasCheckpoint k{jstr(c, "id"), jstr(c, "label")};
            if (k.id.size() > 64 || k.label.size() > 120) break;
            p.checkpoints.push_back(std::move(k));
        }
    // 2.99 (absent before: no splits, no source, no guide).
    if (auto sp = tasSplitsFromJson(jfield(j, "bestSplits", Json::array()), p.bestMs)) p.bestSplits = std::move(*sp);
    p.bestSource = jstr(j, "bestSource").substr(0, 16);
    p.verifying = jget<bool>(j, "verifying", false);
    if (const Json gs = jfield(j, "guides", Json::array()); gs.is_array())
        for (const Json& g : gs) {
            if (p.guides.size() >= kMaxReferences || !g.is_object()) break;
            Reference r;
            r.player = jstr(g, "player").substr(0, 64);
            r.timeMs = std::max<int64_t>(0, jget<int64_t>(g, "timeMs", 0));
            if (auto sp = tasSplitsFromJson(jfield(g, "splits", Json::array()), r.timeMs)) r.splits = std::move(*sp);
            r.oldVersion = jget<bool>(g, "oldVersion", false);
            p.guides.push_back(std::move(r));
        }
    p.sequence = count("sequence");
    // The windows: the consistent ones, in order, the last kMaxWindows.
    if (const Json ws = jfield(j, "windows", Json::array()); ws.is_array())
        for (const Json& x : ws)
            if (auto w = TasAttemptWindow::fromJson(x); w && (p.windows.empty() || w->id > p.windows.back().id)) p.windows.push_back(std::move(*w));
    if (p.windows.size() > kMaxWindows) p.windows.erase(p.windows.begin(), p.windows.end() - static_cast<std::ptrdiff_t>(kMaxWindows));
    // The portfolio's arms (absent before 2.99).
    if (const Json ss = jfield(j, "strategies", Json::array()); ss.is_array())
        for (const Json& x : ss) {
            if (p.strategies.size() >= TasStrategy::kMax || !x.is_object()) break;
            TasStrategy s;
            s.id = jstr(x, "id");
            if (s.id.empty() || s.id.size() > 32) break;
            s.timeMs = std::max<int64_t>(0, jget<int64_t>(x, "timeMs", 0));
            s.gainMs = std::max<int64_t>(0, jget<int64_t>(x, "gainMs", 0));
            s.wins = std::max(0, jget<int>(x, "wins", 0));
            p.strategies.push_back(std::move(s));
        }
    p.message = jstr(j, "message");
    if (p.message.size() > 400) {
        size_t n = 400;  // never in the middle of a UTF-8 character
        while (n > 0 && (static_cast<unsigned char>(p.message[n]) & 0xC0) == 0x80) --n;
        p.message.resize(n);
    }
    return p;
}

std::string tasStrategyLabel(const std::string& id) {
    if (id == "beam") return "faisceau";
    if (id == "explore") return "exploration";
    if (id == "timing") return "polissage";
    if (id == "refine") return "affinage";
    if (id == "planner") return "planificateur";
    if (id == "guides") return "guides";
    if (id == "phase") return "réalignement de phase";
    return id;
}

std::string tasFoundByText(const std::vector<TasStrategy>& s) {
    std::string list;
    for (const TasStrategy& x : s)
        if (x.wins > 0) list += (list.empty() ? "" : " · ") + tasStrategyLabel(x.id) + " " + std::to_string(x.wins);
    return list.empty() ? std::string() : "Trouvé par : " + list;
}

std::string tasTimeShareText(const std::vector<TasStrategy>& s) {
    int64_t total = 0;
    for (const TasStrategy& x : s) total += x.timeMs;
    if (total <= 0) return {};
    std::string list;
    for (const TasStrategy& x : s)
        if (x.timeMs > 0) list += (list.empty() ? "" : " · ") + tasStrategyLabel(x.id) + " " + std::to_string((x.timeMs * 100 + total / 2) / total) + " %";
    return "Part du temps : " + list;
}

std::optional<TasProgress> TasProgress::parse(const std::string& text, std::string* error) {
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "JSON invalide";
        return std::nullopt;
    }
    return fromJson(j, error);
}

}  // namespace cosmic
