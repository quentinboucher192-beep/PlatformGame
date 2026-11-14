// Power slots: a level carries at most N powers (4 by default); a tablet touched with
// every slot taken waits for the player's choice (replace a power or leave the new one),
// which travels in the input frames so the replay reproduces it exactly.

#include <cstdio>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    World() {
        content.reload();
        res.init(nullptr, &audio);
    }
    GameWorld& make(const LevelData& level) {
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(level);
        w->startAt({100, 800});
        return *w;
    }
};

LevelData level(std::vector<std::string> start, int maxPowers, const std::string& tablet) {
    LevelData l = LevelData::makeEmpty("test_slots", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(start);
    l.maxPowers = maxPowers;
    LevelObject o;
    o.id = "tablet";
    o.type = "power_unlock";
    o.layer = "gameplay";
    o.rect = {260, 776, 24, 24};
    o.props = {{"power", tablet}};
    l.objects.push_back(o);
    return l;
}

InputFrame move(int dir) {
    InputFrame f;
    int a = static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft);
    f.held = static_cast<uint16_t>(1u << a);
    f.moveX = static_cast<int8_t>(dir > 0 ? 127 : -127);
    return f;
}

/// Walks right until the tablet asks for a choice (or `steps` frames).
bool walkToOffer(GameWorld& w, std::vector<InputFrame>* log = nullptr, int steps = 400) {
    for (int i = 0; i < steps && !w.powerOffer(); ++i) {
        InputFrame f = move(+1);
        if (log) log->push_back(f);
        stepWorld(w, f, kSimDt);
    }
    return w.powerOffer().has_value();
}

PowerUnlockEntity* tablet(GameWorld& w) { return dynamic_cast<PowerUnlockEntity*>(w.entity("tablet")); }
}  // namespace

TEST_CASE("power slots: 4 by default, and never fewer than the start powers") {
    LevelData l = LevelData::makeEmpty("x", "x", "sylvanox");
    CHECK(l.maxPowers == 4);
    {
        Json old = l.toJson();
        old.erase("maxPowers");  // levels saved before power slots
        auto parsed = LevelData::fromJson(old);
        CHECK(parsed.has_value());
        if (parsed) CHECK(parsed->maxPowers == 4);
    }
    l.startPowers = {"dash", "wave", "shield", "impulse", "teleport", "glide"};
    CHECK(l.powerSlots() == 6);
    l.maxPowers = 0;
    CHECK(l.powerSlots() == 0);
    l.maxPowers = 2;
    Json j = l.toJson();
    CHECK(j["maxPowers"].get<int>() == 2);
    CHECK(LevelData::fromJson(j)->maxPowers == 2);
}

TEST_CASE("power slots: a free slot takes the new power at once") {
    World env;
    GameWorld& w = env.make(level({"dash"}, 4, "shield"));
    CHECK(w.player().powerSlots() == 4);
    CHECK(!walkToOffer(w, nullptr, 200));
    CHECK(w.player().hasPower("shield"));
    CHECK(w.player().powers().size() == 2);
}

TEST_CASE("power slots: full slots wait for the choice, replace leaves the old power on the tablet") {
    World env;
    GameWorld& w = env.make(level({"dash", "wave"}, 2, "shield"));
    CHECK(walkToOffer(w));
    CHECK(w.powerOffer()->powerId == "shield");
    CHECK(!w.player().hasPower("shield"));
    // Without an answer nothing moves.
    float t = w.time();
    Rect box = w.player().box;
    for (int i = 0; i < 60; ++i) stepWorld(w, move(+1), kSimDt);
    CHECK(w.time() == t);
    CHECK(w.player().box.x == box.x);
    CHECK(w.powerOffer().has_value());
    // Replace the first power (dash).
    InputFrame f;
    f.choice = 1;
    stepWorld(w, f, kSimDt);
    CHECK(!w.powerOffer());
    CHECK(w.player().hasPower("shield"));
    CHECK(w.player().hasPower("wave"));
    CHECK(!w.player().hasPower("dash"));
    CHECK(w.player().powers().size() == 2);
    CHECK(w.time() > t);
    // Nothing is lost: the tablet now holds the dash.
    CHECK(tablet(w) != nullptr);
    CHECK(!tablet(w)->removed);
    CHECK(tablet(w)->power() == "dash");
}

TEST_CASE("power slots: leaving keeps the carried powers and the tablet") {
    World env;
    GameWorld& w = env.make(level({"dash", "wave"}, 2, "shield"));
    CHECK(walkToOffer(w));
    InputFrame f;
    f.choice = InputFrame::kChoiceLeave;
    stepWorld(w, f, kSimDt);
    CHECK(!w.powerOffer());
    CHECK(w.player().hasPower("dash"));
    CHECK(w.player().hasPower("wave"));
    CHECK(!w.player().hasPower("shield"));
    CHECK(tablet(w)->power() == "shield");
    // Standing on it does not ask again; coming back does.
    for (int i = 0; i < 30; ++i) stepWorld(w, InputFrame{}, kSimDt);
    CHECK(!w.powerOffer());
    for (int i = 0; i < 90; ++i) stepWorld(w, move(-1), kSimDt);
    CHECK(walkToOffer(w));
}

TEST_CASE("power slots: the choice is part of the replay and plays back identically") {
    std::vector<InputFrame> frames;
    uint64_t live = 0;
    {
        World env;
        GameWorld& w = env.make(level({"dash", "wave"}, 2, "shield"));
        w.seedGameplay(levelSeed("test_slots"));
        CHECK(walkToOffer(w, &frames));
        InputFrame f = move(+1);
        f.choice = 2;  // replace the wave
        frames.push_back(f);
        stepWorld(w, f, kSimDt);
        for (int i = 0; i < 60; ++i) {
            frames.push_back(move(+1));
            stepWorld(w, frames.back(), kSimDt);
        }
        CHECK(w.player().hasPower("shield"));
        CHECK(!w.player().hasPower("wave"));
        live = simulationStateHash(w);
    }
    ReplayData r;
    r.levelId = "test_slots";
    r.levelHash = std::string(64, 'a');
    r.attemptId = makeUuidV4();
    r.inputs = frames;
    r.durationMs = 1000;
    r.frames.push_back(ReplayFrame{});  // the trace needs at least one sample
    std::string err;
    auto parsed = ReplayData::parse(r.serialize(), &err);
    if (!parsed) std::printf("  replay: %s\n", err.c_str());
    CHECK(parsed.has_value());
    if (!parsed) return;
    CHECK(parsed->inputs == frames);
    World env;
    GameWorld& w = env.make(level({"dash", "wave"}, 2, "shield"));
    w.seedGameplay(levelSeed("test_slots"));
    for (const auto& f : parsed->inputs) stepWorld(w, f, kSimDt);
    CHECK(simulationStateHash(w) == live);
    CHECK(tablet(w)->power() == "wave");
}

TEST_CASE("power slots: campaign levels carry 4 powers, stations stand on solid ground with room above") {
    ContentDatabase content;
    content.reload();
    int stations = 0;
    for (const PlanetData* planet : content.planets())
        for (const LevelEntry* e : content.levelsForPlanet(planet->id)) {
            auto l = content.loadLevel(e->id);
            CHECK(l.has_value());
            if (!l) continue;
            CHECK(l->maxPowers == 4);
            CHECK(static_cast<int>(l->startPowers.size()) <= 4);
            std::vector<Rect> solids;
            for (const auto& o : l->objects)
                if (o.type == "solid") solids.push_back(o.rect);
            for (const auto& o : l->objects) {
                if (o.type != "power_unlock" || o.id.rfind("station_", 0) != 0) continue;
                ++stations;
                CHECK(content.power(o.propStr("power")) != nullptr);
                Vec2 c = o.rect.center();
                float feet = c.y + 66.0f;
                Rect standing{c.x - 5.0f, feet - 22.0f, 10.0f, 22.0f};
                bool clear = true, floor = false;
                for (const Rect& s : solids) {
                    clear = clear && !s.intersects(o.rect) && !s.intersects(standing);
                    floor = floor || (c.x >= s.x && c.x <= s.right() && std::abs(s.y - feet) <= 10.0f);
                }
                if (!clear || !floor) std::printf("  %s %s : %s%s\n", e->id.c_str(), o.id.c_str(), clear ? "" : "dans le décor ", floor ? "" : "sans sol");
                CHECK(clear);
                CHECK(floor);
            }
        }
    CHECK(stations >= 20);
}
