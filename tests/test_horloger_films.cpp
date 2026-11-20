// 2.82: the two transition films after Asterion, teasers of raid 2 — « LE BALANCIER » (data/raids/asterion_balancier.json)
// and « ENCORE UN TOUR » (asterion_tour.json), their scenes and pictures written by
// tools/assetgen/environment/story_horloger.py: both load with their real files, last 100 to 125 s, have their English,
// share the workshop and end on the same nine shots (the six worlds, the watch, the eyes, black) and never name the old man; they play from the films' lists (ADMINISTRATION › Films, and
// Collection › Galerie › Films for everyone) and from a given second; the frozen time of an element (`hold`) holds.

#include <SDL3/SDL.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/progression/Progression.h"
#include "tests/TestFramework.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/FilmCatalog.h"
#include "ui/screens/Navigation.h"

using namespace cosmic;

namespace {

struct FilmRig {
    Application app;
    std::unique_ptr<GameServices> g;
    bool ok = false;
    FilmRig() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 960;
        cfg.height = 540;
        cfg.vsync = false;
        ok = app.init(cfg);
        app.headlessDt = 1.0f / 20.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->progression.asterionFilmSeen = true;
        g->settings.gameplay.attractTrailer = false;
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~FilmRig() {
        app.shutdown();
        app.onSceneRender = nullptr;
        std::error_code ec;
        std::filesystem::remove(Paths::savesDir() / "progress.json", ec);
    }
};

bool namesHim(const std::string& s) {
    for (const char* w : {"Horloger", "horloger", "HORLOGER"})
        if (s.find(w) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("transition films: both load with their files, 100 to 125 s, in French and English, the same last nine shots, nobody named") {
    const auto bal = asterionfilm::balancier();
    const auto tour = asterionfilm::tour();
    CHECK(bal != nullptr);
    CHECK(tour != nullptr);
    if (!bal || !tour) return;
    CHECK(bal->title == "LE BALANCIER");
    CHECK(tour->title == "ENCORE UN TOUR");
    for (const auto& f : {bal, tour}) {
        CHECK(f->isFilm());
        const float d = story::duration(*f);
        CHECK(d >= 100.0f && d <= 125.0f);
        CHECK(f->shots.size() >= 16);
        for (const StoryShot& shot : f->shots) {
            CHECK(shot.stage.has_value());  // every shot animated
            CHECK(shot.duration > 0.0f);
            CHECK(!namesHim(shot.text));
            for (const StoryTitle& t : shot.titles) CHECK(!namesHim(t.text) && !namesHim(t.subtitle));
        }
        const story::StageAssets a = story::stageAssets(*f);
        CHECK(!a.textures.empty() && !a.animations.empty() && !a.sounds.empty());
        for (const auto* list : {&a.textures, &a.animations, &a.sounds})
            for (const std::string& p : *list) {
                if (!FileUtil::exists(Paths::assetsDir() / p)) std::printf("  absent : %s\n", p.c_str());
                CHECK(FileUtil::exists(Paths::assetsDir() / p));
            }
        // Its English: the title, every caption, every title in the picture.
        i18n::setLanguage(Language::English);
        CHECK(i18n::dictionary(Language::English).exact(f->title) != nullptr);
        for (const StoryShot& shot : f->shots) {
            if (!shot.text.empty()) CHECK(i18n::tr(shot.text) != shot.text);
            for (const StoryTitle& t : shot.titles) CHECK(i18n::dictionary(Language::English).exact(t.text) != nullptr);
        }
        i18n::setLanguage(Language::French);
    }
    // The same nine last shots (the six worlds, the watch near Lyse, the gear-eyes, black and « À SUIVRE »), and the workshop.
    const size_t nb = bal->shots.size(), nt = tour->shots.size();
    auto same = [](const StoryShot& a, const StoryShot& b) {
        CHECK(a.scene == b.scene);
        CHECK(a.text == b.text);
        CHECK(a.duration == b.duration);
        CHECK(a.titles.size() == b.titles.size());
        CHECK(a.stage->elements.size() == b.stage->elements.size());
        CHECK(a.stage->sounds.size() == b.stage->sounds.size());
        for (size_t e = 0; e < std::min(a.stage->elements.size(), b.stage->elements.size()); ++e) CHECK(a.stage->elements[e].id == b.stage->elements[e].id);
    };
    for (size_t k = 1; k <= 9; ++k) same(bal->shots[nb - k], tour->shots[nt - k]);
    const char* last[] = {"monde_serre", "monde_glacier", "monde_forge", "monde_ruines", "monde_archives", "monde_cadran", "cadran", "regard", "noir"};
    for (size_t k = 0; k < 9; ++k) CHECK(bal->shots[nb - 9 + k].scene == last[k]);
    for (const std::string shared : {"atelier", "manipulations"}) {  // the workshop and the clockmaker at work: in both films
        const StoryShot* wa = nullptr;
        const StoryShot* wt = nullptr;
        for (const StoryShot& s : bal->shots)
            if (s.scene == shared) wa = &s;
        for (const StoryShot& s : tour->shots)
            if (s.scene == shared) wt = &s;
        CHECK(wa != nullptr && wt != nullptr);
        if (wa && wt) same(*wa, *wt);
    }
    CHECK(!bal->shots.back().titles.empty() && bal->shots.back().titles.front().text == "À SUIVRE");
}

TEST_CASE("transition films: frozen time — an element held stays as it was, then goes on") {
    StoryElement e;
    CHECK(e.localTime(3.0f) == 3.0f);  // no hold: the shot's time
    e.holdFrom = 2.0f;
    CHECK(e.localTime(1.0f) == 1.0f);
    CHECK(e.localTime(2.0f) == 2.0f);
    CHECK(e.localTime(7.5f) == 2.0f);  // for ever
    e.holdTo = 5.0f;
    CHECK(e.localTime(4.0f) == 2.0f);
    CHECK(std::fabs(e.localTime(6.0f) - 3.0f) < 1e-5f);  // goes on from where it stopped
    // Read from a scene: a chunk flying, held at 1 s: at 4 s it is where it was at 1 s.
    const Json j = Json::parse(R"({"size": [960, 540], "elements": [
        {"id": "eclat", "sprite": "animations/stories/asterion_eclat.json", "hold": [1.0],
         "keys": [{"t": 0, "pos": [0, 0], "clip": "roche"}, {"t": 2, "pos": [200, 100], "ease": "linear"}]},
        {"id": "libre", "sprite": "animations/stories/asterion_eclat.json",
         "keys": [{"t": 0, "pos": [0, 0], "clip": "roche"}, {"t": 2, "pos": [200, 100], "ease": "linear"}]}]})");
    const auto st = StoryStage::fromJson(j);
    CHECK(st.has_value());
    if (!st) return;
    CHECK(st->elements.size() == 2);
    const StoryElement& held = st->elements[0];
    CHECK(held.holdFrom == 1.0f && held.holdTo < held.holdFrom);
    const Vec2 p = held.at(held.localTime(4.0f)).pos;
    CHECK(std::fabs(p.x - 100.0f) < 0.01f && std::fabs(p.y - 50.0f) < 0.01f);
    const Vec2 q = st->elements[1].at(st->elements[1].localTime(4.0f)).pos;
    CHECK(std::fabs(q.x - 200.0f) < 0.01f);  // the other one went on
}

TEST_CASE("transition films: in the films' lists (for everyone in the Collection), played from their start or from a second") {
    FilmRig r;
    CHECK(r.ok);
    r.g->progression.asterionAftermath = false;  // nothing unlocked: they are teasers, open to all
    int seen = 0;
    for (bool player : {false, true})
        for (const FilmEntry& e : filmCatalog(*r.g, player)) {
            if (e.id != "asterion_balancier" && e.id != "asterion_tour") continue;
            ++seen;
            CHECK(e.unlocked);
            CHECK(e.seconds >= 100.0f && e.seconds <= 125.0f);
            CHECK(FileUtil::exists(Paths::assetsDir() / e.thumb));
            auto s = makeFilmScene(*r.g, e);
            CHECK(s != nullptr);
            if (!s) continue;
            Scene* scene = s.get();
            r.app.pushScene(std::move(s));
            r.app.runFrames(20);  // one second
            CHECK(storyOf(scene) != nullptr);
            CHECK(filmTime(scene) > 0.5f && filmTime(scene) < 1.6f);
            CHECK(!filmSynced(scene));
            r.app.runFrames(1);
        }
    CHECK(seen == 4);
    // From a given second (the command line's --asterion-balancier / --asterion-tour <seconds>).
    for (asterionfilm::Teaser which : {asterionfilm::Teaser::Balancier, asterionfilm::Teaser::Tour}) {
        while (r.app.sceneCount() > 1) {
            r.app.popScene();
            r.app.runFrames(2);
        }
        auto s = asterionfilm::makeTeaser(*r.g, which, 50.0f);
        CHECK(s != nullptr);
        if (!s) continue;
        Scene* scene = s.get();
        r.app.pushScene(std::move(s));
        r.app.runFrames(4);
        CHECK(std::fabs(filmTime(scene) - 50.2f) < 0.6f);
    }
}
