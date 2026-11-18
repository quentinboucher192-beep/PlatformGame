// The attack upwards and downwards (2.71, simulation 15): Up alone + Attack strikes above Nova
// (on the ground or in the air), Down alone + Attack strikes below her, in the air only (on the
// ground Down stays the crouch). The direction is the one the attack started with, and a strike
// downwards never bounces. A replay recorded before (no version of the simulation in the file)
// plays with the rules of its time: the direction read at the moment of the hit, the bounce.

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

// A check that ends the test case when it fails (what follows needs it).
#define REQUIRE(cond)        \
    do {                     \
        CHECK(cond);         \
        if (!(cond)) return; \
    } while (0)

namespace {

constexpr uint32_t bit(Action a) { return 1u << static_cast<int>(a); }

/// The frame of a direction held (dx, dy in -1..1), with Attack / Jump held or not.
InputFrame held(int dx, int dy, bool attack = false, bool jump = false) {
    InputFrame f;
    if (dx > 0) f.held |= bit(Action::MoveRight);
    if (dx < 0) f.held |= bit(Action::MoveLeft);
    if (dy < 0) f.held |= bit(Action::MoveUp);
    if (dy > 0) f.held |= bit(Action::MoveDown);
    if (attack) f.held |= bit(Action::Attack);
    if (jump) f.held |= bit(Action::Jump);
    f.moveX = static_cast<int8_t>(dx * 127);
    f.moveY = static_cast<int8_t>(dy * 127);
    return f;
}

/// Nova alone on a flat test level, played through stepWorld like the game.
struct Slash {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    InputFrame prev;

    explicit Slash(int simulation = kSimulationVersion, int teamSize = 1, bool raid = false) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_slash", "Test", "sylvanox");
        l.intro.steps.clear();
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        if (teamSize > 1) w->setTeamSize(teamSize);
        if (raid) w->setRaidMode(true);
        w->setKeysPerSlot(true);
        w->setSimulationVersion(simulation);
        w->load(l);
        w->seedGameplay(levelSeed("test_slash"));
    }
    /// The frame with its press / release edges (the previous frame of this world).
    InputFrame edges(InputFrame f) {
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        prev = f;
        return f;
    }
    void step(const InputFrame& f) { stepWorld(*w, edges(f), kSimDt); }
    void idle(int ticks) {
        for (int i = 0; i < ticks; ++i) step({});
    }
    Player& p() { return w->player(); }
};

/// Keeps an enemy where the test wants it (no fall, no knockback, no hop).
void pin(Enemy* e, const Rect& r) {
    e->box = r;
    e->vel = {};
}

}  // namespace

TEST_CASE("aimed slash: Up + Attack strikes above Nova on the ground, not the forward strike") {
    for (bool up : {true, false}) {
        Slash s;
        s.idle(150);
        Player& p = s.p();
        CHECK(p.grounded());
        Enemy* e = s.w->spawnEnemy("glowmite", p.feet() + Vec2{0.0f, -80.0f}, true, {}, 0.0f);
        REQUIRE(e != nullptr);
        // Just over her helmet, out of reach of a strike forwards.
        const Rect above = Rect::fromBottomCenter({p.center().x, p.box.y - 10.0f}, e->box.size());
        CHECK(!above.intersects(p.slashArea(SlashDirection::Forward)));
        CHECK(above.intersects(p.slashArea(SlashDirection::Up)));
        const float hp = e->hp();
        for (int i = 0; i < 30; ++i) {
            pin(e, above);
            s.step(held(0, up ? -1 : 0, i < 3));
            if (i == 0) {
                CHECK(p.attacking());
                CHECK(p.slashDirection() == (up ? SlashDirection::Up : SlashDirection::Forward));
                CHECK(p.anim.current() == (up ? "attack_up" : "attack"));
            }
        }
        if (up) CHECK(e->hp() < hp);
        else CHECK(e->hp() == hp);
        CHECK(p.state() != PlayerState::Climb);  // no ladder here: Up only aimed the strike
    }
}

TEST_CASE("aimed slash: Up + Attack strikes above Nova in the air too") {
    Slash s;
    s.idle(150);
    Player& p = s.p();
    for (int i = 0; i < 10; ++i) s.step(held(0, 0, false, true));  // a jump, rising
    CHECK(!p.grounded());
    Enemy* e = s.w->spawnEnemy("glowmite", p.feet() + Vec2{120.0f, 0.0f}, true, {}, 0.0f);
    REQUIRE(e != nullptr);
    const float hp = e->hp();
    bool struck = false;
    for (int i = 0; i < 20 && !struck; ++i) {
        pin(e, Rect::fromBottomCenter({p.center().x, p.box.y - 10.0f}, e->box.size()));
        s.step(held(0, -1, i < 3, true));
        if (i == 0) {
            CHECK(p.slashDirection() == SlashDirection::Up);
            CHECK(p.anim.current() == "air_attack_up");
        }
        struck = e->hp() < hp;
    }
    CHECK(struck);
}

TEST_CASE("aimed slash: Down + Attack strikes below in the air, without the old bounce") {
    // Rules 15 (2.71): no bounce. Rules of a replay recorded before (0, 14): the bounce (pogo).
    for (int rules : {kSimulationVersion, 0, 14}) {
        Slash s(rules);
        s.idle(150);
        Player& p = s.p();
        const Vec2 ground = p.feet();
        Enemy* e = s.w->spawnEnemy("glowmite", ground + Vec2{0.0f, -200.0f}, true, {}, 0.0f);
        REQUIRE(e != nullptr);
        p.teleportTo(ground + Vec2{0.0f, -60.0f});
        // Below her boots, floating 36 px over the ground.
        const Rect below = Rect::fromBottomCenter(ground + Vec2{0.0f, -60.0f + 24.0f}, e->box.size());
        const float hp = e->hp();
        float vyAtHit = 0.0f;
        bool struck = false;
        for (int i = 0; i < 40 && !struck; ++i) {
            pin(e, below);
            // Two steps of falling (not on the ground any more), then Down, and Attack.
            s.step(i < 2 ? InputFrame{} : held(0, 1, i < 5));
            if (i == 2) {
                CHECK(!p.grounded());
                CHECK(p.slashDirection() == SlashDirection::Down);
                CHECK(p.anim.current() == "air_attack_down");
            }
            if (e->hp() < hp) {
                struck = true;
                vyAtHit = p.vel.y;
            }
        }
        CHECK(struck);
        if (rules >= kAimedSlashSimulation) CHECK(vyAtHit > 0.0f);  // still falling
        else CHECK(vyAtHit < -100.0f);                               // bounced off the enemy
    }
}

TEST_CASE("aimed slash: on the ground Down stays the crouch, the attack strikes forwards") {
    Slash s;
    s.idle(150);
    Player& p = s.p();
    for (int i = 0; i < 20; ++i) s.step(held(0, 1));
    CHECK(p.state() == PlayerState::Crouch);
    const float crouchH = p.box.h;
    s.step(held(0, 1, true));
    CHECK(p.attacking());
    CHECK(p.slashDirection() == SlashDirection::Forward);  // never downwards on the ground
    CHECK(p.anim.current() == "attack");
    for (int i = 0; i < 60; ++i) s.step(held(0, 1, false));
    CHECK(!p.attacking());
    CHECK(p.state() == PlayerState::Crouch);  // Down still held: crouching again
    CHECK(p.box.h == crouchH);
    CHECK(p.slashDirectionFor({0.0f, 1.0f}, true) == SlashDirection::Forward);
    CHECK(p.slashDirectionFor({0.0f, 1.0f}, false) == SlashDirection::Down);
    CHECK(p.slashDirectionFor({0.0f, -1.0f}, true) == SlashDirection::Up);
    CHECK(p.slashDirectionFor({0.7071f, -0.7071f}, true) == SlashDirection::Forward);  // a diagonal: forwards
}

TEST_CASE("aimed slash: the strike keeps the direction it started with (rules 15 only)") {
    for (int rules : {kSimulationVersion, 0}) {
        Slash s(rules);
        s.idle(150);
        Player& p = s.p();
        Enemy* e = s.w->spawnEnemy("glowmite", p.feet() + Vec2{0.0f, -80.0f}, true, {}, 0.0f);
        REQUIRE(e != nullptr);
        const Rect above = Rect::fromBottomCenter({p.center().x, p.box.y - 10.0f}, e->box.size());
        const float hp = e->hp();
        // Up only on the step the attack starts, then nothing held.
        for (int i = 0; i < 30; ++i) {
            pin(e, above);
            s.step(i == 0 ? held(0, -1, true) : InputFrame{});
        }
        if (rules >= kAimedSlashSimulation) CHECK(e->hp() < hp);  // upwards, as it started
        else CHECK(e->hp() == hp);                                 // before 2.71: read at the hit (forwards)
    }
}

namespace {

/// What the test does to a world between two steps, the same in the run and in its replay:
/// an enemy pinned under Nova, Nova lifted over it.
struct PogoScene {
    Vec2 ground;
    Enemy* e = nullptr;
    void after(GameWorld& w, uint32_t tick) {
        Player& p = w.player();
        if (tick == 150) {
            ground = p.feet();
            e = w.spawnEnemy("glowmite", ground + Vec2{0.0f, -200.0f}, true, {}, 0.0f);
            p.teleportTo(ground + Vec2{0.0f, -60.0f});
        }
        if (e && tick >= 150 && tick < 200) pin(e, Rect::fromBottomCenter(ground + Vec2{0.0f, -36.0f}, e->box.size()));
    }
};

/// The input of the step after `tick` steps: two steps of falling, then Down, and Attack.
InputFrame pogoInput(uint32_t tick) {
    if (tick >= 152 && tick < 200) return held(0, 1, tick < 156);
    return {};
}

/// Plays the scene live with the given rules and records it.
ReplayData recordPogo(int rules, uint64_t& finalHash, float& vyAfterHit) {
    Slash live(rules);
    ReplayRecorder rec;
    rec.begin("test_slash", std::string(64, 'a'), makeUuidV4(), live.w->gameplaySeed());
    PogoScene scene;
    vyAfterHit = 0.0f;
    float hp = 0.0f;
    bool struck = false;
    for (uint32_t t = 1; t <= 330; ++t) {
        const InputFrame f = live.edges(pogoInput(t - 1));
        rec.recordInput(f);
        stepWorld(*live.w, f, kSimDt);
        rec.tick(*live.w);
        if (scene.e && !struck && scene.e->hp() < hp) {
            struck = true;
            vyAfterHit = live.p().vel.y;
        }
        scene.after(*live.w, t);
        if (scene.e) hp = scene.e->hp();
    }
    CHECK(struck);
    rec.finish(*live.w, live.w->timer().elapsed());
    finalHash = simulationStateHash(*live.w);
    return rec.take();
}

/// Plays a replay of the scene; true when every digest matched (and the final state).
bool replayPogo(const ReplayData& replay, uint64_t finalHash, int* desyncTick = nullptr) {
    Slash view(kSimulationVersion);  // the simulator sets the replay's own rules
    ReplaySimulator sim(replay, *view.w);
    PogoScene scene;
    uint32_t t = 0;
    while (sim.step()) scene.after(*view.w, ++t);
    if (desyncTick) *desyncTick = static_cast<int>(sim.desyncTick());
    return sim.finished() && !sim.desynced() && sim.checksPassed() == replay.checks.size() &&
           simulationStateHash(*view.w) == finalHash;
}

}  // namespace

TEST_CASE("aimed slash: a new replay plays back exactly and says its simulation") {
    uint64_t finalHash = 0;
    float vy = 0.0f;
    ReplayData data = recordPogo(kSimulationVersion, finalHash, vy);
    CHECK(vy > 0.0f);  // struck below, no bounce
    CHECK(data.simVersion == kSimulationVersion);
    auto back = ReplayData::decode(data.encode());
    REQUIRE(back.has_value());
    CHECK(back->simVersion == kSimulationVersion);
    CHECK(back->events.size() == data.events.size());  // the version is not one of its events
    for (const ReplayEvent& ev : back->events) CHECK(ev.type != ReplayEventType::Simulation);
    CHECK(replayPogo(*back, finalHash));
}

TEST_CASE("aimed slash: a replay recorded before 2.71 still reproduces its run") {
    CHECK(kSimulationVersion == 15 && kAimedSlashSimulation == 15);
    CHECK(kRankingGeneration == 8);      // the records are kept (the player asked for it)
    CHECK(kCoopSimulationVersion == 1);  // the rules of a team are the same
    // A run of 2.68 or before: the old rules, and no version in the file (simVersion 0).
    uint64_t finalHash = 0;
    float vy = 0.0f;
    ReplayData old = recordPogo(0, finalHash, vy);
    CHECK(vy < -100.0f);  // the old bounce happened
    CHECK(old.simVersion == 0);
    const auto raw = old.serialize();
    ReplayData marked = old;
    marked.simVersion = static_cast<uint16_t>(kSimulationVersion);
    CHECK(marked.serialize().size() == raw.size() + 8);  // only a new replay carries the version event
    auto back = ReplayData::parse(raw);
    REQUIRE(back.has_value());
    CHECK(back->simVersion == 0);
    CHECK(back->events.size() == old.events.size());
    CHECK(replayPogo(*back, finalHash));  // the rules of its time: exactly the same run
    // Played with today's rules it would not be the same run: the gate is needed.
    ReplayData today = *back;
    today.simVersion = static_cast<uint16_t>(kSimulationVersion);
    int desync = 0;
    CHECK(!replayPogo(today, finalHash, &desync));
    CHECK(desync > 150);
}

TEST_CASE("aimed slash: co-op and raid lockstep give every computer the same run") {
    struct Case {
        int players;
        bool raid;
    };
    for (const Case c : {Case{2, false}, Case{3, false}, Case{4, true}}) {
        // Each player: runs, jumps, strikes upwards, downwards and forwards.
        std::vector<std::vector<InputFrame>> script(120 * 20, std::vector<InputFrame>(static_cast<size_t>(c.players)));
        for (int k = 0; k < c.players; ++k) {
            Random r(77u + static_cast<uint64_t>(k) * 31u);
            InputFrame cur;
            int hold = 0;
            for (size_t i = 0; i < script.size(); ++i) {
                if (hold-- <= 0) {
                    const float x = r.unit();
                    const int dy = x < 0.3f ? -1 : (x < 0.6f ? 1 : 0);
                    cur = held(dy != 0 ? 0 : (r.chance(0.7f) ? 1 : -1), dy, r.chance(0.6f), r.chance(0.5f));
                    hold = r.rangeInt(4, 30);
                }
                script[i][static_cast<size_t>(k)] = cur;
            }
        }
        auto run = [&](Slash& t, bool* sawUp, bool* sawDown) {
            std::vector<InputFrame> prev(static_cast<size_t>(c.players));
            std::vector<uint64_t> checks;
            for (size_t i = 0; i < script.size(); ++i) {
                std::vector<InputFrame> f = script[i];
                for (size_t k = 0; k < f.size(); ++k) {
                    f[k].pressed = f[k].held & ~prev[k].held;
                    f[k].released = prev[k].held & ~f[k].held;
                    prev[k] = f[k];
                }
                stepTeam(*t.w, f.data(), static_cast<int>(f.size()), kSimDt);
                for (int k = 0; k < c.players && sawUp; ++k) {
                    const Player& q = t.w->playerAt(k);
                    if (q.attacking() && q.slashDirection() == SlashDirection::Up) *sawUp = true;
                    if (q.attacking() && q.slashDirection() == SlashDirection::Down) *sawDown = true;
                }
                if ((i + 1) % 60 == 0) checks.push_back(simulationStateHash(*t.w));
            }
            return checks;
        };
        bool up = false, down = false;
        Slash a(kSimulationVersion, c.players, c.raid);
        const auto ra = run(a, &up, &down);
        Slash b(kSimulationVersion, c.players, c.raid);
        const auto rb = run(b, nullptr, nullptr);
        CHECK(up && down);
        CHECK(ra == rb);
        CHECK(!ra.empty());
    }
}
