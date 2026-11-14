// Profile of a player (Cosmic Online), pushed over the current screen: identity, records,
// time played (total and per level, in hours and minutes), creator card (computed by the
// server from the published levels), creations, followers and followed creators, and the
// actions FOLLOW / friend / block. Every number comes from the server.

#include <algorithm>
#include <map>

#include "game/GameServices.h"
#include "game/online/OnlineGlobe.h"
#include "game/progression/Progression.h"
#include "ui/online/GlobeRenderer.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

namespace {

std::string decimal(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string t = b;
    if (auto p = t.find('.'); p != std::string::npos) t[p] = ',';
    return t;
}

std::string dateFr(const std::string& iso) {
    if (iso.size() < 10) return "—";
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
}

std::string runTime(int64_t ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d.%03d", static_cast<int>(ms / 60000), static_cast<int>(ms / 1000 % 60), static_cast<int>(ms % 1000));
    return buf;
}

const char* kCreationSorts[] = {"popular", "recent", "top"};

class PlayerProfileScene : public Scene {
public:
    PlayerProfileScene(GameServices& g, std::string playerId) : g_(g), ui_(g), playerId_(std::move(playerId)) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        g_.ui.setFocus("profile/close");
        if (g_.onlineOpen.rfind("profile-tab:", 0) == 0) {  // automated screenshots
            tab_ = std::clamp(std::atoi(g_.onlineOpen.c_str() + 12), 0, 4);
            g_.onlineOpen.clear();
        }
        load();
    }
    void onResume() override { load(); }
    void update(float) override {
        // One's own time played just reached the server: show it.
        double pending = g_.online.pendingPlaytime();
        if (me() && lastPending_ >= 1.0 && pending < 1.0) load();
        lastPending_ = pending;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 200});
        float W = std::min(ui.screenW() - ui.s(60), ui.s(1500)), H = std::min(ui.screenH() - ui.s(60), ui.s(980));
        box_ = {(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        ui.r().fillRoundedRect(box_, ui.s(18), Color{9, 14, 32, 248});
        ui.r().drawRoundedRect(box_, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("profile");
        ui.inputEnabled = !confirmBlock_;
        UIContext::ButtonStyle cs;
        cs.small = true;
        Rect close{box_.right() - ui.s(170), box_.y + ui.s(20), ui.s(150), ui.s(42)};
        bool leave = ui.button("close", close, "Fermer", cs);
        if (loading_ && !data_.is_object()) {
            ui.label("Chargement du profil…", box_.center(), 20, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        } else if (!data_.is_object()) {
            ui.labelWrapped(error_.empty() ? "Profil indisponible." : error_, {box_.x + ui.s(40), box_.center().y}, box_.w - ui.s(80), 18,
                            ui.theme().danger);
        } else {
            drawProfile(close);
        }
        ui.inputEnabled = true;
        if (confirmBlock_) drawConfirmBlock();
        else if (leave || (ui.backPressed() && !ui.editingText())) g_.app.popScene();
        ui.popId();
        ui.end();
    }

private:
    bool me() const { return playerId_ == g_.online.playerId(); }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    // ---------------------------------------------------------------- data
    void load() {
        loading_ = true;
        int gen = ++generation_;
        g_.online.get("/api/v1/players/" + playerId_, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != generation_) return;
                          loading_ = false;
                          if (!r.ok) {
                              error_ = r.message;
                              data_ = Json();
                              return;
                          }
                          error_.clear();
                          data_ = r.data;
                          fromCache_ = r.fromCache;
                          bool hidden = jget<bool>(data_.value("player", Json::object()), "hidden", false);
                          if (hidden) tab_ = 0;
                          loadTab();
                      }));
    }

    void loadTab() {
        tabData_ = Json();
        tabError_.clear();
        std::string base = "/api/v1/players/" + playerId_;
        std::string path;
        if (tab_ == 0) path = base + "/creations?limit=30&sort=" + kCreationSorts[creationSort_];
        else if (tab_ == 1) path = base + "/playtime";
        else if (tab_ == 3) path = base + "/followers?limit=60";
        else if (tab_ == 4) path = base + "/following?limit=60";
        if (path.empty()) return;  // best times: in the profile itself
        int gen = ++tabGeneration_;
        tabLoading_ = true;
        g_.online.get(path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != tabGeneration_) return;
                          tabLoading_ = false;
                          if (!r.ok) tabError_ = r.code == "FOLLOWS_PRIVATE" ? "Ce joueur garde ses listes d'abonnés et d'abonnements privées." : r.message;
                          else tabData_ = r.data;
                      }));
    }

    void openOther(const std::string& id) {
        if (id.empty() || id == playerId_) return;
        playerId_ = id;
        data_ = Json();
        tab_ = 0;
        load();
    }

    TexturePtr thumbnail(const std::string& id) {
        auto it = thumbs_.find(id);
        if (it != thumbs_.end()) return it->second;
        thumbs_[id] = nullptr;
        g_.online.getBinary("/api/v1/community/levels/" + id + "/thumbnail", [this, id, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
            if (!life.lock() || r.status != 200 || r.body.size() < 8) return;
            thumbs_[id] = textureFromPng(g_, std::vector<uint8_t>(r.body.begin(), r.body.end()));
        });
        return nullptr;
    }

    // ------------------------------------------------------------- actions
    void follow(bool on, bool notify = true) {
        busy_ = true;
        auto cb = OnlineService::guard(life_, [this, on](const ApiResult& r) {
            busy_ = false;
            if (!r.ok) return say(r.message, true);
            say(on ? "Vous suivez ce créateur." : "Vous ne suivez plus ce créateur.", false);
            load();
        });
        if (on) g_.online.send("POST", "/api/v1/players/" + playerId_ + "/follow", {{"notify", notify}}, cb);
        else g_.online.send("DELETE", "/api/v1/players/" + playerId_ + "/follow", Json(), cb);
    }

    void friendAction(const std::string& state) {
        auto done = OnlineService::guard(life_, [this](const ApiResult& r) {
            say(r.ok ? "C'est fait." : r.message, !r.ok);
            if (r.ok) load();
        });
        if (state == "friends") g_.online.send("DELETE", "/api/v1/friends/" + playerId_, Json(), done);
        else if (state == "request_received") g_.online.send("POST", "/api/v1/friends/accept", {{"player_id", playerId_}}, done);
        else g_.online.send("POST", "/api/v1/friends/request", {{"player_id", playerId_}}, done);
    }

    void block(bool on) {
        auto done = OnlineService::guard(life_, [this, on](const ApiResult& r) {
            if (!r.ok) return say(r.message, true);
            say(on ? "Joueur bloqué : il ne peut plus vous écrire, vous suivre ni réagir à vos commentaires." : "Joueur débloqué.", false);
            load();
        });
        if (on) g_.online.send("POST", "/api/v1/social/blocks", {{"player_id", playerId_}}, done);
        else g_.online.send("DELETE", "/api/v1/social/blocks/" + playerId_, Json(), done);
    }

    // ------------------------------------------------------------- drawing
    void drawProfile(const Rect& close) {
        UIContext& ui = g_.ui;
        const Json& p = data_["player"];
        const Json& creator = data_.value("creator", Json::object());
        const Json& fstate = data_.value("follow", Json::object());
        const bool hidden = jget<bool>(p, "hidden", false);
        const bool isCreator = jget<bool>(creator, "is_creator", false);
        const float x = box_.x + ui.s(28);
        const float w = box_.w - ui.s(56);
        float y = box_.y + ui.s(24);
        // Identity.
        const float av = ui.s(112);
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), playerId_, {x, y, av, av});
        float nx = x + av + ui.s(24);
        // Actions on the right of the header, under "Fermer".
        std::vector<std::pair<std::string, std::string>> acts;  // key, label
        const bool logged = g_.online.loggedIn();
        if (me()) {
            acts.push_back({"edit", "Modifier mon profil"});
            acts.push_back({"visibility", jstr(p, "follows_visibility", "public") == "private" ? "Abonnements : privés" : "Abonnements : publics"});
        } else if (logged) {
            bool blocked = jget<bool>(fstate, "blocked", false);
            if (!blocked) {
                acts.push_back({"follow", jget<bool>(fstate, "following", false) ? "SUIVI" : "SUIVRE"});
                if (jget<bool>(fstate, "following", false))
                    acts.push_back({"notify", jget<bool>(fstate, "notify", false) ? "Nouveaux niveaux : notifiés" : "Nouveaux niveaux : sans alerte"});
                std::string fr = jstr(p, "friendship");
                if (!hidden)
                    acts.push_back({"friend", fr == "friends" ? "Retirer des amis" : fr == "request_sent" ? "Demande envoyée" : fr == "request_received" ? "Accepter en ami" : "Ajouter en ami"});
            }
            acts.push_back({"block", blocked ? "Débloquer" : "Bloquer"});
        }
        const float bh = ui.s(42), bgap = ui.s(10);
        float actW = ui.s(250);
        float actRight = box_.right() - ui.s(20);
        // Two columns of actions when there are more than two.
        int cols = acts.size() > 2 ? 2 : 1;
        float actX = actRight - cols * actW - (cols - 1) * bgap;
        float ay = close.bottom() + ui.s(12);
        for (size_t i = 0; i < acts.size(); ++i) {
            int col = static_cast<int>(i % static_cast<size_t>(cols)), row = static_cast<int>(i / static_cast<size_t>(cols));
            Rect r{actX + col * (actW + bgap), ay + row * (bh + bgap), actW, bh};
            const std::string& k = acts[i].first;
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !busy_;
            st.primary = k == "follow" && !jget<bool>(fstate, "following", false);
            st.selected = k == "follow" && jget<bool>(fstate, "following", false);
            st.danger = k == "block" && !jget<bool>(fstate, "blocked", false);
            if (k == "friend") st.enabled = jstr(p, "friendship") != "request_sent";
            if (!ui.button("act/" + k, r, acts[i].second, st)) continue;
            if (k == "edit") g_.app.pushScene(makeOnlineSettings(g_, 1));
            else if (k == "visibility") {
                bool priv = jstr(p, "follows_visibility", "public") == "private";
                g_.online.updateProfile({{"follows_visibility", priv ? "public" : "private"}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                                            say(r.ok ? "Réglage enregistré." : r.message, !r.ok);
                                            if (r.ok) load();
                                        }));
            } else if (k == "follow") follow(!jget<bool>(fstate, "following", false));
            else if (k == "notify") follow(true, !jget<bool>(fstate, "notify", false));
            else if (k == "friend") friendAction(jstr(p, "friendship"));
            else if (k == "block") {
                if (jget<bool>(fstate, "blocked", false)) block(false);
                else confirmBlock_ = true;
            }
        }
        float nameRoom = actX - nx - ui.s(20);
        std::string name = jstr(p, "username") + (me() ? " (vous)" : "");
        float nfs = ui.fitFont(name, FontFace::Title, 32, nameRoom * 0.75f);
        Vec2 ns = ui.label(name, {nx, y + ui.s(24)}, nfs, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        if (isCreator) {
            float cw = ui.text().measure("CRÉATEUR", FontFace::Bold, ui.s(14)).x + ui.s(22);
            Rect chip{nx + ns.x + ui.s(14), y + ui.s(24) - ui.s(14), cw, ui.s(28)};
            if (chip.right() <= nx + nameRoom) {
                ui.r().fillRoundedRect(chip, ui.s(14), ui.theme().warm.withAlphaF(0.22f));
                ui.r().drawRoundedRect(chip, ui.s(14), ui.theme().warm, ui.s(1.2f));
                ui.label("CRÉATEUR", chip.center(), 14, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
            }
        }
        std::string cc = jstr(p, "country_code");
        float ly = y + ui.s(62);
        if (!cc.empty()) {
            ui_.drawFlag(cc, {nx, ly - ui.s(10), ui.s(28), ui.s(21)});
            ui.label(ui_.countryName(cc), {nx + ui.s(36), ly}, ui.fitFont(ui_.countryName(cc), FontFace::Body, 17, nameRoom - ui.s(36)), ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            ly += ui.s(28);
        }
        std::string sub;
        if (!hidden && p.contains("created_at")) sub = "Inscrit le " + dateFr(jstr(p, "created_at"));
        if (jget<bool>(fstate, "followed_by", false)) sub += (sub.empty() ? "" : "  ·  ") + std::string("vous suit");
        if (!sub.empty()) ui.label(sub, {nx, ly}, ui.fitFont(sub, FontFace::Body, 15, nameRoom), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        float headerBottom = std::max(y + av, ay + ((acts.size() + cols - 1) / std::max(cols, 1)) * (bh + bgap) - bgap);
        y = headerBottom + ui.s(18);
        // Statistics.
        const Json& s = data_.value("statistics", Json::object());
        const Json& pt = data_.value("playtime", Json::object());
        struct Tile {
            std::string value, key;
        };
        std::vector<Tile> tiles;
        if (!hidden) {
            tiles.push_back({playTimeText(jget<double>(pt, "total_seconds", 0.0)), "temps de jeu"});
            tiles.push_back({std::to_string(jget<int>(s, "verified_records", 0)), "records vérifiés"});
            tiles.push_back({std::to_string(jget<int>(s, "first_places", 0)), "premières places"});
            tiles.push_back({std::to_string(jget<int>(s, "levels_completed", 0)), "niveaux classés"});
        }
        tiles.push_back({std::to_string(jget<int>(creator, "followers", 0)), "abonnés"});
        tiles.push_back({std::to_string(jget<int>(creator, "following", 0)), "abonnements"});
        y = drawTiles(tiles, x, y, w) + ui.s(10);
        if (isCreator) {
            const Json& rt = creator.value("rating", Json::object());
            std::vector<Tile> ct = {
                {std::to_string(jget<int>(creator, "levels_published", 0)), "niveaux publiés"},
                {std::to_string(jget<int>(creator, "downloads", 0)), "téléchargements"},
                {std::to_string(jget<int>(creator, "unique_players", 0)), "joueurs uniques"},
                {std::to_string(jget<int>(creator, "favorites", 0)), "mis en favori"},
                {rt.is_object() && rt.contains("average") ? decimal(jget<double>(rt, "average", 0.0)) + "/5" : "—", "note moyenne"},
                {dateFr(jstr(creator, "first_published_at")), "premier niveau publié"},
            };
            y = drawTiles(ct, x, y, w) + ui.s(10);
        }
        if (hidden) {
            Vec2 hs = ui.labelWrapped("Ce joueur a choisi de masquer son profil public : seuls son pseudonyme, son avatar et ses niveaux publiés sont visibles.",
                                      {x, y}, w, 16, ui.theme().textDim);
            y += hs.y + ui.s(10);
        }
        // Tabs.
        std::vector<std::string> labels = {"Créations", "Temps de jeu", "Meilleurs temps", "Abonnés", "Abonnements"};
        if (hidden) labels = {"Créations"};
        int tab = tab_;
        ui.tabs("tabs", {x, y, std::min(w, ui.s(220) * labels.size()), ui.s(42)}, labels, tab);
        if (tab != tab_) {
            tab_ = tab;
            loadTab();
        }
        y += ui.s(54);
        Rect area{x, y, w, box_.bottom() - y - ui.s(44)};
        switch (tab_) {
            case 0: drawCreations(area); break;
            case 1: drawPlaytime(area); break;
            case 2: drawBestTimes(area); break;
            default: drawPlayers(area); break;
        }
        std::string foot = fromCache_ ? "Hors ligne : profil en cache" : std::string{};
        if (!status_.empty() && ui.time() - statusT_ < 8.0f) foot = status_;
        if (!foot.empty())
            ui.label(foot, {x, box_.bottom() - ui.s(24)}, ui.fitFont(foot, FontFace::Body, 15, w), statusError_ && foot == status_ ? ui.theme().danger : ui.theme().success,
                     Align::Left, FontFace::Body, VAlign::Middle);
    }

    template <typename T>
    float drawTiles(const std::vector<T>& tiles, float x, float y, float w) {
        UIContext& ui = g_.ui;
        const float gap = ui.s(10), h = ui.s(74);
        float tw = (w - gap * (tiles.size() - 1)) / static_cast<float>(tiles.size());
        for (size_t i = 0; i < tiles.size(); ++i) {
            Rect c{x + i * (tw + gap), y, tw, h};
            ui.r().fillRoundedRect(c, ui.s(10), Color{18, 28, 54, 230});
            ui.label(tiles[i].value, {c.center().x, c.y + ui.s(28)}, ui.fitFont(tiles[i].value, FontFace::Title, 24, tw - ui.s(16)), ui.theme().accent,
                     Align::Center, FontFace::Title, VAlign::Middle);
            ui.label(tiles[i].key, {c.center().x, c.y + ui.s(56)}, ui.fitFont(tiles[i].key, FontFace::Body, 13, tw - ui.s(12)), ui.theme().textDim,
                     Align::Center, FontFace::Body, VAlign::Middle);
        }
        return y + h;
    }

    bool tabEmptyOrBusy(const Rect& area, const Json& list, const std::string& empty) {
        UIContext& ui = g_.ui;
        if (!tabError_.empty()) {
            ui.labelWrapped(tabError_, {area.x, area.y + ui.s(6)}, area.w, 16, ui.theme().textDim);
            return true;
        }
        if (tabLoading_ || !tabData_.is_object()) {
            ui.label("Chargement…", {area.x, area.y + ui.s(16)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return true;
        }
        if (list.empty()) {
            ui.labelWrapped(empty, {area.x, area.y + ui.s(6)}, area.w, 16, ui.theme().textDim);
            return true;
        }
        return false;
    }

    void drawCreations(Rect area) {
        UIContext& ui = g_.ui;
        float sw = std::min(ui.s(420), area.w * 0.5f);
        int sort = creationSort_;
        ui.tabs("csort", {area.right() - sw, area.y, sw, ui.s(36)}, {"Populaires", "Récents", "Mieux notés"}, sort);
        if (sort != creationSort_) {
            creationSort_ = sort;
            loadTab();
        }
        area.y += ui.s(46);
        area.h -= ui.s(46);
        Json list = tabData_.is_object() ? tabData_.value("creations", Json::array()) : Json::array();
        if (tabEmptyOrBusy(area, list, me() ? "Vous n'avez publié aucun niveau : WORKSHOP → Mes niveaux → Publier."
                                             : "Ce joueur n'a publié aucun niveau pour l'instant."))
            return;
        const float rh = ui.s(84), gap = ui.s(8);
        ui.beginScroll("creations", area, list.size() * (rh + gap));
        float cy = area.y - ui.scrollOffset();
        for (const auto& l : list) {
            Rect row{area.x, cy, area.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            std::string id = jstr(l, "id");
            bool published = jstr(l, "status") == "published";
            UIContext::ButtonStyle st;
            st.enabled = published;
            if (ui.button("lvl/" + id, row, "", st)) g_.app.switchScene(makeOnlineLevels(g_, id));
            Rect th{row.x + ui.s(8), row.y + ui.s(8), (rh - ui.s(16)) * 16.0f / 9.0f, rh - ui.s(16)};
            ui.r().fillRoundedRect(th, ui.s(6), Color{6, 9, 20, 255});
            if (TexturePtr t = thumbnail(id)) ui.r().drawTexture(*t, nullptr, th);
            float tx = th.right() + ui.s(14), room = row.right() - tx - ui.s(14);
            std::string title = jstr(l, "title");
            std::string badge = published ? "" : (jstr(l, "status") == "hidden" ? "masqué (signalements)" : "brouillon");
            float bw = badge.empty() ? 0.0f : ui.text().measure(badge, FontFace::Bold, ui.s(14)).x + ui.s(16);
            ui.label(title, {tx, row.y + ui.s(24)}, ui.fitFont(title, FontFace::Bold, 19, room - bw), ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            if (!badge.empty()) ui.label(badge, {row.right() - ui.s(14), row.y + ui.s(24)}, 14, ui.theme().danger, Align::Right, FontFace::Bold, VAlign::Middle);
            std::string stats = "v" + std::to_string(jget<int>(l, "version", 1)) + "  ·  " + std::to_string(jget<int>(l, "downloads", 0)) + " téléch.  ·  " +
                                std::to_string(jget<int>(l, "favorites", 0)) + " favoris  ·  " + std::to_string(jget<int>(l, "comments", 0)) + " commentaires";
            const Json& rt = l.value("rating", Json::object());
            if (rt.is_object() && rt.contains("average")) stats += "  ·  note " + decimal(jget<double>(rt, "average", 0.0)) + "/5";
            ui.label(stats, {tx, row.y + ui.s(56)}, ui.fitFont(stats, FontFace::Body, 14, room), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    void drawPlaytime(Rect area) {
        UIContext& ui = g_.ui;
        const Json& pt = tabData_.is_object() ? tabData_.value("playtime", Json::object()) : Json::object();
        Json list = pt.value("levels", Json::array());
        std::string head = "Temps de jeu total : " + playTimeText(jget<double>(pt, "total_seconds", 0.0));
        if (!tabLoading_ && tabError_.empty() && tabData_.is_object()) {
            ui.label(head, {area.x, area.y + ui.s(14)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            Vec2 ns = ui.labelWrapped("Temps passé dans les niveaux, envoyé par le jeu à la fin de chaque partie ; le serveur ne compte jamais plus que le "
                                      "temps réellement écoulé.",
                                      {area.x, area.y + ui.s(34)}, area.w, 13, ui.theme().textDim);
            area.y += ui.s(42) + ns.y;
            area.h = box_.bottom() - ui.s(44) - area.y;
        }
        if (tabEmptyOrBusy(area, list, "Pas encore de temps de jeu enregistré en ligne.")) return;
        const float rh = ui.s(40);
        ui.beginScroll("playtime", area, list.size() * rh);
        float cy = area.y - ui.scrollOffset();
        for (const auto& l : list) {
            if (cy + rh > area.y && cy < area.bottom()) {
                std::string title = jstr(l, "title");
                std::string when = "dernière partie " + OnlineUi::ageOf(jstr(l, "last_played_at"));
                float tw = ui.s(170), ww = ui.s(260);
                ui.label(title, {area.x, cy + rh * 0.5f}, ui.fitFont(title, FontFace::Body, 17, area.w - tw - ww - ui.s(40)), ui.theme().text, Align::Left,
                         FontFace::Body, VAlign::Middle);
                ui.label(when, {area.right() - tw - ui.s(30), cy + rh * 0.5f}, ui.fitFont(when, FontFace::Body, 14, ww), ui.theme().textDim, Align::Right,
                         FontFace::Body, VAlign::Middle);
                ui.label(playTimeText(jget<double>(l, "seconds", 0.0)), {area.right() - ui.s(16), cy + rh * 0.5f}, 18, ui.theme().accent, Align::Right,
                         FontFace::Bold, VAlign::Middle);
            }
            cy += rh;
        }
        ui.endScroll();
    }

    void drawBestTimes(Rect area) {
        UIContext& ui = g_.ui;
        const Json& s = data_.value("statistics", Json::object());
        Json bests = s.is_object() ? s.value("best_times", Json::array()) : Json::array();
        if (bests.empty()) {
            ui.label("Aucun record vérifié pour l'instant.", {area.x, area.y + ui.s(14)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rh = ui.s(36);
        ui.beginScroll("bests", area, bests.size() * rh);
        float yy = area.y - ui.scrollOffset();
        for (const auto& b : bests) {
            RunCategory cat;
            std::string catLabel = categoryFromId(jstr(b, "category"), cat) ? categoryLabel(cat) : jstr(b, "category");
            std::string what = jstr(b, "level_name") + " · " + catLabel;
            ui.label(what, {area.x, yy + rh * 0.5f}, ui.fitFont(what, FontFace::Body, 16, area.w - ui.s(300)), ui.theme().text, Align::Left, FontFace::Body,
                     VAlign::Middle);
            int rk = jget<int>(b, "rank", 0);
            ui.label("n°" + std::to_string(rk), {area.right() - ui.s(150), yy + rh * 0.5f}, 16,
                     rk <= 3 ? GlobeRenderer::tierColor(markerTier(rk)) : ui.theme().textDim, Align::Right, FontFace::Bold, VAlign::Middle);
            ui.label(runTime(jget<int64_t>(b, "time_ms", 0)), {area.right() - ui.s(16), yy + rh * 0.5f}, 16, ui.theme().text, Align::Right, FontFace::Bold,
                     VAlign::Middle);
            yy += rh;
        }
        ui.endScroll();
    }

    void drawPlayers(const Rect& area) {
        UIContext& ui = g_.ui;
        Json list = tabData_.is_object() ? tabData_.value("players", Json::array()) : Json::array();
        if (tabEmptyOrBusy(area, list, tab_ == 3 ? "Personne ne suit encore ce joueur." : "Ce joueur ne suit aucun créateur."))
            return;
        const float rh = ui.s(58), gap = ui.s(6);
        ui.beginScroll("players", area, list.size() * (rh + gap));
        float cy = area.y - ui.scrollOffset();
        for (const auto& pl : list) {
            Rect row{area.x, cy, area.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            std::string id = jstr(pl, "id");
            if (ui.button("pl/" + id, row, "")) {
                openOther(id);
                return;
            }
            ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), id, {row.x + ui.s(10), row.y + ui.s(9), ui.s(40), ui.s(40)});
            std::string name = jstr(pl, "username");
            int nf = jget<int>(pl, "followers", 0);
            std::string info = std::to_string(nf) + (nf > 1 ? " abonnés" : " abonné") + "  ·  depuis " + OnlineUi::ageOf(jstr(pl, "since"));
            float iw = ui.s(330);
            Vec2 ns = ui.label(name, {row.x + ui.s(64), row.center().y}, ui.fitFont(name, FontFace::Bold, 18, row.w - iw - ui.s(200)), ui.theme().text,
                               Align::Left, FontFace::Bold, VAlign::Middle);
            if (jget<bool>(pl, "creator", false)) {
                float cw = ui.text().measure("CRÉATEUR", FontFace::Bold, ui.s(12)).x + ui.s(16);
                Rect chip{row.x + ui.s(76) + ns.x, row.center().y - ui.s(11), cw, ui.s(22)};
                ui.r().fillRoundedRect(chip, ui.s(11), ui.theme().warm.withAlphaF(0.22f));
                ui.label("CRÉATEUR", chip.center(), 12, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            ui.label(info, {row.right() - ui.s(14), row.center().y}, ui.fitFont(info, FontFace::Body, 14, iw), ui.theme().textDim, Align::Right,
                     FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    void drawConfirmBlock() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(330), ui.screenH() * 0.5f - ui.s(130), ui.s(660), ui.s(260)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().danger.withAlphaF(0.6f), ui.s(1.5f));
        ui.pushId("block");
        std::string name = jstr(data_["player"], "username");
        ui.labelWrapped("Bloquer " + name + " ? Il ne pourra plus vous écrire, vous suivre, ni voir vos commentaires ; ses commentaires vous seront "
                        "masqués. Vous ne le suivrez plus.",
                        {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 17, ui.theme().text);
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button("ok", {box.x + ui.s(30), box.bottom() - ui.s(70), ui.s(240), ui.s(46)}, "Bloquer", d)) {
            confirmBlock_ = false;
            block(true);
        } else if (ui.button("cancel", {box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) {
            confirmBlock_ = false;
        }
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    std::string playerId_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Rect box_;
    double lastPending_ = 0.0;
    Json data_;
    std::string error_;
    bool loading_ = false, fromCache_ = false, busy_ = false, confirmBlock_ = false;
    int generation_ = 0;
    int tab_ = 0, creationSort_ = 0;
    Json tabData_;
    std::string tabError_;
    bool tabLoading_ = false;
    int tabGeneration_ = 0;
    std::map<std::string, TexturePtr> thumbs_;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace

std::unique_ptr<Scene> makePlayerProfile(GameServices& g, const std::string& playerId) {
    return std::make_unique<PlayerProfileScene>(g, playerId);
}

}  // namespace cosmic
