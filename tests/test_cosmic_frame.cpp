// 2.83: the screens restyled with the Cosmic style kit (ui/widgets/CosmicFrame): Défis, Coopération, Succès,
// Collection, Statistiques, Paramètres, Notifications and ADMINISTRATION open and draw at 1280 x 720 and
// 1920 x 1080, in French and in English; the cockpit tabs keep the rules of UIContext::tabs (E / Q, equal
// widths: ADMINISTRATION's twelve); « Retour » (a diamond) answers Entrée; the titles never pass their room.

#include <SDL3/SDL.h>

#include <cstdio>
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
#include "ui/widgets/CosmicStyle2.h"
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

TEST_CASE("cosmic kit: a pointed plate, diamond, cockpit button or tab keeps its focused look while the pointer stays") {
    // 2.84: pointed at, a widget drawn before the one that had the focus lost it at the end of the same frame (end()
    // dropped a focus no widget had registered), so the plates and diamonds, lit by the focus, went out ~0.1 s
    // after the pointer stopped. Each piece is pointed at once, then the mouse rests on it for two seconds.
    FrameRig rig(1280, 720, Language::French);
    cstyle::Anims anims;
    UIContext& ui = rig.g->ui;
    int tab = 0;
    rig.app.pushScene(std::make_unique<Probe>(*rig.g, [&](UIContext& u) {
        const float dt = rig.app.frameTime() / 1000.0f;
        cstyle::Plate p{"Campagne", "sous-titre", "campaign"};
        cstyle::plateButton(u, anims, "plate", {100, 100, 400, 64}, p, 1.0f, 1.0f, u.time(), dt, cstyle::Motion{});
        cstyle::PlateEx px;
        px.plate = {"Niveaux en ligne", "", "online"};
        cstyle::plateButtonEx(u, anims, "platex", {100, 180, 400, 56}, px, 0.9f, 1.0f, u.time(), dt, cstyle::Motion{});
        cstyle::Diamond d{"stats"};
        cstyle::diamondButton(u, anims, "dia", {700, 130}, 28.0f, d, 1.0f, u.time(), dt);
        cframe::button(u, "btn", {100, 300, 300, 44}, "Valider");
        cframe::tabs(u, "tabs", {600, 300, 600, 40}, {"Un", "Deux", "Trois"}, tab);
        cframe::diamond(u, "cdia", {100, 400, 220, 44}, "Retour", "quit");
        cframe::button(u, "last", {900, 600, 200, 44}, "Dernier");  // the focus starts on the last one drawn
    }));
    ui.setFocus("last");
    rig.app.runFrames(3);
    auto pointAt = [&](float x, float y) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = x;
        e.motion.y = y;
        e.motion.xrel = 3;
        SDL_PushEvent(&e);
    };
    struct Target {
        const char* name;
        Vec2 at;
        const char* anim;  // its kit animation ("" : a cockpit piece, lit by the focus or the pointer)
    };
    const Target targets[] = {{"plate", {300, 130}, "plate/plate"}, {"platex", {300, 205}, "plate/platex"}, {"dia", {700, 130}, "diamond/dia"},
                              {"btn", {250, 322}, ""}, {"cdia", {122, 422}, ""}, {"plate", {300, 130}, "plate/plate"}};
    for (const Target& t : targets) {
        pointAt(t.at.x, t.at.y);
        rig.app.runFrames(1);
        bool held = true;
        float lowest = 1.0f;
        for (int f = 0; f < 60; ++f) {  // two seconds at 30 frames a second, the mouse still
            rig.app.runFrames(1);
            held = held && ui.focused(t.name);
            if (f >= 10 && *t.anim) lowest = std::min(lowest, anims.value(t.anim));
        }
        if (!held) std::fprintf(stderr, "hover: %s lost its focus\n", t.name);
        CHECK(held);
        if (*t.anim) CHECK(lowest > 0.95f);  // the slide, glow, chevron and icon held, the sweep looping (k > 0.5)
    }
    CHECK(tab == 0);  // pointing never changes the tab
}
