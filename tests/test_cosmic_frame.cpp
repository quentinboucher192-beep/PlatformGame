// 2.83: the screens restyled with the Cosmic style kit (ui/widgets/CosmicFrame): Défis, Coopération, Succès,
// Collection, Statistiques, Paramètres, Notifications and ADMINISTRATION open and draw at 1280 x 720 and
// 1920 x 1080, in French and in English; the cockpit tabs keep the rules of UIContext::tabs (E / Q, equal
// widths: ADMINISTRATION's twelve); « Retour » (a diamond) answers Entrée; the titles never pass their room.

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "tests/TestFramework.h"
#include "ui/screens/CoopGameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/CosmicFrame.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

void press(SDL_Scancode sc, SDL_Keycode k) {
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = sc;
    down.key.key = k;
    down.key.down = true;
    SDL_PushEvent(&down);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    SDL_PushEvent(&up);
}

struct FrameRig {
    Application app;
    std::unique_ptr<GameServices> g;
    FrameRig(int w, int h, Language lang) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = w;
        cfg.height = h;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->settings.language = i18n::code(lang);
        g->applySettings();
    }
    ~FrameRig() {
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
};

/// A scene drawing what the test asks for, each frame.
class Probe : public Scene {
public:
    Probe(GameServices& g, std::function<void(UIContext&)> draw) : g_(g), draw_(std::move(draw)) {}
    void render(float) override {
        g_.ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        draw_(g_.ui);
        g_.ui.end();
    }

private:
    GameServices& g_;
    std::function<void(UIContext&)> draw_;
};

}  // namespace

TEST_CASE("cosmic frame: the restyled screens open and draw at 1280x720 and 1920x1080, in French and English") {
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            FrameRig rig(w, h, lang);
            GameServices& g = *rig.g;
            const std::vector<std::function<std::unique_ptr<Scene>()>> screens{
                [&] { return makeChallenges(g); },       [&] { return makeChallenges(g, 3); }, [&] { return makeCoopLobby(g); },
                [&] { return makeAchievements(g); },     [&] { return makeCollection(g); },    [&] { return makeCollection(g, 3); },
                [&] { return makeStatistics(g); },       [&] { return makeSettingsScene(g, false); },
                [&] { return makeNotifications(g); },    [&] { return makeAdmin(g); }};
            for (const auto& make : screens) {
                rig.app.pushScene(make());
                rig.app.runFrames(4);
                CHECK(rig.app.sceneCount() == 1);
                rig.app.popScene();
                rig.app.runFrames(1);
            }
        }
}

TEST_CASE("cosmic frame: the cockpit tabs keep UIContext's rules, « Retour » answers Entrée") {
    FrameRig rig(1280, 720, Language::French);
    const std::vector<std::string> labels{"Joueurs", "Calendrier des défis", "Journal", "Sauvegardes", "Campagne", "Créations",
                                          "Ennemis", "Clans", "Assets", "Raids", "Films", "Cadeaux"};
    int active = 0, backs = 0;
    rig.app.pushScene(std::make_unique<Probe>(*rig.g, [&](UIContext& ui) {
        cframe::tabs(ui, "tabs", {20, 20, 1240, 40}, labels, active);
        if (cframe::back(ui, "back", {20, 80, 170, 44})) ++backs;
    }));
    rig.app.runFrames(2);
    press(SDL_SCANCODE_E, SDLK_E);
    rig.app.runFrames(2);
    CHECK(active == 1);
    press(SDL_SCANCODE_Q, SDLK_Q);
    rig.app.runFrames(2);
    press(SDL_SCANCODE_Q, SDLK_Q);
    rig.app.runFrames(2);
    CHECK(active == 11);  // round to « Cadeaux »
    rig.g->ui.setFocus("back");
    rig.app.runFrames(1);
    press(SDL_SCANCODE_RETURN, SDLK_RETURN);
    rig.app.runFrames(2);
    CHECK(backs == 1);
}

TEST_CASE("cosmic frame: the titles never pass their room, in French and in English") {
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            FrameRig rig(w, h, lang);
            UIContext& ui = rig.g->ui;
            rig.app.pushScene(std::make_unique<Probe>(*rig.g, [](UIContext&) {}));
            rig.app.runFrames(1);
            for (const char* t : {"ADMINISTRATION", "PARAMÈTRES", "STATISTIQUES", "NOTIFICATIONS", "SUCCÈS", "COLLECTION", "COOPÉRATION", "DÉFIS"})
                for (float room : {ui.s(180), ui.s(320), ui.s(900)}) CHECK(cframe::measureTitle(ui, t, 40, room).x <= room + 0.5f);
        }
}
