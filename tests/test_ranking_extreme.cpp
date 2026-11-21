// COSMIC ONLINE › CLASSEMENTS on an EXTRÊME level: the level card keeps all its actions (« Jouer ce niveau »,
// « Avis et commentaires (N) »…) as on an ordinary level, each one a keyboard / gamepad stop; the race shown
// while the level is not verified (2.82) gives each attempt its ▶ again (its replay), as the records' rows, and
// (2.85) its 💬 (its comments: « race:<attempt> », « coop:<attempt> » for Duo / Trio) beside it.

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

/// The paths the game asked for (the threads it opened).
std::mutex gPathsMutex;
std::vector<std::string> gPaths;

bool asked(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(gPathsMutex);
    for (const auto& p : gPaths)
        if (p.rfind(prefix, 0) == 0) return true;
    return false;
}

/// A server that logs in, answers the race boards (Solo, Duo, Trio: one attempt each, its replay, 12 comments),
/// and every other request with an empty success.
class EmptyServer final : public IHttpTransport {
public:
    HttpResponse perform(const HttpRequest& rq) override {
        const std::string path = rq.url.substr(rq.url.find("/api/"));
        {
            std::lock_guard<std::mutex> lock(gPathsMutex);
            gPaths.push_back(path);
        }
        const Json player = {{"id", "22222222-2222-4222-8222-222222222222"}, {"username", "Vega"}, {"country_code", "FR"}};
        if (path == "/api/v1/auth/login" || path == "/api/v1/auth/refresh") {
            Json session = {{"access_token", "access-rank-00000000000000000"}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                            {"refresh_token", "refresh-rank-0000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}};
            return ok({{"player", player}, {"session", session}});
        }
        if (path == "/api/v1/auth/me") return ok({{"player", player}});
        if (path.rfind("/api/v1/races/board", 0) == 0) {  // one attempt with its replay and its comments
            Json e = {{"rank", 1},          {"run_id", "run-1"},
                      {"has_replay", true}, {"score", 62.5},
                      {"finished", false},  {"player", player},
                      {"time_ms", 754321},  {"at", "2026-11-20T10:00:00.000Z"},
                      {"comment_count", 12}};
            return ok({{"ranking", Json::array({e})}, {"race", {{"participants", 1}}}});
        }
        if (path.rfind("/api/v1/coop/board", 0) == 0) {  // a team of two or three (long pseudonyms)
            const int size = path.find("size=3") != std::string::npos ? 3 : 2;
            Json players = Json::array();
            for (int k = 0; k < size; ++k)
                players.push_back({{"id", "3333333" + std::to_string(k) + "-3333-4333-8333-333333333333"}, {"username", "Navigatrice_Lointaine_" + std::to_string(k)}});
            Json e = {{"rank", 1},          {"run_id", "team-" + std::to_string(size)},
                      {"has_replay", true}, {"score", 41.5},
                      {"finished", false},  {"players", players},
                      {"revives", 1},       {"incomplete", false},
                      {"time_ms", 754321},  {"at", "2026-11-20T10:00:00.000Z"},
                      {"comment_count", 12}, {"clan_tag", "ASTRA"}};
            return ok({{"ranking", Json::array({e})}, {"race", {{"size", size}}}});
        }
        if (path.find("/comments") != std::string::npos && (path.rfind("/api/v1/races/runs/", 0) == 0 || path.rfind("/api/v1/coop/runs/", 0) == 0))
            return ok({{"comments", Json::array()},
                       {"comment_count", 0},
                       {"attempt", {{"size", 1}, {"run_id", "run-1"}, {"player", player}, {"score", 62.5}}}});
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

namespace {

/// Waits (frames) until the named stop is drawn.
bool waitFor(Application& app, UIContext& ui, const std::string& name) {
    for (int i = 0; i < 300 && !ui.navigable(name); ++i) {
        app.runFrames(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return ui.navigable(name);
}

}  // namespace

TEST_CASE("online ranking: every attempt of the race (Solo, Duo, Trio) has its ▶ and its 💬, side by side, opening its thread") {
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
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
            app.onUpdate = [&g](float dt) { g->online.update(dt); };
            g->progression.tutorial = "finished";
            g->settings.gameplay.attractTrailer = false;
            g->applySettings();
            i18n::setLanguage(lang);
            const char* where = lang == Language::French ? "fr" : "en";
            const fs::path dir = fs::temp_directory_path() / "cosmic_ranking_extreme_comments";
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
            app.pushScene(makeOnlineRanking(*g, "confins_01"));
            float rowRight = 0.0f;
            for (int size = 1; size <= 3; ++size) {
                if (size > 1) {  // Duo, then Trio: the board's next choice, with the keyboard
                    ui.setFocus("online/board/racesize");
                    app.runFrames(1);
                    key(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
                    app.runFrames(2);
                    CHECK(waitFor(app, ui, "online/board/team0/0"));
                }
                const bool rep = waitFor(app, ui, "online/board/racerep0"), com = waitFor(app, ui, "online/board/racecom0");
                if (!rep || !com) std::fprintf(stderr, "%dx%d %s size %d: play %d comments %d\n", w, h, where, size, rep, com);
                CHECK(rep);
                CHECK(com);
                Rect play, bubble;
                if (!ui.navigableRect("online/board/racerep0", play) || !ui.navigableRect("online/board/racecom0", bubble)) continue;
                // The ▶, then the 💬, then the progression: one after the other on the row, never over each other.
                CHECK(play.right() <= bubble.x);
                CHECK(std::abs(play.center().y - bubble.center().y) < 1.0f);
                if (size == 1) {
                    Rect row;
                    CHECK(ui.navigableRect("online/board/race0", row));
                    rowRight = row.right() - ui.s(10);
                    CHECK(bubble.y >= row.y);
                    CHECK(bubble.bottom() <= row.bottom());
                    CHECK(play.x >= row.x + ui.s(84) + ui.s(40));  // the name keeps its room (shortened to it)
                }
                const std::string value = size == 1 ? "62,5 %" : "41,5 %";
                const float valueX = rowRight - ui.text().measure(value, FontFace::Bold, ui.s(18)).x;
                if (bubble.right() > valueX - ui.s(4)) std::fprintf(stderr, "%dx%d %s size %d: comments over the progression\n", w, h, where, size);
                CHECK(bubble.right() <= valueX - ui.s(4));
                // A team: every player's name stops before the ▶.
                for (int k = 0; size > 1 && k < size; ++k) {
                    Rect name;
                    CHECK(ui.navigableRect("online/board/team0/" + std::to_string(k), name));
                    if (name.right() > play.x) std::fprintf(stderr, "%dx%d %s size %d: name %d over the play button\n", w, h, where, size, k);
                    CHECK(name.right() <= play.x);
                }
                // The keyboard (and so the gamepad) opens the attempt's thread.
                ui.setFocus("online/board/racecom0");
                app.runFrames(1);
                key(SDL_SCANCODE_RETURN, SDLK_RETURN);
                for (int i = 0; i < 100 && app.sceneCount() < 2; ++i) app.runFrames(1);
                CHECK(app.sceneCount() == 2);
                const std::string thread = size == 1 ? "/api/v1/races/runs/run-1/comments" : "/api/v1/coop/runs/team-" + std::to_string(size) + "/comments";
                for (int i = 0; i < 200 && !asked(thread); ++i) {
                    app.runFrames(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (!asked(thread)) std::fprintf(stderr, "%dx%d %s: %s never asked\n", w, h, where, thread.c_str());
                CHECK(asked(thread));
                if (app.sceneCount() == 2) {
                    app.popScene();
                    app.runFrames(2);
                }
            }
            app.popScene();
            app.runFrames(1);
            g->online.shutdown();
            app.shutdown();
            app.onSceneRender = nullptr;
            app.onUpdate = nullptr;
            fs::remove_all(dir, ec);
            {
                std::lock_guard<std::mutex> lock(gPathsMutex);
                gPaths.clear();
            }
        }
    i18n::setLanguage(Language::French);
}
