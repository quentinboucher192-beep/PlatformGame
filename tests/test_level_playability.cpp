// Automated traversal checks of the first level: the hardest required moves must be
// achievable with the shipped player tuning.

#include <functional>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float T = 16.0f;
constexpr float DT = 1.0f / 120.0f;

struct Run {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    Run(float tx, float ty, std::initializer_list<const char*> powers) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel("sylvanox_01");
        CHECK(level.has_value());
        level->intro.steps.clear();
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->startAt({tx * T, ty * T});
        for (const char* p : powers) w->player().grantPower(*content.power(p));
        for (int i = 0; i < 20; ++i) w->fixedUpdate(PlayerInput{}, DT);  // settle
    }
    Player& p() { return w->player(); }
    /// Steps the world with a controller returning the input for each step.
    void play(int steps, const std::function<PlayerInput(Player&, int)>& ctrl) {
        for (int i = 0; i < steps; ++i) w->fixedUpdate(ctrl(p(), i), DT);
    }
};

PlayerInput input(bool right, std::initializer_list<Action> pressed, std::initializer_list<Action> held = {}) {
    PlayerInput in;
    if (right) {
        in.held[static_cast<int>(Action::MoveRight)] = true;
        in.move.x = 1.0f;
    }
    for (Action a : pressed) in.pressed[static_cast<int>(a)] = in.held[static_cast<int>(a)] = true;
    for (Action a : held) in.held[static_cast<int>(a)] = true;
    if (in.held[static_cast<int>(Action::MoveDown)]) in.move.y = 1.0f;
    return in;
}
}  // namespace

TEST_CASE("level: zone 1 double-jump wall is climbable") {
    Run r(43, 57, {});
    int phase = 0;
    r.play(600, [&](Player& p, int) {
        if (phase == 0 && p.box.right() > 47.5f * T && p.grounded()) {
            phase = 1;
            return input(true, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -60.0f) {
            phase = 2;
            return input(true, {Action::Jump}, {Action::Jump});
        }
        if (p.box.x > 53.5f * T && p.grounded()) return PlayerInput{};  // made it: stop on the wall
        return input(true, {}, phase >= 1 ? std::initializer_list<Action>{Action::Jump} : std::initializer_list<Action>{});
    });
    CHECK(r.p().alive());
    CHECK(r.p().box.x > 52.0f * T);
    CHECK_NEAR(r.p().box.bottom(), 52.0f * T, 0.5f);
}

TEST_CASE("level: zone 2 dash gap can be crossed") {
    Run r(209, 50, {"dash"});
    int phase = 0;
    r.play(500, [&](Player& p, int) {
        if (phase == 0 && p.box.right() > 215.6f * T) {
            phase = 1;
            return input(true, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return input(true, {Action::Jump}, {Action::Jump});
        }
        if (phase == 2 && p.vel.y > -20.0f) {
            phase = 3;
            return input(true, {Action::Dash});
        }
        return input(true, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.p().box.x > 230.0f * T);
    CHECK(r.p().grounded());
}

TEST_CASE("level: zone 4 cliff needs jump + double jump + impulse") {
    Run r(424, 50, {"impulse"});
    int phase = 0;
    r.play(500, [&](Player& p, int) {
        if (phase == 0 && p.box.right() > 428.6f * T) {
            phase = 1;
            return input(true, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return input(true, {Action::Jump}, {Action::Jump});
        }
        if (phase == 2 && p.vel.y > -30.0f) {
            phase = 3;
            return input(true, {Action::Impulse}, {Action::Jump});
        }
        if (p.box.x > 431.5f * T && p.grounded()) return PlayerInput{};
        return input(true, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.p().box.bottom() <= 40.0f * T + 0.5f);
    CHECK(r.p().box.x > 430.0f * T);
}

TEST_CASE("level: zone 4 low passage requires a slide") {
    Run r(463.5f, 36, {});
    bool slid = false;
    r.play(900, [&](Player& p, int) {
        if (!slid && p.box.right() > 466.2f * T && std::abs(p.vel.x) > 120.0f) {
            slid = true;
            return input(true, {Action::MoveDown}, {Action::MoveDown});
        }
        return input(true, {});
    });
    CHECK(slid);
    CHECK(r.p().alive());
    CHECK(r.p().box.x > 472.0f * T);
}

TEST_CASE("level: zone 4 phase barrier is crossed by teleport") {
    Run r(484, 36, {"teleport"});
    float before = r.p().box.x;
    r.play(120, [&](Player&, int i) { return i < 60 ? input(true, {}) : PlayerInput{}; });
    CHECK(r.p().box.right() <= 488.0f * T + 1.0f);  // blocked by the barrier
    r.play(2, [&](Player&, int i) { return i == 0 ? input(true, {Action::Teleport}) : PlayerInput{}; });
    CHECK(r.p().box.x > 489.0f * T);
    CHECK(r.p().box.x > before);
}

TEST_CASE("level: arena locks then opens when every wave is cleared") {
    Run r(380, 50, {"wave"});
    r.play(60, [&](Player&, int) { return input(true, {}); });
    int alive = 0;
    for (auto& e : r.w->enemies()) alive += e->alive() && !e->dormant() ? 1 : 0;
    CHECK(alive > 0);
    // Clear the waves with heavy damage.
    for (int wave = 0; wave < 2; ++wave) {
        DamageInfo d;
        d.amount = 100;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        for (auto& e : r.w->enemies())
            if (e->alive() && !e->dormant()) {
                d.source = e->center();
                e->takeDamage(*r.w, d);
            }
        r.play(240, [&](Player& p, int) {
            p.setInvulnerable(1.0f);
            return PlayerInput{};
        });
    }
    // Right door (x = 413) must be open now: walking right goes through it.
    r.play(600, [&](Player& p, int) {
        p.setInvulnerable(1.0f);
        return input(true, {});
    });
    CHECK(r.p().box.x > 414.0f * T);
}

TEST_CASE("level: mini-boss wakes, changes phase, dies and opens the way") {
    Run r(556, 48, {"wave"});
    r.play(240, [&](Player& p, int) {
        p.setInvulnerable(1.0f);
        return input(true, {});
    });
    Enemy* boss = r.w->activeBoss();
    CHECK(boss != nullptr);
    CHECK(boss->def().isBoss);
    int maxPhase = 0;
    for (int hit = 0; hit < 200 && boss && boss->alive(); ++hit) {
        DamageInfo d;
        d.amount = 2;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        d.source = boss->center() - Vec2{30, 0};
        boss->takeDamage(*r.w, d);
        r.play(30, [&](Player& p, int) {
            p.setInvulnerable(1.0f);
            p.health = 5;
            return PlayerInput{};
        });
        maxPhase = std::max(maxPhase, boss->phaseIndex());
        boss = r.w->activeBoss();
    }
    CHECK(maxPhase == 2);  // three phases
    CHECK(r.w->activeBoss() == nullptr);
    r.play(600, [&](Player& p, int) {
        p.setInvulnerable(1.0f);
        return input(true, {});
    });
    CHECK(r.p().box.x > 603.0f * T);
}

TEST_CASE("level: reaching the exit plays the outro and completes the level") {
    Run r(640, 30, {});
    CHECK(r.w->state() == WorldState::Playing);
    r.play(240, [&](Player&, int) { return input(true, {}); });
    CHECK(r.w->state() == WorldState::Outro || r.w->state() == WorldState::Completed);
    r.play(1500, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.w->state() == WorldState::Completed);
    CHECK(r.w->timer().elapsed() > 0.0f);
    CHECK(!r.w->timer().running());
}
