#include "ui/screens/AdminRaids.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/online/Clans.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

using namespace raidui;

namespace {

const char* const kFormats[] = {"court", "standard", "legendaire"};
const int kLocks[] = {10, 15, 30, 45, 60};
// 2.69: the late arrivals (minutes after the start a newcomer may still enter play; -1: until the end).
const int kLate[] = {-1, 0, 15, 30, 60};
// 2.69: the length of a catch-up session (days).
const int kCatchupDays[] = {1, 3, 7, 14, 30};

/// The first problem of a map's check (raidWorld.ts checkMap).
std::string firstProblem(const Json& map) {
    const Json p = map.is_object() ? map.value("problems", Json::array()) : Json::array();
    return p.is_array() && !p.empty() && p[0].is_string() ? p[0].get<std::string>() : std::string("problème inconnu");
}

std::string lateLabel(int minutes) {
    if (minutes < 0) return "jusqu'à la fin";
    if (minutes == 0) return "non (dès le début seulement)";
    return "jusqu'à " + std::to_string(minutes) + " min après le début";
}

const Json& at(const Json& j, const char* key) {
    static const Json null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}

std::string plural(int n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

}  // namespace

AdminRaids::AdminRaids(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)), reviews_(g, RaidReviewsPanel::Mode::Admin) {
    clearForm();
    reviews_.say = [this](const std::string& m, bool error) { say_(m, error); };
}

void AdminRaids::enter(bool force) {
    if (!loaded_ || force) load();
}

void AdminRaids::load() {
    const int gen = ++gen_;
    g_.online.get("/api/v1/admin/raids", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != gen_) return;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      events_ = r.data.value("events", Json::array());
                      if (!r.fromCache) skew_ = skewOf(r.data);
                      loaded_ = true;
                      if (!selected_.empty()) loadDetail();
                  }),
                  false);
}

void AdminRaids::loadDetail() {
    if (selected_.empty()) {
        detail_ = Json();
        return;
    }
    const int gen = ++detailGen_;
    g_.online.get("/api/v1/admin/raids/" + selected_, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != detailGen_) return;
                      if (lostRole_(r)) return;
                      if (r.ok) detail_ = r.data;
                  }),
                  false);
}

void AdminRaids::clearForm() {
    selected_.clear();
    detail_ = Json();
    formatIdx_ = 1;
    lockIdx_ = 2;
    difficultyIdx_ = 2;
    open_ = true;
    capacity_ = 0.0f;
    clanCapacity_ = 50.0f;
    lateIdx_ = 0;
    rewards_.clear();
    // A week from now at 21:00 of this computer.
    const raids::Civil d = raids::toSystemLocal(nowUtc(skew_) + 7 * 86400);
    date_ = raids::dateField(d);
    clock_ = "21:00";
    saved_ = Json();
}

void AdminRaids::select(const Json& j) {
    const raids::Event e = raids::parseEvent(j);
    if (!e.valid) return;
    selected_ = e.id;
    formatIdx_ = e.format == "court" ? 0 : e.format == "legendaire" ? 2 : 1;
    lockIdx_ = 2;
    for (int i = 0; i < 5; ++i)
        if (kLocks[i] == e.lockMinutes) lockIdx_ = i;
    difficultyIdx_ = std::clamp(e.difficulty - 1, 0, 4);
    open_ = e.registrationOpen;
    capacity_ = static_cast<float>(e.capacity);
    clanCapacity_ = static_cast<float>(e.clanCapacity);
    lateIdx_ = 0;
    for (int i = 0; i < 5; ++i)
        if (kLate[i] == e.lateMinutes) lateIdx_ = i;
    rewards_ = jstr(j, "rewards_text");
    const raids::Civil loc = raids::toSystemLocal(e.startsAt);
    date_ = raids::dateField(loc);
    clock_ = raids::hhmm(loc);
    saved_ = formJson(e.startsAt);
    // The stored lock time when the list does not offer it: kept as it is.
    if (kLocks[lockIdx_] != e.lockMinutes) saved_["lock_minutes"] = kLocks[lockIdx_];
    if (kLate[lateIdx_] != e.lateMinutes) saved_["late_minutes"] = kLate[lateIdx_] < 0 ? Json() : Json(kLate[lateIdx_]);
    loadDetail();
    live_ = Json();
    liveAt_ = -1000.0f;
}

Json AdminRaids::formJson(int64_t utc) const {
    Json b = {{"format", kFormats[formatIdx_]},
              {"lock_minutes", kLocks[lockIdx_]},
              {"registration_open", open_},
              {"capacity", static_cast<int>(std::lround(capacity_))},
              {"clan_capacity", static_cast<int>(std::lround(clanCapacity_))},
              {"difficulty", difficultyIdx_ + 1},
              {"rewards_text", clans::cleanName(rewards_)}};
    b["late_minutes"] = kLate[lateIdx_] < 0 ? Json() : Json(kLate[lateIdx_]);  // 2.69: null: until the end
    if (utc > 0) b["starts_at"] = raids::isoUtc(utc);
    return b;
}

void AdminRaids::save(const Json& body) {
    busy_ = true;
    const bool creating = selected_.empty();
    const std::string path = creating ? "/api/v1/admin/raids" : "/api/v1/admin/raids/" + selected_;
    g_.online.send(creating ? "POST" : "PATCH", path, body, OnlineService::guard(life_, [this, creating](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) return say_(r.message, true);
                       say_(creating ? "Brouillon créé : publiez-le pour prévenir tous les clans (inscrit au Journal)." : "Événement enregistré (inscrit au Journal).",
                            false);
                       select(at(r.data, "event"));
                       load();
                   }));
}

void AdminRaids::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    ui.pushId("raids");
    const float formW = std::min(ui.s(600), in.w * 0.44f);
    drawForm({in.x, in.y, formW, in.h});
    Rect right{in.x + formW + ui.s(18), in.y, in.w - formW - ui.s(18), in.h};
    // 2.69: three tabs in the right column: the events, the live panel, « Tester Asterion » (2.68).
    std::vector<std::string> tabs = {"Événements", "En direct"};
    const int testTab = g_.online.isAdmin() && !raid::Branch::all().empty() ? static_cast<int>(tabs.size()) : -1;
    if (testTab >= 0) tabs.push_back("Tester Asterion");
    const int reviewsTab = static_cast<int>(tabs.size());  // 2.77.4: the players' reviews
    tabs.push_back("Avis");
    sideTab_ = std::clamp(sideTab_, 0, static_cast<int>(tabs.size()) - 1);
    std::vector<bool> on(tabs.size(), false);
    on[static_cast<size_t>(sideTab_)] = true;
    float bottom = right.y;
    const int t = chips(ui, "side", right.x, right.y, right.right(), tabs, on, bottom);
    if (t >= 0) {
        sideTab_ = t;
        if (t == 1) liveAt_ = -1000.0f;  // read now
    }
    right.h -= bottom + ui.s(12) - right.y;
    right.y = bottom + ui.s(12);
    if (sideTab_ == 1) drawLive(right);
    else if (sideTab_ == testTab) drawTest(right);
    else if (sideTab_ == reviewsTab) drawReviews(right);
    else {
        const float listH = std::max(ui.s(160), right.h * 0.46f);
        drawList({right.x, right.y, right.w, listH});
        drawDetail({right.x, right.y + listH + ui.s(14), right.w, right.h - listH - ui.s(14)});
    }
    ui.popId();
}

void AdminRaids::drawReviews(const Rect& r) {
    // 2.77.4: every review of the selected event (masked ones and their reports included): « Masquer le
    // commentaire » keeps its stars, « Rétablir » clears its reports; each action goes to the Journal.
    UIContext& ui = g_.ui;
    if (selected_.empty()) {
        const float top = section(ui, r, "AVIS DES JOUEURS", kSky);
        drawLines(ui, wrap(ui, "Choisissez un événement dans l'onglet « Événements » pour lire les avis de ses joueurs.", FontFace::Body, 15, r.w - ui.s(28), 3),
                  {r.x + ui.s(14), top}, 15, ui.s(22), kDim);
        return;
    }
    reviews_.open(selected_);
    reviews_.draw(r);
}

void AdminRaids::loadLive() {
    if (selected_.empty()) return;
    liveAt_ = g_.ui.time();
    const int gen = ++liveGen_;
    const std::string id = selected_;
    g_.online.get("/api/v1/admin/raids/" + id + "/live", OnlineService::guard(life_, [this, gen, id](const ApiResult& r) {
                      if (gen != liveGen_) return;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      live_ = r.data;
                      liveFor_ = id;
                      if (!r.fromCache) skew_ = skewOf(r.data);
                  }),
                  false);
}

void AdminRaids::action(const std::string& path, const Json& body, const std::string& done) {
    if (selected_.empty() || busy_) return;
    busy_ = true;
    g_.online.send("POST", "/api/v1/admin/raids/" + selected_ + path, body, OnlineService::guard(life_, [this, done](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           confirmError_ = r.message;
                           return say_(r.message, true);
                       }
                       confirm_ = false;
                       std::string text = done;
                       if (const Json map = r.data.value("map", Json()); map.is_object())
                           text = jget<bool>(map, "ok", false)
                                      ? "Carte validée : " + std::to_string(jget<int>(map, "zones", 0)) + " zones, " + std::to_string(jget<int>(map, "bosses", 0)) +
                                            " antres de gigaboss, " + std::to_string(jget<int>(map, "fights", 0)) + " combat(s) prêt(s) (inscrit au Journal)."
                                      : "Carte refusée : " + firstProblem(map);
                       say_(text, false);
                       load();
                       loadLive();
                   }));
}

void AdminRaids::drawLive(const Rect& r) {
    // 2.69 (maquette « Administration », EN DIRECT): real figures only, read again every 10 s.
    UIContext& ui = g_.ui;
    ui.pushId("live");
    raids::Event e;
    for (const Json& j : events_)
        if (jstr(j, "id") == selected_) e = raids::parseEvent(j);
    if (!selected_.empty() && g_.ui.time() - liveAt_ >= 10.0f) loadLive();
    const bool fresh = liveFor_ == selected_ && live_.is_object();
    const int64_t now = nowUtc(skew_);
    const raids::State st = e.valid ? raids::stateAt(e, now) : raids::State::Draft;
    const Json& ev = at(live_, "event");
    const bool suspended = fresh && jget<bool>(ev, "suspended", false);
    std::string title = e.valid ? "EN DIRECT · " + whenShort(e.startsAt) : "EN DIRECT";
    const float top = section(ui, r, title, kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    if (!e.valid) {
        drawLines(ui, wrap(ui, "Choisissez un événement dans l'onglet « Événements » pour suivre son déroulement en direct.", FontFace::Body, 15, w, 3), {x, top}, 15,
                  ui.s(22), kDim);
        ui.popId();
        return;
    }
    float y = top;
    // The state, then the actions (each measured; they share the width when it is short).
    {
        std::string state = suspended ? "SUSPENDU" : raids::stateLabel(st);
        if (st == raids::State::Running && !suspended) state += " · " + raids::countdownClock(std::max<int64_t>(0, now - e.startsAt));
        if (e.catchup) state = "RATTRAPAGE · " + state;
        const float pw = pill(ui, x, y + ui.s(20), state, suspended ? Color{122, 26, 26, 255} : stateFill(st), suspended ? kText : stateText(st), x + w);
        struct Act {
            std::string id, label;
            bool danger, enabled;
        };
        std::vector<Act> acts;
        const bool live = e.published && !e.cancelled && st != raids::State::Finished;
        if (live && !suspended) acts.push_back({"suspend", "Suspendre l'événement…", true, !busy_});
        if (live && suspended) acts.push_back({"resume", "Reprendre l'événement", false, !busy_});
        acts.push_back({"map", "Valider la carte", false, !busy_ && !e.cancelled});
        if (e.published && !e.cancelled && !e.catchup && st == raids::State::Finished) acts.push_back({"catchup", "Session de rattrapage…", false, !busy_});
        const float gap = ui.s(8), room = w - pw - ui.s(12);
        float need = 0.0f;
        for (const Act& a : acts) need += textW(ui, a.label, 14, FontFace::Bold) + ui.s(28);
        need += gap * static_cast<float>(acts.size() - 1);
        const float scale = need > room ? room / need : 1.0f;
        float bx = x + w;
        for (auto it = acts.rbegin(); it != acts.rend(); ++it) {
            const float bw = (textW(ui, it->label, 14, FontFace::Bold) + ui.s(28)) * scale;
            bx -= bw;
            UIContext::ButtonStyle bs;
            bs.small = true;
            bs.fontSize = 14;
            bs.danger = it->danger;
            bs.enabled = it->enabled;
            if (ui.button(it->id, {bx, y, bw, ui.s(40)}, it->label, bs)) {
                if (it->id == "suspend") {
                    reason_.clear();
                    confirmError_.clear();
                    confirmKind_ = Confirm::Suspend;
                    confirm_ = true;
                } else if (it->id == "resume") {
                    action("/resume", Json::object(), "Événement repris : le temps suspendu est rendu à l'événement (inscrit au Journal).");
                } else if (it->id == "map") {
                    action("/validate-map", Json::object(), "");
                } else if (it->id == "catchup") {
                    confirmError_.clear();
                    confirmKind_ = Confirm::Catchup;
                    confirm_ = true;
                }
            }
            bx -= gap;
        }
        y += ui.s(52);
    }
    if (!fresh) {
        fit(ui, "Lecture des chiffres en direct…", {x, y + ui.s(14)}, 15, w, kDim);
        ui.popId();
        return;
    }
    const Json& stats = at(live_, "stats");
    // Five figures (maquette): players in play, instances, gigaboss down, reconnections, refused reports.
    {
        const float gap = ui.s(8), tw = (w - 4 * gap) / 5.0f, th = ui.s(64);
        struct Fig {
            std::string caption, value;
            Color c;
        };
        const Fig figs[5] = {
            {"Joueurs connectés", std::to_string(jget<int>(stats, "players", 0)), kText},
            {"Instances", std::to_string(jget<int>(stats, "instances", 0)) + " / " + std::to_string(jget<int>(stats, "instances_total", 0)), kText},
            {"Gigaboss vaincus", std::to_string(jget<int>(stats, "gigaboss_defeated", 0)) + " / " + std::to_string(jget<int>(stats, "gigaboss", 7)), kGold},
            {"Reconnexions", std::to_string(jget<int>(stats, "reconnects", 0)), kText},
            {"Rapports refusés", std::to_string(jget<int>(stats, "rejected", 0)), jget<int>(stats, "rejected", 0) > 0 ? Color{255, 138, 138, 255} : kText},
        };
        for (int i = 0; i < 5; ++i) {
            const Rect t{x + static_cast<float>(i) * (tw + gap), y, tw, th};
            ui.r().fillRoundedRect(t, ui.s(10), Color{13, 18, 38, 255});
            fit(ui, figs[i].caption, {t.x + ui.s(10), t.y + ui.s(18)}, 12, t.w - ui.s(20), kDim);
            fit(ui, figs[i].value, {t.x + ui.s(10), t.y + ui.s(44)}, 19, t.w - ui.s(20), figs[i].c, FontFace::Title);
        }
        y += th + ui.s(8);
    }
    // One line of context: registrations, the world's progress, what waits, the duplicates counted once.
    {
        std::string ctx = std::to_string(jget<int>(stats, "registered", 0)) + " inscrits · " + std::to_string(jget<int>(stats, "clans", 0)) + " clans · progression mondiale " +
                          std::to_string(jget<int>(stats, "progress", 0)) + " % · en attente : " + std::to_string(jget<int>(stats, "pending_tables", 0)) + " tableaux, " +
                          std::to_string(jget<int>(stats, "pending_objectives", 0)) + " objectifs · doublons comptés une fois : " +
                          std::to_string(jget<int>(stats, "duplicates", 0));
        if (const std::string outcome = jstr(ev, "outcome"); !outcome.empty()) ctx = "Résultat officiel figé : " + outcome + " · " + ctx;
        y += drawLines(ui, wrap(ui, ctx, FontFace::Body, 13, w, 2), {x, y}, 13, ui.s(18), kDim) + ui.s(6);
    }
    // The map's check at the bottom (its lines first).
    std::string mapText;
    Color mapColor = kDim;
    if (const Json& map = at(live_, "map"); map.is_object()) {
        const bool ok = jget<bool>(map, "ok", false);
        mapText = ok ? "Carte validée par " + jstr(map, "by", "?") + " : " + std::to_string(jget<int>(map, "zones", 0)) + " zones, " +
                           std::to_string(jget<int>(map, "bosses", 0)) + " antres, " + std::to_string(jget<int>(map, "fights", 0)) + " combat(s) prêt(s)" +
                           (jget<bool>(map, "current", false) ? "" : " · les catalogues ont changé depuis : validez de nouveau")
                     : "Carte refusée : " + firstProblem(map);
        mapColor = ok && jget<bool>(map, "current", false) ? kGreen : Color{255, 196, 92, 255};
    } else {
        mapText = "Carte jamais validée pour cet événement : la publication la vérifie d'office.";
    }
    const auto mapLines = wrap(ui, mapText, FontFace::Body, 13, w, 2);
    const float mapH = static_cast<float>(mapLines.size()) * ui.s(18);
    drawLines(ui, mapLines, {x, r.bottom() - ui.s(10) - mapH}, 13, ui.s(18), mapColor);
    // Two columns: the instances, the journal.
    const float colGap = ui.s(12), colW = (w - colGap) * 0.5f;
    const Rect li{x, y, colW, r.bottom() - ui.s(16) - mapH - y};
    const Rect lj{x + colW + colGap, y, colW, li.h};
    if (li.h < ui.s(70)) {
        ui.popId();
        return;
    }
    ui.r().fillRoundedRect(li, ui.s(10), Color{13, 18, 38, 255});
    ui.r().fillRoundedRect(lj, ui.s(10), Color{13, 18, 38, 255});
    fit(ui, "INSTANCES", {li.x + ui.s(10), li.y + ui.s(16)}, 13, li.w - ui.s(20), kSky, FontFace::Bold);
    fit(ui, "JOURNAL (ACTIONS ADMIN ET ANOMALIES)", {lj.x + ui.s(10), lj.y + ui.s(16)}, 13, lj.w - ui.s(20), kSky, FontFace::Bold);
    {
        const Json& list = at(live_, "instances");
        const Rect area{li.x + ui.s(8), li.y + ui.s(30), li.w - ui.s(10), li.h - ui.s(36)};
        if (!list.is_array() || list.empty()) {
            drawLines(ui, wrap(ui, "Aucune instance : elles s'ouvrent quand les joueurs entrent en jeu.", FontFace::Body, 13, area.w - ui.s(8), 3), {area.x, area.y + ui.s(4)},
                      13, ui.s(18), kDim);
        } else {
            const float rowH = ui.s(26);
            ui.beginScroll("instances", area, static_cast<float>(list.size()) * rowH);
            float ry = area.y - ui.scrollOffset();
            const float idW = ui.s(44), nW = ui.s(48), msW = ui.s(64), aw = area.w - ui.s(14);
            for (const Json& i : list) {
                const float cy = ry + rowH * 0.5f;
                ry += rowH;
                if (cy < area.y || cy > area.bottom()) continue;
                const int players = jget<int>(i, "players", 0);
                const Json lat = i.value("latency_ms", Json());
                const int ms = lat.is_number_integer() ? lat.get<int>() : -1;
                fit(ui, "#" + std::to_string(jget<int>(i, "number", 0)), {area.x, cy}, 13, idW, kDim);
                fit(ui, "[" + jstr(i, "clan_tag") + "]·" + std::to_string(jget<int>(i, "group", 1)) + " · " + jstr(i, "branch_name") + " · " + jstr(i, "zone_name"),
                    {area.x + idW, cy}, 13, aw - idW - nW - msW - ui.s(12), players > 0 ? kText : kDim);
                fit(ui, std::to_string(players) + " / 8", {area.x + aw - msW - ui.s(6), cy}, 13, nW, kText, FontFace::Body, Align::Right);
                const Color mc = ms < 0 ? kDim : ms <= 100 ? kGreen : ms <= 180 ? kGold : Color{255, 138, 138, 255};
                fit(ui, ms < 0 ? std::string("—") : std::to_string(ms) + " ms", {area.x + aw, cy}, 13, msW, mc, FontFace::Body, Align::Right);
            }
            ui.endScroll();
        }
    }
    {
        const Json& list = at(live_, "journal");
        const Rect area{lj.x + ui.s(8), lj.y + ui.s(30), lj.w - ui.s(10), lj.h - ui.s(36)};
        if (!list.is_array() || list.empty()) {
            fit(ui, "Rien à signaler.", {area.x, area.y + ui.s(12)}, 13, area.w, kDim);
        } else {
            const float timeW = ui.s(48), lineH = ui.s(17), tw = area.w - timeW - ui.s(16);
            std::vector<std::vector<std::string>> lines;
            float total = 0.0f;
            for (const Json& j : list) {
                lines.push_back(wrap(ui, jstr(j, "text"), FontFace::Body, 13, tw, 3));
                total += static_cast<float>(lines.back().size()) * lineH + ui.s(6);
            }
            ui.beginScroll("journal", area, total);
            float ry = area.y - ui.scrollOffset();
            size_t k = 0;
            for (const Json& j : list) {
                const auto& ls = lines[k++];
                const float h = static_cast<float>(ls.size()) * lineH;
                if (ry + h >= area.y && ry <= area.bottom()) {
                    std::string when;
                    if (auto t = raids::parseUtc(jstr(j, "at"))) when = raids::hhmm(raids::toSystemLocal(*t));
                    const std::string kind = jstr(j, "kind");
                    const Color c = kind == "anomaly" ? Color{255, 138, 138, 255} : kind == "admin" ? kSky : kText;
                    fit(ui, when, {area.x, ry + lineH * 0.5f}, 13, timeW, kDim);
                    drawLines(ui, ls, {area.x + timeW, ry}, 13, lineH, c);
                }
                ry += h + ui.s(6);
            }
            ui.endScroll();
        }
    }
    ui.popId();
}

void AdminRaids::drawTest(const Rect& r) {
    // 2.68: a branch of the catalog (raid::Branch::all: a branch added later appears by itself), one
    // of its zones, then « Lancer le test »: solo, local, never reported.
    UIContext& ui = g_.ui;
    ui.pushId("test");
    float top = section(ui, r, "TESTER ASTERION · ADMINISTRATEURS", kGold);
    {
        // 2.77: one zone of a branch, or the whole clan raid simulated (every branch, then the heart).
        std::vector<bool> on{testMode_ == 0, testMode_ == 1};
        float bottom = top;
        const int m = chips(ui, "mode", r.x + ui.s(14), top, r.right() - ui.s(14), {"Une zone", "Simuler un raid complet"}, on, bottom);
        if (m >= 0) testMode_ = m;
        top = bottom + ui.s(10);
    }
    if (testMode_ == 1) {
        ui.popId();
        drawSim({r.x, top, r.w, r.bottom() - top});
        return;
    }
    const auto& branches = raid::Branch::all();
    testBranch_ = std::clamp(testBranch_, 0, static_cast<int>(branches.size()) - 1);
    const raid::Branch& b = *branches[static_cast<size_t>(testBranch_)];
    std::vector<std::string> branchNames;
    for (const raid::Branch* x : branches) branchNames.push_back(x->name);
    std::vector<std::string> zoneNames;
    int bossZone = 0;
    for (const raid::ZoneInfo& z : b.zones) {
        zoneNames.push_back(std::to_string(z.id) + " · " + z.name + (z.boss ? " (" + b.gigaboss + ")" : ""));
        if (z.boss && bossZone == 0) bossZone = static_cast<int>(zoneNames.size()) - 1;
    }
    // 2.75: the armory (« vestiaire ») before the branch's zones, last in the list.
    const bool armory = b.hasArmory();
    if (armory) zoneNames.push_back("Vestiaire (choix des pouvoirs)");
    if (testZone_ < 0 || testZone_ >= static_cast<int>(zoneNames.size())) testZone_ = bossZone;  // the gigaboss's fight by default
    const bool atArmory = armory && testZone_ == static_cast<int>(b.zones.size());
    const raid::ZoneRules* rules = atArmory ? b.zoneRules(0) : b.zoneRules(b.zones[static_cast<size_t>(testZone_)].id);
    const bool giga = rules && rules->gigaboss.present;
    const float x = r.x + ui.s(14), w = r.w - ui.s(28), gap = ui.s(10), rowH = ui.s(44);
    const float half = (w - gap) * 0.5f;
    float y = top;
    const int prevBranch = testBranch_;
    ui.cycle("branch", {x, y, half, rowH}, "Branche", testBranch_, branchNames);
    if (testBranch_ != prevBranch) testZone_ = -1;
    ui.cycle("zone", {x + half + gap, y, half, rowH}, "Zone", testZone_, zoneNames);
    y += rowH + gap;
    // 2.75: the simulated players only (the administrator is not counted): 0 (alone) to 7; seats = 1 + them.
    static const std::vector<std::string> kPlayers = {"0 (vous seul)", "1", "2", "3", "4", "5", "6", "7"};
    // 2.74 (phase 6): the Cœur d'Asterion has four phases; the seven gigaboss three.
    const bool heart = b.part == raid::kHeartPart;
    static const std::vector<std::string> kPhases = {"1", "2", "3"};
    static const std::vector<std::string> kHeartPhases = {"1", "2", "3", "4"};
    if (!heart) testPhase_ = std::min(testPhase_, 2);
    ui.cycle("players", {x, y, half, rowH}, "Joueurs simulés", testPlayers_, kPlayers);
    const bool ro = ui.readOnly;
    ui.readOnly = !giga;
    ui.cycle("phase", {x + half + gap, y, half, rowH}, giga ? "Phase de départ" : "Phase (sans gigaboss)", testPhase_, heart ? kHeartPhases : kPhases);
    ui.readOnly = ro;
    y += rowH + gap;
    if (heart) {
        // The parts of the titan simulated as fallen (their effects on the heart), and the relics.
        static const char* kParts[] = {"Crâne", "Bras gauche", "Bras droit", "Torse", "Abdomen", "Jambe gauche", "Jambe droite"};
        const float th = ui.s(34), tgap = ui.s(6);
        const int perRow = 2;  // the label of a toggle needs its room beside the switch
        const float tw = (w - tgap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
        fit(ui, "Parties du titan simulées comme tombées (leurs effets sur le Cœur) :", {x, y + ui.s(10)}, 13, w, kDim);
        y += ui.s(22);
        for (int p = 0; p < 7; ++p) {
            bool on = (testParts_ >> p) & 1u;
            const float px = x + static_cast<float>(p % perRow) * (tw + tgap), py = y + static_cast<float>(p / perRow) * (th + tgap);
            if (ui.toggle("part" + std::to_string(p), {px, py, tw, th}, kParts[p], on)) testParts_ = on ? (testParts_ | (1u << p)) : (testParts_ & ~(1u << p));
        }
        y += static_cast<float>((7 + perRow - 1) / perRow) * (th + tgap) + gap - tgap;
        static const std::vector<std::string> kRelics = {"0", "1", "2", "3", "4", "5", "6", "7"};
        ui.cycle("relics", {x, y, half, rowH}, "Reliques validées", testRelics_, kRelics);
        y += rowH + gap;
    }
    {
        // 2.77: the simulated players' level (their reaction time and precision), the whole width.
        static const std::vector<std::string> kSkills = {"Débutant", "Confirmé", "Expert"};
        ui.cycle("skill", {x, y, w, rowH}, "Niveau des joueurs simulés", testSkill_, kSkills);
        y += rowH + gap;
    }
    bool inv = g_.settings.gameplay.adminInvincible;
    if (ui.toggle("invincible", {x, y, half, rowH}, "Invincible (admin) · non classé", inv)) {
        g_.settings.gameplay.adminInvincible = inv;
        g_.saveSettings();
    }
    UIContext::ButtonStyle bs;
    bs.primary = true;
    if (ui.button("launch", {x + half + gap, y, half, rowH}, "Lancer le test", bs)) {
        RaidTestSetup t;
        t.branch = b.id;
        t.zone = atArmory ? raid::RaidRun::kArmory : b.zones[static_cast<size_t>(testZone_)].id;
        t.players = 1 + testPlayers_;  // 2.75: the administrator + the simulated players chosen
        t.startPhase = giga ? testPhase_ + 1 : 1;
        t.invincible = g_.settings.gameplay.adminInvincible;
        t.parts = testParts_;  // 2.74 (phase 6): the heart's test only
        t.relics = testRelics_;
        t.skill = testSkill_;  // 2.77
        g_.app.pushScene(makeRaidTest(g_, t));
    }
    y += rowH + ui.s(6);
    drawLines(ui, wrap(ui, std::string("En local et seul : aucun rapport, ni consensus, ni état de branche, ni classement, ni contribution, ni récompense. "
                                   "Les joueurs simulés jouent la zone eux-mêmes (") +
                               (testSkill_ == 0 ? "réaction 0,3 s, 60 % de précision" : testSkill_ == 2 ? "réaction 0,05 s, toujours précis" : "réaction 0,15 s, 85 % de précision") +
                               ") ; après 15 s sans commande, votre joueur passe en pilote automatique.",
                       FontFace::Body, 13, w,
                       std::max(1, static_cast<int>((r.bottom() - ui.s(8) - y) / ui.s(18)))),
              {x, y}, 13, ui.s(18), kDim);
    ui.popId();
}

void AdminRaids::drawSim(const Rect& r, bool capture) {
    // 2.77: « Simuler un raid complet » — the whole clan raid in local: the seven branches (each one's
    // armory first), their zones, then the Cœur d'Asterion with every group. Nothing is ever sent.
    UIContext& ui = g_.ui;
    ui.pushId("sim");
    const float x = r.x + ui.s(14), w = r.w - ui.s(28), gap = ui.s(10), rowH = ui.s(44);
    const float half = (w - gap) * 0.5f;
    float y = r.y;
    static const std::vector<std::string> kGroups = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14"};
    static const std::vector<std::string> kBots = {"1", "2", "3", "4", "5", "6", "7"};
    static const std::vector<std::string> kSkills = {"Débutant", "Confirmé", "Expert"};
    static const std::vector<std::string> kSpeeds = {"×1 (temps réel)", "×2 (accéléré)", "×4 (accéléré)"};
    // Each setting on the whole width (its caption has room).
    ui.cycle("groups", {x, y, w, rowH}, "Groupes", simGroups_, kGroups);
    y += rowH + gap;
    ui.cycle("bots", {x, y, w, rowH}, "Joueurs simulés par groupe (sans vous)", simBots_, kBots);
    y += rowH + gap;
    ui.cycle("skill", {x, y, w, rowH}, "Niveau des joueurs simulés", testSkill_, kSkills);
    y += rowH + gap;
    ui.cycle("speed", {x, y, w, rowH}, "Vitesse des groupes non regardés", simSpeed_, kSpeeds);
    y += rowH + gap;
    bool inv = g_.settings.gameplay.adminInvincible;
    if (ui.toggle("invincible", {x, y, half, rowH}, "Invincible (admin) · non classé", inv)) {
        g_.settings.gameplay.adminInvincible = inv;
        if (!capture) g_.saveSettings();
    }
    ui.toggle("botsinvincible", {x + half + gap, y, half, rowH}, "Joueurs simulés invincibles", simBotsInvincible_);
    y += rowH + gap;
    UIContext::ButtonStyle bs;
    bs.primary = true;
    bs.fontSize = std::max(ui.fitFont("Lancer la simulation", FontFace::Bold, 17, w - ui.s(20), 0.7f), 12.0f / ui.s(1.0f));
    if (ui.button("launch", {x, y, w, rowH}, "Lancer la simulation", bs) && !capture) {
        RaidSimSetup s;
        s.groups = simGroups_ + 1;
        s.bots = simBots_ + 1;
        s.skill = testSkill_;
        s.background = simSpeed_ == 2 ? 4 : simSpeed_ == 1 ? 2 : 1;
        s.invincible = g_.settings.gameplay.adminInvincible;
        s.botsInvincible = simBotsInvincible_;
        g_.app.pushScene(makeRaidSim(g_, s));
    }
    y += rowH + ui.s(10);
    // What it is, measured into what is left of the panel.
    const int seats = (simBots_ + 1) + 1;  // the simulated players and the free place
    const std::string text = "Les 7 branches et toutes leurs zones (vestiaire compris), puis le Cœur d'Asterion avec tous les groupes : " +
                             std::to_string(simGroups_ + 1) + " groupes de " + std::to_string(simBots_ + 1) + " joueurs simulés et une place libre (" +
                             std::to_string(seats) + " places). Événements mondiaux, carte du titan, zones terminées, contributions, groupes "
                             "éliminés et renforts comme dans un vrai raid. Rejoignez ou regardez n'importe quel groupe depuis la pause.";
    const float lineH = ui.s(18);
    const int lines = std::max(1, static_cast<int>((r.bottom() - ui.s(34) - y) / lineH));
    y += drawLines(ui, wrap(ui, text, FontFace::Body, 13, w, lines), {x, y}, 13, lineH, kDim) + ui.s(6);
    if (y + ui.s(22) <= r.bottom())
        fit(ui, "TEST LOCAL — rien n'est envoyé : aucun appel réseau, aucun résultat, aucune récompense.", {x, y + ui.s(10)}, 13, w, kGold, FontFace::Bold);
    ui.popId();
}

void AdminRaids::drawForm(const Rect& r) {
    UIContext& ui = g_.ui;
    raids::Event cur;
    for (const Json& e : events_)
        if (jstr(e, "id") == selected_) cur = raids::parseEvent(e);
    const bool editing = !selected_.empty() && cur.valid;
    const int64_t now = nowUtc(skew_);
    const raids::State st = editing ? raids::stateAt(cur, now) : raids::State::Draft;
    const std::string title = !editing ? "PROGRAMMER UN ÉVÉNEMENT MONDIAL" : !cur.published ? "BROUILLON · ÉVÉNEMENT MONDIAL" : "ÉVÉNEMENT MONDIAL · " + raids::stateLabel(st);
    const float top = section(ui, r, title, kPeach);
    const float x = r.x + ui.s(16), w = r.w - ui.s(32), right = x + w;
    const float labelW = std::min(ui.s(200), w * 0.38f), fx = x + labelW + ui.s(10), fw = right - fx;
    const float rowH = ui.s(56);
    const float buttonsH = ui.s(116), previewH = ui.s(104);
    const Rect fields{x, top, w + ui.s(10), r.bottom() - ui.s(14) - buttonsH - previewH - top};
    const float contentH = 10 * rowH;
    const bool locked = editing && (st == raids::State::Running || st == raids::State::Finished || st == raids::State::Cancelled);
    ui.beginScroll("fields", fields, contentH);
    float y = fields.y - ui.scrollOffset();
    auto label = [&](const std::string& t) { fit(ui, t, {x, y + rowH * 0.5f}, 15, labelW, kDim); };
    const bool ro = ui.readOnly;
    ui.readOnly = locked;
    label("Raid");
    fit(ui, "Asterion, le Dévoreur de mondes", {fx, y + rowH * 0.5f}, 16, fw, kText, FontFace::Bold);
    y += rowH;
    label("Format");
    {
        static const char* kNames[] = {"Court", "Standard", "Légendaire"};
        const float bw = (fw - 2 * ui.s(8)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            UIContext::ButtonStyle st2;
            st2.small = true;
            st2.selected = formatIdx_ == i;
            if (ui.button(std::string("format/") + kFormats[i], {fx + static_cast<float>(i) * (bw + ui.s(8)), y + ui.s(6), bw, ui.s(44)}, kNames[i], st2) && !locked)
                formatIdx_ = i;
        }
    }
    y += rowH;
    label("Début (votre heure)");
    {
        const float dw = fw * 0.58f;
        ui.textField("date", {fx, y + ui.s(6), dw, ui.s(44)}, date_, "21/11/2026", 10);
        ui.textField("clock", {fx + dw + ui.s(8), y + ui.s(6), fw - dw - ui.s(8), ui.s(44)}, clock_, "21:00", 5);
    }
    y += rowH;
    std::vector<std::string> locks;
    for (int m : kLocks) locks.push_back(std::to_string(m) + " min avant le début");
    ui.cycle("lock", {x, y + ui.s(4), w, ui.s(48)}, "Verrouillage des inscriptions", lockIdx_, locks);
    lockIdx_ = std::clamp(lockIdx_, 0, 4);
    y += rowH;
    ui.toggle("open", {x, y + ui.s(4), w, ui.s(48)}, "Inscriptions ouvertes", open_);
    y += rowH;
    label("Joueurs maximum (0 : illimité)");
    ui.numberField("capacity", {fx, y + ui.s(6), std::min(fw, ui.s(240)), ui.s(44)}, capacity_, 10.0f, 0.0f, 100000.0f, "%.0f");
    y += rowH;
    label("Joueurs par clan (1 à 50)");
    ui.numberField("clancap", {fx, y + ui.s(6), std::min(fw, ui.s(240)), ui.s(44)}, clanCapacity_, 1.0f, 1.0f, 50.0f, "%.0f");
    y += rowH;
    ui.cycle("difficulty", {x, y + ui.s(4), w, ui.s(48)}, "Difficulté", difficultyIdx_, {"1 / 5", "2 / 5", "3 / 5", "4 / 5", "5 / 5"});
    difficultyIdx_ = std::clamp(difficultyIdx_, 0, 4);
    y += rowH;
    // 2.69: the late arrivals: a newcomer may still enter play this long after the start.
    {
        std::vector<std::string> lates;
        for (int m : kLate) lates.push_back(lateLabel(m));
        ui.cycle("late", {x, y + ui.s(4), w, ui.s(48)}, "Arrivée en retard", lateIdx_, lates);
        lateIdx_ = std::clamp(lateIdx_, 0, 4);
    }
    y += rowH;
    label("Récompenses (texte)");
    ui.textField("rewards", {fx, y + ui.s(6), fw, ui.s(44)}, rewards_, "Vide : le texte d'Asterion", raids::kMaxRewardsText);
    ui.readOnly = ro;
    ui.endScroll();

    // The start: read in this computer's zone, sent in UTC, previewed in several zones.
    std::string problem;
    int64_t utc = 0;
    int yy = 0, mo = 0, dd = 0, hh = 0, mi = 0;
    if (!raids::parseDate(date_, yy, mo, dd)) problem = "Date attendue : JJ/MM/AAAA.";
    else if (!raids::parseClock(clock_, hh, mi)) problem = "Heure attendue : HH:MM.";
    else {
        raids::Civil wall;
        wall.year = yy;
        wall.month = mo;
        wall.day = dd;
        wall.hour = hh;
        wall.minute = mi;
        utc = raids::fromSystemLocal(wall);
    }
    const Json body = formJson(utc);
    Json changes = Json::object();
    if (editing && saved_.is_object())
        for (auto it = body.begin(); it != body.end(); ++it)
            if (!saved_.contains(it.key()) || saved_[it.key()] != it.value()) changes[it.key()] = it.value();
    const bool startChanged = !editing || changes.contains("starts_at");
    if (problem.empty() && startChanged) {
        const int lead = std::max(raids::kMinAheadMinutes, kLocks[lockIdx_] + 5);
        if (utc < now + 60LL * lead) problem = "Au moins " + std::to_string(lead) + " minutes à l'avance.";
        else if (utc > now + 365LL * 86400) problem = "Au plus un an à l'avance.";
    }
    const float py = fields.bottom() + ui.s(8);
    const Rect prev{x, py, w, previewH - ui.s(14)};
    ui.r().fillRoundedRect(prev, ui.s(10), Color{13, 18, 38, 255});
    ui.r().drawRoundedRect(prev, ui.s(10), kBorder, ui.s(1));
    std::string text;
    if (!problem.empty()) text = problem;
    else {
        std::string zones;
        for (const raids::Zone& z : raids::previewZones()) {
            const raids::Civil c = raids::toLocal(utc, z);
            zones += (zones.empty() ? "" : " · ") + z.name + " " + raids::weekdayShort(c.weekday) + " " + raids::hhmm(c);
        }
        text = "Enregistré en UTC : " + raids::isoUtc(utc) + ". Aperçu des heures locales : " + zones +
               ". Le changement d'heure est calculé par le jeu de chaque joueur.";
    }
    drawLines(ui, wrap(ui, text, FontFace::Body, 13, prev.w - ui.s(20), static_cast<int>((prev.h - ui.s(12)) / ui.s(18))), {prev.x + ui.s(10), prev.y + ui.s(6)}, 13,
              ui.s(18), problem.empty() ? kDim : ui.theme().danger);

    // The buttons: two rows, each measured to share the width.
    const float by1 = r.bottom() - ui.s(14) - ui.s(44) - ui.s(10) - ui.s(44), by2 = r.bottom() - ui.s(14) - ui.s(44);
    const float half = (w - ui.s(10)) * 0.5f;
    UIContext::ButtonStyle plain;
    plain.small = true;
    plain.enabled = !busy_;
    if (ui.button("new", {x, by1, half, ui.s(44)}, "Nouvel événement", plain)) clearForm();
    UIContext::ButtonStyle saveSt = plain;
    saveSt.primary = !editing;
    saveSt.enabled = !busy_ && problem.empty() && !locked && (!editing || !changes.empty());
    if (ui.button("save", {x + half + ui.s(10), by1, half, ui.s(44)}, editing ? "Enregistrer les changements" : "Créer le brouillon", saveSt))
        save(editing ? changes : body);
    if (editing && !cur.published) {
        // 2.69: « Valider la carte » (the server checks the titan's catalogs; publishing needs a valid map).
        UIContext::ButtonStyle check = plain;
        if (ui.button("validate", {x, by2, half, ui.s(44)}, "Valider la carte", check)) action("/validate-map", Json::object(), "");
        UIContext::ButtonStyle pub = plain;
        pub.primary = true;
        pub.enabled = !busy_ && changes.empty() && st == raids::State::Draft && now < cur.lockAt;
        if (ui.button("publish", {x + half + ui.s(10), by2, half, ui.s(44)}, changes.empty() ? "Publier et notifier tous les clans" : "Enregistrez avant de publier", pub)) {
            busy_ = true;
            g_.online.send("POST", "/api/v1/admin/raids/" + selected_ + "/publish", Json::object(), OnlineService::guard(life_, [this](const ApiResult& res) {
                               busy_ = false;
                               if (lostRole_(res)) return;
                               if (!res.ok) return say_(res.message, true);
                               const int n = jget<int>(res.data, "notified", 0);
                               say_("Événement publié : " + plural(n, "joueur prévenu", "joueurs prévenus") + " (inscrit au Journal).", false);
                               load();
                           }));
        }
    } else if (editing && !locked) {
        UIContext::ButtonStyle tog = plain;
        tog.enabled = !busy_ && changes.empty();
        if (ui.button("toggle", {x, by2, half, ui.s(44)}, cur.registrationOpen ? "Fermer les inscriptions" : "Ouvrir les inscriptions", tog))
            save({{"registration_open", !cur.registrationOpen}});
        UIContext::ButtonStyle cancel = plain;
        cancel.danger = true;
        if (ui.button("cancel", {x + half + ui.s(10), by2, half, ui.s(44)}, "Annuler l'événement…", cancel)) {
            reason_.clear();
            confirmError_.clear();
            confirmKind_ = Confirm::Cancel;
            confirm_ = true;
        }
    } else if (editing && !cur.cancelled && st != raids::State::Finished) {
        UIContext::ButtonStyle cancel = plain;
        cancel.danger = true;
        if (ui.button("cancel", {x, by2, w, ui.s(44)}, "Annuler l'événement…", cancel)) {
            reason_.clear();
            confirmError_.clear();
            confirmKind_ = Confirm::Cancel;
            confirm_ = true;
        }
    }
}

void AdminRaids::drawList(const Rect& r) {
    UIContext& ui = g_.ui;
    const float top = section(ui, r, "ÉVÉNEMENTS MONDIAUX", kSky);
    const Rect list{r.x + ui.s(14), top, r.w - ui.s(20), r.bottom() - ui.s(12) - top};
    if (events_.empty()) {
        fit(ui, loaded_ ? "Aucun événement mondial pour l'instant : programmez-en un à gauche." : "Chargement…", {list.x, list.y + ui.s(14)}, 15, list.w - ui.s(10), kDim);
        return;
    }
    const int64_t now = nowUtc(skew_);
    const float rowH = ui.s(52), gap = ui.s(6);
    ui.beginScroll("events", list, static_cast<float>(events_.size()) * (rowH + gap));
    float ry = list.y - ui.scrollOffset();
    for (const Json& j : events_) {
        const raids::Event e = raids::parseEvent(j);
        const Rect row{list.x, ry, list.w - ui.s(14), rowH};
        ry += rowH + gap;
        if (!e.valid || row.bottom() < list.y || row.y > list.bottom()) continue;
        bool hover = false;
        if (ui.hotspot("ev/" + e.id, row, &hover)) select(j);
        const bool sel = e.id == selected_;
        ui.r().fillRoundedRect(row, ui.s(9), sel ? Color{58, 28, 34, 245} : hover ? Color{34, 44, 92, 245} : kCard);
        if (sel) ui.r().drawRoundedRect(row, ui.s(9), kOrange, ui.s(1.5f));
        const raids::State st = raids::stateAt(e, now);
        const std::string sl = raids::stateLabel(st);
        const float pw = textW(ui, sl, 13, FontFace::Bold) + ui.s(22);
        pill(ui, row.right() - ui.s(10) - pw, row.center().y, sl, stateFill(st), stateText(st), row.right(), 13);
        const std::string count = plural(e.clans, "clan", "clans") + " · " + plural(e.players, "joueur", "joueurs");
        const float cw = std::min(textW(ui, count, 13), (row.w - pw) * 0.35f);
        fit(ui, count, {row.right() - ui.s(22) - pw, row.center().y}, 13, cw, kDim, FontFace::Body, Align::Right);
        fit(ui, whenShort(e.startsAt) + " · " + raids::formatLabel(e.format), {row.x + ui.s(12), row.center().y}, 15,
            row.w - ui.s(46) - pw - cw, kText, FontFace::Bold);
    }
    ui.endScroll();
}

void AdminRaids::drawDetail(const Rect& r) {
    UIContext& ui = g_.ui;
    raids::Event e = raids::parseEvent(at(detail_, "event"));
    const std::string title = e.valid && e.id == selected_ ? "INSCRIPTIONS · " + whenShort(e.startsAt) : "INSCRIPTIONS";
    const float top = section(ui, r, title, kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    const auto note = wrap(ui, "Les chiffres du combat (joueurs en jeu, instances, gigaboss, reconnexions, rapports refusés, journal) sont dans l'onglet « En direct ».",
                           FontFace::Body, 13, w, 3);
    const float noteH = static_cast<float>(note.size()) * ui.s(18);
    drawLines(ui, note, {x, r.bottom() - ui.s(12) - noteH}, 13, ui.s(18), kDim);
    if (!e.valid || e.id != selected_) {
        fit(ui, "Choisissez un événement pour voir ses inscriptions.", {x, top + ui.s(14)}, 15, w, kDim);
        return;
    }
    // Three figures, then the clans.
    const float tw = (w - 2 * ui.s(10)) / 3.0f, th = ui.s(70);
    const std::string vals[3] = {std::to_string(e.clans), std::to_string(e.players), e.capacity > 0 ? std::to_string(e.capacity) : std::string("illimitées")};
    const char* caps[3] = {"Clans inscrits", "Joueurs inscrits", "Places"};
    for (int i = 0; i < 3; ++i) {
        const Rect t{x + static_cast<float>(i) * (tw + ui.s(10)), top, tw, th};
        ui.r().fillRoundedRect(t, ui.s(10), Color{13, 18, 38, 255});
        fit(ui, caps[i], {t.x + ui.s(10), t.y + ui.s(18)}, 12, t.w - ui.s(20), kDim);
        fit(ui, vals[i], {t.x + ui.s(10), t.y + ui.s(46)}, 20, t.w - ui.s(20), kText, FontFace::Title);
    }
    const Rect list{x, top + th + ui.s(12), w + ui.s(6), r.bottom() - ui.s(18) - noteH - (top + th + ui.s(12))};
    const Json& cl = at(detail_, "clans");
    if (!cl.is_array() || cl.empty()) {
        fit(ui, "Aucun clan inscrit pour l'instant.", {x, list.y + ui.s(14)}, 15, w, kDim);
        return;
    }
    const float rowH = ui.s(30);
    ui.beginScroll("clans", list, static_cast<float>(cl.size()) * rowH);
    float ry = list.y - ui.scrollOffset();
    for (const Json& c : cl) {
        const float cy = ry + rowH * 0.5f;
        ry += rowH;
        if (cy < list.y || cy > list.bottom()) continue;
        const std::string n = plural(jget<int>(c, "players", 0), "inscrit", "inscrits");
        const float nw = fit(ui, n, {x + w - ui.s(8), cy}, 14, w * 0.25f, kGold, FontFace::Bold, Align::Right).x;
        const std::string leader = jstr(c, "leader");
        const float lw = leader.empty() ? 0.0f : std::min(textW(ui, "chef : " + leader, 13), w * 0.3f);
        if (!leader.empty()) fit(ui, "chef : " + leader, {x + w - ui.s(20) - nw, cy}, 13, lw, kDim, FontFace::Body, Align::Right);
        fit(ui, clans::tagged(jstr(c, "name"), jstr(c, "tag")), {x, cy}, 14, w - nw - lw - ui.s(36), kText, FontFace::Body, Align::Left, true);
    }
    ui.endScroll();
}

void AdminRaids::drawModal() {
    // The confirmation of an action on the selected event: cancel it, suspend it (2.69), or open a
    // catch-up session after it (2.69). Each one goes to the Journal; the server checks the role.
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
    const float w = std::min(ui.screenW() - ui.s(40), ui.s(680)), tw = w - ui.s(60);
    const bool cancelKind = confirmKind_ == Confirm::Cancel, suspendKind = confirmKind_ == Confirm::Suspend;
    const std::string title = cancelKind ? "Annuler l'événement" : suspendKind ? "Suspendre l'événement" : "Session de rattrapage";
    const std::string text =
        cancelKind ? "Annuler cet événement mondial ? Les clans et les joueurs inscrits sont prévenus ; le Journal garde la trace."
        : suspendKind
            ? "Suspendre cet événement ? Plus personne n'entre en jeu et aucun rapport n'est accepté jusqu'à la reprise ; le temps suspendu est rendu à "
              "l'événement. Les inscrits sont prévenus ; le Journal garde la trace."
            : "Ouvrir une session de rattrapage ? Les joueurs absents découvrent le raid avec ses propres instances et gagnent des versions argentées ; le "
              "résultat officiel de l'événement ne change jamais. Tous les clans sont prévenus ; le Journal garde la trace.";
    const auto intro = wrap(ui, text, FontFace::Body, 16, tw, 5);
    const float introH = static_cast<float>(intro.size()) * ui.s(22);
    const float h = ui.s(80) + introH + ui.s(16) + ui.s(76) + ui.s(34) + ui.s(76);
    const Rect box{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), confirmKind_ == Confirm::Catchup ? kSky : ui.theme().danger, ui.s(1.5f));
    ui.pushId("admin/raids/modal");
    const float x = box.x + ui.s(30);
    fit(ui, title, {x, box.y + ui.s(40)}, 26, tw, kText, FontFace::Title);
    drawLines(ui, intro, {x, box.y + ui.s(70)}, 16, ui.s(22), ui.theme().textDim);
    float y = box.y + ui.s(80) + introH + ui.s(16);
    if (confirmKind_ == Confirm::Catchup) {
        std::vector<std::string> days;
        for (int d : kCatchupDays) days.push_back(std::to_string(d) + (d > 1 ? " jours" : " jour"));
        ui.cycle("days", {x, y + ui.s(16), tw, ui.s(48)}, "Durée de la session", catchupIdx_, days);
        catchupIdx_ = std::clamp(catchupIdx_, 0, 4);
    } else {
        fit(ui, "Motif (facultatif, gardé au Journal)", {x, y + ui.s(10)}, 15, tw, ui.theme().textDim);
        ui.textField("reason", {x, y + ui.s(24), tw, ui.s(44)}, reason_, cancelKind ? "Mise à jour du jeu, erreur d'horaire…" : "Désynchronisation, triche signalée…",
                     200);
    }
    y += ui.s(76);
    fit(ui, confirmError_, {x, y + ui.s(12)}, 15, tw, ui.theme().danger);
    const float by = box.bottom() - ui.s(66);
    UIContext::ButtonStyle ok;
    ok.danger = confirmKind_ != Confirm::Catchup;
    ok.primary = confirmKind_ == Confirm::Catchup;
    ok.enabled = !busy_;
    const std::string okLabel = cancelKind ? "Annuler l'événement" : suspendKind ? "Suspendre" : "Ouvrir la session";
    const float okW = std::min(textW(ui, okLabel, 17, FontFace::Bold) + ui.s(44), tw * 0.55f);
    const float cW = std::min(textW(ui, "Garder", 17, FontFace::Bold) + ui.s(44), tw * 0.4f);
    const bool go = ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, okLabel, ok);
    const bool keep = ui.button("keep", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Garder") || (ui.backPressed() && !ui.editingText());
    ui.popId();
    if (keep) {
        confirm_ = false;
        return;
    }
    if (!go || busy_) return;
    Json body = Json::object();
    if (confirmKind_ != Confirm::Catchup && !clans::cleanName(reason_).empty()) body["reason"] = clans::cleanName(reason_);
    if (suspendKind) return action("/suspend", body, "Événement suspendu : les inscrits sont prévenus, rien ne compte jusqu'à la reprise (inscrit au Journal).");
    if (confirmKind_ == Confirm::Catchup) {
        body["days"] = kCatchupDays[catchupIdx_];
        return action("/catchup", body, "Session de rattrapage ouverte : tous les clans sont prévenus (inscrit au Journal).");
    }
    busy_ = true;
    g_.online.send("DELETE", "/api/v1/admin/raids/" + selected_, body, OnlineService::guard(life_, [this](const ApiResult& r) {
                       busy_ = false;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           confirmError_ = r.message;
                           return;
                       }
                       confirm_ = false;
                       say_("Événement annulé : les inscrits sont prévenus (inscrit au Journal).", false);
                       load();
                   }));
}

namespace {

/// 2.77, captures for the documentation (headless only, docs/BUILDING.md): the settings of « Simuler un
/// raid complet » as ADMINISTRATION › Raids › Tester Asterion shows them (no account: nothing is launched).
class RaidSimMenuScene : public Scene {
public:
    explicit RaidSimMenuScene(GameServices& g)
        : g_(g), raids_(g, [](const std::string&, bool) {}, [](const ApiResult&) { return false; }) {}
    void onEnter() override { bg_.init(g_); }
    void update(float dt) override { bg_.update(dt); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        const float w = std::min(ui.s(760), ui.screenW() - ui.s(40)), h = std::min(ui.s(600), ui.screenH() - ui.s(40));
        const Rect r{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
        const float top = section(ui, r, "SIMULER UN RAID COMPLET (TESTER ASTERION)", kGold);
        raids_.drawSim({r.x, top, r.w, r.bottom() - ui.s(12) - top}, true);
        const bool back = ui.backPressed();
        ui.end();
        if (back) g_.app.popScene();
    }

private:
    GameServices& g_;
    MenuBackground bg_;
    AdminRaids raids_;
};

}  // namespace

std::unique_ptr<Scene> makeRaidSimMenu(GameServices& g) { return std::make_unique<RaidSimMenuScene>(g); }

}  // namespace cosmic
