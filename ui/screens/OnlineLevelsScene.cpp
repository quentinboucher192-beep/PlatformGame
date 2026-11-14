// ONLINE LEVELS: levels published by the players (Cosmic Online). Lists (popular, hardest,
// recent, top rated, friends', search, mine, favorites), level card with its leaderboard,
// download and play (the package goes through the same checks as any imported .cplanet),
// ratings, favorites, comments, reports, the creator's profile. Nothing is invented: every
// list and number comes from the server.

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/PackageImporter.h"
#include "ui/online/CommentsPanel.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/OnlineNav.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

namespace {

const char* kSorts[] = {"popular", "hardest", "recent", "top", "friends", "search", "mine", "favorites"};
constexpr int kSearchTab = 5, kFriendsTab = 4, kMineTab = 6, kFavoritesTab = 7;
const char* kFavSorts[] = {"added", "title", "rating", "downloads", "updated"};
const std::vector<std::string> kFavSortLabels = {"Ajout le plus récent", "Titre", "Mieux notés", "Plus téléchargés", "Mis à jour récemment"};
const char* kFavFilters[] = {"all", "available", "unavailable"};
const std::vector<std::string> kFavFilterLabels = {"Tous", "Disponibles", "Indisponibles"};

std::string decimal(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string t = b;
    if (auto p = t.find('.'); p != std::string::npos) t[p] = ',';
    return t;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string timeText(int64_t ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d.%03d", static_cast<int>(ms / 60000), static_cast<int>(ms / 1000 % 60), static_cast<int>(ms % 1000));
    return buf;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

class OnlineLevelsScene : public Scene {
public:
    OnlineLevelsScene(GameServices& g, std::string wanted) : g_(g), ui_(g), comments_(g, ui_), wanted_(std::move(wanted)) {}

    void onEnter() override {
        g_.playMusic("music/menu_theme.ogg");
        bg_.init(g_);
        g_.ui.setFocus("online/nav/levels");
        autoPlay_ = g_.onlineOpen == "play-first";  // automated checks
        if (autoPlay_) {
            g_.onlineOpen.clear();
            tab_ = 2;
        }
        // Automated screenshots: "tab:N" opens a list, "creators" the creator search.
        if (g_.onlineOpen.rfind("tab:", 0) == 0) {
            tab_ = std::clamp(std::atoi(g_.onlineOpen.c_str() + 4), 0, kFavoritesTab);
            g_.onlineOpen.clear();
        } else if (g_.onlineOpen == "creators") {
            tab_ = kSearchTab;
            searchCreators_ = true;
            g_.onlineOpen.clear();
        }
        load(false);
        if (!wanted_.empty()) openLevel(wanted_);
        requireAccount();
    }
    void onResume() override {
        accountAsked_ = false;
        load(false);
        if (!selected_.empty()) openLevel(selected_);
    }

    void update(float dt) override {
        bg_.update(dt);
        requireAccount();
        if (autoPlay_ && !levels_.empty() && selected_.empty()) openLevel(jstr(levels_[0], "id"));
        if (autoPlay_ && detail_.contains("level") && !working_) {
            autoPlay_ = false;
            downloadAndPlay(detail_["level"]);
        }
        if (searchDirty_ > 0.0f && (searchDirty_ -= dt) <= 0.0f) load(false);
        if ((g_.onlineOpen == "comments" || g_.onlineOpen == "comments-replies") && detail_.contains("level")) {
            // From a notification ("Voir les commentaires"), or automated screenshots.
            if (g_.onlineOpen == "comments-replies") comments_.expandAll();
            g_.onlineOpen.clear();
            comments_.open(selected_, jstr(detail_["level"], "title"));
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        layout();
        bg_.render(g_, 0.5f);
        bool modal = confirmReport_ || comments_.modal();
        // Under a screen pushed over this one (profile, account) nothing here reacts.
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = !modal && top;
        drawHeader();
        drawOnlineNav(g_, zoneA_, OnlineSection::Levels);
        if (comments_.isOpen()) {
            ui.r().fillRoundedRect(zoneL_, ui.s(14), Color{8, 13, 30, 220});
            ui.r().drawRoundedRect(zoneL_, ui.s(14), ui.theme().border, ui.s(1.2f));
            if (!comments_.draw(zoneL_)) comments_.close();
        } else {
            drawList();
        }
        drawDetail();
        ui.inputEnabled = top;
        if (confirmReport_) drawReport();
        else if (comments_.modal()) comments_.drawModal();
        else if (ui.backPressed() && !ui.editingText()) {
            if (comments_.isOpen()) comments_.close();
            else if (!selected_.empty()) selected_.clear();
            else g_.app.switchScene(makeMainMenu(g_));
        }
        ui.inputEnabled = true;
        if (top) ui.end();
    }

private:
    void requireAccount() {
        if (g_.online.loggedIn()) {
            accountAsked_ = false;
            return;
        }
        if (accountAsked_ || g_.app.topScene() != this || g_.app.transitioning()) return;
        accountAsked_ = true;
        g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, true));
    }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    // ---------------------------------------------------------------- data
    void load(bool more) {
        if (busy_) return;
        std::string sort = kSorts[tab_];
        if (tab_ == kFavoritesTab) return loadFavorites(more);
        if (tab_ == kSearchTab && searchCreators_) return loadCreators(more);
        if (sort == "search" && query_.size() < 2) {
            levels_ = Json::array();
            nextOffset_ = -1;
            loaded_ = true;
            return;
        }
        std::string path = "/api/v1/community/levels?limit=20&sort=" + (sort == "search" ? std::string("recent") : sort);
        if (sort == "search") path += "&q=" + urlEncode(query_);
        if (more && nextOffset_ > 0) path += "&offset=" + std::to_string(nextOffset_);
        busy_ = true;
        int gen = ++generation_;
        g_.online.get(
            path, OnlineService::guard(life_, [this, more, gen](const ApiResult& r) {
                if (gen != generation_) return;
                busy_ = false;
                loaded_ = true;
                if (!r.ok) return say(r.message, true);
                fromCache_ = r.fromCache;
                Json list = r.data.value("levels", Json::array());
                if (more)
                    for (auto& l : list) levels_.push_back(l);
                else
                    levels_ = list;
                nextOffset_ = r.meta.is_object() && r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            }),
            !more);
    }

    void loadFavorites(bool more) {
        std::string path = std::string("/api/v1/community/favorites?limit=20&sort=") + kFavSorts[favSort_] + "&filter=" + kFavFilters[favFilter_];
        if (more && nextOffset_ > 0) path += "&offset=" + std::to_string(nextOffset_);
        busy_ = true;
        int gen = ++generation_;
        g_.online.get(
            path, OnlineService::guard(life_, [this, more, gen](const ApiResult& r) {
                if (gen != generation_) return;
                busy_ = false;
                loaded_ = true;
                if (!r.ok) return say(r.message, true);
                fromCache_ = r.fromCache;
                Json list = r.data.value("favorites", Json::array());
                if (more)
                    for (auto& l : list) levels_.push_back(l);
                else
                    levels_ = list;
                nextOffset_ = r.meta.is_object() && r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            }),
            false);
    }

    /// Creators (players with a published level), the most followed first; filtered by the
    /// beginning of their pseudonym.
    void loadCreators(bool more) {
        std::string path = "/api/v1/creators?limit=30&sort=followers";
        if (!query_.empty()) path += "&q=" + urlEncode(query_);
        if (more && nextOffset_ > 0) path += "&offset=" + std::to_string(nextOffset_);
        busy_ = true;
        int gen = ++generation_;
        g_.online.get(
            path, OnlineService::guard(life_, [this, more, gen](const ApiResult& r) {
                if (gen != generation_) return;
                busy_ = false;
                loaded_ = true;
                if (!r.ok) return say(r.message, true);
                Json list = r.data.value("creators", Json::array());
                if (more)
                    for (auto& l : list) levels_.push_back(l);
                else
                    levels_ = list;
                nextOffset_ = r.meta.is_object() && r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            }),
            false);
    }

    void setFavorite(const std::string& id, bool on) {
        g_.online.send(on ? "POST" : "DELETE", "/api/v1/community/levels/" + id + "/favorite", Json(),
                       OnlineService::guard(life_, [this, id, on](const ApiResult& r) {
                           if (!r.ok) return say(r.message, true);
                           say(on ? "Ajouté à vos favoris." : "Retiré de vos favoris.", false);
                           if (detail_.contains("level") && jstr(detail_["level"], "id") == id) {
                               detail_["level"]["my_favorite"] = on;
                               detail_["level"]["favorites"] = jget<int>(r.data, "favorites", 0);
                           }
                           if (tab_ == kFavoritesTab) load(false);
                       }));
    }

    void openLevel(const std::string& id) {
        selected_ = id;
        detail_ = Json::object();
        g_.online.get(
            "/api/v1/community/levels/" + id, OnlineService::guard(life_, [this, id](const ApiResult& r) {
                if (id != selected_) return;
                if (!r.ok) {
                    say(r.message, true);
                    if (r.status == 404) selected_.clear();
                    return;
                }
                detail_ = r.data;
            }),
            false);
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

    /// Local level installed from this community level (any version), "" if none.
    std::string installedId(const std::string& communityId, int* version = nullptr, bool* verified = nullptr) const {
        for (const LevelEntry* e : g_.content.levels()) {
            const Json* info = g_.online.communityInstall(e->id);
            if (info && jstr(*info, "id") == communityId) {
                if (version) *version = jget<int>(*info, "version", 0);
                if (verified) *verified = jget<bool>(*info, "verified", false);
                return e->id;
            }
        }
        return {};
    }

    void downloadAndPlay(const Json& level) {
        std::string id = jstr(level, "id");
        int version = jget<int>(level, "version", 1);
        std::string title = jstr(level, "title");
        working_ = true;
        say("Téléchargement…", false);
        g_.online.getBinary(
            "/api/v1/community/levels/" + id + "/package",
            [this, id, version, title, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                if (!life.lock()) return;
                working_ = false;
                if (r.status != 200) return say(r.networkFailure() ? "Connexion impossible : " + r.errorMessage : "Téléchargement refusé (" + std::to_string(r.status) + ").", true);
                fs::path dir = Paths::userDir() / "online" / "downloads";
                std::error_code ec;
                fs::create_directories(dir, ec);
                fs::path file = dir / (id + "-v" + std::to_string(version) + ".cplanet");
                if (!FileUtil::writeBinary(file, std::vector<uint8_t>(r.body.begin(), r.body.end()))) return say("Écriture impossible.", true);
                // The same verifications as any imported package (paths, types, sizes, hashes...).
                PackageImporter imp(g_.content);
                PackageInspection insp = imp.inspect(file);
                if (!insp.valid) return say("Package refusé : " + (insp.errors.empty() ? std::string("invalide") : insp.errors.front()), true);
                std::string installed, err;
                if (!imp.install(insp, ConflictPolicy::Replace, &installed, &err)) return say("Installation impossible : " + err, true);
                g_.content.reload();
                g_.ratings.contentChanged();
                auto lvl = g_.content.loadLevel(installed);
                if (!lvl) return say("Niveau installé illisible.", true);
                std::string serverHash = r.header("x-level-hash");
                bool verified = !serverHash.empty() && fingerprintGameplay(g_.content, *lvl) == serverHash;
                g_.online.noteCommunityInstall(installed, {{"id", id}, {"version", version}, {"level_hash", serverHash}, {"title", title}, {"verified", verified}});
                if (!verified) say("Installé, mais cette copie diffère de la version publiée : vos temps n'y seront pas classés.", true);
                play(installed);
            },
            16u << 20);
    }

    void play(const std::string& localId) {
        PlayOptions po;
        po.fromOnline = true;
        g_.app.switchScene(makeGameScene(g_, localId, po));
    }

    // -------------------------------------------------------------- layout
    void layout() {
        UIContext& ui = g_.ui;
        float W = ui.screenW(), H = ui.screenH(), m = ui.s(22);
        header_ = {0, 0, W, ui.s(64)};
        zoneA_ = {m, header_.bottom() + ui.s(8), ui.s(272), H - header_.bottom() - ui.s(8) - m};
        zoneD_ = {W - m - ui.s(500), zoneA_.y, ui.s(500), zoneA_.h};
        zoneL_ = {zoneA_.right() + ui.s(18), zoneA_.y, zoneD_.x - ui.s(18) - zoneA_.right() - ui.s(18), zoneA_.h};
    }

    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(header_, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = ui.label("COSMIC ONLINE", {ui.s(32), header_.center().y}, 34, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label("Niveaux de la communauté", {ui.s(32) + tsz.x + ui.s(18), header_.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        ui_.drawConnection({zoneD_.right() - ui.s(240), header_.center().y});
    }

    void drawList() {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(zoneL_, ui.s(14), Color{8, 13, 30, 220});
        ui.r().drawRoundedRect(zoneL_, ui.s(14), ui.theme().border, ui.s(1.2f));
        ui.pushId("levels/list");
        int tab = tab_;
        ui.tabs("tabs", {zoneL_.x + ui.s(12), zoneL_.y + ui.s(12), zoneL_.w - ui.s(24), ui.s(40)},
                {"Populaires", "Plus durs", "Récents", "Mieux notés", "Amis", "Recherche", "Les miens", "Favoris"}, tab);
        if (tab != tab_) {
            tab_ = tab;
            levels_ = Json::array();
            loaded_ = false;
            load(false);
        }
        float y = zoneL_.y + ui.s(64);
        if (tab_ == kSearchTab) {
            std::string before = query_;
            float mw = ui.s(300);
            int mode = searchCreators_ ? 1 : 0;
            ui.tabs("mode", {zoneL_.x + ui.s(12), y, mw, ui.s(42)}, {"Niveaux", "Créateurs"}, mode);
            if ((mode == 1) != searchCreators_) {
                searchCreators_ = mode == 1;
                levels_ = Json::array();
                loaded_ = false;
                load(false);
            }
            ui.textField("q", {zoneL_.x + ui.s(22) + mw, y, zoneL_.w - ui.s(34) - mw, ui.s(42)}, query_,
                         searchCreators_ ? "Début du pseudonyme (vide : les plus suivis)" : "Titre ou pseudonyme de l'auteur (2 lettres au moins)", 60);
            if (query_ != before) searchDirty_ = 0.4f;
            y += ui.s(52);
        }
        if (tab_ == kFavoritesTab) {
            float half = (zoneL_.w - ui.s(24) - ui.s(10)) * 0.5f;
            int sort = favSort_, filter = favFilter_;
            ui.cycle("favsort", {zoneL_.x + ui.s(12), y, half, ui.s(42)}, "Tri", sort, kFavSortLabels);
            ui.cycle("favfilter", {zoneL_.x + ui.s(22) + half, y, half, ui.s(42)}, "Afficher", filter, kFavFilterLabels);
            if (sort != favSort_ || filter != favFilter_) {
                favSort_ = sort;
                favFilter_ = filter;
                load(false);
            }
            y += ui.s(52);
        }
        Rect view{zoneL_.x + ui.s(12), y, zoneL_.w - ui.s(24), zoneL_.bottom() - y - ui.s(12)};
        if (levels_.empty()) {
            std::string empty = !loaded_ || busy_ ? "Chargement…"
                                : tab_ == kSearchTab && searchCreators_ ? "Aucun créateur ne correspond."
                                : tab_ == kSearchTab ? (query_.size() < 2 ? "Tapez au moins deux lettres." : "Aucun niveau ne correspond.")
                                : tab_ == kFriendsTab ? "Vos amis n'ont pas encore publié de niveau."
                                : tab_ == kMineTab ? "Vous n'avez rien publié : WORKSHOP → Mes niveaux → Publier."
                                : tab_ == kFavoritesTab ? (favFilter_ == 0 ? "Aucun favori : ouvrez un niveau et choisissez « Ajouter aux favoris »."
                                                                           : "Aucun favori dans cette catégorie.")
                                            : "Aucun niveau publié pour l'instant : soyez le premier (WORKSHOP → Mes niveaux → Publier).";
            ui.labelWrapped(empty, {view.x + ui.s(8), view.y + ui.s(8)}, view.w - ui.s(16), 17, ui.theme().textDim);
            drawStatus({view.x, view.bottom() - ui.s(30), view.w, ui.s(28)});
            ui.popId();
            return;
        }
        const float rh = ui.s(112), gap = ui.s(8);
        float content = static_cast<float>(levels_.size()) * (rh + gap) + (nextOffset_ > 0 ? ui.s(60) : 0.0f);
        ui.beginScroll("cards", view, content);
        float cy = view.y - ui.scrollOffset();
        for (const auto& l : levels_) {
            std::string id = jstr(l, tab_ == kFavoritesTab ? "level_id" : "id");
            Rect row{view.x, cy, view.w - ui.s(12), rh};
            if (tab_ == kSearchTab && searchCreators_) {
                if (cy > view.y - rh && cy < view.bottom()) drawCreator(l, id, row);
                cy += rh + gap;
                continue;
            }
            if (tab_ == kFavoritesTab && !jget<bool>(l, "available", false)) {
                if (cy > view.y - rh && cy < view.bottom()) drawUnavailableFavorite(l, id, row);
                cy += rh + gap;
                continue;
            }
            if (cy > view.y - rh && cy < view.bottom()) {
                UIContext::ButtonStyle st;
                st.selected = id == selected_;
                if (ui.button("c" + id, row, "", st)) openLevel(id);
                Rect th{row.x + ui.s(8), row.y + ui.s(8), (rh - ui.s(16)) * 16.0f / 9.0f, rh - ui.s(16)};
                ui.r().fillRoundedRect(th, ui.s(6), Color{6, 9, 20, 255});
                if (TexturePtr t = thumbnail(id)) ui.r().drawTexture(*t, nullptr, th);
                float tx = th.right() + ui.s(14);
                float room = row.right() - tx - ui.s(12);
                std::string title = jstr(l, "title");
                ui.label(title, {tx, row.y + ui.s(24)}, ui.fitFont(title, FontFace::Bold, 20, room), ui.theme().text, Align::Left, FontFace::Bold,
                         VAlign::Middle);
                std::string sub;
                if (tab_ == kFavoritesTab) {
                    sub = "par " + jstr(l, "author_name") + "  ·  v" + std::to_string(jget<int>(l, "version", 1)) + "  ·  favori " +
                          OnlineUi::ageOf(jstr(l, "favorited_at"));
                } else {
                    const Json& a = l.value("author", Json::object());
                    const PlanetData* pl = g_.content.planet(jstr(l, "planet"));
                    sub = "par " + jstr(a, "username") + "  ·  " + (pl ? pl->name : jstr(l, "planet")) + "  ·  v" + std::to_string(jget<int>(l, "version", 1));
                }
                float subRoom = room;
                if (tab_ != kFavoritesTab && jstr(l, "status") != "published")
                    subRoom -= ui.text().measure("masqué (signalements)", FontFace::Bold, ui.s(14)).x + ui.s(20);
                ui.label(sub, {tx, row.y + ui.s(52)}, ui.fitFont(sub, FontFace::Body, 15, subRoom), ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
                std::string stats;
                if (l.contains("difficulty") && l["difficulty"].is_number()) stats = "Difficulté " + decimal(l["difficulty"].get<double>()) + "/5  ·  ";
                const Json& rt = l.value("rating", Json::object());
                if (rt.is_object() && rt.contains("average") && rt["average"].is_number())
                    stats += "note " + decimal(rt["average"].get<double>()) + "/5 (" + std::to_string(jget<int>(rt, "count", 0)) + ")  ·  ";
                stats += plural(jget<int>(l, "downloads", 0), "téléchargement", "téléchargements") + "  ·  " +
                         plural(jget<int>(l, "favorites", 0), "favori", "favoris") + "  ·  " +
                         plural(jget<int>(l, "comments", 0), "commentaire", "commentaires");
                ui.label(stats, {tx, row.y + ui.s(78)}, ui.fitFont(stats, FontFace::Body, 14, room), ui.theme().warm, Align::Left, FontFace::Body,
                         VAlign::Middle);
                if (l.contains("best") && l["best"].is_object())
                    ui.label("Record " + timeText(jget<int64_t>(l["best"], "time_ms", 0)) + " · " + jstr(l["best"], "player"),
                             {row.right() - ui.s(14), row.y + ui.s(24)}, 14, ui.theme().accent, Align::Right, FontFace::Bold, VAlign::Middle);
                if (tab_ != kFavoritesTab && jstr(l, "status") != "published")
                    ui.label(jstr(l, "status") == "hidden" ? "masqué (signalements)" : "brouillon", {row.right() - ui.s(14), row.y + ui.s(52)}, 14,
                             ui.theme().danger, Align::Right, FontFace::Bold, VAlign::Middle);
            }
            cy += rh + gap;
        }
        if (nextOffset_ > 0) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !busy_;
            if (ui.button("more", {view.center().x - ui.s(150), cy + ui.s(8), ui.s(300), ui.s(42)}, busy_ ? "Chargement…" : "Afficher plus", st)) load(true);
        }
        ui.endScroll();
        if (fromCache_)
            ui.label("Hors ligne : liste en cache", {view.right(), zoneL_.y + ui.s(64) - ui.s(8)}, 13, ui.theme().warm, Align::Right, FontFace::Body,
                     VAlign::Middle);
        ui.popId();
    }

    /// A favorite whose level is gone (removed, hidden or no longer public): kept with the
    /// title and author it had, and can be removed from the favorites.
    void drawUnavailableFavorite(const Json& l, const std::string& id, const Rect& row) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(row, ui.s(8), Color{14, 18, 32, 200});
        ui.r().drawRoundedRect(row, ui.s(8), ui.theme().border.withAlphaF(0.5f), ui.s(1));
        float rh = row.h;
        Rect th{row.x + ui.s(8), row.y + ui.s(8), (rh - ui.s(16)) * 16.0f / 9.0f, rh - ui.s(16)};
        ui.r().fillRoundedRect(th, ui.s(6), Color{10, 12, 22, 255});
        ui.label("indisponible", th.center(), 13, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        float tx = th.right() + ui.s(14);
        float bw = ui.s(200);
        float room = row.right() - tx - bw - ui.s(24);
        std::string title = jstr(l, "title");
        ui.label(title, {tx, row.y + ui.s(24)}, ui.fitFont(title, FontFace::Bold, 20, room), ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string sub = "par " + jstr(l, "author_name") + "  ·  favori " + OnlineUi::ageOf(jstr(l, "favorited_at"));
        ui.label(sub, {tx, row.y + ui.s(52)}, ui.fitFont(sub, FontFace::Body, 15, room), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        std::string why = jstr(l, "unavailable_reason");
        std::string text = why == "deleted" ? "Indisponible : retiré par son auteur"
                         : why == "hidden"  ? "Indisponible : masqué après des signalements"
                                            : "Indisponible : ce niveau n'est plus public";
        ui.label(text, {tx, row.y + ui.s(78)}, ui.fitFont(text, FontFace::Body, 14, room), ui.theme().danger, Align::Left, FontFace::Body, VAlign::Middle);
        UIContext::ButtonStyle d;
        d.small = true;
        d.danger = true;
        if (ui.button("unfav" + id, {row.right() - bw - ui.s(12), row.center().y - ui.s(20), bw, ui.s(40)}, "Retirer des favoris", d)) setFavorite(id, false);
    }

    void drawCreator(const Json& c, const std::string& id, const Rect& row) {
        UIContext& ui = g_.ui;
        if (ui.button("cr" + id, row, "")) g_.app.pushScene(makePlayerProfile(g_, id));
        float av = row.h - ui.s(24);
        ui_.drawAvatar(jstr(c, "avatar", "avatar_01"), id, {row.x + ui.s(12), row.y + ui.s(12), av, av});
        float tx = row.x + av + ui.s(30), room = row.right() - tx - ui.s(16);
        std::string name = jstr(c, "username");
        Vec2 ns = ui.label(name, {tx, row.y + ui.s(28)}, ui.fitFont(name, FontFace::Bold, 21, room * 0.6f), ui.theme().text, Align::Left, FontFace::Bold,
                           VAlign::Middle);
        float cw = ui.text().measure("CRÉATEUR", FontFace::Bold, ui.s(12)).x + ui.s(16);
        Rect chip{tx + ns.x + ui.s(12), row.y + ui.s(28) - ui.s(11), cw, ui.s(22)};
        ui.r().fillRoundedRect(chip, ui.s(11), ui.theme().warm.withAlphaF(0.22f));
        ui.label("CRÉATEUR", chip.center(), 12, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
        if (jget<bool>(c, "following", false))
            ui.label("SUIVI", {row.right() - ui.s(16), row.y + ui.s(28)}, 15, ui.theme().success, Align::Right, FontFace::Bold, VAlign::Middle);
        std::string stats = plural(jget<int>(c, "levels", 0), "niveau publié", "niveaux publiés") + "  ·  " +
                            plural(jget<int>(c, "followers", 0), "abonné", "abonnés") + "  ·  " +
                            plural(jget<int>(c, "downloads", 0), "téléchargement", "téléchargements");
        ui.label(stats, {tx, row.y + ui.s(62)}, ui.fitFont(stats, FontFace::Body, 15, room), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        std::string last = "dernier niveau " + OnlineUi::ageOf(jstr(c, "last_published_at"));
        ui.label(last, {tx, row.y + ui.s(88)}, ui.fitFont(last, FontFace::Body, 14, room), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }

    void drawStatus(const Rect& r) {
        UIContext& ui = g_.ui;
        if (status_.empty() || ui.time() - statusT_ > 8.0f) return;
        ui.labelWrapped(status_, {r.x, r.y}, r.w, 15, statusError_ ? ui.theme().danger : ui.theme().success);
    }

    void drawDetail() {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(zoneD_, ui.s(14), Color{8, 13, 30, 225});
        ui.r().drawRoundedRect(zoneD_, ui.s(14), ui.theme().border, ui.s(1.2f));
        ui.pushId("levels/detail");
        float x = zoneD_.x + ui.s(20), w = zoneD_.w - ui.s(40), y = zoneD_.y + ui.s(18);
        if (selected_.empty() || !detail_.contains("level")) {
            ui.label(selected_.empty() ? "Choisissez un niveau" : "Chargement…", {zoneD_.center().x, zoneD_.y + ui.s(60)}, 20, ui.theme().textDim,
                     Align::Center, FontFace::Bold, VAlign::Middle);
            ui.labelWrapped("Les niveaux sont créés par les joueurs dans le WORKSHOP. Chacun a été terminé par son auteur avant d'être "
                            "publié. Les temps de chaque niveau sont classés après vérification de leur replay.",
                            {x, zoneD_.y + ui.s(100)}, w, 15, ui.theme().textDim);
            drawStatus({x, zoneD_.bottom() - ui.s(70), w, ui.s(60)});
            ui.popId();
            return;
        }
        const Json& l = detail_["level"];
        std::string id = jstr(l, "id");
        Rect th{x, y, w, w * 9.0f / 16.0f};
        ui.r().fillRoundedRect(th, ui.s(8), Color{6, 9, 20, 255});
        if (TexturePtr t = thumbnail(id)) ui.r().drawTexture(*t, nullptr, th);
        y = th.bottom() + ui.s(12);
        std::string title = jstr(l, "title");
        ui.label(title, {x, y + ui.s(14)}, ui.fitFont(title, FontFace::Title, 24, w), ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(34);
        const Json& a = l.value("author", Json::object());
        bool mine = jstr(a, "id") == g_.online.playerId();
        {
            // The author's name opens the creator's profile.
            std::string who = "par " + jstr(a, "username") + (mine ? " (vous)" : "");
            UIContext::ButtonStyle as;
            as.small = true;
            as.fontSize = 15;
            float aw = std::min(w * 0.55f, ui.text().measure(who, FontFace::Bold, ui.s(15)).x + ui.s(24));
            if (ui.button("author", {x, y, aw, ui.s(30)}, who, as) && !jstr(a, "id").empty()) g_.app.pushScene(makePlayerProfile(g_, jstr(a, "id")));
            std::string info = "version " + std::to_string(jget<int>(l, "version", 1)) + "  ·  " + std::to_string(jget<int>(l, "downloads", 0)) +
                               " téléch.  ·  " + plural(jget<int>(l, "completions", 0), "fin", "fins");
            float room = w - aw - ui.s(12);
            ui.label(info, {x + aw + ui.s(12), y + ui.s(15)}, ui.fitFont(info, FontFace::Body, 14, room), ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        }
        y += ui.s(38);
        std::string desc = jstr(l, "description");
        if (!desc.empty()) {
            Vec2 sz = ui.labelWrapped(desc, {x, y}, w, 15, ui.theme().text);
            y += std::min(sz.y, ui.s(80)) + ui.s(8);
        }
        // Play
        int ver = 0;
        bool verified = false;
        std::string local = installedId(id, &ver, &verified);
        int current = jget<int>(l, "version", 1);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !working_;
        std::string playLabel = local.empty() ? "Télécharger et jouer" : (ver == current ? "Jouer" : "Mettre à jour et jouer (v" + std::to_string(current) + ")");
        if (ui.button("play", {x, y, w, ui.s(48)}, working_ ? "Téléchargement…" : playLabel, p)) {
            if (!local.empty() && ver == current) play(local);
            else downloadAndPlay(l);
        }
        y += ui.s(58);
        // Favorites and comments.
        {
            bool fav = jget<bool>(l, "my_favorite", false);
            int favs = jget<int>(l, "favorites", 0), coms = jget<int>(l, "comments", 0);
            float half = (w - ui.s(10)) * 0.5f;
            UIContext::ButtonStyle fs;
            fs.small = true;
            fs.selected = fav;
            fs.primary = fav;
            if (ui.button("favorite", {x, y, half, ui.s(42)}, (fav ? "Dans vos favoris (" : "Ajouter aux favoris (") + std::to_string(favs) + ")", fs))
                setFavorite(id, !fav);
            UIContext::ButtonStyle cs;
            cs.small = true;
            cs.selected = comments_.isOpen() && comments_.levelId() == id;
            if (ui.button("comments", {x + half + ui.s(10), y, half, ui.s(42)}, "Commentaires (" + std::to_string(coms) + ")", cs)) {
                if (cs.selected) comments_.close();
                else comments_.open(id, title);
            }
            y += ui.s(52);
        }
        // Rating
        if (!mine) {
            int myStars = l.contains("my_rating") && l["my_rating"].is_number() ? l["my_rating"].get<int>() : 0;
            ui.label("Votre note", {x, y + ui.s(18)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            for (int s = 1; s <= 5; ++s) {
                UIContext::ButtonStyle st;
                st.small = true;
                st.selected = s <= myStars;
                if (ui.button("star" + std::to_string(s), {x + ui.s(110) + (s - 1) * ui.s(50), y, ui.s(44), ui.s(38)}, std::to_string(s), st)) {
                    g_.online.send("POST", "/api/v1/community/levels/" + id + "/rate", {{"stars", s}}, OnlineService::guard(life_, [this, id](const ApiResult& r) {
                        say(r.ok ? "Merci pour votre note." : r.message, !r.ok);
                        if (r.ok) openLevel(id);
                    }));
                }
            }
            ui.label("/ 5", {x + ui.s(110) + 5 * ui.s(50) + ui.s(4), y + ui.s(19)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(46);
            UIContext::ButtonStyle d;
            d.small = true;
            d.danger = true;
            if (ui.button("report", {x, y, ui.s(160), ui.s(36)}, "Signaler", d)) {
                confirmReport_ = true;
                reportReason_ = 0;
            }
            y += ui.s(48);
        } else {
            UIContext::ButtonStyle d;
            d.small = true;
            d.danger = true;
            if (ui.button("remove", {x, y, w, ui.s(40)}, confirmRemove_ ? "Confirmer : retirer ce niveau d'ONLINE LEVELS" : "Retirer de la publication", d)) {
                if (!confirmRemove_) confirmRemove_ = true;
                else {
                    confirmRemove_ = false;
                    g_.online.send("DELETE", "/api/v1/community/levels/" + id, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                        say(r.ok ? "Niveau retiré." : r.message, !r.ok);
                        if (r.ok) {
                            selected_.clear();
                            load(false);
                        }
                    }));
                }
            }
            y += ui.s(50);
        }
        // Leaderboard of the current version
        const Json& b = detail_.value("leaderboard", Json::object());
        ui.label("Classement (version " + std::to_string(jget<int>(b, "version", current)) + ")", {x, y + ui.s(10)}, 16, ui.theme().accent, Align::Left,
                 FontFace::Bold, VAlign::Middle);
        y += ui.s(26);
        ui.labelWrapped("Temps comptés après vérification du replay (structure, version, durée, trajectoire), non re-simulés.", {x, y}, w, 12,
                        ui.theme().textDim);
        y += ui.s(30);
        Json entries = b.value("entries", Json::array());
        Rect list{x, y, w, zoneD_.bottom() - y - ui.s(76)};
        if (entries.empty()) {
            ui.label("Pas encore de temps classé : à vous !", {x, y + ui.s(14)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            ui.beginScroll("board", list, static_cast<float>(entries.size()) * ui.s(34));
            float ey = list.y - ui.scrollOffset();
            for (const auto& e : entries) {
                bool me = jget<bool>(e, "me", false);
                ui.label("#" + std::to_string(jget<int>(e, "rank", 0)), {x, ey + ui.s(15)}, 15, me ? ui.theme().accent : ui.theme().textDim, Align::Left,
                         FontFace::Bold, VAlign::Middle);
                ui.label(jstr(e["player"], "username"), {x + ui.s(54), ey + ui.s(15)}, 15, me ? ui.theme().accent : ui.theme().text, Align::Left,
                         FontFace::Body, VAlign::Middle);
                ui.label(timeText(jget<int64_t>(e, "time_ms", 0)), {x + w - ui.s(12), ey + ui.s(15)}, 15, ui.theme().text, Align::Right, FontFace::Bold,
                         VAlign::Middle);
                ey += ui.s(34);
            }
            ui.endScroll();
        }
        if (b.contains("me") && b["me"].is_object())
            ui.label("Votre meilleur : rang " + std::to_string(jget<int>(b["me"], "rank", 0)) + " · " + timeText(jget<int64_t>(b["me"], "time_ms", 0)),
                     {x, zoneD_.bottom() - ui.s(64)}, 15, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        drawStatus({x, zoneD_.bottom() - ui.s(46), w, ui.s(40)});
        ui.popId();
    }

    void drawReport() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(320), ui.screenH() * 0.5f - ui.s(150), ui.s(640), ui.s(300)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("levels/report");
        ui.labelWrapped("Signaler ce niveau aux modérateurs ? Après plusieurs signalements, il est masqué en attendant leur décision.",
                        {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 17, ui.theme().text);
        static const std::vector<std::string> reasons = {"Contenu inapproprié", "Niveau impossible ou cassé", "Plagiat", "Autre"};
        ui.cycle("reason", {box.x + ui.s(30), box.y + ui.s(120), box.w - ui.s(60), ui.s(42)}, "Motif", reportReason_, reasons);
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button("ok", {box.x + ui.s(30), box.bottom() - ui.s(70), ui.s(240), ui.s(46)}, "Signaler", d)) {
            confirmReport_ = false;
            std::string id = selected_;
            g_.online.send("POST", "/api/v1/community/levels/" + id + "/report", {{"reason", reasons[static_cast<size_t>(reportReason_)]}},
                           OnlineService::guard(life_, [this](const ApiResult& r) { say(r.ok ? "Niveau signalé. Merci." : r.message, !r.ok); }));
        } else if (ui.button("cancel", {box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) {
            confirmReport_ = false;
        }
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    CommentsPanel comments_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string wanted_;
    bool accountAsked_ = false;
    Rect header_, zoneA_, zoneL_, zoneD_;
    int tab_ = 0;
    int favSort_ = 0, favFilter_ = 0;
    bool searchCreators_ = false;
    std::string query_;
    float searchDirty_ = 0.0f;
    Json levels_ = Json::array();
    int nextOffset_ = -1;
    bool busy_ = false, loaded_ = false, fromCache_ = false;
    int generation_ = 0;
    std::string selected_;
    Json detail_ = Json::object();
    std::map<std::string, TexturePtr> thumbs_;
    bool working_ = false, autoPlay_ = false;
    bool confirmReport_ = false, confirmRemove_ = false;
    int reportReason_ = 0;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeOnlineLevels(GameServices& g, const std::string& communityId) {
    return std::make_unique<OnlineLevelsScene>(g, communityId);
}

}  // namespace cosmic
