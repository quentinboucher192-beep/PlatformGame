// Splash screen and main menu.

#include <cmath>
#include <cstdio>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

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
            g_.app.switchScene(makeMainMenu(g_), 0.6f);
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
        g_.ui.setFocus(!g_.progression.lastLevel.empty() && !g_.content.campaignPlanetOf(g_.progression.lastLevel).empty()
                           ? "main/continue"
                           : "main/campaign");
    }
    void onResume() override { g_.playMusic(g_.menuMusic()); }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.15f);
        float appear = std::clamp(t_ / 0.7f, 0.0f, 1.0f);
        drawGameTitle(g_, {ui.screenW() * 0.66f, ui.screenH() * 0.36f}, 110, appear);
        ui.label("Sylvanox · Pyralis · Glacior · Aethra", {ui.screenW() * 0.66f, ui.screenH() * 0.36f + ui.s(120)}, 26,
                 ui.theme().text.withAlphaF(appear * 0.85f), Align::Center, FontFace::Body, VAlign::Middle);

        // Only a campaign level: WORKSHOP levels are reached from the WORKSHOP.
        bool canContinue = !g_.progression.lastLevel.empty() && g_.content.level(g_.progression.lastLevel) &&
                           !g_.content.campaignPlanetOf(g_.progression.lastLevel).empty();
        int rows = canContinue ? 10 : 9;
        // Ten rows (2.31: « Défis »): a little tighter so that the menu keeps clear of the footer.
        float x = ui.s(120), w = ui.s(470), h = ui.s(rows > 9 ? 62 : 66), gap = ui.s(rows > 9 ? 12 : 14);
        float y = ui.screenH() * 0.5f - (rows * (h + gap)) * 0.5f + ui.s(40);
        Rect side{x - ui.s(40), y - ui.s(40), w + ui.s(80), rows * (h + gap) + ui.s(70)};
        ui.r().fillRoundedRect(side, ui.s(18), Color{6, 10, 22, 150});
        ui.pushId("main");
        // `badge`: unread messages, in a red pill at the right edge (the subtitle keeps the room left of it).
        auto btn = [&](const char* id, const std::string& label, const std::string& sub, bool primary = false, int badge = 0) {
            UIContext::ButtonStyle st;
            st.primary = primary;
            st.align = Align::Left;
            float slide = (1.0f - applyEase(Ease::OutCubic, std::clamp((t_ - 0.1f) * 3.0f, 0.0f, 1.0f))) * ui.s(-60);
            bool c = ui.button(id, {x + slide, y, w, h}, label, st);
            ui.newBadge(id, {x + slide, y, w, h}, std::string("menu.") + id);
            float right = x + slide + w - ui.s(20);
            if (badge > 0) right = OnlineUi::countPill(ui, right, y + h * 0.5f, badge, ui.theme().danger, Color::white(), 28, 16) - ui.s(12);
            if (!sub.empty()) {
                // Never under the button's own label nor under the counter.
                float labelW = ui.text().measure(label, FontFace::Bold, ui.s(26)).x;
                float room = right - (x + slide + ui.s(20) + labelW + ui.s(24));
                if (room > ui.s(40))
                    ui.label(ui.elide(sub, FontFace::Body, 16, room), {right, y + h * 0.5f}, 16, ui.theme().textDim, Align::Right, FontFace::Body,
                             VAlign::Middle);
            }
            y += h + gap;
            return c;
        };
        if (canContinue) {
            const LevelEntry* last = g_.content.level(g_.progression.lastLevel);
            if (btn("continue", "Continuer", last->name, true)) g_.app.switchScene(makeGameScene(g_, last->id), 0.5f);
        }
        if (btn("campaign", "Campagne", "planètes et niveaux", !canContinue)) g_.app.switchScene(makePlanetMap(g_));
        if (btn("challenges", "Défis", challengeSubtitle())) g_.app.switchScene(makeChallenges(g_), 0.3f);
        std::string onlineSub = g_.online.loggedIn() ? g_.online.username() : "compte requis";
        if (int n = g_.online.loggedIn() ? g_.online.pendingFriendRequests() : 0; n > 0)
            onlineSub += " · " + std::to_string(n) + (n > 1 ? " demandes d'amis" : " demande d'ami");
        // Unread messages: the red counter of the button (as on SOCIAL and on each conversation).
        if (btn("online", "En ligne", onlineSub, false, g_.online.loggedIn() ? g_.online.unreadMessages() : 0))
            g_.app.switchScene(makeOnlineRanking(g_));
        {
            int n = g_.news.unread();
            int online = g_.online.loggedIn() ? g_.online.unreadNotifications() : 0;
            std::string sub = n > 0 ? std::to_string(n) + (n > 1 ? " nouveautés" : " nouveauté") : "";
            if (online > 0) sub += (sub.empty() ? "" : " · ") + std::to_string(online) + " en ligne";
            // Straight to the Cosmic Online centre when only it has something new.
            if (btn("news", "Notifications", sub)) g_.app.switchScene(makeNotifications(g_, n == 0 && online > 0 ? 1 : 0), 0.3f);
        }
        {
            std::string sub = std::to_string(g_.achievements.stars(g_.achievementCatalog)) + " / " +
                              std::to_string(g_.achievementCatalog.totalStars()) + " étoiles";
            if (btn("achievements", "Succès", sub)) g_.app.switchScene(makeAchievements(g_), 0.3f);
        }
        {
            auto p = g_.cosmeticProgress();
            int got = 0, all = 0;
            for (const auto& it : g_.cosmetics.items()) {
                ++all;
                got += cosmeticUnlocked(it, p) ? 1 : 0;
            }
            std::string sub = std::to_string(got) + " / " + std::to_string(all) + " récompenses";
            if (btn("collection", "Collection", sub)) g_.app.switchScene(makeCollection(g_), 0.3f);
        }
        {
            int kills = 0;
            for (const auto& [id, r] : g_.bossStats.all()) kills += r.kills;
            std::string sub = kills > 0 ? std::to_string(kills) + (kills > 1 ? " victoires sur les boss" : " victoire sur un boss") : "boss";
            if (btn("stats", "Statistiques", sub)) g_.app.switchScene(makeBossStats(g_), 0.3f);
        }
        if (btn("settings", "Paramètres", "")) g_.app.pushScene(makeSettingsScene(g_, false));
        UIContext::ButtonStyle danger;
        danger.danger = true;
        danger.align = Align::Left;
        if (ui.button("quit", {x, y, w, h}, confirmQuit_ ? "Confirmer : quitter" : "Quitter", danger)) {
            if (confirmQuit_) g_.app.quit();
            confirmQuit_ = true;
        }
        if (!ui.focused("quit")) confirmQuit_ = false;
        ui.popId();

        // Footer
        float fy = ui.screenH() - ui.s(44);
        std::string device = g_.app.input().hasGamepad() ? "Manette : " + g_.app.input().gamepadName() : "Clavier / souris";
        ui.label(device, {ui.s(40), fy}, 18, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(std::string("v") + COSMIC_VERSION_STRING, {ui.screenW() - ui.s(40), fy}, 18, ui.theme().textDim, Align::Right,
                 FontFace::Body, VAlign::Middle);
        Glyphs::drawHint(ui, Action::MenuConfirm, "Valider", {ui.screenW() * 0.5f - ui.s(150), fy}, ui.s(32));
        ui.end();
    }

private:
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
    MenuBackground bg_;
    float t_ = 0.0f;
    bool confirmQuit_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeSplashScene(GameServices& g) { return std::make_unique<SplashScene>(g); }
std::unique_ptr<Scene> makeMainMenu(GameServices& g) { return std::make_unique<MainMenuScene>(g); }

}  // namespace cosmic
