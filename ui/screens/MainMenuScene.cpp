// Splash screen and main menu.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Easing.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/tutorial/Tutorial.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/CoopGameScene.h"
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
        g_.ui.setFocus(!g_.progression.lastLevel.empty() && !g_.content.campaignPlanetOf(g_.progression.lastLevel).empty()
                           ? "main/continue"
                           : "main/campaign");
        activity_ = g_.app.input().activityCount();
    }
    void onResume() override {
        g_.playMusic(g_.menuMusic());
        idle_ = 0.0f;  // 2.63: back from a screen (or from the trailer): a new minute to wait
        activity_ = g_.app.input().activityCount();
    }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
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
        bg_.render(g_, 0.15f);
        float appear = std::clamp(t_ / 0.7f, 0.0f, 1.0f);
        drawGameTitle(g_, {ui.screenW() * 0.66f, ui.screenH() * 0.36f}, 110, appear);
        ui.label("Sylvanox · Pyralis · Glacior · Aethra", {ui.screenW() * 0.66f, ui.screenH() * 0.36f + ui.s(120)}, 26,
                 ui.theme().text.withAlphaF(appear * 0.85f), Align::Center, FontFace::Body, VAlign::Middle);

        // Only a campaign level: WORKSHOP levels are reached from the WORKSHOP.
        bool canContinue = !g_.progression.lastLevel.empty() && g_.content.level(g_.progression.lastLevel) &&
                           !g_.content.campaignPlanetOf(g_.progression.lastLevel).empty();
        // ADMINISTRATION (2.40): only for an account with the admin role, never for the others.
        const bool admin = g_.online.isAdmin();
        int rows = (canContinue ? 11 : 10) + (admin ? 1 : 0);  // 2.62: « Coopération »
        // Ten rows (2.32: « Défis »): a little tighter so that the menu keeps clear of the footer;
        // eleven (an admin's menu): tighter still.
        float x = ui.s(120), w = ui.s(470);
        float h = ui.s(rows > 11 ? 52 : rows > 10 ? 56 : rows > 9 ? 62 : 66), gap = ui.s(rows > 11 ? 8 : rows > 10 ? 10 : rows > 9 ? 12 : 14);
        float y = ui.screenH() * 0.5f - (rows * (h + gap)) * 0.5f + ui.s(rows > 10 ? 12 : 40);
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
        if (btn("coop", "Coopération", "2 ou 3 joueurs · niveaux EXTRÊME")) g_.app.switchScene(makeCoopLobby(g_), 0.3f);
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
                if (!cosmeticShown(it, p)) continue;  // 2.64: a souvenir counts once earned
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
            if (btn("stats", "Statistiques", sub)) g_.app.switchScene(makeStatistics(g_), 0.3f);  // 2.54: the player's statistics, the bosses' within
        }
        if (btn("settings", "Paramètres", "")) g_.app.pushScene(makeSettingsScene(g_, false));
        if (admin && btn("admin", "Administration", "joueurs · défis · journal")) g_.app.switchScene(makeAdmin(g_), 0.3f);
        UIContext::ButtonStyle danger;
        danger.danger = true;
        danger.align = Align::Left;
        if (ui.button("quit", {x, y, w, h}, confirmQuit_ ? "Confirmer : quitter" : "Quitter", danger)) {
            if (confirmQuit_) g_.app.quit();
            confirmQuit_ = true;
        }
        if (!ui.focused("quit")) confirmQuit_ = false;
        drawUpdateAlert(side);
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

    /// A newer version exists (announced by Cosmic Online): an alert at the bottom of the menu.
    /// A click downloads the installer, checks it (SHA-256), then runs it and closes the game.
    void drawUpdateAlert(const Rect& side) {
        const NewsCenter::Item* it = g_.updates.offer();
        if (!it) return;
        UIContext& ui = g_.ui;
        const std::string version = it->target;
        const float fy = ui.screenH() - ui.s(44);
        float left = side.right() + ui.s(40);
        float w = std::min(ui.s(760), ui.screenW() - ui.s(40) - left);
        if (w < ui.s(320)) return;
        Rect r{left + (ui.screenW() - ui.s(40) - left - w) * 0.5f, fy - ui.s(30) - ui.s(76), w, ui.s(76)};
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
        Vec2 mark{r.x + ui.s(36), r.center().y};
        ui.r().fillCircle(mark, ui.s(15), ui.theme().warm.withAlphaF(pulse));
        ui.label("!", mark, 22, Color{20, 14, 4, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        float tx = r.x + ui.s(66), room = r.right() - ui.s(18) - tx;
        const std::string head = "Nouvelle version " + version + " disponible";
        float f1 = ui.fitFont(head, FontFace::Bold, 22, room, 0.6f);
        ui.label(head, {tx, r.y + r.h * 0.32f}, f1, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        float f2 = ui.fitFont(line, FontFace::Body, 16, room, 0.6f);
        Color c2 = !g_.updates.error(version).empty() ? ui.theme().danger : ui.theme().textDim;
        ui.label(line, {tx, r.y + r.h * 0.70f}, f2, c2, Align::Left, FontFace::Body, VAlign::Middle);
        if (st == UpdateInstaller::State::Downloading) {
            auto [got, total] = g_.updates.progress(version);
            if (total > 0) {  // a thin bar along the bottom of the alert, inside its frame
                const float k = std::clamp(static_cast<float>(static_cast<double>(got) / static_cast<double>(total)), 0.0f, 1.0f);
                Rect bar{tx, r.bottom() - ui.s(9), room, ui.s(4)};
                ui.r().fillRoundedRect(bar, ui.s(2), Color{40, 50, 70, 255});
                ui.r().fillRoundedRect({bar.x, bar.y, bar.w * k, bar.h}, ui.s(2), ui.theme().accent);
            }
        }
        if (!clicked) return;
        if (st == UpdateInstaller::State::Ready) {
            if (g_.updates.launch(version)) g_.app.quit();
        } else {
            g_.updates.download(*it, true);
        }
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
    MenuBackground bg_;
    float t_ = 0.0f;
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
