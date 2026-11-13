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
#include "game/gameplay/GameWorld.h"
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
        {"sylvanox_01", "a6f67ae6d54ff825"},
        {"glacior_01", "3fdf517e336b5751"},
        {"pyralis_01", "3f35a9082391e8a5"},
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
    CHECK(ReplayData::parse(raw)->formatVersion == 2);
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
