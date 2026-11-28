// Simulation 20 (kSlideInWallsSimulation): the slide's hitbox is wider than the standing one; Nova
// flush against a closed door who slid found herself a little inside it, and the collisions only
// stop a wall ahead of the box: she went through it (the TAS of Les Ruines Suspendues left the
// arena of Thornwarden that way, the boss alive). Now the wider box is moved off the wall, and out
// of a slide under a low ceiling the standing box is taken only where there is room for it; a run
// of before plays with its rules.

#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

constexpr float kDoorX = 600.0f;

struct DoorWorld {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit DoorWorld(int simVersion) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_slide_walls", "Test", "sylvanox");
        l.intro.steps.clear();
        LevelObject door;
        door.id = "door";
        door.type = "door";
        door.layer = "gameplay";
        door.rect = {kDoorX, 672, 16, 128};  // on the ground (y 800), closed
        door.props = {{"open", false}};
        l.objects.push_back(door);
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->setSimulationVersion(simVersion);
        w->load(l);
        for (int i = 0; i < 60; ++i) step({});
    }
    void step(const PlayerInput& in) { w->fixedUpdate(in, 1.0f / 120.0f); }
    Player& p() { return w->player(); }
};

PlayerInput runRight(bool down, bool downPressed) {
    PlayerInput in;
    in.move = {1.0f, down ? 1.0f : 0.0f};
    in.held[static_cast<int>(Action::MoveRight)] = true;
    if (down) in.held[static_cast<int>(Action::MoveDown)] = true;
    if (downPressed) in.pressed[static_cast<int>(Action::MoveDown)] = true;
    return in;
}

// Nova running at full speed, her box 0.3 px from the door, then Down (a slide), then Right held.
float slideIntoDoor(int simVersion, bool& slid) {
    DoorWorld dw(simVersion);
    Player& p = dw.p();
    CHECK(p.grounded());
    p.teleportTo({kDoorX - 0.3f - p.box.w * 0.5f, 800.0f});
    p.vel.x = p.config().maxRunSpeed;
    dw.step(runRight(true, true));
    slid = p.state() == PlayerState::Slide;
    for (int i = 0; i < 90; ++i) dw.step(runRight(i < 30, false));
    return p.box.x;
}

}  // namespace

TEST_CASE("slide: flush against a closed door, a slide no longer goes through it (simulation 20)") {
    bool slid = false;
    const float x = slideIntoDoor(kSimulationVersion, slid);
    CHECK(slid);                // the slide is still allowed there (room behind Nova)
    CHECK(x + 10.0f < kDoorX + 0.01f);  // ... and the door stops it
}

TEST_CASE("slide: a run of before plays with its rules (simulation 19 went through the door)") {
    bool slid = false;
    const float x = slideIntoDoor(kSlideInWallsSimulation - 1, slid);
    CHECK(slid);
    CHECK(x > kDoorX + 16.0f);  // through the closed door: the bug a replay of before keeps
}

namespace {

// A low ceiling over the floor (y 800): 16 px of room, the slide's box (11 high) passes under it,
// the standing one (22) does not.
constexpr float kCeilingTop = 740.0f, kCeilingBottom = 784.0f;

struct CeilingWorld {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit CeilingWorld(int simVersion) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_slide_ceiling", "Test", "sylvanox");
        l.intro.steps.clear();
        LevelObject c;
        c.id = "plafond";
        c.type = "solid";
        c.layer = "terrain";
        c.rect = {500, kCeilingTop, 300, kCeilingBottom - kCeilingTop};
        l.objects.push_back(c);
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->setSimulationVersion(simVersion);
        w->load(l);
        for (int i = 0; i < 60; ++i) step({});
    }
    void step(const PlayerInput& in) { w->fixedUpdate(in, 1.0f / 120.0f); }
    Player& p() { return w->player(); }
};

// Nova slides under the ceiling, then a bounce throws her upwards (BouncePadEntity, ImpulsePower:
// Player::launch). Returns the highest her feet went (the smallest y).
float launchedUnderCeiling(int simVersion, bool& under) {
    CeilingWorld cw(simVersion);
    Player& p = cw.p();
    p.teleportTo({470.0f, 800.0f});
    p.vel.x = p.config().maxRunSpeed;
    cw.step(runRight(true, true));
    for (int i = 0; i < 120 && p.box.x < 560.0f; ++i) cw.step(runRight(false, false));
    under = p.state() == PlayerState::Slide && p.box.x >= 560.0f && p.box.right() <= 800.0f;
    p.launch(*cw.w, {0.0f, -600.0f}, true);
    float highest = p.feet().y;
    for (int i = 0; i < 90; ++i) {
        cw.step({});
        highest = std::min(highest, p.feet().y);
    }
    return highest;
}

}  // namespace

TEST_CASE("slide: thrown upwards under a low ceiling, Nova no longer goes through it (simulation 20)") {
    bool under = false;
    const float highest = launchedUnderCeiling(kSimulationVersion, under);
    CHECK(under);
    CHECK(highest > kCeilingBottom - 0.01f);  // the ceiling stops her: her feet stay below it
}

TEST_CASE("slide: under a low ceiling, a run of before plays with its rules (simulation 19 went through it)") {
    bool under = false;
    const float highest = launchedUnderCeiling(kSlideInWallsSimulation - 1, under);
    CHECK(under);
    CHECK(highest < kCeilingTop);  // above the ceiling: the bug a replay of before keeps
}

TEST_CASE("slide: the simulation version follows the rule") {
    CHECK(kSimulationVersion >= kSlideInWallsSimulation);
}
