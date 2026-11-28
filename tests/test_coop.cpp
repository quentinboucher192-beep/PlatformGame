// 2.62: cooperation on the EXTRÊME levels (docs/COOP.md): 2 or 3 players in one world. A
// solo world plays exactly as before (tests/test_determinism.cpp keeps its digests); a team
// has its own rules and its own reference digests here (kCoopSimulationVersion).

#include <cmath>
#include <cstdio>
#include <functional>
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
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

constexpr int R = static_cast<int>(Action::MoveRight);
constexpr int L = static_cast<int>(Action::MoveLeft);
constexpr int J = static_cast<int>(Action::Jump);
constexpr int A = static_cast<int>(Action::Attack);

InputFrame frame(int dir, bool jump = false, bool attack = false) {
    InputFrame f;
    if (dir > 0) f.held |= 1u << R;
    if (dir < 0) f.held |= 1u << L;
    f.moveX = static_cast<int8_t>(dir * 127);
    if (jump) f.held |= 1u << J;
    if (attack) f.held |= 1u << A;
    return f;
}

/// A team on a flat test level (no intro), or on a level of the game.
struct Team {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::vector<InputFrame> prev;

    explicit Team(int n, const std::string& levelId = {}, int coopVersion = kCoopSimulationVersion,
                  const std::function<void(LevelData&)>& shape = {}, int rules = kSimulationVersion) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l;
        if (levelId.empty()) {
            l = LevelData::makeEmpty("test_coop", "Test", "sylvanox");
            l.intro.steps.clear();
        } else {
            auto lvl = content.loadLevel(levelId);
            CHECK(lvl.has_value());
            if (lvl) l = *lvl;
        }
        if (shape) shape(l);  // 2.77: a test level made to measure (a pit)
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->setTeamSize(n);
        w->setCoopVersion(coopVersion);  // 2.75: 1 = solid players (2.62 to 2.74), 2 = they pass through
        w->setSimulationVersion(rules);  // 2.92: 18 = the level went on during the intro (the replays of before)
        w->setKeysPerSlot(true);
        w->load(l);
        w->seedGameplay(levelSeed(levelId.empty() ? "test_coop" : levelId));
        prev.assign(static_cast<size_t>(n), InputFrame{});
    }
    /// One step: `frames` (one per player), with the press / release edges.
    void step(std::vector<InputFrame> frames) {
        frames.resize(prev.size());
        bool skip = w->cinematic().active() && w->cinematic().skippable();
        for (size_t i = 0; i < frames.size(); ++i) {
            frames[i].pressed = frames[i].held & ~prev[i].held;
            frames[i].released = prev[i].held & ~frames[i].held;
            if (skip && i == 0) frames[i].flags |= InputFrame::SkipCinematic;
            prev[i] = frames[i];
        }
        stepTeam(*w, frames.data(), static_cast<int>(frames.size()), kSimDt);
    }
    void idle(int ticks) {
        for (int i = 0; i < ticks; ++i) step({});
    }
    Player& p(int i) { return w->playerAt(i); }
    const GameWorld::TeamMember& m(int i) { return w->member(i); }
    bool overlapping() {
        for (int i = 0; i < w->teamSize(); ++i)
            for (int j = i + 1; j < w->teamSize(); ++j) {
                if (m(i).presence != GameWorld::Presence::Playing || m(j).presence != GameWorld::Presence::Playing) continue;
                if (p(i).state() == PlayerState::Hidden || p(j).state() == PlayerState::Hidden) continue;
                if (p(i).box.intersects(p(j).box)) return true;
            }
        return false;
    }
    /// Puts player `i` with its feet at `feet`, at rest.
    void place(int i, Vec2 feet) {
        p(i).teleportTo(feet);
        p(i).vel = {};
    }
};

/// Plausible play for every player (running right, jumps, attacks), each with its own stream.
std::vector<std::vector<InputFrame>> teamScript(int n, uint64_t seed, int ticks) {
    std::vector<std::vector<InputFrame>> out(static_cast<size_t>(ticks), std::vector<InputFrame>(static_cast<size_t>(n)));
    for (int k = 0; k < n; ++k) {
        Random r(seed + static_cast<uint64_t>(k) * 7919u);
        InputFrame cur;
        int hold = 0;
        for (int i = 0; i < ticks; ++i) {
            if (hold-- <= 0) {
                float x = r.unit();
                cur = frame(x < 0.75f ? 1 : (x < 0.88f ? -1 : 0), r.chance(0.45f), r.chance(0.2f));
                hold = r.rangeInt(4, 40);
            }
            out[static_cast<size_t>(i)][static_cast<size_t>(k)] = cur;
        }
    }
    return out;
}

std::string hex(uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

std::vector<uint64_t> runScript(Team& t, const std::vector<std::vector<InputFrame>>& script) {
    std::vector<uint64_t> checks;
    for (size_t i = 0; i < script.size(); ++i) {
        t.step(script[i]);
        if ((i + 1) % 60 == 0) checks.push_back(simulationStateHash(*t.w));
    }
    return checks;
}

}  // namespace

TEST_CASE("coop: a team starts side by side, never one inside another") {
    for (int n : {2, 3}) {
        Team t(n);
        t.idle(120);
        CHECK(t.w->teamSize() == n);
        CHECK(t.w->coop());
        CHECK(!t.overlapping());
        for (int i = 0; i < n; ++i) {
            CHECK(t.m(i).presence == GameWorld::Presence::Playing);
            CHECK(t.p(i).grounded());
        }
    }
    // Alone, nothing of the team exists.
    Team solo(1);
    CHECK(!solo.w->coop());
    CHECK(solo.w->teamSize() == 1);
}

TEST_CASE("coop 1 (replays of 2.62 to 2.74): players are solid for each other, and nobody is ever pushed") {
    Team t(2, {}, 1);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {700.0f, ground});
    t.place(1, {600.0f, ground});
    t.idle(10);
    const Vec2 still = t.p(0).feet();
    // Player 2 runs into player 1 for 2 s: stopped against it, player 1 does not move.
    for (int i = 0; i < 240; ++i) {
        t.step({InputFrame{}, frame(1)});
        CHECK(!t.overlapping());
    }
    CHECK(std::abs(t.p(0).feet().x - still.x) < 0.001f);
    CHECK(std::abs(t.p(0).vel.x) < 0.001f);
    CHECK(t.p(1).box.right() <= t.p(0).box.x + 0.01f);
    CHECK(t.p(0).box.x - t.p(1).box.right() < 1.0f);  // right against it
    // The same from the other side, player 1 running into player 2 (resolved in order).
    t.place(1, {800.0f, ground});
    t.idle(10);
    const Vec2 still2 = t.p(1).feet();
    for (int i = 0; i < 240; ++i) {
        t.step({frame(1), InputFrame{}});
        CHECK(!t.overlapping());
    }
    CHECK(std::abs(t.p(1).feet().x - still2.x) < 0.001f);
    CHECK(t.p(0).box.right() <= t.p(1).box.x + 0.01f);
}

TEST_CASE("coop 1 (replays of 2.62 to 2.74): landing on an ally, who carries nobody") {
    Team t(2, {}, 1);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {700.0f, ground});
    t.place(1, {700.0f, ground - 120.0f});  // above player 1's head
    t.idle(90);
    // Player 2 stands on player 1's head; player 1 was not moved.
    CHECK(std::abs(t.p(1).feet().y - t.p(0).box.y) < 0.01f);
    CHECK(t.p(1).grounded());
    CHECK(std::abs(t.p(0).feet().y - ground) < 0.01f);
    CHECK(std::abs(t.p(0).feet().x - 700.0f) < 0.01f);
    // Player 1 walks away: player 2 is not carried, loses its support and falls to the ground.
    const float x2 = t.p(1).feet().x;
    for (int i = 0; i < 120; ++i) t.step({frame(1), InputFrame{}});
    CHECK(std::abs(t.p(1).feet().x - x2) < 0.01f);
    CHECK(std::abs(t.p(1).feet().y - ground) < 0.01f);
    CHECK(!t.overlapping());
}

TEST_CASE("coop 2 (2.75): the players pass through each other, nobody pushed nor carried") {
    CHECK(kCoopSimulationVersion >= 2);
    Team t(2);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {700.0f, ground});
    t.place(1, {600.0f, ground});
    t.idle(10);
    const Vec2 still = t.p(0).feet();
    // Player 2 runs through player 1 and goes on: player 1 never moves.
    bool crossed = false;
    for (int i = 0; i < 240; ++i) {
        t.step({InputFrame{}, frame(1)});
        if (t.p(1).box.intersects(t.p(0).box)) crossed = true;
    }
    CHECK(crossed);
    CHECK(t.p(1).box.x > t.p(0).box.right());
    CHECK(std::abs(t.p(0).feet().x - still.x) < 0.001f);
    // Falling on an ally's head: through it, down to the ground.
    t.place(1, {700.0f, ground - 120.0f});
    t.idle(90);
    CHECK(std::abs(t.p(1).feet().y - ground) < 0.01f);
    CHECK(std::abs(t.p(0).feet().y - ground) < 0.01f);
    // A team still starts side by side (never one inside another).
    Team u(3);
    u.idle(60);
    CHECK(!u.overlapping());
}

TEST_CASE("coop: attacks and powers never hurt an ally") {
    Team t(2);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {700.0f, ground});
    t.place(1, {716.0f, ground});
    t.idle(5);
    const int h1 = t.p(1).health;
    for (int i = 0; i < 360; ++i) t.step({frame(i % 40 < 20 ? 1 : -1, false, (i / 8) % 2 == 0), frame(0, false, (i / 7) % 2 == 0)});
    CHECK(t.p(1).health == h1);
    CHECK(t.p(0).health == t.p(0).maxHealth());
}

TEST_CASE("coop: an enemy aims at the nearest player in play") {
    Team t(3);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {300.0f, ground});
    t.place(1, {900.0f, ground});
    t.place(2, {1500.0f, ground});
    CHECK(t.w->targetIndex({880.0f, ground - 10.0f}) == 1);
    CHECK(t.w->targetIndex({100.0f, ground - 10.0f}) == 0);
    CHECK(t.w->targetIndex({1450.0f, ground - 10.0f}) == 2);
    // Equally near: the lowest number.
    CHECK(t.w->targetIndex({600.0f, ground - 11.0f}) == 0);
    // A player in standby is never aimed at.
    std::vector<InputFrame> f(3);
    f[1].flags = InputFrame::Standby;
    t.step(f);
    CHECK(t.m(1).presence == GameWorld::Presence::Standby);
    CHECK(t.w->targetIndex({880.0f, ground - 10.0f}) != 1);
}

TEST_CASE("coop: a player down is revived by an ally holding « Interagir » 2 s near its soul (rules 3, 2.77), 2 per attempt") {
    InputFrame hold;
    hold.held = 1u << static_cast<int>(Action::Interact);
    auto holding = [&](Team& t, int ticks) {
        for (int i = 0; i < ticks; ++i) t.step({hold, InputFrame{}});
    };
    Team t(2);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {500.0f, ground});
    t.place(1, {900.0f, ground});
    t.idle(5);
    for (int round = 0; round < 3; ++round) {
        {
            GameWorld::PlayerScope s(*t.w, 1);
            t.p(1).kill(*t.w);
        }
        t.idle(150);  // its fall
        CHECK(t.m(1).presence == GameWorld::Presence::Down);
        CHECK(t.w->state() == WorldState::Playing);  // the other one is still in play
        const Vec2 soul = t.m(1).soul;
        // Far from the soul: nothing happens.
        t.place(0, {soul.x - 300.0f, ground});
        t.idle(300);
        CHECK(t.m(1).presence == GameWorld::Presence::Down);
        // Near it without « Interagir »: nothing (rules 3).
        t.place(0, {soul.x - 20.0f, ground});
        t.idle(300);
        CHECK(t.m(1).presence == GameWorld::Presence::Down);
        CHECK(t.m(1).revive == 0.0f);
        // Near it, held 1 s, then away: the count starts again.
        holding(t, 120);
        CHECK(t.m(1).presence == GameWorld::Presence::Down);
        t.place(0, {soul.x - 300.0f, ground});
        holding(t, 2);
        CHECK(t.m(1).revive == 0.0f);
        t.place(0, {soul.x - 20.0f, ground});
        holding(t, 250);
        if (round < 2) {
            CHECK(t.m(1).presence == GameWorld::Presence::Playing);
            CHECK(t.w->revivesLeft() == 1 - round);
            CHECK(t.p(1).health == (t.p(1).maxHealth() + 1) / 2);
            CHECK(!t.overlapping());
            t.idle(120);
            CHECK(!t.overlapping());
        } else {
            CHECK(t.m(1).presence == GameWorld::Presence::Down);  // no revival left
            CHECK(t.w->revivesLeft() == 0);
        }
    }
}

TEST_CASE("coop 3 (2.77): killed in mid-jump, the soul falls to the ground under it; over the void or too high, at the last safe place; revived") {
    InputFrame hold;
    hold.held = 1u << static_cast<int>(Action::Interact);
    // The flat level with a pit from x 1000 to 1400, down to the bottom of the level.
    Team t(2, {}, kCoopSimulationVersion, [](LevelData& l) {
        for (LevelObject& o : l.objects)
            if (o.id == "o1") o.rect = {0.0f, 800.0f, 1000.0f, 160.0f};
        LevelObject far;
        far.id = "o9";
        far.type = "solid";
        far.layer = "terrain";
        far.rect = {1400.0f, 800.0f, 1000.0f, 160.0f};
        far.props = {{"material", "ruin_stone"}};
        l.objects.push_back(far);
    });
    t.idle(60);
    const float ground = t.p(0).feet().y;
    CHECK(std::abs(ground - 800.0f) < 0.5f);
    t.place(0, {500.0f, ground});
    t.place(1, {700.0f, ground});
    t.idle(10);
    CHECK(std::abs(t.m(1).safeFeet.x - 700.0f) < 1.0f);
    auto killAt = [&](Vec2 feet) {
        t.place(1, feet);
        GameWorld::PlayerScope s(*t.w, 1);
        t.p(1).kill(*t.w);
    };
    auto revived = [&]() {
        const Vec2 soul = t.m(1).soul;
        t.place(0, {soul.x - 20.0f, soul.y});
        for (int k = 0; k < 2 * kSimHz + 60 && t.m(1).presence == GameWorld::Presence::Down; ++k) t.step({hold, InputFrame{}});
        return t.m(1).presence == GameWorld::Presence::Playing;
    };
    // 1. Killed at the top of a jump over the ground, 200 px from where it last stood: the soul falls under it.
    killAt({900.0f, ground - 70.0f});
    t.idle(150);
    CHECK(t.m(1).presence == GameWorld::Presence::Down);
    std::printf("  mort en (900, %.0f) : âme en (%.0f, %.0f)\n", ground - 70.0f, t.m(1).soul.x, t.m(1).soul.y);
    CHECK(std::abs(t.m(1).soul.x - 900.0f) < 0.5f);
    CHECK(std::abs(t.m(1).soul.y - ground) < 0.5f);
    CHECK(revived());
    t.idle(30);
    // 2. Killed over the pit: its soul at its last safe place (the ground where it stood).
    const Vec2 safe = t.m(1).safeFeet;
    CHECK(safe.x < 1000.0f);
    killAt({1200.0f, ground - 40.0f});
    t.idle(150);
    CHECK(t.m(1).presence == GameWorld::Presence::Down);
    std::printf("  mort au-dessus du vide (1200, %.0f) : âme en (%.0f, %.0f), dernier endroit sûr (%.0f, %.0f)\n", ground - 40.0f, t.m(1).soul.x,
                t.m(1).soul.y, safe.x, safe.y);
    CHECK(std::abs(t.m(1).soul.x - safe.x) < 0.5f);
    CHECK(std::abs(t.m(1).soul.y - safe.y) < 0.5f);
    CHECK(revived());
    t.idle(30);
    // 3. Killed over the ground but more than 6 tiles above it: the last safe place too (no revival left to try).
    const Vec2 safe2 = t.m(1).safeFeet;
    killAt({600.0f, ground - 200.0f});
    t.idle(150);
    CHECK(t.m(1).presence == GameWorld::Presence::Down);
    CHECK(std::abs(t.m(1).soul.x - safe2.x) < 0.5f);
    CHECK(std::abs(t.m(1).soul.y - safe2.y) < 0.5f);
}

TEST_CASE("coop: everybody down at once ends the attempt") {
    Team t(3);
    t.idle(60);
    for (int i = 0; i < 3; ++i) {
        GameWorld::PlayerScope s(*t.w, i);
        t.p(i).kill(*t.w);
    }
    t.idle(200);
    CHECK(t.w->state() == WorldState::Failed);
}

TEST_CASE("coop 1: a player in standby is not solid, comes back near the team, or is gone after 60 s") {
    Team t(2, {}, 1);  // the overlap checks are the solid players' (cooperation 2: they pass through anyway)
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {700.0f, ground});
    t.place(1, {900.0f, ground});
    t.idle(5);
    std::vector<InputFrame> away(2);
    away[1].flags = InputFrame::Standby;
    for (int i = 0; i < 120; ++i) t.step(away);
    CHECK(t.m(1).presence == GameWorld::Presence::Standby);
    // Player 1 walks through where player 2 stopped: not solid any more.
    for (int i = 0; i < 240; ++i) t.step({frame(1), away[1]});
    CHECK(t.p(0).box.x > t.p(1).box.right());  // right through it
    // Back within 60 s: near the team, never inside an ally.
    t.idle(1);
    CHECK(t.m(1).presence == GameWorld::Presence::Playing);
    CHECK(std::abs(t.p(1).feet().x - t.p(0).feet().x) < 120.0f);
    CHECK(!t.overlapping());
    t.idle(120);
    CHECK(!t.overlapping());
    CHECK(!t.w->teamIncomplete());
    // Away for more than 60 s: gone, the team goes on, « incomplète ».
    for (int i = 0; i < GameWorld::kStandbyLimitTicks + 5; ++i) t.step(away);
    CHECK(t.m(1).presence == GameWorld::Presence::Gone);
    CHECK(t.w->teamIncomplete());
    CHECK(t.w->state() == WorldState::Playing);
    t.idle(10);  // coming back now changes nothing
    CHECK(t.m(1).presence == GameWorld::Presence::Gone);
}

TEST_CASE("coop: a player out of the team's frame for 3 s comes back near the nearest ally") {
    Team t(3);
    t.idle(60);
    const float ground = t.p(0).feet().y;
    t.place(0, {300.0f, ground});
    t.place(1, {340.0f, ground});
    t.place(2, {2000.0f, ground});  // far beyond the frame
    t.idle(2);
    CHECK(!t.m(2).inFrame);
    CHECK(t.m(0).inFrame && t.m(1).inFrame);
    t.idle(400);  // more than 3 s
    CHECK(t.m(2).regrouped == 1);
    CHECK(std::abs(t.p(2).feet().x - 340.0f) < 100.0f);
    CHECK(!t.overlapping());
    // Two players alone: the frame keeps player 1's side.
    Team two(2);
    two.idle(60);
    two.place(0, {300.0f, ground});
    two.place(1, {2000.0f, ground});
    two.idle(2);
    CHECK(two.m(0).inFrame);
    CHECK(!two.m(1).inFrame);
}

TEST_CASE("coop: same inputs, same team run; reference digests") {
    struct Golden {
        int players;
        const char* digest;
        int version;
        int rules = 18;  // the simulation's (kSimulationVersion)
    };
    // 2.75: version 1 (solid players: the replays of 2.62 to 2.74) keeps its digests exactly;
    // version 2 (they pass through each other) has its own.
    // 2.77: version 3 (a soul on the last safe place, a revival held) has its own; 1 and 2 keep theirs.
    // 2.80: version 4 (a passage that closes behind the team waits for every player): the same as 3 here (no
    // door closes in these 40 s of sylvanox_01); its own digests where a door waits: tests/test_coop_passages.cpp.
    // 2.92 (simulation 19): the level waits for the clock at its start (the team's materialisation after the
    // skipped intro, GameWorld::startHeld): the team of today has its own; with the rules of 18 (the replays of
    // before) every version keeps its digests.
    const Golden golden[] = {{2, "bb2676ab728f2c52", 1}, {3, "2ac42252542c66bd", 1}, {2, "91d4357955571446", 2}, {3, "c965b1d603d7a5d0", 2},
                             {2, "f7e9af3feb027a98", 3},  {3, "ccb9426b84949f0b", 3},  {2, "f7e9af3feb027a98", 4}, {3, "ccb9426b84949f0b", 4},
                             {2, "0bd106b842078fce", 4, 19},  {3, "8712b2ce21f03f39", 4, 19}};
    int mismatches = 0;
    for (const auto& g : golden) {
        auto script = teamScript(g.players, 31, 120 * 40);
        Team a(g.players, "sylvanox_01", g.version, {}, g.rules);
        auto ra = runScript(a, script);
        Team b(g.players, "sylvanox_01", g.version, {}, g.rules);
        auto rb = runScript(b, script);
        CHECK(ra == rb);
        std::string got = ra.empty() ? "" : hex(ra.back());
        if (got != g.digest) {
            std::printf("  équipe de %d, coop %d, règles %d (sylvanox_01, 40 s) : %s (attendu %s)\n", g.players, g.version, g.rules,
                        got.c_str(), g.digest);
            ++mismatches;
        }
        // Another input of player 2 changes the run.
        auto other = script;
        other[200][1].held ^= 1u << J;
        Team c(g.players, "sylvanox_01", g.version, {}, g.rules);
        CHECK(runScript(c, other) != ra);
    }
    CHECK(kSimulationVersion == 20);
    CHECK(mismatches == 0);
    CHECK(kCoopSimulationVersion == 4);
}

TEST_CASE("coop and raid (2.84): a power picked up is celebrated on its player's computer only, never on the others'") {
    // Display only: the same simulation everywhere (the power granted, the tablet), only what each computer shows
    // differs. Player 2 (index 1) picks the tablet up; player 1's computer shows no « Nouveau pouvoir », no flash.
    for (bool raid : {false, true}) {
        Team t(2, {}, kCoopSimulationVersion, [](LevelData& l) {
            LevelObject o;
            o.id = "tab";
            o.type = "power_unlock";
            o.layer = "gameplay";
            o.rect = {900.0f, 200.0f, 32.0f, 32.0f};
            o.props = Json{{"power", "dash"}};
            l.objects.push_back(o);
        });
        t.w->setRaidMode(raid);
        LevelEntity* tab = t.w->entity("tab");
        CHECK(tab != nullptr);
        if (!tab) return;
        t.idle(30);
        t.w->viewers = 1ull;  // the computer of player 1 (bit 0)
        t.p(1).teleportTo({tab->box.center().x, tab->box.bottom()});
        bool got = false;
        for (int i = 0; i < 20 && !got; ++i) {
            t.step({InputFrame{}, InputFrame{}});
            got = t.p(1).hasPower("dash");
        }
        CHECK(got);
        CHECK(!t.p(0).hasPower("dash"));
        const WorldMessage* title = nullptr;
        for (const WorldMessage& m : t.w->messages())
            if (m.style == WorldMessage::Style::Title) title = &m;
        CHECK(title != nullptr);
        if (title) {
            CHECK(title->player == 1);
            CHECK(!t.w->shownHere(*title));  // not on player 1's computer
        }
        CHECK(t.w->flashColor().a == 0);  // nor its flash
        t.w->viewers = 2ull;  // on player 2's computer (bit 1): there
        if (title) CHECK(t.w->shownHere(*title));
        t.w->viewers = ~0ull;  // one computer for both (same PC): there
        if (title) CHECK(t.w->shownHere(*title));
    }
}
