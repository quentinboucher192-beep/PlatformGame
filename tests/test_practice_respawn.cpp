// « Réessayer en entraînement » (2.48) on a hardcore level (EXTRÊME, one life): a fall that
// would end the run brings Nova back where she last stood, blinking and unhurt for a while;
// the run is a training (nothing is ranked or recorded: GameScene checks world.training()).

#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/input/InputManager.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct PracticeRun {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit PracticeRun(bool practice) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("practice_test", "Entraînement", "sylvanox");
        l.intro.steps.clear();
        l.extreme = true;
        l.lives = 1;
        WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(l);
        w->setPracticeRespawn(practice);
    }
    void run(int frames, float moveX = 0.0f) {
        PlayerInput in;
        in.move.x = moveX;
        for (int i = 0; i < frames; ++i) w->fixedUpdate(in, 1.0f / 120.0f);
    }
};

}  // namespace

TEST_CASE("practice respawn: a fall on a one-life level fails it, unless the run is a practice") {
    for (bool practice : {false, true}) {
        PracticeRun r(practice);
        r.run(240);  // appear, then stand on the ground
        CHECK(r.w->state() == WorldState::Playing);
        r.run(90, 1.0f);  // walk a little to the right
        r.run(60);
        const Vec2 stood = r.w->player().feet();
        r.w->player().kill(*r.w);
        r.run(600);
        if (!practice) {
            CHECK(r.w->state() == WorldState::Failed);
            CHECK(!r.w->training());
            continue;
        }
        CHECK(r.w->training());
        CHECK(r.w->practiceRespawn());
        CHECK(r.w->state() == WorldState::Playing);
        CHECK(r.w->player().alive());
        CHECK(r.w->stats().deaths == 1);
        // Back where she stood (not at the start of the level).
        CHECK(std::fabs(r.w->player().feet().x - stood.x) < 24.0f);
    }
}

TEST_CASE("practice respawn: Nova comes back blinking, unhurt for a while") {
    PracticeRun r(true);
    r.run(240);
    r.w->player().kill(*r.w);
    // The death animation, then the materialisation: invulnerable right after.
    bool sawInvulnerable = false;
    for (int i = 0; i < 600 && !sawInvulnerable; ++i) {
        r.run(1);
        sawInvulnerable = r.w->player().alive() && r.w->player().state() != PlayerState::Respawn && r.w->player().invulnerable();
    }
    CHECK(sawInvulnerable);
}
