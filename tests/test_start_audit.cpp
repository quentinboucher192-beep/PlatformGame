// Audit of determinism at the start of a level (docs/TAS.md, « Audit du déterminisme »): every
// campaign level started in several ways — its intro skipped at once, skipped later, watched to the
// end; with the display, sound, particles, shaking, warm-ups and reference splits of a real game
// or none of them — is the same world at the first step Nova can move, and stays the same world
// after the same inputs. The machine's runs, the replays and the rankings rely on it: the way a
// level was started never changes how it plays.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/InputFrame.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// Plausible play (the same mix as the determinism tests): running right, jumps, attacks, dashes.
std::vector<InputFrame> playInputs(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
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
            if (r.chance(0.06f)) cur.held |= 1u << static_cast<int>(Action::PowerSlot1);
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

/// One way of starting a level.
struct Way {
    const char* name;
    int skipAt = 0;            // the step the intro is skipped (-1: watched to the end)
    float particles = 0.0f;    // WorldServices::particleQuality
    float shake = 0.0f;        // WorldServices::screenShake
    bool quiet = false;        // GameWorld::setQuiet
    bool display = false;      // display updates between the steps, at uneven rates
    bool warmUp = false;       // Hud::warmUp, reference splits, viewers: what the game's scene does
    int rules = kSimulationVersion;  // GameWorld::setSimulationVersion
};

struct Start {
    bool ok = false;
    uint32_t controllable = 0;     // steps until Nova can move
    std::vector<uint64_t> digests; // at that step, then every 60 steps of the same inputs
};

Start startLevel(ContentDatabase& content, const LevelData& level, const Way& way, const std::vector<InputFrame>& inputs) {
    Start out;
    AudioSystem audio;
    ResourceManager res;
    res.init(nullptr, &audio);
    WorldServices services{res, audio, content, nullptr, way.shake, way.particles};
    GameWorld w(services, WorldMode::Play);
    w.setKeysPerSlot(true);
    w.load(level);
    w.seedGameplay(levelSeed(level.id));
    w.setSimulationVersion(way.rules);
    if (way.quiet) w.setQuiet(true);
    if (way.warmUp) {
        Hud::warmUp(w);
        w.setReferenceSplits({3.0f, 9.5f, 20.0f});
        w.viewers = 1;
        w.setCameraFollow(0);
    }
    const float uneven[] = {1.0f / 37.0f, 1.0f / 144.0f, 1.0f / 60.0f, 0.05f};
    size_t frame = 0;
    double simTime = 0.0, shownTime = 0.0;
    auto step = [&](InputFrame f) {
        while (way.display && shownTime <= simTime) {
            const float dt = uneven[frame++ % 4];
            w.update(dt);
            shownTime += dt;
        }
        stepWorld(w, f, kSimDt);
        simTime += kSimDt;
    };
    // Until the clock starts (the first step Nova can move).
    uint32_t n = 0;
    while (!w.timer().running() && n < 120u * 180u) {
        InputFrame f;
        if (way.skipAt >= 0 && static_cast<int>(n) >= way.skipAt && w.cinematic().active() && w.cinematic().skippable())
            f.flags = InputFrame::SkipCinematic;
        step(f);
        ++n;
    }
    if (!w.timer().running()) return out;
    out.controllable = n;
    out.digests.push_back(simulationStateHash(w));
    for (size_t i = 0; i < inputs.size(); ++i) {
        step(inputs[i]);
        if ((i + 1) % 60 == 0) out.digests.push_back(simulationStateHash(w));
    }
    out.ok = true;
    return out;
}

}  // namespace

TEST_CASE("start audit: the first level of every planet is the same world whatever the way it was started") {
    ContentDatabase content;
    content.reload();
    const Way ways[] = {
        {"intro passée au premier pas", 0},
        {"intro passée après 0,5 s", 60},
        {"intro regardée jusqu'au bout", -1},
        {"intro passée, affichage et effets du jeu", 0, 1.0f, 1.0f, false, true, true},
        {"intro regardée, affichage et effets du jeu", -1, 0.7f, 1.0f, false, true, true},
        {"intro passée, silencieux sans particules", 0, 0.0f, 0.0f, true, false, false},
    };
    const std::vector<InputFrame> inputs = playInputs(31, 120 * 20);
    int levels = 0, differences = 0;
    for (const PlanetData* planet : content.campaignPlanets())
        for (const std::string& id : {planet->levels.empty() ? std::string() : planet->levels.front()}) {
            if (id.empty()) continue;
            auto level = content.loadLevel(id);
            CHECK(level.has_value());
            if (!level) continue;
            ++levels;
            const Start ref = startLevel(content, *level, ways[0], inputs);
            CHECK(ref.ok);
            for (size_t k = 1; k < std::size(ways); ++k) {
                const Start s = startLevel(content, *level, ways[k], inputs);
                if (!s.ok || s.digests.empty() || s.digests.front() != ref.digests.front()) {
                    std::printf("  %s, %s : monde différent au premier pas jouable\n", id.c_str(), ways[k].name);
                    ++differences;
                    continue;
                }
                if (s.digests != ref.digests) {
                    size_t at = 0;
                    while (at < s.digests.size() && at < ref.digests.size() && s.digests[at] == ref.digests[at]) ++at;
                    std::printf("  %s, %s : écart après %.1f s des mêmes entrées\n", id.c_str(), ways[k].name, at * 0.5);
                    ++differences;
                }
            }
        }
    CHECK(levels >= 5);
    CHECK(differences == 0);
}

// The audit sees a difference when there is one: with the rules of 18 (before 2.92, the level went
// on during the intro), watching the intro or skipping it does not give the same world.
TEST_CASE("start audit: with the rules of 18, the intro watched or skipped gives another world") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    if (!level) return;
    const std::vector<InputFrame> inputs = playInputs(31, 120 * 5);
    Way skipped{"intro passée, règles 18", 0};
    Way watched{"intro regardée, règles 18", -1};
    skipped.rules = watched.rules = 18;
    const Start a = startLevel(content, *level, skipped, inputs);
    const Start b = startLevel(content, *level, watched, inputs);
    CHECK(a.ok && b.ok);
    CHECK(b.controllable > a.controllable);
    CHECK(a.digests != b.digests);
}
