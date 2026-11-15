// « Course à la vérification » of the EXTRÊME levels (2.16): the progress score (route 40
// points, bosses' health and ambush waves 60 points), the race layout, the tracker, the
// replay events the server recomputes the score from, and the rankings kept intact.

#include <algorithm>
#include <cmath>
#include <string>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/race/Race.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

RaceLayout layoutOf(int bosses, std::vector<int> ambushWaves) {
    RaceLayout l;
    l.route = {{0, 0}, {1000, 0}};
    for (int i = 0; i < bosses; ++i) l.bosses.push_back({"b" + std::to_string(i), "Boss"});
    for (size_t i = 0; i < ambushWaves.size(); ++i) l.ambushes.push_back({"a" + std::to_string(i), ambushWaves[i]});
    return l;
}

}  // namespace

TEST_CASE("race: the score weighs the route 40 points and the fights 60 points") {
    // Route only (no boss, no ambush): the route is the whole score.
    RaceLayout plain = layoutOf(0, {});
    RaceProgress p;
    p.route = 0.42f;
    CHECK(std::abs(raceScore(plain, p) - 42.0f) < 0.051f);

    // 2 bosses (2 shares each) and one ambush of 4 waves (1 share): 5 shares.
    RaceLayout l = layoutOf(2, {4});
    RaceProgress q;
    q.route = 0.5f;
    q.bossRemoved = {1.0f, 0.5f};
    q.wavesDone = {2};
    // 40 * 0.5 + 60 * (2*1 + 2*0.5 + 0.5) / 5 = 20 + 42 = 62
    CHECK(std::abs(raceScore(l, q) - 62.0f) < 0.051f);
    CHECK(q.bossesBeaten() == 1);

    // Everything but the exit: never 100; only a finished run is.
    RaceProgress all;
    all.route = 1.0f;
    all.bossRemoved = {1.0f, 1.0f};
    all.wavesDone = {4};
    CHECK(raceScore(l, all) < 100.0f);
    CHECK(raceScore(l, all) >= 99.9f - 0.001f);
    all.finished = true;
    CHECK(raceScore(l, all) == 100.0f);

    // One decimal, rounded down (the server does the same).
    RaceProgress r;
    r.route = 0.12345f;
    float s = raceScore(plain, r);
    CHECK(std::abs(s * 10.0f - std::round(s * 10.0f)) < 0.001f);
    CHECK(s <= 12.345f);
}

TEST_CASE("race: a boss's health removed counts, not only the bosses beaten") {
    RaceLayout l = layoutOf(1, {});
    RaceProgress a, b;
    a.route = b.route = 0.3f;
    a.bossRemoved = {0.2f};
    b.bossRemoved = {0.8f};
    CHECK(raceScore(l, b) > raceScore(l, a));
    CHECK(a.bossesBeaten() == 0 && b.bossesBeaten() == 0);
}

TEST_CASE("race: the boss chain of the campaign is EXTRÊME, one life, sixteen bosses") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("noyau_01");
    CHECK(level.has_value());
    if (!level) return;
    CHECK(level->extreme);
    CHECK(level->oneLife());
    RaceLayout l = buildRaceLayout(*level, content, true);
    CHECK(l.bosses.size() == 16);  // the twelve guardians and, since 2.19, the four of Aethra
    CHECK(l.ambushes.empty());  // the arenas hold only their boss: counted by its health
    CHECK(l.route.size() >= 2);
    CHECK(l.route.size() <= 401);
    // The layout sent to the server keeps whole coordinates.
    Json j = l.toJson();
    CHECK(j["route"].size() == l.route.size());
    CHECK(j["bosses"].size() == 16);
}

TEST_CASE("race: the final level's ambushes are in its race") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("noyau_02");
    CHECK(level.has_value());
    if (!level) return;
    RaceLayout l = buildRaceLayout(*level, content, false);
    CHECK(l.route.empty());  // without the route (computed off the main thread in game)
    CHECK(!l.ambushes.empty());
    for (const auto& a : l.ambushes) CHECK(a.waves >= 1);
}

TEST_CASE("race: the EXTRÊME flag never changes a level's ranked fingerprint") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("noyau_01");
    CHECK(level.has_value());
    if (!level) return;
    LevelData off = *level;
    off.extreme = false;
    CHECK(fingerprintGameplay(content, *level) == fingerprintGameplay(content, off));
    // Saved and read back.
    auto back = LevelData::fromJson(level->toJson());
    auto backOff = LevelData::fromJson(off.toJson());
    CHECK(back.has_value() && back->extreme);
    CHECK(backOff.has_value() && !backOff->extreme);
}

TEST_CASE("race: one life is what an EXTRÊME level needs") {
    LevelData l = LevelData::makeEmpty("x", "X", "sylvanox");
    l.lives = 1;
    CHECK(l.oneLife());
    l.lives = 2;
    CHECK(!l.oneLife());
    l.lives = 0;
    l.timerMode = TimerMode::Challenge;
    l.challengeMaxDeaths = 0;
    CHECK(l.oneLife());
    l.challengeMaxDeaths = 1;
    CHECK(!l.oneLife());
}

TEST_CASE("race: the tracker follows a boss's health and the replay keeps it") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    LevelData l = LevelData::makeEmpty("race_test", "Course", "sylvanox");
    l.intro.steps.clear();
    l.extreme = true;
    l.lives = 1;
    Vec2 start{200, 400};
    for (const auto& o : l.objects)
        if (o.type == "player_start") start = o.rect.center();
    LevelObject boss;
    boss.id = "boss_1";
    boss.type = "enemy";
    boss.rect = {start.x + 360, start.y - 80, 96, 96};
    boss.props = {{"enemy", "echo_thornwarden"}, {"boss", true}};
    l.objects.push_back(boss);
    RaceLayout layout = buildRaceLayout(l, content, false);
    CHECK(layout.bosses.size() == 1);
    WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
    GameWorld w(s, WorldMode::Play);
    w.load(l);
    RaceTracker t;
    t.begin(layout);
    t.setRoute({start, start + Vec2{2000, 0}});
    ReplayRecorder rec;
    rec.begin(l.id, std::string(64, 'a'), makeUuidV4(), 1);
    const float dt = 1.0f / 120.0f;
    Enemy* e = nullptr;
    for (int i = 0; i < 600 && !e; ++i) {
        rec.recordInput(InputFrame{});
        w.fixedUpdate(PlayerInput{}, dt);
        rec.tick(w);
        t.update(w);
        rec.raceProgress(t.progress().bossRemoved, t.progress().wavesDone);
        if (auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity("boss_1"))) e = es->enemy(w);
    }
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(t.progress().bossRemoved[0] == 0.0f);
    e->kill(w);
    for (int i = 0; i < 4; ++i) {
        rec.recordInput(InputFrame{});
        w.fixedUpdate(PlayerInput{}, dt);
        rec.tick(w);
        t.update(w);
        rec.raceProgress(t.progress().bossRemoved, t.progress().wavesDone);
    }
    CHECK(t.progress().bossRemoved[0] == 1.0f);
    CHECK(t.progress().bossesBeaten() == 1);
    CHECK(!t.progress().finished);
    // The attempt ends without reaching the exit: no Finish event, the boss event kept.
    rec.finishAttempt(w, w.timer().elapsed());
    ReplayData rep = rec.take();
    bool finish = false;
    int bossHit = -1;
    for (const auto& ev : rep.events) {
        finish = finish || ev.type == ReplayEventType::Finish;
        if (ev.type == ReplayEventType::BossHit && ev.arg == 0) bossHit = ev.value;
    }
    CHECK(!finish);
    CHECK(bossHit == 1000);
    auto back = ReplayData::decode(rep.encode());
    CHECK(back.has_value());
    if (back) CHECK(back->events.size() == rep.events.size());
    // Its estimate: route not travelled, one boss of one beaten (60 points).
    CHECK(raceScore(t.layout(), t.progress()) >= 60.0f);
}
