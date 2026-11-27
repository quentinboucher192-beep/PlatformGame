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
    return m;
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
            {"broken", broken}};
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

void TasAttemptStats::addFinished(int64_t clockMs) {
    clockMs = std::max<int64_t>(1, clockMs);
    ++attempts_;
    ++finished_;
    best_ = best_ == 0 ? clockMs : std::min(best_, clockMs);
    worst_ = std::max(worst_, clockMs);
    // Reservoir sampling: every time has the same chance to be in the sample.
    ++timesSeen_;
    if (times_.size() < kSample) times_.push_back(clockMs);
    else if (const uint64_t k = next() % static_cast<uint64_t>(timesSeen_); k < kSample) times_[static_cast<size_t>(k)] = clockMs;
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

TasAttemptWindow TasAttemptStats::take() {
    TasAttemptWindow w;
    w.sections = sections_;
    w.attempts = attempts_;
    w.finished = finished_;
    if (finished_ > 0) {
        std::vector<int64_t> t = times_;
        std::sort(t.begin(), t.end());
        w.bestMs = best_;
        w.worstMs = worst_;
        w.medianMs = std::clamp(t[t.size() / 2], best_, worst_);
    }
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

Json TasProgress::toJson() const {
    Json ws = Json::array();
    for (const TasAttemptWindow& w : windows) ws.push_back(w.toJson());
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
            {"sequence", sequence},
            {"message", message},
            {"windows", ws}};
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
    p.sequence = count("sequence");
    // The windows: the consistent ones, in order, the last kMaxWindows.
    if (const Json ws = jfield(j, "windows", Json::array()); ws.is_array())
        for (const Json& x : ws)
            if (auto w = TasAttemptWindow::fromJson(x); w && (p.windows.empty() || w->id > p.windows.back().id)) p.windows.push_back(std::move(*w));
    if (p.windows.size() > kMaxWindows) p.windows.erase(p.windows.begin(), p.windows.end() - static_cast<std::ptrdiff_t>(kMaxWindows));
    p.message = jstr(j, "message");
    if (p.message.size() > 400) {
        size_t n = 400;  // never in the middle of a UTF-8 character
        while (n > 0 && (static_cast<unsigned char>(p.message[n]) & 0xC0) == 0x80) --n;
        p.message.resize(n);
    }
    return p;
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
