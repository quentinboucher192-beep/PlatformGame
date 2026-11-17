// « Histoire des giga-boss » (2.51): before the first fight against a giga boss (8 phases or
// more, never an echo), its story is told over the run, which waits for it: not a step is
// simulated, not an input recorded (the replays, records and rankings never see it). Told when
// it has not been seen (a story skipped counts), never in a boss training, an editor test or an
// automated run; « seen » is kept with the boss statistics and united by the progress merge.

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <set>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/ProgressMerge.h"
#include "game/player/Player.h"
#include "game/records/BossStats.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"

using namespace cosmic;

namespace {

/// Width and height of a PNG file (its IHDR chunk), {0, 0} when it is not one.
std::pair<int, int> pngSize(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    unsigned char h[24] = {};
    if (!f.read(reinterpret_cast<char*>(h), sizeof h)) return {0, 0};
    if (h[0] != 0x89 || h[1] != 'P' || h[2] != 'N' || h[3] != 'G') return {0, 0};
    auto be = [&](int i) { return (h[i] << 24) | (h[i + 1] << 16) | (h[i + 2] << 8) | h[i + 3]; };
    return {be(16), be(20)};
}

}  // namespace

TEST_CASE("boss stories: every giga boss of the game has its story of six lines, long enough to read") {
    ContentDatabase content;
    content.reload();
    BossStories stories;
    std::string err;
    CHECK(stories.loadDefault(&err));
    CHECK(err.empty());
    std::set<std::string> giga;
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->builtin || !d->isBoss || !d->variantOf.empty() || d->phases.size() < 8) continue;
        CHECK(isGigaBoss(*d));
        giga.insert(d->id);
        const BossStory* s = stories.find(d->id);
        CHECK(s != nullptr);
        if (!s) continue;
        CHECK(s->shots.size() == 6);
        CHECK(!s->title.empty());
        CHECK(story::duration(*s) >= 15.0f);
        for (const StoryShot& shot : s->shots) {
            CHECK(!shot.text.empty() && !shot.scene.empty());
            CHECK(story::shotDuration(shot.text) >= story::kMinShot);
            // Its painting: in the game's assets, 16:9.
            auto [w, h] = pngSize(Paths::assetsDir() / shot.image);
            CHECK(w > 0 && h > 0);
            CHECK(w * 9 == h * 16);
        }
    }
    CHECK(giga.count("singularis") && giga.count("pyrrhax") && giga.count("monolithe") && giga.count("kraken"));  // 2.53: the Kraken
    CHECK(giga.count("phenix"));  // 2.56: the Phénix
    // Every story is a giga boss's (the echoes and the bosses of three phases have none).
    for (const auto& [id, s] : stories.all()) {
        const EnemyDefinition* d = content.enemy(id);
        CHECK(d != nullptr && isGigaBoss(*d));
    }
    // The texts as written (the first and last lines of each).
    CHECK(stories.find("singularis")->shots[0].text ==
          "Il y a très longtemps, au centre de Kepler-Voss, brillait une toute jeune étoile. Elle s'appelait Lyse.");
    CHECK(stories.find("pyrrhax")->shots[5].text == "Depuis, il garde la dernière frontière. Plus personne ne se moquera jamais de son feu.");
    CHECK(stories.find("monolithe")->shots[1].text ==
          "Les premiers enfants des Confins le trouvèrent dans une caverne et y gravèrent une rune, pour jouer : « Protège-nous ».");
    CHECK(stories.find("kraken")->shots[1].text ==
          "Dans le noir, les poissons perdus suivaient sa lueur jusqu'aux sources chaudes. On l'appelait « la Lanterne ».");
    CHECK(stories.find("kraken")->shots[5].text ==
          "Le Kraken allume toujours ses lueurs au fond de la Fosse. Il attend qu'on le suive, comme autrefois.");
    CHECK(stories.find("pyrrhax")->title == "Le Dragon d'Obsidienne");
    CHECK(stories.find("kraken")->title == "La Lanterne des profondeurs");  // its name on the card: « LE KRAKEN DES ABYSSES »
    CHECK(stories.find("phenix")->shots[0].text ==
          "Autrefois, chaque matin, un petit oiseau de lumière quittait l'étoile du centre pour porter l'aube aux Confins.");
    CHECK(stories.find("phenix")->shots[5].text ==
          "Depuis, le Phénix se consume chaque matin dans son nid. Il ne sait plus s'arrêter, de peur que la nuit gagne.");
    CHECK(stories.find("phenix")->title == "Le Porteur d'aube");  // its name on the card: « LE PHÉNIX STELLAIRE »
    // Time to read: max(4.5 s, 1.2 s + characters / 16); 35 to 50 s with the title card.
    CHECK_NEAR(story::shotDuration("court"), 4.5f, 1e-4f);
    CHECK_NEAR(story::shotDuration(std::string(160, 'a')), 11.2f, 1e-4f);
    CHECK(story::characters("éàü") == 3);
    for (const char* id : {"singularis", "pyrrhax", "monolithe", "kraken", "phenix"}) {
        float t = story::duration(*stories.find(id));
        CHECK(t >= 35.0f && t <= 50.0f);
    }
}

TEST_CASE("boss stories: told when not seen, for giga bosses only, never in a boss training") {
    ContentDatabase content;
    content.reload();
    BossStories stories;
    CHECK(stories.loadDefault());
    const EnemyDefinition* pyrrhax = content.enemy("pyrrhax");
    const EnemyDefinition* thornwarden = content.enemy("thornwarden");
    const EnemyDefinition* echo = content.enemy("echo_thornwarden");
    CHECK(pyrrhax && thornwarden && echo);
    BossStats stats;
    StoryContext run;
    const int before = static_cast<int>(StorySetting::BeforeFirstFight), never = static_cast<int>(StorySetting::Never);
    CHECK(storyDue(*pyrrhax, stories, stats, before, run));
    // Not a giga boss: a boss of three phases, an echo (even of a giga boss).
    CHECK(!storyDue(*thornwarden, stories, stats, before, run));
    EnemyDefinition echoOfGiga = *pyrrhax;
    echoOfGiga.variantOf = "pyrrhax";
    CHECK(!isGigaBoss(echoOfGiga));
    CHECK(!storyDue(echoOfGiga, stories, stats, before, run));
    CHECK(!isGigaBoss(*echo));
    EnemyDefinition seven = *pyrrhax;
    seven.phases.resize(7);
    CHECK(!isGigaBoss(seven));
    // A giga boss without a story (made in the enemy editor): nothing to tell.
    EnemyDefinition homemade = *pyrrhax;
    homemade.id = "my_giga_boss";
    CHECK(isGigaBoss(homemade));
    CHECK(!storyDue(homemade, stories, stats, before, run));
    // The setting « Jamais ».
    CHECK(!storyDue(*pyrrhax, stories, stats, never, run));
    // Never in a boss training, an editor test, an automated run.
    StoryContext training;
    training.bossTraining = true;
    CHECK(!storyDue(*pyrrhax, stories, stats, before, training));
    StoryContext editor;
    editor.editorTest = true;
    CHECK(!storyDue(*pyrrhax, stories, stats, before, editor));
    StoryContext scripted;
    scripted.scripted = true;
    CHECK(!storyDue(*pyrrhax, stories, stats, before, scripted));
    // Once seen (or skipped), never again; the other giga bosses still have theirs.
    stats.markStorySeen("pyrrhax");
    CHECK(stats.dirty());
    CHECK(!storyDue(*pyrrhax, stories, stats, before, run));
    CHECK(storyDue(*content.enemy("monolithe"), stories, stats, before, run));
}

TEST_CASE("boss stories: « seen » kept with the boss statistics, united by the progress merge, setting saved") {
    BossStats a;
    a.at("pyrrhax").fights = 2;
    a.markStorySeen("pyrrhax");
    BossStats copy;
    copy.fromJson(a.toJson());
    CHECK(copy.storySeen("pyrrhax"));
    CHECK(!copy.storySeen("singularis"));
    CHECK(copy.find("pyrrhax") && copy.find("pyrrhax")->fights == 2);
    // A save written before 2.51 (no list): nothing seen, nothing lost.
    BossStats old;
    old.fromJson(Json{{"bosses", {{"pyrrhax", BossRecord{}.toJson()}}}});
    CHECK(old.storiesSeen().empty() && old.find("pyrrhax"));
    // « Migrer vers un nouveau compte »: the stories seen on either side.
    BossStats b;
    b.markStorySeen("monolithe");
    b.markStorySeen("pyrrhax");
    BossStats merged;
    merged.fromJson(mergeBossStatsJson(a.toJson(), b.toJson()));
    CHECK(merged.storySeen("pyrrhax") && merged.storySeen("monolithe") && !merged.storySeen("singularis"));
    CHECK(merged.storiesSeen().size() == 2);
    BossStats mine;
    mine.fromJson(mergeBossStatsJson(a.toJson(), Json::object()));
    CHECK(mine.storySeen("pyrrhax"));
    // Paramètres › Jeu › « Histoire des giga-boss »: before the first fight by default.
    Settings s;
    CHECK(s.gameplay.bossStories == 0);
    s.gameplay.bossStories = 1;
    CHECK(Settings::fromJson(s.toJson()).gameplay.bossStories == 1);
    Json bad = s.toJson();
    bad["gameplay"]["bossStories"] = 7;
    CHECK(Settings::fromJson(bad).gameplay.bossStories == 1);
}

// The run in the real game loop (headless): Pyrrhax's fight starts, its story is told over the
// run, and for as long as it lasts nothing of the run moves — its time, its timer, the player,
// the boss. Skipped (Space), the fight starts where it was; the story counts as seen.
TEST_CASE("boss stories: the run waits while the story is told (headless game loop)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 960;
    cfg.height = 540;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        CHECK(!g.bossStats.storySeen("pyrrhax"));
        PlayOptions opt;
        opt.spawn = Vec2{64.0f, 640.0f};  // Les Confins 1, its start (no intro)
        std::unique_ptr<Scene> scene = makeGameScene(g, "confins_01", opt);
        GameScene* game = static_cast<GameScene*>(scene.get());
        app.pushScene(std::move(scene));
        app.runFrames(20);
        GameWorld* w = game->world();
        CHECK(w != nullptr);
        if (w) {
            const float t0 = w->time();
            CHECK(t0 > 0.0f);
            CHECK(game->storyBoss().empty());
            // The arena wakes Pyrrhax (its object in the level).
            LevelEntity* spawn = w->entity("o86");
            CHECK(spawn != nullptr);
            if (spawn) spawn->handleAction(*w, "activate", Json::object());
            app.runFrames(3);
            CHECK(w->activeBoss() != nullptr);
            CHECK(game->storyBoss() == "pyrrhax");
            CHECK(app.sceneCount() == 2);  // the story over the run
            CHECK(g.bossStats.storySeen("pyrrhax"));
            const float time = w->time(), timer = w->timer().elapsed();
            const Vec2 player = w->player().box.pos();
            const float bossHp = w->activeBoss() ? w->activeBoss()->hp() : -1.0f;
            const Vec2 boss = w->activeBoss() ? w->activeBoss()->box.pos() : Vec2{};
            app.runFrames(300);  // five seconds of story
            CHECK(app.sceneCount() == 2);
            CHECK(w->time() == time);
            CHECK(w->timer().elapsed() == timer);
            CHECK(w->player().box.pos().x == player.x && w->player().box.pos().y == player.y);
            CHECK(w->activeBoss() && w->activeBoss()->hp() == bossHp);
            CHECK(w->activeBoss() && w->activeBoss()->box.pos().x == boss.x && w->activeBoss()->box.pos().y == boss.y);
            // Skipped: a short fade, then the fight goes on from where it was.
            SDL_Event down{};
            down.type = SDL_EVENT_KEY_DOWN;
            down.key.scancode = SDL_SCANCODE_SPACE;
            down.key.key = SDLK_SPACE;
            down.key.down = true;
            SDL_PushEvent(&down);
            app.runFrames(2);
            SDL_Event up = down;
            up.type = SDL_EVENT_KEY_UP;
            up.key.down = false;
            SDL_PushEvent(&up);
            app.runFrames(30);
            CHECK(app.sceneCount() == 1);
            CHECK(game->storyBoss().empty());
            app.runFrames(60);
            CHECK(w->time() > time + 0.8f);
            CHECK(w->timer().elapsed() > timer);
        }
        // The narration never reaches « Espace · Ⓐ Passer »: every line of every story (the
        // longest ones included), in both languages, at the smallest window (960x540) and at
        // 1600x900, stays in two lines at most, left of the measured hint, inside the band.
        for (Vec2 size : {Vec2{960, 540}, Vec2{1600, 900}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            for (Language lang : {Language::French, Language::English}) {
                i18n::setLanguage(lang);
                app.runFrames(1);
                g.ui.begin(app, 1.0f / 60.0f);
                CHECK(std::fabs(g.ui.screenW() - size.x) < 1.0f && std::fabs(g.ui.screenH() - size.y) < 1.0f);
                size_t longest = 0;
                for (const auto& [id, st] : g.stories.all())
                    for (const StoryShot& shot : st.shots) {
                        const std::string shown = i18n::tr(shot.text);
                        longest = std::max(longest, story::characters(shown));
                        const StoryNarrationLayout lay = layoutStoryNarration(g.ui, shown);
                        CHECK(!lay.lines.empty() && lay.lines.size() <= kStoryNarrationLines);
                        CHECK(lay.area.right() < lay.hint.x);
                        CHECK(lay.hint.right() <= g.ui.screenW());
                        CHECK(lay.px >= 12.0f);
                        CHECK(lay.top >= lay.area.y - 0.5f);
                        CHECK(lay.top + lay.lineH * static_cast<float>(lay.lines.size()) <= lay.area.bottom() + 0.5f);
                        i18n::Verbatim asShown;
                        for (const auto& l : lay.lines) CHECK(g.ui.text().measure(l, FontFace::Body, lay.px).x <= lay.area.w + 0.5f);
                    }
                CHECK(longest > 100);  // the longest lines were among them
            }
        }
        i18n::setLanguage(Language::French);
        app.shutdown();  // the scenes first (they use the services)
    }
}
