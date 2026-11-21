// 2.83: the main menu redesigned with the Cosmic style kit (ui/widgets/CosmicStyle, ui/screens/MainMenuLayout):
// the pixel icons' sheet is its generator's (tools/assetgen/environment/menu_icons.py --check) and its order the
// code's; the layout fits the screen with nothing over anything else at 1280 x 720 and 1920 x 1080, with and
// without « Continuer » and ADMINISTRATION; every label fits its place in French and in English without being cut;
// the keyboard goes from the plates to the icon row and back, Échap to « Quitter ».

#include <SDL3/SDL.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "tests/TestFramework.h"
#include "ui/screens/MainMenuLayout.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/CosmicStyle.h"
#include "ui/widgets/UIContext.h"

#ifdef _WIN32
#define COSMIC_MENU_NULL " > NUL 2>&1"
#else
#define COSMIC_MENU_NULL " > /dev/null 2>&1"
#endif

using namespace cosmic;

namespace {

void key(SDL_Scancode sc, SDL_Keycode k) {
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

bool inside(const Rect& r, float w, float h) { return r.x >= -0.5f && r.y >= -0.5f && r.right() <= w + 0.5f && r.bottom() <= h + 0.5f; }
bool overlap(const Rect& a, const Rect& b) { return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f; }

/// A headless game at `w` x `h` with its main menu, in `lang`.
struct MenuRig {
    Application app;
    std::unique_ptr<GameServices> g;
    MenuRig(int w, int h, Language lang, bool withContinue) {
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
        g->progression.asterionFilmSeen = true;
        g->progression.lastLevel.clear();
        if (withContinue)
            for (const PlanetData* p : g->content.planets())
                if (auto l = g->content.levelsForPlanet(p->id); !l.empty() && g->progression.lastLevel.empty()) g->progression.lastLevel = l.front()->id;
        g->settings.language = i18n::code(lang);
        g->applySettings();
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~MenuRig() {
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
};

}  // namespace

TEST_CASE("cosmic menu: the icon sheet is its generator's, in the code's order") {
    CHECK(std::system("python3 tools/assetgen/environment/menu_icons.py --check" COSMIC_MENU_NULL) == 0);
    auto j = FileUtil::loadJson("assets/ui/menu_icons.json");
    CHECK(j.has_value());
    if (!j) return;
    CHECK(jget<int>(*j, "cell", 0) == 24);
    int n = 0;
    for (const auto& [id, index] : (*j)["icons"].items()) {
        CHECK(cstyle::menuIconIndex(id) == index.get<int>());
        ++n;
    }
    CHECK(n == 12);
    CHECK(cstyle::menuIconIndex("nothing") == -1);
    CHECK(cstyle::upper("Défis · coopération") == "DÉFIS · COOPÉRATION");
}

TEST_CASE("cosmic menu: the layout fits the screen, nothing over anything else") {
    for (auto [w, h] : {std::pair{1280.0f, 720.0f}, std::pair{1920.0f, 1080.0f}})
        for (bool cont : {false, true})
            for (bool admin : {false, true}) {
                const MainMenuLayout L = MainMenuLayout::compute(w, h, cont, admin);
                CHECK(L.plates.size() == (cont ? 5u : 4u));
                CHECK(L.diamonds.size() == (admin ? 7u : 6u));
                std::vector<Rect> blocks;  // what holds text or a widget, each apart from the others
                for (size_t i = 0; i < L.plates.size(); ++i) {
                    blocks.push_back(L.plateExtent(static_cast<int>(i)));
                    CHECK(L.labelRoom(static_cast<int>(i), cont && i == 0) > 150.0f * L.unit);
                }
                for (size_t i = 0; i + 1 < L.plates.size(); ++i) CHECK(!overlap(L.plates[i], L.plates[i + 1]));
                const float d = L.diamondHalf;
                Rect row{L.diamonds.front().x - d, L.diamonds.front().y - d * 1.3f, L.diamonds.back().x - L.diamonds.front().x + d * 2.4f, d * 2.3f};
                for (size_t i = 0; i + 1 < L.diamonds.size(); ++i) CHECK(L.diamonds[i + 1].x - L.diamonds[i].x >= 2.0f * d);  // the badges stay in the gaps
                for (const Rect& r : {row, L.iconLabel, L.hints, L.title, L.names, L.card, L.alerts, L.chips}) blocks.push_back(r);
                for (const Rect& b : blocks) CHECK(inside(b, w, h));
                for (size_t i = 0; i < blocks.size(); ++i)
                    for (size_t k = i + 1; k < blocks.size(); ++k) {
                        // Two plates: only one is focused at a time (their own rects are checked apart above).
                        if (k < L.plates.size() || !overlap(blocks[i], blocks[k])) continue;
                        std::printf("  overlap at %.0fx%.0f (continue %d, admin %d): blocks %zu and %zu\n", w, h, cont, admin, i, k);
                        CHECK(false);
                    }
                CHECK(L.card.containsRect(L.preview));
                CHECK(L.card.containsRect(L.cardText));
                CHECK(!overlap(L.preview, L.cardText));
                CHECK(L.alerts.h >= 2 * L.alertH + L.alertGap - 0.5f);  // the update's and the gifts' alerts
                // The runner on its runway, right of the left column, under the card and the alerts.
                CHECK(L.runwayX0 > row.right());
                CHECK(L.groundY - 96.0f * L.unit >= L.card.bottom() - 0.5f);
                CHECK(L.novaX + 48.0f * L.unit < L.alerts.x);
            }
}

TEST_CASE("cosmic menu: every label fits its place in French and in English") {
    const char* plates[] = {"Continuer", "Campagne", "Défis", "Coopération", "En ligne"};
    const char* icons[] = {"Notifications", "Succès", "Collection", "Statistiques", "Paramètres", "Administration", "Quitter", "Confirmer : quitter"};
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            MenuRig rig(w, h, lang, true);
            UIContext& ui = rig.g->ui;
            const MainMenuLayout L = MainMenuLayout::compute(ui.screenW(), ui.screenH(), true, true);
            for (int i = 0; i < 5; ++i) {
                const std::string s = cstyle::upper(i18n::tr(plates[i]));
                const float room = L.labelRoom(i, i == 0);
                CHECK(ui.text().measure(s, FontFace::Bold, 21.0f * L.unit).x <= room);
            }
            for (const char* s : icons) {
                const std::string t = cstyle::upper(i18n::tr(s));
                CHECK(ui.text().measure(t, FontFace::Bold, 15.0f * L.unit).x + ui.newPillWidth(true) + 10.0f * L.unit <= L.iconLabel.w);
            }
            rig.app.runFrames(20);  // the arrival, the title and the card drawn without trouble
            CHECK(rig.app.sceneCount() == 1);
        }
}

TEST_CASE("cosmic menu: the keyboard goes from the plates to the icon row and back") {
    MenuRig rig(1280, 720, Language::French, true);
    UIContext& ui = rig.g->ui;
    CHECK(ui.focused("main/continue"));
    const char* order[] = {"main/campaign", "main/challenges", "main/coop", "main/online", "main/news"};
    for (const char* id : order) {
        key(SDL_SCANCODE_DOWN, SDLK_DOWN);
        rig.app.runFrames(2);
        CHECK(ui.focused(id));
    }
    key(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    rig.app.runFrames(2);
    CHECK(ui.focused("main/achievements"));
    key(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    rig.app.runFrames(2);
    CHECK(ui.focused("main/collection"));
    key(SDL_SCANCODE_LEFT, SDLK_LEFT);
    rig.app.runFrames(2);
    CHECK(ui.focused("main/achievements"));
    key(SDL_SCANCODE_UP, SDLK_UP);
    rig.app.runFrames(2);
    CHECK(ui.focused("main/online"));
    key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    rig.app.runFrames(2);
    CHECK(ui.focused("main/quit"));
    // « Quitter » asks to be confirmed: one Enter arms it, the game stays open.
    key(SDL_SCANCODE_RETURN, SDLK_RETURN);
    rig.app.runFrames(2);
    CHECK(rig.app.sceneCount() == 1);
    CHECK(ui.focused("main/quit"));
}
