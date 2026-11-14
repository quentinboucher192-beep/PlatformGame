// Powers added in 2.5: time slow, magnet, air dash, plasma bomb; energy reserve of 150.

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;

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
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->load(level);
        return *w;
    }
};

LevelData flat(std::vector<std::string> powers) {
    LevelData l = LevelData::makeEmpty("test_new_powers", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(powers);
    l.maxPowers = 0;
    return l;
}

void addObject(LevelData& l, const std::string& type, Rect r, Json props = Json::object()) {
    LevelObject o;
    o.id = "t" + std::to_string(l.objects.size() + 1);
    o.type = type;
    o.layer = "gameplay";
    o.rect = r;
    o.props = std::move(props);
    l.objects.push_back(o);
}

PlayerInput press(Action a, Action hold = Action::Count) {
    PlayerInput in;
    in.held[static_cast<int>(a)] = in.pressed[static_cast<int>(a)] = true;
    if (hold != Action::Count) {
        in.held[static_cast<int>(hold)] = true;
        if (hold == Action::MoveRight) in.move.x = 1.0f;
    }
    return in;
}

void settle(GameWorld& w, int steps = 40) {
    for (int i = 0; i < steps; ++i) w.fixedUpdate(PlayerInput{}, kDt);
}

/// Distance an enemy walks in `steps` (time slow on or off).
float enemyWalk(bool slow) {
    World env;
    GameWorld& w = env.make(flat({"time_slow"}));
    settle(w);
    Enemy* e = w.spawnEnemy("glowmite", w.player().feet() + Vec2{300, 0}, true, {}, 400.0f);
    settle(w, 30);
    if (slow) w.fixedUpdate(press(Action::Special), kDt);
    float x0 = e->box.x;
    float travelled = 0.0f;
    for (int i = 0; i < 240; ++i) {
        float before = e->box.x;
        w.fixedUpdate(PlayerInput{}, kDt);
        travelled += std::abs(e->box.x - before);
    }
    (void)x0;
    return travelled;
}
}  // namespace

TEST_CASE("new powers: the energy reserve is 150") {
    World env;
    GameWorld& w = env.make(flat({}));
    settle(w);
    CHECK_NEAR(w.player().config().maxEnergy, 150.0f, 0.01f);
}

TEST_CASE("new powers: the time slow makes enemies walk much less") {
    float normal = enemyWalk(false);
    float slowed = enemyWalk(true);
    CHECK(normal > 20.0f);
    CHECK(slowed < normal * 0.6f);
}

TEST_CASE("new powers: the magnet pulls a shard from far away") {
    for (bool magnet : {false, true}) {
        World env;
        LevelData l = flat(magnet ? std::vector<std::string>{"magnet"} : std::vector<std::string>{});
        addObject(l, "shard", {100 + 120, 760, 12, 12});
        GameWorld& w = env.make(l);
        settle(w, 180);
        CHECK(w.stats().shards == (magnet ? 1 : 0));
    }
}

TEST_CASE("new powers: the air dash gives a second dash in the air") {
    for (bool upgrade : {false, true}) {
        World env;
        GameWorld& w = env.make(flat(upgrade ? std::vector<std::string>{"dash", "air_dash"} : std::vector<std::string>{"dash"}));
        settle(w);
        w.player().teleportTo({100, 200});  // high above the floor: a long fall
        for (int i = 0; i < 4; ++i) w.fixedUpdate(PlayerInput{}, kDt);
        w.fixedUpdate(press(Action::Dash, Action::MoveRight), kDt);
        CHECK(w.player().dashing());
        for (int i = 0; i < 60; ++i) w.fixedUpdate(PlayerInput{}, kDt);  // dash over, cooldown over
        CHECK(!w.player().grounded());
        w.fixedUpdate(press(Action::Dash, Action::MoveRight), kDt);
        CHECK(w.player().dashing() == upgrade);
    }
}

TEST_CASE("new powers: the plasma bomb bounces, explodes, hurts enemies and breaks cracked rock") {
    World env;
    LevelData l = flat({"plasma_bomb"});
    addObject(l, "power_gate", {205, 736, 32, 64}, {{"power", "impulse"}});
    GameWorld& w = env.make(l);
    settle(w);
    Enemy* e = w.spawnEnemy("glowmite", {175, 800}, true, {}, 1.0f);
    CHECK(e != nullptr);
    float hp = e->hp();
    w.player().facing = 1;
    w.fixedUpdate(press(Action::Special), kDt);
    bool thrown = false;
    for (int i = 0; i < 240; ++i) {
        w.fixedUpdate(PlayerInput{}, kDt);
        thrown = thrown || !w.projectiles().empty();
    }
    CHECK(thrown);
    CHECK(!e->alive() || e->hp() < hp);
    // The cracked rock is open: walking right goes past it.
    for (int i = 0; i < 180; ++i) w.fixedUpdate(press(Action::MoveRight, Action::MoveRight), kDt);
    CHECK(w.player().feet().x > 250.0f);
}
