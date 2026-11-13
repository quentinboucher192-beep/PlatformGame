// Splash screen and main menu.

#include <cmath>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
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
        g_.playMusic("music/menu_theme.ogg");
        g_.ui.setFocus("main/play");
    }
    void onResume() override { g_.playMusic("music/menu_theme.ogg"); }
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
        ui.label("Les Ruines Suspendues de Sylvanox", {ui.screenW() * 0.66f, ui.screenH() * 0.36f + ui.s(120)}, 26,
                 ui.theme().text.withAlphaF(appear * 0.85f), Align::Center, FontFace::Body, VAlign::Middle);

        float x = ui.s(120), w = ui.s(470), h = ui.s(66), gap = ui.s(14);
        float y = ui.screenH() * 0.5f - (7 * (h + gap)) * 0.5f + ui.s(40);
        Rect side{x - ui.s(40), y - ui.s(40), w + ui.s(80), 7 * (h + gap) + ui.s(70)};
        ui.r().fillRoundedRect(side, ui.s(18), Color{6, 10, 22, 150});
        ui.pushId("main");
        auto btn = [&](const char* id, const std::string& label, int icon, bool primary = false) {
            UIContext::ButtonStyle st;
            st.primary = primary;
            st.align = Align::Left;
            float slide = (1.0f - applyEase(Ease::OutCubic, std::clamp((t_ - 0.1f) * 3.0f, 0.0f, 1.0f))) * ui.s(-60);
            bool c = ui.button(id, {x + slide, y, w, h}, label, st);
            (void)icon;
            y += h + gap;
            return c;
        };
        std::string playLabel = g_.progression.lastLevel.empty() ? "Jouer" : "Continuer";
        if (btn("play", playLabel, 0, true)) {
            std::string planet = g_.progression.lastPlanet.empty() ? "sylvanox" : g_.progression.lastPlanet;
            if (!g_.content.planet(planet)) planet = "sylvanox";
            g_.app.switchScene(makeLevelSelect(g_, planet));
        }
        if (btn("planets", "Sélection des planètes", 1)) g_.app.switchScene(makePlanetMap(g_));
        if (btn("leveleditor", "Éditeur de niveaux", 2)) g_.app.switchScene(makeLevelEditor(g_, ""));
        if (btn("enemyeditor", "Éditeur d'ennemis", 3)) g_.app.switchScene(makeEnemyEditor(g_));
        if (btn("settings", "Paramètres", 4)) g_.app.pushScene(makeSettingsScene(g_, false));
        if (btn("content", "Gestion des contenus", 5)) g_.app.switchScene(makeContentManager(g_));
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
    GameServices& g_;
    MenuBackground bg_;
    float t_ = 0.0f;
    bool confirmQuit_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeSplashScene(GameServices& g) { return std::make_unique<SplashScene>(g); }
std::unique_ptr<Scene> makeMainMenu(GameServices& g) { return std::make_unique<MainMenuScene>(g); }

}  // namespace cosmic
