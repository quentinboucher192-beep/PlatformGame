// 2.68: Asterion's mini-film (data/raids/asterion_film.json, its scenes written by
// tools/assetgen/environment/story_asterion.py, played by the stories' scene): about two minutes in
// ten shots, real files; its first viewing — before anything of the titan is shown — cannot be
// skipped and is « seen » only at its end (an interrupted one plays again); « Revoir la
// cinématique » may be left; « seen » survives the progression copies and the migration.

#include <SDL3/SDL.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/online/ProgressMerge.h"
#include "game/progression/Progression.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/Navigation.h"

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

void click(float x, float y) {
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = x;
    down.button.y = y;
    SDL_PushEvent(&down);
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    SDL_PushEvent(&up);
}

/// A headless game at 10 frames per simulated second (two minutes of film in a few seconds).
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    bool ok = false;
    Rig() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 960;
        cfg.height = 540;
        cfg.vsync = false;
        ok = app.init(cfg);
        app.headlessDt = 1.0f / 10.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = false;
        app.pushScene(makeMainMenu(*g));
        frames(0.5f);
    }
    void frames(float seconds) { app.runFrames(static_cast<int>(std::lround(seconds * 10.0f))); }
    /// The game closed (its window): the scenes go first (they use the services).
    void close() {
        if (closed) return;
        closed = true;
        app.shutdown();
        app.onSceneRender = nullptr;
        asterionfilm::autoplayHeadless = false;
    }
    bool closed = false;
    ~Rig() {
        close();
        std::error_code ec;  // the saves folder is shared by every test: no progression left behind
        std::filesystem::remove(Paths::savesDir() / "progress.json", ec);
    }
};

}  // namespace

TEST_CASE("asterion film: ten shots, about two minutes, the real titan, captions and titles in French and English") {
    std::string err;
    auto f = story::loadAsterionFilm(&err);
    CHECK(f.has_value());
    if (!f) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(f->isFilm());
    CHECK(f->shots.size() == 10);
    CHECK(f->titleCard == 0.0f);  // it ends on its last shot
    const float total = story::duration(*f);
    CHECK(total >= 110.0f && total <= 130.0f);
    CHECK(!f->music.empty() && FileUtil::exists(Paths::assetsDir() / f->music));  // its own score
    std::set<std::string> sheets;
    int titles = 0, captions = 0;
    for (const StoryShot& shot : f->shots) {
        CHECK(shot.duration > 0.0f && story::shotLength(shot) == shot.duration);
        CHECK(shot.stage.has_value());
        if (!shot.stage) continue;
        const StoryStage& st = *shot.stage;
        CHECK(st.length > 0.0f);                                // written for a length (stretched to the script's)
        CHECK(st.lastTime() <= st.length + 1e-3f);
        const auto [c0, z0] = st.cameraAt(0.0f);
        const auto [c1, z1] = st.cameraAt(st.length);
        CHECK(c0.x != c1.x || c0.y != c1.y || z0 != z1);         // the camera moves in every shot
        CHECK(!st.sounds.empty());                               // and every shot is heard
        for (const StorySfx& fx : st.sounds) CHECK(FileUtil::exists(Paths::assetsDir() / fx.sound));
        for (const StoryElement& e : st.elements) {
            if (e.kind == StoryElementKind::Layer) {
                CHECK(FileUtil::exists(Paths::assetsDir() / e.source));
            } else if (e.kind == StoryElementKind::Sprite) {
                sheets.insert(e.source);
                auto j = FileUtil::loadJson(Paths::assetsDir() / e.source);
                CHECK(j.has_value());
                if (!j) continue;
                auto set = AnimationSet::fromJson(*j, e.source);
                CHECK(FileUtil::exists(Paths::assetsDir() / set->texturePath));
                for (const StoryKey& k : e.keys)
                    if (!k.clip.empty() && !set->clip(k.clip)) {
                        CHECK(set->clip(k.clip) != nullptr);
                        std::printf("  %s: no clip %s\n", e.source.c_str(), k.clip.c_str());
                    }
            }
        }
        // Every caption, title and subtitle has its English (names stay names).
        i18n::setLanguage(Language::English);
        if (!shot.text.empty()) {
            ++captions;
            CHECK(i18n::tr(shot.text) != shot.text);
        }
        for (const StoryTitle& t : shot.titles) {
            ++titles;
            CHECK(t.to > t.t && t.to <= shot.duration + 1e-3f);
            CHECK(i18n::dictionary(Language::English).exact(t.text) != nullptr);
            if (!t.subtitle.empty()) CHECK(i18n::tr(t.subtitle) != t.subtitle);
        }
        i18n::setLanguage(Language::French);
    }
    CHECK(captions >= 7);
    CHECK(titles >= 9);  // the seven gigaboss, the title card, the call
    // The real titan, in layers: each branch, its heart and its aura; Nova's own sheet.
    for (const char* part : {"aura", "crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite", "coeur"})
        CHECK(sheets.count(std::string("animations/stories/asterion_") + part + ".json") == 1);
    CHECK(sheets.count("animations/nova.json") == 1);
    // The seven parts named one by one, as the raid's body names them (data/raids/asterion.json).
    std::set<std::string> named;
    for (const StoryShot& shot : f->shots)
        for (const StoryTitle& t : shot.titles) named.insert(t.text);
    for (const char* boss : {"SKULLMAW", "DREADFANG", "PYROCLAST", "HEARTBREAKER", "VOIDWORM", "GRAVITON", "TITANUS", "ASTERION"}) CHECK(named.count(boss) == 1);
    // Two titles of a shot never overlap in time (one is drawn at a time anyway).
    for (const StoryShot& shot : f->shots)
        for (size_t i = 1; i < shot.titles.size(); ++i) CHECK(shot.titles[i].t >= shot.titles[i - 1].to);
}

TEST_CASE("asterion film: its script is edited by hand — a new length stretches the scene, a shot may have no caption") {
    const auto dir = std::filesystem::temp_directory_path() / "cosmic_film_script";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "raids", ec);
    auto script = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion_film.json");
    CHECK(script.has_value());
    if (!script) return;
    std::filesystem::copy_file(Paths::dataDir() / "raids" / "asterion_film_scenes.json", dir / "raids" / "asterion_film_scenes.json", ec);
    (*script)["shots"][0]["duration"] = 24.0;
    (*script)["shots"][0]["text"] = "";
    {
        std::ofstream o(dir / "raids" / "asterion_film.json");
        o << script->dump();
    }
    std::string err;
    auto f = story::loadAsterionFilm(dir, &err);
    CHECK(f.has_value());
    if (f) {
        CHECK(f->shots[0].duration == 24.0f);
        CHECK(f->shots[0].text.empty());
        CHECK(f->shots[0].stage && f->shots[0].stage->length == 12.0f);  // written for 12 s: played at half speed
    }
    // A scene missing from the scenes file: no film (it never holds a screen then).
    (*script)["shots"][1]["scene"] = "inconnue";
    {
        std::ofstream o(dir / "raids" / "asterion_film.json");
        o << script->dump();
    }
    CHECK(!story::loadAsterionFilm(dir, &err).has_value());
    // A story (not a film) still needs its lines.
    Json story = {{"boss", "x"}, {"version", 2}, {"shots", Json::array({Json{{"text", ""}}})}};
    CHECK(!BossStory::fromJson(story).has_value());
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("asterion film: the first viewing cannot be skipped, « seen » only at its end, then never forced again") {
    Rig r;
    CHECK(r.ok);
    GameServices& g = *r.g;
    Scene* menu = r.app.topScene();
    CHECK(asterionfilm::film() != nullptr);
    CHECK(asterionfilm::due(g.progression));
    // Automated runs never start it by themselves (screenshots keep their screens)…
    CHECK(!asterionfilm::hold(g));
    // …the game does: the guard of the screens that show Asterion pushes it, once.
    asterionfilm::autoplayHeadless = true;
    CHECK(asterionfilm::hold(g));
    CHECK(asterionfilm::hold(g));
    r.frames(0.5f);
    CHECK(r.app.sceneCount() == 2);
    CHECK(filmLocked(r.app.topScene()));
    CHECK(storyShot(r.app.topScene()) == 0);
    CHECK(asterionfilm::hold(g));  // while it plays: still held, never pushed twice
    r.frames(0.3f);
    CHECK(r.app.sceneCount() == 2);
    // Nothing skips it or leaves it: Échap, Entrée, Espace, a click anywhere.
    r.frames(1.0f);
    for (int round = 0; round < 2; ++round) {
        key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
        r.frames(0.3f);
        key(SDL_SCANCODE_RETURN, SDLK_RETURN);
        r.frames(0.3f);
        key(SDL_SCANCODE_SPACE, SDLK_SPACE);
        r.frames(0.3f);
        click(480.0f, 270.0f);
        r.frames(0.3f);
        click(900.0f, 520.0f);  // where « Quitter » would be
        r.frames(0.3f);
        key(SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
        r.frames(0.3f);
    }
    CHECK(r.app.sceneCount() == 2);
    CHECK(filmLocked(r.app.topScene()));
    CHECK(storyShot(r.app.topScene()) == 0);  // still the first shot (12 s): nothing skipped
    CHECK(!g.progression.asterionFilmSeen);
    // Its caption has the bottom band's whole width (no hint to measure around), two lines at most.
    g.ui.begin(r.app, 0.016f);
    const auto f = asterionfilm::film();
    for (const StoryShot& shot : f->shots) {
        if (shot.text.empty()) continue;
        const StoryNarrationLayout free = layoutStoryNarration(g.ui, shot.text, false);
        const StoryNarrationLayout hinted = layoutStoryNarration(g.ui, shot.text, true);
        CHECK(free.hint.w == 0.0f && free.area.w > hinted.area.w);
        CHECK(!free.lines.empty() && free.lines.size() <= kStoryNarrationLines);
        for (const std::string& l : free.lines) CHECK(g.ui.text().measure(l, FontFace::Body, free.px).x <= free.area.w + 0.5f);
    }
    // To its end: « seen » now (and saved), the screen below again.
    r.frames(story::duration(*f) + 2.5f);
    CHECK(g.progression.asterionFilmSeen);
    CHECK(r.app.sceneCount() == 1);
    CHECK(r.app.topScene() == menu);
    Progression saved;
    saved.load();
    CHECK(saved.asterionFilmSeen);
    // Never forced again: the guard lets every screen show the titan.
    CHECK(!asterionfilm::due(g.progression));
    CHECK(!asterionfilm::hold(g));
    r.frames(0.5f);
    CHECK(r.app.sceneCount() == 1);
}

TEST_CASE("asterion film: a first viewing interrupted (the window closed) plays again next time") {
    Progression p;
    {
        Rig r;
        asterionfilm::autoplayHeadless = true;
        CHECK(asterionfilm::hold(*r.g));
        r.frames(20.0f);  // well into it
        CHECK(filmLocked(r.app.topScene()));
        CHECK(storyShot(r.app.topScene()) >= 1);
        r.g->progression.save();  // what the game writes as it closes
        r.close();                // the window closed: the film goes, never « seen »
        CHECK(!r.g->progression.asterionFilmSeen);
        p.load();
    }
    CHECK(!p.asterionFilmSeen);
    Rig r;
    r.g->progression = p;
    asterionfilm::autoplayHeadless = true;
    CHECK(asterionfilm::hold(*r.g));  // pushed again: the previous one is gone
    r.frames(0.5f);
    CHECK(filmLocked(r.app.topScene()));
    CHECK(storyShot(r.app.topScene()) == 0);  // from its start
}

TEST_CASE("asterion film: « Revoir la cinématique » plays it again, and that viewing may be left") {
    Rig r;
    GameServices& g = *r.g;
    g.progression.asterionFilmSeen = true;
    Scene* menu = r.app.topScene();
    auto replay = asterionfilm::make(g, false);
    CHECK(replay != nullptr);
    r.app.pushScene(std::move(replay));
    r.frames(1.0f);
    CHECK(r.app.sceneCount() == 2);
    CHECK(!filmLocked(r.app.topScene()));
    CHECK(storyShot(r.app.topScene()) == 0);
    // « Passer » goes on to the next shot…
    key(SDL_SCANCODE_SPACE, SDLK_SPACE);
    r.frames(1.0f);
    CHECK(storyShot(r.app.topScene()) == 1);
    // …« Quitter » (Échap, Ⓑ) leaves it.
    key(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    r.frames(1.5f);
    CHECK(r.app.sceneCount() == 1);
    CHECK(r.app.topScene() == menu);
    CHECK(g.progression.asterionFilmSeen);
    // The button needs its label measured: a width that holds « Revoir la cinématique ».
    g.ui.begin(r.app, 0.016f);
    CHECK(asterionfilm::replayButtonWidth(g.ui) > g.ui.text().measure("Revoir la cinématique", FontFace::Bold, g.ui.s(17)).x);
}

TEST_CASE("asterion film: « seen » kept by the progression, its copies and the account migration") {
    Progression p;
    CHECK(!p.asterionFilmSeen);
    p.asterionFilmSeen = true;
    Progression q;
    q.fromJson(p.toJson());
    CHECK(q.asterionFilmSeen);
    Progression old;  // a progression written before 2.68: not seen
    old.fromJson(Json{{"completedLevels", Json::array()}});
    CHECK(!old.asterionFilmSeen);
    // Either side seen: seen (a copy from another computer, the migration of an old account).
    Json mine = Progression().toJson(), theirs = p.toJson();
    CHECK(mergeProgressJson(mine, theirs)["asterionFilmSeen"].get<bool>());
    CHECK(mergeProgressJson(theirs, mine)["asterionFilmSeen"].get<bool>());
    CHECK(!mergeProgressJson(mine, Progression().toJson())["asterionFilmSeen"].get<bool>());
    const Json merged = mergeProgressCopyFiles(Json{{"progress", mine}}, Json{{"progress", theirs}}, 20000);
    Progression r;
    r.fromJson(merged["progress"]);
    CHECK(r.asterionFilmSeen);
}

TEST_CASE("asterion film: the generator gives the same bytes at every run") {
    // Needs Python with numpy and Pillow (the asset tools); skipped without them.
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_NULL_DEVICE " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification du générateur du film d'Asterion sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assetgen/environment/story_asterion.py --check > " COSMIC_NULL_DEVICE " 2>&1") == 0);
}
