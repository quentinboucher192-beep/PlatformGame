// No campaign level can be skipped: the shortcut detector (game/levels/LevelAnalyzer) finds
// no bypassed checkpoint, no closed door that can be walked around and no route much shorter
// than the intended one, with the abilities the player has in each level.

#include <cstdio>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelAnalyzer.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("levels: no shortcut in the campaign (shortcut detector)") {
    ContentDatabase content;
    content.reload();
    int levels = 0;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        CHECK(level.has_value());
        if (!level) continue;
        ++levels;
        LevelAnalysis a = analyzeLevel(*level, content);
        for (const auto& p : a.problems) std::printf("  %s : %s\n", e->id.c_str(), p.c_str());
        CHECK(a.reachable);
        CHECK(a.ok());
        bool oneLife = level->timerMode == TimerMode::Challenge && level->challengeMaxDeaths == 0;  // the boss rush
        CHECK(!a.checkpoints.empty() || oneLife);
    }
    CHECK(levels >= 9);
}

TEST_CASE("levels: the detector finds a route over a wall that does not reach the ceiling") {
    ContentDatabase content;
    content.reload();
    LevelData l = LevelData::makeEmpty("shortcut_probe", "Sonde", "sylvanox");
    l.bounds = {0, 0, 2400, 800};
    l.objects.clear();
    auto add = [&](const std::string& type, Rect r, Json props = Json::object()) {
        LevelObject o;
        o.id = type + std::to_string(l.objects.size());
        o.type = type;
        o.layer = "gameplay";
        o.rect = r;
        o.props = props;
        l.objects.push_back(o);
    };
    add("solid", {0, 700, 2400, 100});                   // floor
    add("player_start", {100, 678, 10, 22});
    add("exit", {2200, 636, 40, 64});
    // The intended way goes down a pit and up again; the wall between is short.
    add("solid", {1000, 300, 64, 400});                  // wall (top at 300)
    add("checkpoint", {600, 652, 24, 48}, {{"label", "avant"}});
    add("checkpoint", {1032, 652 + 400, 24, 48}, {{"label", "fond du puits"}});  // outside: unreachable
    LevelAnalysis a = analyzeLevel(l, content);
    CHECK(a.reachable);
    // The route goes over the wall (it does not reach the ceiling).
    bool over = false;
    for (const auto& p : a.route) over |= p.x > 1000 && p.x < 1064 && p.y <= 300;
    CHECK(over);
    // Up to the ceiling: no way over any more.
    l.objects[3].rect = {1000, -kCeilingMargin, 64, 700 + kCeilingMargin};
    LevelAnalysis b = analyzeLevel(l, content);
    CHECK(!b.reachable);
}
