// 2.84: what is new shows again. SUCCÈS lists the achievements unlocked since the last visit (kept in the save
// as « unseen », seen once the screen is left; a save of before counts all as seen) in a banner that fits; the
// main menu counts them on « Succès ». Statistiques offers every level of the campaign (planet after planet) and
// every boss. The Cosmic kit's diamonds show « NOUVEAU » in its pill again.

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/CosmicStyle2.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

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
        g->settings.language = i18n::code(lang);
        g->applySettings();
    }
    ~Rig() {
        g->online.shutdown();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
};

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

TEST_CASE("achievements (2.84): the unlocks not seen in SUCCÈS yet, kept in the save, an older save has seen all") {
    ContentDatabase content;
    content.reload();
    AchievementCatalog catalog;
    std::string err;
    catalog.loadDefault(&err);
    Progression progression;
    RecordsManager records;
    BossStats bosses;
    const fs::path file = fs::temp_directory_path() / ("cosmic_unseen_" + makeUuidV4().substr(0, 8) + ".json");
    AchievementBook book{file};
    AchievementInputs in;
    in.content = &content;
    in.progression = &progression;
    in.records = &records;
    in.bosses = &bosses;
    in.stats = &book.stats();
    book.stats().add("kills", 120);
    const auto got = book.evaluate(catalog, in, "2026-11-21 10:00");
    CHECK(!got.empty());
    CHECK(book.unseenCount() == static_cast<int>(got.size()));
    for (const AchievementDef* a : got) CHECK(book.isUnseen(a->id));
    CHECK(book.unseen().front() == got.front()->id);  // oldest first
    // Kept in the save, and read back.
    CHECK(book.save());
    AchievementBook again{file};
    again.load();
    CHECK(again.unseen() == book.unseen());
    // Seen: gone, saved so.
    again.markSeen(got.front()->id);
    CHECK(!again.isUnseen(got.front()->id));
    CHECK(again.unseenCount() == book.unseenCount() - 1);
    // A save of before 2.84 (no « unseen »): everything it unlocked has been seen; a damaged list keeps only unlocked ids.
    Json j = book.toJson();
    j.erase("unseen");
    AchievementBook old;
    old.fromJson(j);
    CHECK(old.unlocked(got.front()->id));
    CHECK(old.unseenCount() == 0);
    j["unseen"] = Json::array({"not_an_achievement", got.front()->id, got.front()->id, 42});
    old.fromJson(j);
    CHECK(old.unseenCount() == 1);
    // Unlocked again later: never twice in the list.
    CHECK(book.evaluate(catalog, in, "2026-11-21 10:05").empty());
    CHECK(book.unseenCount() == static_cast<int>(got.size()));
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("achievements (2.84): SUCCÈS shows what is new since the last visit, fits, and sees it once left") {
    for (auto [w, h] : {std::pair{1280, 720}, std::pair{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            Rig rig(w, h, lang);
            GameServices& g = *rig.g;
            const auto& all = g.achievementCatalog.achievements();
            CHECK(all.size() > 20);
            if (all.size() <= 20) return;
            // Twenty new unlocks: the banner shows what fits, « +K » for the rest.
            const AchievementBook keep = g.achievements;
            Json j = g.achievements.toJson();
            Json unseen = Json::array();
            for (size_t i = 0; i < 20; ++i) {
                j["unlocked"][all[i].id] = "2026-11-21 10:00";
                unseen.push_back(all[i].id);
            }
            j["unseen"] = unseen;
            g.achievements.fromJson(j);
            CHECK(g.achievements.unseenCount() >= 20);
            rig.app.pushScene(makeMainMenu(g));
            rig.app.runFrames(3);
            rig.app.switchScene(makeAchievements(g), 0.0f);
            rig.app.runFrames(6);
            Scene* s = rig.app.topScene();
            const std::vector<Rect> pieces = achievementsFreshPieces(s);
            CHECK(pieces.size() >= 4);  // its frame, « Voir », the title, at least one name
            if (pieces.size() >= 4) {
                const Rect banner = pieces[0];
                CHECK(banner.right() <= static_cast<float>(w) && banner.x >= 0.0f);
                for (size_t a = 1; a < pieces.size(); ++a) {
                    CHECK(banner.containsRect(pieces[a]));
                    for (size_t b = a + 1; b < pieces.size(); ++b) CHECK(!pieces[a].intersects(pieces[b]));
                }
            }
            // The first new one is chosen and focused; « Voir les nouveaux » and its name are keyboard stops.
            CHECK(g.ui.focused("ach/n0"));
            CHECK(g.ui.navigable("ach/fresh/see"));
            CHECK(g.ui.navigable("ach/fresh/c0"));
            CHECK(g.achievements.unseenCount() >= 20);  // still new while on the screen
            // Left: seen (and saved).
            rig.app.switchScene(makeMainMenu(g), 0.0f);
            rig.app.runFrames(4);
            CHECK(g.achievements.unseenCount() == 0);
            g.achievements = keep;
            g.achievements.save();
        }
}

TEST_CASE("statistics (2.84): every level of the campaign and every boss with statistics are offered") {
    Rig rig(1280, 720, Language::French);
    GameServices& g = *rig.g;
    const auto groups = statisticsLevelGroups(g, [](const std::string&) { return false; });
    std::set<std::string> offered;
    for (const auto& grp : groups)
        for (const auto& l : grp.levels) offered.insert(l);
    int campaign = 0;
    for (const PlanetData* p : g.content.campaignPlanets())
        for (const LevelEntry* l : g.content.levelsForPlanet(p->id)) {
            ++campaign;
            if (!offered.count(l->id)) std::fprintf(stderr, "statistics: %s missing\n", l->id.c_str());
            CHECK(offered.count(l->id) == 1);
        }
    std::fprintf(stderr, "statistics: %d campaign levels\n", campaign);
    CHECK(campaign > 20);
    CHECK(groups.size() == g.content.campaignPlanets().size());  // no « Autres niveaux » without other data
    for (const char* planet : {"aethra", "noyau", "confins"}) {
        bool found = false;
        for (const auto& grp : groups) found = found || grp.planetId.find(planet) != std::string::npos;
        CHECK(found);
    }
    // A level with statistics outside the campaign: under « Autres niveaux ».
    const auto withOther = statisticsLevelGroups(g, [](const std::string& id) { return id == "atelier_test"; });
    CHECK(withOther.size() == groups.size());  // no statistics for it: nothing more
    // Every boss of the content and of the older boss statistics.
    const auto bosses = statisticsBosses(g);
    const std::set<std::string> bs(bosses.begin(), bosses.end());
    CHECK(bs.size() == bosses.size());
    int count = 0;
    for (const EnemyDefinition* d : g.content.enemies())
        if (d->isBoss && !d->tutorial) {
            ++count;
            CHECK(bs.count(d->id) == 1);
        }
    CHECK(count > 10);
    for (const auto& [id, rec] : g.bossStats.all()) CHECK(bs.count(id) == 1);
    // The screen draws its Niveaux, Passages difficiles and Boss tabs with them.
    for (int tab : {2, 3, 4}) {
        rig.app.pushScene(makeStatistics(g, tab));
        rig.app.runFrames(4);
        CHECK(rig.app.sceneCount() == 1);
        CHECK(g.ui.navigable(tab == 2 ? "stats/pb0" : tab == 3 ? "stats/runsplanet0" : "stats/slicesplanet0"));
        rig.app.popScene();
        rig.app.runFrames(1);
    }
}

TEST_CASE("cosmic kit (2.84): « NOUVEAU » in its pill on the diamonds, the plates and the main menu again") {
    Rig rig(1280, 720, Language::French);
    GameServices& g = *rig.g;
    UIContext& ui = g.ui;
    auto before = ui.isNewFeature;
    cstyle::Anims anims;
    int pills = -1;
    bool reported = false;
    ui.isNewFeature = [](const std::string& f) { return f == "test.new"; };
    rig.app.pushScene(std::make_unique<Probe>(g, [&](UIContext& u) {
        cstyle::Diamond d{"stats", 0, false, "test.new"};
        cstyle::diamondButton(u, anims, "dia", {200, 200}, 28.0f, d, 1.0f, u.time(), 1.0f / 30.0f, &reported);
        const int afterDiamond = u.newPillsDrawn();
        cstyle::PlateEx p;
        p.plate = {"Niveaux en ligne", "", "campaign"};
        p.plate.feature = "test.new";
        cstyle::plateButtonEx(u, anims, "plate", {400, 300, 400, 56}, p, 0.9f, 1.0f, u.time(), 1.0f / 30.0f, cstyle::Motion{});
        pills = afterDiamond * 10 + (u.newPillsDrawn() - afterDiamond);
    }));
    rig.app.runFrames(3);
    CHECK(reported);
    CHECK(pills == 11);  // one on the diamond, one on the plate
    rig.app.popScene();
    rig.app.runFrames(1);
    // The main menu: « Succès » (a diamond) shows its pill when something under it is new.
    int menuPills[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        ui.isNewFeature = [k](const std::string& f) { return k == 1 && f == "menu.achievements"; };
        rig.app.pushScene(makeMainMenu(g));
        rig.app.runFrames(40);  // the icon row in
        menuPills[k] = ui.newPillsDrawn();
        rig.app.popScene();
        rig.app.runFrames(1);
    }
    CHECK(menuPills[1] == menuPills[0] + 1);
    ui.isNewFeature = before;
}
