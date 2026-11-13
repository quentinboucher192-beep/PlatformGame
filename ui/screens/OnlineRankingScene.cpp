// COSMIC ONLINE - ranking screen: navigation (A), interactive globe (B), leaderboards (C),
// selected level and player card (D). Every request goes through OnlineService (network
// thread); this screen only reads results, so it stays fluid whatever the connection.

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/core/Easing.h"
#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "ui/online/GlobeRenderer.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/Thumbnail.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

constexpr const char* kTabs[] = {"GLOBAL", "AMIS", "PAYS", "PLANÈTE", "NIVEAU"};
enum Tab { TabGlobal = 0, TabFriends, TabCountry, TabPlanet, TabLevel };

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

class OnlineRankingScene : public Scene {
public:
    OnlineRankingScene(GameServices& g, std::string levelId) : g_(g), ui_(g), wantedLevel_(std::move(levelId)) {}

    void onEnter() override {
        g_.playMusic("music/menu_theme.ogg");
        globe_.init(g_.app.resources());
        for (const PlanetData* p : g_.content.planets())
            for (const auto& id : p->levels)
                if (const LevelEntry* e = g_.content.level(id); e && e->source == ContentSource::Builtin) levels_.push_back(e);
        if (wantedLevel_.empty()) wantedLevel_ = g_.progression.lastLevel;
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i]->id == wantedLevel_) levelIdx_ = static_cast<int>(i);
        tab_ = TabLevel;
        g_.ui.setFocus("online/nav/ranking");
        auto changes = g_.online.takeRankChanges();
        if (!changes.empty()) startRankAnimation(changes.back());
        requestAll();
        // First visit: explain Cosmic Online and offer an account (never mandatory).
        if (g_.online.usable() && !g_.online.loggedIn() && !g_.settings.online.onboardingDone)
            g_.app.pushScene(makeOnlineAccount(g_, true));
    }

    void onResume() override {
        g_.playMusic("music/menu_theme.ogg");
        requestAll();
    }

    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
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
        bg_.render(g_, 0.35f);
        ui.inputEnabled = !profileOpen_;
        drawGlobeZone();
        drawHeader();
        drawNavigation();
        drawPlayerCard();
        drawBoardZone();
        drawLevelZone();
        ui.inputEnabled = true;
        if (profileOpen_) drawProfile();
        drawRankAnimation();
        if (ui.backPressed() && !profileOpen_) g_.app.switchScene(makeMainMenu(g_));
        else if (ui.backPressed() && profileOpen_) profileOpen_ = false;
        ui.end();
    }

private:
    // ----------------------------------------------------------------- data
    const LevelEntry* level() const { return levels_.empty() ? nullptr : levels_[static_cast<size_t>(levelIdx_)]; }
    RunCategory category() const { return kAllCategories[static_cast<size_t>(category_)]; }
    std::string periodId() const { return period_ == 1 ? "weekly" : "all"; }
    bool levelBoard() const { return tab_ == TabFriends || tab_ == TabCountry || tab_ == TabLevel; }

    std::string myCountry() const { return jstr(g_.online.profile(), "country_code"); }

    std::string boardPath(int offset) const {
        const LevelEntry* l = level();
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
        requestGlobe();
        if (!g_.online.usable()) return;
        g_.online.get("/api/v1/meta", OnlineService::guard(life_, [this](const ApiResult& r) {
            if (r.ok) meta_ = r.data;
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
        g_.online.get(boardPath(offset), OnlineService::guard(life_, [this, gen, more, boardKey](const ApiResult& r) {
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
            board_.total = jget<int>(r.meta, "total", static_cast<int>(board_.entries.size()));
            board_.nextOffset = r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            if (!r.fromCache && !more && board_.me.is_object() && board_.me.contains("entry") && board_.me["entry"].is_object())
                g_.online.observeBoard(boardKey, jget<int>(board_.me["entry"], "rank", 0), board_.total);
        }));
    }

    void requestGlobe() {
        const LevelEntry* l = level();
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
        zoneD_ = {bx, H - m - ui.s(206), bw, ui.s(206)};
        zoneB_ = {bx, zoneA_.y, bw, zoneD_.y - ui.s(14) - zoneA_.y};
        globeView_.center = zoneB_.center();
        globeView_.baseRadius = std::min(zoneB_.w, zoneB_.h) * 0.42f;
    }

    // ---------------------------------------------------------------- globe
    void handleGlobeInput(float dt) {
        if (profileOpen_ || g_.app.topScene() != this || g_.ui.editingText()) return;
        InputManager& in = g_.app.input();
        Vec2 mp = in.mousePos();
        bool inside = zoneB_.contains(mp);
        bool overUi = infoRect_.contains(mp) || globeButtons_.contains(mp);
        if (inside && !overUi && in.wheel() != 0.0f) {
            cam_.cancelFlight();
            cam_.zoomBy(std::pow(1.15, in.wheel()));
            idle_ = 0.0f;
        }
        if (in.mousePressed(SDL_BUTTON_LEFT) && inside && !overUi) {
            pressing_ = true;
            dragged_ = false;
            pressPos_ = mp;
        }
        if (pressing_ && in.mouseDown(SDL_BUTTON_LEFT)) {
            Vec2 d = in.mouseDelta();
            if ((mp - pressPos_).length() > g_.ui.s(5)) dragged_ = true;
            if (dragged_ && (d.x != 0.0f || d.y != 0.0f)) {
                float R = globeView_.radius(cam_);
                cam_.rotate(d.x / R * 57.3, d.y / R * 57.3);
                idle_ = 0.0f;
            }
        }
        if (pressing_ && in.mouseReleased(SDL_BUTTON_LEFT)) {
            pressing_ = false;
            if (!dragged_) clickGlobe(mp);
        }
        // Gamepad: right stick turns the globe, triggers zoom.
        Vec2 rs = in.rightStick();
        if (std::abs(rs.x) > 0.05f || std::abs(rs.y) > 0.05f) {
            cam_.rotate(rs.x * dt * 90.0 / cam_.zoom, -rs.y * dt * 70.0 / cam_.zoom);
            idle_ = 0.0f;
        }
        float zt = in.rightTrigger() - in.leftTrigger();
        if (std::abs(zt) > 0.1f) {
            cam_.cancelFlight();
            cam_.zoomBy(std::pow(1.9, zt * dt));
            idle_ = 0.0f;
        }
        Vec2 hoverPos = mp;
        hoverCluster_ = inside && !overUi ? pickCluster(clusters_, hoverPos, g_.ui.s(16)) : -1;
    }

    void clickGlobe(Vec2 mp) {
        idle_ = 0.0f;
        int c = pickCluster(clusters_, mp, g_.ui.s(16));
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
        // Markers: layout at the current zoom, clustering, then drawing back to front.
        layoutMarkers(markers_, globeView_.radius(cam_), 15.0f);
        clusters_ = clusterMarkers(markers_, cam_, globeView_, ui.s(cam_.zoom > 2.5 ? 10.0f : 18.0f),
                                   g_.settings.online.bandwidth == 0 ? 120 : 400);
        for (size_t i = 0; i < clusters_.size(); ++i) {
            const GlobeCluster& c = clusters_[i];
            const GlobeMarker& best = markers_[static_cast<size_t>(c.members.front())];
            bool sel = std::find(c.members.begin(), c.members.end(), selected_) != c.members.end();
            bool me = std::any_of(c.members.begin(), c.members.end(), [&](int k) { return markers_[static_cast<size_t>(k)].isMe; });
            float pulse = std::sin(t_ * 2.4f + static_cast<float>(i) * 0.7f) * 0.5f + 0.5f;
            float scale = ui.scale() * (c.single() ? 1.0f : 1.0f + std::min(0.9f, 0.18f * std::log2(static_cast<float>(c.members.size()))));
            if (static_cast<int>(i) == hoverCluster_) scale *= 1.25f;
            globe_.renderMarker(r, c.screen, markerTier(best.rank), scale, pulse, sel, me, static_cast<float>(c.depth));
            if (!c.single())
                ui.label(std::to_string(c.members.size()), c.screen + Vec2{ui.s(9), -ui.s(11)}, 15, Color{255, 255, 255, 235}, Align::Left,
                         FontFace::Bold, VAlign::Middle);
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
            std::string t = c.single() ? ui_.displayName(m.playerName, m.rank, m.isMe) + " · " + formatRunTime(m.timeMs)
                                       : std::to_string(c.members.size()) + " joueurs · meilleur n°" + std::to_string(m.rank);
            Vec2 sz = ui.text().measure(t, FontFace::Body, ui.s(17));
            Rect tip{c.screen.x + ui.s(14), c.screen.y - ui.s(36), sz.x + ui.s(18), ui.s(28)};
            ui.r().fillRoundedRect(tip, ui.s(6), Color{8, 12, 26, 225});
            ui.label(t, {tip.x + ui.s(9), tip.center().y}, 17, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        }
        // Information window of the selected marker.
        if (selected_ >= 0 && selected_ < static_cast<int>(markers_.size())) {
            selectT_ += g_.app.frameTime() / 1000.0f;
            const GlobeMarker& m = markers_[static_cast<size_t>(selected_)];
            Projected p = projectGeo(cam_, globeView_, m.display);
            float a = std::clamp(selectT_ * 4.0f, 0.0f, 1.0f);
            float w = ui.s(300), h = ui.s(232);
            Vec2 anchor = p.visible() ? p.screen : globeView_.center;
            Rect box{anchor.x + ui.s(26), anchor.y - h - ui.s(10), w, h};
            box.x = std::clamp(box.x, zoneB_.x + ui.s(8), zoneB_.right() - w - ui.s(8));
            box.y = std::clamp(box.y, zoneB_.y + ui.s(8), zoneB_.bottom() - h - ui.s(8));
            box.y += (1.0f - a) * ui.s(12);
            infoRect_ = box;
            Color tier = GlobeRenderer::tierColor(markerTier(m.rank));
            ui.r().fillRoundedRect(box, ui.s(12), Color{8, 13, 30, static_cast<uint8_t>(235 * a)});
            ui.r().drawRoundedRect(box, ui.s(12), tier.withAlphaF(0.8f * a), ui.s(1.5f));
            if (p.visible()) ui.r().drawLine(anchor, {box.x, box.bottom() - ui.s(20)}, tier.withAlphaF(0.6f * a), ui.s(1.5f));
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
            const LevelEntry* le = g_.content.level(m.levelId);
            row("Niveau", le ? le->name : m.levelId, ui.theme().text);
            row("Date", m.achievedAt.substr(0, 10), ui.theme().text);
            UIContext::ButtonStyle st;
            st.small = true;
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
        ui.label(status, {zoneB_.x + ui.s(12), zoneB_.bottom() - ui.s(16)}, 16, globeFromCache_ || !globeError_.empty() ? ui.theme().warm : ui.theme().textDim,
                 Align::Left, FontFace::Body, VAlign::Middle);
        ui.label("Positions approximatives : centre du pays", {zoneB_.right() - ui.s(12), zoneB_.bottom() - ui.s(16)}, 14,
                 ui.theme().textDim.withAlphaF(0.8f), Align::Right, FontFace::Body, VAlign::Middle);
    }

    // --------------------------------------------------------------- header
    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(header_, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = ui.label("COSMIC ONLINE", {ui.s(32), header_.center().y}, 34, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label("Classements mondiaux", {ui.s(32) + tsz.x + ui.s(18), header_.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        ui_.drawConnection({zoneC_.x - ui.s(250), header_.center().y});
        UIContext::ButtonStyle st;
        st.small = true;
        ui.pushId("online/header");
        st.fontSize = 18;
        float w = ui.text().measure("Paramètres en ligne", FontFace::Bold, ui.s(18)).x + ui.s(36);
        if (ui.button("settings", {zoneC_.right() - w, header_.center().y - ui.s(19), w, ui.s(38)}, "Paramètres en ligne", st))
            g_.app.pushScene(makeOnlineSettings(g_));
        if (g_.online.usable() && g_.online.unsentCount() + g_.online.pendingVerificationCount() > 0 && g_.online.loggedIn()) {
            if (ui.button("sync", {zoneC_.right() - w * 2 - ui.s(10), header_.center().y - ui.s(19), w, ui.s(38)},
                          g_.online.syncing() ? "Synchronisation…" : "Synchroniser", st))
                g_.online.syncNow();
        }
        ui.popId();
    }

    // ----------------------------------------------------------- navigation
    void drawNavigation() {
        UIContext& ui = g_.ui;
        Rect nav{zoneA_.x, zoneA_.y, zoneA_.w, ui.s(6 * 58 + 20)};
        ui.r().fillRoundedRect(nav, ui.s(14), Color{6, 10, 22, 170});
        ui.pushId("online/nav");
        float y = nav.y + ui.s(10), h = ui.s(50);
        auto item = [&](const char* id, const char* label, bool current) {
            UIContext::ButtonStyle st;
            st.align = Align::Left;
            st.fontSize = 21;
            st.selected = current;
            st.primary = current;
            bool c = ui.button(id, {nav.x + ui.s(10), y, nav.w - ui.s(20), h}, label, st);
            if (current) ui.r().fillRect({nav.x + ui.s(4), y + ui.s(8), ui.s(4), h - ui.s(16)}, ui.theme().accent);
            y += h + ui.s(8);
            return c;
        };
        if (item("play", "JOUER", false)) {
            std::string planet = g_.progression.lastPlanet.empty() ? "sylvanox" : g_.progression.lastPlanet;
            g_.app.switchScene(makeLevelSelect(g_, planet));
        }
        if (item("planets", "PLANÈTES", false)) g_.app.switchScene(makePlanetMap(g_));
        item("ranking", "ONLINE RANKING", true);
        if (item("workshop", "ATELIER", false)) g_.app.switchScene(makeContentManager(g_));
        if (item("settings", "PARAMÈTRES", false)) g_.app.pushScene(makeSettingsScene(g_, false));
        if (item("back", "RETOUR", false)) g_.app.switchScene(makeMainMenu(g_));
        ui.popId();
    }

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
            ui_.drawConnection({card.x + ui.s(20), card.y + ui.s(92)});
            int totalLevels = static_cast<int>(levels_.size());
            int planetsDone = 0;
            for (const PlanetData* pl : g_.content.planets()) planetsDone += g_.progression.planetCompleted(*pl, g_.content) ? 1 : 0;
            ui.label("Niveaux terminés : " + std::to_string(std::min(g_.progression.completedCount(), totalLevels)) + "/" + std::to_string(totalLevels) +
                         " · Planètes : " + std::to_string(planetsDone) + "/" + std::to_string(g_.content.planets().size()),
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
        }
        float y = zoneC_.y + ui.s(60);
        float x = zoneC_.x + ui.s(12), w = zoneC_.w - ui.s(24);
        // Filters
        std::vector<std::string> cats;
        for (RunCategory c : kAllCategories) cats.push_back(categoryLabel(c));
        int cat = category_;
        float half = levelBoard() ? w * 0.6f : w;
        if (ui.cycle("category", {x, y, half - ui.s(6), ui.s(36)}, "", cat, cats) && cat != category_) {
            category_ = cat;
            refreshAfterChange();
        }
        if (levelBoard()) {
            int per = period_;
            if (ui.cycle("period", {x + half, y, w - half, ui.s(36)}, "", per, {"Historique", "Semaine"}) && per != period_) {
                period_ = per;
                refreshAfterChange();
            }
        }
        y += ui.s(42);
        ui.label(categoryRule(category()), {x, y + ui.s(8)}, 14, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
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
        ui.label("JOUEUR", {x + ui.s(84), y + ui.s(8)}, 14, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(tab_ == TabGlobal ? "POINTS" : "TEMPS", {x + w - ui.s(8), y + ui.s(8)}, 14, ui.theme().textDim, Align::Right, FontFace::Bold,
                 VAlign::Middle);
        y += ui.s(18);
        float footerH = ui.s(84);
        Rect list{x, y, w, zoneC_.bottom() - footerH - y};
        drawEntries(list);
        drawBoardFooter({zoneC_.x, zoneC_.bottom() - footerH, zoneC_.w, footerH});
        ui.popId();
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
        // Top 3 get a medal-coloured badge.
        Vec2 badge{row.x + ui.s(26), row.center().y};
        if (rank >= 1 && rank <= 3) {
            Color c = GlobeRenderer::tierColor(markerTier(rank));
            ui.r().drawGlow(badge, ui.s(18), c.withAlphaF(0.35f));
            ui.r().fillCircle(badge, ui.s(13), c.withAlphaF(0.9f));
            ui.label(std::to_string(rank), badge, 16, Color{20, 20, 30, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        } else {
            ui.label(std::to_string(rank), badge, 16, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), pid, {row.x + ui.s(46), row.y + ui.s(5), row.h - ui.s(10), row.h - ui.s(10)});
        float nx = row.x + ui.s(52) + row.h - ui.s(10);
        std::string cc = jstr(p, "country_code");
        if (!cc.empty()) {
            ui_.drawFlag(cc, {nx, row.center().y - ui.s(8), ui.s(22), ui.s(16)});
            nx += ui.s(28);
        }
        std::string name = ui_.displayName(jstr(p, "username"), rank, me);
        ui.label(name, {nx, row.center().y}, 18, me ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        float rx = row.right() - ui.s(10);
        if (me) {
            ui.label("PB", {rx - ui.s(110), row.center().y}, 13, ui.theme().violet, Align::Right, FontFace::Bold, VAlign::Middle);
        }
        ui.label(entryValue(e), {rx, row.center().y}, 18, rank == 1 ? GlobeRenderer::tierColor(MarkerTier::Gold) : ui.theme().text, Align::Right,
                 FontFace::Bold, VAlign::Middle);
    }

    void drawEntries(const Rect& list) {
        UIContext& ui = g_.ui;
        float rowH = ui.s(46), gap = ui.s(5);
        if (!g_.online.usable()) {
            drawEmpty(list, onlineStateLabel(g_.online.state()),
                      g_.online.state() == OnlineState::NotConfigured
                          ? "Aucun serveur Cosmic Online n'est configuré pour cette version. Vos records restent enregistrés localement."
                          : g_.online.state() == OnlineState::Disabled ? "Activez Cosmic Online dans les paramètres en ligne pour voir les classements."
                                                                       : "Cette version du jeu ne peut pas accéder au réseau.");
            return;
        }
        if (board_.loading && board_.entries.empty()) {
            float a = 0.5f + 0.5f * std::sin(t_ * 5.0f);
            for (int i = 0; i < 6; ++i)
                ui.r().fillRoundedRect({list.x, list.y + i * (rowH + gap), list.w, rowH}, ui.s(6), Color{20, 30, 56, static_cast<uint8_t>(80 + 60 * a)});
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
    TexturePtr thumbnail(const LevelEntry& l) {
        auto it = thumbs_.find(l.id);
        if (it != thumbs_.end()) return it->second;
        TexturePtr t;
        if (auto data = g_.content.loadLevel(l.id)) {
            auto png = renderLevelThumbnail(g_, *data, Paths::toUtf8(l.assetRoot));
            if (!png.empty()) t = textureFromPng(g_, png);
        }
        thumbs_[l.id] = t;
        return t;
    }

    void drawLevelZone() {
        UIContext& ui = g_.ui;
        const LevelEntry* l = level();
        ui.panel(zoneD_, false, 14);
        if (!l) return;
        ui.pushId("online/level");
        Rect th{zoneD_.x + ui.s(14), zoneD_.y + ui.s(14), ui.s(240), ui.s(135)};
        if (TexturePtr t = thumbnail(*l)) ui.r().drawTexture(*t, nullptr, th);
        else ui.r().fillRect(th, Color{20, 30, 56, 255});
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
        ui.label(planet ? planet->name : l->planet, {x, zoneD_.y + ui.s(26)}, 17, planet ? planet->color : ui.theme().accent, Align::Left,
                 FontFace::Bold, VAlign::Middle);
        ui.label(l->name, {x, zoneD_.y + ui.s(54)}, 26, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
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
        UIContext::ButtonStyle st;
        st.small = true;
        float bw = ui.s(170), bx = zoneD_.right() - bw - ui.s(14);
        std::string replayRecord = replayCandidate();
        st.enabled = !replayRecord.empty();
        if (ui.button("replay", {bx, zoneD_.y + ui.s(18), bw, ui.s(40)}, "VIEW REPLAY", st))
            g_.app.pushScene(makeReplayViewer(g_, replayRecord));
        int unsent = 0;
        for (const auto& a : g_.online.attempts().all())
            if (a.levelId == l->id && (a.ownerId.empty() || a.ownerId == g_.online.playerId()))
                for (const auto& s2 : a.submissions) unsent += s2.status == RecordStatus::Local && !s2.blocked ? 1 : 0;
        st.enabled = unsent > 0 && g_.online.usable();
        st.primary = unsent > 0;
        if (ui.button("submit", {bx, zoneD_.y + ui.s(66), bw, ui.s(40)}, "SUBMIT RECORD", st)) {
            if (!g_.online.loggedIn()) g_.app.pushScene(makeOnlineAccount(g_, false));
            else g_.online.syncNow(true);
        }
        st.primary = false;
        st.enabled = true;
        if (ui.button("play", {bx, zoneD_.y + ui.s(114), bw, ui.s(40)}, "Jouer ce niveau", st)) g_.app.switchScene(makeGameScene(g_, l->id));
        if (unsent > 0)
            ui.label(std::to_string(unsent) + " performance(s) à envoyer", {bx + bw * 0.5f, zoneD_.y + ui.s(172)}, 14, ui.theme().warm, Align::Center,
                     FontFace::Body, VAlign::Middle);
        // Different revision locally and on the server: warn (records are never mixed).
        if (meta_.is_object())
            for (const auto& ml : meta_.value("levels", Json::array()))
                if (jstr(ml, "id") == l->id)
                    if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, l->id); fp && fp->hash != jstr(ml, "content_hash"))
                        ui.label("Version du niveau différente de celle du serveur", {x, zoneD_.bottom() - ui.s(14)}, 14, ui.theme().danger,
                                 Align::Left, FontFace::Body, VAlign::Middle);
        ui.popId();
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
        profileOpen_ = true;
        profileLoading_ = true;
        profile_ = Json();
        profileError_.clear();
        friendMessage_.clear();
        int gen = ++profileGen_;
        g_.online.get("/api/v1/players/" + playerId, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
            if (gen != profileGen_) return;
            profileLoading_ = false;
            if (r.ok) profile_ = r.data;
            else profileError_ = r.message;
        }));
    }

    void drawProfile() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(330), ui.screenH() * 0.5f - ui.s(300), ui.s(660), ui.s(600)};
        ui.r().fillRoundedRect(box, ui.s(16), Color{10, 16, 34, 245});
        ui.r().drawRoundedRect(box, ui.s(16), ui.theme().accent.withAlphaF(0.6f), ui.s(1.5f));
        ui.pushId("online/profile");
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("close", {box.right() - ui.s(120), box.y + ui.s(14), ui.s(106), ui.s(34)}, "Fermer", st)) profileOpen_ = false;
        if (profileLoading_) {
            ui.label("Chargement du profil…", box.center(), 20, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        } else if (!profileError_.empty()) {
            ui.labelWrapped(profileError_, {box.x + ui.s(30), box.center().y}, box.w - ui.s(60), 18, ui.theme().danger);
        } else if (profile_.is_object()) {
            const Json& p = profile_["player"];
            bool hidden = jget<bool>(p, "hidden", false);
            std::string pid = jstr(p, "id");
            ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), pid, {box.x + ui.s(24), box.y + ui.s(24), ui.s(96), ui.s(96)});
            ui.label(jstr(p, "username"), {box.x + ui.s(140), box.y + ui.s(50)}, 30, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
            std::string cc = jstr(p, "country_code");
            if (!cc.empty()) {
                ui_.drawFlag(cc, {box.x + ui.s(140), box.y + ui.s(78), ui.s(28), ui.s(21)});
                ui.label(ui_.countryName(cc), {box.x + ui.s(176), box.y + ui.s(89)}, 17, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            }
            if (hidden) {
                ui.labelWrapped("Ce joueur a choisi de masquer son profil public. Seuls son pseudonyme et son avatar sont visibles.",
                                {box.x + ui.s(30), box.y + ui.s(160)}, box.w - ui.s(60), 18, ui.theme().textDim);
            } else {
                std::string since = jstr(p, "created_at").substr(0, 10);
                ui.label("Inscrit le " + since, {box.x + ui.s(140), box.y + ui.s(112)}, 15, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
                const Json& s = profile_.value("statistics", Json::object());
                float y = box.y + ui.s(150);
                float cw = (box.w - ui.s(48)) / 4;
                auto stat = [&](int i, const std::string& v, const std::string& k) {
                    Rect c{box.x + ui.s(24) + i * cw, y, cw - ui.s(10), ui.s(70)};
                    ui.r().fillRoundedRect(c, ui.s(10), Color{18, 28, 54, 230});
                    ui.label(v, {c.center().x, c.y + ui.s(26)}, 26, ui.theme().accent, Align::Center, FontFace::Title, VAlign::Middle);
                    ui.label(k, {c.center().x, c.y + ui.s(54)}, 13, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
                };
                stat(0, std::to_string(jget<int>(s, "verified_records", 0)), "records vérifiés");
                stat(1, std::to_string(jget<int>(s, "first_places", 0)), "premières places");
                stat(2, std::to_string(jget<int>(s, "levels_completed", 0)), "niveaux classés");
                stat(3, std::to_string(jget<int>(s, "planets_completed", 0)), "planètes terminées");
                y += ui.s(90);
                ui.label("Meilleurs temps", {box.x + ui.s(24), y}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                y += ui.s(22);
                const Json bests = s.value("best_times", Json::array());
                Rect view{box.x + ui.s(24), y, box.w - ui.s(48), box.bottom() - ui.s(76) - y};
                ui.beginScroll("bests", view, static_cast<float>(bests.size()) * ui.s(30));
                float yy = view.y - ui.scrollOffset();
                for (const auto& b : bests) {
                    RunCategory cat;
                    std::string catLabel = categoryFromId(jstr(b, "category"), cat) ? categoryLabel(cat) : jstr(b, "category");
                    ui.label(jstr(b, "level_name") + " · " + catLabel, {view.x, yy + ui.s(14)}, 16, ui.theme().text, Align::Left, FontFace::Body,
                             VAlign::Middle);
                    int rk = jget<int>(b, "rank", 0);
                    ui.label("n°" + std::to_string(rk), {view.right() - ui.s(130), yy + ui.s(14)}, 16,
                             rk <= 3 ? GlobeRenderer::tierColor(markerTier(rk)) : ui.theme().textDim, Align::Right, FontFace::Bold, VAlign::Middle);
                    ui.label(formatRunTime(jget<int64_t>(b, "time_ms", 0)), {view.right() - ui.s(8), yy + ui.s(14)}, 16, ui.theme().text,
                             Align::Right, FontFace::Bold, VAlign::Middle);
                    yy += ui.s(30);
                }
                if (bests.empty())
                    ui.label("Aucun record vérifié pour l'instant.", {view.x, view.y + ui.s(14)}, 16, ui.theme().textDim, Align::Left,
                             FontFace::Body, VAlign::Middle);
                ui.endScroll();
            }
            // Friendship
            if (g_.online.loggedIn() && pid != g_.online.playerId()) {
                std::string fr = jstr(p, "friendship");
                std::string label = fr == "friends" ? "Retirer des amis" : fr == "request_sent" ? "Demande envoyée" : fr == "request_received" ? "Accepter la demande" : "Ajouter en ami";
                UIContext::ButtonStyle fs;
                fs.small = true;
                fs.enabled = fr != "request_sent";
                if (ui.button("friend", {box.x + ui.s(24), box.bottom() - ui.s(56), ui.s(220), ui.s(38)}, label, fs)) friendAction(pid, fr);
                if (!friendMessage_.empty())
                    ui.label(friendMessage_, {box.x + ui.s(260), box.bottom() - ui.s(37)}, 15, ui.theme().textDim, Align::Left, FontFace::Body,
                             VAlign::Middle);
            }
        }
        ui.popId();
    }

    void friendAction(const std::string& pid, const std::string& state) {
        auto done = OnlineService::guard(life_, [this, pid](const ApiResult& r) {
            friendMessage_ = r.ok ? "C'est fait." : r.message;
            if (r.ok) openProfile(pid);
        });
        if (state == "friends") g_.online.send("DELETE", "/api/v1/friends/" + pid, Json(), done);
        else if (state == "request_received") g_.online.send("POST", "/api/v1/friends/accept", {{"player_id", pid}}, done);
        else g_.online.send("POST", "/api/v1/friends/request", {{"player_id", pid}}, done);
    }

    // -------------------------------------------------------- rank change
    void startRankAnimation(const OnlineService::RankChange& c) {
        rankAnim_ = c;
        rankAnimT_ = 0.0f;
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i]->id == c.levelId) levelIdx_ = static_cast<int>(i);
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
    std::string wantedLevel_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    MenuBackground bg_;
    GlobeRenderer globe_;
    GlobeCamera cam_;
    GlobeView globeView_;
    float t_ = 0.0f;
    float idle_ = 10.0f;
    Rect header_, zoneA_, zoneB_, zoneC_, zoneD_, infoRect_, globeButtons_;

    std::vector<const LevelEntry*> levels_;
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
    bool pressing_ = false, dragged_ = false;
    Vec2 pressPos_;

    bool profileOpen_ = false;
    bool profileLoading_ = false;
    Json profile_;
    std::string profileError_;
    std::string friendMessage_;
    int profileGen_ = 0;

    std::optional<OnlineService::RankChange> rankAnim_;
    float rankAnimT_ = 10.0f;
    std::string shockCountry_;
    float shockT_ = 2.0f;
    std::map<std::string, TexturePtr> thumbs_;
};

}  // namespace

std::unique_ptr<Scene> makeOnlineRanking(GameServices& g, const std::string& levelId) {
    return std::make_unique<OnlineRankingScene>(g, levelId);
}

}  // namespace cosmic
