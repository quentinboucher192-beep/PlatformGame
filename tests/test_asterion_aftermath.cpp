// 2.81: « Après Asterion » (data/raids/asterion_apres.json, its scenes written by
// tools/assetgen/environment/story_asterion_apres.py, game/raid/Aftermath, ui/screens/AsterionFilm): what the fall
// changes in the world, watched together at the server's time — the same server time gives the same image, a late
// joiner starts mid-film (« Depuis le début » offered), the clans of the fall named from the server (the viewer's
// highlighted, the generic text without data); the administrators' test of the Heart plays the victory film then this
// one; ADMINISTRATION › Films lists every film found and plays each (nothing overlaps, in both languages); the
// tutorial replayed by an administrator grants nothing and resets nothing.

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
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
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/progression/Progression.h"
#include "game/raid/Aftermath.h"
#include "game/tutorial/Tutorial.h"
#include "tests/TestFramework.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/FilmCatalog.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/TutorialScenes.h"

using namespace cosmic;

namespace {

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}
bool inside(const Rect& in, const Rect& out) {
    return in.x >= out.x - 0.5f && in.y >= out.y - 0.5f && in.right() <= out.right() + 0.5f && in.bottom() <= out.bottom() + 0.5f;
}

void pressKey(Application& app, SDL_Scancode sc, SDL_Keycode key) {
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = sc;
    down.key.key = key;
    down.key.down = true;
    SDL_PushEvent(&down);
    app.runFrames(2);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    SDL_PushEvent(&up);
    app.runFrames(1);
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string isoOf(int64_t ms) {
    const time_t s = static_cast<time_t>(ms / 1000);
    std::tm t{};
#ifdef _WIN32
    gmtime_s(&t, &s);
#else
    gmtime_r(&s, &t);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                  static_cast<int>(ms % 1000));
    return b;
}

/// An event's sheet as the server gives it: its clock `serverMs`, the film starting at `filmAtMs`, those clans.
Json sheet(int64_t serverMs, int64_t filmAtMs, const Json& clans, const std::string& mine) {
    return Json{{"server_time", isoOf(serverMs)},
                {"aftermath",
                 {{"fallen_at", isoOf(filmAtMs - aftermath::kDelayMs)},
                  {"film_at", isoOf(filmAtMs)},
                  {"clans", clans},
                  {"my_clan", mine.empty() ? Json() : Json(mine)},
                  {"took_part", !mine.empty()},
                  {"official", false},
                  {"unlocked", !mine.empty()}}}};
}

struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    bool ok = false;
    explicit Rig(float dt = 1.0f / 10.0f) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 960;
        cfg.height = 540;
        cfg.vsync = false;
        ok = app.init(cfg);
        app.headlessDt = dt;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->progression.asterionFilmSeen = true;
        g->settings.gameplay.attractTrailer = false;
        asterionfilm::resetParties();
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~Rig() {
        app.shutdown();  // the scenes first (they use the services)
        app.onSceneRender = nullptr;
        asterionfilm::resetParties();
        std::error_code ec;  // the saves folder is shared by every test: no progression left behind
        std::filesystem::remove(Paths::savesDir() / "progress.json", ec);
    }
};

}  // namespace

TEST_CASE("après asterion: eight shots, 60 to 90 s, real files, one shot naming the clans, French and English") {
    std::string err;
    auto f = story::loadAsterionAftermath(&err);
    CHECK(f.has_value());
    if (!f) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(f->isFilm());
    CHECK(f->shots.size() == 8);
    const float total = story::duration(*f);
    CHECK(total >= 60.0f && total <= 90.0f);
    CHECK(!f->music.empty() && FileUtil::exists(Paths::assetsDir() / f->music));
    int rosters = 0, captions = 0;
    for (const StoryShot& shot : f->shots) {
        CHECK(shot.stage.has_value());
        if (!shot.stage) continue;
        const StoryStage& st = *shot.stage;
        CHECK(st.length > 0.0f && st.lastTime() <= st.length + 1e-3f);
        const auto [c0, z0] = st.cameraAt(0.0f);
        const auto [c1, z1] = st.cameraAt(st.length);
        CHECK(c0.x != c1.x || c0.y != c1.y || z0 != z1);  // the camera moves in every shot
        CHECK(!st.sounds.empty());
        for (const StorySfx& fx : st.sounds) CHECK(FileUtil::exists(Paths::assetsDir() / fx.sound));
        for (const StoryElement& e : st.elements) {
            if (e.kind == StoryElementKind::Layer) CHECK(FileUtil::exists(Paths::assetsDir() / e.source));
            if (e.kind != StoryElementKind::Sprite) continue;
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
        i18n::setLanguage(Language::English);
        if (!shot.text.empty()) {
            ++captions;
            CHECK(i18n::tr(shot.text) != shot.text);
        }
        for (const StoryTitle& t : shot.titles) {
            CHECK(t.to > t.t && t.to <= shot.duration + 1e-3f);
            CHECK(i18n::dictionary(Language::English).exact(t.text) != nullptr);
            if (!t.subtitle.empty()) CHECK(i18n::tr(t.subtitle) != t.subtitle);
        }
        if (shot.roster) {
            ++rosters;
            CHECK(shot.roster->to > shot.roster->t && shot.roster->to <= shot.duration + 1e-3f);
            CHECK(i18n::tr(shot.roster->generic) != shot.roster->generic);  // « les clans de la galaxie »
        }
        i18n::setLanguage(Language::French);
    }
    CHECK(rosters == 1);
    CHECK(captions >= 6);
    CHECK(i18n::dictionary(Language::English).exact(f->title) != nullptr);
}

TEST_CASE("après asterion: the server's time and clans read, the same server time gives the same image, a late joiner starts mid-film") {
    // Parsing: the server's time (its milliseconds), the start, the clans in its order, the viewer's.
    CHECK(aftermath::parseIsoMs("2026-11-20T18:04:05.250Z").value_or(0) == aftermath::parseIsoMs("2026-11-20T18:04:05Z").value_or(0) + 250);
    CHECK(!aftermath::parseIsoMs("hier"));
    const int64_t film = 1'800'000'000'000;
    Json clans = Json::array({{{"id", "c1"}, {"tag", "AAA"}, {"name", "Les Premiers"}}, {{"id", "c2"}, {"tag", "BBB"}, {"name", "Clan Bleu"}}});
    auto p = aftermath::partyFromJson(sheet(film, film, clans, "c2")["aftermath"], "ev");
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->filmAtMs == film && p->fallenAtMs == film - aftermath::kDelayMs);
    int own = -1;
    const auto names = aftermath::rosterNames(*p, &own);
    CHECK(names.size() == 2 && names[0] == "[AAA] Les Premiers" && names[1] == "[BBB] Clan Bleu");
    CHECK(own == 1);
    CHECK(!aftermath::partyFromJson(Json{{"clans", clans}}, "ev"));  // never a start of our own
    // The position: the same server time, the same second of the film, whatever the computer's own clock.
    CHECK(aftermath::positionAt(film + 30'000, film) == 30.0);
    CHECK(aftermath::positionAt(film - 5'000, film) == -5.0);
    CHECK(aftermath::liveAt(film + 30'000, film, 81.0) && !aftermath::liveAt(film + 82'000, film, 81.0) && !aftermath::liveAt(film - 20'000, film, 81.0));
    CHECK(aftermath::liveAt(film - 10'000, film, 81.0, 15.0));
    const Json a = Json{{"server_time", isoOf(film)}};
    CHECK(aftermath::skewMs(a, film - 120'000) == 120'000);  // a computer two minutes late

    Rig r(1.0f / 20.0f);
    CHECK(r.ok);
    // Two computers whose clocks differ by five minutes read the same sheet: the same film time.
    const int64_t local = nowMs();
    float times[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        const int64_t server = local + (k == 0 ? 0 : 300'000);  // the server's clock as each one sees it
        asterionfilm::resetParties();
        asterionfilm::noteEvent(*r.g, sheet(server, server - 30'000, clans, "c1"), "ev1");
        auto s = asterionfilm::makeAftermath(*r.g, "ev1", true);
        CHECK(s != nullptr);
        if (!s) return;
        Scene* scene = s.get();
        r.app.pushScene(std::move(s));
        r.app.runFrames(2);
        CHECK(filmSynced(scene));
        times[k] = filmTime(scene);
        CHECK(std::fabs(times[k] - 30.1f) < 0.6f);  // joined 30 s in: mid-film
        r.app.runFrames(16);                          // its first instant never takes a key
        if (k == 1) {
            // « Depuis le début » (R): from its first image, on its own.
            pressKey(r.app, SDL_SCANCODE_R, SDLK_R);
            CHECK(!filmSynced(scene));
            CHECK(filmTime(scene) < 1.0f);
        }
        pressKey(r.app, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
        r.app.runFrames(20);
        CHECK(r.app.sceneCount() == 1);
    }
    CHECK(std::fabs(times[0] - times[1]) < 0.15f);
    // Before its time: a countdown (negative), then its first image at the server's time.
    asterionfilm::resetParties();
    asterionfilm::noteEvent(*r.g, sheet(local, local + 3'000, clans, ""), "ev2");
    auto s = asterionfilm::makeAftermath(*r.g, "ev2", true);
    Scene* scene = s.get();
    r.app.pushScene(std::move(s));
    r.app.runFrames(2);
    CHECK(filmSynced(scene) && filmTime(scene) < -2.5f && filmTime(scene) > -3.2f);
    r.app.runFrames(80);  // four seconds
    CHECK(filmSynced(scene) && filmTime(scene) > 0.5f && filmTime(scene) < 1.3f);
    pressKey(r.app, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    r.app.runFrames(20);
    // Over: never pushed by itself, but watched again from its start.
    asterionfilm::resetParties();
    asterionfilm::noteEvent(*r.g, sheet(local, local - 600'000, clans, "c1"), "ev3");
    auto late = asterionfilm::makeAftermath(*r.g, "ev3", true);
    CHECK(late && !filmSynced(late.get()));
    CHECK(r.g->progression.asterionAftermath);  // took part: unlocked for later
}

TEST_CASE("après asterion: the clans' names from the server, the viewer's highlighted; without data, the generic text") {
    Rig r;
    CHECK(r.ok);
    auto f = asterionfilm::aftermath();
    CHECK(f != nullptr);
    if (!f) return;
    const StoryRoster* ro = nullptr;
    for (const StoryShot& shot : f->shots)
        if (shot.roster) ro = &*shot.roster;
    CHECK(ro != nullptr);
    if (!ro) return;
    std::vector<std::string> many;
    for (int i = 0; i < 150; ++i) many.push_back("[T" + std::to_string(i) + "] Clan numéro " + std::to_string(i));
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{960, 540}}) {
        r.app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
        r.app.renderer().refreshOutputSize();
        for (Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            r.app.runFrames(1);
            r.g->ui.begin(r.app, 1.0f / 60.0f);
            const float band = std::round(r.g->ui.screenH() * 0.13f);
            const Rect img{0, band, r.g->ui.screenW(), r.g->ui.screenH() - 2.0f * band};
            const float titleBottom = img.y + img.h * 0.25f;
            // No data (offline, a test): the generic text, alone, inside its area.
            const FilmRosterLayout G = layoutFilmRoster(r.g->ui, img, *ro, {}, -1, titleBottom);
            CHECK(G.generic && G.texts.size() == 1 && G.texts[0] == i18n::tr("les clans de la galaxie"));
            CHECK(inside(G.pills[0], G.area) && G.area.y >= titleBottom);
            // A few clans: every name whole, the viewer's marked « · votre clan ».
            const std::vector<std::string> few = {"[AAA] Les Premiers", "[BBB] Clan Bleu", "[ÉTÉ] Les Étoiles d'été", "[ZZ] Z"};
            const FilmRosterLayout F = layoutFilmRoster(r.g->ui, img, *ro, few, 2, titleBottom);
            CHECK(!F.generic && F.pills.size() == 4);
            CHECK(F.texts[2].find(i18n::tr("votre clan")) != std::string::npos);
            // Many: the first ones, the viewer's always, then « et N autres clans »; nothing outside, nothing overlapping.
            const FilmRosterLayout M = layoutFilmRoster(r.g->ui, img, *ro, many, 140, titleBottom);
            CHECK(M.index.back() == -1);
            bool ownShown = false;
            for (int i : M.index) ownShown = ownShown || i == 140;
            CHECK(ownShown);
            for (const FilmRosterLayout* L : {&F, &M}) {
                for (size_t i = 0; i < L->pills.size(); ++i) {
                    CHECK(inside(L->pills[i], L->area));
                    {
                        i18n::Verbatim asShown;
                        CHECK(r.g->ui.text().measure(L->texts[i], FontFace::Bold, L->px).x <= L->pills[i].w + 0.5f);
                    }
                    for (size_t j = i + 1; j < L->pills.size(); ++j) CHECK(!overlaps(L->pills[i], L->pills[j]));
                }
            }
            // The top band of a film watched together: « EN DIRECT » and « Depuis le début » never meet.
            for (bool countdown : {false, true}) {
                const FilmLiveLayout B = layoutFilmLive(r.g->ui, countdown, true);
                CHECK(B.showButton && inside(B.button, B.band) && inside(B.label, B.band) && !overlaps(B.label, B.button));
            }
        }
    }
    i18n::setLanguage(Language::French);
    // Played without data: its roster shot shows the generic text (an administrator's preview, a test).
    auto preview = asterionfilm::makeAftermathPreview(*r.g);
    CHECK(preview != nullptr && !filmSynced(preview.get()));
}

TEST_CASE("après asterion: the administrators' test of the Heart plays the victory film, then this one with the generic text") {
    Rig r;
    CHECK(r.ok);
    asterionfilm::playFall(*r.g, "", false);  // a test: no event, nothing asked of the server
    r.app.runFrames(10);
    CHECK(storyOf(r.app.topScene()) == asterionfilm::victory().get());
    pressKey(r.app, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);  // the victory film left (or played to its end)
    r.app.runFrames(10);
    CHECK(storyOf(r.app.topScene()) == asterionfilm::aftermath().get());
    CHECK(!filmSynced(r.app.topScene()));
    CHECK(filmTime(r.app.topScene()) < 3.0f);  // from its start
    r.app.runFrames(10);
    pressKey(r.app, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    r.app.runFrames(20);
    CHECK(r.app.sceneCount() == 1);
}

TEST_CASE("films list: every film of the game found and played; nothing overlaps at 1280x720 and 1920x1080, in French and English") {
    Rig r;
    CHECK(r.ok);
    const std::vector<FilmEntry> all = filmCatalog(*r.g);
    std::set<std::string> ids;
    for (const FilmEntry& e : all) ids.insert(e.id);
    CHECK(!r.g->stories.all().empty());
    for (const auto& [boss, s] : r.g->stories.all()) CHECK(ids.count("story:" + boss) == 1);  // every gigaboss story
    for (const char* id : {"asterion_film", "asterion_victoire", "asterion_apres", "asterion_balancier", "asterion_tour", "trailer", "tutorial"})
        CHECK(ids.count(id) == 1);
    CHECK(all.size() == r.g->stories.all().size() + 7);  // 2.82: the two transition films
    for (const FilmEntry& e : all) {
        CHECK(!e.title.empty() && !e.kind.empty() && !e.where.empty());
        CHECK(e.tutorial || e.seconds > 4.0f);
        if (!e.thumb.empty()) CHECK(FileUtil::exists(Paths::assetsDir() / e.thumb));
        CHECK(makeFilmScene(*r.g, e) != nullptr);  // each one plays (the tutorial: its replay)
    }
    // Their texts in English.
    i18n::setLanguage(Language::English);
    for (const FilmEntry& e : all)
        for (const std::string* t : {&e.kind, &e.where}) CHECK(i18n::tr(*t) != *t);
    CHECK(i18n::tr(filmLengthText(all.back())) != filmLengthText(all.back()));
    i18n::setLanguage(Language::French);
    // The search and the filters.
    FilmList list(*r.g, false);
    list.search = "pyrrhax";
    CHECK(list.shown().size() == 1);
    list.search.clear();
    list.filter = 2;
    CHECK(list.shown().size() == 5);  // 2.82: with « Le Balancier » and « Encore un tour »
    list.filter = 0;
    // Each row measured: inside, nothing overlapping, every text in its place.
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
        r.app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
        r.app.renderer().refreshOutputSize();
        for (Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            r.app.runFrames(1);
            UIContext& ui = r.g->ui;
            ui.begin(r.app, 1.0f / 60.0f);
            const float w = std::min(ui.screenW() - ui.s(96), ui.s(1760)) - ui.s(44) - ui.s(16);  // the administration's body, its scroll bar
            for (bool player : {false, true})
                for (const FilmEntry& e : filmCatalog(*r.g, player)) {
                    const Rect row{ui.s(70), ui.s(200), w, filmRowHeight(ui)};
                    const FilmRowLayout L = layoutFilmRow(ui, row, e, player);
                    const std::vector<Rect> parts = {L.thumb, L.title, L.meta, L.where, L.button};
                    for (size_t i = 0; i < parts.size(); ++i) {
                        CHECK(inside(parts[i], row));
                        for (size_t j = i + 1; j < parts.size(); ++j)
                            if (parts[i].h > 0.0f && parts[j].h > 0.0f) {
                                if (overlaps(parts[i], parts[j])) std::printf("  %s : %zu et %zu se chevauchent (%gx%g)\n", e.id.c_str(), i, j, size.x, size.y);
                                CHECK(!overlaps(parts[i], parts[j]));
                            }
                    }
                    i18n::Verbatim asShown;
                    CHECK(ui.text().measure(L.titleShown, FontFace::Bold, L.titlePx).x <= L.title.w + 0.5f);
                    CHECK(ui.text().measure(L.metaShown, FontFace::Body, L.metaPx).x <= L.meta.w + 0.5f);
                    for (const std::string& l : L.whereLines) CHECK(ui.text().measure(l, FontFace::Body, L.wherePx).x <= L.where.w + 0.5f);
                    CHECK(static_cast<float>(L.whereLines.size()) * L.wherePx * 1.3f <= L.where.h + 0.5f);
                    if (L.whereLines.empty())
                        std::printf("  %s (%gx%g) : « où » sans place (ligne %g, titre %g, genre %g, taille %g)\n", e.id.c_str(), size.x, size.y, row.h,
                                    L.title.h, L.meta.h, L.wherePx);
                    CHECK(!L.whereLines.empty());  // where players see it is always shown
                    CHECK(ui.text().measure(i18n::tr(L.buttonText), FontFace::Bold, ui.s(16)).x <= L.button.w);
                }
            ui.end();
        }
    }
    i18n::setLanguage(Language::French);
    r.app.setWindowSize(960, 540);
    r.app.renderer().refreshOutputSize();
    // The player's list (Collection › Galerie › Films): Asterion's films and the trailer, each with its lock.
    r.g->progression.asterionAftermath = false;
    for (const FilmEntry& e : filmCatalog(*r.g, true)) {
        CHECK(e.id.rfind("story:", 0) != 0 && e.id != "tutorial");
        if (e.id == "asterion_apres" || e.id == "asterion_victoire") CHECK(!e.unlocked);
        if (e.id == "trailer") CHECK(e.unlocked);
    }
    r.g->progression.asterionAftermath = true;
    for (const FilmEntry& e : filmCatalog(*r.g, true))
        if (e.id == "asterion_apres") CHECK(e.unlocked);
}

TEST_CASE("films list: the tutorial replayed by an administrator grants nothing and resets nothing") {
    for (const char* state : {"finished", "skipped", ""}) {
        Rig r;
        CHECK(r.ok);
        GameServices& g = *r.g;
        g.progression.tutorial = state;
        const Json before = g.progressCopyBody();
        const Json collectionBefore = g.collection.toJson();
        const Json achievementsBefore = g.achievements.toJson();
        const bool announced = g.collection.announced(tutorial::kSouvenirTitle);
        const std::string worn = g.collection.equippedId("title");
        auto scene = makeTutorialReplay(g);
        CHECK(scene != nullptr);
        if (!scene) return;
        GameScene* game = static_cast<GameScene*>(scene.get());
        CHECK(game->tutorial() && game->tutorialReplay());
        r.app.switchScene(std::move(scene), 0.0f);
        r.app.runFrames(10);
        CHECK(g.progression.tutorial == state);  // never « started »
        GameWorld* w = game->world();
        CHECK(w && w->training());
        if (w)
            for (auto& e : w->entities())
                if (e->type() == "exit") {
                    w->reachExit(*e);
                    break;
                }
        for (int i = 0; i < 300; ++i) r.app.runFrames(1);
        CHECK(g.progression.tutorial == state);  // never « finished »
        {
            const Json after = g.progressCopyBody();
            for (auto it = before["files"].begin(); it != before["files"].end(); ++it)
                if (after["files"][it.key()] != it.value()) {
                    std::printf("  « %s » : %s a changé\n", state, it.key().c_str());
                    for (auto f = it.value().begin(); it.value().is_object() && f != it.value().end(); ++f)
                        if (after["files"][it.key()][f.key()] != f.value())
                            std::printf("    %s : %s -> %s\n", f.key().c_str(), f.value().dump().substr(0, 200).c_str(),
                                        after["files"][it.key()][f.key()].dump().substr(0, 200).c_str());
                }
        }
        CHECK(g.collection.announced(tutorial::kSouvenirTitle) == announced);
        CHECK(g.collection.equippedId("title") == worn);
        CHECK(g.progressCopyBody() == before);
        CHECK(g.collection.toJson() == collectionBefore);
        CHECK(g.achievements.toJson() == achievementsBefore);
        // Its end card, without souvenir: nothing of it overlaps.
        r.g->ui.begin(r.app, 1.0f / 60.0f);
        const TutorialEndLayout E = layoutTutorialEnd(r.g->ui, true);
        const std::vector<Rect> parts = {E.bip, E.title, E.subtitle, E.learntTitle, E.reminder, E.start};
        for (size_t i = 0; i < parts.size(); ++i)
            for (size_t j = i + 1; j < parts.size(); ++j) CHECK(!overlaps(parts[i], parts[j]));
        // Left from its pause menu instead: nothing either.
        auto again = makeTutorialReplay(g);
        GameScene* game2 = static_cast<GameScene*>(again.get());
        r.app.switchScene(std::move(again), 0.0f);
        r.app.runFrames(10);
        game2->quitToMenu();
        r.app.runFrames(30);
        CHECK(g.progression.tutorial == state);
        CHECK(g.progressCopyBody() == before);
    }
}
