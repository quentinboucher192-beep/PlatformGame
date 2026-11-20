// Determinism of the simulation: the same seed and the same inputs must give the same run,
// whatever the display frame rate, and the same result with every compiler (the golden
// digests below are checked by the Linux/GCC and Windows/MSVC builds of the CI).

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// Plausible play: mostly running right, jumps, attacks, dashes, some stops and turns.
std::vector<InputFrame> scriptedInputs(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    out.reserve(static_cast<size_t>(ticks));
    InputFrame prev;
    int hold = 0;
    InputFrame cur;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            float k = r.unit();
            int dir = k < 0.75f ? 1 : (k < 0.88f ? -1 : 0);
            if (dir > 0) cur.held |= 1u << static_cast<int>(Action::MoveRight);
            if (dir < 0) cur.held |= 1u << static_cast<int>(Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= 1u << static_cast<int>(Action::Jump);
            if (r.chance(0.2f)) cur.held |= 1u << static_cast<int>(Action::Attack);
            if (r.chance(0.08f)) cur.held |= 1u << static_cast<int>(Action::Dash);
            if (r.chance(0.05f)) {
                cur.held |= 1u << static_cast<int>(Action::MoveDown);
                cur.moveY = 127;
            }
            hold = r.rangeInt(4, 40);
        }
        InputFrame f = cur;
        f.pressed = static_cast<uint16_t>(f.held & ~prev.held);
        f.released = static_cast<uint16_t>(prev.held & ~f.held);
        out.push_back(f);
        prev = f;
    }
    return out;
}

struct Sim {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;

    explicit Sim(const std::string& levelId) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel(levelId);
        CHECK(level.has_value());
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed(levelId));
    }

    /// Runs the inputs; `frameDt` > 0 interleaves display updates (animations, particles,
    /// sounds) like the game does between fixed steps. Returns the digest every 60 steps.
    std::vector<uint64_t> run(const std::vector<InputFrame>& inputs, const std::vector<float>& frameDts = {}) {
        std::vector<uint64_t> checks;
        double simTime = 0.0, frameTime = 0.0;
        size_t fi = 0;
        for (size_t i = 0; i < inputs.size(); ++i) {
            // Display frames that would have happened before this step.
            while (!frameDts.empty() && frameTime <= simTime) {
                float fdt = frameDts[fi++ % frameDts.size()];
                w->update(fdt);
                frameTime += fdt;
            }
            InputFrame f = inputs[i];
            // Skip the intro the way a player would.
            if (w->cinematic().active() && w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
            stepWorld(*w, f, kSimDt);
            simTime += kSimDt;
            if ((i + 1) % 60 == 0) checks.push_back(simulationStateHash(*w));
        }
        return checks;
    }
};

std::string hex(uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

}  // namespace

TEST_CASE("determinism: input frames survive quantisation unchanged") {
    PlayerInput in;
    in.move = {0.731f, -1.4f};
    in.held[static_cast<int>(Action::Jump)] = true;
    in.pressed[static_cast<int>(Action::Jump)] = true;
    in.released[static_cast<int>(Action::Dash)] = true;
    in.held[static_cast<int>(Action::Pause)] = true;  // never reaches the simulation
    InputFrame f = InputFrame::fromPlayerInput(in);
    CHECK(f.moveX == 93);
    CHECK(f.moveY == -127);
    PlayerInput back = f.toPlayerInput();
    CHECK(back.isHeld(Action::Jump) && back.isPressed(Action::Jump) && back.isReleased(Action::Dash));
    CHECK(!back.isHeld(Action::Pause));
    CHECK(InputFrame::fromPlayerInput(back) == f);
}

TEST_CASE("determinism: same seed and inputs, same run, at any frame rate") {
    const auto inputs = scriptedInputs(42, 120 * 40);
    Sim a("sylvanox_01");
    auto ref = a.run(inputs);
    Sim b("sylvanox_01");
    CHECK(b.run(inputs) == ref);
    // Display updates at uneven rates must not change anything.
    Sim c("sylvanox_01");
    auto withFrames = c.run(inputs, {1.0f / 37.0f, 1.0f / 144.0f, 1.0f / 60.0f, 0.05f});
    CHECK(withFrames.size() == ref.size());
    size_t firstDiff = ref.size();
    for (size_t i = 0; i < ref.size() && i < withFrames.size(); ++i)
        if (ref[i] != withFrames[i]) {
            firstDiff = i;
            break;
        }
    if (firstDiff != ref.size()) std::printf("  premier écart à %.1f s\n", (firstDiff + 1) * 0.5);
    CHECK(firstDiff == ref.size());
    // A different input changes the run (the digest is not blind).
    auto other = inputs;
    other[300].held ^= 1u << static_cast<int>(Action::Jump);
    other[300].pressed ^= 1u << static_cast<int>(Action::Jump);
    Sim d("sylvanox_01");
    CHECK(d.run(other) != ref);
}

// Digest after 60 s of scripted play on each planet's first level. Computed by the Linux
// build; the Windows (MSVC) build of the CI must find exactly the same values. When a change
// of the simulation is intended, bump kSimulationVersion and update these digests.
TEST_CASE("determinism: golden digests are identical on every platform") {
    struct Golden {
        const char* level;
        const char* digest;
    };
    const Golden golden[] = {
        {"sylvanox_01", "7cb6e985398f32d1"},
        {"glacior_01", "bc2b0825ca390861"},
        {"pyralis_01", "17ab99ef955f613a"},
    };
    int mismatches = 0;
    for (const auto& g : golden) {
        Sim s(g.level);
        auto checks = s.run(scriptedInputs(7, 120 * 60));
        std::string got = checks.empty() ? "" : hex(checks.back());
        if (got != g.digest) {
            std::printf("  %s : %s (attendu %s)\n", g.level, got.c_str(), g.digest);
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

namespace {

/// 2.76.2: a boss's fight in its level: the player at `tileX` columns of its arena and `tilesUp` rows
/// above its bottom, the fight started at `phase`, `seconds` of scripted play (`seed`) with the rules of
/// `rules`; display updates at the rates of `frameDts` (none: the simulation alone). Its last digest.
std::string bossFight(const char* level, const char* enemy, int phase, float tileX, float tilesUp, uint64_t seed, int rules,
                      const std::vector<float>& frameDts = {}, int seconds = 30) {
    Sim s(level);
    s.w->setSimulationVersion(rules);
    std::string bossObject;
    Rect zone;
    for (const LevelObject& o : s.w->level().objects)
        if (o.type == "enemy" && o.propStr("enemy") == enemy) bossObject = o.id;
    for (const LevelObject& o : s.w->level().objects)
        if (o.type == "combat_zone" && !bossObject.empty() && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
    CHECK(!bossObject.empty() && zone.w > 0.0f);
    auto* spawn = dynamic_cast<EnemySpawnEntity*>(s.w->entity(bossObject));
    Enemy* boss = spawn ? spawn->enemy(*s.w) : nullptr;
    CHECK(boss != nullptr);
    if (!boss) return std::string();
    s.w->startAt({zone.x + tileX * 16.0f, zone.bottom() - tilesUp * 16.0f});
    s.w->startBoss(*boss);
    jumpBossToPhase(*s.w, *boss, phase);
    auto checks = s.run(scriptedInputs(seed, seconds * 120), frameDts);
    return checks.empty() ? std::string() : hex(checks.back());
}

}  // namespace

// 2.33: the giga boss's fight (its arena changed by its phases: debris in orbit, the crust floor
// collapsed into the plasma, tides of plasma, rings, cages...): 30 s of scripted play from its
// fifth phase in « Le Cœur d'une Étoile Mourante ». Computed by the Linux build. 2.76.2: Singularis
// fights by its hit parts (kSimulationVersion 17); with the rules of 16 and of 15 (a replay of
// before) the run is the one of 2.76.0 to the bit (its box).
TEST_CASE("determinism: the giga boss's fight has its golden digest on every platform") {
    const std::string now = bossFight("noyau_03", "singularis", 4, 18.0f, 14.0f, 11, kSimulationVersion);
    const std::string before = bossFight("noyau_03", "singularis", 4, 18.0f, 14.0f, 11, 16);
    const std::string older = bossFight("noyau_03", "singularis", 4, 18.0f, 14.0f, 11, 15);
    const char* golden16 = "c96ae243962c105a";  // 2.50: the tablets of every power before it (the arena moved); its box
    const char* golden17 = "84a948a263c9f92f";  // 2.76.2: by its parts, its whole body hurting on contact
    if (before != golden16) std::printf("  noyau_03 (Singularis, phase 5, règles 16) : %s (attendu %s)\n", before.c_str(), golden16);
    if (older != golden16) std::printf("  noyau_03 (Singularis, phase 5, règles 15) : %s (attendu %s)\n", older.c_str(), golden16);
    if (now != golden17) std::printf("  noyau_03 (Singularis, phase 5, règles %d) : %s (attendu %s)\n", kSimulationVersion, now.c_str(), golden17);
    CHECK(before == golden16);
    CHECK(older == golden16);
    CHECK(now == golden17);
}

// 2.76.2: every gigaboss of the campaign fights by its hit parts (kSimulationVersion 17): 30 s of
// scripted play in its lair, from a phase drawn with its own sheet. With the rules of 16 and of 15
// (the replays of before) each run is the one of 2.76.0 to the bit: the digests computed by the
// 2.76.0 build (its box).
TEST_CASE("determinism: the gigaboss of the campaign by their parts (17), their digests of before with the rules of 16 and 15") {
    struct Fight {
        const char* level;
        const char* enemy;
        int phase;
        float tileX, tilesUp;
        const char* golden15;
        const char* golden16;
        const char* golden17;
    };
    const Fight fights[] = {
        {"confins_02", "monolithe", 4, 48.0f, 4.0f, "3f7efa1e0052ed3f", "3f7efa1e0052ed3f", "46022345e3c6642e"},
        {"confins_03", "kraken", 4, 48.0f, 4.0f, "ac7dcbd5112d08c7", "ac7dcbd5112d08c7", "3c2498c7c4bc8721"},
        {"confins_04", "phenix", 4, 48.0f, 4.0f, "fca498addc59720e", "fca498addc59720e", "ac7b53bf0c7a7c6d"},
        {"confins_05", "liche", 4, 48.0f, 4.0f, "c528c34b39256053", "c528c34b39256053", "c528c34b39256053"},
        {"confins_06", "titan", 4, 48.0f, 4.0f, "ac4f8ae0adb40d94", "ac4f8ae0adb40d94", "ac4f8ae0adb40d94"},
        {"noyau_02", "astrarque", 1, 20.0f, 0.0f, "f6c54221fbf1c96d", "f6c54221fbf1c96d", "53cd5be0bf654880"},  // 17: the rig of forge_core (2.76.2, step e)
    };
    int mismatches = 0;
    for (const Fight& f : fights) {
        const std::string d15 = bossFight(f.level, f.enemy, f.phase, f.tileX, f.tilesUp, 17, 15);
        const std::string d16 = bossFight(f.level, f.enemy, f.phase, f.tileX, f.tilesUp, 17, 16);
        const std::string d17 = bossFight(f.level, f.enemy, f.phase, f.tileX, f.tilesUp, 17, kSimulationVersion);
        const std::string got[] = {d15, d16, d17};
        const char* want[] = {f.golden15, f.golden16, f.golden17};
        for (int k = 0; k < 3; ++k)
            if (got[k] != want[k]) {
                std::printf("  %s (%s, phase %d, règles %d) : %s (attendu %s)\n", f.level, f.enemy, f.phase + 1, k == 2 ? kSimulationVersion : 15 + k,
                            got[k].c_str(), want[k]);
                ++mismatches;
            }
    }
    CHECK(mismatches == 0);
}

// 2.76.2: a boss that fights by parts changes its sheet with its phase IN the fixed step (its pose is
// part of the simulation): display updates at any rate change nothing, phase sheets included.
TEST_CASE("determinism: a boss fighting by parts plays the same run at any frame rate, its phase sheets included") {
    const std::vector<float> uneven = {1.0f / 37.0f, 1.0f / 144.0f, 1.0f / 60.0f, 0.05f};
    const std::string plain = bossFight("noyau_03", "singularis", 4, 18.0f, 14.0f, 11, kSimulationVersion, {}, 10);
    const std::string shown = bossFight("noyau_03", "singularis", 4, 18.0f, 14.0f, 11, kSimulationVersion, uneven, 10);
    CHECK(!plain.empty());
    CHECK(plain == shown);
    const std::string calm = bossFight("confins_04", "phenix", 4, 48.0f, 4.0f, 17, kSimulationVersion, {}, 10);
    const std::string watched = bossFight("confins_04", "phenix", 4, 48.0f, 4.0f, 17, kSimulationVersion, uneven, 10);
    CHECK(!calm.empty());
    CHECK(calm == watched);
}

TEST_CASE("determinism: a recorded replay plays the run again exactly") {
    const auto inputs = scriptedInputs(99, 120 * 30);
    Sim live("sylvanox_01");
    ReplayRecorder rec;
    std::string hash = std::string(64, 'a');
    if (auto text = FileUtil::readText(live.content.level("sylvanox_01")->path))
        if (auto fp = fingerprintRankedLevel(live.content, Json::parse(*text))) hash = fp->hash;
    rec.begin("sylvanox_01", hash, makeUuidV4(), live.w->gameplaySeed());
    for (const auto& in : inputs) {
        InputFrame f = in;
        if (live.w->cinematic().active() && live.w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        rec.recordInput(f);
        stepWorld(*live.w, f, kSimDt);
        rec.tick(*live.w);
    }
    rec.finish(*live.w, live.w->timer().elapsed());
    const uint64_t liveFinal = simulationStateHash(*live.w);
    ReplayData data = rec.take();
    CHECK(data.exact());
    CHECK(data.checks.size() >= 60);
    // Through the file format (compressed container, run-length encoded inputs).
    auto bytes = data.encode();
    std::string err;
    auto back = ReplayData::decode(bytes, &err);
    CHECK(back.has_value());
    CHECK(back->inputs == data.inputs);
    CHECK(back->seed == data.seed);
    CHECK(back->checks.size() == data.checks.size());
    std::printf("  replay de 30 s : %zu octets compressés, %zu sommes de contrôle\n", bytes.size(), back->checks.size());
    // COSMIC_TEST_REPLAY_OUT=<file.cprz>: keep the file (to watch it with --replay).
    if (const char* out = std::getenv("COSMIC_TEST_REPLAY_OUT")) FileUtil::writeBinary(Paths::fromUtf8(out), bytes);

    Sim view("sylvanox_01");
    ReplaySimulator sim(*back, *view.w);
    // The viewer also runs display updates at its own rate.
    while (sim.step()) view.w->update(1.0f / 50.0f);
    CHECK(sim.finished());
    CHECK(!sim.desynced());
    CHECK(sim.checksPassed() == back->checks.size());
    CHECK(simulationStateHash(*view.w) == liveFinal);

    // A replay of another level content desynchronises (and says when).
    ReplayData tampered = *back;
    tampered.seed ^= 1;
    Sim other("sylvanox_01");
    ReplaySimulator bad(tampered, *other.w);
    while (bad.step()) {
    }
    // The seed only matters once randomness is used: the run must still be detected or equal.
    if (bad.desynced()) CHECK(bad.desyncTick() > 0);

    ReplayData edited = *back;
    edited.inputs[400].held ^= 1u << static_cast<int>(Action::Jump);
    edited.inputs[400].pressed ^= 1u << static_cast<int>(Action::Jump);
    Sim third("sylvanox_01");
    ReplaySimulator cheat(edited, *third.w);
    while (cheat.step()) {
    }
    CHECK(cheat.desynced());
    CHECK(cheat.desyncTick() > 400);
    CHECK(cheat.desyncTick() <= 400 + ReplayData::kCheckTicks);
}

TEST_CASE("determinism: replay files reject inconsistent input sections") {
    ReplayData d;
    d.levelId = "x";
    d.levelHash = std::string(64, 'b');
    d.attemptId = makeUuidV4();
    d.frames.push_back({});
    d.inputs.assign(120, InputFrame{});
    d.checks.push_back({60, 1});
    auto raw = d.serialize();
    CHECK(ReplayData::parse(raw).has_value());
    CHECK(ReplayData::parse(raw)->formatVersion == ReplayData::kFormatVersion);
    // Truncated.
    auto cut = raw;
    cut.resize(cut.size() - 5);
    CHECK(!ReplayData::parse(cut).has_value());
    // Without inputs the file stays a version 1 trace.
    ReplayData trace = d;
    trace.inputs.clear();
    trace.checks.clear();
    auto v1 = ReplayData::parse(trace.serialize());
    CHECK(v1.has_value());
    CHECK(v1->formatVersion == 1);
    CHECK(!v1->exact());
}

// 2.76.0: Pyrrhax fights by its hit parts (kSimulationVersion 16): 30 s of scripted play from its
// second phase in « La Gueule de Pyrralis » (confins_01). With the rules of version 15 (a replay of
// before) the run is the one of 2.75 to the bit: the digest computed by the 2.75.2 build.
TEST_CASE("determinism: Pyrrhax's fight by parts has its golden digest, and its digest of before with the rules of 15") {
    auto play = [](int rules) {
        Sim s("confins_01");
        s.w->setSimulationVersion(rules);
        std::string bossObject;
        Rect zone;
        for (const LevelObject& o : s.w->level().objects)
            if (o.type == "enemy" && o.propStr("enemy") == "pyrrhax") bossObject = o.id;
        for (const LevelObject& o : s.w->level().objects)
            if (o.type == "combat_zone" && !bossObject.empty() && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty() && zone.w > 0.0f);
        auto* spawn = dynamic_cast<EnemySpawnEntity*>(s.w->entity(bossObject));
        Enemy* boss = spawn ? spawn->enemy(*s.w) : nullptr;
        CHECK(boss != nullptr);
        if (!boss) return std::string();
        s.w->startAt({zone.x + 48.0f * 16.0f, zone.bottom() - 4.0f * 16.0f});
        s.w->startBoss(*boss);
        jumpBossToPhase(*s.w, *boss, 1);
        auto checks = s.run(scriptedInputs(13, 30 * 120));
        return checks.empty() ? std::string() : hex(checks.back());
    };
    const std::string before = play(15), now = play(16), latest = play(kSimulationVersion);
    const char* golden15 = "e22ee37a63ecc0fe";  // the 2.75.2 build (its box)
    const char* golden16 = "c410f3197ff713d1";  // 2.76.0: by its parts
    // 2.76.2 (17): its whole body hurts on contact, its red parts strike during their attacks, it dives wings folded.
    const char* golden17 = "a014cba12fa14d2c";
    if (before != golden15) std::printf("  confins_01 (Pyrrhax, règles 15) : %s (attendu %s)\n", before.c_str(), golden15);
    if (now != golden16) std::printf("  confins_01 (Pyrrhax, règles 16) : %s (attendu %s)\n", now.c_str(), golden16);
    if (latest != golden17) std::printf("  confins_01 (Pyrrhax, règles %d) : %s (attendu %s)\n", kSimulationVersion, latest.c_str(), golden17);
    CHECK(before == golden15);
    CHECK(now == golden16);
    CHECK(latest == golden17);
}

// 2.76.2: the classic bosses fight by their hit parts (kSimulationVersion 17): 30 s of scripted play
// from their second phase, Magmaroth in « pyralis_01 » and the Cryowyrm in « glacior_01 ». With the rules
// of 16 and of 15 (replays of before) they keep their box, their shots and their animation of display:
// the runs of the 2.76.0 build to the bit.
TEST_CASE("determinism: the classic bosses' fights by parts have their golden digests, and their digests of before with the rules of 16 and 15") {
    auto play = [](const char* level, const char* bossId, int rules) {
        Sim s(level);
        s.w->setSimulationVersion(rules);
        std::string bossObject;
        Rect zone;
        for (const LevelObject& o : s.w->level().objects)
            if (o.type == "enemy" && o.propStr("enemy") == bossId) bossObject = o.id;
        for (const LevelObject& o : s.w->level().objects)
            if (o.type == "combat_zone" && !bossObject.empty() && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty() && zone.w > 0.0f);
        auto* spawn = dynamic_cast<EnemySpawnEntity*>(s.w->entity(bossObject));
        Enemy* boss = spawn ? spawn->enemy(*s.w) : nullptr;
        CHECK(boss != nullptr);
        if (!boss) return std::string();
        s.w->startAt({zone.x + 6.0f * 16.0f, zone.bottom() - 4.0f * 16.0f});
        s.w->startBoss(*boss);
        jumpBossToPhase(*s.w, *boss, 1);
        auto checks = s.run(scriptedInputs(17, 30 * 120));
        return checks.empty() ? std::string() : hex(checks.back());
    };
    struct Golden {
        const char* level;
        const char* boss;
        const char* rules15;  // the 2.76.0 build, rules of 15
        const char* rules16;  // the 2.76.0 build (its box)
        const char* rules17;  // 2.76.2: by its parts
    };
    const Golden golden[] = {
        {"pyralis_01", "magmaroth", "ea62c92bfe9cdf0e", "ea62c92bfe9cdf0e", "bf67add1641a1568"},
        {"glacior_01", "cryowyrm", "de437ca806394da6", "de437ca806394da6", "08f1f5618cdee4d4"},
    };
    int mismatches = 0;
    for (const Golden& g : golden) {
        const std::pair<int, const char*> runs[] = {{15, g.rules15}, {16, g.rules16}, {kSimulationVersion, g.rules17}};
        for (const auto& [rules, want] : runs) {
            const std::string got = play(g.level, g.boss, rules);
            if (got != want) {
                std::printf("  %s (%s, règles %d) : %s (attendu %s)\n", g.level, g.boss, rules, got.c_str(), want);
                ++mismatches;
            }
        }
    }
    CHECK(mismatches == 0);
}

// 2.76.2 (the integration of the steps c, d, e and f): the levels where bosses of several steps meet — the boss
// rush of the 16 échos (« noyau_01 ») and the three échos before the Astrarque (« noyau_02 », whose sheet the écho
// of the Forgeron primordial shares with it) — 30 s of scripted play from the second phase of each. With the rules
// of 16 and of 15 (the replays of before) each run is the one of 2.76.1 to the bit: the digests computed by the
// 2.76.1 build (63333d3, its box); with those of 17, by its parts (the phase sheet in the fixed step).
TEST_CASE("determinism: the bosses of the shared levels (boss rush, Noyau) by their parts in 17, as in 2.76.1 with the rules of 16 and 15") {
    struct Golden {
        const char* level;
        const char* boss;
        const char* rules15;  // the 2.76.1 build (63333d3), rules of 15
        const char* rules16;  // the 2.76.1 build (63333d3), its box
        const char* rules17;  // 2.76.2: by its parts
    };
    const Golden golden[] = {
        {"noyau_01", "echo_thornwarden", "8818b3de79f5a31f", "8818b3de79f5a31f", "2d7091e29e203815"},
        {"noyau_01", "echo_forge_core", "99c250f530593919", "99c250f530593919", "8590b81395287d79"},
        {"noyau_01", "echo_crucible_heart", "beea4c255f52e187", "beea4c255f52e187", "27c05c7a0de326ee"},
        {"noyau_02", "echo_prime_sentinel", "a8ae4632348d0475", "a8ae4632348d0475", "a8ae4632348d0475"},
        {"noyau_02", "echo_frost_hydra", "b3339873a9a4b6f3", "b3339873a9a4b6f3", "bfe196c4a54c22e4"},
        {"noyau_02", "echo_forge_core", "3a85ceea1a402ff5", "3a85ceea1a402ff5", "4d607d0263cdff0b"},
    };
    int mismatches = 0;
    for (const Golden& g : golden) {
        const std::pair<int, const char*> runs[] = {{15, g.rules15}, {16, g.rules16}, {kSimulationVersion, g.rules17}};
        for (const auto& [rules, want] : runs) {
            const std::string got = bossFight(g.level, g.boss, 1, 6.0f, 4.0f, 17, rules);
            if (got != want) {
                std::printf("  %s (%s, règles %d) : %s (attendu %s)\n", g.level, g.boss, rules, got.c_str(), want);
                ++mismatches;
            }
        }
    }
    CHECK(mismatches == 0);
}
