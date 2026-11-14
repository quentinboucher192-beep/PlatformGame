// The power guide (help) shows every power of the player working: each scripted demo
// reaches its goal before its loop restarts, and every power of the game has a demo.

#include <cstdio>
#include <cstdlib>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/powers/PowerDemos.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("power guide: every power of the game has a demo") {
    ContentDatabase content;
    content.reload();
    std::set<std::string> demos;
    for (const auto& d : powerDemos()) demos.insert(d.power);
    for (const PowerDefinition* p : content.powers()) {
        if (!demos.count(p->id)) std::printf("  pouvoir sans démo : %s\n", p->id.c_str());
        CHECK(demos.count(p->id) == 1);
    }
}

TEST_CASE("power guide: each demo really shows its power working") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    int failed = 0;
    for (const auto& d : powerDemos()) {
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        GameWorld w(s, WorldMode::Arena);
        w.silent = true;
        w.load(powerDemoLevel(d));
        int reached = -1;
        for (int step = 0; step < d.period && reached < 0; ++step) {
            w.fixedUpdate(d.script(step), 1.0f / 120.0f);
            if (d.success(w)) reached = step;
            if (const char* t = std::getenv("COSMIC_DEMO_TRACE"); t && d.power == t && step % 4 == 0)
                std::printf("    %3d x %.0f y %.0f vy %.0f sol %d\n", step, w.player().box.center().x, w.player().box.bottom(), w.player().vel.y, w.player().grounded() ? 1 : 0);
        }
        if (reached < 0)
            std::printf("    fin : joueur en (%.0f, %.0f), au sol %d, éliminations %d, éclats %d\n", w.player().box.center().x,
                        w.player().box.bottom(), w.player().grounded() ? 1 : 0, w.stats().kills, w.stats().shards);
        std::printf("  %-13s %s (pas %d / %d)\n", d.power.c_str(), reached >= 0 ? "réussie" : "RATÉE", reached, d.period);
        failed += reached < 0 ? 1 : 0;
    }
    CHECK(failed == 0);
}
