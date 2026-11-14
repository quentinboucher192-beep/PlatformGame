#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelTimer.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;  // not initialised: silent
    std::unique_ptr<GameWorld> w;
    World() {
        content.reload();
        res.init(nullptr, &audio);
    }
    GameWorld& make(const LevelData& level, WorldMode mode = WorldMode::Arena) {
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, mode);
        w->load(level);
        w->autoPowerChoice = 1;  // full power slots: take the tablet, leave the first power
        // The zone tests start anywhere: they get the powers of the level's stations (2.3 power slots).
        for (const auto& so : w->level().objects)
            if (so.type == "power_unlock" && so.id.rfind("station_", 0) == 0)
                if (const PowerDefinition* sd = w->content().power(so.propStr("power"))) w->player().grantPower(*sd);
        return *w;
    }
};

LevelData flatLevel() {
    LevelData l = LevelData::makeEmpty("test_flat", "Test", "sylvanox");
    l.intro.steps.clear();
    return l;
}

PlayerInput hold(std::initializer_list<Action> actions, bool pressNow) {
    PlayerInput in;
    for (Action a : actions) {
        in.held[static_cast<int>(a)] = true;
        if (pressNow) in.pressed[static_cast<int>(a)] = true;
        if (a == Action::MoveRight) in.move.x = 1;
        if (a == Action::MoveLeft) in.move.x = -1;
    }
    return in;
}
}  // namespace

TEST_CASE("gameplay: jump height matches the designer parameters") {
    World env;
    GameWorld& w = env.make(flatLevel());
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 60; ++i) w.fixedUpdate(PlayerInput{}, dt);  // settle on the ground
    CHECK(w.player().grounded());
    float groundY = w.player().box.bottom();
    float apex = groundY;
    w.fixedUpdate(hold({Action::Jump}, true), dt);
    for (int i = 0; i < 120; ++i) {
        w.fixedUpdate(hold({Action::Jump}, false), dt);
        apex = std::min(apex, w.player().box.bottom());
    }
    float height = groundY - apex;
    const PlayerConfig& c = w.player().config();
    CHECK(height > c.jumpHeight * 0.95f);
    CHECK(height < c.jumpHeight * 1.2f);  // apex hang adds a little
    // Short hop when released immediately (variable jump height).
    for (int i = 0; i < 120; ++i) w.fixedUpdate(PlayerInput{}, dt);
    groundY = w.player().box.bottom();
    apex = groundY;
    w.fixedUpdate(hold({Action::Jump}, true), dt);
    for (int i = 0; i < 120; ++i) {
        w.fixedUpdate(PlayerInput{}, dt);
        apex = std::min(apex, w.player().box.bottom());
    }
    CHECK(groundY - apex < height * 0.6f);
}

TEST_CASE("gameplay: running reaches max speed and stops with friction") {
    World env;
    GameWorld& w = env.make(flatLevel());
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, dt);
    for (int i = 0; i < 60; ++i) w.fixedUpdate(hold({Action::MoveRight}, i == 0), dt);
    CHECK_NEAR(w.player().vel.x, w.player().config().maxRunSpeed, 1.0);
    for (int i = 0; i < 60; ++i) w.fixedUpdate(PlayerInput{}, dt);
    CHECK_NEAR(w.player().vel.x, 0.0, 0.01);
}

TEST_CASE("gameplay: double jump and dash power") {
    World env;
    LevelData l = flatLevel();
    l.startPowers = {"dash"};
    GameWorld& w = env.make(l);
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, dt);
    CHECK(w.player().hasPower("dash"));
    float x0 = w.player().box.x;
    w.fixedUpdate(hold({Action::Dash}, true), dt);
    CHECK(w.player().dashing());
    for (int i = 0; i < 20; ++i) w.fixedUpdate(PlayerInput{}, dt);
    CHECK(w.player().box.x - x0 > 50.0f);
    CHECK(w.player().energy < w.player().config().maxEnergy);
}

TEST_CASE("gameplay: enemy detects, attacks and dies") {
    World env;
    LevelData l = flatLevel();
    GameWorld& w = env.make(l);
    const float dt = 1.0f / 120.0f;
    Enemy* e = w.spawnEnemy("glowmite", w.player().feet() + Vec2{60, 0}, true);
    CHECK(e != nullptr);
    bool attacked = false;
    for (int i = 0; i < 600 && !attacked; ++i) {
        w.fixedUpdate(PlayerInput{}, dt);
        if (!w.enemies().empty()) attacked |= w.enemies().front()->state() == EnemyState::Telegraph;
    }
    CHECK(attacked);
    // Kill it with melee damage.
    DamageInfo d;
    d.amount = 10;
    d.type = DamageType::Melee;
    d.source = w.player().center();
    d.fromPlayer = true;
    w.damageEnemies(w.enemies().front()->box, d);
    CHECK(!w.enemies().front()->alive());
    CHECK(w.stats().kills == 1);
}

TEST_CASE("levels: first level is complete and round-trips") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    CHECK(level->countObjects("player_start") == 1);
    CHECK(level->countObjects("exit") == 1);
    CHECK(level->countObjects("checkpoint") >= 6);
    CHECK(level->countObjects("relic") == 3);
    CHECK(level->countObjects("secret_area") >= 3);
    int tutorials = 0;  // the five tutorial tablets (power stations aside)
    for (const auto& o : level->objects) tutorials += o.type == "power_unlock" && o.id.rfind("station_", 0) != 0 ? 1 : 0;
    CHECK(tutorials == 5);
    bool boss = false;
    for (const auto& o : level->objects) boss |= o.type == "enemy" && o.prop<bool>("boss", false);
    CHECK(boss);
    CHECK(!level->intro.empty());
    auto again = LevelData::fromJson(level->toJson());
    CHECK(again.has_value());
    CHECK(again->toJson() == level->toJson());
    for (const auto& o : level->objects) {
        if (o.type != "enemy") continue;
        CHECK(content.enemy(o.propStr("enemy")) != nullptr);
    }
}

TEST_CASE("records: medals, score and persistence") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    RunResult r;
    r.levelId = "sylvanox_01";
    r.completed = true;
    r.time = 500;
    r.deaths = 3;
    CHECK(RecordsManager::evaluateMedal(*level, r) == Medal::Bronze);
    r.time = 400;
    CHECK(RecordsManager::evaluateMedal(*level, r) == Medal::Silver);
    r.time = 250;
    CHECK(RecordsManager::evaluateMedal(*level, r) == Medal::Gold);
    r.time = 150;
    CHECK(RecordsManager::evaluateMedal(*level, r) == Medal::Gold);  // platinum requires 0 deaths
    r.deaths = 0;
    CHECK(RecordsManager::evaluateMedal(*level, r) == Medal::Platinum);
    r.completed = false;
    CHECK(RecordsManager::evaluateMedal(*level, r) == Medal::None);

    RecordsManager rec;
    RunResult a;
    a.levelId = "x";
    a.completed = true;
    a.time = 100;
    a.score = 5000;
    a.medal = Medal::Silver;
    RecordUpdate u = rec.submit(a);
    CHECK(u.firstCompletion && u.newBestTime && u.newMedal);
    RunResult b = a;
    b.time = 120;
    b.score = 7000;
    u = rec.submit(b);
    CHECK(!u.newBestTime && u.newBestScore);
    CHECK(rec.get("x")->bestTime == 100);
    CHECK(rec.get("x")->leaderboard.size() == 2);
    CHECK(rec.save());
    RecordsManager loaded;
    loaded.load();
    CHECK(loaded.get("x") != nullptr && loaded.get("x")->bestScore == 7000);
    // Migration from the v1 format (milliseconds).
    Json v2 = RecordsManager::migrateV1ToV2({{"levels", {{"lvl", {{"bestTimeMs", 61500}, {"completions", 2}}}}}});
    loaded.fromJson(v2);
    CHECK_NEAR(loaded.get("lvl")->bestTime, 61.5, 1e-4);
}

TEST_CASE("timer: frame-rate independent and formatted") {
    LevelTimer t;
    t.configure(TimerMode::Limited, 60, ExpireAction::GameOver);
    t.start();
    for (int i = 0; i < 120 * 30; ++i) t.fixedUpdate(1.0f / 120.0f);
    CHECK_NEAR(t.elapsed(), 30.0, 1e-3);
    CHECK_NEAR(t.remaining(), 30.0, 1e-3);
    CHECK(!t.expired());
    for (int i = 0; i < 120 * 31; ++i) t.fixedUpdate(1.0f / 120.0f);
    CHECK(t.expired());
    CHECK(LevelTimer::format(83.456f) == "1:23.45");
    CHECK(LevelTimer::format(3725.0f) == "1:02:05");
    CHECK(LevelTimer::formatDelta(-1.5f) == "-1.50");
}
