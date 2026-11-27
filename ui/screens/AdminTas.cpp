#include "ui/screens/AdminTas.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineSession.h"
#include "game/online/Spectate.h"
#include "game/planets/PlanetData.h"
#include "game/tas/TasCurve.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasTraining.h"
#include "game/tas/TasVerify.h"
#include "ui/widgets/CosmicFrame.h"

namespace cosmic {

namespace tasadmin {

std::string countText(int64_t n) {
    // French digit groups (the translation writes them the English way).
    std::string d = std::to_string(std::max<int64_t>(0, n)), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return out;
}

std::string compactCount(int64_t n) {
    n = std::max<int64_t>(0, n);
    if (n < 10000) return countText(n);
    char b[32];
    auto decimal = [&](double v, const char* unit) {
        if (v < 100.0) std::snprintf(b, sizeof b, "%.1f %s", v, unit);
        else std::snprintf(b, sizeof b, "%.0f %s", v, unit);
        std::string s = b;
        for (char& c : s)
            if (c == '.') c = ',';  // French decimal comma
        return s;
    };
    if (n < 1000000) return decimal(static_cast<double>(n) / 1000.0, "k");
    if (n < 1000000000) return decimal(static_cast<double>(n) / 1e6, "M");
    return decimal(static_cast<double>(n) / 1e9, "G");
}

std::string percentText(int64_t part, int64_t total) {
    if (total <= 0) return "—";
    const double p = 100.0 * static_cast<double>(std::clamp<int64_t>(part, 0, total)) / static_cast<double>(total);
    char b[32];
    if (p == 0.0 || p >= 10.0) std::snprintf(b, sizeof b, "%.0f %%", p);
    else if (p >= 1.0) std::snprintf(b, sizeof b, "%.1f %%", p);
    else if (p >= 0.01) std::snprintf(b, sizeof b, "%.2f %%", p);
    else std::snprintf(b, sizeof b, "< 0.01 %%");
    std::string s = b;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string durationTick(int64_t ms) {
    const int64_t s = std::max<int64_t>(0, ms) / 1000;
    char b[40];
    if (s == 0) return "0";
    // Days in hours (« 48 h »: the same in both languages).
    if (s >= 3600 && s % 3600 == 0) std::snprintf(b, sizeof b, "%lld h", static_cast<long long>(s / 3600));
    else if (s >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    else if (s >= 60 && s % 60 == 0) std::snprintf(b, sizeof b, "%lld min", static_cast<long long>(s / 60));
    else if (s >= 60) std::snprintf(b, sizeof b, "%lld min %02lld", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    else std::snprintf(b, sizeof b, "%lld s", static_cast<long long>(s));
    return b;
}

std::vector<int64_t> axisTicks(int64_t lo, int64_t hi, int maxCount, const std::vector<int64_t>& steps) {
    std::vector<int64_t> out;
    maxCount = std::max(1, maxCount);
    if (hi <= lo || steps.empty()) {
        out.push_back(lo);
        return out;
    }
    int64_t step = steps.back();
    for (int64_t s : steps)
        if ((hi - lo) / s <= maxCount) {
            step = s;
            break;
        }
    // Beyond the largest step: its multiples.
    while ((hi - lo) / step > maxCount) step *= 2;
    for (int64_t v = (lo + step - 1) / step * step; v <= hi; v += step) out.push_back(v);
    if (out.empty()) out.push_back(lo);
    return out;
}

const std::vector<int64_t>& durationSteps() {
    static const std::vector<int64_t> s = {1000,    2000,    5000,     10000,    15000,    30000,    60000,     120000,    300000,    600000,
                                           900000,  1800000, 3600000,  7200000,  10800000, 21600000, 43200000,  86400000,  172800000, 604800000};
    return s;
}

const std::vector<int64_t>& runTimeSteps() {
    static const std::vector<int64_t> s = {1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000};
    return s;
}

std::vector<std::pair<int64_t, int>> roundStarts(const std::vector<TasCurveWindow>& windows) {
    std::vector<std::pair<int64_t, int>> out;
    int session = -1, round = -1;
    for (const TasCurveWindow& w : windows) {
        if (w.session != session || w.stats.round != round) out.emplace_back(w.fromMs, w.stats.round);
        session = w.session;
        round = w.stats.round;
    }
    return out;
}

std::string durationText(int64_t ms) {
    const int64_t s = std::max<int64_t>(0, ms) / 1000;
    char b[48];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    else if (s >= 60) std::snprintf(b, sizeof b, "%lld min %02lld s", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    else std::snprintf(b, sizeof b, "%lld s", static_cast<long long>(s));
    return b;
}

namespace {

const Json* versionOf(const Json& list, const std::string& hash) {
    if (!list.is_array()) return nullptr;
    for (const Json& t : list)
        if (jstr(t, "level_hash") == hash && jget<int>(t, "sim_version", 0) == kSimulationVersion) return &t;
    return nullptr;
}

bool sameVersion(const Json& run, const std::string& hash) {
    return run.is_object() && jstr(run, "level_hash") == hash && jget<int>(run, "sim_version", 0) == kSimulationVersion;
}

}  // namespace

Server serverFor(const Json& pair, const std::string& hash) {
    Server s;
    if (!pair.is_object()) return s;
    const Json& published = jfield(pair, "published", Json());
    const Json& archive = jfield(pair, "archive", Json());
    // The run of this version, wherever the server placed it (its idea of the current version
    // may not be this game's: an older or newer game).
    const Json* mine = sameVersion(published, hash) ? &published : sameVersion(archive, hash) ? &archive : nullptr;
    if (mine) {
        s.publishedMs = jget<int64_t>(*mine, "time_ms", 0);
        s.revision = jget<int>(*mine, "revision", 0);
        s.publishedAt = jstr(*mine, "published_at");
        s.publishedBy = jstr(jfield(*mine, "published_by", Json()), "username");
    }
    for (const Json* other : {&published, &archive})
        if (other != mine && other->is_object() && s.archiveMs == 0) s.archiveMs = jget<int64_t>(*other, "time_ms", 0);
    const Json trainings = jfield(pair, "trainings", Json::array());
    if (const Json* t = versionOf(trainings, hash)) {
        s.trainingMs = jget<int64_t>(*t, "total_ms", 0);
        s.explored = jget<int64_t>(*t, "total_explored", 0);
        s.sessions = jget<int>(*t, "sessions", 0);
        const Json& lease = jfield(*t, "lease", Json());
        if (lease.is_object()) {
            s.holder = jstr(jfield(lease, "holder", Json()), "username", "?");
            s.holderSince = jstr(lease, "since");
            s.mine = jget<bool>(lease, "mine", false);
        }
    }
    return s;
}

bool unpublished(const Row& r) { return r.local.localMs > 0 && (r.server.publishedMs == 0 || r.local.localMs < r.server.publishedMs); }

bool recompute(const Row& r) {
    const bool current = r.local.shippedMs > 0 || r.local.localMs > 0 || r.local.downloadedMs > 0 || r.server.publishedMs > 0;
    const bool old = r.local.oldShippedMs > 0 || r.local.oldLocalMs > 0 || r.server.archiveMs > 0;
    return !current && old;
}

Status statusOf(const Row& r, const TasTraining& t) {
    const TasPair p{r.local.levelId, r.local.category};
    if (const TasTraining::Job* j = t.jobFor(p)) {
        if (j->state == TasTraining::JobState::Acquiring) return Status::Acquiring;
        if (j->state == TasTraining::JobState::Running) return Status::Training;
        if (j->state == TasTraining::JobState::Stopping) return Status::Stopping;
    }
    if (t.queued(p)) return Status::Queued;
    if (!r.server.holder.empty()) return r.server.mine ? Status::HeldByMe : Status::HeldByOther;
    if (!t.failureOf(p).empty()) return Status::Failed;
    if (unpublished(r)) return Status::Unpublished;
    if (recompute(r)) return Status::Recompute;
    return Status::Free;
}

std::string statusLabel(Status s) {
    switch (s) {
        case Status::Free: return "Libre";
        case Status::Recompute: return "À recalculer (niveau modifié)";
        case Status::Acquiring: return "Réservation du verrou…";
        case Status::Training: return "En entraînement ici";
        case Status::Stopping: return "Arrêt : vérification de la meilleure course…";
        case Status::Queued: return "En file d'attente";
        case Status::HeldByOther: return "En entraînement par un autre administrateur";
        case Status::HeldByMe: return "En entraînement par vous sur un autre ordinateur";
        case Status::Unpublished: return "Amélioration locale non publiée";
        case Status::Failed: return "Échec de l'entraînement";
    }
    return "";
}

std::vector<Local> scanLocal(const fs::path& userDir, const std::string& server) {
    ContentDatabase content;
    content.reload();
    applyOnlineRevisions(content, userDir, server);
    LevelFingerprints fps;
    std::vector<const LevelEntry*> levels;
    std::set<std::string> seen;
    // The campaign's order (each planet's levels), then the other levels shipped with the game.
    for (const PlanetData* pl : content.planets())
        for (const std::string& id : pl->levels)
            if (const LevelEntry* e = content.level(id); e && e->source == ContentSource::Builtin && seen.insert(id).second) levels.push_back(e);
    std::vector<const LevelEntry*> rest;
    for (const LevelEntry* e : content.levels())
        if (e->source == ContentSource::Builtin && !seen.count(e->id)) rest.push_back(e);
    std::sort(rest.begin(), rest.end(), [](const LevelEntry* a, const LevelEntry* b) { return a->id < b->id; });
    levels.insert(levels.end(), rest.begin(), rest.end());
    const fs::path local = TasTraining::localRoot(), downloaded = tasDownloadedRoot();
    std::vector<Local> out;
    for (const LevelEntry* e : levels) {
        const LevelFingerprint* fp = fps.get(content, e->id);
        std::string planetName = e->planet;
        for (const PlanetData* pl : content.planets())
            if (pl->id == e->planet) planetName = pl->name;
        for (RunCategory c : kAllCategories) {
            Local l;
            l.levelId = e->id;
            l.levelName = e->name.empty() ? e->id : e->name;
            l.planetId = e->planet;
            l.planetName = planetName;
            l.category = c;
            l.hash = fp ? fp->hash : std::string();
            auto timeIn = [&](const fs::path& root) -> int64_t {
                if (l.hash.empty() || root.empty()) return 0;
                auto r = bestTasAmong(e->id, c, l.hash, {root});
                return r ? r->timeMs : 0;
            };
            // A file of another version of the level, or of other rules: its time, not comparable.
            auto oldIn = [&](const fs::path& root) -> int64_t {
                if (root.empty()) return 0;
                auto r = readTasFile(tasFilePath(e->id, c, root));
                if (!r || (r->levelHash == l.hash && r->simVersion == kSimulationVersion)) return 0;
                return r->timeMs;
            };
            l.shippedMs = timeIn(tasRoot());
            l.localMs = timeIn(local);
            l.downloadedMs = timeIn(downloaded);
            l.oldShippedMs = oldIn(tasRoot());
            l.oldLocalMs = std::max(oldIn(local), oldIn(downloaded));
            out.push_back(std::move(l));
        }
    }
    return out;
}

std::string stamp(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "—";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%02d/%02d · %02d:%02d", m.tm_mday, m.tm_mon + 1, m.tm_hour, m.tm_min);
    return b;
}

}  // namespace tasadmin

using namespace tasadmin;

namespace {

constexpr int kBudgets[] = {0, 10, 20, 30, 60, 120, 240};

/// The columns of the table (shares of the room left of the actions), their titles.
constexpr float kCols[] = {0.0f, 0.21f, 0.32f, 0.45f, 0.56f, 0.76f, 1.0f};
constexpr int kColCount = 6;
const char* const kColTitles[] = {"Niveau", "Livrée", "En ligne", "Entraînement", "Essais", "État"};

std::string budgetText(int minutes) { return minutes <= 0 ? std::string("Sans limite") : std::to_string(minutes) + " min"; }

}  // namespace

AdminTas::AdminTas(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {}

AdminTas::~AdminTas() {
    // The trainings go on without the page: its callbacks leave with it.
    g_.tasTraining.onMessage = nullptr;
    g_.tasTraining.onEnded = nullptr;
    // A reading of the levels under way is never waited for: it finishes on its own (kept until
    // it does; the last ones at the game's exit).
    if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        static std::vector<std::future<std::vector<Local>>> orphans;
        orphans.erase(std::remove_if(orphans.begin(), orphans.end(),
                                     [](auto& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }),
                      orphans.end());
        orphans.push_back(std::move(scan_));
    }
}

void AdminTas::enter(bool force) {
    toolFound_ = !TasTraining::toolPath().empty();
    std::weak_ptr<int> alive = life_;
    g_.tasTraining.onMessage = [this, alive](const std::string& m, bool error) {
        if (!alive.expired()) say_(m, error);
    };
    g_.tasTraining.onEnded = [this, alive](const TasPair&) {
        if (alive.expired()) return;
        rescan_ = true;  // a new run of this computer, maybe
        serverT_ = -100.0f;
        curveT_ = -100.0f;  // the curve opened: its windows again
    };
    if ((!scanned_ || force) && !scan_.valid())
        scan_ = std::async(std::launch::async, [dir = Paths::userDir(), server = g_.online.config().serverUrl] { return scanLocal(dir, server); });
    if (!serverLoaded_ || force) loadServer();
}

void AdminTas::setLocal(std::vector<Local> local) {
    local_ = std::move(local);
    scanned_ = true;
}

namespace {

/// The curves of a pair of the server (GET /admin/tas: points; …/curve: windows too) taken into
/// this computer's (its own pending and finer ones kept).
void takeCurves(TasCurveBook& book, const std::string& levelId, RunCategory cat, const Json& versions, bool withWindows) {
    if (!versions.is_array() || levelId.empty()) return;
    bool changed = false;
    for (const Json& v : versions) {
        const std::string hash = jstr(v, "level_hash");
        const int sim = jget<int>(v, "sim_version", 0);
        if (hash.empty() || sim <= 0) continue;
        std::vector<TasCurvePoint> pts;
        if (const Json list = jfield(v, "points", Json::array()); list.is_array())
            for (const Json& p : list)
                if (auto pt = TasCurvePoint::fromServer(p)) pts.push_back(std::move(*pt));
        TasCurve& c = book.at(levelId, cat);
        changed = c.mergeServerPoints(hash, sim, std::move(pts)) || changed;
        if (!withWindows) continue;
        std::vector<TasCurveWindow> ws;
        if (const Json list = jfield(v, "windows", Json::array()); list.is_array())
            for (const Json& w : list)
                if (auto cw = TasCurveWindow::fromServer(w)) ws.push_back(std::move(*cw));
        changed = c.mergeServerWindows(hash, sim, std::move(ws)) || changed;
    }
    if (changed) book.changed(levelId, cat);
}

}  // namespace

void AdminTas::applyServer(const Json& data) {
    server_ = data.is_object() ? data : Json::object();
    serverLoaded_ = true;
    const Json& pairs = jfield(server_, "pairs", Json::array());
    if (!pairs.is_array()) return;
    for (const Json& p : pairs) {
        RunCategory cat;
        if (!categoryFromId(jstr(p, "category"), cat)) continue;
        takeCurves(g_.tasTraining.curves(), jstr(p, "level_id"), cat, jfield(p, "curves", Json::array()), false);
    }
}

void AdminTas::applyCurve(const Json& data) {
    RunCategory cat;
    if (!categoryFromId(jstr(data, "category"), cat)) return;
    takeCurves(g_.tasTraining.curves(), jstr(data, "level_id"), cat, jfield(data, "versions", Json::array()), true);
}

void AdminTas::loadCurve() {
    if (curveBusy_ || selLevel_.empty() || !g_.online.usable() || !g_.online.loggedIn()) return;
    curveBusy_ = true;
    curveT_ = g_.ui.time();
    g_.online.get(
        "/api/v1/admin/tas/" + selLevel_ + "/" + categoryId(selCat_) + "/curve", OnlineService::guard(life_, [this](const ApiResult& r) {
            curveBusy_ = false;
            if (lostRole_(r)) return;
            if (r.ok) applyCurve(r.data);  // a failure: this computer's curve stays (said by the next list)
        }),
        false);
}

void AdminTas::select(const std::string& levelId, RunCategory c) {
    if (levelId.empty() || selected(levelId, c)) {
        selLevel_.clear();  // closed (the same row again)
        tip_ = charts::Tip{};
        return;
    }
    selLevel_ = levelId;
    selCat_ = c;
    selVersion_ = 0;
    pointCursor_ = windowCursor_ = -1;
    scrollToSel_ = true;
    curveT_ = -100.0f;
}

void AdminTas::loadServer() {
    if (serverBusy_) return;
    serverBusy_ = true;
    serverT_ = g_.ui.time();
    g_.online.get(
        "/api/v1/admin/tas", OnlineService::guard(life_, [this](const ApiResult& r) {
            serverBusy_ = false;
            if (lostRole_(r)) return;
            if (!r.ok) {
                serverError_ = r.message;
                return;
            }
            serverError_.clear();
            applyServer(r.data);
        }),
        false);
}

std::vector<Row> AdminTas::rows() const {
    std::vector<Row> out;
    const Json& pairs = jfield(server_, "pairs", Json::array());
    for (const Local& l : local_) {
        Row r;
        r.local = l;
        if (pairs.is_array())
            for (const Json& p : pairs)
                if (jstr(p, "level_id") == l.levelId && jstr(p, "category") == categoryId(l.category)) r.server = serverFor(p, l.hash);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> AdminTas::planets() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Local& l : local_)
        if (std::none_of(out.begin(), out.end(), [&](const auto& p) { return p.first == l.planetId; })) out.emplace_back(l.planetId, l.planetName);
    return out;
}

void AdminTas::text(const std::string& t, const Rect& box, float size, Color c, FontFace face, Align a, bool verbatim) {
    UIContext& ui = g_.ui;
    if (t.empty() || box.w <= ui.s(8)) return;
    i18n::Verbatim v(verbatim);
    const float f = ui.fitFont(t, face, size, box.w, 0.75f);
    const std::string shownText = ui.elide(t, face, f, box.w);
    const float x = a == Align::Left ? box.x : a == Align::Right ? box.right() : box.center().x;
    ui.label(shownText, {x, box.center().y}, f, c, a, face, VAlign::Middle);
    // What the list's scroll region cuts is not on screen: not placed (the fit tests).
    if (!inList_ || layout_.list.expanded(0.5f).containsRect(box)) layout_.texts.push_back({box, shownText, f, face, verbatim});
}

bool AdminTas::button(const std::string& id, const Rect& r, const std::string& label, bool enabled, bool primary, bool danger) {
    UIContext::ButtonStyle st;
    st.small = true;
    st.enabled = enabled;
    st.primary = primary;
    st.danger = danger;
    if (!inList_ || layout_.list.expanded(0.5f).containsRect(r)) layout_.buttons.push_back(r);
    return cframe::button(g_.ui, id, r, label, st);
}

std::vector<const Local*> AdminTas::shown() const {
    std::vector<const Local*> out;
    const auto pl = planets();
    for (const Local& l : local_) {
        if (category_ >= 0 && l.category != kAllCategories[static_cast<size_t>(category_)]) continue;
        if (planet_ >= 0 && planet_ < static_cast<int>(pl.size()) && l.planetId != pl[static_cast<size_t>(planet_)].first) continue;
        if (activeOnly_) {
            const TasPair p{l.levelId, l.category};
            if (!g_.tasTraining.jobFor(p) && !g_.tasTraining.queued(p)) continue;
        }
        out.push_back(&l);
    }
    return out;
}

void AdminTas::publish(const Row& r, std::function<void(bool)> done) {
    const fs::path root = TasTraining::localRoot();
    const fs::path file = tasFilePath(r.local.levelId, r.local.category, root);
    auto text = FileUtil::readText(file);
    auto rec = readTasFile(file);
    auto bytes = rec ? FileUtil::readBinary(tasReplayPath(*rec, root)) : std::nullopt;
    if (!text || !rec || !bytes) {
        say_("Course locale illisible : " + r.local.levelName + " · " + categoryLabel(r.local.category), true);
        return done(false);
    }
    Json body = {{"record", *text}, {"replay", spectate::base64Encode(bytes->data(), bytes->size())}};
    const std::string what = r.local.levelName + " · " + categoryLabel(r.local.category);
    g_.online.send(
        "POST", "/api/v1/admin/tas/" + r.local.levelId + "/" + categoryId(r.local.category) + "/publish", body,
        OnlineService::guard(life_, [this, what, done](const ApiResult& res) {
            if (lostRole_(res)) return done(false);
            if (!res.ok) {
                say_(what + " : " + res.message, true);
                return done(false);
            }
            say_("Course publiée : " + what + " en " + formatRunTime(jget<int64_t>(jfield(res.data, "published", Json()), "time_ms", 0)) + ".", false);
            g_.tasOnline.refresh();  // this game too gets it (downloaded and verified like any other)
            done(true);
        }),
        120000);
}

void AdminTas::publishNext() {
    if (publishQueue_.empty()) {
        publishing_ = false;
        if (published_ + publishFailed_ > 1)
            say_(std::to_string(published_) + " course(s) publiée(s), " + std::to_string(publishFailed_) + " refusée(s).", publishFailed_ > 0);
        loadServer();
        return;
    }
    const auto [levelId, cat] = publishQueue_.front();
    publishQueue_.erase(publishQueue_.begin());
    for (const Row& r : rows())
        if (r.local.levelId == levelId && r.local.category == cat) {
            std::weak_ptr<int> alive = life_;
            return publish(r, [this, alive](bool ok) {
                if (alive.expired()) return;
                (ok ? published_ : publishFailed_)++;
                publishNext();
            });
        }
    publishNext();
}

void AdminTas::drawToolbar(float& y) {
    UIContext& ui = g_.ui;
    const Rect& in = layout_.inner;
    const float h = ui.s(40), gap = ui.s(10);
    float x = in.x;
    // Places a box of width w on the line, or on the next one when it does not fit.
    auto place = [&](float w) {
        if (x > in.x && x + w > in.right()) {
            x = in.x;
            y += h + ui.s(8);
        }
        Rect r{x, y, std::min(w, in.w), h};
        x += r.w + gap;
        return r;
    };
    auto bw = [&](const std::string& label) { return ui.text().measure(label, FontFace::Bold, ui.s(15)).x + ui.s(30); };
    auto newLine = [&] {
        x = in.x;
        y += h + ui.s(8);
    };

    // 1. The filters: the categories, the planet, the trainings under way.
    {
        const std::string all = "Toutes les catégories";
        if (button("cat_all", place(bw(all)), all, true, category_ < 0)) category_ = -1;
        for (size_t i = 0; i < std::size(kAllCategories); ++i) {
            const std::string l = categoryLabel(kAllCategories[i]);
            if (button("cat_" + std::to_string(i), place(bw(l)), l, true, category_ == static_cast<int>(i))) category_ = static_cast<int>(i);
        }
        const auto pl = planets();
        const std::string pName = planet_ >= 0 && planet_ < static_cast<int>(pl.size()) ? pl[static_cast<size_t>(planet_)].second : std::string("Toutes");
        const std::string pLabel = "Planète : " + pName;
        if (button("planet", place(bw(pLabel)), pLabel, !pl.empty(), planet_ >= 0)) planet_ = planet_ + 1 >= static_cast<int>(pl.size()) ? -1 : planet_ + 1;
        const std::string act = "En cours et en file (" + std::to_string(g_.tasTraining.running() + static_cast<int>(g_.tasTraining.queue().size())) + ")";
        if (button("active", place(bw(act)), act, true, activeOnly_)) activeOnly_ = !activeOnly_;
    }
    newLine();

    // 2. The actions on the pairs shown.
    const bool online = g_.online.usable() && g_.online.loggedIn();
    const auto list = rows();
    const auto visible = shown();
    std::vector<TasPair> trainable;
    int publishable = 0;
    for (const Row& r : list) {
        const bool isShown = std::any_of(visible.begin(), visible.end(), [&](const Local* l) { return l->levelId == r.local.levelId && l->category == r.local.category; });
        const TasPair p{r.local.levelId, r.local.category};
        if (isShown && !r.local.hash.empty() && (r.server.holder.empty() || r.server.mine) && !g_.tasTraining.jobFor(p) && !g_.tasTraining.queued(p))
            trainable.push_back(p);
        if (unpublished(r) && (r.server.holder.empty() || r.server.mine)) ++publishable;
    }
    {
        const std::string trainAll = "Tout entraîner (" + std::to_string(trainable.size()) + ")";
        if (button("train_all", place(bw(trainAll)), trainAll, toolFound_ && online && !trainable.empty(), true)) {
            const int n = g_.tasTraining.enqueue(trainable);
            say_(std::to_string(n) + " paire(s) ajoutée(s) à la file d'entraînement.", false);
        }
        const std::string stopAll = "Tout arrêter";
        if (button("stop_all", place(bw(stopAll)), stopAll, g_.tasTraining.running() > 0 || !g_.tasTraining.queue().empty(), false, true)) g_.tasTraining.stopAll();
        const std::string clear = "Vider la file";
        if (button("clear_queue", place(bw(clear)), clear, !g_.tasTraining.queue().empty())) g_.tasTraining.clearQueue();
        const std::string pubAll = "Publier toutes les améliorations (" + std::to_string(publishable) + ")";
        if (button("publish_all", place(bw(pubAll)), pubAll, online && publishable > 0 && !publishing_, true)) {
            publishQueue_.clear();
            for (const Row& r : list)
                if (unpublished(r) && (r.server.holder.empty() || r.server.mine)) publishQueue_.emplace_back(r.local.levelId, r.local.category);
            publishing_ = true;
            published_ = publishFailed_ = 0;
            publishNext();
        }
    }
    newLine();

    // 3. The settings of the trainings (kept on this computer).
    TasTrainingSettings& st = g_.tasTraining.settings();
    bool changed = false;
    auto stepper = [&](const std::string& id, const std::string& label, const std::string& value, const std::string& widest, int& v, int lo, int hi) {
        const float lw = ui.text().measure(label, FontFace::Body, ui.s(15)).x + ui.s(8);
        const float vw = ui.text().measure(widest, FontFace::Bold, ui.s(15)).x + ui.s(16);
        const float sb = ui.s(34);
        Rect r = place(lw + sb + vw + sb);
        text(label, {r.x, r.y, lw, r.h}, 15, ui.theme().textDim);
        if (button(id + "_minus", {r.x + lw, r.y + ui.s(3), sb, r.h - ui.s(6)}, "−", v > lo)) {
            --v;
            changed = true;
        }
        text(value, {r.x + lw + sb, r.y, vw, r.h}, 15, ui.theme().text, FontFace::Bold, Align::Center);
        if (button(id + "_plus", {r.x + lw + sb + vw, r.y + ui.s(3), sb, r.h - ui.s(6)}, "+", v < hi)) {
            ++v;
            changed = true;
        }
    };
    stepper("parallel", "Entraînements en parallèle", std::to_string(st.parallel), "16", st.parallel, 1, TasTrainingSettings::kMaxParallel);
    stepper("threads", "Threads par entraînement", std::to_string(st.threadsPerJob), "16", st.threadsPerJob, 1, TasTrainingSettings::kMaxThreads);
    int budgetIndex = 0;
    for (size_t i = 0; i < std::size(kBudgets); ++i)
        if (kBudgets[i] == st.budgetMinutes) budgetIndex = static_cast<int>(i);
    stepper("budget", "Budget par paire", budgetText(st.budgetMinutes), "Sans limite", budgetIndex, 0, static_cast<int>(std::size(kBudgets)) - 1);
    st.budgetMinutes = kBudgets[static_cast<size_t>(std::clamp(budgetIndex, 0, static_cast<int>(std::size(kBudgets)) - 1))];
    if (changed) g_.tasTraining.saveSettings();
    {
        const std::string info = std::to_string(TasTraining::cores()) + " cœurs · " + std::to_string(g_.tasTraining.running()) + " en cours · " +
                                 std::to_string(g_.tasTraining.queue().size()) + " en file";
        const Rect r = place(ui.text().measure(info, FontFace::Body, ui.s(15)).x + ui.s(4));
        text(info, r, 15, ui.theme().textDim);
    }
    newLine();
    // Why training may be impossible here (said, never hidden).
    std::string why;
    if (!toolFound_) why = "cosmic_tas est introuvable à côté du jeu : l'entraînement est impossible sur cet ordinateur.";
    else if (!online) why = "Connexion à Cosmic Online nécessaire : chaque entraînement réserve son niveau auprès du serveur.";
    else if (!serverError_.empty()) why = serverError_;
    if (!why.empty()) {
        text(why, {in.x, y, in.w, ui.s(26)}, 15, ui.theme().danger);
        y += ui.s(32);
    }
}

void AdminTas::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    layout_ = AdminTasLayout{};
    layout_.inner = in;
    if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) setLocal(scan_.get());
    if (rescan_ && !scan_.valid()) {
        rescan_ = false;
        scan_ = std::async(std::launch::async, [dir = Paths::userDir(), server = g_.online.config().serverUrl] { return scanLocal(dir, server); });
    }
    // The server's list again every 30 s (the other administrators' leases), after a training ended.
    if (g_.online.usable() && g_.online.loggedIn() && ui.time() - serverT_ > 30.0f) loadServer();
    ui.pushId("tas");
    float y = in.y;
    drawToolbar(y);
    y += ui.s(6);

    // The column titles, at the shares of the rows.
    float actionsW = 0.0f;
    for (const char* a : {"Entraîner", "Arrêter", "Retirer", "Arrêt…"})
        actionsW = std::max(actionsW, ui.text().measure(a, FontFace::Bold, ui.s(15)).x + ui.s(30));
    actionsW += ui.s(8) + ui.text().measure("Publier", FontFace::Bold, ui.s(15)).x + ui.s(30) + ui.s(20);
    const float rowRight = in.right() - ui.s(16);
    const float x0 = in.x + ui.s(12), cw = rowRight - actionsW - x0;
    for (int i = 0; i < kColCount; ++i)
        text(kColTitles[i], {x0 + cw * kCols[i], y, cw * (kCols[i + 1] - kCols[i]) - ui.s(10), ui.s(24)}, 14, ui.theme().textDim, FontFace::Bold);
    y += ui.s(30);

    Rect list{in.x, y, in.w, in.bottom() - y};
    const auto visible = shown();
    const auto all = rows();
    const float rowH = ui.s(60), gap = ui.s(6);
    // The curve of the row selected: under the table, the table keeping three rows and more.
    const Row* sel = nullptr;
    for (const Row& r : all)
        if (selected(r.local.levelId, r.local.category)) sel = &r;
    if (!sel) selLevel_.clear();
    Rect detail;
    if (sel) {
        const float avail = list.h, keep = (rowH + gap) * 3.3f, dgap = ui.s(10);
        const float h = std::clamp(avail - keep - dgap, std::min(ui.s(250), avail * 0.6f), ui.s(520));
        list.h = std::max(ui.s(40), avail - h - dgap);
        detail = {in.x, list.bottom() + dgap, in.w, in.bottom() - list.bottom() - dgap};
        // Échap / B closes it (ADMINISTRATION does not leave: capturesBack).
        if (ui.acceptsInput() && ui.backPressed()) {
            const std::string level = selLevel_;
            select(level, selCat_);
        }
    }
    layout_.list = list;
    if (local_.empty() || visible.empty()) {
        const std::string msg = !scanned_ ? "Lecture des niveaux…" : local_.empty() ? "Aucun niveau du jeu." : "Aucune paire ne correspond.";
        text(msg, {list.x, list.y + ui.s(20), list.w, ui.s(30)}, 18, ui.theme().textDim, FontFace::Body, Align::Center);
        ui.popId();
        return;
    }
    // Opened: the row selected brought into the shorter list.
    if (scrollToSel_ && sel) {
        scrollToSel_ = false;
        for (size_t i = 0; i < visible.size(); ++i)
            if (visible[i]->levelId == sel->local.levelId && visible[i]->category == sel->local.category)
                ui.setScroll("list", std::max(0.0f, static_cast<float>(i) * (rowH + gap) - (list.h - rowH) * 0.5f));
    }
    ui.beginScroll("list", list, static_cast<float>(visible.size()) * (rowH + gap));
    float ry = list.y - ui.scrollOffset();
    inList_ = true;
    for (const Local* l : visible) {
        Rect row{list.x, ry, list.w - ui.s(16), rowH};
        ry += rowH + gap;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        for (const Row& r : all)
            if (r.local.levelId == l->levelId && r.local.category == l->category) drawRow(r, row, actionsW);
        ++layout_.rowsDrawn;
    }
    inList_ = false;
    ui.endScroll();
    if (sel && !selLevel_.empty()) {
        // Its windows of attempts from the server: at once, then every 30 s while it is open.
        if (ui.time() - curveT_ > 30.0f) loadCurve();
        drawDetail(*sel, detail);
    } else {
        tip_ = charts::Tip{};
    }
    ui.popId();
}

void AdminTas::drawRow(const Row& row, const Rect& r, float actionsW) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const TasPair pair{row.local.levelId, row.local.category};
    const Status st = statusOf(row, g_.tasTraining);
    const TasTraining::Job* job = g_.tasTraining.jobFor(pair);
    ui.r().fillRoundedRect(r, ui.s(8), st == Status::Training ? Color{20, 44, 52, 240} : Color{18, 26, 50, 235});
    const std::string id = pair.key();
    const bool online = g_.online.usable() && g_.online.loggedIn();

    // The actions at the right.
    float bx = r.right() - ui.s(10);
    const float bh = ui.s(36), by = r.y + (r.h - bh) * 0.5f;
    auto bw = [&](const std::string& label) { return ui.text().measure(label, FontFace::Bold, ui.s(15)).x + ui.s(30); };
    {
        const std::string pub = "Publier";
        bx -= bw(pub);
        const bool canPublish = online && unpublished(row) && (row.server.holder.empty() || row.server.mine) && !publishing_ &&
                                (!job || job->state != TasTraining::JobState::Stopping);
        if (button("pub_" + id, {bx, by, bw(pub), bh}, pub, canPublish, true)) {
            publishing_ = true;
            published_ = publishFailed_ = 0;
            publishQueue_ = {{row.local.levelId, row.local.category}};
            publishNext();
        }
        bx -= ui.s(8);
    }
    {
        std::string label = "Entraîner";
        bool enabled = toolFound_ && online && !row.local.hash.empty() && st != Status::HeldByOther;
        bool danger = false;
        if (st == Status::Training || st == Status::Acquiring) label = "Arrêter", danger = true, enabled = true;
        else if (st == Status::Stopping) label = "Arrêt…", enabled = false;
        else if (st == Status::Queued) label = "Retirer", enabled = true;
        bx -= bw(label);
        if (button("train_" + id, {bx, by, bw(label), bh}, label, enabled, !danger && st != Status::Queued, danger)) {
            if (st == Status::Training || st == Status::Acquiring) g_.tasTraining.stop(pair);
            else if (st == Status::Queued) g_.tasTraining.unqueue(pair);
            else g_.tasTraining.train(pair);
        }
    }

    const float x0 = r.x + ui.s(12);
    const float cw = r.right() - actionsW - x0 + ui.s(16) - ui.s(16);
    const float limit = bx - ui.s(10);
    auto colBox = [&](int i, bool second) {
        const float cx = x0 + cw * kCols[i];
        const float right = std::min(x0 + cw * kCols[i + 1] - ui.s(10), limit);
        return Rect{cx, second ? r.y + ui.s(32) : r.y + ui.s(8), std::max(0.0f, right - cx), second ? ui.s(20) : ui.s(22)};
    };
    // The row itself selects its curve (click, Entrée / A; arrows and d-pad reach it): the part
    // left of its buttons.
    {
        bool hovered = false;
        const Rect zone{r.x, r.y, std::max(0.0f, limit - r.x), r.h};
        if (ui.hotspot("row_" + id, zone, &hovered)) select(row.local.levelId, row.local.category);
        const bool sel = selected(row.local.levelId, row.local.category);
        if (sel) ui.r().drawRoundedRect(r, ui.s(8), th.accent, std::max(1.5f, ui.s(2.0f)));
        else if (ui.focused("row_" + id) && !ui.usingPointer()) ui.r().drawRoundedRect(r, ui.s(8), th.accent.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
        else if (hovered && ui.usingPointer()) ui.r().drawRoundedRect(r, ui.s(8), th.accent.withAlpha(90), std::max(1.0f, ui.s(1.0f)));
    }
    // Level and category.
    text(row.local.levelName, colBox(0, false), 16, th.text, FontFace::Bold);
    text(std::string(categoryLabel(row.local.category)) + " · " + row.local.planetName, colBox(0, true), 13, th.textDim);
    // Shipped with the game.
    if (row.local.shippedMs > 0) text(formatRunTime(row.local.shippedMs), colBox(1, false), 15, th.text);
    else if (row.local.oldShippedMs > 0) {
        text(formatRunTime(row.local.oldShippedMs), colBox(1, false), 15, th.textDim);
        text("ancienne version, non comparable", colBox(1, true), 12, th.warm);
    } else text("—", colBox(1, false), 15, th.textDim);
    // Published online: this version's run, when and by whom; else the old one, apart.
    if (row.server.publishedMs > 0) {
        text(formatRunTime(row.server.publishedMs), colBox(2, false), 15, th.text);
        std::string by = "v" + std::to_string(row.server.revision) + " · " + stamp(row.server.publishedAt);
        if (!row.server.publishedBy.empty()) by += " · " + row.server.publishedBy;
        text(by, colBox(2, true), 12, th.textDim, FontFace::Body, Align::Left, true);
    } else if (row.server.archiveMs > 0) {
        text(formatRunTime(row.server.archiveMs), colBox(2, false), 15, th.textDim);
        text("ancienne version, non comparable", colBox(2, true), 12, th.warm);
    } else text(serverLoaded_ ? "—" : "…", colBox(2, false), 15, th.textDim);
    // Training of this version: every administrator's, this computer's session counted live.
    int64_t trainingMs = row.server.trainingMs, explored = row.server.explored;
    if (job && (job->state == TasTraining::JobState::Running || job->state == TasTraining::JobState::Stopping)) {
        trainingMs = std::max(trainingMs, job->trainingTotalMs());
        explored = std::max(explored, job->exploredAt(job->progress ? job->progress->explored : 0));
    }
    if (trainingMs > 0 || explored > 0) {
        text(durationText(trainingMs), colBox(3, false), 15, th.text);
        text(countText(explored) + " essais", colBox(3, true), 12, th.textDim);
    } else text("jamais entraîné", colBox(3, false), 15, th.textDim);
    // The attempts in short (the windows counted: finished, their share) and the best time
    // verified, beside a small step line of the best time along the training.
    {
        const Rect box{colBox(4, false).x, r.y + ui.s(8), colBox(4, false).w, r.h - ui.s(16)};
        const TasCurve* curve = g_.tasTraining.curves().find(row.local.levelId, row.local.category);
        const TasCurveVersion* v = curve && !row.local.hash.empty() ? curve->find(row.local.hash, kSimulationVersion) : nullptr;
        float tx = box.x;
        std::vector<Vec2> steps;  // (training time, best time)
        if (v)
            for (const TasCurvePoint& p : v->points)
                if (p.best()) steps.push_back({static_cast<float>(p.trainingMs), static_cast<float>(p.timeMs)});
        const float sw = std::min(ui.s(84), box.w * 0.4f);
        if (!steps.empty() && sw >= ui.s(30)) {
            const Rect sp{box.x, box.y + ui.s(4), sw, box.h - ui.s(8)};
            const float xEnd = std::max({steps.back().x + 1.0f, static_cast<float>(trainingMs), steps.front().x + 1.0f});
            float lo = steps.back().y, hi = steps.front().y;
            if (hi <= lo) hi = lo + 1.0f;
            auto X = [&](float t) { return sp.x + (t - steps.front().x) / std::max(1.0f, xEnd - steps.front().x) * sp.w; };
            auto Y = [&](float t) { return sp.y + (1.0f - (t - lo) / (hi - lo)) * sp.h; };
            ui.r().fillRoundedRect(sp.expanded(ui.s(2)), ui.s(4), Color{12, 18, 36, 200});
            const float th2 = std::max(1.0f, ui.s(1.5f));
            Vec2 prev{X(steps[0].x), Y(steps[0].y)};
            for (size_t i = 1; i < steps.size(); ++i) {
                const Vec2 at{X(steps[i].x), Y(steps[i].y)};
                ui.r().drawLine(prev, {at.x, prev.y}, th.accent, th2);
                ui.r().drawLine({at.x, prev.y}, at, th.accent, th2);
                prev = at;
            }
            ui.r().drawLine(prev, {sp.right(), prev.y}, th.accent, th2);
            ui.r().fillCircle(prev, ui.s(2.6f), th.accent);
            tx = sp.right() + ui.s(10);
            ++layout_.sparklines;
        }
        const Rect line1{tx, colBox(4, false).y, std::max(0.0f, box.right() - tx), colBox(4, false).h};
        const Rect line2{tx, colBox(4, true).y, std::max(0.0f, box.right() - tx), colBox(4, true).h};
        std::vector<const TasCurveWindow*> ws;
        if (v)
            for (const TasCurveWindow& w : v->windows) ws.push_back(&w);
        const TasAttemptWindow t = tasAttemptTotal(ws);
        if (t.attempts > 0) text(compactCount(t.finished) + " finis · " + percentText(t.finished, t.attempts), line1, 13, th.text);
        else if (trainingMs > 0) text("essais non détaillés", line1, 12, th.textDim);
        const int64_t best = v ? v->bestMs() : 0;
        if (best > 0) text("meilleur " + formatRunTime(best), line2, 12, th.textDim);
    }
    // Where it stands.
    Color sc = th.textDim;
    std::string line1 = statusLabel(st), line2;
    switch (st) {
        case Status::Training: {
            sc = th.accent;
            line1 += " · " + durationText(job ? job->sessionMs() : 0);
            if (job && job->progress) {
                const TasProgress& p = *job->progress;
                const int64_t best = p.bestMs > 0 ? p.bestMs : p.candidateMs;
                line2 = best > 0 ? "meilleur " + formatRunTime(best) : std::string("aucune course encore");
                const int64_t ref = row.server.publishedMs > 0 ? row.server.publishedMs : std::max(row.local.shippedMs, row.local.downloadedMs);
                if (ref > 0) line2 += " · en ligne " + formatRunTime(ref);
                line2 += " · " + std::to_string(p.improvements) + " amélioration(s)";
            } else line2 = "démarrage de cosmic_tas…";
            break;
        }
        case Status::Acquiring:
        case Status::Stopping: sc = th.warm; break;
        case Status::Queued: {
            sc = th.text;
            int k = 1;
            for (const TasPair& q : g_.tasTraining.queue()) {
                if (q == pair) break;
                ++k;
            }
            line2 = "n° " + std::to_string(k) + " de la file";
            break;
        }
        case Status::HeldByOther: {
            sc = th.warm;
            line1 = "En entraînement par";  // then the administrator's name, as he wrote it
            line2 = "depuis " + stamp(row.server.holderSince);
            break;
        }
        case Status::HeldByMe: sc = th.warm; line2 = "depuis " + stamp(row.server.holderSince); break;
        case Status::Unpublished: {
            sc = th.success;
            line2 = formatRunTime(row.local.localMs);
            if (row.server.publishedMs > 0) line2 += " (en ligne " + formatRunTime(row.server.publishedMs) + ")";
            break;
        }
        case Status::Recompute: sc = th.warm; line2 = "jamais entraîné pour cette version"; break;
        case Status::Failed: sc = th.danger; line2 = g_.tasTraining.failureOf(pair); break;
        case Status::Free:
            if (row.local.hash.empty()) line2 = "empreinte du niveau inconnue";
            break;
    }
    if (st == Status::HeldByOther) {
        // « En entraînement par » translated, the name never: two boxes side by side, measured.
        const Rect box = colBox(5, false);
        const float lw = std::min(box.w * 0.6f, ui.text().measure(line1, FontFace::Bold, ui.s(15)).x + ui.s(2));
        text(line1, {box.x, box.y, lw, box.h}, 15, sc, FontFace::Bold);
        text(row.server.holder, {box.x + lw + ui.s(6), box.y, std::max(0.0f, box.w - lw - ui.s(6)), box.h}, 15, sc, FontFace::Bold, Align::Left, true);
    } else {
        text(line1, colBox(5, false), 15, sc, FontFace::Bold);
    }
    if (!line2.empty()) text(line2, colBox(5, true), 12, th.textDim);
}

}  // namespace cosmic
