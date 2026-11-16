// ADMINISTRATION (2.40): the screen of the accounts with the administrator role. It only exists
// for them (the main menu shows no entry otherwise, and the server answers like an unknown
// route to any other account, on every request). Three tabs:
//   « Joueurs »: search by username, filter (all, admins, online), give or remove the role
//                after a confirmation (never one's own, never the last admin: refused by the
//                server), the admins always listed below;
//   « Calendrier des défis » (2.41, AdminCalendar.cpp): the challenges of a month, the coming
//                ones too; the challenge of a period that has not begun is chosen here (level,
//                rules, objective and tiers) or put back to the server's draw;
//   « Journal »: who did what and when, the server's refusals included; no IP, no e-mail;
//   « Campagne » (2.45, AdminCampaign.cpp): the campaign levels and their revision online,
//                edited in the game's editor and published without a game update;
//   « Créations » (AdminCreations.cpp): the suits an administrator makes (a name, three
//                colours, a live preview), given as rewards of the challenges.
// The country, the last activity and the presence of a player appear only as they share them.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/AdminBackups.h"
#include "ui/screens/AdminCampaign.h"
#include "ui/screens/AdminCalendar.h"
#include "ui/screens/AdminCreations.h"
#include "ui/screens/AdminEnemies.h"
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

std::tm localTmOf(const std::string& iso, bool* ok) {
    std::tm tmv{};
    auto t = parseIsoUtc(iso);
    *ok = t.has_value();
    if (!t) return tmv;
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    return tmv;
}

/// "02/10/2026" (local date of this computer).
std::string dayText(const std::string& iso) {
    bool ok = false;
    std::tm t = localTmOf(iso, &ok);
    if (!ok) return "—";
    char buf[24];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return buf;
}

/// "16/11 · 02:14" (local time), with the year when it is not this one.
std::string stampText(const std::string& iso) {
    bool ok = false;
    std::tm t = localTmOf(iso, &ok);
    if (!ok) return "—";
    std::time_t now = std::time(nullptr);
    std::tm n{};
#ifdef _WIN32
    localtime_s(&n, &now);
#else
    localtime_r(&now, &n);
#endif
    char buf[40];
    if (t.tm_year == n.tm_year)
        std::snprintf(buf, sizeof buf, "%02d/%02d · %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
    else
        std::snprintf(buf, sizeof buf, "%02d/%02d/%04d · %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
    return buf;
}

Color presenceColorOf(const std::string& p) {
    if (p == "online") return {120, 230, 140, 255};
    if (p == "away") return {255, 179, 71, 255};
    return {86, 100, 126, 255};
}

std::string presenceText(const std::string& p) {
    if (p == "online") return "en ligne";
    if (p == "away") return "absent";
    if (p == "hidden") return "masquée";
    return "hors ligne";
}

const char* kFilters[] = {"all", "admins", "online"};
const char* kKinds[] = {"all", "role", "challenge", "campaign", "creation", "enemy", "refused"};

/// The tabs: Joueurs, Calendrier des défis (2.41), Journal, Sauvegardes, Campagne (2.44), Créations (2.45), Ennemis (2.46).
constexpr int kTabPlayers = 0, kTabCalendar = 1, kTabLog = 2, kTabBackups = 3, kTabCampaign = 4, kTabCreations = 5, kTabEnemies = 6;

/// "20/11" of "2026-11-20".
std::string shortDate(const std::string& date) { return date.size() >= 10 ? date.substr(8, 2) + "/" + date.substr(5, 2) : date; }

/// "1:45" (whole seconds), else "1:45.250".
std::string tierTimeText(int64_t ms) {
    if (ms <= 0) return "—";
    if (ms % 1000 != 0) return formatRunTime(ms);
    int64_t s = ms / 1000;
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", static_cast<int>(s / 3600), static_cast<int>((s / 60) % 60), static_cast<int>(s % 60));
    else std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
    return b;
}

class AdminScene : public Scene {
public:
    AdminScene(GameServices& g, std::string open)
        : g_(g), ui_(g), calendar_(g, ui_, [this](const std::string& m, bool e) { say(m, e); }, [this](const ApiResult& r) { return lostRole(r); }),
          backups_(g, [this](const std::string& m, bool e) { say(m, e); }, [this](const ApiResult& r) { return lostRole(r); }),
          campaign_(g, [this](const std::string& m, bool e) { say(m, e); }, [this](const ApiResult& r) { return lostRole(r); }),
          creations_(g, [this](const std::string& m, bool e) { say(m, e); }, [this](const ApiResult& r) { return lostRole(r); }),
          enemies_(g, [this](const std::string& m, bool e) { say(m, e); }, [this](const ApiResult& r) { return lostRole(r); }) {
        if (open.rfind("backups", 0) == 0) tab_ = kTabBackups;
        // "creations[:new]" (the editor opened on a new suit).
        if (open.rfind("creations", 0) == 0) {
            tab_ = kTabCreations;
            if (open.size() > 10) creations_.open(open.substr(10));
        }
        if (open.rfind("enemies", 0) == 0) tab_ = kTabEnemies;  // 2.46
        // 2.45: "campaign[:<level id>]" (that level's history open).
        if (open.rfind("campaign", 0) == 0) {
            tab_ = kTabCampaign;
            if (open.size() > 9) campaignSelect_ = open.substr(9);
        }
        // Automated screenshots: "players[:<search>[:confirm]]", "calendar[:YYYY-MM[:<id>[:objectif]]]" or "log".
        if (open.rfind("log", 0) == 0) tab_ = kTabLog;
        if (open.rfind("calendar", 0) == 0) {
            tab_ = kTabCalendar;
            if (open.size() > 9) calendar_.open(open.substr(9));
        }
        if (open.rfind("players:", 0) == 0) {
            std::string rest = open.substr(8);
            size_t c = rest.find(':');
            search_ = rest.substr(0, c);
            autoConfirm_ = c != std::string::npos && rest.substr(c + 1) == "confirm";
        }
    }

    void onEnter() override {
        g_.ui.setFocus("admin/back");
        if (!g_.online.loggedIn()) return leave();
        if (g_.online.profile().is_null()) g_.online.refreshProfile();
        reloadAll();
    }
    void onResume() override { reloadAll(); }

    void update(float dt) override {
        bg_.update(dt);
        // The profile known, without the role: nothing to show (the server would refuse anyway).
        if (g_.online.profile().is_object() && !g_.online.isAdmin()) return leave();
        if (searchDirty_ && g_.ui.time() - searchT_ > 0.35f) {
            searchDirty_ = false;
            offset_ = 0;
            loadPlayers();
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        const bool modal = !confirmId_.empty() || calendar_.modal() || backups_.modal() || campaign_.modal();
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top && !modal;
        // 2.41: wider (the calendar's eight columns and its editor side by side).
        W_ = std::min(ui.screenW() - ui.s(96), ui.s(1760));
        x0_ = (ui.screenW() - W_) * 0.5f;
        float y = ui.s(36), hh = ui.s(52);
        ui.pushId("admin");
        float titleW = ui.text().measure("ADMINISTRATION", FontFace::Title, ui.s(40)).x;
        ui.label("ADMINISTRATION", {x0_, y + hh * 0.5f}, 40, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        float backW = ui.s(170);
        bool back = ui.button("back", {x0_ + W_ - backW, y, backW, hh}, "Retour") || (!modal && ui.backPressed());
        // The tabs after the title when they fit, else on the line below. Equal tabs, each as
        // wide as its longest label needs (« Calendrier des défis »).
        static const std::vector<std::string> kTabs = {"Joueurs", "Calendrier des défis", "Journal", "Sauvegardes", "Campagne", "Créations", "Ennemis"};
        float tabW = 0.0f;
        for (const std::string& t : kTabs) tabW = std::max(tabW, ui.text().measure(t, FontFace::Bold, ui.s(20)).x + ui.s(44));
        float tabsW = tabW * static_cast<float>(kTabs.size()), tabsX = x0_ + titleW + ui.s(28);
        float bodyY = y + hh + ui.s(22);
        Rect tabsR{tabsX, y, tabsW, hh};
        if (tabsX + tabsW > x0_ + W_ - backW - ui.s(20)) {
            // On the line below, never wider than the screen's column (the labels then shrink to their tab).
            tabW = std::min(tabW, W_ / static_cast<float>(kTabs.size()));
            tabsW = tabW * static_cast<float>(kTabs.size());
            tabsR = {x0_, bodyY, tabsW, hh};
            bodyY += hh + ui.s(16);
        }
        int tab = tab_;
        ui.tabs("tabs", tabsR, kTabs, tab);
        // « NOUVEAU » (2.41) on « Calendrier des défis » until it is opened.
        Rect calTab{tabsR.x + tabW + ui.s(2), tabsR.y, tabW - ui.s(4), tabsR.h};
        ui.newBadge("tabcal", calTab, "admin.tab.calendar", true, true);
        Rect bakTab{tabsR.x + 3 * tabW + ui.s(2), tabsR.y, tabW - ui.s(4), tabsR.h};
        ui.newBadge("tabbak", bakTab, "admin.tab.backups", true, true);
        Rect campTab{tabsR.x + 4 * tabW + ui.s(2), tabsR.y, tabW - ui.s(4), tabsR.h};
        ui.newBadge("tabcamp", campTab, "admin.tab.campaign", true, true);
        Rect creaTab{tabsR.x + 5 * tabW + ui.s(2), tabsR.y, tabW - ui.s(4), tabsR.h};
        ui.newBadge("tabcrea", creaTab, "admin.tab.creations", true, true);
        Rect enTab{tabsR.x + kTabEnemies * tabW + ui.s(2), tabsR.y, tabW - ui.s(4), tabsR.h};
        ui.newBadge("tabenemies", enTab, "admin.tab.enemies", true, true);
        if (tab != tab_) {
            tab_ = tab;
            if (tab_ == kTabLog) loadLog(false);
            if (tab_ == kTabBackups) {
                if (ui.featureSeen && ui.isNew("admin.tab.backups")) ui.featureSeen("admin.tab.backups");
                backups_.enter();
            }
            if (tab_ == kTabCalendar) {
                if (ui.featureSeen && ui.isNew("admin.tab.calendar")) ui.featureSeen("admin.tab.calendar");
                calendar_.enter();
            }
            if (tab_ == kTabCampaign) {
                if (ui.featureSeen && ui.isNew("admin.tab.campaign")) ui.featureSeen("admin.tab.campaign");
                campaign_.enter();
            }
            if (tab_ == kTabCreations) {
                if (ui.featureSeen && ui.isNew("admin.tab.creations")) ui.featureSeen("admin.tab.creations");
                creations_.enter();
            }
            if (tab_ == kTabEnemies) {
                if (ui.featureSeen && ui.isNew("admin.tab.enemies")) ui.featureSeen("admin.tab.enemies");
                enemies_.enter();
            }
        }
        Rect body{x0_, bodyY, W_, ui.screenH() - bodyY - ui.s(64)};
        if (tab_ == kTabCalendar) {
            // Its own two panels: the month and the editor.
            calendar_.draw(body);
        } else {
            ui.r().fillRoundedRect(body, ui.s(14), Color{10, 15, 32, 225});
            ui.r().drawRoundedRect(body, ui.s(14), ui.theme().border, ui.s(1));
            Rect inner{body.x + ui.s(22), body.y + ui.s(18), body.w - ui.s(44), body.h - ui.s(36)};
            if (tab_ == kTabPlayers) drawPlayers(inner);
            else if (tab_ == kTabBackups) backups_.draw(inner);
            else if (tab_ == kTabCampaign) campaign_.draw(inner);
            else if (tab_ == kTabCreations) creations_.draw(inner);
            else if (tab_ == kTabEnemies) enemies_.draw(inner);
            else drawLog(inner);
        }
        drawStatus();
        ui.popId();
        ui.inputEnabled = top;
        if (!confirmId_.empty()) drawConfirm();
        else if (calendar_.modal()) calendar_.drawModal();
        else if (backups_.modal()) backups_.drawModal();
        else if (campaign_.modal()) campaign_.drawModal();
        ui.end();
        if (back) g_.app.switchScene(makeMainMenu(g_), 0.3f);
    }

private:
    void leave() {
        if (left_) return;
        left_ = true;
        g_.app.switchScene(makeMainMenu(g_), 0.2f);
    }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }
    /// The server answered « unknown route »: this account is no longer admin.
    bool lostRole(const ApiResult& r) {
        if (r.status != 404 || r.code != "NOT_FOUND") return false;
        g_.online.refreshProfile();
        leave();
        return true;
    }

    void reloadAll() {
        loadPlayers();
        loadAdmins();
        if (tab_ == kTabLog) loadLog(false);
        if (tab_ == kTabCalendar) calendar_.enter(true);
        if (tab_ == kTabBackups) backups_.enter(true);
        if (tab_ == kTabCampaign) {
            campaign_.enter(true, campaignSelect_);
            campaignSelect_.clear();
        }
        if (tab_ == kTabCreations) creations_.enter(true);
        if (tab_ == kTabEnemies) enemies_.enter(true);
    }

    void loadPlayers() {
        std::string path = std::string("/api/v1/admin/players?limit=50&filter=") + kFilters[filter_] + "&offset=" + std::to_string(offset_);
        if (search_.size() >= 2) path += "&q=" + search_;
        int gen = ++playersGen_;
        busy_ = true;
        g_.online.get(path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != playersGen_) return;
                          busy_ = false;
                          if (lostRole(r)) return;
                          if (!r.ok) return say(r.message, true);
                          players_ = r.data;
                          loadedPlayers_ = true;
                          if (autoConfirm_) {
                              autoConfirm_ = false;
                              for (const auto& p : players_.value("players", Json::array()))
                                  if (jstr(p, "id") != g_.online.playerId()) {
                                      askRole(p);
                                      break;
                                  }
                          }
                      }),
                      false);
    }

    void loadAdmins() {
        g_.online.get("/api/v1/admin/players?limit=100&filter=admins", OnlineService::guard(life_, [this](const ApiResult& r) {
                          if (lostRole(r)) return;
                          if (r.ok) admins_ = r.data.value("players", Json::array());
                      }),
                      false);
    }

    void loadLog(bool more) {
        std::string path = std::string("/api/v1/admin/log?limit=50&kind=") + kKinds[kind_];
        if (actor_ > 0 && actor_ - 1 < static_cast<int>(admins_.size())) path += "&actor=" + jstr(admins_[actor_ - 1], "id");
        int offset = more ? static_cast<int>(log_.size()) : 0;
        if (offset > 0) path += "&offset=" + std::to_string(offset);
        int gen = ++logGen_;
        g_.online.get(path, OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                          if (gen != logGen_) return;
                          if (lostRole(r)) return;
                          if (!r.ok) return say(r.message, true);
                          Json list = r.data.value("entries", Json::array());
                          if (!more) log_ = Json::array();
                          for (auto& e : list) log_.push_back(e);
                          logMore_ = jget<bool>(r.data, "has_more", false);
                          loadedLog_ = true;
                      }),
                      false);
    }

    /// A row of small buttons, the chosen one highlighted; returns the right edge.
    float chips(const std::string& id, float x, float y, float h, const std::vector<std::string>& labels, int& active, float maxRight = 0.0f) {
        UIContext& ui = g_.ui;
        // The row never passes `maxRight`: the labels get smaller (down to 12) until it fits.
        int font = 16;
        auto rowWidth = [&](int f) {
            float w = 0.0f;
            for (const std::string& l : labels) w += ui.text().measure(l, FontFace::Bold, ui.s(static_cast<float>(f))).x + ui.s(36) + ui.s(8);
            return w - ui.s(8);
        };
        while (maxRight > 0.0f && font > 12 && x + rowWidth(font) > maxRight) --font;
        for (size_t i = 0; i < labels.size(); ++i) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = static_cast<int>(i) == active;
            st.fontSize = font;
            float w = ui.text().measure(labels[i], FontFace::Bold, ui.s(static_cast<float>(font))).x + ui.s(36);
            if (ui.button(id + std::to_string(i), {x, y, w, h}, labels[i], st)) active = static_cast<int>(i);
            x += w + ui.s(8);
        }
        return x - ui.s(8);
    }

    // ------------------------------------------------------------- Joueurs
    struct Columns {
        float x[8];
    };
    Columns columns(const Rect& r) const {
        static const float share[] = {0.25f, 0.13f, 0.12f, 0.15f, 0.12f, 0.11f, 0.12f};
        Columns c{};
        c.x[0] = r.x;
        for (int i = 0; i < 7; ++i) c.x[i + 1] = c.x[i] + r.w * share[i];
        return c;
    }

    void drawPlayers(const Rect& in) {
        UIContext& ui = g_.ui;
        ui.pushId("players");
        float y = in.y, fh = ui.s(46);
        std::string before = search_;
        ui.textField("search", {in.x, y, ui.s(420), fh}, search_, "Rechercher un pseudo (2 lettres au moins)", 20);
        if (search_ != before) {
            searchDirty_ = true;
            searchT_ = ui.time();
        }
        int filter = filter_;
        float chipsRight = chips("filter", in.x + ui.s(436), y, fh, {"Tous", "Admins", "En ligne"}, filter);
        if (filter != filter_) {
            filter_ = filter;
            offset_ = 0;
            loadPlayers();
        }
        {
            std::string info;
            if (loadedPlayers_) {
                int total = jget<int>(players_, "total", 0), all = jget<int>(players_, "players_total", 0), adm = jget<int>(players_, "admins_total", 0);
                info = std::to_string(total) + (total > 1 ? " joueurs trouvés" : " joueur trouvé") + " sur " + std::to_string(all) + " · " +
                       std::to_string(adm) + (adm > 1 ? " admins" : " admin");
            } else {
                info = "Chargement…";
            }
            float room = in.right() - chipsRight - ui.s(20);
            float fs = ui.fitFont(info, FontFace::Body, 16, room);
            ui.label(ui.elide(info, FontFace::Body, fs, room), {in.right(), y + fh * 0.5f}, fs, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        y += fh + ui.s(14);

        // The admins, always listed at the bottom; the results above them.
        int nAdmins = static_cast<int>(admins_.size());
        float rowH = ui.s(52), gap = ui.s(6), pagerH = ui.s(44);
        float adminsH = ui.s(36) + std::min(3, std::max(1, nAdmins)) * (rowH + gap);
        Rect results{in.x, y, in.w, in.bottom() - pagerH - ui.s(14) - adminsH - ui.s(10) - y};
        drawHeader({results.x, results.y, results.w, ui.s(26)});
        Rect list{results.x, results.y + ui.s(30), results.w, results.h - ui.s(30)};
        const Json& rows = players_.is_object() ? players_["players"] : Json();
        int n = rows.is_array() ? static_cast<int>(rows.size()) : 0;
        if (n == 0)
            ui.label(loadedPlayers_ ? "Aucun joueur ne correspond." : "Chargement…", {list.center().x, list.y + ui.s(40)}, 18, ui.theme().textDim,
                     Align::Center, FontFace::Body, VAlign::Middle);
        ui.beginScroll("list", list, n * (rowH + gap));
        for (int i = 0; i < n; ++i) drawPlayerRow(rows[i], {list.x, list.y + i * (rowH + gap) - ui.scrollOffset(), list.w - ui.s(16), rowH}, "r");
        ui.endScroll();

        // Admins
        float ay = results.bottom() + ui.s(10);
        ui.label("Admins", {in.x, ay + ui.s(14)}, 19, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        float aw = ui.text().measure("Admins", FontFace::Bold, ui.s(19)).x;
        ui.label("toujours affichés ici, recherche ou pas", {in.x + aw + ui.s(14), ay + ui.s(14)}, 15, ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        Rect alist{in.x, ay + ui.s(36), in.w, adminsH - ui.s(36)};
        ui.beginScroll("admins", alist, nAdmins * (rowH + gap));
        for (int i = 0; i < nAdmins; ++i) drawPlayerRow(admins_[i], {alist.x, alist.y + i * (rowH + gap) - ui.scrollOffset(), alist.w - ui.s(16), rowH}, "a");
        ui.endScroll();

        // Pager
        float py = in.bottom() - pagerH;
        float bw = ui.s(170);
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = offset_ > 0 && !busy_;
        float hintRoom = in.w - 2 * bw - ui.s(40);
        std::string hint = "Clic sur un pseudo : son profil · Entrée sur le rôle : le changer";
        float hs = ui.fitFont(hint, FontFace::Body, 15, hintRoom);
        ui.label(ui.elide(hint, FontFace::Body, hs, hintRoom), {in.x, py + pagerH * 0.5f}, hs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        if (ui.button("prev", {in.right() - 2 * bw - ui.s(10), py, bw, pagerH}, "Précédents", st)) {
            offset_ = std::max(0, offset_ - 50);
            loadPlayers();
        }
        st.enabled = jget<bool>(players_, "has_more", false) && !busy_;
        if (ui.button("next", {in.right() - bw, py, bw, pagerH}, "Suivants", st)) {
            offset_ += 50;
            loadPlayers();
        }
        ui.popId();
    }

    void drawHeader(const Rect& r) {
        UIContext& ui = g_.ui;
        Columns c = columns(r);
        static const char* titles[] = {"Joueur", "Pays", "Inscrit le", "Dernière activité", "Présence", "Créations", "Rôle"};
        for (int i = 0; i < 7; ++i) {
            float room = c.x[i + 1] - c.x[i] - ui.s(14);
            ui.label(ui.elide(titles[i], FontFace::Bold, 14, room), {c.x[i] + ui.s(12), r.y + r.h * 0.5f}, 14, ui.theme().textDim, Align::Left,
                     FontFace::Bold, VAlign::Middle);
        }
    }

    void cell(const Columns& c, int col, const Rect& row, const std::string& t, Color color, FontFace face = FontFace::Body) {
        UIContext& ui = g_.ui;
        float room = c.x[col + 1] - c.x[col] - ui.s(16);
        ui.label(ui.elide(t, face, 16, room), {c.x[col] + ui.s(12), row.y + row.h * 0.5f}, 16, color, Align::Left, face, VAlign::Middle);
    }

    void drawPlayerRow(const Json& p, const Rect& row, const std::string& prefix) {
        UIContext& ui = g_.ui;
        std::string id = jstr(p, "id");
        bool me = id == g_.online.playerId();
        bool admin = jstr(p, "role") == "admin";
        Color bgc = admin ? Color{30, 26, 22, 235} : Color{18, 26, 50, 235};
        ui.r().fillRoundedRect(row, ui.s(8), bgc);
        Columns c = columns(row);
        // Player: avatar, name (opens the profile), « vous ».
        float av = row.h - ui.s(14);
        Rect ar{row.x + ui.s(10), row.y + ui.s(7), av, av};
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), id, ar);
        std::string pres = jstr(p, "presence");
        if (pres == "online" || pres == "away") {
            float rad = std::max(ui.s(5), av * 0.14f);
            Vec2 pc{ar.right() - rad * 0.6f, ar.bottom() - rad * 0.6f};
            ui.r().fillCircle(pc, rad + ui.s(2), Color{10, 14, 30, 255});
            ui.r().fillCircle(pc, rad, presenceColorOf(pres));
        }
        float nx = ar.right() + ui.s(12);
        float pillW = me ? ui.text().measure("vous", FontFace::Bold, ui.s(13)).x + ui.s(18) : 0.0f;
        float room = c.x[1] - nx - ui.s(10) - (me ? pillW + ui.s(8) : 0.0f);
        std::string name = ui.elide(jstr(p, "username"), FontFace::Bold, 17, room);
        float nw = ui.text().measure(name, FontFace::Bold, ui.s(17)).x;
        Rect nameR{nx, row.y, nw, row.h};
        bool hov = false;
        if (ui.mouseClick("name" + prefix + id, nameR, &hov)) g_.app.pushScene(makePlayerProfile(g_, id));
        ui.label(name, {nx, row.y + row.h * 0.5f}, 17, hov ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        if (me) {
            Rect pr{nx + nw + ui.s(8), row.y + row.h * 0.5f - ui.s(11), pillW, ui.s(22)};
            ui.r().fillRoundedRect(pr, ui.s(11), ui.theme().warm.withAlphaF(0.16f));
            ui.r().drawRoundedRect(pr, ui.s(11), ui.theme().warm, ui.s(1));
            ui.label("vous", {pr.center().x, pr.center().y}, 13, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        std::string country = jstr(p, "country_code");
        cell(c, 1, row, country.empty() ? "masqué" : ui_.countryName(country), country.empty() ? ui.theme().textDim : ui.theme().text);
        cell(c, 2, row, dayText(jstr(p, "created_at")), ui.theme().text);
        std::string seen = jstr(p, "last_seen_at");
        cell(c, 3, row, seen.empty() ? "masquée" : OnlineUi::ageOf(seen), seen.empty() ? ui.theme().textDim : ui.theme().text);
        {
            float dx = c.x[4] + ui.s(12);
            ui.r().fillCircle({dx + ui.s(5), row.y + row.h * 0.5f}, ui.s(5), presenceColorOf(pres));
            float r2 = c.x[5] - dx - ui.s(30);
            ui.label(ui.elide(presenceText(pres), FontFace::Body, 16, r2), {dx + ui.s(16), row.y + row.h * 0.5f}, 16,
                     pres == "hidden" ? ui.theme().textDim : ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        }
        int made = jget<int>(p, "creations", 0);
        cell(c, 5, row, made == 0 ? "—" : std::to_string(made) + (made > 1 ? " niveaux" : " niveau"), made == 0 ? ui.theme().textDim : ui.theme().text);
        // Role: a button (never one's own).
        Rect rb{c.x[6] + ui.s(8), row.y + ui.s(8), std::min(ui.s(130), row.right() - c.x[6] - ui.s(16)), row.h - ui.s(16)};
        if (me) {
            ui.label("admin", {rb.x + ui.s(4), row.y + row.h * 0.5f}, 16, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
        } else {
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = admin;
            st.fontSize = 15;
            st.enabled = !busyRole_;
            if (ui.button("role" + prefix + id, rb, admin ? "admin" : "joueur", st)) askRole(p);
        }
    }

    void askRole(const Json& p) {
        confirmId_ = jstr(p, "id");
        confirmPlayer_ = p;
        g_.ui.setFocus("confirm/cancel");
    }

    void drawConfirm() {
        UIContext& ui = g_.ui;
        bool grant = jstr(confirmPlayer_, "role") != "admin";
        std::string name = jstr(confirmPlayer_, "username");
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 185});
        Rect box{ui.screenW() * 0.5f - ui.s(340), ui.screenH() * 0.5f - ui.s(190), ui.s(680), ui.s(380)};
        Color edge = grant ? ui.theme().warm : ui.theme().danger;
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), edge.withAlphaF(0.8f), ui.s(1.5f));
        ui.pushId("confirm");
        float x = box.x + ui.s(32), w = box.w - ui.s(64), y = box.y + ui.s(30);
        std::string title = grant ? "Donner le rôle admin ?" : "Retirer le rôle admin ?";
        float ts = ui.fitFont(title, FontFace::Title, 26, w);
        ui.label(title, {x, y + ui.s(16)}, ts, edge, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(52);
        Rect ar{x, y, ui.s(52), ui.s(52)};
        ui_.avatarsOpenProfile = false;
        ui_.drawAvatar(jstr(confirmPlayer_, "avatar", "avatar_01"), confirmId_, ar);
        ui_.avatarsOpenProfile = true;
        ui.label(ui.elide(name, FontFace::Bold, 22, w - ui.s(70)), {ar.right() + ui.s(14), y + ui.s(16)}, 22, ui.theme().text, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        std::string country = jstr(confirmPlayer_, "country_code");
        std::string sub = (country.empty() ? std::string("pays masqué") : ui_.countryName(country)) + " · inscrit le " + dayText(jstr(confirmPlayer_, "created_at"));
        ui.label(ui.elide(sub, FontFace::Body, 16, w - ui.s(70)), {ar.right() + ui.s(14), y + ui.s(40)}, 16, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        y += ui.s(74);
        std::string text = grant ? name + " pourra ouvrir l'écran Administration : nommer d'autres admins ou leur retirer le rôle, et lire le journal."
                                 : name + " n'aura plus accès à l'écran Administration. Il doit toujours rester au moins un admin.";
        Vec2 used = ui.labelWrapped(text, {x, y}, w, 17, ui.theme().text);
        y += used.y + ui.s(12);
        ui.labelWrapped("L'action est inscrite au journal avec votre pseudo.", {x, y}, w, 15, ui.theme().textDim);
        float bh = ui.s(48), by = box.bottom() - bh - ui.s(26);
        UIContext::ButtonStyle ok;
        ok.primary = grant;
        ok.danger = !grant;
        if (ui.button("cancel", {box.right() - ui.s(32) - ui.s(170) - ui.s(14) - ui.s(230), by, ui.s(170), bh}, "Annuler") || ui.backPressed()) {
            confirmId_.clear();
        } else if (ui.button("ok", {box.right() - ui.s(32) - ui.s(230), by, ui.s(230), bh}, grant ? "Donner le rôle" : "Retirer le rôle", ok)) {
            setRole(confirmId_, grant, name);
            confirmId_.clear();
        }
        ui.popId();
    }

    void setRole(const std::string& id, bool admin, const std::string& name) {
        busyRole_ = true;
        g_.online.send("POST", "/api/v1/admin/players/" + id + "/role", Json{{"admin", admin}},
                       OnlineService::guard(life_, [this, admin, name](const ApiResult& r) {
                           busyRole_ = false;
                           if (lostRole(r)) return;
                           if (!r.ok) say(r.message, true);
                           else say(admin ? name + " est maintenant admin." : name + " n'est plus admin.", false);
                           reloadAll();
                       }));
    }

    // ------------------------------------------------------------- Journal
    /// A Journal entry as shown: its pill (kind) and its sentence.
    struct LogLine {
        std::string pill;
        Color color;
        std::string text;
    };

    /// "du jour du 20/11", "de la semaine 48", "du mois de décembre 2026".
    static std::string periodPhrase(const Json& ch) {
        std::string kind = jstr(ch, "kind"), period = jstr(ch, "period");
        if (kind == "weekly") return "de la semaine " + std::to_string(period.size() > 6 ? std::atoi(period.substr(6).c_str()) : 0);
        if (kind == "monthly") return "du mois de " + challenge_ui::frenchMonth(period);
        return "du jour du " + shortDate(jstr(ch, "start", period));
    }

    /// "Les Plaines de Givre · 3 cœurs · sans checkpoint · Argent 1:45 · Or 1:20 sans dégât".
    std::string challengeSummary(const Json& ch) {
        const Json& lv = jfield(ch, "level", Json());
        std::string title = jstr(lv, "title");
        if (jstr(lv, "source") == "campaign")
            if (const LevelEntry* le = g_.content.level(jstr(lv, "id"))) title = le->name;
        std::string out = title.empty() ? std::string("?") : title;
        const Json& rules = jfield(ch, "rules", Json());
        if (rules.is_object()) {
            int hearts = jget<int>(rules, "hearts", 0);
            out += " · " + std::to_string(hearts) + (hearts > 1 ? " cœurs" : " cœur");
            if (!jget<bool>(rules, "checkpoints", true)) out += " · sans checkpoint";
            if (!jget<bool>(rules, "stations", true)) out += " · sans station";
            std::vector<std::string> powers;
            for (const Json& p : jfield(rules, "powers", Json::array()))
                if (p.is_string()) powers.push_back(p.get<std::string>());
            if (powers.size() == 1) out += " · " + challenge_ui::powerList(g_, powers) + " imposé";
            else if (powers.size() > 1) out += " · " + challenge_ui::powerList(g_, powers) + " imposés";
        }
        const Json& tiers = jfield(ch, "tiers", Json());
        if (tiers.is_object()) {
            bool noDamage = jget<bool>(tiers, "gold_no_damage", true);
            if (jstr(ch, "objective") == "finish")
                out += " · finir le niveau · Bronze " + std::to_string(jget<int>(tiers, "bronze_progress", 50)) + " % · Argent " +
                       std::to_string(jget<int>(tiers, "silver_progress", 80)) + " % · Or" + (noDamage ? std::string(" sans dégât") : std::string(" en finissant"));
            else
                out += " · Argent " + tierTimeText(jget<int64_t>(tiers, "silver_ms", 0)) + " · Or " + tierTimeText(jget<int64_t>(tiers, "gold_ms", 0)) +
                       (noDamage ? std::string(" sans dégât") : std::string());
        }
        return out;
    }

    /// « Or », « Argent », « Cyan », « Violet »: a title's colour (2.43).
    static std::string colorName(const std::string& c) {
        return c == "silver" ? "Argent" : c == "cyan" ? "Cyan" : c == "violet" ? "Violet" : "Or";
    }

    /// « combinaison #c8e1ff, accent #78c8ff, écharpe #ebf5ff »: the colours of a creation as the Journal keeps them.
    static std::string colorsText(const Json& creation) {
        const Json& c = jfield(creation, "colors", Json());
        if (!c.is_object()) return "couleurs inconnues";
        return "combinaison " + jstr(c, "suit", "?") + ", accent " + jstr(c, "accent", "?") + ", écharpe " + jstr(c, "scarf", "?");
    }

    /// The rewards of a challenge as the Journal keeps them: « Titre « X » (Or atteint) · Cadre Y (Top 10 final) » (2.43).
    static std::string rewardsSummary(const Json& list) {
        std::string out;
        if (!list.is_array()) return out;
        for (const Json& r : list) {
            if (!r.is_object()) continue;
            ChallengePrize p;
            p.condition = jstr(r, "condition");
            p.value = jget<int>(r, "value", 0);
            const std::string category = jstr(r, "category");
            std::string one = cosmeticCategorySingular(category == "title" || jstr(r, "kind") == "title" ? "title" : category) + " « " + jstr(r, "name") + " »";
            one += " (" + prizeConditionShort(p) + ")";
            out += (out.empty() ? "" : " · ") + one;
        }
        return out;
    }

    LogLine lineOf(const Json& e) {
        UIContext& ui = g_.ui;
        std::string action = jstr(e, "action");
        std::string target = e.contains("target") && e["target"].is_object() ? jstr(e["target"], "username", "un compte supprimé") : "";
        std::string reason = jstr(e, "reason");
        const Json& ch = jfield(e, "challenge", Json());
        if (action == "admin_role_granted")
            return {"Rôle", ui.theme().warm, jstr(e, "by") == "migration" ? "a nommé " + target + " premier admin (mise en service)" : "a donné le rôle admin à " + target};
        if (action == "admin_role_revoked") return {"Rôle", ui.theme().danger, "a retiré le rôle admin à " + target};
        if (action == "admin_challenge_set" && ch.is_object()) {
            // 2.41: « a choisi le défi du jour du 20/11 : … (remplace le tirage « … ») ».
            std::string text = "a choisi le défi " + periodPhrase(ch) + " : " + challengeSummary(ch);
            const Json& rep = jfield(ch, "replaced", Json());
            if (rep.is_object() && !jstr(rep, "title").empty())
                text += jstr(rep, "source") == "auto" ? " (remplace le tirage « " + jstr(rep, "title") + " »)" : " (remplace « " + jstr(rep, "title") + " »)";
            return {"Défi", ui.theme().accent, text};
        }
        if (action == "admin_challenge_reset" && ch.is_object()) {
            std::string text = "a remis au tirage le défi " + periodPhrase(ch);
            const Json& drawn = jfield(ch, "drawn", Json());
            if (drawn.is_object() && !jstr(drawn, "title").empty()) text += " (tiré à nouveau : « " + jstr(drawn, "title") + " »)";
            else if (!jstr(ch, "draws_on").empty()) text += " (tirage le " + shortDate(jstr(ch, "draws_on")) + ")";
            return {"Défi", Color{185, 200, 218, 255}, text};
        }
        if (action == "admin_challenge_rewards" && ch.is_object()) {
            // 2.43: « a fixé les récompenses du défi du jour du 20/11 : Titre « … » (Or atteint) · … ».
            const std::string list = rewardsSummary(jfield(ch, "rewards", Json()));
            if (list.empty()) return {"Défi", Color{185, 200, 218, 255}, "a retiré les récompenses du défi " + periodPhrase(ch)};
            return {"Défi", ui.theme().accent, "a fixé les récompenses du défi " + periodPhrase(ch) + " : " + list};
        }
        // 2.45: a native entity's revision published with the levels that use it, or refused.
        const Json& en = jfield(e, "enemy", Json());
        if (en.is_object()) {
            std::string ename = jstr(en, "enemy_name", jstr(en, "enemy_id"));
            if (const EnemyDefinition* d = g_.content.enemy(jstr(en, "enemy_id"))) ename = d->name;
            if (action == "admin_enemy_published") {
                std::string text = "a publié la révision " + std::to_string(jget<int>(en, "revision", 0)) + " de l'entité « " + ename + " »";
                const Json& levels = jfield(en, "levels", Json::array());
                if (levels.empty()) text += " (aucun niveau de la campagne ne l'utilise)";
                for (const Json& l : levels) {
                    std::string lname = jstr(l, "level_name", jstr(l, "level_id"));
                    if (const LevelEntry* le = g_.content.level(jstr(l, "level_id"))) lname = le->name;
                    const int carried = jget<int>(l, "carried_bests", 0);
                    text += " · « " + lname + " » révision " + std::to_string(jget<int>(l, "version", 0)) + " (" +
                            (jstr(l, "records") == "keep" ? "records gardés, " + std::to_string(carried) +
                                                                (carried > 1 ? " meilleurs temps reportés)" : " meilleur temps reporté)")
                                                          : std::string("records réinitialisés)"));
                }
                return {"Ennemi", ui.theme().warm, text};
            }
            std::string why = reason == "BASE_CHANGED" ? std::string("la définition ou un niveau en ligne avait changé entre-temps")
                                                       : std::string("refus du serveur");
            return {"Refus", ui.theme().textDim, "publication d'une révision de l'entité « " + ename + " » refusée : " + why};
        }
        // 2.45: a campaign revision published, made current again, or refused.
        const Json& camp = jfield(e, "campaign", Json());
        if (camp.is_object()) {
            std::string lname = jstr(camp, "level_name", jstr(camp, "level_id"));
            if (const LevelEntry* le = g_.content.level(jstr(camp, "level_id"))) lname = le->name;
            const std::string records = jstr(camp, "records") == "keep"
                                            ? "records gardés (" + std::to_string(jget<int>(camp, "carried_bests", 0)) + " meilleurs temps reportés)"
                                            : std::string("records réinitialisés");
            const std::string rev = std::to_string(jget<int>(camp, "version", 0));
            if (action == "admin_campaign_published")
                return {"Campagne", ui.theme().warm, "a publié la révision " + rev + " de « " + lname + " » (remplace la révision " +
                                                         std::to_string(jget<int>(camp, "from_version", 0)) + ") · " + records};
            if (action == "admin_campaign_reverted")
                return {"Campagne", Color{185, 200, 218, 255}, "a remis en ligne la révision " + rev + " de « " + lname + " » (à la place de la révision " +
                                                                   std::to_string(jget<int>(camp, "from_version", 0)) + ") · " + records};
            std::string why = reason == "BASE_CHANGED" ? std::string("la révision en ligne avait changé entre-temps") : std::string("refus du serveur");
            std::string what = jstr(camp, "action") == "revert" ? "remise en ligne d'une révision de « " + lname + " »" : "publication d'une révision de « " + lname + " »";
            return {"Refus", ui.theme().textDim, what + " refusée : " + why};
        }
        // A suit made in « Créations »: created, edited, archived, restored, or refused.
        const Json& crea = jfield(e, "creation", Json());
        if (crea.is_object()) {
            const std::string name = "« " + jstr(crea, "name", "?") + " »";
            if (action == "admin_creation_created") return {"Création", ui.theme().accent, "a créé la combinaison " + name + " (" + colorsText(crea) + ")"};
            if (action == "admin_creation_updated") {
                const Json& prev = jfield(crea, "previous", Json());
                std::string text = "a modifié la combinaison " + name + " (" + colorsText(crea) + ")";
                if (prev.is_object()) {
                    if (jstr(prev, "name") != jstr(crea, "name")) text += " · ancien nom « " + jstr(prev, "name") + " »";
                    if (colorsText(prev) != colorsText(crea)) text += " · anciennes couleurs " + colorsText(prev);
                }
                return {"Création", ui.theme().accent, text};
            }
            if (action == "admin_creation_archived") return {"Création", Color{185, 200, 218, 255}, "a archivé la combinaison " + name};
            if (action == "admin_creation_restored") return {"Création", ui.theme().accent, "a restauré la combinaison " + name};
            const std::string act = jstr(crea, "action");
            const std::string what = act == "create"    ? "création de la combinaison " + name
                                     : act == "update"  ? "modification de la combinaison " + name
                                     : act == "restore" ? "restauration de la combinaison " + name
                                                        : "archivage de la combinaison " + name;
            const std::string why = reason == "CREATION_EXISTS"     ? std::string("une combinaison porte déjà ce nom")
                                    : reason == "CREATION_CHANGED"  ? std::string("elle avait changé entre-temps")
                                    : reason == "CREATION_ARCHIVED" ? std::string("elle est archivée")
                                    : reason == "VALIDATION_ERROR"  ? std::string("nom ou couleurs invalides")
                                                                    : std::string("refus du serveur");
            return {"Refus", ui.theme().textDim, what + " refusée : " + why};
        }
        const Json& title = jfield(e, "title", Json());
        if (action == "admin_title_created" && title.is_object())
            return {"Défi", ui.theme().accent, "a créé le titre « " + jstr(title, "text") + " » (" + colorName(jstr(title, "color")) + ")"};
        if (title.is_object() && !ch.is_object()) {
            // A title refused by the server.
            std::string why = reason == "TITLE_EXISTS" ? std::string("il existe déjà") : reason == "VALIDATION_ERROR" ? std::string("texte ou couleur invalide")
                                                                                                                       : std::string("refus du serveur");
            return {"Refus", ui.theme().textDim, "création du titre « " + jstr(title, "text") + " » refusée : " + why};
        }
        if (ch.is_object()) {
            // A change of the calendar refused by the server.
            const std::string act = jstr(ch, "action");
            std::string what = (act == "reset" ? "retour au tirage du défi " : act == "rewards" ? "modification des récompenses du défi " : "modification du défi ") +
                               periodPhrase(ch);
            std::string why = reason == "CHALLENGE_STARTED"    ? std::string("déjà commencé")
                              : reason == "VALIDATION_ERROR"   ? std::string(act == "rewards" ? "récompenses invalides" : "règles invalides")
                              : reason == "LEVEL_UNAVAILABLE"  ? std::string("niveau indisponible")
                              : reason == "FINISH_NEEDS_ROUTE" ? std::string("« Finir le niveau » sans parcours tracé")
                              : reason == "NOT_CHOSEN"         ? std::string("c'était déjà le tirage")
                                                               : std::string("refus du serveur");
            return {"Refus", ui.theme().textDim, what + " refusée : " + why};
        }
        bool grant = jstr(e, "role") == "admin";
        std::string tried = (grant ? "donner le rôle admin à " : "retirer le rôle admin à ") + target;
        std::string text;
        if (reason == "OWN_ROLE") text = "a tenté de changer son propre rôle : refusé";
        else if (reason == "LAST_ADMIN") text = "a tenté de " + tried + " : refusé, il doit rester un admin";
        else if (reason == "ACCOUNT_SUSPENDED") text = "a tenté de " + tried + " : refusé, compte suspendu";
        else if (reason == "ROLE_CHANGED") text = "a tenté de " + tried + " : refusé, le rôle avait changé entre-temps";
        else text = "a tenté de " + tried + " : refusé";
        return {"Refus", ui.theme().textDim, text};
    }

    void drawLog(const Rect& in) {
        UIContext& ui = g_.ui;
        ui.pushId("log");
        float y = in.y, fh = ui.s(46);
        int kind = kind_;
        float kr = chips("kind", in.x, y, fh, {"Toutes les actions", "Rôles", "Défis", "Campagne", "Créations", "Ennemis", "Refus"}, kind, in.right());
        std::vector<std::string> who{"Tous"};
        for (const auto& a : admins_) who.push_back(jstr(a, "username"));
        int actor = std::min(actor_, static_cast<int>(who.size()) - 1);
        // The admin's choice after the chips when it has room there, else on the line below.
        float cycleX = kr + ui.s(16);
        if (in.right() - cycleX < ui.s(240)) {
            y += fh + ui.s(10);
            cycleX = in.x;
        }
        float cycleW = std::min(ui.s(400), in.right() - cycleX);
        ui.cycle("actor", {cycleX, y, cycleW, fh}, "Admin", actor, who);
        if (kind != kind_ || actor != actor_) {
            kind_ = kind;
            actor_ = actor;
            loadLog(false);
        }
        std::string keep = "Gardé 6 mois · ni adresse IP ni e-mail";
        float room = in.right() - (cycleX + ui.s(20) + cycleW);
        if (room > ui.s(120)) {
            float ks = ui.fitFont(keep, FontFace::Body, 15, room);
            ui.label(ui.elide(keep, FontFace::Body, ks, room), {in.right(), y + fh * 0.5f}, ks, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        y += fh + ui.s(14);

        float c1 = in.x + ui.s(12), c2 = in.x + in.w * 0.15f, c3 = in.x + in.w * 0.31f;
        ui.label("Quand", {c1, y + ui.s(13)}, 14, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label("Admin", {c2, y + ui.s(13)}, 14, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label("Action", {c3, y + ui.s(13)}, 14, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(30);
        float gap = ui.s(6), moreH = logMore_ ? ui.s(54) : 0.0f;
        Rect list{in.x, y, in.w, in.bottom() - y - moreH};
        int n = log_.is_array() ? static_cast<int>(log_.size()) : 0;
        if (n == 0)
            ui.label(loadedLog_ ? "Rien d'inscrit pour ces filtres." : "Chargement…", {list.center().x, list.y + ui.s(40)}, 18, ui.theme().textDim,
                     Align::Center, FontFace::Body, VAlign::Middle);
        // Each row as high as its sentence needs (a challenge's wraps, never cut).
        std::vector<LogLine> lines;
        std::vector<float> heights;
        float content = 0.0f;
        const float rowRight = list.right() - ui.s(16) - ui.s(12);
        for (int i = 0; i < n; ++i) {
            lines.push_back(lineOf(log_[i]));
            float pw = ui.text().measure(lines.back().pill, FontFace::Bold, ui.s(13)).x + ui.s(20);
            float tw = rowRight - (c3 + pw + ui.s(12));
            float th = ui.text().measureWrapped(lines.back().text, FontFace::Body, ui.s(16), tw).y;
            heights.push_back(std::max(ui.s(50), th + ui.s(22)));
            content += heights.back() + gap;
        }
        ui.beginScroll("entries", list, content);
        float ry = list.y - ui.scrollOffset();
        for (int i = 0; i < n; ++i) {
            const Json& e = log_[i];
            Rect row{list.x, ry, list.w - ui.s(16), heights[static_cast<size_t>(i)]};
            ry += row.h + gap;
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(8), Color{18, 26, 50, 235});
            // The time and the admin on the first line (centred on a one-line row).
            float cy = row.y + ui.s(25);
            ui.label(ui.elide(stampText(jstr(e, "at")), FontFace::Body, 16, c2 - c1 - ui.s(10)), {c1, cy}, 16, ui.theme().text, Align::Left, FontFace::Body,
                     VAlign::Middle);
            const Json& who2 = e.contains("actor") ? e["actor"] : Json();
            float av = ui.s(34);
            if (who2.is_object()) {
                Rect ar{c2, cy - av * 0.5f, av, av};
                ui_.drawAvatar(jstr(who2, "avatar", "avatar_01"), jstr(who2, "id"), ar);
                i18n::Verbatim pseudo;  // the pseudonym as its player chose it
                ui.label(ui.elide(jstr(who2, "username", "compte supprimé"), FontFace::Bold, 16, c3 - ar.right() - ui.s(24)), {ar.right() + ui.s(10), cy}, 16,
                         ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            } else {
                ui.r().fillCircle({c2 + av * 0.5f, cy}, av * 0.5f, Color{86, 100, 126, 255});
                ui.label("Système", {c2 + av + ui.s(10), cy}, 16, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
            }
            drawAction(lines[static_cast<size_t>(i)], {c3, row.y, row.right() - c3 - ui.s(12), row.h});
        }
        ui.endScroll();
        if (logMore_) {
            UIContext::ButtonStyle st;
            st.small = true;
            if (ui.button("more", {in.right() - ui.s(220), in.bottom() - ui.s(44), ui.s(220), ui.s(44)}, "Plus anciens", st)) loadLog(true);
        }
        ui.popId();
    }

    void drawAction(const LogLine& l, const Rect& r) {
        UIContext& ui = g_.ui;
        float ph = ui.s(24), pw = ui.text().measure(l.pill, FontFace::Bold, ui.s(13)).x + ui.s(20);
        Rect pr{r.x, r.y + ui.s(25) - ph * 0.5f, pw, ph};
        ui.r().fillRoundedRect(pr, ph * 0.5f, l.color.withAlphaF(0.14f));
        ui.r().drawRoundedRect(pr, ph * 0.5f, l.color, ui.s(1));
        ui.label(l.pill, pr.center(), 13, l.color, Align::Center, FontFace::Bold, VAlign::Middle);
        float tx = pr.right() + ui.s(12), room = r.right() - tx;
        float th = ui.text().measureWrapped(l.text, FontFace::Body, ui.s(16), room).y;
        // One line: centred on the pill; more: from the top of the row.
        float ty = th <= ui.s(30) ? pr.center().y - th * 0.5f : r.y + ui.s(11);
        ui.labelWrapped(l.text, {tx, ty}, room, 16, ui.theme().text);
    }

    void drawStatus() {
        UIContext& ui = g_.ui;
        if (status_.empty() || ui.time() - statusT_ > 6.0f) return;
        float y = ui.screenH() - ui.s(34);
        float room = W_;
        float fs = ui.fitFont(status_, FontFace::Bold, 17, room);
        ui.label(ui.elide(status_, FontFace::Bold, fs, room), {ui.screenW() * 0.5f, y}, fs, statusError_ ? ui.theme().danger : ui.theme().success,
                 Align::Center, FontFace::Bold, VAlign::Middle);
    }

    GameServices& g_;
    OnlineUi ui_;
    AdminCalendar calendar_;  // « Calendrier des défis » (2.42)
    AdminBackups backups_;    // « Sauvegardes » (2.43)
    AdminCampaign campaign_;  // « Campagne » (2.45)
    std::string campaignSelect_;
    AdminCreations creations_;  // « Créations »: the suits made on the server
    AdminEnemies enemies_;  // « Ennemis » (2.46)
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    float W_ = 0.0f, x0_ = 0.0f;
    int tab_ = 0;
    bool left_ = false;
    // Joueurs
    std::string search_;
    bool searchDirty_ = false;
    float searchT_ = 0.0f;
    int filter_ = 0, offset_ = 0, playersGen_ = 0;
    bool busy_ = false, busyRole_ = false, loadedPlayers_ = false, autoConfirm_ = false;
    Json players_ = Json::object();
    Json admins_ = Json::array();
    std::string confirmId_;
    Json confirmPlayer_;
    // Journal
    int kind_ = 0, actor_ = 0, logGen_ = 0;
    bool logMore_ = false, loadedLog_ = false;
    Json log_ = Json::array();
    // Messages
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace

std::unique_ptr<Scene> makeAdmin(GameServices& g, const std::string& open) { return std::make_unique<AdminScene>(g, open); }

}  // namespace cosmic
