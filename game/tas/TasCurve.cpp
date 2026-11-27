#include "game/tas/TasCurve.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>
#include <tuple>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

namespace {

constexpr const char* kCurveFormat = "cosmic.tas.curve";

const char* shareId(TasShare s) { return s == TasShare::Pending ? "pending" : s == TasShare::Unshared ? "unshared" : "shared"; }

TasShare shareFromId(const std::string& id) { return id == "pending" ? TasShare::Pending : id == "unshared" ? TasShare::Unshared : TasShare::Shared; }

int64_t nonNegative(const Json& j, const char* key) { return std::max<int64_t>(0, jget<int64_t>(j, key, 0)); }

/// The administrator of a server's entry: { id, username } or null.
std::string byOf(const Json& j) {
    const Json& by = jfield(j, "by", Json());
    return by.is_object() ? jstr(by, "username") : std::string();
}

/// A window of the server's (snake_case) as the progress file writes one, for its checks.
Json camelWindow(const Json& j) {
    const int64_t start = jget<int64_t>(j, "start_ms", 0), end = jget<int64_t>(j, "end_ms", 0);
    return {{"id", 0},
            {"startMs", 0},
            {"endMs", std::max<int64_t>(0, end - start)},
            {"round", jfield(j, "round", Json())},
            {"section", jfield(j, "section", Json())},
            {"sections", jfield(j, "sections", Json())},
            {"attempts", jfield(j, "attempts", Json())},
            {"finished", jfield(j, "finished", Json())},
            {"bestMs", jfield(j, "best_ms", Json())},
            {"medianMs", jfield(j, "median_ms", Json())},
            {"worstMs", jfield(j, "worst_ms", Json())},
            {"routeMedian", jfield(j, "route_median", Json())},
            {"routeMax", jfield(j, "route_max", Json())},
            {"stopped", jfield(j, "stopped", Json::array())},
            {"broken", jfield(j, "broken", Json::array())}};
}

/// Two neighbouring windows that can be one (beyond kMaxWindows).
bool mergeable(const TasCurveWindow& a, const TasCurveWindow& b) {
    return a.session == b.session && a.local == b.local && a.share == b.share && a.leaseId == b.leaseId && a.stats.round == b.stats.round &&
           a.stats.section == b.stats.section && a.stats.sections == b.stats.sections;
}

TasCurveWindow mergedWindow(const TasCurveWindow& a, const TasCurveWindow& b) {
    TasCurveWindow m = b;
    m.fromMs = std::min(a.fromMs, b.fromMs);
    m.toMs = std::max(a.toMs, b.toMs);
    m.stats = TasAttemptWindow::merged(a.stats, b.stats);
    return m;
}

void sortWindows(std::vector<TasCurveWindow>& ws) {
    std::stable_sort(ws.begin(), ws.end(), [](const TasCurveWindow& a, const TasCurveWindow& b) { return a.fromMs < b.fromMs; });
}

/// At most TasCurve::kMaxWindows: neighbours merged from the oldest (to three quarters, so that
/// it is not done again at every new window), else the oldest dropped.
void capWindows(std::vector<TasCurveWindow>& ws) {
    if (ws.size() <= TasCurve::kMaxWindows) return;
    const size_t target = TasCurve::kMaxWindows * 3 / 4;
    std::vector<TasCurveWindow> out;
    out.reserve(ws.size());
    size_t excess = ws.size() - target;
    for (size_t i = 0; i < ws.size(); ++i) {
        if (excess > 0 && !out.empty() && mergeable(out.back(), ws[i])) {
            out.back() = mergedWindow(out.back(), ws[i]);
            --excess;
            continue;
        }
        out.push_back(ws[i]);
    }
    if (out.size() > TasCurve::kMaxWindows) out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(out.size() - TasCurve::kMaxWindows));
    ws = std::move(out);
}

}  // namespace

const char* tasCurveKindId(TasCurveKind k) {
    switch (k) {
        case TasCurveKind::Start: return "start";
        case TasCurveKind::Improvement: return "improvement";
        case TasCurveKind::Publish: return "publish";
    }
    return "improvement";
}

bool tasCurveKindFromId(const std::string& id, TasCurveKind& out) {
    for (TasCurveKind k : {TasCurveKind::Start, TasCurveKind::Improvement, TasCurveKind::Publish})
        if (id == tasCurveKindId(k)) {
            out = k;
            return true;
        }
    return false;
}

std::string tasIsoUtc(int64_t epochMs) {
    const std::time_t t = static_cast<std::time_t>(std::max<int64_t>(0, epochMs) / 1000);
    std::tm m{};
#ifdef _WIN32
    gmtime_s(&m, &t);
#else
    gmtime_r(&t, &m);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &m);
    return b;
}

int64_t tasNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------ points and windows

bool TasCurvePoint::sameAs(const TasCurvePoint& o) const {
    return kind == o.kind && timeMs == o.timeMs && trainingMs == o.trainingMs && explored == o.explored && at == o.at && by == o.by &&
           revision == o.revision && session == o.session && share == o.share;
}

Json TasCurvePoint::toJson() const {
    Json j = {{"kind", tasCurveKindId(kind)}, {"timeMs", timeMs}, {"trainingMs", trainingMs}, {"explored", explored}, {"at", at},
              {"by", by},                     {"session", session}, {"share", shareId(share)}};
    if (revision > 0) j["revision"] = revision;
    return j;
}

std::optional<TasCurvePoint> TasCurvePoint::fromJson(const Json& j) {
    TasCurvePoint p;
    if (!j.is_object() || !tasCurveKindFromId(jstr(j, "kind"), p.kind)) return std::nullopt;
    p.timeMs = jget<int64_t>(j, "timeMs", 0);
    if (p.timeMs <= 0) return std::nullopt;
    p.trainingMs = nonNegative(j, "trainingMs");
    p.explored = nonNegative(j, "explored");
    p.at = jstr(j, "at");
    p.by = jstr(j, "by");
    p.revision = std::max(0, jget<int>(j, "revision", 0));
    p.session = std::max(0, jget<int>(j, "session", 0));
    p.share = shareFromId(jstr(j, "share"));
    if (p.share == TasShare::Pending) p.share = TasShare::Unshared;  // its lease ended with the game that wrote it
    return p;
}

std::optional<TasCurvePoint> TasCurvePoint::fromServer(const Json& j) {
    TasCurvePoint p;
    if (!j.is_object() || !tasCurveKindFromId(jstr(j, "kind"), p.kind)) return std::nullopt;
    p.timeMs = jget<int64_t>(j, "time_ms", 0);
    if (p.timeMs <= 0) return std::nullopt;
    p.trainingMs = nonNegative(j, "training_ms");
    p.explored = nonNegative(j, "explored");
    p.at = jstr(j, "at");
    p.by = byOf(j);
    p.revision = std::max(0, jget<int>(j, "revision", 0));
    p.session = std::max(0, jget<int>(j, "session", 0));
    p.share = TasShare::Shared;
    return p;
}

Json TasCurveWindow::toJson() const {
    return {{"fromMs", fromMs}, {"toMs", toMs}, {"session", session}, {"by", by}, {"at", at}, {"local", local}, {"share", shareId(share)}, {"stats", stats.toJson()}};
}

std::optional<TasCurveWindow> TasCurveWindow::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    auto stats = TasAttemptWindow::fromJson(jfield(j, "stats", Json()));
    if (!stats) return std::nullopt;
    TasCurveWindow w;
    w.stats = std::move(*stats);
    w.fromMs = nonNegative(j, "fromMs");
    w.toMs = std::max(w.fromMs, nonNegative(j, "toMs"));
    w.session = std::max(0, jget<int>(j, "session", 0));
    w.by = jstr(j, "by");
    w.at = jstr(j, "at");
    w.local = jget<bool>(j, "local", false);
    w.share = shareFromId(jstr(j, "share"));
    if (w.share == TasShare::Pending) w.share = TasShare::Unshared;
    return w;
}

std::optional<TasCurveWindow> TasCurveWindow::fromServer(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    auto stats = TasAttemptWindow::fromJson(camelWindow(j));
    if (!stats) return std::nullopt;
    TasCurveWindow w;
    w.stats = std::move(*stats);
    w.fromMs = nonNegative(j, "start_ms");
    w.toMs = std::max(w.fromMs, nonNegative(j, "end_ms"));
    w.session = std::max(0, jget<int>(j, "session", 0));
    w.by = byOf(j);
    w.at = jstr(j, "at");
    w.share = TasShare::Shared;
    return w;
}

int64_t TasCurveVersion::bestMs() const {
    int64_t best = 0;
    for (const TasCurvePoint& p : points)
        if (p.best() && (best == 0 || p.timeMs < best)) best = p.timeMs;
    return best;
}

size_t TasCurveVersion::bests() const {
    return static_cast<size_t>(std::count_if(points.begin(), points.end(), [](const TasCurvePoint& p) { return p.best(); }));
}

int64_t TasCurveVersion::lastTrainingMs() const {
    int64_t t = 0;
    for (const TasCurvePoint& p : points) t = std::max(t, p.trainingMs);
    for (const TasCurveWindow& w : windows) t = std::max(t, w.toMs);
    return t;
}

TasAttemptWindow tasAttemptTotal(const std::vector<const TasCurveWindow*>& windows) {
    TasAttemptWindow total;
    bool first = true;
    for (const TasCurveWindow* w : windows) {
        if (!w) continue;
        total = first ? w->stats : TasAttemptWindow::merged(total, w->stats);
        first = false;
    }
    return total;
}

// ------------------------------------------------------------------ a pair's curve

const TasCurveVersion* TasCurve::find(const std::string& hash, int sim) const {
    for (const TasCurveVersion& v : versions)
        if (v.levelHash == hash && v.simVersion == sim) return &v;
    return nullptr;
}

TasCurveVersion& TasCurve::version(const std::string& hash, int sim) {
    for (TasCurveVersion& v : versions)
        if (v.levelHash == hash && v.simVersion == sim) return v;
    versions.push_back(TasCurveVersion{hash, sim, {}, {}});
    return versions.back();
}

void TasCurve::normalize(TasCurveVersion& v) {
    std::stable_sort(v.points.begin(), v.points.end(), [](const TasCurvePoint& a, const TasCurvePoint& b) { return a.trainingMs < b.trainingMs; });
    std::vector<TasCurvePoint> out;
    out.reserve(v.points.size());
    int64_t best = 0;
    size_t bests = 0;
    for (TasCurvePoint& p : v.points) {
        if (p.timeMs <= 0) continue;
        if (p.best()) {
            if (best > 0 && p.timeMs >= best) continue;  // not an improvement: never a point
            best = p.timeMs;
            ++bests;
        }
        out.push_back(std::move(p));
    }
    // Beyond the cap, the best points before the last give way (the start and the best stay).
    while (bests > kMaxPoints) {
        size_t last = out.size(), before = out.size();
        for (size_t i = out.size(); i-- > 0;)
            if (out[i].best()) {
                if (last == out.size()) last = i;
                else {
                    before = i;
                    break;
                }
            }
        if (before == out.size()) break;
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(before));
        --bests;
    }
    v.points = std::move(out);
}

bool TasCurve::addBest(const std::string& hash, int sim, TasCurvePoint p) {
    if (p.timeMs <= 0 || !p.best()) return false;
    TasCurveVersion& v = version(hash, sim);
    const int64_t best = v.bestMs();
    if (best > 0 && p.timeMs >= best) return false;
    // Never before the last point: the training time only grows.
    for (const TasCurvePoint& q : v.points) p.trainingMs = std::max(p.trainingMs, q.best() ? q.trainingMs : 0);
    if (v.bests() >= kMaxPoints) {
        for (size_t i = v.points.size(); i-- > 0;)
            if (v.points[i].best()) {
                v.points.erase(v.points.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    }
    v.points.push_back(std::move(p));
    normalize(v);
    return true;
}

void TasCurve::addWindow(const std::string& hash, int sim, TasCurveWindow w) {
    TasCurveVersion& v = version(hash, sim);
    auto at = std::upper_bound(v.windows.begin(), v.windows.end(), w.fromMs, [](int64_t x, const TasCurveWindow& o) { return x < o.fromMs; });
    v.windows.insert(at, std::move(w));
    capWindows(v.windows);
}

bool TasCurve::mergeServerPoints(const std::string& hash, int sim, std::vector<TasCurvePoint> pts) {
    TasCurveVersion& v = version(hash, sim);
    const std::vector<TasCurvePoint> before = v.points;
    for (TasCurvePoint& p : pts) p.share = TasShare::Shared;
    for (const TasCurvePoint& p : before)
        if (p.share != TasShare::Shared) pts.push_back(p);
    v.points = std::move(pts);
    normalize(v);
    if (v.points.size() != before.size()) return true;
    for (size_t i = 0; i < before.size(); ++i)
        if (!v.points[i].sameAs(before[i])) return true;
    return false;
}

bool TasCurve::mergeServerWindows(const std::string& hash, int sim, std::vector<TasCurveWindow> ws) {
    TasCurveVersion& v = version(hash, sim);
    std::set<int> mine;
    for (const TasCurveWindow& w : v.windows)
        if (w.local) mine.insert(w.session);
    std::vector<TasCurveWindow> out;
    for (const TasCurveWindow& w : v.windows)
        if (w.local) out.push_back(w);
    for (TasCurveWindow& w : ws)
        if (!mine.count(w.session)) {
            w.local = false;
            w.share = TasShare::Shared;
            out.push_back(std::move(w));
        }
    sortWindows(out);
    capWindows(out);
    // Changed: the others' windows are not what they were (their count, attempts, end).
    auto signature = [](const std::vector<TasCurveWindow>& list) {
        int64_t n = 0, attempts = 0, end = 0;
        for (const TasCurveWindow& w : list)
            if (!w.local) ++n, attempts += w.stats.attempts, end = std::max(end, w.toMs);
        return std::make_tuple(n, attempts, end);
    };
    const bool changed = signature(out) != signature(v.windows);
    v.windows = std::move(out);
    return changed;
}

void TasCurve::acknowledge(const std::string& hash, int sim, const std::string& leaseId, const std::vector<int64_t>& sentPoints,
                           const std::vector<TasCurvePoint>& added, const std::vector<int64_t>& sentWindows) {
    TasCurveVersion& v = version(hash, sim);
    v.points.erase(std::remove_if(v.points.begin(), v.points.end(),
                                  [&](const TasCurvePoint& p) {
                                      return p.share == TasShare::Pending && p.leaseId == leaseId &&
                                             std::find(sentPoints.begin(), sentPoints.end(), p.foundAtMs) != sentPoints.end();
                                  }),
                   v.points.end());
    for (TasCurvePoint p : added) {
        p.share = TasShare::Shared;
        v.points.push_back(std::move(p));
    }
    normalize(v);
    for (TasCurveWindow& w : v.windows)
        if (w.share == TasShare::Pending && w.leaseId == leaseId && std::find(sentWindows.begin(), sentWindows.end(), w.stats.id) != sentWindows.end())
            w.share = TasShare::Shared;
}

void TasCurve::abandon(const std::string& leaseId) {
    if (leaseId.empty()) return;
    for (TasCurveVersion& v : versions) {
        for (TasCurvePoint& p : v.points)
            if (p.share == TasShare::Pending && p.leaseId == leaseId) p.share = TasShare::Unshared;
        for (TasCurveWindow& w : v.windows)
            if (w.share == TasShare::Pending && w.leaseId == leaseId) w.share = TasShare::Unshared;
    }
}

Json TasCurve::toJson() const {
    Json vs = Json::array();
    for (const TasCurveVersion& v : versions) {
        Json pts = Json::array(), ws = Json::array();
        for (const TasCurvePoint& p : v.points) pts.push_back(p.toJson());
        for (const TasCurveWindow& w : v.windows) ws.push_back(w.toJson());
        vs.push_back({{"levelHash", v.levelHash}, {"simVersion", v.simVersion}, {"points", pts}, {"windows", ws}});
    }
    return {{"format", kCurveFormat}, {"level", levelId}, {"category", categoryId(category)}, {"versions", vs}};
}

std::optional<TasCurve> TasCurve::fromJson(const Json& j) {
    if (!j.is_object() || jstr(j, "format") != kCurveFormat) return std::nullopt;
    TasCurve c;
    c.levelId = jstr(j, "level");
    if (c.levelId.empty() || !categoryFromId(jstr(j, "category"), c.category)) return std::nullopt;
    const Json vs = jfield(j, "versions", Json::array());
    if (!vs.is_array()) return std::nullopt;
    for (const Json& x : vs) {
        TasCurveVersion v;
        v.levelHash = jstr(x, "levelHash");
        v.simVersion = jget<int>(x, "simVersion", 0);
        if (v.levelHash.empty() || v.simVersion <= 0 || c.find(v.levelHash, v.simVersion)) continue;
        if (const Json pts = jfield(x, "points", Json::array()); pts.is_array())
            for (const Json& p : pts)
                if (auto pt = TasCurvePoint::fromJson(p)) v.points.push_back(std::move(*pt));
        if (const Json ws = jfield(x, "windows", Json::array()); ws.is_array())
            for (const Json& w : ws)
                if (auto cw = TasCurveWindow::fromJson(w)) v.windows.push_back(std::move(*cw));
        normalize(v);
        sortWindows(v.windows);
        capWindows(v.windows);
        c.versions.push_back(std::move(v));
    }
    return c;
}

// ------------------------------------------------------------------ the book

TasCurveBook::~TasCurveBook() {
    flush();
    {
        std::lock_guard<std::mutex> l(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (reading_.valid()) reading_.wait();
}

std::string TasCurveBook::key(const std::string& levelId, RunCategory c) { return levelId + "/" + categoryId(c); }

fs::path TasCurveBook::fileOf(const fs::path& dir, const std::string& levelId, RunCategory c) {
    return dir / (levelId + "_" + categoryId(c) + ".curve.json");
}

void TasCurveBook::open(const fs::path& dir) {
    if (dir.empty() || !dir_.empty()) return;  // once: what is in memory is the newest
    dir_ = dir;
    reading_ = std::async(std::launch::async, [dir] {
        std::vector<TasCurve> out;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = Paths::toUtf8(it->path().filename());
            if (name.size() < 11 || name.compare(name.size() - 11, 11, ".curve.json") != 0) continue;
            auto text = FileUtil::readText(it->path());
            if (!text) continue;
            Json j = Json::parse(*text, nullptr, false);
            if (auto c = TasCurve::fromJson(j)) out.push_back(std::move(*c));
        }
        return out;
    });
}

void TasCurveBook::update(float dt) {
    if (reading_.valid() && reading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        for (TasCurve& c : reading_.get()) {
            const std::string k = key(c.levelId, c.category);
            auto it = curves_.find(k);
            if (it == curves_.end()) {
                curves_.emplace(k, std::move(c));
                continue;
            }
            // Changed before it was read (a training started at once): what was read first, then this game's.
            TasCurve& mine = it->second;
            for (TasCurveVersion& v : c.versions) {
                TasCurveVersion& m = mine.version(v.levelHash, v.simVersion);
                m.points.insert(m.points.begin(), v.points.begin(), v.points.end());
                TasCurve::normalize(m);
                m.windows.insert(m.windows.begin(), v.windows.begin(), v.windows.end());
                sortWindows(m.windows);
                capWindows(m.windows);
            }
            dirty_[k] = 0.0f;
        }
        loaded_ = true;
    }
    if (dir_.empty()) {
        dirty_.clear();
        return;
    }
    for (auto it = dirty_.begin(); it != dirty_.end();) {
        it->second -= dt;
        if (it->second > 0.0f) {
            ++it;
            continue;
        }
        auto c = curves_.find(it->first);
        if (c != curves_.end()) {
            std::lock_guard<std::mutex> l(mutex_);
            pending_[it->first] = {fileOf(dir_, c->second.levelId, c->second.category), c->second};
            if (!thread_.joinable()) thread_ = std::thread([this] { writer(); });
        }
        it = dirty_.erase(it);
    }
    cv_.notify_all();
}

void TasCurveBook::writer() {
    std::unique_lock<std::mutex> l(mutex_);
    for (;;) {
        cv_.wait(l, [this] { return !pending_.empty() || stop_; });
        if (pending_.empty()) return;
        auto job = std::move(pending_.begin()->second);
        pending_.erase(pending_.begin());
        writing_ = true;
        l.unlock();
        std::error_code ec;
        fs::create_directories(job.first.parent_path(), ec);
        if (!FileUtil::writeTextAtomic(job.first, job.second.toJson().dump() + "\n")) Log::warn("TAS : courbe non écrite ({})", Paths::toUtf8(job.first));
        l.lock();
        writing_ = false;
        cv_.notify_all();
    }
}

void TasCurveBook::flush() {
    for (auto& d : dirty_) d.second = 0.0f;
    update(0.0f);
    std::unique_lock<std::mutex> l(mutex_);
    cv_.wait(l, [this] { return (pending_.empty() && !writing_) || !thread_.joinable(); });
}

const TasCurve* TasCurveBook::find(const std::string& levelId, RunCategory c) const {
    auto it = curves_.find(key(levelId, c));
    return it == curves_.end() ? nullptr : &it->second;
}

TasCurve& TasCurveBook::at(const std::string& levelId, RunCategory c) {
    auto [it, fresh] = curves_.try_emplace(key(levelId, c));
    if (fresh) {
        it->second.levelId = levelId;
        it->second.category = c;
    }
    return it->second;
}

void TasCurveBook::changed(const std::string& levelId, RunCategory c, bool now) {
    const std::string k = key(levelId, c);
    auto it = dirty_.find(k);
    if (it == dirty_.end()) dirty_[k] = now ? 0.0f : 20.0f;
    else if (now) it->second = 0.0f;
}

}  // namespace cosmic
