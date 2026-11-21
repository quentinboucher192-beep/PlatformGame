// 2.84: the campaign's map and levels, the pause menu, the results and the frame of ONLINE restyled with the
// Cosmic kit (ui/widgets/CosmicStyle2, ui/screens/CosmicScreensLayout): each layout fits the screen with nothing
// over anything else at 1280 x 720 and 1920 x 1080; every label fits its place in French and in English (on one
// line, or on two for the plates that wrap) without being cut; the keyboard still goes everywhere: the planets,
// « Explorer », the levels, Échap back; the pause's plates and Échap; the results' plates.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "tests/TestFramework.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/CosmicScreensLayout.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/PauseLayout.h"
#include "ui/screens/PauseScene.h"
#include "ui/screens/ResultsScene.h"
#include "ui/widgets/CosmicStyle2.h"
#include "ui/widgets/UIContext.h"

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
bool within(const Rect& r, const Rect& box) { return r.x >= box.x - 0.5f && r.y >= box.y - 0.5f && r.right() <= box.right() + 0.5f && r.bottom() <= box.bottom() + 0.5f; }
bool overlap(const Rect& a, const Rect& b) { return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f; }
Rect diamondBox(Vec2 c, float half) { return Rect::fromCenter(c, {half * 2.0f, half * 2.0f}); }
/// A focused plate slides right and glows (cstyle::plateButtonEx).
Rect extent(const Rect& r, float u) { return {r.x - 8 * u, r.y - 8 * u, r.w + 32 * u, r.h + 16 * u}; }

/// None of `blocks` over another, each on the screen.
void checkBlocks(const std::vector<Rect>& blocks, float w, float h, const char* what) {
    for (size_t i = 0; i < blocks.size(); ++i) {
        CHECK(inside(blocks[i], w, h));
        for (size_t k = i + 1; k < blocks.size(); ++k)
            if (overlap(blocks[i], blocks[k])) {
                std::printf("  %s at %.0fx%.0f: blocks %zu and %zu overlap\n", what, w, h, i, k);
                CHECK(false);
            }
    }
}

/// A headless game at `w` x `h` in `lang`.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig(int w, int h, Language lang) {
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
        g->progression.lastPlanet = "sylvanox";
        g->progression.lastLevel.clear();
        g->settings.language = i18n::code(lang);
        g->applySettings();
    }
    ~Rig() {
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
    void press(SDL_Scancode sc, SDL_Keycode k, int frames = 3) {
        key(sc, k);
        app.runFrames(frames);
    }
};

/// The label of a plate fits its room at `pu` (screen pixels per reference pixel): on one line at 60 % of its
/// size, or (`wrap`) on two lines at 60 % of the smaller size.
bool plateFits(UIContext& ui, const std::string& label, float plateW, float pu, bool chevron, bool wrap, float rightW = 0.0f) {
    const float room = plateW - (cstyle::plateLabelLeft() + cstyle::plateLabelRightPad(chevron)) * pu - (rightW > 0.0f ? rightW + 8.0f * pu : 0.0f);
    const std::string s = cstyle::upper(i18n::tr(label));
    if (ui.text().measure(s, FontFace::Bold, 21.0f * pu * 0.6f).x <= room) return true;
    if (!wrap) return false;
    const size_t mid = s.size() / 2;
    size_t best = std::string::npos;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == ' ' && (best == std::string::npos || (i > mid ? i - mid : mid - i) < (best > mid ? best - mid : mid - best))) best = i;
    if (best == std::string::npos) return false;
    return ui.text().measure(s.substr(0, best), FontFace::Bold, 18.0f * pu * 0.6f).x <= room &&
           ui.text().measure(s.substr(best + 1), FontFace::Bold, 18.0f * pu * 0.6f).x <= room;
}

}  // namespace

TEST_CASE("cosmic screens: the layouts fit the screen, nothing over anything else") {
    for (auto [w, h] : {std::pair{1280.0f, 720.0f}, std::pair{1920.0f, 1080.0f}, std::pair{1920.0f, 1200.0f}}) {
        {
            const PlanetMapLayout L = PlanetMapLayout::compute(w, h);
            checkBlocks({L.title, L.subtitle, L.map, L.card, L.exploreExtent(), diamondBox(L.back, L.diamondHalf), L.backCaption, L.hints}, w, h,
                        "planet map");
            for (const Rect& r : {L.banner, L.cardText, L.progress}) CHECK(within(r, L.card));
            checkBlocks({L.banner, L.cardText, L.progress}, w, h, "planet card");
            // The whole system (the widest orbit, a planet and its name) fits the map.
            CHECK(640.0f * L.mapScale <= L.map.w * 0.5f + 0.5f);
            CHECK(300.0f * L.mapScale <= L.map.h * 0.5f + 0.5f);
        }
        {
            const LevelSelectLayout L = LevelSelectLayout::compute(w, h);
            std::vector<Rect> blocks{L.title, L.subtitle, L.list, L.card, L.playExtent(), L.caption, diamondBox(L.back, L.diamondHalf), L.backCaption, L.hints};
            for (Vec2 d : L.diamonds) blocks.push_back(diamondBox(d, L.diamondHalf));
            checkBlocks(blocks, w, h, "level select");
            CHECK(L.list.h >= 4 * (L.cardH + L.cardGap));  // four cards at least without scrolling
        }
        {
            const ResultsLayout L = ResultsLayout::compute(w, h);
            const float u = L.unit;
            std::vector<Rect> blocks{L.title, L.levelName, L.subLine, L.caption, extent(L.online, u), L.rating, L.stats, L.medalCard, L.info, L.status};
            for (Vec2 d : L.diamonds) blocks.push_back(diamondBox(d, L.diamondHalf));
            // The action plates (up to four), each with its slide.
            for (int count = 2; count <= 4; ++count) {
                std::vector<Rect> b = blocks;
                const float bw = std::min(300.0f * u, (L.buttons.w - (count - 1) * L.buttonGap) / count);
                float x = L.buttons.center().x - (count * bw + (count - 1) * L.buttonGap) * 0.5f;
                for (int i = 0; i < count; ++i, x += bw + L.buttonGap) b.push_back({x, L.buttons.y, bw, L.buttons.h});
                checkBlocks(b, w, h, "results");
                CHECK(inside(extent({x - bw - L.buttonGap, L.buttons.y, bw, L.buttons.h}, u), w, h));
            }
            for (const Rect& r : {L.statsTime, L.statsRows}) CHECK(within(r, L.stats));
            CHECK(L.statsRows.h >= 7 * L.rowH - 0.5f);
            for (const Rect& r : {L.record, L.nova, L.medal, L.medalName, L.stars, L.goalsTitle, L.goals}) CHECK(within(r, L.medalCard));
            checkBlocks({L.record, L.nova, L.medal, L.stars, L.goalsTitle, L.goals}, w, h, "medal card");
            checkBlocks({L.nova, L.medalName}, w, h, "medal name");
            CHECK(L.goals.h >= 4 * L.goalH - 0.5f);
        }
    }
}

TEST_CASE("cosmic screens: every label fits its place in French and in English") {
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            Rig rig(w, h, lang);
            UIContext& ui = rig.g->ui;
            rig.app.pushScene(makePlanetMap(*rig.g));  // the screen drawn once: its sizes and fonts known
            rig.app.runFrames(3);
            const PlanetMapLayout M =PlanetMapLayout::compute(ui.screenW(), ui.screenH());
            CHECK(plateFits(ui, "Explorer", M.explore.w, M.unit, true, false));
            const LevelSelectLayout S = LevelSelectLayout::compute(ui.screenW(), ui.screenH());
            for (const char* s : {"Jouer", "Verrouillé"}) CHECK(plateFits(ui, s, S.play.w, S.unit, true, false));
            // The diamonds' names (15 / 14 reference pixels, at 70 % at least, a « NOUVEAU » after them).
            for (const char* s : {"Menu principal", "Carte des planètes", "Statistiques des boss", "Administration"})
                CHECK(ui.text().measure(cstyle::upper(i18n::tr(s)), FontFace::Bold, 15.0f * S.unit * 0.7f).x <= S.backCaption.w);
            for (const char* s : {"Fantômes…", "Touches…"})
                CHECK(ui.text().measure(cstyle::upper(i18n::tr(s)), FontFace::Bold, 15.0f * S.unit * 0.7f).x + ui.newPillWidth(true) + 10.0f * S.unit <=
                      S.caption.w);
            const ResultsLayout R = ResultsLayout::compute(ui.screenW(), ui.screenH());
            for (const char* s : {"Revoir la partie", "Segments"})
                CHECK(ui.text().measure(cstyle::upper(i18n::tr(s)), FontFace::Bold, 14.0f * R.unit * 0.7f).x <= R.caption.w);
            {
                const float bw = std::min(300.0f * R.unit, (R.buttons.w - 3 * R.buttonGap) / 4);
                for (const char* s : {"Recommencer", "Niveau suivant", "Réessayer en entraînement", "Course à la vérification", "Retour à l'éditeur",
                                      "Retour au menu En ligne", "Retour à l'Atelier", "Sélection des niveaux"})
                    CHECK(plateFits(ui, s, bw, R.buttons.h / 64.0f, true, true));
                CHECK(plateFits(ui, "Classement en ligne", R.online.w, R.online.h / 64.0f, true, false));
            }
            // The titles in chrome: shrunk at most to 60 % of their size.
            for (auto [s, size, room] : {std::tuple{"SYSTÈME KEPLER-VOSS", 44.0f * M.unit, M.title.w}, std::tuple{"NIVEAU TERMINÉ", 50.0f * R.unit, R.title.w},
                                         std::tuple{"NOUVEAU RECORD !", 32.0f * R.unit, R.record.w}, std::tuple{"PREMIÈRE VICTOIRE !", 32.0f * R.unit, R.record.w}})
                CHECK(ui.text().measure(i18n::tr(s), FontFace::Title, size * 0.6f).x <= room);
            // The pause's plates, at the comfortable and the tight sizes of its column.
            for (float screenH : {ui.screenH(), ui.screenH() * 2.0f}) {
                const PauseLayout P = pauseLayout(9, 5, true, ui.scale(), screenH);
                const float pu = ui.s(P.buttonH) / 64.0f, bw = ui.s(560 - 120);
                for (const char* s : {"Reprendre", "Recommencer", "Paramètres", "Guide des pouvoirs", "Touches de ce niveau", "Invincible (admin) : activé · non classé",
                                      "Invincible (admin) : désactivé", "Fantômes (masqués)", "Retour à l'éditeur", "Retour à l'entraînement",
                                      "Retour à l'administration", "Confirmer : passer le didacticiel", "Passer le didacticiel",
                                      "Confirmer : quitter le niveau", "Retour au menu"})
                    CHECK(plateFits(ui, s, bw, pu, true, true));
                CHECK(plateFits(ui, "Mode concentré", bw, pu, true, true, ui.s(48)));
            }
            // ONLINE's column.
            {
                const float pu = ui.s(50) / 64.0f, pw = ui.s(272 - 18);
                for (const char* s : {"NIVEAUX EN LIGNE", "CLASSEMENTS", "SOCIAL", "CLANS", "CADEAUX", "PROJETS", "ATELIER", "RETOUR"})
                    CHECK(plateFits(ui, s, pw, pu, true, false, 0.0f));
                // SOCIAL with its two pills (unread messages, friend requests): the chevron gives them its place.
                const float pills = OnlineUi::countPillWidth(ui, 12, 26, 15) + OnlineUi::countPillWidth(ui, 3, 26, 15) + ui.s(12);
                CHECK(plateFits(ui, "SOCIAL", pw, pu, false, false, pills));
            }
        }
}

TEST_CASE("cosmic screens: the keyboard goes through the map, the levels and back") {
    Rig rig(1280, 720, Language::French);
    UIContext& ui = rig.g->ui;
    rig.app.pushScene(makePlanetMap(*rig.g));
    rig.app.runFrames(10);
    CHECK(ui.focused("map/planet_open"));
    // The arrows choose the planet; the focus stays on « Explorer ».
    rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
    CHECK(ui.focused("map/planet_open"));
    rig.press(SDL_SCANCODE_LEFT, SDLK_LEFT);
    CHECK(ui.focused("map/planet_open"));
    CHECK(rig.g->progression.lastPlanet == "sylvanox");
    // Enter: its levels, the last one (none: the first) chosen.
    rig.press(SDL_SCANCODE_RETURN, SDLK_RETURN, 40);
    CHECK(ui.focused("levels/l0"));
    rig.press(SDL_SCANCODE_DOWN, SDLK_DOWN);
    CHECK(ui.focused("levels/l1"));
    rig.press(SDL_SCANCODE_UP, SDLK_UP);
    CHECK(ui.focused("levels/l0"));
    rig.app.runFrames(30);  // the cards' EXTRÊME and bosses read off the main thread, the details drawn
    // Échap: back to the map.
    rig.press(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, 40);
    CHECK(ui.focused("map/planet_open"));
    CHECK(rig.app.sceneCount() == 1);
}

TEST_CASE("cosmic screens: the pause and the results, by the keyboard") {
    for (Language lang : {Language::French, Language::English}) {
        Rig rig(1280, 720, lang);
        GameServices& g = *rig.g;
        UIContext& ui = g.ui;
        std::unique_ptr<Scene> scene = makeGameScene(g, "sylvanox_01");
        GameScene* game = static_cast<GameScene*>(scene.get());
        rig.app.pushScene(std::move(scene));
        rig.app.runFrames(20);
        rig.app.pushScene(std::make_unique<PauseScene>(g, *game));
        rig.app.runFrames(10);
        CHECK(rig.app.sceneCount() == 2);
        CHECK(ui.focused("pause/resume"));
        rig.press(SDL_SCANCODE_DOWN, SDLK_DOWN);
        CHECK(ui.focused("pause/restart"));
        rig.press(SDL_SCANCODE_DOWN, SDLK_DOWN);
        CHECK(ui.focused("pause/settings"));
        rig.press(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
        CHECK(rig.app.sceneCount() == 1);  // Échap: back to the run

        ResultsData d;
        d.run.levelId = "sylvanox_01";
        d.run.completed = true;
        d.run.time = 83.4f;
        d.run.medal = Medal::Gold;
        d.update.newBestTime = true;
        d.nextLevel = "sylvanox_02";
        if (auto l = g.content.loadLevel("sylvanox_01")) d.level = *l;
        rig.app.pushScene(std::make_unique<ResultsScene>(g, *game, d));
        rig.app.runFrames(60);  // the time counted, Nova's victory played (before the rewards, at 2.8 s)
        CHECK(rig.app.sceneCount() >= 2);
        CHECK(ui.focused("results/next"));
        rig.press(SDL_SCANCODE_LEFT, SDLK_LEFT);
        CHECK(ui.focused("results/retry"));
        rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
        CHECK(ui.focused("results/next"));
        rig.press(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
        CHECK(ui.focused("results/back"));
    }
}
