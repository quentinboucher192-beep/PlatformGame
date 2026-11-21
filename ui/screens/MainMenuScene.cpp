// Splash screen and main menu.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/core/Easing.h"
#include "engine/core/I18n.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/tutorial/Tutorial.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/GiftArt.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/MainMenuLayout.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/CoopGameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/CosmicStyle.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

MainMenuLayout MainMenuLayout::compute(float W, float H, bool hasContinue, bool admin) {
    const cstyle::CosmicLayout L = cstyle::CosmicLayout::of(W, H);
    MainMenuLayout m;
    m.unit = L.unit;
    // Plates: the five of the mockup between y 112 and 508, centred there when « Continuer » is absent.
    const float total = (hasContinue ? 100.0f + 10.0f : 0.0f) + 4 * 64.0f + 3 * 10.0f;
    float y = 112.0f + (396.0f - total) * 0.5f;
    for (int i = 0; i < (hasContinue ? 5 : 4); ++i) {
        const float h = hasContinue && i == 0 ? 100.0f : 64.0f;
        m.plates.push_back(L.rect(52, y, 420, h));
        y += h + 10.0f;
    }
    m.diamondHalf = 28.0f * L.unit;
    for (int i = 0; i < (admin ? 7 : 6); ++i) m.diamonds.push_back(L.at(52 + 28 + i * 64.0f, 564));
    m.iconLabel = L.rect(52, 602, 440, 24);
    m.hints = L.rect(52, 642, 440, 62);
    m.titleCx = L.at(905, 0).x;
    m.titleTop = L.at(0, 40).y;
    m.title = L.rect(565, 60, 680, 180);
    m.names = L.rect(545, 249, 720, 26);
    m.card = L.rect(640, 318, 592, 236);
    m.preview = L.rect(640, 318, 300, 236);
    m.cardText = L.rect(948, 340, 262, 196);
    m.alerts = L.rect(832, 566, 400, 124);
    m.alertH = 58.0f * L.unit;
    m.alertGap = 8.0f * L.unit;
    m.chips = L.rect(760, 14, 496, 34);
    m.runwayX0 = L.at(560, 0).x;
    m.runwayX1 = W;
    m.novaX = L.at(720, 0).x;
    m.groundY = L.at(0, 694).y;
    m.warpCenter = L.at(905, 330);
    m.veilRight = L.at(620, 0).x;
    return m;
}

namespace {

class SplashScene : public Scene {
public:
    explicit SplashScene(GameServices& g) : g_(g) {}
    void onEnter() override { g_.playMusic("music/menu_theme.ogg"); }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
        bool skip = t_ > 0.6f && (g_.app.input().anyMenuInput() || g_.app.input().mousePressed(SDL_BUTTON_LEFT));
        if ((t_ > 3.2f || skip) && !leaving_) {
            leaving_ = true;
            // 2.64 / 2.62.2: whoever has not seen the tutorial yet gets it once, before the menu.
            g_.app.switchScene(tutorial::due(g_.progression) ? makeTutorialWelcome(g_) : makeMainMenu(g_), 0.6f);
        }
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.55f);
        float a = std::clamp(std::min(t_ / 0.8f, (3.2f - t_) / 0.6f), 0.0f, 1.0f);
        Vec2 c{ui.screenW() * 0.5f, ui.screenH() * 0.45f};
        ui.label("COSMIC STUDIO", {c.x, c.y - ui.s(40)}, 30, Color{143, 255, 240, 255}.withAlphaF(a), Align::Center,
                 FontFace::Title, VAlign::Middle);
        ui.label("présente", {c.x, c.y + ui.s(10)}, 22, ui.theme().textDim.withAlphaF(a), Align::Center, FontFace::Body,
                 VAlign::Middle);
    }

private:
    GameServices& g_;
    MenuBackground bg_;
    float t_ = 0.0f;
    bool leaving_ = false;
};


class MainMenuScene : public Scene {
public:
    explicit MainMenuScene(GameServices& g) : g_(g) {}
    void onEnter() override {
        g_.playMusic(g_.menuMusic());
        backdrop_.start(g_.content);
        sel_ = canContinue() ? "continue" : "campaign";
        g_.ui.setFocus("main/" + sel_);
        activity_ = g_.app.input().activityCount();
    }
    void onResume() override {
        g_.playMusic(g_.menuMusic());
        idle_ = 0.0f;  // 2.63: back from a screen (or from the trailer): a new minute to wait
        activity_ = g_.app.input().activityCount();
    }
    void update(float dt) override {
        t_ += dt;
        dt_ = dt;
        backdrop_.update(g_.ui, dt, motion());
        // 2.63, attract mode: a minute without any input on this menu (this screen on top: no
        // window over it), nothing typed, no update downloading, the tutorial done, and the
        // trailer plays, looping until someone touches anything.
        const uint64_t act = g_.app.input().activityCount();
        if (act != activity_ || !attractAllowed(g_)) {
            activity_ = act;
            idle_ = 0.0f;
        } else if ((idle_ += dt) >= kAttractDelay) {
            idle_ = 0.0f;
            if (auto trailer = makeTrailer(g_, true)) g_.app.pushScene(std::move(trailer));
        } else if (idle_ >= kAttractDelay - 10.0f) {
            preloadTrailer();
        }
        // The installer asked from the alert is downloaded and checked: run it, close the game.
        if (std::string v; g_.updates.takeLaunch(&v) && g_.updates.launch(v)) g_.app.quit();
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const cstyle::Motion mo = motion();
        const bool cont = canContinue();
        // ADMINISTRATION (2.40): only for an account with the admin role, never for the others.
        const bool admin = g_.online.isAdmin();
        const MainMenuLayout L = MainMenuLayout::compute(ui.screenW(), ui.screenH(), cont, admin);
        const float u = L.unit;

        backdrop_.render(ui, L.warpCenter, L.veilRight, u, mo);
        title_.draw(ui, L.titleCx, L.titleTop, u, t_, mo);
        backdrop_.drawNames(ui, L.names.center(), L.names.w, 18.0f, u);
        runner_.draw(ui, L.runwayX0, L.runwayX1, L.novaX, L.groundY, u, t_, mo);

        ui.pushId("main");
        // ----- the plates
        std::vector<std::pair<std::string, cstyle::Plate>> plates;
        if (cont) {
            const LevelEntry* last = g_.content.level(g_.progression.lastLevel);
            const PlanetData* p = g_.content.planet(g_.content.campaignPlanetOf(last->id));
            cstyle::Plate pl{"Continuer", last->name + (p ? " · " + p->name : std::string()), "continue", {255, 205, 110, 255}, true};
            if (p && !p->banner.empty() && g_.app.resources().exists(p->banner)) pl.thumb = g_.app.resources().texture(p->banner, false);
            if (p) {
                const auto levels = g_.content.levelsForPlanet(p->id);
                int done = 0;
                for (const LevelEntry* l : levels) done += g_.progression.levelCompleted(l->id) ? 1 : 0;
                pl.progress = levels.empty() ? -1.0f : static_cast<float>(done) / levels.size();
            }
            plates.push_back({"continue", pl});
        }
        const auto [levelsDone, levelsAll] = campaignCount();
        plates.push_back({"campaign", {"Campagne", std::to_string(backdrop_.planets().size()) + " planètes · " + std::to_string(levelsDone) + " / " +
                                                      std::to_string(levelsAll) + " niveaux",
                                       "campaign", {94, 224, 255, 255}}});
        plates.push_back({"challenges", {"Défis", challengeSubtitle(), "challenges", {255, 112, 136, 255}}});
        plates.push_back({"coop", {"Coopération", "2 ou 3 joueurs · niveaux EXTRÊME", "coop", {190, 150, 255, 255}}});
        {
            std::string onlineSub = g_.online.loggedIn() ? g_.online.username() : "compte requis";
            if (int n = g_.online.loggedIn() ? g_.online.pendingFriendRequests() : 0; n > 0)
                onlineSub += " · " + std::to_string(n) + (n > 1 ? " demandes d'amis" : " demande d'ami");
            cstyle::Plate pl{"En ligne", onlineSub, "online", {110, 225, 150, 255}};
            pl.badge = g_.online.loggedIn() ? g_.online.unreadMessages() : 0;  // unread messages, as on SOCIAL
            plates.push_back({"online", pl});
        }
        std::string activated;
        for (size_t i = 0; i < plates.size(); ++i) {
            auto& [id, pl] = plates[i];
            pl.feature = "menu." + id;
            const float appear = mo.reduced ? 1.0f : std::clamp((t_ - 0.2f - 0.08f * i) / 0.6f, 0.0f, 1.0f);
            if (cstyle::plateButton(ui, anims_, id, L.plates[i], pl, u, appear, t_, dt_, mo)) activated = id;
        }
        // ----- the icon row
        struct Side {
            const char* id;
            const char* label;
            int badge;
        };
        const int newsUnread = g_.news.unread(), onlineUnread = g_.online.loggedIn() ? g_.online.unreadNotifications() : 0;
        std::vector<Side> side{{"news", "Notifications", newsUnread + onlineUnread},
                               {"achievements", "Succès", 0},
                               {"collection", "Collection", 0},
                               {"stats", "Statistiques", 0},
                               {"settings", "Paramètres", 0}};
        if (admin) side.push_back({"admin", "Administration", 0});
        side.push_back({"quit", "Quitter", 0});
        std::string sideLabel;
        bool sideNew = false;
        for (size_t i = 0; i < side.size() && i < L.diamonds.size(); ++i) {
            cstyle::Diamond d{side[i].id, side[i].badge, std::string(side[i].id) == "quit", std::string("menu.") + side[i].id};
            if (d.danger) d.feature.clear();
            const float appear = mo.reduced ? 1.0f : std::clamp((t_ - 0.6f - 0.06f * i) / 0.5f, 0.0f, 1.0f);
            bool fresh = false;
            if (cstyle::diamondButton(ui, anims_, side[i].id, L.diamonds[i], L.diamondHalf, d, appear, t_, dt_, &fresh)) activated = side[i].id;
            if (ui.focused(side[i].id)) {
                sideLabel = d.danger && confirmQuit_ ? "Confirmer : quitter" : side[i].label;
                sideNew = fresh;
            }
        }
        if (!sideLabel.empty()) {  // the focused icon's name (and its « NOUVEAU »), fitted to the row
            const Rect& r = L.iconLabel;
            const float pill = sideNew ? ui.newPillWidth(true) + 10.0f * u : 0.0f;
            const std::string s = cstyle::upper(i18n::tr(sideLabel));
            const float f = ui.fitFont(s, FontFace::Bold, 15.0f * u / ui.scale(), r.w - pill, 0.7f);
            const std::string e = ui.elide(s, FontFace::Bold, f, r.w - pill);
            const Vec2 sz = ui.label(e, {r.x, r.center().y}, f, confirmQuit_ && sel_ == "quit" ? Color{255, 106, 106, 255} : Color{255, 205, 110, 255},
                                     Align::Left, FontFace::Bold, VAlign::Middle);
            if (sideNew) ui.drawNewPill(r.x + sz.x + pill, r.center().y, true);
        }
        // ----- which entry is chosen (the card, the runner's jump, the quit's confirmation)
        std::string now = sel_;
        for (const auto& [id, pl] : plates)
            if (ui.focused(id)) now = id;
        for (const Side& s : side)
            if (ui.focused(s.id)) now = s.id;
        if (now != sel_) {
            sel_ = now;
            card_.flicker(t_);
            runner_.jump(t_);
        }
        if (sel_ != "quit") confirmQuit_ = false;
        if (ui.backPressed() && sel_ != "quit") ui.setFocus("quit");  // Échap / B: to « Quitter »
        // ----- the holographic card (a click on it opens the chosen entry)
        card_.begin(ui, L.card, t_, u, mo);
        drawPreview(ui, L.preview, u);
        cstyle::HoloCard::text(ui, L.cardText, cardText(), u);
        card_.end(ui, t_);
        if (ui.mouseClick("card", L.card)) activated = sel_;
        // ----- alerts, top chips, hints
        Rect slot{L.alerts.x, L.alerts.y, L.alerts.w, L.alertH};
        if (drawUpdateAlert(slot)) slot.y += L.alertH + L.alertGap;
        drawGiftAlert(slot);
        ui.popId();
        drawChips(L);
        drawHints(L);
        if (!activated.empty()) activate(activated);
        ui.end();
    }

private:
    cstyle::Motion motion() const { return cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake); }

    /// Only a campaign level: WORKSHOP levels are reached from the WORKSHOP.
    bool canContinue() const {
        return !g_.progression.lastLevel.empty() && g_.content.level(g_.progression.lastLevel) &&
               !g_.content.campaignPlanetOf(g_.progression.lastLevel).empty();
    }

    /// Levels completed and levels of the campaign (the planets of the backdrop).
    std::pair<int, int> campaignCount() const {
        int done = 0, all = 0;
        for (const auto& p : backdrop_.planets())
            for (const LevelEntry* l : g_.content.levelsForPlanet(p.id)) {
                ++all;
                done += g_.progression.levelCompleted(l->id) ? 1 : 0;
            }
        return {done, all};
    }

    void activate(const std::string& id) {
        if (id == "continue" && canContinue()) g_.app.switchScene(makeGameScene(g_, g_.progression.lastLevel), 0.5f);
        else if (id == "campaign") g_.app.switchScene(makePlanetMap(g_));
        else if (id == "challenges") g_.app.switchScene(makeChallenges(g_), 0.3f);
        else if (id == "coop") g_.app.switchScene(makeCoopLobby(g_), 0.3f);
        else if (id == "online") g_.app.switchScene(makeOnlineRanking(g_));
        else if (id == "news") {
            // Straight to the Cosmic Online centre when only it has something new.
            const int online = g_.online.loggedIn() ? g_.online.unreadNotifications() : 0;
            g_.app.switchScene(makeNotifications(g_, g_.news.unread() == 0 && online > 0 ? 1 : 0), 0.3f);
        } else if (id == "achievements") g_.app.switchScene(makeAchievements(g_), 0.3f);
        else if (id == "collection") g_.app.switchScene(makeCollection(g_), 0.3f);
        else if (id == "stats") g_.app.switchScene(makeStatistics(g_), 0.3f);  // 2.54: the player's statistics, the bosses' within
        else if (id == "settings") g_.app.pushScene(makeSettingsScene(g_, false));
        else if (id == "admin" && g_.online.isAdmin()) g_.app.switchScene(makeAdmin(g_), 0.3f);
        else if (id == "quit") {
            if (confirmQuit_) g_.app.quit();
            confirmQuit_ = true;
            card_.flicker(t_);
        }
    }

    // ------------------------------------------------------------- the card

    cstyle::HoloCard::Text cardText() const {
        cstyle::HoloCard::Text c;
        const std::string& id = sel_;
        if (id == "continue" && canContinue()) {
            const LevelEntry* last = g_.content.level(g_.progression.lastLevel);
            c.kicker = "Reprendre";
            c.head = last->name;
            if (const PlanetData* p = g_.content.planet(g_.content.campaignPlanetOf(last->id))) {
                const auto levels = g_.content.levelsForPlanet(p->id);
                const int at = static_cast<int>(std::find(levels.begin(), levels.end(), last) - levels.begin()) + 1;
                c.para = p->name + " · niveau " + std::to_string(at) + " sur " + std::to_string(levels.size());
            }
            c.meta = g_.progression.levelCompleted(last->id) ? "Niveau terminé" : "Niveau pas encore terminé";
            if (double s = g_.progression.levelPlayTime(last->id); s >= 60.0) c.meta += " · " + playTimeText(s) + " de jeu";
        } else if (id == "campaign") {
            const auto [done, all] = campaignCount();
            c.kicker = "Campagne";
            c.head = std::to_string(backdrop_.planets().size()) + " planètes";
            for (const auto& p : backdrop_.planets()) c.para += (c.para.empty() ? "" : ", ") + p.name;
            c.meta = std::to_string(done) + " niveaux terminés sur " + std::to_string(all);
        } else if (id == "challenges") {
            const ChallengeService& s = g_.challenges;
            if (const DailyChallenge* d = s.current()) {
                c.kicker = d->kind == ChallengeKind::Weekly ? "Défi de la semaine" : d->kind == ChallengeKind::Monthly ? "Défi du mois" : "Défi du jour";
                c.head = d->level.title;
                const int64_t left = std::max<int64_t>(0, (d->endsAtMs - s.nowMs()) / 1000);
                char b[40];
                std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", static_cast<long long>(left / 3600), static_cast<long long>(left / 60 % 60),
                              static_cast<long long>(left % 60));
                c.big = b;
                const ChallengeResult* r = s.result(d->id);
                c.para = r && r->bestMs > 0 ? "Votre meilleur temps : " + formatRunTime(r->bestMs) : "Pas encore terminé";
                if (d->silverMs > 0 && d->goldMs > 0) c.meta = "Argent " + formatRunTime(d->silverMs) + " · Or " + formatRunTime(d->goldMs);
            } else {
                c.kicker = "Défis";
                c.head = challengeSubtitle();
                c.para = "Un défi chaque jour, chaque semaine et chaque mois.";
            }
        } else if (id == "coop") {
            c.kicker = "Coopération";
            c.head = "Duo ou trio";
            c.para = "2 ou 3 joueurs sur les niveaux EXTRÊME.";
        } else if (id == "online") {
            c.kicker = "COSMIC ONLINE";
            if (g_.online.loggedIn()) {
                c.head = g_.online.username();
                c.headVerbatim = true;
                const int f = g_.online.friendCount(), q = g_.online.pendingFriendRequests(), m = g_.online.unreadMessages();
                auto add = [&](const std::string& s) { c.meta += (c.meta.empty() ? "" : " · ") + s; };
                if (f > 0) add(std::to_string(f) + (f > 1 ? " amis" : " ami"));
                if (q > 0) add(std::to_string(q) + (q > 1 ? " demandes d'amis" : " demande d'ami"));
                if (m > 0) add(std::to_string(m) + (m > 1 ? " messages non lus" : " message non lu"));
            } else {
                c.head = "Compte requis";
                c.meta = "Le jeu reste jouable sans compte.";
            }
            c.para = "Classements, clans, raids et niveaux de la communauté.";
        } else if (id == "news") {
            const int n = g_.news.unread(), online = g_.online.loggedIn() ? g_.online.unreadNotifications() : 0;
            c.kicker = "Notifications";
            c.head = n > 0 ? std::to_string(n) + (n > 1 ? " nouveautés" : " nouveauté") : "Rien de nouveau";
            for (const auto& it : g_.news.items())
                if (!it.read) {
                    c.para = it.title;
                    break;
                }
            if (online > 0) c.meta = std::to_string(online) + " en ligne";
        } else if (id == "achievements") {
            c.kicker = "Succès";
            c.head = std::to_string(g_.achievements.stars(g_.achievementCatalog)) + " / " + std::to_string(g_.achievementCatalog.totalStars()) + " étoiles";
        } else if (id == "collection") {
            const auto [got, all] = collectionCount();
            c.kicker = "Collection";
            c.head = std::to_string(got) + " / " + std::to_string(all) + " récompenses";
            c.para = "Combinaisons, traînées, cadres, titres, effets de victoire et badges de profil.";
        } else if (id == "stats") {
            c.kicker = "Statistiques";
            c.head = playTimeText(g_.progression.playTimeSeconds) + " de jeu";
            const int kills = bossKills();
            if (kills > 0) c.meta = std::to_string(kills) + (kills > 1 ? " victoires sur les boss" : " victoire sur un boss");
        } else if (id == "settings") {
            c.kicker = "Paramètres";
            c.head = "Réglages";
            c.para = "Graphismes, audio, clavier, manette et langue.";
        } else if (id == "admin") {
            c.kicker = "Administration";
            c.head = "Outils admin";
            c.para = "Joueurs, défis et journal.";
            c.meta = "Visible seulement pour un compte admin";
        } else if (id == "quit") {
            c.kicker = "Quitter";
            c.head = confirmQuit_ ? "Validez encore pour quitter" : "Quitter le jeu ?";
            c.bigColor = Color{255, 106, 106, 255};
            if (!confirmQuit_) c.para = "Validez une seconde fois pour confirmer.";
        }
        return c;
    }

    std::pair<int, int> collectionCount() const {
        auto p = g_.cosmeticProgress();
        int got = 0, all = 0;
        for (const auto& it : g_.cosmetics.items()) {
            if (!cosmeticShown(it, p)) continue;  // 2.64: a souvenir counts once earned
            ++all;
            got += cosmeticUnlocked(it, p) ? 1 : 0;
        }
        return {got, all};
    }
    int bossKills() const {
        int kills = 0;
        for (const auto& [id, r] : g_.bossStats.all()) kills += r.kills;
        return kills;
    }

    /// The animated preview of the chosen entry (only what the game knows).
    void drawPreview(UIContext& ui, const Rect& pv, float u) {
        Renderer& rd = ui.r();
        ResourceManager& res = g_.app.resources();
        const Vec2 c = pv.center();
        const float t = t_, since = t_ - selT_;
        if (selId_ != sel_) selId_ = sel_, selT_ = t_;
        rd.drawGlow(c, 150.0f * u, Color{94, 224, 255, 40});
        auto nova = [&](const char* clip, float fps, Vec2 at, float scale, bool flip, const TexturePtr& sheet = nullptr, float alpha = 1.0f) {
            AnimationSetPtr set = res.animation("animations/nova.json");
            const AnimationClip* cl = set ? set->clip(clip) : nullptr;
            const Texture* tex = sheet ? sheet.get() : set ? set->texture.get() : nullptr;
            if (!cl || !tex || cl->frameCount() == 0) return;
            Rect src = set->frameRect(*cl, static_cast<int>(std::floor(t * fps)) % cl->frameCount());
            rd.drawTexture(*tex, &src, Rect::fromCenter(at, {32.0f * scale * u, 32.0f * scale * u}), Color::white().withAlphaF(alpha), BlendMode::Alpha, flip);
        };
        auto suit = [&](const char* id) { return res.exists(id) ? res.texture(id, true) : TexturePtr(); };
        const std::string& id = sel_;
        if (id == "continue" && canContinue()) {
            const PlanetData* p = g_.content.planet(g_.content.campaignPlanetOf(g_.progression.lastLevel));
            const Rect frame{pv.x + 18.0f * u, pv.y + 22.0f * u, 264.0f * u, 192.0f * u};
            if (p && !p->banner.empty() && res.exists(p->banner))
                if (TexturePtr b = res.texture(p->banner, false)) {
                    const float z = 1.08f + 0.06f * std::sin(t * 0.3f);
                    rd.pushClip(frame);
                    const float bw = frame.h * z * b->width() / std::max(1, b->height()), bh = frame.h * z;
                    rd.drawTexture(*b, nullptr, Rect::fromCenter(frame.center(), {std::max(bw, frame.w * z), bh}));
                    rd.popClip();
                    rd.drawRoundedRect(frame, 10.0f * u, Color{255, 255, 255, 50}, u);
                }
            nova("run", 12.0f, {pv.x + (60.0f + std::fmod(since * 60.0f, 180.0f)) * u, pv.y + 178.0f * u}, 1.6f, false);
        } else if (id == "campaign") {
            rd.drawGlow(c, 40.0f * u, Color{255, 217, 138, 150});
            rd.fillCircle(c, 12.0f * u, Color{255, 217, 138, 255});
            int i = 0;
            for (const auto& p : backdrop_.planets()) {
                const PlanetData* pd = g_.content.planet(p.id);
                const float r = (34.0f + i * 16.0f) * u, a = t * (0.7f - i * 0.08f) + i * 1.3f;
                for (int k = 0; k < 40; ++k) {
                    const float b = k * 0.157f;
                    rd.fillRect({c.x + std::cos(b) * r * 1.15f, c.y + std::sin(b) * r * 0.6f, u, u}, Color{140, 180, 255, 70});
                }
                const Vec2 at{c.x + std::cos(a) * r * 1.15f, c.y + std::sin(a) * r * 0.6f};
                if (pd && !pd->illustration.empty() && res.exists(pd->illustration)) {
                    if (TexturePtr tex = res.texture(pd->illustration, false)) rd.drawTexture(*tex, nullptr, Rect::fromCenter(at, {22.0f * u, 22.0f * u}));
                } else {
                    rd.fillCircle(at, 8.0f * u, p.color);
                }
                ++i;
            }
        } else if (id == "challenges") {
            if (TexturePtr m = res.texture("ui/icons_medals.png", true))
                for (int i = 0; i < 3; ++i) {
                    Rect src{(i + 1) * 32.0f, 0, 32, 32};
                    const float bob = std::sin(t * 3.0f + i) * 5.0f * u;
                    rd.drawTexture(*m, &src, {pv.x + (50.0f + i * 70.0f) * u, c.y - 40.0f * u + bob, 56.0f * u, 56.0f * u});
                }
            if (const DailyChallenge* d = g_.challenges.current(); d && d->endsAtMs > d->startsAtMs) {
                const float k = std::clamp(static_cast<float>(g_.challenges.nowMs() - d->startsAtMs) / static_cast<float>(d->endsAtMs - d->startsAtMs), 0.0f, 1.0f);
                const Vec2 at{c.x, c.y + 60.0f * u};
                rd.drawRing(at, 26.0f * u, 4.0f * u, Color{255, 112, 136, 50});
                rd.drawArc(at, 26.0f * u, 4.0f * u, -1.5708f, -1.5708f + 6.2832f * k, Color{255, 112, 136, 255});
            }
        } else if (id == "coop") {
            const TexturePtr sheets[3] = {nullptr, suit("sprites/player/suits/nova_nebuleuse.png"), suit("sprites/player/suits/nova_givre.png")};
            for (int i = 0; i < 3; ++i) nova("run", 13.0f, {pv.x + (90.0f + i * 60.0f) * u, c.y + (20.0f + std::sin(t * 6.0f + i) * 3.0f) * u}, 2.2f, false, sheets[i]);
            rd.fillRect({pv.x + 40.0f * u, c.y + 58.0f * u, 220.0f * u, 3.0f * u}, Color{190, 150, 255, 255});
        } else if (id == "online") {
            const float R = 80.0f * u;
            rd.fillCircle(c, R, Color{12, 42, 74, 255});
            for (int la = -80; la <= 80; la += 10)
                for (int lo = 0; lo < 360; lo += 8) {
                    const float L = (lo + t * 20.0f) * 0.01745f, P = la * 0.01745f, z = std::cos(P) * std::cos(L);
                    if (z < 0.0f) continue;
                    const bool land = std::sin(lo * 0.09f + la * 0.05f) + std::cos(lo * 0.05f - la * 0.08f) > 0.6f;
                    const Color col = land ? Color{110, 225, 150, 255}.withAlphaF(0.4f + z * 0.6f) : Color{94, 224, 255, 255}.withAlphaF(0.15f * z);
                    rd.fillRect({c.x + std::cos(P) * std::sin(L) * R - u, c.y - std::sin(P) * R - u, 2.0f * u, 2.0f * u}, col);
                }
            rd.drawRing(c, R, std::max(1.0f, u), Color{94, 224, 255, 150});
            const float pings[4][2] = {{0.3f, 0.2f}, {-0.4f, -0.3f}, {0.1f, -0.5f}, {-0.2f, 0.45f}};
            const int shown = g_.online.loggedIn() ? std::min(4, g_.online.friendCount()) : 0;  // one ping per friend, up to four
            for (int i = 0; i < shown; ++i) {
                const float p = std::fmod(t * 0.8f + i * 0.25f, 1.0f);
                rd.drawRing({c.x + pings[i][0] * R, c.y + pings[i][1] * R}, (3.0f + p * 14.0f) * u, std::max(1.0f, u), Color{110, 225, 150, 255}.withAlphaF(1.0f - p));
            }
        } else if (id == "news") {
            const float sw = std::sin(t * 5.0f) * 14.0f * std::max(0.0f, 1.0f - std::fmod(t, 3.0f) / 1.2f);
            cstyle::menuIcon(ui, "news", Rect::fromCenter({c.x, c.y + 6.0f * u}, {72.0f * u, 72.0f * u}), sw);
            const int n = g_.news.unread() + (g_.online.loggedIn() ? g_.online.unreadNotifications() : 0);
            cstyle::badgePill(ui, {c.x + 30.0f * u, c.y - 24.0f * u}, n, 24.0f * u, Color{255, 106, 106, 255});
        } else if (id == "achievements") {
            const int all = std::max(1, g_.achievementCatalog.totalStars());
            const float k = std::clamp(static_cast<float>(g_.achievements.stars(g_.achievementCatalog)) / all, 0.0f, 1.0f) * std::min(1.0f, since / 1.2f);
            rd.drawRing(c, 74.0f * u, 12.0f * u, Color{255, 205, 110, 38});
            if (k > 0.0f) rd.drawArc(c, 74.0f * u, 12.0f * u, -1.5708f, -1.5708f + 6.2832f * k, Color{255, 205, 110, 255});
            star(rd, c, 32.0f * u, std::sin(t) * 0.2f, Color{255, 205, 110, 255});
            for (int i = 0; i < 8; ++i) {
                const float a = t + i * 0.785f;
                star(rd, {c.x + std::cos(a) * 100.0f * u, c.y + std::sin(a) * 100.0f * u}, 4.0f * u, t * 3.0f, Color{255, 205, 110, 150});
            }
        } else if (id == "collection") {
            const TexturePtr sheets[3] = {nullptr, suit("sprites/player/suits/nova_nebuleuse.png"), suit("sprites/player/suits/nova_givre.png")};
            for (int i = 0; i < 3; ++i) {
                const float a = t * 0.8f + (i - 1) * 2.09f, d = (std::cos(a) + 1.0f) * 0.5f;
                nova(i == 0 ? "idle" : "run", 8.0f, {c.x + std::sin(a) * 90.0f * u, c.y + 10.0f * u}, 1.6f + 1.4f * d, false, sheets[i], 0.4f + 0.6f * d);
            }
        } else if (id == "stats") {
            // The bosses beaten most, one bar each (the player's own numbers).
            std::vector<int> kills;
            for (const auto& [bid, r] : g_.bossStats.all())
                if (r.kills > 0) kills.push_back(r.kills);
            std::sort(kills.rbegin(), kills.rend());
            if (kills.size() > 6) kills.resize(6);
            static const Color cols[6] = {{94, 224, 255, 255}, {190, 150, 255, 255}, {110, 225, 150, 255}, {255, 205, 110, 255}, {255, 112, 136, 255}, {94, 224, 255, 255}};
            if (kills.empty()) {
                cstyle::menuIcon(ui, "stats", Rect::fromCenter(c, {96.0f * u, 96.0f * u}));
            } else {
                const float grow = std::min(1.0f, since / 0.8f), bw = 24.0f * u, gap = 12.0f * u;
                float x = c.x - (kills.size() * (bw + gap) - gap) * 0.5f;
                for (size_t i = 0; i < kills.size(); ++i, x += bw + gap) {
                    const float h = 130.0f * u * kills[i] / kills.front() * grow;
                    rd.fillRoundedRect({x, pv.y + 190.0f * u - h, bw, h}, std::min(4.0f * u, h * 0.5f), cols[i]);
                }
                rd.fillRect({pv.x + 40.0f * u, pv.y + 191.0f * u, 224.0f * u, std::max(1.0f, u)}, Color{255, 255, 255, 50});
            }
        } else if (id == "settings") {
            cstyle::menuIcon(ui, "settings", Rect::fromCenter({c.x - 20.0f * u, c.y}, {96.0f * u, 96.0f * u}), t * 46.0f);
            cstyle::menuIcon(ui, "settings", Rect::fromCenter({c.x + 52.0f * u, c.y - 40.0f * u}, {48.0f * u, 48.0f * u}), -t * 92.0f);
        } else if (id == "admin") {
            const float s = 96.0f * u * (1.0f + 0.05f * std::sin(t * 3.0f));
            cstyle::menuIcon(ui, "admin", Rect::fromCenter(c, {s, s}));
        } else if (id == "quit") {
            nova("idle", 6.0f, {c.x, c.y + 10.0f * u}, 3.0f, true);
            rd.fillRect(pv, Color{255, 106, 106, 255}.withAlphaF(confirmQuit_ ? 0.25f : 0.08f));
        }
    }

    static void star(Renderer& rd, Vec2 c, float r, float rot, Color col) {
        Vec2 pts[10];
        for (int i = 0; i < 10; ++i) {
            const float rr = i % 2 ? r * 0.45f : r, a = -1.5708f + rot + i * 0.6283f;
            pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
        }
        for (int i = 0; i < 10; ++i) rd.fillTriangle(c, pts[i], pts[(i + 1) % 10], col);
    }

    // ------------------------------------------------------------ top, bottom

    void drawChips(const MainMenuLayout& L) {
        UIContext& ui = g_.ui;
        const float u = L.unit, h = L.chips.h, f = 13.0f * u / ui.scale(), px = 13.0f * u;
        float right = L.chips.right();
        auto chip = [&](const std::string& text, bool dot, bool verbatim) {
            i18n::Verbatim v(verbatim);
            const float room = right - L.chips.x - 26.0f * u - (dot ? 15.0f * u : 0.0f);
            if (room < 30.0f * u) return;
            const std::string e = ui.elide(text, FontFace::Bold, f, room);
            const float w = ui.text().measure(e, FontFace::Bold, px).x + 24.0f * u + (dot ? 15.0f * u : 0.0f);
            const Rect r{right - w, L.chips.y, w, h};
            ui.r().fillRoundedRect(r, h * 0.5f, Color{10, 18, 42, 190});
            ui.r().drawRoundedRect(r, h * 0.5f, Color{44, 58, 104, 255}, std::max(1.0f, u));
            float x = r.x + 12.0f * u;
            if (dot) {
                ui.r().drawGlow({x + 4.0f * u, r.center().y}, 10.0f * u, Color{110, 225, 150, 120});
                ui.r().fillCircle({x + 4.0f * u, r.center().y}, 4.0f * u, Color{110, 225, 150, 255});
                x += 15.0f * u;
            }
            ui.label(e, {x, r.center().y}, f, Color{230, 236, 250, 255}, Align::Left, FontFace::Bold, VAlign::Middle);
            right = r.x - 8.0f * u;
        };
        chip(std::string("v") + COSMIC_VERSION_STRING, false, true);
        if (g_.online.loggedIn()) chip(g_.online.username(), true, true);  // connected: the account's name
    }

    void drawHints(const MainMenuLayout& L) {
        UIContext& ui = g_.ui;
        const float u = L.unit, gh = 26.0f * u;
        float x = L.hints.x;
        const float y = L.hints.y + gh * 0.5f;
        x += Glyphs::drawHint(ui, Action::MenuConfirm, "Valider", {x, y}, gh) + 24.0f * u;
        if (x < L.hints.right() - 120.0f * u) Glyphs::drawHint(ui, Action::MenuBack, "Quitter", {x, y}, gh);
        const std::string device = g_.app.input().hasGamepad() ? "Manette : " + g_.app.input().gamepadName() : "Clavier / souris";
        const float f = 13.0f * u / ui.scale();
        ui.label(ui.elide(device, FontFace::Body, f, L.hints.w), {L.hints.x, L.hints.bottom() - 10.0f * u}, f, Color{140, 152, 186, 255}, Align::Left,
                 FontFace::Body, VAlign::Middle);
    }

    /// 2.63: the trailer's files, one per frame during the last seconds of the wait, so that it
    /// starts without a pause (its scene finds them all loaded).
    void preloadTrailer() {
        if (!g_.trailer) return;
        if (preload_.empty() && preloadNext_ == 0) {
            const story::StageAssets a = story::stageAssets(*g_.trailer);
            for (const auto& t : a.textures) preload_.push_back({t, false});
            for (const auto& s : a.animations) preload_.push_back({s, true});
        }
        if (preloadNext_ >= preload_.size()) return;
        const auto& [path, anim] = preload_[preloadNext_++];
        ResourceManager& res = g_.app.resources();
        if (!res.exists(path)) return;
        if (anim) res.animation(path);
        else res.texture(path, true);
    }

    /// A newer version exists (announced by Cosmic Online): an alert in `r` (the right column).
    /// A click downloads the installer, checks it (SHA-256), then runs it and closes the game.
    /// Returns whether it is shown.
    bool drawUpdateAlert(const Rect& r) {
        const NewsCenter::Item* it = g_.updates.offer();
        if (!it) return false;
        UIContext& ui = g_.ui;
        const std::string version = it->target;
        UpdateInstaller::State st = g_.updates.state(version);
        bool usable = g_.online.usable();
        const std::string size = it->size > 0 ? " (" + std::to_string((it->size + 524288) / 1048576) + " Mo)" : std::string();
        std::string line;
        switch (st) {
            case UpdateInstaller::State::Downloading: {
                // 2.63.8: how far it is (Mo received / expected), not only « … ».
                auto [got, total] = g_.updates.progress(version);
                const int gotMb = static_cast<int>(got / (1024 * 1024)), totalMb = static_cast<int>(total / (1024 * 1024));
                line = total > 0 ? "Téléchargement de l'installateur : " + std::to_string(gotMb) + " / " + std::to_string(totalMb) + " Mo (" +
                                       std::to_string(static_cast<int>(100.0 * static_cast<double>(got) / static_cast<double>(total))) + " %)"
                                 : "Téléchargement de l'installateur : " + std::to_string(gotMb) + " Mo…";
                break;
            }
            case UpdateInstaller::State::Checking: line = "Vérification de l'installateur (SHA-256)…"; break;
            case UpdateInstaller::State::Ready:
                line = g_.updates.error(version);
                if (line.empty()) line = "Prêt : cliquez pour lancer l'installation (le jeu se ferme).";
                break;
            case UpdateInstaller::State::Failed: line = g_.updates.error(version) + " Cliquez pour réessayer."; break;
            default:
                line = usable ? "Cliquez pour télécharger" + size + " et installer : le jeu se ferme."
                              : "Connexion à Cosmic Online nécessaire pour la télécharger.";
        }
        UIContext::ButtonStyle bs;
        bs.primary = true;
        bool busy = st == UpdateInstaller::State::Downloading || st == UpdateInstaller::State::Checking;
        bs.enabled = !busy && (usable || st == UpdateInstaller::State::Ready);
        bool clicked = ui.button("update", r, "", bs);
        // A warm mark at the left, then the two lines, each fitted to the room left (never cut).
        float pulse = 0.75f + 0.25f * std::sin(t_ * 3.0f);
        const float mr = std::min(ui.s(15), r.h * 0.26f);
        Vec2 mark{r.x + mr + ui.s(12), r.center().y};
        ui.r().fillCircle(mark, mr, ui.theme().warm.withAlphaF(pulse));
        ui.label("!", mark, std::min(22.0f, mr * 1.4f / ui.scale()), Color{20, 14, 4, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        float tx = mark.x + mr + ui.s(14), room = r.right() - ui.s(14) - tx;
        const std::string head = "Nouvelle version " + version + " disponible";
        float f1 = ui.fitFont(head, FontFace::Bold, std::min(20.0f, r.h * 0.30f / ui.scale()), room, 0.6f);
        ui.label(ui.elide(head, FontFace::Bold, f1, room), {tx, r.y + r.h * 0.32f}, f1, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        float f2 = ui.fitFont(line, FontFace::Body, std::min(15.0f, r.h * 0.24f / ui.scale()), room, 0.6f);
        Color c2 = !g_.updates.error(version).empty() ? ui.theme().danger : ui.theme().textDim;
        ui.label(ui.elide(line, FontFace::Body, f2, room), {tx, r.y + r.h * 0.68f}, f2, c2, Align::Left, FontFace::Body, VAlign::Middle);
        if (st == UpdateInstaller::State::Downloading) {
            auto [got, total] = g_.updates.progress(version);
            if (total > 0) {  // a thin bar along the bottom of the alert, inside its frame
                const float k = std::clamp(static_cast<float>(static_cast<double>(got) / static_cast<double>(total)), 0.0f, 1.0f);
                Rect bar{tx, r.bottom() - ui.s(7), room, ui.s(3)};
                ui.r().fillRoundedRect(bar, ui.s(2), Color{40, 50, 70, 255});
                ui.r().fillRoundedRect({bar.x, bar.y, bar.w * k, bar.h}, ui.s(2), ui.theme().accent);
            }
        }
        if (clicked) {
            if (st == UpdateInstaller::State::Ready) {
                if (g_.updates.launch(version)) g_.app.quit();
            } else {
                g_.updates.download(*it, true);
            }
        }
        return true;
    }

    /// Gifts waiting to be opened (COSMIC ONLINE › CADEAUX): an alert in `r`, leading there.
    void drawGiftAlert(const Rect& r) {
        const int n = g_.online.giftsUnopened();
        if (n <= 0) return;
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle bs;
        const bool clicked = ui.button("gifts", r, "", bs);
        std::string wrap = "or";
        for (const auto& x : g_.online.myGifts())
            if (!x.opened) {
                wrap = x.wrap;
                break;
            }
        const Rect icon{r.x + ui.s(10), r.y + ui.s(5), r.h - ui.s(10), r.h - ui.s(10)};
        giftart::drawWrapIcon(g_, wrap, icon, t_, 0.0f);
        const float tx = icon.right() + ui.s(12), room = r.right() - ui.s(14) - tx;
        const std::string head = "Vous avez reçu " + std::to_string(n) + (n > 1 ? " cadeaux" : " cadeau");
        const std::string line = "COSMIC ONLINE › CADEAUX : ouvrez-les quand vous voulez.";
        const float f1 = ui.fitFont(head, FontFace::Bold, std::min(20.0f, r.h * 0.30f / ui.scale()), room, 0.6f);
        ui.label(ui.elide(head, FontFace::Bold, f1, room), {tx, r.y + r.h * 0.32f}, f1, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
        const float f2 = ui.fitFont(line, FontFace::Body, std::min(15.0f, r.h * 0.24f / ui.scale()), room, 0.6f);
        ui.label(ui.elide(line, FontFace::Body, f2, room), {tx, r.y + r.h * 0.68f}, f2, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        if (clicked) g_.app.switchScene(makeGifts(g_), 0.3f);
    }

    /// « Défis »: the countdown of the challenge of the day, or a short state.
    std::string challengeSubtitle() const {
        const ChallengeService& c = g_.challenges;
        if (const DailyChallenge* d = c.current()) {
            int64_t left = std::max<int64_t>(0, (d->endsAtMs - c.nowMs()) / 1000);
            char b[48];
            if (left >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min", static_cast<long long>(left / 3600), static_cast<long long>((left / 60) % 60));
            else std::snprintf(b, sizeof b, "%lld min", static_cast<long long>(std::max<int64_t>(1, left / 60)));
            const ChallengeResult* r = c.result(d->id);
            return std::string(r && r->bestMs > 0 ? "fait · " : "") + "encore " + b;
        }
        switch (c.fetchState()) {
            case ChallengeService::Fetch::Loading: return "recherche du défi…";
            case ChallengeService::Fetch::Offline: return "hors ligne";
            case ChallengeService::Fetch::Failed: return "indisponible";
            default: break;
        }
        return g_.online.usable() ? "défi du jour" : "hors ligne";
    }

    GameServices& g_;
    cstyle::PlanetBackdrop backdrop_;
    cstyle::TitleLogo title_;
    cstyle::HoloCard card_;
    cstyle::Runner runner_;
    cstyle::Anims anims_;
    std::string sel_;          // the chosen entry (its card)
    std::string selId_;        // the entry the preview's animation started for, and when
    float selT_ = 0.0f;
    float t_ = 0.0f, dt_ = 0.0f;
    bool confirmQuit_ = false;
    float idle_ = 0.0f;        // 2.63: seconds without any input (attract mode)
    uint64_t activity_ = 0;
    std::vector<std::pair<std::string, bool>> preload_;  // the trailer's files (true: an animation)
    size_t preloadNext_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeSplashScene(GameServices& g) { return std::make_unique<SplashScene>(g); }
std::unique_ptr<Scene> makeMainMenu(GameServices& g) { return std::make_unique<MainMenuScene>(g); }

}  // namespace cosmic
