// Rules of a level (2.8), set in the level editor and shown before playing: the damage
// factor and the number of lives.

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
struct Env {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Env() {
        content.reload();
        res.init(nullptr, &audio);
    }
};
LevelData flat() {
    LevelData l = LevelData::makeEmpty("test_rules", "Test", "sylvanox");
    l.intro.steps.clear();
    return l;
}
int healthAfterHit(Env& env, float factor) {
    LevelData l = flat();
    l.damageFactor = factor;
    WorldServices s{env.res, env.audio, env.content, nullptr, 1.0f, 1.0f};
    GameWorld w(s, WorldMode::Arena);
    w.load(l);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
    int before = w.player().health;
    DamageInfo d;
    d.amount = 1;
    d.type = DamageType::Melee;
    d.source = w.player().center() + Vec2{10, 0};
    w.player().hurt(w, d);
    return before - w.player().health;
}
}  // namespace

TEST_CASE("level rules: the damage factor multiplies the damage received") {
    Env env;
    CHECK(healthAfterHit(env, 1.0f) == 1);
    CHECK(healthAfterHit(env, 3.0f) == 3);
    CHECK(healthAfterHit(env, 0.25f) == 1);  // never less than one heart
}

TEST_CASE("level rules: lives end the run, unlimited by default") {
    Env env;
    for (int lives : {0, 2}) {
        LevelData l = flat();
        l.lives = lives;
        WorldServices s{env.res, env.audio, env.content, nullptr, 1.0f, 1.0f};
        GameWorld w(s, WorldMode::Play);
        w.load(l);
        w.startAt({160, 800});
        for (int death = 0; death < 3; ++death) {
            w.player().kill(w);
            for (int i = 0; i < 900 && !w.player().alive() && w.state() == WorldState::Playing; ++i)
                w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
        }
        if (lives == 0) CHECK(w.state() == WorldState::Playing);
        else CHECK(w.state() != WorldState::Playing);
    }
}

TEST_CASE("level rules: saved only when set, part of the fingerprint, shown from the challenge mode") {
    Env env;
    LevelData l = flat();
    Json j = l.toJson();
    CHECK(!j.contains("damageFactor"));
    CHECK(!j.contains("lives"));
    std::string base = fingerprintGameplay(env.content, l);
    l.damageFactor = 2.0f;
    l.lives = 3;
    auto back = LevelData::fromJson(l.toJson());
    CHECK(back.has_value());
    if (back) {
        CHECK_NEAR(back->damageFactor, 2.0f, 1e-5);
        CHECK(back->lives == 3);
    }
    CHECK(fingerprintGameplay(env.content, l) != base);
    LevelData c = flat();
    c.timerMode = TimerMode::Challenge;
    c.challengeMaxDeaths = 0;
    CHECK(c.livesAllowed() == 1);  // the boss rush: one life
}
