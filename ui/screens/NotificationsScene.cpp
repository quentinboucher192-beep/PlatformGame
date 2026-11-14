// Notifications of the main menu: new campaign levels, powers, monsters and bosses, campaign
// levels modified (records dropped), the version installed and newer installers to download;
// and, in a second tab, the Cosmic Online notification centre (creators followed, favorites
// updated, comments, reactions, followers), read from the server: mark read, delete, filter,
// and categories the player can turn off.

#include <algorithm>
#include <functional>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include <SDL3/SDL_misc.h>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "packaging/Sha256.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

struct KindStyle {
    const char* glyph;
    Color color;
    const char* name;
};

KindStyle styleOf(const std::string& kind) {
    if (kind == "level") return {"N", {57, 240, 216, 255}, "Nouveau niveau"};
    if (kind == "level_changed") return {"M", {255, 179, 71, 255}, "Niveau modifié"};
    if (kind == "record") return {"R", {255, 77, 109, 255}, "Record"};
    if (kind == "power") return {"P", {176, 76, 255, 255}, "Pouvoir"};
    if (kind == "enemy") return {"E", {255, 138, 92, 255}, "Monstre"};
    if (kind == "boss") return {"B", {255, 217, 138, 255}, "Boss"};
    if (kind == "update") return {"", {120, 230, 140, 255}, "Mise à jour"};
    return {"i", {127, 184, 255, 255}, "Version"};
}

/// Cosmic Online notification kinds (server NOTIFICATION_KINDS).
struct OnlineKind {
    const char* kind;
    const char* glyph;
    Color color;
    const char* name;      // on the card
    const char* setting;   // in the settings
};
const std::vector<OnlineKind>& onlineKinds() {
    static const std::vector<OnlineKind> k = {
        {"new_level", "N", {57, 240, 216, 255}, "Créateur suivi", "Nouveau niveau d'un créateur que je suis"},
        {"level_update", "M", {255, 179, 71, 255}, "Favori mis à jour", "Nouvelle version d'un de mes favoris"},
        {"comment_reply", "R", {127, 184, 255, 255}, "Réponse", "Réponse à un de mes commentaires"},
        {"comment_reaction", "J", {255, 120, 170, 255}, "Réaction", "Réaction à un de mes commentaires"},
        {"level_comment", "C", {150, 170, 255, 255}, "Commentaire", "Commentaire sur un de mes niveaux"},
        {"new_follower", "A", {120, 230, 140, 255}, "Abonné", "Nouvel abonné"},
        {"project_invite", "P", {176, 76, 255, 255}, "Projet", "Invitation à un projet"},
        {"project_role", "P", {176, 76, 255, 255}, "Projet", "Changement de mon rôle dans un projet"},
        // Named versions and mentions arrive with the history and the live editor.
    };
    return k;
}
const OnlineKind& onlineKind(const std::string& kind) {
    for (const auto& k : onlineKinds())
        if (kind == k.kind) return k;
    return onlineKinds().front();
}
const char* kOnlineFilters[] = {"all", "unread", "creators", "comments", "projects"};

std::string sizeText(int64_t bytes) {
    if (bytes <= 0) return "";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

class NotificationsScene : public Scene {
public:
    explicit NotificationsScene(GameServices& g, int tab) : g_(g), ui_(g), tab_(tab) {}

    void onEnter() override {
        g_.ui.setFocus("news/back");
        if (g_.online.usable()) g_.checkForUpdate();
        if (tab_ == 1) loadOnline(false);
        if (g_.onlineOpen == "notif-settings") {  // automated screenshots
            g_.onlineOpen.clear();
            settingsOpen_ = true;
            loadSettings();
        }
    }
    void onResume() override {
        if (tab_ == 1) loadOnline(false);
    }
    void onExit() override { g_.news.markAllRead(); }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.55f);
        float W = std::min(ui.screenW() - ui.s(120), ui.s(1240));
        float x0 = (ui.screenW() - W) * 0.5f;
        float y = ui.s(48);
        ui.label("NOTIFICATIONS", {x0, y}, 44, ui.theme().text, Align::Left, FontFace::Title);
        ui.pushId("news");
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = tab_ == 0 ? g_.news.unread() > 0 : g_.online.unreadNotifications() > 0;
        float bw = ui.s(280);
        if (ui.button("readall", {x0 + W - bw - ui.s(196), y, bw, ui.s(52)}, "Tout marquer comme lu", st)) {
            if (tab_ == 0) g_.news.markAllRead();
            else readAll();
        }
        if (ui.button("back", {x0 + W - ui.s(180), y, ui.s(180), ui.s(52)}, "Retour") || ui.backPressed()) {
            g_.app.switchScene(makeMainMenu(g_), 0.3f);
            ui.inputEnabled = true;
            ui.popId();
            ui.end();
            return;
        }
        y += ui.s(72);
        {
            int localN = g_.news.unread(), onlineN = g_.online.loggedIn() ? g_.online.unreadNotifications() : 0;
            int tab = tab_;
            ui.tabs("source", {x0, y, std::min(W, ui.s(720)), ui.s(46)},
                    {"Nouveautés du jeu" + (localN > 0 ? " (" + std::to_string(localN) + ")" : std::string()),
                     "Cosmic Online" + (onlineN > 0 ? " (" + std::to_string(onlineN) + ")" : std::string())},
                    tab);
            if (tab != tab_) {
                tab_ = tab;
                if (tab_ == 1) loadOnline(false);
            }
            y += ui.s(62);
        }
        if (tab_ == 1) {
            drawOnline(x0, y, W);
            ui.inputEnabled = true;
            ui.popId();
            if (top) ui.end();
            return;
        }
        const auto& items = g_.news.items();
        if (items.empty()) {
            ui.labelWrapped(g_.news.scanned() ? "Rien de nouveau pour l'instant. Les nouveaux niveaux, pouvoirs, monstres et boss, les records "
                                                "à refaire après une modification et les nouvelles versions du jeu apparaîtront ici."
                                              : "Recherche des nouveautés…",
                            {x0, y}, W, 22, ui.theme().textDim);
            ui.inputEnabled = true;
            ui.popId();
            if (top) ui.end();
            return;
        }
        // Card heights from the wrapped text (nothing overlaps).
        float textW = W - ui.s(120) - ui.s(300);
        std::vector<float> heights;
        float total = 0.0f;
        for (const auto& it : items) {
            float th = it.text.empty() ? 0.0f : ui.text().measureWrapped(it.text, FontFace::Body, ui.s(18), textW).y;
            float h = std::max(ui.s(100), ui.s(70) + th + ui.s(22));
            heights.push_back(h);
            total += h + ui.s(12);
        }
        Rect view{x0, y, W, ui.screenH() - y - ui.s(40)};
        ui.beginScroll("list", view, total);
        float cy = view.y - ui.scrollOffset();
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            Rect card{x0, cy, W - ui.s(14), heights[i]};
            cy += heights[i] + ui.s(12);
            if (card.bottom() < view.y || card.y > view.bottom()) continue;
            KindStyle k = styleOf(it.kind);
            ui.panel(card, !it.read, 12);
            if (!it.read) ui.r().fillRoundedRect({card.x, card.y + ui.s(10), ui.s(4), card.h - ui.s(20)}, ui.s(2), k.color);
            Vec2 ic{card.x + ui.s(52), card.y + ui.s(48)};
            ui.r().drawGlow(ic, ui.s(40), k.color.withAlpha(it.read ? 40 : 90));
            ui.r().fillCircle(ic, ui.s(26), Color{8, 12, 26, 255});
            ui.r().drawRing(ic, ui.s(26), ui.s(2.5f), k.color);
            if (k.glyph[0]) {
                ui.label(k.glyph, ic, 24, k.color, Align::Center, FontFace::Bold, VAlign::Middle);
            } else {  // download arrow
                ui.r().drawLine(ic + Vec2{0, -ui.s(11)}, ic + Vec2{0, ui.s(4)}, k.color, ui.s(3.5f));
                ui.r().fillTriangle(ic + Vec2{-ui.s(9), ui.s(1)}, ic + Vec2{ui.s(9), ui.s(1)}, ic + Vec2{0, ui.s(11)}, k.color);
                ui.r().fillRect({ic.x - ui.s(11), ic.y + ui.s(13), ui.s(22), ui.s(3)}, k.color);
            }
            float tx = card.x + ui.s(100);
            float titleRoom = card.right() - tx - ui.s(320);
            ui.label(it.title, {tx, card.y + ui.s(16)}, ui.fitFont(it.title, FontFace::Bold, 22, titleRoom), ui.theme().text, Align::Left,
                     FontFace::Bold);
            ui.label(std::string(k.name) + "  ·  " + it.date, {tx, card.y + ui.s(46)}, 15, k.color.withAlpha(200));
            if (!it.text.empty()) ui.labelWrapped(it.text, {tx, card.y + ui.s(70)}, textW, 18, ui.theme().textDim);
            actions(it, card);
        }
        ui.endScroll();
        ui.inputEnabled = true;
        ui.popId();
        if (top) ui.end();
    }

private:
    // ------------------------------------------------------- Cosmic Online
    void loadOnline(bool more) {
        if (!g_.online.loggedIn()) return;
        std::string path = std::string("/api/v1/notifications?limit=30&filter=") + kOnlineFilters[filter_];
        if (more && nextOffset_ > 0) path += "&offset=" + std::to_string(nextOffset_);
        busy_ = true;
        int gen = ++generation_;
        g_.online.get(
            path, OnlineService::guard(life_, [this, more, gen](const ApiResult& r) {
                if (gen != generation_) return;
                busy_ = false;
                loaded_ = true;
                if (!r.ok) return say(r.message, true);
                Json list = r.data.value("notifications", Json::array());
                if (more)
                    for (auto& n : list) online_.push_back(n);
                else
                    online_ = list;
                g_.online.noteNotificationsUnread(jget<int>(r.data, "unread", 0));
                nextOffset_ = r.meta.is_object() && r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            }),
            false);
    }

    void loadSettings() {
        g_.online.get("/api/v1/notifications/settings", OnlineService::guard(life_, [this](const ApiResult& r) {
                          if (!r.ok) return say(r.message, true);
                          settings_ = r.data.value("settings", Json::object());
                      }),
                      false);
    }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    void readAll() {
        g_.online.send("POST", "/api/v1/notifications/read-all", Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                           if (!r.ok) return say(r.message, true);
                           g_.online.noteNotificationsUnread(0);
                           loadOnline(false);
                       }));
    }

    void markRead(const std::string& id, std::function<void()> then = {}) {
        g_.online.send("POST", "/api/v1/notifications/" + id + "/read", Json(), OnlineService::guard(life_, [this, then](const ApiResult& r) {
                           if (!r.ok) return say(r.message, true);
                           if (then) then();
                           else loadOnline(false);
                       }));
    }

    /// Where a notification leads (nothing for kinds that have no screen yet).
    std::string openLabel(const Json& n) const {
        std::string kind = jstr(n, "kind");
        if ((kind == "new_level" || kind == "level_update") && !jstr(n, "level_id").empty()) return "Voir le niveau";
        if ((kind == "comment_reply" || kind == "comment_reaction" || kind == "level_comment") && !jstr(n, "level_id").empty()) return "Voir les commentaires";
        if (kind == "new_follower" && n.contains("actor") && n["actor"].is_object()) return "Voir le profil";
        if (kind == "project_invite") return "Voir l'invitation";
        if (kind == "project_role" && n.contains("data") && !jstr(n["data"], "project_id").empty()) return "Voir le projet";
        return {};
    }

    void open(const Json& n) {
        std::string kind = jstr(n, "kind");
        std::string id = jstr(n, "id");
        bool unread = !jget<bool>(n, "read", false);
        auto go = [this, n, kind]() {
            if (kind == "new_follower") {
                g_.app.pushScene(makePlayerProfile(g_, jstr(n["actor"], "id")));
                return;
            }
            if (kind == "project_invite") {
                g_.onlineOpen = "project-invitations";
                g_.app.switchScene(makeProjects(g_));
                return;
            }
            if (kind == "project_role") {
                g_.app.switchScene(makeProjects(g_, jstr(n["data"], "project_id")));
                return;
            }
            if (kind == "comment_reply" || kind == "comment_reaction" || kind == "level_comment") g_.onlineOpen = "comments";
            g_.app.switchScene(makeOnlineLevels(g_, jstr(n, "level_id")));
        };
        if (unread) markRead(id, go);
        else go();
    }

    void drawOnline(float x0, float y, float W) {
        UIContext& ui = g_.ui;
        if (!g_.online.loggedIn()) {
            ui.labelWrapped("Connectez-vous à Cosmic Online (menu ONLINE) pour recevoir les notifications des créateurs que vous suivez, de vos "
                            "favoris et de vos commentaires. Le jeu reste entièrement jouable sans compte.",
                            {x0, y}, W, 20, ui.theme().textDim);
            return;
        }
        // Filters and settings.
        int filter = filter_;
        float fw = std::min(W - ui.s(240), ui.s(860));
        ui.tabs("filter", {x0, y, fw, ui.s(42)}, {"Toutes", "Non lues", "Créateurs", "Commentaires", "Projets"}, filter);
        if (filter != filter_) {
            filter_ = filter;
            loadOnline(false);
        }
        UIContext::ButtonStyle ss;
        ss.small = true;
        ss.selected = settingsOpen_;
        if (ui.button("settings", {x0 + W - ui.s(220), y, ui.s(220), ui.s(42)}, settingsOpen_ ? "Fermer les réglages" : "Réglages", ss)) {
            settingsOpen_ = !settingsOpen_;
            if (settingsOpen_) loadSettings();
        }
        y += ui.s(56);
        float bottom = ui.screenH() - ui.s(40);
        if (!status_.empty() && ui.time() - statusT_ < 8.0f) {
            ui.label(status_, {x0, bottom}, ui.fitFont(status_, FontFace::Body, 15, W), statusError_ ? ui.theme().danger : ui.theme().success, Align::Left,
                     FontFace::Body, VAlign::Middle);
        }
        bottom -= ui.s(24);
        if (settingsOpen_) {
            drawSettings({x0, y, W, bottom - y});
            return;
        }
        if (online_.empty()) {
            ui.labelWrapped(!loaded_ || busy_ ? "Chargement…"
                                              : filter_ == 1 ? "Aucune notification non lue."
                                                             : "Aucune notification pour l'instant. Suivez des créateurs, ajoutez des niveaux à vos favoris "
                                                               "et commentez : leurs nouveautés apparaîtront ici.",
                            {x0, y}, W, 20, ui.theme().textDim);
            return;
        }
        // Cards sized from their text (nothing overlaps).
        const float bw = ui.s(240);
        float textW = W - ui.s(100) - bw - ui.s(40);
        std::vector<float> heights;
        float total = 0.0f;
        for (const auto& n : online_) {
            float th = ui.text().measureWrapped(jstr(n, "text"), FontFace::Body, ui.s(19), textW).y;
            float h = std::max(ui.s(118), ui.s(54) + th + ui.s(16));
            heights.push_back(h);
            total += h + ui.s(10);
        }
        if (nextOffset_ > 0) total += ui.s(60);
        Rect view{x0, y, W, bottom - y};
        ui.beginScroll("online", view, total);
        float cy = view.y - ui.scrollOffset();
        for (size_t i = 0; i < online_.size(); ++i) {
            const Json n = online_[i];
            Rect card{x0, cy, W - ui.s(14), heights[i]};
            cy += heights[i] + ui.s(10);
            if (card.bottom() < view.y || card.y > view.bottom()) continue;
            const OnlineKind& k = onlineKind(jstr(n, "kind"));
            bool unread = !jget<bool>(n, "read", false);
            ui.panel(card, unread, 12);
            if (unread) ui.r().fillRoundedRect({card.x, card.y + ui.s(10), ui.s(4), card.h - ui.s(20)}, ui.s(2), k.color);
            Vec2 ic{card.x + ui.s(50), card.y + ui.s(46)};
            if (n.contains("actor") && n["actor"].is_object()) {
                ui_.drawAvatar(jstr(n["actor"], "avatar", "avatar_01"), jstr(n["actor"], "id"), {ic.x - ui.s(26), ic.y - ui.s(26), ui.s(52), ui.s(52)});
                ui.r().drawRing(ic, ui.s(27), ui.s(2.0f), k.color);
            } else {
                ui.r().fillCircle(ic, ui.s(26), Color{8, 12, 26, 255});
                ui.r().drawRing(ic, ui.s(26), ui.s(2.5f), k.color);
                ui.label(k.glyph, ic, 24, k.color, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            float tx = card.x + ui.s(100);
            std::string meta = std::string(k.name) + "  ·  " + OnlineUi::ageOf(jstr(n, "created_at")) + (unread ? "  ·  non lue" : "");
            ui.label(meta, {tx, card.y + ui.s(26)}, ui.fitFont(meta, FontFace::Body, 15, textW), k.color.withAlpha(220), Align::Left, FontFace::Body,
                     VAlign::Middle);
            ui.labelWrapped(jstr(n, "text"), {tx, card.y + ui.s(46)}, textW, 19, unread ? ui.theme().text : ui.theme().textDim);
            // Actions.
            std::string id = jstr(n, "id");
            float bx = card.right() - bw - ui.s(16), by = card.y + ui.s(12);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            std::string go = openLabel(n);
            if (!go.empty()) {
                UIContext::ButtonStyle ps = st;
                ps.primary = true;
                if (ui.button("open" + id, {bx, by, bw, ui.s(30)}, go, ps)) open(n);
                by += ui.s(34);
            }
            if (unread && ui.button("read" + id, {bx, by, bw, ui.s(30)}, "Marquer comme lue", st)) markRead(id);
            if (unread) by += ui.s(34);
            UIContext::ButtonStyle ds = st;
            ds.danger = true;
            if (ui.button("del" + id, {bx, by, bw, ui.s(30)}, "Supprimer", ds)) {
                g_.online.send("DELETE", "/api/v1/notifications/" + id, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                                   if (!r.ok) return say(r.message, true);
                                   loadOnline(false);
                               }));
            }
        }
        if (nextOffset_ > 0) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !busy_;
            if (ui.button("more", {view.center().x - ui.s(160), cy + ui.s(8), ui.s(320), ui.s(42)}, busy_ ? "Chargement…" : "Plus de notifications", st))
                loadOnline(true);
        }
        ui.endScroll();
    }

    void drawSettings(const Rect& area) {
        UIContext& ui = g_.ui;
        Vec2 hs = ui.labelWrapped("Choisissez les notifications Cosmic Online que vous recevez. Une catégorie désactivée n'est plus enregistrée par le "
                                  "serveur ; les autres joueurs n'en sont pas informés.",
                                  {area.x, area.y}, area.w, 17, ui.theme().textDim);
        float y = area.y + hs.y + ui.s(16);
        if (!settings_.is_object() || settings_.empty()) {
            ui.label("Chargement…", {area.x, y + ui.s(14)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        float colW = (area.w - ui.s(20)) * 0.5f;
        int i = 0;
        for (const auto& k : onlineKinds()) {
            float cx = area.x + (i % 2) * (colW + ui.s(20));
            float cy = y + (i / 2) * ui.s(56);
            bool on = jget<bool>(settings_, k.kind, true);
            bool before = on;
            ui.toggle(std::string("set/") + k.kind, {cx, cy, colW, ui.s(46)}, k.setting, on);
            if (on != before) {
                settings_[k.kind] = on;
                g_.online.send("PUT", "/api/v1/notifications/settings", {{k.kind, on}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                                   if (!r.ok) return say(r.message, true);
                                   settings_ = r.data.value("settings", settings_);
                                   say("Réglage enregistré.", false);
                               }));
            }
            ++i;
        }
    }

    void actions(const NewsCenter::Item& it, const Rect& card) {
        UIContext& ui = g_.ui;
        Rect b{card.right() - ui.s(296), card.y + ui.s(18), ui.s(276), ui.s(50)};
        UIContext::ButtonStyle st;
        st.small = true;
        if (it.kind == "level" || it.kind == "level_changed" || it.kind == "record") {
            if (g_.content.level(it.target) && ui.button("play" + it.id, b, it.kind == "record" ? "Refaire un record" : "Jouer", st))
                g_.app.switchScene(makeGameScene(g_, it.target), 0.5f);
            return;
        }
        if (it.kind != "update") return;
        // Installer: download, check its SHA-256, then run it or open its folder.
        fs::path file = Paths::userDir() / "telechargements" / ("CosmicPlatformer-Setup-" + it.target + ".exe");
        bool ready = FileUtil::exists(file) && verified_.count(it.target);
        if (downloading_ == it.target) {
            ui.label("Téléchargement…", b.center(), 18, ui.theme().accent, Align::Center, FontFace::Bold, VAlign::Middle);
        } else if (ready) {
            st.primary = true;
            if (ui.button("run" + it.id, b, "Lancer l'installateur", st)) {
                std::string url = "file:///" + Paths::toUtf8(file);
                std::replace(url.begin(), url.end(), '\\', '/');
                SDL_OpenURL(url.c_str());
            }
            UIContext::ButtonStyle s2;
            s2.small = true;
            if (ui.button("dir" + it.id, {b.x, b.bottom() + ui.s(8), b.w, ui.s(42)}, "Ouvrir le dossier", s2)) {
                std::string url = "file:///" + Paths::toUtf8(file.parent_path());
                std::replace(url.begin(), url.end(), '\\', '/');
                SDL_OpenURL(url.c_str());
            }
        } else {
            st.primary = true;
            st.enabled = g_.online.usable() && downloading_.empty() && !it.download.empty();
            std::string label = "Télécharger (.exe" + (it.size > 0 ? ", " + sizeText(it.size) : std::string()) + ")";
            if (ui.button("dl" + it.id, b, label, st)) download(it, file);
            if (!errors_[it.target].empty())
                ui.labelWrapped(errors_[it.target], {b.x, b.bottom() + ui.s(6)}, b.w, 15, ui.theme().danger);
        }
    }

    void download(const NewsCenter::Item& it, const fs::path& file) {
        downloading_ = it.target;
        errors_[it.target].clear();
        std::string version = it.target, sha = it.sha256;
        auto alive = life_;
        g_.online.getBinary(
            it.download,
            [this, version, sha, file, alive = std::weak_ptr<int>(alive)](const HttpResponse& r) {
                if (alive.expired()) return;
                downloading_.clear();
                if (r.status != 200 || r.body.empty()) {
                    errors_[version] = "Échec du téléchargement (" + (r.status ? "erreur " + std::to_string(r.status) : std::string("réseau")) + ").";
                    return;
                }
                std::vector<uint8_t> bytes(r.body.begin(), r.body.end());
                if (!sha.empty() && Sha256::hex(bytes) != sha) {
                    errors_[version] = "Fichier refusé : son empreinte SHA-256 ne correspond pas à celle annoncée.";
                    return;
                }
                std::error_code ec;
                fs::create_directories(file.parent_path(), ec);
                if (!FileUtil::writeBinary(file, bytes)) {
                    errors_[version] = "Impossible d'écrire le fichier dans le dossier de l'utilisateur.";
                    return;
                }
                verified_.insert(version);
            },
            256u * 1024u * 1024u);
    }

    GameServices& g_;
    OnlineUi ui_;
    MenuBackground bg_;
    int tab_ = 0;
    // Cosmic Online centre.
    int filter_ = 0;
    Json online_ = Json::array();
    Json settings_ = Json::object();
    int nextOffset_ = -1, generation_ = 0;
    bool busy_ = false, loaded_ = false, settingsOpen_ = false;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
    float t_ = 0.0f;
    std::string downloading_;
    std::set<std::string> verified_;
    std::map<std::string, std::string> errors_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace

std::unique_ptr<Scene> makeNotifications(GameServices& g, int tab) { return std::make_unique<NotificationsScene>(g, tab); }

}  // namespace cosmic
