// COSMIC ONLINE - ranking screen: navigation (A), interactive globe (B), leaderboards (C),
// selected level and player card (D). Every request goes through OnlineService (network
// thread); this screen only reads results, so it stays fluid whatever the connection.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "engine/core/Easing.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "ui/widgets/ExtremeFrame.h"
#include "ui/online/GlobeRenderer.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/online/RatingWidgets.h"
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/widgets/CosmicStyle.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
#include "ui/screens/OnlineNav.h"
#include "ui/screens/ReplayDownloadUi.h"
#include "ui/screens/Thumbnail.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

constexpr const char* kTabs[] = {"GLOBAL", "AMIS", "PAYS", "PLANÈTE", "NIVEAU"};
enum Tab { TabGlobal = 0, TabFriends, TabCountry, TabPlanet, TabLevel };

/// A level of the ranking list: installed in this game, or only known by the server.
struct RankedLevel {
    std::string id;
    std::string name;
    std::string planet;
    std::string planetName;
    const LevelEntry* entry = nullptr;  // null: ranked online but not installed here
};

struct BoardState {
    bool loading = false;
    bool loaded = false;
    bool fromCache = false;
    double age = 0.0;
    std::string error;
    std::string errorCode;
    Json board;
    Json entries = Json::array();
    Json me;
    int total = 0;
    int nextOffset = -1;
    int generation = 0;
};

/// The « Course à la vérification » of the selected EXTRÊME level (GET /races/board, as its
/// own screen reads it): shown in place of the records on the NIVEAU tab (2.81.2).
struct RaceBoardState {
    bool loading = false;
    bool loaded = false;
    bool fromCache = false;
    double age = 0.0;
    std::string levelId;
    int size = 1;  // 1 Solo (GET /races/board), 2 Duo or 3 Trio (GET /coop/board, 2.62)
    std::string error;
    Json data;
    int generation = 0;
};

class OnlineRankingScene : public Scene {
public:
    OnlineRankingScene(GameServices& g, std::string levelId) : g_(g), ui_(g), levelRating_(g), wantedLevel_(std::move(levelId)) {}

    void onEnter() override {
        backdrop_.start(g_.content);
        g_.playMusic("music/menu_theme.ogg");
        globe_.init(g_.app.resources());
        for (const PlanetData* p : g_.content.planets())
            for (const auto& id : p->levels)
                if (const LevelEntry* e = g_.content.level(id); e && e->source == ContentSource::Builtin)
                    levels_.push_back({e->id, e->name, p->id, p->name, e});
        installedCount_ = levels_.size();
        if (wantedLevel_.empty()) wantedLevel_ = g_.progression.lastLevel;
        selectWanted();
        tab_ = TabLevel;
        g_.ui.setFocus("online/nav/ranking");
        auto changes = g_.online.takeRankChanges();
        if (!changes.empty()) startRankAnimation(changes.back());
        requestAll();
        // ONLINE requires an account (the rest of the game never does).
        if (g_.onlineOpen == "login" || g_.onlineOpen.rfind("register", 0) == 0) {
            accountAsked_ = true;
            g_.app.pushScene(makeOnlineAccount(g_, false, true));
        } else if (g_.onlineOpen == "settings" || g_.onlineOpen == "friends") {
            int tab = g_.onlineOpen == "friends" ? 2 : 0;
            g_.onlineOpen.clear();
            g_.app.pushScene(makeOnlineSettings(g_, tab));
        } else if (g_.onlineOpen.rfind("thread:", 0) == 0 && g_.online.loggedIn()) {
            // A notification of a campaign thread (2.19): "thread:level:<id>" or "thread:run:<record>".
            std::string thread = g_.onlineOpen.substr(7);
            g_.onlineOpen = g_.onlineOpen.find("|replies") != std::string::npos ? "comments-replies" : std::string{};
            thread = thread.substr(0, thread.find('|'));
            if (thread.rfind("level:", 0) == 0 || thread.rfind("run:", 0) == 0)
                g_.app.pushScene(makeOnlineThread(g_, thread, level() ? level()->id : std::string{}));
        }
        requireAccount();
    }

    void onResume() override {
        g_.playMusic("music/menu_theme.ogg");
        accountAsked_ = false;
        requestAll();
        levelRating_.refresh();
    }

    void update(float dt) override {
        t_ += dt;
        backdrop_.update(g_.ui, dt, cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake));
        requireAccount();
        cam_.update(dt);
        idle_ += dt;
        rankAnimT_ += dt;
        shockT_ += dt * 0.8f;
        if (searchDirty_ > 0.0f && (searchDirty_ -= dt) <= 0.0f) requestBoard(false);
        handleGlobeInput(dt);
        // Gentle automatic rotation while nobody touches the globe.
        if (idle_ > 5.0f && selected_ < 0 && !cam_.flying()) cam_.center.lon = wrapLon(cam_.center.lon + dt * 3.5);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        layout();
        {
            const cstyle::Motion mo = cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake);
            backdrop_.render(ui, zoneB_.center(), zoneA_.right() + ui.s(40), cstyle::CosmicLayout::of(ui.screenW(), ui.screenH()).unit, mo);
            ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 90});  // the boards stay readable over it
        }
        // Under the profile (pushed over this screen) nothing here reacts.
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        drawGlobeZone();
        drawHeader();
        drawNavigation();
        drawPlayerCard();
        drawBoardZone();
        drawLevelZone();
        drawRankAnimation();
        ReplayDownloadUi::drawTip(g_);  // the download icons' tooltip, over the rest
        if (top && ui.backPressed()) g_.app.switchScene(makeMainMenu(g_));
        ui.inputEnabled = true;
        if (top) ui.end();
    }

private:
    /// Without an account (never logged in, logged out, session lost) the account screen
    /// opens; leaving it without logging in returns to the main menu.
    void requireAccount() {
        if (g_.online.loggedIn()) {
            accountAsked_ = false;
            return;
        }
        if (accountAsked_ || g_.app.topScene() != this || g_.app.transitioning()) return;
        accountAsked_ = true;
        g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, true));
    }

    // ----------------------------------------------------------------- data
    const RankedLevel* level() const { return levels_.empty() ? nullptr : &levels_[static_cast<size_t>(levelIdx_)]; }

    void selectWanted() {
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i].id == wantedLevel_) levelIdx_ = static_cast<int>(i);
    }

    /// Adds the levels ranked by the server that this installation does not have.
    void mergeServerLevels() {
        bool added = false;
        for (const auto& ml : meta_.value("levels", Json::array())) {
            std::string id = jstr(ml, "id");
            if (id.empty() || !jget<bool>(ml, "ranking_enabled", true)) continue;
            if (std::any_of(levels_.begin(), levels_.end(), [&](const RankedLevel& l) { return l.id == id; })) continue;
            // A copy installed by the player (package, user folder) can still be played and replayed.
            levels_.push_back({id, jstr(ml, "name", id), jstr(ml, "planet_id"), jstr(ml, "planet_name"), g_.content.level(id)});
            added = true;
        }
        if (!added) return;
        std::string current = level() ? level()->id : std::string();
        // The level asked for (command line, last played) may be one of the new ones.
        bool switchTo = !wantedLevel_.empty() && wantedLevel_ != current &&
                        std::any_of(levels_.begin(), levels_.end(), [&](const RankedLevel& l) { return l.id == wantedLevel_; });
        if (!switchTo) wantedLevel_ = current;
        selectWanted();
        if (switchTo) refreshAfterChange();
    }
    RunCategory category() const { return kAllCategories[static_cast<size_t>(category_)]; }
    std::string periodId() const { return period_ == 1 ? "weekly" : "all"; }
    bool levelBoard() const { return tab_ == TabFriends || tab_ == TabCountry || tab_ == TabLevel; }

    std::string myCountry() const { return jstr(g_.online.profile(), "country_code"); }

    std::string boardPath(int offset) const {
        const RankedLevel* l = level();
        std::string cat = categoryId(category());
        std::string q = "?category=" + cat + "&limit=50&offset=" + std::to_string(offset);
        std::string lvl = l ? "&level_id=" + l->id : "";
        switch (tab_) {
            case TabGlobal: return "/api/v1/leaderboards/global" + q;
            case TabFriends: return "/api/v1/leaderboards/friends" + q + lvl + "&period=" + periodId();
            case TabCountry: return "/api/v1/leaderboards/country/" + country_ + q + lvl + "&period=" + periodId();
            case TabPlanet: return "/api/v1/leaderboards/planet/" + (l ? l->planet : std::string("sylvanox")) + q;
            default: {
                std::string p = "/api/v1/leaderboards/level/" + (l ? l->id : std::string()) + q + "&period=" + periodId();
                if (!search_.empty()) p += "&player=" + online::encodeQueryValue(search_);
                return p;
            }
        }
    }

    void requestAll() {
        requestBoard(false);
        requestRace();
        requestGlobe();
        if (!g_.online.usable()) return;
        // The players' rating of the selected level and its number of comments (2.19).
        if (const RankedLevel* l = level(); l && l->id != levelRating_.levelId()) levelRating_.open(l->id);
        g_.online.get("/api/v1/meta", OnlineService::guard(life_, [this](const ApiResult& r) {
            if (!r.ok) return;
            meta_ = r.data;
            mergeServerLevels();
        }));
    }

    void requestBoard(bool more) {
        if (tab_ == TabCountry && country_.empty()) country_ = myCountry().empty() ? "FR" : myCountry();
        int gen = ++board_.generation;
        int offset = more ? std::max(0, board_.nextOffset) : 0;
        if (!more) {
            board_.loading = true;
            board_.error.clear();
        }
        if (tab_ == TabFriends && !g_.online.loggedIn()) {
            board_ = BoardState{};
            board_.generation = gen;
            board_.loaded = true;
            board_.errorCode = "NOT_LOGGED_IN";
            board_.error = "Connectez-vous pour comparer vos temps avec vos amis.";
            return;
        }
        std::string boardKey = (level() ? level()->id : std::string()) + "|" + categoryId(category()) + "|" + std::to_string(tab_);
        // A level whose board holds a verified record is verified (its race is no longer the default view).
        std::string levelOfBoard = tab_ == TabLevel && level() ? level()->id : std::string();
        g_.online.get(boardPath(offset), OnlineService::guard(life_, [this, gen, more, boardKey, levelOfBoard](const ApiResult& r) {
            if (gen != board_.generation) return;  // an older request
            board_.loading = false;
            board_.loaded = true;
            if (!r.ok) {
                board_.error = r.message;
                board_.errorCode = r.code;
                if (!more) {
                    board_.entries = Json::array();
                    board_.me = Json();
                }
                return;
            }
            board_.error.clear();
            board_.fromCache = r.fromCache;
            board_.age = r.cacheAgeSeconds;
            board_.board = r.data.value("board", Json());
            Json entries = r.data.value("entries", Json::array());
            if (more)
                for (auto& e : entries) board_.entries.push_back(e);
            else
                board_.entries = entries;
            board_.me = r.data.value("me", Json());
            if (!levelOfBoard.empty() && !board_.entries.empty()) verifiedLevels_.insert(levelOfBoard);
            board_.total = jget<int>(r.meta, "total", static_cast<int>(board_.entries.size()));
            board_.nextOffset = r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            if (!r.fromCache && !more && board_.me.is_object() && board_.me.contains("entry") && board_.me["entry"].is_object())
                g_.online.observeBoard(boardKey, jget<int>(board_.me["entry"], "rank", 0), board_.total);
            // Automated checks: open a player's profile, replay or run comments from the board.
            for (const char* kind : {"profile:", "replay:", "comments:"}) {
                std::string prefix = kind;
                if (g_.onlineOpen.rfind(prefix, 0) != 0) continue;
                std::string who = g_.onlineOpen.substr(prefix.size());
                for (size_t i = 0; i < board_.entries.size(); ++i) {
                    const Json& e = board_.entries[i];
                    if (jstr(e["player"], "username") != who) continue;
                    g_.onlineOpen.clear();
                    selectedRow_ = static_cast<int>(i);
                    if (prefix == "profile:") openProfile(jstr(e["player"], "id"));
                    else if (prefix == "comments:") openRunComments(e);
                    else g_.app.pushScene(makeReplayViewer(g_, jstr(e, "record_id")));
                    break;
                }
            }
        }));
    }

    void requestGlobe() {
        const RankedLevel* l = level();
        if (!l) return;
        int gen = ++globeGen_;
        globeLoading_ = true;
        int limit = g_.settings.online.bandwidth == 0 ? 150 : 500;
        std::string q = "?level_id=" + l->id + "&category=" + categoryId(category()) + "&period=" + periodId();
        g_.online.get("/api/v1/globe/records" + q + "&limit=" + std::to_string(limit), OnlineService::guard(life_, [this, gen](const ApiResult& r) {
            if (gen != globeGen_) return;
            globeLoading_ = false;
            globeFromCache_ = r.fromCache;
            globeAge_ = r.cacheAgeSeconds;
            globeError_ = r.ok ? std::string() : r.message;
            if (!r.ok) {
                markers_.clear();
                return;
            }
            std::string keepRecord = selected_ >= 0 && selected_ < static_cast<int>(markers_.size()) ? markers_[static_cast<size_t>(selected_)].recordId : "";
            markers_.clear();
            selected_ = -1;
            for (const auto& m : r.data.value("records", Json::array()))
                if (auto mk = GlobeMarker::fromJson(m)) markers_.push_back(*mk);
            for (size_t i = 0; i < markers_.size(); ++i)
                if (!keepRecord.empty() && markers_[i].recordId == keepRecord) selected_ = static_cast<int>(i);
            // Automated checks (--online-focus <pseudonym>): select that player's marker.
            if (!g_.onlineFocus.empty())
                for (size_t i = 0; i < markers_.size(); ++i)
                    if (markers_[i].playerName == g_.onlineFocus) {
                        g_.onlineFocus.clear();
                        selectMarker(static_cast<int>(i));
                        break;
                    }
        }));
    }

    void refreshAfterChange() {
        board_.entries = Json::array();
        board_.me = Json();
        selectedRow_ = -1;
        requestAll();
    }

    // --------------------------------------------------------------- layout
    void layout() {
        UIContext& ui = g_.ui;
        float W = ui.screenW(), H = ui.screenH();
        float m = ui.s(22);
        header_ = {0, 0, W, ui.s(64)};
        zoneA_ = {m, header_.bottom() + ui.s(8), ui.s(272), H - header_.bottom() - ui.s(8) - m};
        zoneC_ = {W - m - ui.s(460), zoneA_.y, ui.s(460), zoneA_.h};
        float bx = zoneA_.right() + ui.s(18);
        float bw = zoneC_.x - ui.s(18) - bx;
        zoneD_ = {bx, H - m - ui.s(252), bw, ui.s(252)};
        zoneB_ = {bx, zoneA_.y, bw, zoneD_.y - ui.s(14) - zoneA_.y};
        globeView_.center = zoneB_.center();
        globeView_.baseRadius = std::min(zoneB_.w, zoneB_.h) * 0.42f;
        statusRect_ = {zoneB_.x, zoneB_.bottom() - ui.s(30), zoneB_.w, ui.s(28)};
    }

    // ---------------------------------------------------------------- globe
    void handleGlobeInput(float dt) {
        if (g_.app.topScene() != this || g_.ui.editingText()) return;
        InputManager& in = g_.app.input();
        Vec2 mp = in.mousePos();
        bool overUi = infoRect_.contains(mp) || globeButtons_.contains(mp);
        bool click = GlobeRenderer::handleInput(in, globeInput_, cam_, globeView_, zoneB_, overUi, dt, g_.ui.s(5));
        if (globeInput_.touched) idle_ = 0.0f;
        if (click) clickGlobe(mp);
        hoverCluster_ = zoneB_.contains(mp) && !overUi ? pickCluster(clusters_, mp, pickRadius()) : -1;
    }

    /// Pointer distance at which a marker is hovered / clicked: its dot and a margin.
    float pickRadius() const { return std::max(g_.ui.s(16), 4.6f * markerScale(g_.ui.scale(), cam_.zoom) + g_.ui.s(7)); }

    void clickGlobe(Vec2 mp) {
        idle_ = 0.0f;
        int c = pickCluster(clusters_, mp, pickRadius());
        if (c >= 0) {
            const GlobeCluster& cl = clusters_[static_cast<size_t>(c)];
            if (cl.single() || cam_.zoom >= GlobeCamera::kMaxZoom * 0.95) {
                selectMarker(cl.members.front());
            } else {
                // A group: zoom in on it.
                const GlobeMarker& m = markers_[static_cast<size_t>(cl.members.front())];
                cam_.flyTo(m.location, std::min(GlobeCamera::kMaxZoom, cam_.zoom * 2.2), 0.9f);
            }
            g_.uiSound("ui_move");
            return;
        }
        // A click on a country filters the ranking by this country.
        if (auto geo = unprojectScreen(cam_, globeView_, mp)) {
            int idx = ui_.countryMap().indexAt(*geo);
            if (const CountryInfo* ci = ui_.countries().byIndex(idx)) {
                selected_ = -1;
                filterCountry(ci->code, false);
                g_.uiSound("ui_confirm");
                return;
            }
        }
        selected_ = -1;
    }

    void selectMarker(int index) {
        if (index < 0 || index >= static_cast<int>(markers_.size())) return;
        selected_ = index;
        selectT_ = 0.0f;
        const GlobeMarker& m = markers_[static_cast<size_t>(index)];
        cam_.flyTo(m.display, std::max(cam_.zoom, 1.6), 1.1f);
    }

    void filterCountry(const std::string& code, bool fly) {
        country_ = code;
        tab_ = TabCountry;
        requestBoard(false);
        if (fly)
            if (const CountryInfo* ci = ui_.countries().find(code); ci && ci->center) cam_.flyTo(*ci->center, std::max(cam_.zoom, 1.5), 1.0f);
    }

    void drawGlobeZone() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.pushClip(zoneB_);
        GeoPoint sun = subsolarPoint(std::chrono::system_clock::now());
        globe_.renderBackdrop(r, globeView_, cam_, t_);
        globe_.renderEarth(r, globeView_, cam_, sun);
        globe_.renderAtmosphere(r, globeView_, cam_, sun, t_);
        // Markers: layout at the current zoom, clustering, then drawing back to front. Their
        // size follows the screen and the zoom; the spacing keeps two dots apart.
        const float ms = markerScale(ui.scale(), cam_.zoom), dotR = 4.6f * ms;
        layoutMarkers(markers_, globeView_.radius(cam_), dotR * 2.8f, cam_.zoom >= 1.8);
        clusters_ = clusterMarkers(markers_, cam_, globeView_, std::max(dotR * 2.6f, ui.s(cam_.zoom > 2.5 ? 10.0f : 18.0f)),
                                   g_.settings.online.bandwidth == 0 ? 120 : 400);
        std::vector<size_t> order(clusters_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return clusters_[a].depth < clusters_[b].depth; });
        std::vector<Rect> taken;
        std::vector<float> radii(clusters_.size(), 0.0f);
        for (size_t i : order) {
            const GlobeCluster& c = clusters_[i];
            const GlobeMarker& best = markers_[static_cast<size_t>(c.members.front())];
            bool sel = std::find(c.members.begin(), c.members.end(), selected_) != c.members.end();
            bool me = std::any_of(c.members.begin(), c.members.end(), [&](int k) { return markers_[static_cast<size_t>(k)].isMe; });
            float pulse = std::sin(t_ * 2.4f + static_cast<float>(i) * 0.7f) * 0.5f + 0.5f;
            float scale = ms * (c.single() ? 1.0f : 1.0f + std::min(0.6f, 0.12f * std::log2(static_cast<float>(c.members.size()))));
            if (static_cast<int>(i) == hoverCluster_) scale *= 1.25f;
            float fade = markerFade(c.depth);
            // Room taken by the marker: its dot and the rings of « me » / of the selection.
            radii[i] = globe_.renderMarker(r, c.screen, markerTier(best.rank), scale, pulse, sel, me, fade) + (sel ? 11.0f : me ? 5.0f : 0.0f) * scale;
            float rr = radii[i] + ui.s(2);
            taken.push_back({c.screen.x - rr, c.screen.y - rr, rr * 2, rr * 2});
        }
        // Size of the groups: a measured pill beside the dot, the biggest groups first; one
        // that would cover a dot, another pill or a control is left to the tooltip.
        taken.push_back(globeButtons_);
        if (infoRect_.w > 0.0f) taken.push_back(infoRect_);  // the window of the selected marker (last frame)
        std::vector<size_t> bySize(order);
        std::stable_sort(bySize.begin(), bySize.end(), [&](size_t a, size_t b) { return clusters_[a].members.size() > clusters_[b].members.size(); });
        Rect pillZone{zoneB_.x + ui.s(6), zoneB_.y + ui.s(6), zoneB_.w - ui.s(12), statusRect_.y - zoneB_.y - ui.s(10)};
        for (size_t i : bySize) {
            const GlobeCluster& c = clusters_[i];
            if (c.single()) continue;
            Color col = GlobeRenderer::tierColor(markerTier(markers_[static_cast<size_t>(c.members.front())].rank));
            // Placed beyond the dot's own rectangle (which is in `taken`), never over it.
            GlobeRenderer::renderCount(ui, c.screen, radii[i], static_cast<int>(c.members.size()), col, markerFade(c.depth), pillZone, &taken);
        }
        // Glow of a new record of the player at their country.
        if (shockT_ < 1.0f && !shockCountry_.empty())
            if (const CountryInfo* ci = ui_.countries().find(shockCountry_); ci && ci->center) {
                Projected p = projectGeo(cam_, globeView_, *ci->center);
                if (p.visible()) GlobeRenderer::renderShockwave(r, p.screen, shockT_, Color{255, 206, 84, 255});
            }
        r.popClip();
        drawGlobeOverlay();
    }

    void drawGlobeOverlay() {
        UIContext& ui = g_.ui;
        infoRect_ = {};
        // Hovered marker: name and time.
        if (hoverCluster_ >= 0 && hoverCluster_ < static_cast<int>(clusters_.size()) && selected_ < 0) {
            const GlobeCluster& c = clusters_[static_cast<size_t>(hoverCluster_)];
            const GlobeMarker& m = markers_[static_cast<size_t>(c.members.front())];
            bool oneCountry = std::all_of(c.members.begin(), c.members.end(),
                                          [&](int k) { return markers_[static_cast<size_t>(k)].countryCode == m.countryCode; });
            std::string where = oneCountry && !m.countryCode.empty() ? ui_.countryName(m.countryCode) + " · " : std::string();
            std::string t = c.single() ? ui_.displayName(m.playerName, m.rank, m.isMe) + " · " + formatRunTime(m.timeMs)
                                       : where + std::to_string(c.members.size()) + " joueurs · meilleur n°" + std::to_string(m.rank);
            // Measured, beside the dot, kept inside the globe zone.
            float fs = ui.fitFont(t, FontFace::Body, 17, zoneB_.w - ui.s(40));
            Vec2 sz = ui.text().measure(t, FontFace::Body, ui.s(fs));
            float off = pickRadius();
            Rect tip{c.screen.x + off, c.screen.y - off - ui.s(28), sz.x + ui.s(18), ui.s(28)};
            if (tip.right() > zoneB_.right() - ui.s(6)) tip.x = c.screen.x - off - tip.w;
            tip.x = std::clamp(tip.x, zoneB_.x + ui.s(6), std::max(zoneB_.x + ui.s(6), zoneB_.right() - ui.s(6) - tip.w));
            tip.y = std::clamp(tip.y, zoneB_.y + ui.s(6), zoneB_.bottom() - ui.s(6) - tip.h);
            ui.r().fillRoundedRect(tip, ui.s(6), Color{8, 12, 26, 235});
            ui.r().drawRoundedRect(tip, ui.s(6), ui.theme().border, ui.s(1));
            ui.label(t, {tip.x + ui.s(9), tip.center().y}, fs, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        }
        // Information window of the selected marker.
        if (selected_ >= 0 && selected_ < static_cast<int>(markers_.size())) {
            selectT_ += g_.app.frameTime() / 1000.0f;
            const GlobeMarker& m = markers_[static_cast<size_t>(selected_)];
            Projected p = projectGeo(cam_, globeView_, m.display);
            float a = std::clamp(selectT_ * 4.0f, 0.0f, 1.0f);
            float w = ui.s(300), h = ui.s(232);
            const bool shown = markerFade(p.depth) > 0.0f;  // on the hidden side: no leader line
            Vec2 anchor = shown ? p.screen : globeView_.center;
            Rect box{anchor.x + ui.s(26), anchor.y - h - ui.s(10), w, h};
            box.x = std::clamp(box.x, zoneB_.x + ui.s(8), zoneB_.right() - w - ui.s(8));
            box.y = std::clamp(box.y, zoneB_.y + ui.s(8), zoneB_.bottom() - h - ui.s(8));
            box.y += (1.0f - a) * ui.s(12);
            infoRect_ = box;
            Color tier = GlobeRenderer::tierColor(markerTier(m.rank));
            ui.r().fillRoundedRect(box, ui.s(12), Color{8, 13, 30, static_cast<uint8_t>(235 * a)});
            ui.r().drawRoundedRect(box, ui.s(12), tier.withAlphaF(0.8f * a), ui.s(1.5f));
            if (shown) ui.r().drawLine(anchor, {box.x, box.bottom() - ui.s(20)}, tier.withAlphaF(0.6f * a), ui.s(1.5f));
            ui_.drawAvatar(m.avatar, m.playerId, {box.x + ui.s(14), box.y + ui.s(14), ui.s(56), ui.s(56)});
            ui.label(ui_.displayName(m.playerName, m.rank, m.isMe), {box.x + ui.s(82), box.y + ui.s(30)}, 22, ui.theme().text,
                     Align::Left, FontFace::Bold, VAlign::Middle);
            ui_.drawFlag(m.countryCode, {box.x + ui.s(82), box.y + ui.s(48), ui.s(24), ui.s(18)});
            ui.label(ui_.countryName(m.countryCode), {box.x + ui.s(112), box.y + ui.s(57)}, 16, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            float y = box.y + ui.s(90);
            auto row = [&](const std::string& k, const std::string& v, Color c) {
                ui.label(k, {box.x + ui.s(16), y}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                ui.label(v, {box.right() - ui.s(16), y}, 17, c, Align::Right, FontFace::Bold, VAlign::Middle);
                y += ui.s(23);
            };
            row("Rang mondial", "n°" + std::to_string(m.rank), tier);
            row("Temps", formatRunTime(m.timeMs), ui.theme().text);
            std::string levelName = m.levelId;
            for (const auto& rl : levels_)
                if (rl.id == m.levelId) levelName = rl.name;
            row("Niveau", levelName, ui.theme().text);
            row("Date", m.achievedAt.substr(0, 10), ui.theme().text);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            float bw = (w - ui.s(40)) / 2;
            ui.pushId("online/info");
            if (ui.button("profile", {box.x + ui.s(14), box.bottom() - ui.s(44), bw, ui.s(34)}, "Profil", st)) openProfile(m.playerId);
            if (ui.button("country", {box.x + ui.s(26) + bw, box.bottom() - ui.s(44), bw, ui.s(34)}, "Filtrer le pays", st))
                filterCountry(m.countryCode, false);
            ui.popId();
        }
        // Globe controls (also reachable with the gamepad).
        UIContext::ButtonStyle st;
        st.small = true;
        float bw = ui.s(44);
        globeButtons_ = {zoneB_.right() - bw - ui.s(10), zoneB_.y + ui.s(10), bw, ui.s(44) * 4 + ui.s(18)};
        ui.pushId("online/globe");
        float y = globeButtons_.y;
        if (ui.button("zoomin", {globeButtons_.x, y, bw, bw}, "+", st)) cam_.zoomBy(1.3);
        y += bw + ui.s(6);
        if (ui.button("zoomout", {globeButtons_.x, y, bw, bw}, "−", st)) cam_.zoomBy(1.0 / 1.3);
        y += bw + ui.s(6);
        if (ui.button("next", {globeButtons_.x, y, bw, bw}, "»", st) && !markers_.empty()) {
            int n = static_cast<int>(markers_.size());
            selectMarker((selected_ + 1 + n) % n);
        }
        y += bw + ui.s(6);
        if (ui.button("home", {globeButtons_.x, y, bw, bw}, "", st)) {
            selected_ = -1;
            cam_.flyTo({25.0, cam_.center.lon}, 1.0, 0.8f);
        }
        {
            // "Recentre" icon: a small target.
            Vec2 c{globeButtons_.x + bw * 0.5f, y + bw * 0.5f};
            ui.r().drawRing(c, ui.s(9), ui.s(2), ui.theme().text);
            ui.r().fillCircle(c, ui.s(3), ui.theme().text);
        }
        ui.popId();
        // State of the data shown on the globe.
        std::string status;
        if (!g_.online.usable()) status = onlineStateLabel(g_.online.state());
        else if (globeLoading_ && markers_.empty()) status = "Chargement des records…";
        else if (!globeError_.empty()) status = "Globe indisponible : " + globeError_;
        else if (globeFromCache_) status = "Hors ligne · données " + OnlineUi::ageText(globeAge_);
        else if (markers_.empty()) status = "Aucun joueur visible sur le globe pour ce classement";
        else status = std::to_string(markers_.size()) + " record" + (markers_.size() > 1 ? "s" : "") + " sur le globe";
        // One row: the state at the left, the privacy note at the right when both fit side by side.
        const char* note = "Positions approximatives : centre du pays";
        const float rowW = statusRect_.w - ui.s(24), noteW = ui.text().measure(note, FontFace::Body, ui.s(14)).x;
        const float statusW = ui.text().measure(status, FontFace::Body, ui.s(16)).x;
        const bool withNote = statusW + ui.s(24) + noteW <= rowW;
        float sfs = ui.fitFont(status, FontFace::Body, 16, withNote ? rowW - noteW - ui.s(24) : rowW);
        ui.label(ui.elide(status, FontFace::Body, sfs, withNote ? rowW - noteW - ui.s(24) : rowW), {statusRect_.x + ui.s(12), statusRect_.center().y}, sfs,
                 globeFromCache_ || !globeError_.empty() ? ui.theme().warm : ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        if (withNote)
            ui.label(note, {statusRect_.right() - ui.s(12), statusRect_.center().y}, 14, ui.theme().textDim.withAlphaF(0.8f), Align::Right, FontFace::Body,
                     VAlign::Middle);
    }

    // --------------------------------------------------------------- header
    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(header_, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = drawOnlineTitle(g_, {ui.s(32), header_.center().y});
        ui.label("Classements mondiaux", {ui.s(32) + tsz.x + ui.s(18), header_.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        ui_.drawConnection({zoneC_.x - ui.s(250), header_.center().y});
        UIContext::ButtonStyle st;
        st.small = true;
        ui.pushId("online/header");
        st.fontSize = 18;
        // Friend requests and messages are shown on the SOCIAL item of the navigation.
        std::string settingsLabel = "Paramètres en ligne";
        float w = ui.text().measure(settingsLabel, FontFace::Bold, ui.s(18)).x + ui.s(36);
        if (ui.button("settings", {zoneC_.right() - w, header_.center().y - ui.s(19), w, ui.s(38)}, settingsLabel, st))
            g_.app.pushScene(makeOnlineSettings(g_, 0));
        if (g_.online.usable() && g_.online.unsentCount() + g_.online.pendingVerificationCount() > 0 && g_.online.loggedIn()) {
            if (ui.button("sync", {zoneC_.right() - w * 2 - ui.s(10), header_.center().y - ui.s(19), w, ui.s(38)},
                          g_.online.syncing() ? "Synchronisation…" : "Synchroniser", st))
                g_.online.syncNow();
        }
        ui.popId();
    }

    // ----------------------------------------------------------- navigation
    void drawNavigation() { drawOnlineNav(g_, zoneA_, OnlineSection::Ranking); }

    void drawPlayerCard() {
        UIContext& ui = g_.ui;
        Rect card{zoneA_.x, zoneA_.bottom() - ui.s(196), zoneA_.w, ui.s(196)};
        ui.r().fillRoundedRect(card, ui.s(14), Color{10, 16, 34, 215});
        ui.r().drawRoundedRect(card, ui.s(14), ui.theme().border, ui.s(1.2f));
        ui.pushId("online/card");
        if (!g_.online.loggedIn()) {
            ui_.drawAvatar("avatar_01", "", {card.x + ui.s(14), card.y + ui.s(14), ui.s(56), ui.s(56)});
            ui.label("Invité", {card.x + ui.s(82), card.y + ui.s(30)}, 21, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label("Records locaux", {card.x + ui.s(82), card.y + ui.s(56)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.labelWrapped("Un compte (facultatif) envoie vos temps dans les classements mondiaux.", {card.x + ui.s(14), card.y + ui.s(82)},
                            card.w - ui.s(28), 14, ui.theme().textDim);
            UIContext::ButtonStyle st;
            st.primary = true;
            st.small = true;
            st.fontSize = 17;
            if (ui.button("login", {card.x + ui.s(14), card.bottom() - ui.s(50), card.w - ui.s(28), ui.s(38)}, "Connexion / inscription", st))
                g_.app.pushScene(makeOnlineAccount(g_, false));
        } else {
            const Json& p = g_.online.profile();
            ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), g_.online.playerId(), {card.x + ui.s(14), card.y + ui.s(14), ui.s(56), ui.s(56)});
            ui.label(g_.online.username(), {card.x + ui.s(82), card.y + ui.s(28)}, 21, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            std::string cc = jstr(p, "country_code");
            if (!cc.empty()) {
                ui_.drawFlag(cc, {card.x + ui.s(82), card.y + ui.s(46), ui.s(22), ui.s(16)});
                ui.label(ui_.countryName(cc), {card.x + ui.s(110), card.y + ui.s(54)}, 15, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
            } else {
                ui.label("Pays non renseigné", {card.x + ui.s(82), card.y + ui.s(54)}, 15, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
            }
            ui_.drawConnection({card.x + ui.s(20), card.y + ui.s(92)}, card.right() - ui.s(14) - (card.x + ui.s(20)));
            int totalLevels = static_cast<int>(installedCount_);
            int planetsDone = 0;
            for (const PlanetData* pl : g_.content.planets()) planetsDone += g_.progression.planetCompleted(*pl, g_.content) ? 1 : 0;
            ui.label(std::to_string(std::min(g_.progression.completedCount(), totalLevels)) + "/" + std::to_string(totalLevels) + " niveaux · " +
                         std::to_string(planetsDone) + "/" + std::to_string(g_.content.planets().size()) + " planètes terminés",
                     {card.x + ui.s(14), card.y + ui.s(120)}, 15, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
            int unsent = g_.online.unsentCount(), pending = g_.online.pendingVerificationCount();
            std::string q = unsent > 0 ? std::to_string(unsent) + " envoi(s) en attente" : pending > 0 ? std::to_string(pending) + " record(s) en vérification" : "Tout est synchronisé";
            ui.label(q, {card.x + ui.s(14), card.y + ui.s(144)}, 15, unsent + pending > 0 ? ui.theme().warm : ui.theme().success, Align::Left,
                     FontFace::Body, VAlign::Middle);
            UIContext::ButtonStyle st;
            st.small = true;
            if (ui.button("me", {card.x + ui.s(14), card.bottom() - ui.s(40), card.w - ui.s(28), ui.s(30)}, "Mon profil public", st))
                openProfile(g_.online.playerId());
        }
        ui.popId();
    }

    // ---------------------------------------------------------------- board
    void drawBoardZone() {
        UIContext& ui = g_.ui;
        ui.panel(zoneC_, false, 14);
        ui.pushId("online/board");
        int tab = tab_;
        std::vector<std::string> labels(std::begin(kTabs), std::end(kTabs));
        if (ui.tabs("tabs", {zoneC_.x + ui.s(10), zoneC_.y + ui.s(10), zoneC_.w - ui.s(20), ui.s(40)}, labels, tab) && tab != tab_) {
            tab_ = tab;
            selectedRow_ = -1;
            requestBoard(false);
            requestRace();
        }
        float y = zoneC_.y + ui.s(60);
        float x = zoneC_.x + ui.s(12), w = zoneC_.w - ui.s(24);
        // Filters. An EXTRÊME level (NIVEAU tab) offers its race first, as one more category:
        // the race has no period, its place holds Solo / Duo / Trio (coop EXTRÊME: 2 or 3 players).
        const bool raceLvl = raceLevel(), race = showRace();
        std::vector<std::string> cats;
        if (raceLvl) cats.push_back("Course à la vérification");
        for (RunCategory c : kAllCategories) cats.push_back(categoryLabel(c));
        const int first = raceLvl ? 1 : 0;
        int cat = race ? 0 : category_ + first;
        float half = levelBoard() ? w * 0.6f : w;
        if (ui.cycle("category", {x, y, half - ui.s(6), ui.s(36)}, "", cat, cats) && cat != (race ? 0 : category_ + first)) {
            if (raceLvl) {
                raceView_[level()->id] = cat == 0;
                selectedRow_ = -1;
            }
            if (cat >= first && cat - first != category_) {
                category_ = cat - first;
                refreshAfterChange();
            }
        }
        if (race) {
            int sz = raceSize_ - 1;
            if (ui.cycle("racesize", {x + half, y, w - half, ui.s(36)}, "", sz, {"Solo", "Duo", "Trio"}) && sz + 1 != raceSize_) {
                raceSize_ = sz + 1;
                selectedRow_ = -1;
                requestRace();
            }
        } else if (levelBoard()) {
            int per = period_;
            if (ui.cycle("period", {x + half, y, w - half, ui.s(36)}, "", per, {"Historique", "Semaine"}) && per != period_) {
                period_ = per;
                refreshAfterChange();
            }
        }
        y += ui.s(42);
        if (race) {
            const std::string rule = raceSize_ > 1 ? "Meilleure progression de chaque équipe, sur une seule vie"
                                                   : "Meilleure progression de chaque joueur, sur une seule vie";
            ui.label(rule, {x, y + ui.s(8)}, ui.fitFont(rule, FontFace::Body, 14, w), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            ui.label(categoryRule(category()), {x, y + ui.s(8)}, 14, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        y += ui.s(22);
        if (tab_ == TabLevel) {
            std::string before = search_;
            ui.textField("search", {x, y, w, ui.s(34)}, search_, "Rechercher un pseudonyme…", 20);
            if (search_ != before) searchDirty_ = 0.5f;
            y += ui.s(40);
        } else if (tab_ == TabCountry) {
            ui_.drawFlag(country_, {x, y + ui.s(4), ui.s(32), ui.s(24)});
            ui.label(ui_.countryName(country_), {x + ui.s(40), y + ui.s(16)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            UIContext::ButtonStyle st;
            st.small = true;
            if (!myCountry().empty() && country_ != myCountry() &&
                ui.button("mycountry", {zoneC_.right() - ui.s(150), y, ui.s(138), ui.s(32)}, "Mon pays", st)) {
                country_ = myCountry();
                requestBoard(false);
            }
            y += ui.s(40);
        } else if (tab_ == TabPlanet || tab_ == TabGlobal) {
            std::string what = tab_ == TabPlanet ? "Somme des meilleurs temps sur tous les niveaux de la planète"
                                                 : "Points cosmiques : 1000 × record mondial / votre temps, sur tous les niveaux";
            ui.labelWrapped(what, {x, y}, w, 14, ui.theme().textDim);
            y += ui.s(40);
        }
        // Column headers
        ui.label("#", {x + ui.s(14), y + ui.s(8)}, 14, ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.label(race && raceSize_ > 1 ? "ÉQUIPE" : "JOUEUR", {x + ui.s(84), y + ui.s(8)}, 14, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(race ? "PROGRESSION" : tab_ == TabGlobal ? "POINTS" : "TEMPS", {x + w - ui.s(8), y + ui.s(8)}, 14, ui.theme().textDim, Align::Right,
                 FontFace::Bold, VAlign::Middle);
        y += ui.s(18);
        float footerH = ui.s(84);
        Rect list{x, y, w, zoneC_.bottom() - footerH - y};
        if (race) {
            drawRaceEntries(list);
            drawRaceFooter({zoneC_.x, zoneC_.bottom() - footerH, zoneC_.w, footerH});
        } else {
            drawEntries(list);
            drawBoardFooter({zoneC_.x, zoneC_.bottom() - footerH, zoneC_.w, footerH});
        }
        ui.popId();
    }

    // ------------------------------------------------- the race of an EXTRÊME level
    /// The NIVEAU tab on an EXTRÊME level: its « Course à la vérification » can replace the records.
    bool raceLevel() {
        const RankedLevel* l = level();
        return tab_ == TabLevel && l && isExtreme(*l);
    }

    /// The race is shown: the player's choice for this level, else while the level is not
    /// verified (no verifier and no verified record seen); a verified level shows its records.
    bool showRace() {
        if (!raceLevel()) return false;
        const std::string& id = level()->id;
        if (auto it = raceView_.find(id); it != raceView_.end()) return it->second;
        return verifiedLevels_.count(id) == 0;
    }

    /// The same request as the race's own screen (official board of this game's version of the level).
    void requestRace() {
        const RankedLevel* l = level();
        if (!l || tab_ != TabLevel || !isExtreme(*l) || !g_.online.usable()) return;
        if (race_.levelId != l->id || race_.size != raceSize_) {
            const int keep = race_.generation;
            race_ = RaceBoardState{};
            race_.generation = keep;
            race_.levelId = l->id;
            race_.size = raceSize_;
        }
        int gen = ++race_.generation;
        race_.loading = true;
        std::string q = raceSize_ > 1 ? "/api/v1/coop/board?size=" + std::to_string(raceSize_) + "&kind=official&level_id=" + l->id
                                      : "/api/v1/races/board?kind=official&level_id=" + l->id;
        if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, l->id); fp && !fp->hash.empty()) q += "&level_hash=" + fp->hash;
        g_.online.get(q, OnlineService::guard(life_, [this, gen, id = l->id](const ApiResult& r) {
            if (gen != race_.generation) return;
            race_.loading = false;
            race_.loaded = true;
            if (!r.ok) {
                race_.error = r.message;
                return;
            }
            race_.error.clear();
            race_.fromCache = r.fromCache;
            race_.age = r.cacheAgeSeconds;
            race_.data = r.data;
            if (jfield(r.data, "verifier").is_object()) verifiedLevels_.insert(id);
        }));
    }

    void drawRaceEntries(const Rect& list) {
        UIContext& ui = g_.ui;
        if (drawUnusable(list)) return;
        const bool current = raceCurrent();
        if (!current) requestRace();  // never asked for this level yet (asked once: it records the level)
        if (!current || (race_.loading && !race_.data.is_object())) {
            drawSkeleton(list);
            return;
        }
        if (!race_.data.is_object()) {
            drawEmpty(list, "Classement indisponible", race_.error.empty() ? std::string("Classement indisponible.") : race_.error);
            UIContext::ButtonStyle st;
            st.small = true;
            if (ui.button("raceretry", {list.center().x - ui.s(90), list.center().y + ui.s(60), ui.s(180), ui.s(38)}, "Réessayer", st)) requestRace();
            return;
        }
        // Every ranked player (the server's best attempt of each); the search filters them here.
        std::vector<const Json*> rows;
        const auto rk = race_.data.find("ranking");
        const bool any = rk != race_.data.end() && rk->is_array() && !rk->empty();
        if (any)
            for (const auto& e : *rk)
                if (raceSize_ > 1 ? teamMatchesSearch(e) : matchesSearch(jstr(jfield(e, "player", Json::object()), "username"))) rows.push_back(&e);
        if (!any) {
            drawEmpty(list, "Personne n'est encore classé",
                      raceSize_ == 2   ? "Aucune équipe de deux n'est encore classée sur ce niveau."
                      : raceSize_ == 3 ? "Aucune équipe de trois n'est encore classée sur ce niveau."
                                       : "Toute tentative compte, même sans arriver au bout : jouez ce niveau connecté à Cosmic Online.");
            return;
        }
        if (rows.empty()) {
            drawEmpty(list, "Aucun joueur trouvé", "Aucun joueur ne correspond à la recherche.");
            return;
        }
        float rowH = ui.s(46), gap = ui.s(5);
        ui.beginScroll("race", list, static_cast<float>(rows.size()) * (rowH + gap));
        float y = list.y - ui.scrollOffset();
        for (size_t i = 0; i < rows.size(); ++i) {
            Rect row{list.x, y, list.w - ui.s(10), rowH};
            if (row.bottom() >= list.y - rowH && row.y <= list.bottom() + rowH) {
                if (raceSize_ > 1) drawTeamEntry(*rows[i], row, static_cast<int>(i));
                else drawRaceEntry(*rows[i], row, static_cast<int>(i));
            }
            y += rowH + gap;
        }
        ui.endScroll();
    }

    /// Search of a pseudonym in the race (case ignored for the Latin letters).
    bool matchesSearch(const std::string& name) const {
        if (search_.empty()) return true;
        auto lower = [](std::string t) {
            for (char& c : t)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return t;
        };
        return lower(name).find(lower(search_)) != std::string::npos;
    }

    /// A ranked player of the race: rank, avatar, name, then (dimmed) the verifier or a
    /// finished attempt, its time and its date; their best progression at the right.
    void drawRaceEntry(const Json& e, const Rect& row, int index) {
        UIContext& ui = g_.ui;
        const int rank = jget<int>(e, "rank", 0);
        const bool me = jget<bool>(e, "me", false), finished = jget<bool>(e, "finished", false);
        const Json p = jfield(e, "player", Json::object());
        const std::string pid = jstr(p, "id");
        Color accent = rank >= 1 && rank <= 3 ? GlobeRenderer::tierColor(markerTier(rank)) : (me ? ui.theme().violet : Color{0, 0, 0, 0});
        if (ui.listItem("race" + std::to_string(index), row, "", selectedRow_ == index || me, "", accent) && !pid.empty()) {
            selectedRow_ = index;
            openProfile(pid);
        }
        float nx = drawRankAndAvatar(p, rank, row);
        const float rx = row.right() - ui.s(10);
        const std::string value = progressText(e);
        float right = rx - ui.text().measure(value, FontFace::Bold, ui.s(18)).x - ui.s(12);
        right = raceReplayButton(e, row, index, right, nx);
        const float room = std::max(ui.s(40), right - nx);
        const Json ver = jfield(race_.data, "verifier");
        const bool verifier = !pid.empty() && ver.is_object() && jstr(jfield(ver, "player", Json::object()), "id") == pid;
        std::string sub = verifier ? "Vérificateur" : finished ? "Fini" : "";
        appendTimeAndDate(sub, e);
        const float nameY = sub.empty() ? row.center().y : row.center().y - ui.s(8);
        std::string name = ui_.displayName(p, rank, me);
        {
            i18n::Verbatim playerWords(me || g_.settings.online.showPseudonyms);
            float nf = std::max(11.5f / ui.s(1.0f), ui.fitFont(name, FontFace::Bold, sub.empty() ? 18 : 17, room, 0.75f));
            ui.label(ui.elide(name, FontFace::Bold, nf, room), {nx, nameY}, nf, me ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            ui_.clanTagLink("rc" + std::to_string(index), p, name, {nx, nameY}, nf, FontFace::Bold, room);
        }
        if (!sub.empty()) {
            float sf = std::max(12.5f, 11.5f / ui.s(1.0f));
            ui.label(ui.elide(sub, FontFace::Body, sf, room), {nx, row.center().y + ui.s(10)}, sf, verifier ? ui.theme().warm : ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.label(value, {rx, row.center().y}, 18,
                 finished ? ui.theme().success : rank == 1 ? GlobeRenderer::tierColor(MarkerTier::Gold) : ui.theme().text, Align::Right, FontFace::Bold,
                 VAlign::Middle);
    }

    /// 2.84: the ▶ of an attempt of the race (as on the records' rows, lost with the race's panel in 2.82): its replay,
    /// downloaded then played (the race's own screen does the same). Left of `right` when there is room (more than
    /// 120 px after `left`); returns where the row's text must stop.
    float raceReplayButton(const Json& e, const Rect& row, int index, float right, float left) {
        UIContext& ui = g_.ui;
        const std::string runId = jstr(e, "run_id");
        if (!jget<bool>(e, "has_replay", false) || runId.empty() || right - ui.s(48) - left < ui.s(120)) return right;
        const Rect vb{right - ui.s(40), row.center().y - ui.s(14), ui.s(40), ui.s(28)};
        UIContext::ButtonStyle vst;
        vst.small = true;
        vst.enabled = raceFetching_.empty();
        if (ui.button("racerep" + std::to_string(index), vb, "", vst)) {
            raceFetching_ = runId;
            watchRaceAttempt(g_, e, raceSize_, life_, [this](const std::string& m) {
                raceMsg_ = m;
                if (m != "Téléchargement du replay…") raceFetching_.clear();
            });
        }
        const Vec2 c = vb.center();
        if (raceFetching_ == runId)
            ui.label("…", c, 16, ui.theme().accent, Align::Center, FontFace::Bold, VAlign::Middle);
        else
            ui.r().fillTriangle({c.x - ui.s(5), c.y - ui.s(7)}, {c.x - ui.s(5), c.y + ui.s(7)}, {c.x + ui.s(7), c.y},
                                vst.enabled ? ui.theme().accent : ui.theme().textDim);
        return vb.x - ui.s(8);
    }

    /// The board in memory is the selected level's, at the size chosen.
    bool raceCurrent() const { return level() && race_.levelId == level()->id && race_.size == raceSize_; }

    /// A team is found by any of its players.
    bool teamMatchesSearch(const Json& e) const {
        if (search_.empty()) return true;
        const Json players = jfield(e, "players", Json::array());
        if (!players.is_array()) return false;
        return std::any_of(players.begin(), players.end(), [&](const Json& p) { return matchesSearch(jstr(p, "username")); });
    }

    /// « 87,5 % »: the server's score of the attempt.
    static std::string progressText(const Json& e) {
        char pct[32];
        std::snprintf(pct, sizeof pct, "%.1f", jget<double>(e, "score", 0.0));
        std::string value = pct;
        if (auto d = value.find('.'); d != std::string::npos) value[d] = ',';
        return value + " %";
    }

    /// The attempt's time and the day it was verified, when the board has them.
    static void appendTimeAndDate(std::string& sub, const Json& e) {
        if (const int64_t ms = jget<int64_t>(e, "time_ms", 0); ms > 0) sub += (sub.empty() ? "" : " · ") + std::string("temps ") + formatRunTime(ms);
        if (const std::string at = jstr(e, "at"); at.size() >= 10)
            sub += (sub.empty() ? "" : " · ") + at.substr(8, 2) + "/" + at.substr(5, 2) + "/" + at.substr(0, 4);
    }

    /// A team of the Duo or Trio race: rank, its players' names (each one opens that player's profile,
    /// with the mouse, the keyboard or the gamepad), then (dimmed) a finished attempt, the revivals, an
    /// incomplete team, its time and its date; its best progression at the right.
    void drawTeamEntry(const Json& e, const Rect& row, int index) {
        UIContext& ui = g_.ui;
        const int rank = jget<int>(e, "rank", 0);
        const bool me = jget<bool>(e, "me", false), finished = jget<bool>(e, "finished", false);
        // The row is drawn here: its players' names are its buttons.
        const Color accent = rank >= 1 && rank <= 3 ? GlobeRenderer::tierColor(markerTier(rank)) : (me ? ui.theme().violet : Color{0, 0, 0, 0});
        ui.r().fillRoundedRect(row, ui.s(6), me ? Color{18, 48, 74, 230} : Color{14, 22, 44, 200});
        if (accent.a > 0) ui.r().fillRoundedRect({row.x, row.y + ui.s(4), ui.s(4), row.h - ui.s(8)}, ui.s(2), accent);
        drawRankBadge(rank, row);
        const std::string value = progressText(e);
        const float rx = row.right() - ui.s(10);
        const float nx = row.x + ui.s(84);
        const float right = raceReplayButton(e, row, index, rx - ui.text().measure(value, FontFace::Bold, ui.s(18)).x - ui.s(12), nx);
        const float room = std::max(ui.s(40), right - nx);
        const float nameY = row.center().y - ui.s(8);
        // The names: their size at most, else smaller (down to 80 %), else each one shortened to its share.
        struct Name {
            std::string id, shown;
            bool mine = false;
            float w = 0.0f;
        };
        std::vector<Name> names;
        const std::string myId = g_.online.playerId();
        const Json players = jfield(e, "players", Json::array());
        if (players.is_array())
            for (const auto& p : players) {
                Name n;
                n.id = jstr(p, "id");
                n.mine = !myId.empty() && n.id == myId;
                n.shown = ui_.displayName(jstr(p, "username"), 0, n.mine);
                names.push_back(std::move(n));
            }
        float x = nx, left = room;
        const std::string tag = jstr(e, "clan_tag");  // the clan of all its players (else empty)
        if (!tag.empty()) {
            i18n::Verbatim words;  // a tag is never translated
            const std::string t = "[" + tag + "] ";
            const float tw = ui.text().measure(t, FontFace::Bold, ui.s(16)).x;
            if (tw <= room * 0.3f) {
                ui.label(t, {x, nameY}, 16, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
                x += tw;
                left -= tw;
            }
        }
        const std::string sep = " · ";
        const float sepW = ui.text().measure(sep, FontFace::Bold, ui.s(16)).x;
        auto widthOf = [&](const Name& n, float size) {
            i18n::Verbatim words(n.mine || g_.settings.online.showPseudonyms);
            return ui.text().measure(n.shown, FontFace::Bold, ui.s(size)).x;
        };
        float total = sepW * static_cast<float>(std::max<size_t>(names.size(), 1) - 1);
        for (const Name& n : names) total += widthOf(n, 16.0f);
        const float size = total <= left ? 16.0f : std::max(12.8f, 16.0f * left / total);
        const float sepAt = ui.text().measure(sep, FontFace::Bold, ui.s(size)).x;
        const float share = names.empty() ? left : (left - sepAt * static_cast<float>(names.size() - 1)) / static_cast<float>(names.size());
        for (size_t k = 0; k < names.size(); ++k) {
            Name& n = names[k];
            i18n::Verbatim words(n.mine || g_.settings.online.showPseudonyms);
            n.w = widthOf(n, size);
            if (n.w > share) {
                n.shown = ui.elide(n.shown, FontFace::Bold, size, share);
                n.w = std::min(share, ui.text().measure(n.shown, FontFace::Bold, ui.s(size)).x);
            }
            const std::string key = "team" + std::to_string(index) + "/" + std::to_string(k);
            const Rect hit{x, nameY - ui.s(11), std::max(n.w, ui.s(8)), ui.s(22)};
            bool hovered = false;
            if (ui.hotspot(key, hit, &hovered) && !n.id.empty()) {
                selectedRow_ = index;
                openProfile(n.id);
            }
            if (hovered || ui.focused(key)) ui.r().fillRect({hit.x, hit.bottom() - ui.s(2), hit.w, ui.s(2)}, ui.theme().accent);
            ui.label(n.shown, {x, nameY}, size, n.mine ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            x += n.w;
            if (k + 1 < names.size()) {
                ui.label(sep, {x, nameY}, size, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
                x += sepAt;
            }
        }
        const int revives = jget<int>(e, "revives", 0);
        std::string sub = finished ? "Fini" : "";
        sub += (sub.empty() ? "" : " · ") + std::to_string(revives) + (revives > 1 ? " réanimations" : " réanimation");
        if (jget<bool>(e, "incomplete", false)) sub += " · équipe incomplète";
        appendTimeAndDate(sub, e);
        const float sf = std::max(12.5f, 11.5f / ui.s(1.0f));
        ui.label(ui.elide(sub, FontFace::Body, sf, room), {nx, row.center().y + ui.s(10)}, sf,
                 jget<bool>(e, "incomplete", false) ? ui.theme().warm : ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(value, {rx, row.center().y}, 18,
                 finished ? ui.theme().success : rank == 1 ? GlobeRenderer::tierColor(MarkerTier::Gold) : ui.theme().text, Align::Right, FontFace::Bold,
                 VAlign::Middle);
    }

    void drawRaceFooter(const Rect& f) {
        UIContext& ui = g_.ui;
        ui.separator({f.x + ui.s(12), f.y + ui.s(4)}, f.w - ui.s(24));
        const bool current = raceCurrent();
        std::string info;
        Color c = ui.theme().textDim;
        if (current && race_.fromCache) {
            info = "Hors ligne · classement " + OnlineUi::ageText(race_.age) + " (peut être ancien)";
            c = ui.theme().warm;
        } else if (current && race_.data.is_object()) {
            if (raceSize_ > 1) {
                const Json rk = jfield(race_.data, "ranking", Json::array());
                const int n = rk.is_array() ? static_cast<int>(rk.size()) : 0;
                info = std::to_string(n) + (n > 1 ? " équipes" : " équipe") + " · meilleure progression de chacune";
            } else {
                const int n = jget<int>(jfield(race_.data, "race", Json::object()), "participants", 0);
                info = std::to_string(n) + (n > 1 ? " joueurs" : " joueur") + " · meilleure progression de chacun";
            }
        }
        const float room = f.w - ui.s(28);
        if (!info.empty()) {
            float fs = ui.fitFont(info, FontFace::Body, 15, room);
            ui.label(ui.elide(info, FontFace::Body, fs, room), {f.x + ui.s(14), f.y + ui.s(26)}, fs, c, Align::Left, FontFace::Body, VAlign::Middle);
        }
        // Second line, before « Actualiser »: one's attempts still being checked, or how to enter.
        const Rect refresh{f.right() - ui.s(132), f.y + ui.s(40), ui.s(118), ui.s(32)};
        const int pending = current ? jget<int>(race_.data, "my_pending", 0) : 0;
        std::string note;
        if (!g_.online.loggedIn()) note = "Connectez-vous à Cosmic Online pour entrer dans la course.";
        else if (pending > 0 && raceSize_ > 1)
            note = std::to_string(pending) + (pending > 1 ? " tentatives d'équipe en cours de vérification." : " tentative d'équipe en cours de vérification.");
        else if (pending > 0)
            note = std::to_string(pending) + (pending > 1 ? " de vos tentatives sont en cours de vérification." : " de vos tentatives est en cours de vérification.");
        if (!raceMsg_.empty()) note = raceMsg_;  // 2.84: a replay downloading, or why it cannot be watched
        if (!note.empty()) {
            const float nr = refresh.x - ui.s(12) - (f.x + ui.s(14));
            float fs = ui.fitFont(note, FontFace::Body, 15, nr);
            ui.label(ui.elide(note, FontFace::Body, fs, nr), {f.x + ui.s(14), f.y + ui.s(56)}, fs, ui.theme().text, Align::Left, FontFace::Body,
                     VAlign::Middle);
        }
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("refresh", refresh, "Actualiser", st)) requestAll();
    }

    /// A leaderboard player's title (2.23): the name of the catalogue's item, the server's
    /// name for an item this game does not know; "" without a title (or a hidden profile).
    std::string titleOf(const Json& player) const {
        if (!player.contains("title") || !player["title"].is_object()) return {};
        const Json& t = player["title"];
        if (const CosmeticItem* i = g_.cosmetics.find(jstr(t, "id")); i && i->category == "title")
            return i->isDefault ? std::string() : i->name;
        std::string n = jstr(t, "name");  // elided when drawn
        for (char& c : n)
            if (static_cast<unsigned char>(c) < 0x20) c = ' ';
        return n;
    }

    std::string entryValue(const Json& e) const {
        if (e.contains("points")) return std::to_string(jget<int>(e, "points", 0)) + " pts";
        if (e.contains("total_time_ms")) return formatRunTime(jget<int64_t>(e, "total_time_ms", 0));
        return formatRunTime(jget<int64_t>(e, "time_ms", 0));
    }

    void drawEntry(const Json& e, const Rect& row, int index, bool pinned) {
        UIContext& ui = g_.ui;
        int rank = jget<int>(e, "rank", 0);
        bool me = jget<bool>(e, "is_me", false);
        const Json& p = e.contains("player") ? e["player"] : Json::object();
        std::string pid = jstr(p, "id");
        bool sel = selectedRow_ == index && !pinned;
        Color accent = rank >= 1 && rank <= 3 ? GlobeRenderer::tierColor(markerTier(rank)) : (me ? ui.theme().violet : Color{0, 0, 0, 0});
        if (ui.listItem(pinned ? "pinned" : "row" + std::to_string(index), row, "", sel || me, "", accent)) {
            selectedRow_ = pinned ? -1 : index;
            openProfile(pid);
        }
        if (row.bottom() < 0) return;
        float nx = drawRankAndAvatar(p, rank, row);
        std::string cc = jstr(p, "country_code");
        if (!cc.empty()) {
            ui_.drawFlag(cc, {nx, row.center().y - ui.s(8), ui.s(22), ui.s(16)});
            nx += ui.s(28);
        }
        float rx = row.right() - ui.s(10);
        std::string value = entryValue(e);
        float right = rx - ui.text().measure(value, FontFace::Bold, ui.s(18)).x - ui.s(12);
        if (me) {
            ui.label("PB", {right, row.center().y}, 13, ui.theme().violet, Align::Right, FontFace::Bold, VAlign::Middle);
            right -= ui.text().measure("PB", FontFace::Bold, ui.s(13)).x + ui.s(10);
        }
        // Comments of the run (2.19): their number, opens its thread. Null: its author hid
        // their profile (no comments by others).
        if (e.contains("comments") && e["comments"].is_number_integer()) {
            std::string count = std::to_string(e["comments"].get<int>());
            float cw = ui.text().measure(count, FontFace::Bold, ui.s(14)).x + ui.s(40);
            Rect cb{right - cw, row.center().y - ui.s(14), cw, ui.s(28)};
            UIContext::ButtonStyle cst;
            cst.small = true;
            if (ui.button((pinned ? std::string("pinnedcom") : "com" + std::to_string(index)), cb, "", cst)) openRunComments(e);
            // A speech bubble, then the count.
            Rect bubble{cb.x + ui.s(9), cb.center().y - ui.s(7), ui.s(16), ui.s(11)};
            ui.r().drawRoundedRect(bubble, ui.s(3), ui.theme().textDim, ui.s(1.5f));
            ui.r().fillTriangle({bubble.x + ui.s(4), bubble.bottom()}, {bubble.x + ui.s(9), bubble.bottom()}, {bubble.x + ui.s(3), bubble.bottom() + ui.s(5)},
                                ui.theme().textDim);
            ui.label(count, {bubble.right() + ui.s(6), cb.center().y}, 14, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            right = cb.x - ui.s(8);
        }
        // The replay of this record (2.32.2): every ranked player's, not only the selected one.
        if (jget<bool>(e, "has_replay", false) && !jstr(e, "record_id").empty() && right - nx > ui.s(120)) {
            Rect vb{right - ui.s(40), row.center().y - ui.s(14), ui.s(40), ui.s(28)};
            UIContext::ButtonStyle vst;
            vst.small = true;
            if (ui.button((pinned ? std::string("pinnedrep") : "rep" + std::to_string(index)), vb, "", vst))
                g_.app.pushScene(makeReplayViewer(g_, jstr(e, "record_id")));
            Vec2 c = vb.center();
            ui.r().fillTriangle({c.x - ui.s(5), c.y - ui.s(7)}, {c.x - ui.s(5), c.y + ui.s(7)}, {c.x + ui.s(7), c.y}, ui.theme().accent);
            right = vb.x - ui.s(8);
            // 2.45: keep it on this computer, to watch it without a connection.
            if (right - nx > ui.s(120)) {
                Rect db{right - ui.s(40), row.center().y - ui.s(14), ui.s(40), ui.s(28)};
                ReplayDownloadUi::icon(g_, pinned ? std::string("pinneddl") : "dl" + std::to_string(index), db, recordDownload(e, levelBoard() ? level() : nullptr));
                right = db.x - ui.s(8);
            }
        }
        // 2.66: « [TAG] name » (the tag of the player's clan), as the player chose them.
        std::string name = ui_.displayName(p, rank, me);
        float room = std::max(ui.s(40), right - nx);
        // The player's title under the name (2.23), smaller and dimmed.
        std::string title = titleOf(p);
        float nameY = title.empty() ? row.center().y : row.center().y - ui.s(8);
        {
            i18n::Verbatim playerWords(me || g_.settings.online.showPseudonyms);
            float nf = std::max(11.5f / ui.s(1.0f), ui.fitFont(name, FontFace::Bold, title.empty() ? 18 : 17, room, 0.75f));
            ui.label(ui.elide(name, FontFace::Bold, nf, room), {nx, nameY}, nf, me ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            // 2.63.6: the tag opens its clan's sheet.
            ui_.clanTagLink((pinned ? std::string("pinned") : "e" + std::to_string(index)), p, name, {nx, nameY}, nf, FontFace::Bold, room);
        }
        if (!title.empty()) {
            float tf = std::max(12.5f, 11.5f / ui.s(1.0f));  // never under 11.5 px on screen
            // 2.43: a title created on Cosmic Online shows in its colour.
            const Json& tj = jfield(p, "title", Json());
            const std::string tcol = tj.is_object() ? jstr(tj, "color") : std::string();
            ui.label(ui.elide(title, FontFace::Body, tf, room), {nx, row.center().y + ui.s(10)}, tf, tcol.empty() ? ui.theme().textDim : titleColor(tcol),
                     Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.label(value, {rx, row.center().y}, 18, rank == 1 ? GlobeRenderer::tierColor(MarkerTier::Gold) : ui.theme().text, Align::Right,
                 FontFace::Bold, VAlign::Middle);
    }

    void drawRankBadge(int rank, const Rect& row) {
        UIContext& ui = g_.ui;
        Vec2 badge{row.x + ui.s(26), row.center().y};
        if (rank >= 1 && rank <= 3) {
            Color c = GlobeRenderer::tierColor(markerTier(rank));
            ui.r().drawGlow(badge, ui.s(18), c.withAlphaF(0.35f));
            ui.r().fillCircle(badge, ui.s(13), c.withAlphaF(0.9f));
            ui.label(std::to_string(rank), badge, 16, Color{20, 20, 30, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        } else {
            ui.label(std::to_string(rank), badge, 16, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        }
    }

    /// A ranking row's start: the rank (a medal-coloured badge for the top 3), then the
    /// avatar in the player's frame. Returns where the name starts.
    float drawRankAndAvatar(const Json& p, int rank, const Rect& row) {
        UIContext& ui = g_.ui;
        drawRankBadge(rank, row);
        // The avatar in the player's frame (2.21; the simple one for a hidden profile).
        float avs = row.h - ui.s(14);
        Vec2 avc{row.x + ui.s(46) + (row.h - ui.s(10)) * 0.5f, row.center().y};
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), jstr(p, "id"), Rect::fromCenter(avc, {avs, avs}));
        drawAvatarFrame(ui.r(), avc, avs * 0.5f, g_.cosmetics.lookOr("frame", jstr(p, "frame")), ui.s(0.5f), t_);
        return avc.x + avs * 0.5f + ui.s(kAvatarFrameMargin * 0.5f + 6);
    }

    /// Online unusable (no server, disabled, no network): said in place of a ranking.
    bool drawUnusable(const Rect& list) {
        if (g_.online.usable()) return false;
        drawEmpty(list, onlineStateLabel(g_.online.state()),
                  g_.online.state() == OnlineState::NotConfigured
                      ? "Aucun serveur Cosmic Online n'est configuré pour cette version. Vos records restent enregistrés localement."
                      : g_.online.state() == OnlineState::Disabled ? "Activez Cosmic Online dans les paramètres en ligne pour voir les classements."
                                                                   : "Cette version du jeu ne peut pas accéder au réseau.");
        return true;
    }

    /// Rows pulsing while a ranking loads.
    void drawSkeleton(const Rect& list) {
        UIContext& ui = g_.ui;
        float rowH = ui.s(46), gap = ui.s(5);
        float a = 0.5f + 0.5f * std::sin(t_ * 5.0f);
        for (int i = 0; i < 6; ++i)
            ui.r().fillRoundedRect({list.x, list.y + i * (rowH + gap), list.w, rowH}, ui.s(6), Color{20, 30, 56, static_cast<uint8_t>(80 + 60 * a)});
    }

    void drawEntries(const Rect& list) {
        UIContext& ui = g_.ui;
        float rowH = ui.s(46), gap = ui.s(5);
        if (drawUnusable(list)) return;
        if (board_.loading && board_.entries.empty()) {
            drawSkeleton(list);
            return;
        }
        if (!board_.error.empty() && board_.entries.empty()) {
            drawEmpty(list, board_.errorCode == "NOT_LOGGED_IN" ? "Non connecté" : "Classement indisponible", board_.error);
            UIContext::ButtonStyle st;
            st.small = true;
            if (board_.errorCode == "NOT_LOGGED_IN") {
                if (ui.button("login", {list.center().x - ui.s(110), list.center().y + ui.s(60), ui.s(220), ui.s(38)}, "Se connecter", st))
                    g_.app.pushScene(makeOnlineAccount(g_, false));
            } else if (ui.button("retry", {list.center().x - ui.s(90), list.center().y + ui.s(60), ui.s(180), ui.s(38)}, "Réessayer", st)) {
                requestAll();
            }
            return;
        }
        if (board_.entries.empty()) {
            drawEmpty(list, "Aucun record vérifié", "Soyez le premier : terminez ce niveau connecté à Cosmic Online.");
            return;
        }
        bool meVisible = false;
        for (const auto& e : board_.entries) meVisible |= jget<bool>(e, "is_me", false);
        const Json* pinned = nullptr;
        if (!meVisible && board_.me.is_object() && board_.me.contains("entry") && board_.me["entry"].is_object()) pinned = &board_.me["entry"];
        Rect view = list;
        if (pinned) view.h -= rowH + ui.s(14);
        float content = board_.entries.size() * (rowH + gap) + (board_.nextOffset >= 0 ? rowH : 0.0f);
        ui.beginScroll("entries", view, content);
        float y = view.y - ui.scrollOffset();
        for (size_t i = 0; i < board_.entries.size(); ++i) {
            Rect row{view.x, y, view.w - ui.s(10), rowH};
            if (row.bottom() >= view.y - rowH && row.y <= view.bottom() + rowH) drawEntry(board_.entries[i], row, static_cast<int>(i), false);
            y += rowH + gap;
        }
        if (board_.nextOffset >= 0) {
            UIContext::ButtonStyle st;
            st.small = true;
            if (ui.button("more", {view.x, y, view.w - ui.s(10), rowH - ui.s(6)}, board_.loading ? "Chargement…" : "Afficher plus", st) && !board_.loading)
                requestBoard(true);
        }
        ui.endScroll();
        if (pinned) {
            Rect row{list.x, list.bottom() - rowH, list.w - ui.s(10), rowH};
            ui.separator({list.x, row.y - ui.s(7)}, list.w);
            drawEntry(*pinned, row, -2, true);
        }
    }

    void drawEmpty(const Rect& r, const std::string& title, const std::string& text) {
        UIContext& ui = g_.ui;
        ui.label(title, {r.center().x, r.y + r.h * 0.32f}, 22, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.labelWrapped(text, {r.x + ui.s(20), r.y + r.h * 0.32f + ui.s(22)}, r.w - ui.s(40), 16, ui.theme().textDim);
    }

    void drawBoardFooter(const Rect& f) {
        UIContext& ui = g_.ui;
        ui.separator({f.x + ui.s(12), f.y + ui.s(4)}, f.w - ui.s(24));
        std::string info;
        Color c = ui.theme().textDim;
        if (board_.fromCache) {
            info = "Hors ligne · classement " + OnlineUi::ageText(board_.age) + " (peut être ancien)";
            c = ui.theme().warm;
        } else if (board_.loaded && board_.error.empty()) {
            info = std::to_string(board_.total) + " joueur" + (board_.total > 1 ? "s" : "") + " classé" + (board_.total > 1 ? "s" : "") +
                   " · records vérifiés uniquement";
        }
        ui.label(info, {f.x + ui.s(14), f.y + ui.s(26)}, 15, c, Align::Left, FontFace::Body, VAlign::Middle);
        if (board_.me.is_object() && board_.me.contains("pending") && board_.me["pending"].is_object()) {
            const Json& pd = board_.me["pending"];
            ui.label("Votre temps " + formatRunTime(jget<int64_t>(pd, "time_ms", 0)), {f.x + ui.s(14), f.y + ui.s(56)}, 15, ui.theme().text,
                     Align::Left, FontFace::Body, VAlign::Middle);
            ui_.drawStatusChip(RecordStatus::Pending, {f.x + ui.s(190), f.y + ui.s(45)}, ui.s(22));
        }
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("refresh", {f.right() - ui.s(132), f.y + ui.s(40), ui.s(118), ui.s(32)}, "Actualiser", st)) requestAll();
    }

    // ---------------------------------------------------------------- level
    TexturePtr thumbnail(const RankedLevel& l) {
        auto it = thumbs_.find(l.id);
        if (it != thumbs_.end()) return it->second;
        TexturePtr t;
        if (l.entry)
            if (auto data = g_.content.loadLevel(l.id)) {
                auto png = renderLevelThumbnail(g_, *data, Paths::toUtf8(l.entry->assetRoot));
                if (!png.empty()) t = textureFromPng(g_, png);
            }
        thumbs_[l.id] = t;
        return t;
    }

    bool isExtreme(const RankedLevel& l) {
        auto it = extreme_.find(l.id);
        if (it != extreme_.end()) return it->second;
        bool e = false;
        if (l.entry)
            if (auto data = g_.content.loadLevel(l.id)) e = data->extreme && data->oneLife();
        extreme_[l.id] = e;
        return e;
    }

    void drawLevelZone() {
        UIContext& ui = g_.ui;
        const RankedLevel* l = level();
        // An EXTRÊME level's card gets the frame that warns (2.32.3).
        if (l && isExtreme(*l)) drawExtremeFrame(ui, zoneD_, 14, 1.7f, 22.0f);
        else ui.panel(zoneD_, false, 14);
        if (!l) return;
        ui.pushId("online/level");
        Rect th{zoneD_.x + ui.s(14), zoneD_.y + ui.s(14), ui.s(240), ui.s(135)};
        if (TexturePtr t = thumbnail(*l)) {
            ui.r().drawTexture(*t, nullptr, th);
        } else {
            ui.r().fillRect(th, Color{20, 30, 56, 255});
            ui.label(l->entry ? "Aperçu indisponible" : "Niveau non installé", th.center(), 16, ui.theme().textDim, Align::Center, FontFace::Body,
                     VAlign::Middle);
        }
        ui.r().drawRect(th, ui.theme().border, ui.s(1));
        UIContext::ButtonStyle arrow;
        arrow.small = true;
        int n = static_cast<int>(levels_.size());
        if (ui.button("prev", {th.x, th.bottom() + ui.s(10), ui.s(54), ui.s(38)}, "◀", arrow)) {
            levelIdx_ = (levelIdx_ - 1 + n) % n;
            refreshAfterChange();
        }
        if (ui.button("next", {th.right() - ui.s(54), th.bottom() + ui.s(10), ui.s(54), ui.s(38)}, "▶", arrow)) {
            levelIdx_ = (levelIdx_ + 1) % n;
            refreshAfterChange();
        }
        ui.label(std::to_string(levelIdx_ + 1) + " / " + std::to_string(n), {th.center().x, th.bottom() + ui.s(29)}, 16, ui.theme().textDim,
                 Align::Center, FontFace::Body, VAlign::Middle);
        float x = th.right() + ui.s(22);
        const PlanetData* planet = g_.content.planet(l->planet);
        UIContext::ButtonStyle st;
        st.small = true;
        float bw = ui.s(170), bx = zoneD_.right() - bw - ui.s(14);
        bool extreme = isExtreme(*l);
        // EXTRÊME: its « course à la vérification », next to the other actions.
        float textRight = bx - ui.s(14);
        if (extreme) {
            float rw = ui.s(250);
            Rect rb{bx - rw - ui.s(10), zoneD_.y + ui.s(18), rw, ui.s(40)};
            if (ui.button("race", rb, "Course à la vérification", st)) {
                const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, l->id);
                g_.app.pushScene(makeRaceBoard(g_, "official", l->id, 0, fp ? fp->hash : std::string{}));
            }
            textRight = rb.x - ui.s(14);
        }
        std::string planetName = planet ? planet->name : (l->planetName.empty() ? l->planet : l->planetName);
        Vec2 pn = ui.label(planetName, {x, zoneD_.y + ui.s(26)}, 17, planet ? planet->color : ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        if (extreme) ui.extremeBadge({x + pn.x + ui.s(12), zoneD_.y + ui.s(26)}, ui.s(24));
        float nf = ui.fitFont(l->name, FontFace::Title, 26, textRight - x, 0.7f);
        ui.label(ui.elide(l->name, FontFace::Title, nf, textRight - x), {x, zoneD_.y + ui.s(54)}, nf, ui.theme().text, Align::Left, FontFace::Title,
                 VAlign::Middle);
        float col2 = x + std::min(ui.s(330), (zoneD_.right() - x) * 0.5f);
        float y = zoneD_.y + ui.s(88);
        auto info = [&](float cx, float& yy, const std::string& k, const std::string& v, Color c) {
            ui.label(k, {cx, yy}, 14, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(v, {cx, yy + ui.s(19)}, 18, c, Align::Left, FontFace::Bold, VAlign::Middle);
            yy += ui.s(46);
        };
        float y1 = y, y2 = y;
        std::string route = std::string(categoryLabel(category())) + (period_ == 1 ? " · semaine" : " · historique");
        info(x, y1, "Type de parcours", route, ui.theme().text);
        const LevelRecord* rec = g_.records.get(l->id);
        std::string pb = rec && rec->bestTime > 0 ? formatRunTime(static_cast<int64_t>(std::llround(rec->bestTime * 1000.0))) + " (local)" : "—";
        info(x, y1, "Record personnel", pb, ui.theme().text);
        // World position and best time come from the level board when it is displayed.
        std::string pos = "—", best = "—";
        if (levelBoard() && board_.error.empty()) {
            if (board_.me.is_object() && board_.me.contains("entry") && board_.me["entry"].is_object())
                pos = "n°" + std::to_string(jget<int>(board_.me["entry"], "world_rank", jget<int>(board_.me["entry"], "rank", 0))) + " / " +
                      std::to_string(board_.total);
            else if (g_.online.loggedIn() && board_.loaded)
                pos = "non classé";
            for (const auto& e : board_.entries)
                if (jget<int>(e, "world_rank", jget<int>(e, "rank", 0)) == 1)
                    best = formatRunTime(jget<int64_t>(e, "time_ms", 0)) + " · " + ui_.displayName(jstr(e["player"], "username"), 1, jget<bool>(e, "is_me", false));
        }
        info(col2, y2, "Position mondiale", pos, ui.theme().accent);
        info(col2, y2, "Meilleur temps mondial", best, GlobeRenderer::tierColor(MarkerTier::Gold));
        // Local performance status (LOCAL / PENDING / VERIFIED / REJECTED).
        const OnlineAttempt* latest = nullptr;
        for (const auto& a : g_.online.attempts().all())
            if (a.levelId == l->id && a.submission(category())) latest = &a;
        if (latest) {
            const OnlineSubmission* s = latest->submission(category());
            ui_.drawStatusChip(s->status, {x + ui.s(190), zoneD_.y + ui.s(126)}, ui.s(22));
        }
        // Actions
        // Five actions, one under the other (36 px apart).
        const float ah = ui.s(30);
        auto actionY = [&](int i) { return zoneD_.y + ui.s(12) + ui.s(36) * static_cast<float>(i); };
        // One font and one size for the five: the largest at which every label fits whole.
        static const char* const kActions[] = {"Voir le replay", "Comparer…", "Défier ce temps", "Envoyer le record", "Jouer ce niveau"};
        // « Voir le replay » shares its line with the download icon (2.45).
        const float dlW = ah + ui.s(6), gapW = ui.s(6);
        st.fontSize = 18.0f;
        for (const char* a : kActions) st.fontSize = std::min(st.fontSize, ui.fitFont(a, FontFace::Bold, 18.0f, bw - ui.s(24), 0.5f));
        st.fontSize = std::min(st.fontSize, ui.fitFont(kActions[0], FontFace::Bold, 18.0f, bw - dlW - gapW - ui.s(24), 0.5f));
        std::string replayRecord = replayCandidate();
        st.enabled = !replayRecord.empty() && l->entry != nullptr;
        if (ui.button("replay", {bx, actionY(0), bw - dlW - gapW, ah}, kActions[0], st))
            g_.app.pushScene(makeReplayViewer(g_, replayRecord));
        {
            Json entry;
            for (const auto& e : board_.entries)
                if (!replayRecord.empty() && jstr(e, "record_id") == replayRecord) entry = e;
            if (entry.is_null() && !replayRecord.empty()) entry = Json{{"record_id", replayRecord}};
            ReplayDownloadUi::icon(g_, "download", {bx + bw - dlW, actionY(0), dlW, ah}, recordDownload(entry, l), !replayRecord.empty());
        }
        // Two records of this level side by side: the selected one (A) and the best other one
        // with a replay (B), downloaded before the comparison (read only).
        std::vector<Json> pair = comparePair(replayRecord);
        st.enabled = pair.size() == 2 && l->entry != nullptr;
        if (ui.button("compare", {bx, actionY(1), bw, ah}, kActions[1], st)) {
            ReplayCompareRequest req;
            req.levelId = l->id;
            for (const Json& e : pair) req.preselect.push_back(jstr(e, "record_id"));
            req.onlineEntries = pair;
            g_.app.pushScene(makeReplayCompareSetup(g_, std::move(req)));
        }
        st.enabled = !replayRecord.empty() && l->entry != nullptr;
        // Ghost Racing: race the selected record (or the world record) as a ghost.
        if (ui.button("ghost", {bx, actionY(2), bw, ah}, kActions[2], st)) {
            GhostSetupRequest req;
            req.recordId = replayRecord;
            req.fromOnline = true;
            g_.app.pushScene(makeGhostSetup(g_, l->id, req));
        }
        int unsent = 0;
        for (const auto& a : g_.online.attempts().all())
            if (a.levelId == l->id && (a.ownerId.empty() || a.ownerId == g_.online.playerId()))
                for (const auto& s2 : a.submissions) unsent += s2.status == RecordStatus::Local && !s2.blocked ? 1 : 0;
        st.enabled = unsent > 0 && g_.online.usable();
        st.primary = unsent > 0;
        if (ui.button("submit", {bx, actionY(3), bw, ah}, kActions[3], st)) {
            if (!g_.online.loggedIn()) g_.app.pushScene(makeOnlineAccount(g_, false));
            else g_.online.syncNow(true);
        }
        st.primary = false;
        st.enabled = l->entry != nullptr;
        if (ui.button("play", {bx, actionY(4), bw, ah}, kActions[4], st)) {
            PlayOptions po;
            po.fromOnline = true;
            g_.app.switchScene(makeGameScene(g_, l->id, po));
        }
        if (unsent > 0)
            ui.label(std::to_string(unsent) + " performance(s) à envoyer", {bx + bw * 0.5f, actionY(4) + ah + ui.s(10)}, 13, ui.theme().warm, Align::Center,
                     FontFace::Body, VAlign::Middle);
        drawLevelSocial({x, zoneD_.y + ui.s(186), bx - ui.s(14) - x, ui.s(38)}, *l);
        // Different revision locally and on the server: warn (records are never mixed).
        if (meta_.is_object())
            for (const auto& ml : meta_.value("levels", Json::array()))
                if (jstr(ml, "id") == l->id)
                    if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, l->id); fp && fp->hash != jstr(ml, "content_hash"))
                        ui.label("Version du niveau différente de celle du serveur", {x, zoneD_.bottom() - ui.s(12)}, 14, ui.theme().danger,
                                 Align::Left, FontFace::Body, VAlign::Middle);
        ui.popId();
    }

    /// « Télécharger » (2.45): the replay of a record of the board, kept on this computer with
    /// what the board says of it (the level and its version are read from the replay).
    /// `l`: its level (the board's when it is a level's board).
    ReplayDownloadRequest recordDownload(const Json& e, const RankedLevel* l) const {
        const std::string rid = jstr(e, "record_id");
        const Json& p = jfield(e, "player", Json::object());
        const std::string name = jstr(p, "username");
        const int rank = jget<int>(e, "world_rank", jget<int>(e, "rank", 0));
        return ReplayDownloadUi::request("record", rid, "/api/v1/records/" + rid + "/replay", name,
                                         name.empty() ? std::string() : ui_.displayName(name, rank, jget<bool>(e, "is_me", false)),
                                         l ? l->id : std::string(), l ? l->name : std::string(), categoryLabel(category()),
                                         jget<int64_t>(e, "time_ms", 0), jstr(e, "achieved_at"));
    }

    /// « Comparer… »: the record VIEW REPLAY opens (A) and the best other one with a replay
    /// (B): their board entries, or nothing when there are not two.
    std::vector<Json> comparePair(const std::string& first) const {
        std::vector<Json> out;
        if (first.empty()) return out;
        for (const auto& e : board_.entries)
            if (jstr(e, "record_id") == first) out.push_back(e);
        for (const auto& e : board_.entries)
            if (out.size() == 1 && jget<bool>(e, "has_replay", false) && jstr(e, "record_id") != first) out.push_back(e);
        return out;
    }

    /// The players' rating of the level (computed by the server, not the difficulty /5 of the
    /// game) and the button of its « Avis et commentaires » page (2.19).
    void drawLevelSocial(const Rect& r, const RankedLevel& l) {
        UIContext& ui = g_.ui;
        const Json& v = levelRating_.view();
        bool known = levelRating_.loaded() && levelRating_.levelId() == l.id;
        bool missing = levelRating_.errorCode() == "LEVEL_NOT_FOUND";
        std::string label = known ? "Avis et commentaires (" + std::to_string(levelRating_.comments()) + ")" : "Avis et commentaires";
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 16;
        st.enabled = g_.online.usable() && !missing;
        float bw = std::min(r.w * 0.5f, ui.text().measure(label, FontFace::Bold, ui.s(16)).x + ui.s(36));
        Rect b{r.right() - bw, r.y, bw, r.h};
        if (ui.button("social", b, label, st)) g_.app.pushScene(makeOnlineThread(g_, "level:" + l.id, l.id));
        float room = b.x - ui.s(14) - r.x;
        const std::string head = "Note des joueurs : ";
        float hf = ui.fitFont(head, FontFace::Body, 16, room * 0.45f);
        Vec2 hs = ui.label(head, {r.x, r.center().y}, hf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        float vx = r.x + hs.x, vroom = room - hs.x;
        if (known) {
            const Json& rt = v.value("rating", Json::object());
            int count = jget<int>(rt, "count", 0);
            double avg = rt.contains("average") && rt["average"].is_number() ? rt["average"].get<double>() : 0.0;
            std::string tail = levelRating_.fromCache() ? " (hors ligne)" : "";
            float tw = tail.empty() ? 0.0f : ui.text().measure(tail, FontFace::Body, ui.s(14)).x;
            float used = drawRatingSummary(ui, {vx, r.center().y}, avg, count, 16, ui.theme().text, vroom - tw);
            if (!tail.empty()) ui.label(tail, {vx + used, r.center().y}, 14, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            std::string t = !g_.online.usable() ? "en ligne"
                            : missing           ? "niveau inconnu du serveur"
                            : levelRating_.busy() || levelRating_.error().empty() ? "chargement…"
                                                                                   : "indisponible";
            ui.label(t, {vx, r.center().y}, ui.fitFont(t, FontFace::Body, 16, vroom), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
    }

    /// The comments of a run of the board (its author is notified of new ones).
    void openRunComments(const Json& e) {
        std::string rid = jstr(e, "record_id");
        if (rid.empty() || !g_.online.usable()) return;
        g_.app.pushScene(makeOnlineThread(g_, "run:" + rid, level() ? level()->id : std::string{}));
    }

    /// Record whose replay VIEW REPLAY opens: selected row, else the world record.
    std::string replayCandidate() const {
        if (!levelBoard()) return {};
        if (selectedRow_ >= 0 && selectedRow_ < static_cast<int>(board_.entries.size())) {
            const Json& e = board_.entries[static_cast<size_t>(selectedRow_)];
            if (jget<bool>(e, "has_replay", false)) return jstr(e, "record_id");
        }
        for (const auto& e : board_.entries)
            if (jget<bool>(e, "has_replay", false)) return jstr(e, "record_id");
        return {};
    }

    // -------------------------------------------------------------- profile
    void openProfile(const std::string& playerId) {
        if (playerId.empty() || !g_.online.usable()) return;
        g_.app.pushScene(makePlayerProfile(g_, playerId));
    }

    // -------------------------------------------------------- rank change
    void startRankAnimation(const OnlineService::RankChange& c) {
        rankAnim_ = c;
        rankAnimT_ = 0.0f;
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i].id == c.levelId) levelIdx_ = static_cast<int>(i);
        for (size_t i = 0; i < kAllCategories.size(); ++i)
            if (kAllCategories[i] == c.category) category_ = static_cast<int>(i);
        shockCountry_ = myCountry();
        shockT_ = -0.6f;
        if (const CountryInfo* ci = ui_.countries().find(shockCountry_); ci && ci->center) cam_.flyTo(*ci->center, 1.4, 1.4f);
        g_.uiSound("ui_confirm", 0.8f);
    }

    void drawRankAnimation() {
        if (!rankAnim_ || rankAnimT_ > 6.0f) return;
        UIContext& ui = g_.ui;
        float a = std::clamp(std::min(rankAnimT_ / 0.4f, (6.0f - rankAnimT_) / 0.6f), 0.0f, 1.0f);
        Rect box{zoneB_.center().x - ui.s(250), zoneB_.y + ui.s(24), ui.s(500), ui.s(130)};
        ui.r().fillRoundedRect(box, ui.s(14), Color{12, 18, 40, static_cast<uint8_t>(235 * a)});
        ui.r().drawRoundedRect(box, ui.s(14), GlobeRenderer::tierColor(MarkerTier::Gold).withAlphaF(a), ui.s(2));
        ui.label("NOUVEAU RECORD PERSONNEL", {box.center().x, box.y + ui.s(26)}, 22, GlobeRenderer::tierColor(MarkerTier::Gold).withAlphaF(a),
                 Align::Center, FontFace::Title, VAlign::Middle);
        ui.label(formatRunTime(rankAnim_->timeMs) + " · " + categoryLabel(rankAnim_->category), {box.center().x, box.y + ui.s(58)}, 20,
                 ui.theme().text.withAlphaF(a), Align::Center, FontFace::Bold, VAlign::Middle);
        // Old position slides to the new one.
        float k = applyEase(Ease::OutCubic, std::clamp((rankAnimT_ - 0.8f) / 1.2f, 0.0f, 1.0f));
        int from = rankAnim_->previousRank, to = rankAnim_->rank;
        std::string oldText = from > 0 ? "n°" + std::to_string(from) : "non classé";
        ui.label(oldText, {box.center().x - ui.s(90), box.y + ui.s(96)}, 20, ui.theme().textDim.withAlphaF(a * (1.0f - 0.6f * k)), Align::Center,
                 FontFace::Bold, VAlign::Middle);
        ui.label("→", {box.center().x, box.y + ui.s(96)}, 20, ui.theme().text.withAlphaF(a), Align::Center, FontFace::Bold, VAlign::Middle);
        float shown = from > 0 ? from + (to - from) * k : static_cast<float>(to);
        ui.label("n°" + std::to_string(static_cast<int>(std::lround(shown))), {box.center().x + ui.s(90), box.y + ui.s(96) - ui.s(6) * (1.0f - k)}, 24,
                 ui.theme().accent.withAlphaF(a), Align::Center, FontFace::Title, VAlign::Middle);
    }

    GameServices& g_;
    OnlineUi ui_;
    LevelRatingPanel levelRating_;
    std::string wantedLevel_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    cstyle::PlanetBackdrop backdrop_;  // 2.84: the planets of the campaign behind the globe (Cosmic kit)
    GlobeRenderer globe_;
    GlobeCamera cam_;
    GlobeView globeView_;
    float t_ = 0.0f;
    float idle_ = 10.0f;
    Rect header_, zoneA_, zoneB_, zoneC_, zoneD_, infoRect_, globeButtons_;

    std::vector<RankedLevel> levels_;
    size_t installedCount_ = 0;
    bool accountAsked_ = false;
    int levelIdx_ = 0;
    int category_ = 0;
    int period_ = 0;
    int tab_ = TabLevel;
    std::string country_;
    std::string search_;
    float searchDirty_ = 0.0f;
    BoardState board_;
    int selectedRow_ = -1;
    Json meta_;

    std::vector<GlobeMarker> markers_;
    std::vector<GlobeCluster> clusters_;
    bool globeLoading_ = false;
    bool globeFromCache_ = false;
    double globeAge_ = 0.0;
    std::string globeError_;
    int globeGen_ = 0;
    int selected_ = -1;
    int hoverCluster_ = -1;
    float selectT_ = 0.0f;
    GlobeRenderer::Input globeInput_;
    Rect statusRect_;


    std::optional<OnlineService::RankChange> rankAnim_;
    float rankAnimT_ = 10.0f;
    std::string shockCountry_;
    float shockT_ = 2.0f;
    std::map<std::string, TexturePtr> thumbs_;
    std::map<std::string, bool> extreme_;  // EXTRÊME levels (one life): they have a race
    RaceBoardState race_;                   // the race of the selected EXTRÊME level (NIVEAU tab)
    std::set<std::string> verifiedLevels_;  // levels seen with a verified record or a verifier
    std::string raceFetching_, raceMsg_;  // 2.84: the attempt whose replay is downloading; what became of it
    std::map<std::string, bool> raceView_;  // the player's choice per EXTRÊME level: true = its race
    int raceSize_ = 1;                      // the race shown: 1 Solo, 2 Duo, 3 Trio
};

}  // namespace

std::unique_ptr<Scene> makeOnlineRanking(GameServices& g, const std::string& levelId) {
    return std::make_unique<OnlineRankingScene>(g, levelId);
}

}  // namespace cosmic
