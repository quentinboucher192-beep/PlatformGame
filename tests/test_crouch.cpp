// Crouching in place (2.17): Down alone on the ground lowers the hitbox to let attacks pass
// overhead, even standing still. Down while running is still a slide, a diagonal keeps
// walking, Jump stands up.

#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct CrouchWorld {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    CrouchWorld() {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_crouch", "Test", "sylvanox");
        l.intro.steps.clear();
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->load(l);
        for (int i = 0; i < 60; ++i) step({});
    }
    void step(const PlayerInput& in) { w->fixedUpdate(in, 1.0f / 120.0f); }
    Player& p() { return w->player(); }
};

PlayerInput input(float mx, float my, bool jumpPressed = false) {
    PlayerInput in;
    in.move = {mx, my};
    if (my > 0.5f) in.held[static_cast<int>(Action::MoveDown)] = true;
    if (mx > 0.5f) in.held[static_cast<int>(Action::MoveRight)] = true;
    if (jumpPressed) {
        in.held[static_cast<int>(Action::Jump)] = true;
        in.pressed[static_cast<int>(Action::Jump)] = true;
    }
    return in;
}

}  // namespace

TEST_CASE("crouch: Down alone on the ground crouches in place, release stands up") {
    CrouchWorld cw;
    CHECK(cw.p().grounded());
    const float standH = cw.p().box.h;
    const float feet = cw.p().box.bottom();
    const float x = cw.p().box.center().x;
    for (int i = 0; i < 30; ++i) cw.step(input(0, 1));
    CHECK(cw.p().state() == PlayerState::Crouch);
    CHECK(cw.p().box.h < standH * 0.7f);
    CHECK(std::abs(cw.p().box.bottom() - feet) < 0.01f);  // the feet stay on the ground
    CHECK(std::abs(cw.p().box.center().x - x) < 1.0f);    // in place
    for (int i = 0; i < 4; ++i) cw.step(input(0, 0));
    CHECK(cw.p().state() != PlayerState::Crouch);
    CHECK(std::abs(cw.p().box.h - standH) < 0.01f);
}

TEST_CASE("crouch: a diagonal keeps walking, running makes a slide, Jump stands up") {
    {
        CrouchWorld cw;
        float x0 = cw.p().box.center().x;
        for (int i = 0; i < 40; ++i) cw.step(input(0.7f, 0.7f));
        CHECK(cw.p().state() != PlayerState::Crouch);
        CHECK(cw.p().box.center().x > x0 + 10.0f);
    }
    {
        CrouchWorld cw;
        for (int i = 0; i < 80; ++i) cw.step(input(1, 0));  // full speed
        PlayerInput slide = input(1, 1);
        slide.pressed[static_cast<int>(Action::MoveDown)] = true;
        cw.step(slide);
        CHECK(cw.p().state() == PlayerState::Slide);
    }
    {
        CrouchWorld cw;
        for (int i = 0; i < 20; ++i) cw.step(input(0, 1));
        CHECK(cw.p().state() == PlayerState::Crouch);
        float standH = cw.p().config().size.y;
        cw.step(input(0, 1, true));
        cw.step(input(0, 1));
        CHECK(cw.p().vel.y < 0.0f);  // jumping
        CHECK(std::abs(cw.p().box.h - standH) < 0.01f);
    }
}

TEST_CASE("crouch: a shot at head height passes over a crouching player") {
    CrouchWorld cw;
    Rect stand = cw.p().box;
    for (int i = 0; i < 20; ++i) cw.step(input(0, 1));
    Rect low = cw.p().box;
    // A horizontal shot 4 px tall flying at the standing player's chest.
    Rect shot{stand.x - 40.0f, stand.y + 3.0f, 120.0f, 4.0f};
    CHECK(shot.intersects(stand));
    CHECK(!shot.intersects(low));
}

TEST_CASE("crouch: the rankings generation follows the rules (2.17: a new generation)") {
    CHECK(kSimulationVersion == 15);  // 2.69: the attack upwards and downwards (2.62.2: ambushes kept)
    CHECK(kRankingGeneration == 8);  // ... so the 2.17 rankings are kept
    ContentDatabase content;
    content.reload();
    LevelFingerprints fps;
    const LevelFingerprint* fp = fps.get(content, "noyau_01");
    CHECK(fp != nullptr);
    // Generation 6's hash (before the energy costs and the minions rule) is no longer used:
    // the 2.17 rankings start again (revision 8 of every campaign level).
    if (fp) {
        CHECK(fp->hash != "e6b2f3ab5ab26dfa9d28cedf0e776f0b6662681b46f0e67c50a703053c2feefc");
        CHECK(fp->version >= 8);
    }
}
