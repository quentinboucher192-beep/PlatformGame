// COSMIC ONLINE › CLASSEMENTS on an EXTRÊME level: the level card keeps all its actions (« Jouer ce niveau »,
// « Avis et commentaires (N) »…) as on an ordinary level, each one a keyboard / gamepad stop; the race shown
// while the level is not verified (2.82) gives each attempt its ▶ again (its replay), as the records' rows.

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/HttpTransport.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;
namespace fs = std::filesystem;

namespace {

/// A server that logs in and answers every other request with an empty success.
class EmptyServer final : public IHttpTransport {
public:
    HttpResponse perform(const HttpRequest& rq) override {
        const std::string path = rq.url.substr(rq.url.find("/api/"));
        const Json player = {{"id", "22222222-2222-4222-8222-222222222222"}, {"username", "Vega"}, {"country_code", "FR"}};
        if (path == "/api/v1/auth/login" || path == "/api/v1/auth/refresh") {
            Json session = {{"access_token", "access-rank-00000000000000000"}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                            {"refresh_token", "refresh-rank-0000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}};
            return ok({{"player", player}, {"session", session}});
        }
        if (path == "/api/v1/auth/me") return ok({{"player", player}});
        if (path.rfind("/api/v1/races/board", 0) == 0) {  // one attempt with its replay
            Json e = {{"rank", 1}, {"run_id", "run-1"}, {"has_replay", true}, {"score", 62.5}, {"finished", false}, {"player", player}};
            return ok({{"ranking", Json::array({e})}, {"race", {{"participants", 1}}}});
        }
        return ok(Json::object());
    }
    const char* name() const override { return "ranking"; }

private:
    static HttpResponse ok(const Json& data) {
        HttpResponse r;
        r.status = 200;
        r.body = Json{{"ok", true}, {"data", data}}.dump();
        r.headers["content-type"] = "application/json";
        return r;
    }
};

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

}  // namespace

TEST_CASE("online ranking: an EXTRÊME level's card keeps « Jouer ce niveau » and « Avis et commentaires »") {
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}}) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        Application app;
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = w;
        cfg.height = h;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        auto g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [&g](bool covered) { g->ui.sceneInput = !covered; };
        app.onUpdate = [&g](float dt) { g->online.update(dt); };  // the answers of the server, as the game does
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->applySettings();
        const fs::path dir = fs::temp_directory_path() / "cosmic_ranking_extreme";
        std::error_code ec;
        fs::remove_all(dir, ec);
        OnlineSettings os = g->settings.online;
        os.enabled = true;
        os.onboardingDone = true;
        g->online.init(os, "http://127.0.0.1:9", std::make_unique<EmptyServer>(), dir);
        bool done = false;
        g->online.login("Vega", "ranking password", false, [&](const ApiResult&) { done = true; });
        for (int i = 0; i < 300 && !done; ++i) {
            g->online.update(0.016f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(g->online.loggedIn());
        UIContext& ui = g->ui;
        for (const char* level : {"confins_01", "noyau_01", "sylvanox_01"}) {
            app.pushScene(makeOnlineRanking(*g, level));
            for (int i = 0; i < 20; ++i) {
                app.runFrames(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CHECK(app.sceneCount() == 1);
            for (const char* b : {"online/level/replay", "online/level/compare", "online/level/ghost", "online/level/submit", "online/level/play",
                                  "online/level/social"}) {
                const bool there = ui.navigable(b);
                if (!there) std::fprintf(stderr, "%dx%d %s: %s missing\n", w, h, level, b);
                CHECK(there);
            }
            // The race of an EXTRÊME level (its view while it is not verified): each attempt keeps its ▶ (its replay),
            // as the records' rows do.
            if (std::string(level) != "sylvanox_01") {
                for (int i = 0; i < 200 && !ui.navigable("online/board/racerep0"); ++i) {  // the board answered
                    app.runFrames(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                CHECK(ui.navigable("online/board/racerep0"));
            }
            // The keyboard reaches them: down from « Envoyer le record » to « Jouer ce niveau », and the
            // focus stays on « Avis et commentaires » once there.
            ui.setFocus("online/level/submit");
            app.runFrames(1);
            key(SDL_SCANCODE_DOWN, SDLK_DOWN);
            app.runFrames(2);
            CHECK(ui.focused("online/level/play"));
            ui.setFocus("online/level/social");
            app.runFrames(2);
            CHECK(ui.focused("online/level/social"));
            app.popScene();
            app.runFrames(1);
        }
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        app.onUpdate = nullptr;
        fs::remove_all(dir, ec);
    }
    i18n::setLanguage(Language::French);
}
