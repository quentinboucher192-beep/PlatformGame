// 2.92 (simulation 19, GameWorld::startHeld): a level that opens on its intro holds its start until its
// clock starts (Nova can move), so that the level — its enemies, platforms, events, dangers and the
// simulation's time — is the same at that step whether the intro was watched to the end or skipped
// (before, the level went on during the intro and during Nova's materialisation after a skip). A replay
// recorded with the rules of before still plays its run exactly (GameWorld::simulationVersion).

#include <cstdio>
#include <memory>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// A level of the campaign loaded for play, with the rules of `rules`.
struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;

    World(const std::string& levelId, int rules) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel(levelId);
        CHECK(level.has_value());
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->setSimulationVersion(rules);
        w->load(*level);
        w->seedGameplay(levelSeed(levelId));
    }
};

/// Plausible play (running right, jumps, attacks, dashes), with the press / release edges.
std::vector<InputFrame> script(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            const int dir = r.unit() < 0.8f ? 1 : -1;
            cur.held |= 1u << static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= 1u << static_cast<int>(Action::Jump);
            if (r.chance(0.25f)) cur.held |= 1u << static_cast<int>(Action::Attack);
            if (r.chance(0.08f)) cur.held |= 1u << static_cast<int>(Action::Dash);
            hold = r.rangeInt(4, 40);
        }
        InputFrame f = cur;
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        out.push_back(f);
        prev = f;
    }
    return out;
}

/// The input of step `i` of a run that skips the intro at its first step (or watches it): nothing
/// before the step where the clock starts (`clockStep`), then the script from that step on.
InputFrame inputAt(int i, bool skip, int clockStep, const std::vector<InputFrame>& play) {
    if (i < clockStep) {
        InputFrame f;
        if (skip && i == 0) f.flags = InputFrame::SkipCinematic;
        return f;
    }
    const size_t k = static_cast<size_t>(i - clockStep);
    return k < play.size() ? play[k] : InputFrame{};
}

/// The step (from 0) in which the clock starts, the intro watched or skipped at the first step.
int clockStep(const std::string& level, bool skip, int rules) {
    World s(level, rules);
    for (int i = 0; i < 120 * 120; ++i) {
        stepWorld(*s.w, inputAt(i, skip, 1 << 30, {}), kSimDt);
        if (s.w->timer().running()) return i;
    }
    return -1;
}

struct Run {
    int clockStep = -1;
    uint64_t atClock = 0;              // the digest after the step where the clock started
    std::vector<Rect> enemies;         // where the enemies are then
    std::vector<Rect> entities;        // and the scenery's objects
    float time = 0.0f, shown = 0.0f;   // the simulation's time and the time shown then
    std::vector<uint64_t> checks;      // a digest every 60 steps after it
};

Run play(const std::string& level, bool skip, int rules, const std::vector<InputFrame>& inputs) {
    Run r;
    r.clockStep = clockStep(level, skip, rules);
    CHECK(r.clockStep > 0);
    World s(level, rules);
    const int total = r.clockStep + 1 + static_cast<int>(inputs.size());
    for (int i = 0; i < total; ++i) {
        stepWorld(*s.w, inputAt(i, skip, r.clockStep, inputs), kSimDt);
        if (i < r.clockStep) CHECK(!s.w->timer().running());
        if (i == r.clockStep) {
            CHECK(s.w->timer().running());
            CHECK(!s.w->startHeld());
            r.atClock = simulationStateHash(*s.w);
            for (const auto& e : s.w->enemies()) r.enemies.push_back(e->box);
            for (const auto& e : s.w->entities()) r.entities.push_back(e->box);
            r.time = s.w->time();
            r.shown = s.w->shownTime();
        } else if (i > r.clockStep && (i - r.clockStep) % 60 == 0) {
            r.checks.push_back(simulationStateHash(*s.w));
        }
    }
    return r;
}

bool sameRects(const std::vector<Rect>& a, const std::vector<Rect>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h) return false;
    return true;
}

}  // namespace

TEST_CASE("held start (19): the level is the same when Nova can move, its intro watched or skipped") {
    CHECK(kSimulationVersion == 19 && kHeldStartSimulation == 19);
    CHECK(kRankingGeneration == 8);  // the records are kept (the player asked for it)
    // sylvanox_01: Nova appears during its intro (« spawnPlayer »); aethra_01 and confins_01: after it.
    const auto inputs = script(2026, 120 * 20);
    for (const char* level : {"sylvanox_01", "aethra_01", "confins_01"}) {
        const Run watched = play(level, false, kSimulationVersion, inputs);
        const Run skipped = play(level, true, kSimulationVersion, inputs);
        std::printf("  %s : l'horloge démarre au pas %d (intro regardée), %d (passée)\n", level, watched.clockStep, skipped.clockStep);
        CHECK(watched.clockStep > skipped.clockStep);  // the intro takes its time...
        CHECK(!watched.enemies.empty());
        CHECK(sameRects(watched.enemies, skipped.enemies));    // ...the enemies wait where they appeared
        CHECK(sameRects(watched.entities, skipped.entities));  // and the moving scenery
        CHECK(watched.time == skipped.time);                   // the simulation's time starts with the clock
        CHECK(watched.time == kSimDt);
        CHECK(watched.shown > skipped.shown);                  // the time shown went on during the intro
        CHECK(watched.atClock == skipped.atClock);
        size_t same = 0;
        while (same < watched.checks.size() && same < skipped.checks.size() && watched.checks[same] == skipped.checks[same]) ++same;
        if (same != watched.checks.size()) std::printf("  %s : premier écart %.1f s après le départ\n", level, (same + 1) * 0.5);
        CHECK(watched.checks.size() == 40 && same == 40);  // and the whole run after it (20 s)
    }
    // With the rules of 18 (a replay of before), the level went on during the intro: never the same.
    const Run watched18 = play("sylvanox_01", false, 18, inputs);
    const Run skipped18 = play("sylvanox_01", true, 18, inputs);
    CHECK(watched18.clockStep == play("sylvanox_01", false, kSimulationVersion, {}).clockStep);  // the clock: as before
    CHECK(watched18.atClock != skipped18.atClock);
    CHECK(!sameRects(watched18.enemies, skipped18.enemies));
}

TEST_CASE("held start (19): only the intro, the camera, the display and Nova's appearance go on") {
    World s("sylvanox_01", kSimulationVersion);
    CHECK(s.w->state() == WorldState::Intro && s.w->startHeld());
    std::vector<Rect> at;
    stepWorld(*s.w, InputFrame{}, kSimDt);  // the enemies placed at the load join the world's list
    for (const auto& e : s.w->enemies()) at.push_back(e->box);
    CHECK(!at.empty());
    bool appeared = false;
    int steps = 1;
    while (s.w->startHeld() && steps < 120 * 120) {
        stepWorld(*s.w, InputFrame{}, kSimDt);
        ++steps;
        if (!s.w->startHeld()) break;
        CHECK(s.w->time() == 0.0f);
        CHECK(!s.w->timer().running());
        std::vector<Rect> now;
        for (const auto& e : s.w->enemies()) now.push_back(e->box);
        CHECK(sameRects(at, now));
        if (s.w->player().state() == PlayerState::Idle) appeared = true;  // she appeared during the intro
        if (appeared) CHECK(s.w->player().state() == PlayerState::Idle);
    }
    CHECK(appeared);
    CHECK(s.w->shownTime() > 10.0f);  // the intro of sylvanox_01 lasts about twenty seconds
    CHECK(s.w->state() == WorldState::Playing && s.w->timer().running());
    // After the start, a fall's materialisation is never held: the level goes on around Nova.
    for (int i = 0; i < 120; ++i) stepWorld(*s.w, InputFrame{}, kSimDt);
    s.w->player().kill(*s.w);
    for (int i = 0; i < 1200 && s.w->player().state() != PlayerState::Respawn; ++i) stepWorld(*s.w, InputFrame{}, kSimDt);
    CHECK(s.w->player().state() == PlayerState::Respawn);
    const float t = s.w->time(), clock = s.w->timer().elapsed();
    stepWorld(*s.w, InputFrame{}, kSimDt);
    CHECK(!s.w->startHeld());
    CHECK(s.w->time() > t);
    CHECK(s.w->timer().elapsed() > clock);
}

namespace {

/// Plays `level` with `rules` and records it as the game does (the intro watched or skipped at the
/// first step); its final digest in `finalHash`.
ReplayData record(const std::string& level, bool skip, int rules, const std::vector<InputFrame>& inputs, uint64_t& finalHash) {
    World live(level, rules);
    ReplayRecorder rec;
    rec.begin(level, std::string(64, 'a'), makeUuidV4(), live.w->gameplaySeed());
    const int start = clockStep(level, skip, rules);
    CHECK(start > 0);
    const int total = start + 1 + static_cast<int>(inputs.size());
    for (int i = 0; i < total; ++i) {
        const InputFrame f = inputAt(i, skip, start, inputs);
        rec.recordInput(f);
        stepWorld(*live.w, f, kSimDt);
        rec.tick(*live.w);
    }
    rec.finishAttempt(*live.w, live.w->timer().elapsed());
    finalHash = simulationStateHash(*live.w);
    return rec.take();
}

/// Plays a replay back in a world of today (the simulator gives it the replay's rules): true when
/// every digest matched, and the final state.
bool replays(const std::string& level, const ReplayData& replay, uint64_t finalHash, int* desyncTick = nullptr) {
    World view(level, kSimulationVersion);
    ReplaySimulator sim(replay, *view.w);
    while (sim.step()) {
    }
    if (desyncTick) *desyncTick = static_cast<int>(sim.desyncTick());
    return sim.finished() && !sim.desynced() && sim.checksPassed() == replay.checks.size() && simulationStateHash(*view.w) == finalHash;
}

}  // namespace

TEST_CASE("held start (19): the replays of before still play their run, today's too, intro watched or skipped") {
    const auto inputs = script(77, 120 * 12);
    for (bool skip : {false, true}) {
        // A run of 2.91 (rules 18): the level went on during the intro. Its replay says so and plays
        // with those rules, exactly; played with today's rules it would not be the same run.
        uint64_t oldHash = 0;
        const ReplayData old = record("sylvanox_01", skip, 18, inputs, oldHash);
        CHECK(old.simVersion == 18);
        auto back = ReplayData::decode(old.encode());
        CHECK(back.has_value());
        CHECK(back->simVersion == 18);
        CHECK(replays("sylvanox_01", *back, oldHash));
        ReplayData forced = *back;
        forced.simVersion = static_cast<uint16_t>(kSimulationVersion);
        int desync = 0;
        CHECK(!replays("sylvanox_01", forced, oldHash, &desync));
        CHECK(desync > 0);
        // A run of today: its replay carries 19 and plays back exactly.
        uint64_t newHash = 0;
        const ReplayData now = record("sylvanox_01", skip, kSimulationVersion, inputs, newHash);
        CHECK(now.simVersion == kSimulationVersion);
        auto back2 = ReplayData::decode(now.encode());
        CHECK(back2.has_value());
        CHECK(replays("sylvanox_01", *back2, newHash));
    }
}
