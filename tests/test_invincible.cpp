// « Invincible » (2.51): administrators can switch it on in a training run (pause menu): Nova
// takes no damage and a fall brings her back on her last solid ground. Only in a training run
// (never one that counts), never in a challenge.

#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct Run {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    Run() {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("invincible_test", "Entraînement", "sylvanox");
        l.intro.steps.clear();
        l.lives = 1;
        WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(l);
    }
    void run(int frames, float moveX = 0.0f) {
        PlayerInput in;
        in.move.x = moveX;
        for (int i = 0; i < frames; ++i) w->fixedUpdate(in, 1.0f / 120.0f);
    }
    bool hit() {
        DamageInfo d;
        d.amount = 3.0f;
        d.type = DamageType::Contact;
        d.source = w->player().center() + Vec2{10.0f, 0.0f};
        return w->player().hurt(*w, d);
    }
};

}  // namespace

TEST_CASE("invincible: only in a training run, never in a run that counts") {
    Run r;
    r.run(240);
    r.w->setInvincible(true);
    CHECK(!r.w->invincible());  // not a training: a run that counts never gets it
    const int before = r.w->player().health;
    r.hit();
    CHECK(r.w->player().health < before);
}

TEST_CASE("invincible: no damage and a fall goes back to solid ground, in a training run") {
    Run r;
    r.run(240);
    r.w->setTraining(true);
    r.w->setInvincible(true);
    CHECK(r.w->invincible());
    r.run(90, 1.0f);
    r.run(60);
    const Vec2 stood = r.w->player().feet();
    const int health = r.w->player().health;
    for (int i = 0; i < 5; ++i) {
        CHECK(!r.hit());
        r.run(30);
    }
    CHECK(r.w->player().health == health);
    CHECK(r.w->stats().damageTaken == 0);
    // A fall (or any death) brings Nova back where she last stood, alive, without a death counted.
    r.w->player().kill(*r.w);
    CHECK(r.w->player().alive());
    CHECK(r.w->stats().deaths == 0);
    CHECK(std::abs(r.w->player().feet().x - stood.x) < 24.0f);
    r.run(120);
    CHECK(r.w->state() == WorldState::Playing);
    // Switched off: hits hurt again.
    r.w->setInvincible(false);
    CHECK(!r.w->invincible());
    r.run(240);  // past the short invulnerability of the rescue
    CHECK(r.hit() || r.w->player().health < health);
}
