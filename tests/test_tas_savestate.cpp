// Savestates of a solo run (game/tas/ArenaWorld.h, docs/TAS.md « États sauvegardés »): the whole
// simulation is taken and put back exactly. On the first level of every planet, and in a boss
// fight: N steps, a savestate, M more steps; back to the savestate, the same M steps give the
// same digests AND the same bytes of the whole arena (every object of the run, its allocator
// included). A world in an arena plays exactly as a world on the normal heap.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/ArenaHeap.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/tas/ArenaWorld.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

std::vector<InputFrame> playInputs(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            float k = r.unit();
            int dir = k < 0.7f ? 1 : (k < 0.88f ? -1 : 0);
            if (dir > 0) cur.held |= 1u << static_cast<int>(Action::MoveRight);
            if (dir < 0) cur.held |= 1u << static_cast<int>(Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= 1u << static_cast<int>(Action::Jump);
            if (r.chance(0.25f)) cur.held |= 1u << static_cast<int>(Action::Attack);
            if (r.chance(0.08f)) cur.held |= 1u << static_cast<int>(Action::Dash);
            if (r.chance(0.08f)) cur.held |= 1u << static_cast<int>(Action::PowerSlot1);
            if (r.chance(0.05f)) cur.held |= 1u << static_cast<int>(Action::PowerSlot2);
            hold = r.rangeInt(4, 40);
        }
        InputFrame f = cur;
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        if (i == 0) f.flags = InputFrame::SkipCinematic;
        f.choice = InputFrame::kChoiceLeave;  // a power offer is answered at once
        out.push_back(f);
        prev = f;
    }
    return out;
}

bool sameBytes(const tas::ArenaWorld::State& a, const tas::ArenaWorld::State& b) {
    return a.size == b.size && std::memcmp(a.bytes.get(), b.bytes.get(), a.size) == 0;
}

/// Steps `inputs[from, to)`, the digest every 60 steps.
std::vector<uint64_t> play(tas::ArenaWorld& w, const std::vector<InputFrame>& inputs, size_t from, size_t to) {
    std::vector<uint64_t> d;
    for (size_t i = from; i < to; ++i) {
        w.step(inputs[i]);
        if ((i + 1 - from) % 60 == 0) d.push_back(w.digest());
    }
    d.push_back(w.digest());
    return d;
}

/// N steps, a savestate, M steps; restored, M steps again (and once more after other inputs in
/// between). Returns a description of the first problem, or "".
std::string checkRestore(tas::ArenaWorld& w, const std::vector<InputFrame>& inputs, const std::vector<InputFrame>& other, size_t n, size_t m) {
    w.setScrub(0);  // freed memory as it was before it was ever used: the same steps, the same bytes
    play(w, inputs, 0, n);
    tas::ArenaWorld::State s, end1, end2;
    w.save(s);
    const uint64_t atSave = w.digest();
    const uint32_t stepsAtSave = w.steps();
    const auto first = play(w, inputs, n, n + m);
    w.save(end1);
    w.restore(s);
    if (w.steps() != stepsAtSave || w.digest() != atSave) return "l'état rendu n'est pas celui de la sauvegarde";
    const auto second = play(w, inputs, n, n + m);
    if (second != first) return "les mêmes entrées ne donnent plus les mêmes empreintes";
    w.save(end2);
    if (!sameBytes(end1, end2)) return "les mêmes entrées ne laissent pas les mêmes octets";
    // Other inputs in between, then back: still the same run, the memory freed by each restore
    // overwritten (nothing may still use it).
    w.setScrub(0xDB);
    w.restore(s);
    play(w, other, n, n + m);
    w.restore(s);
    if (play(w, inputs, n, n + m) != first) return "après d'autres entrées, la sauvegarde ne rend plus la même course";
    return {};
}

}  // namespace

TEST_CASE("savestate: a world in its arena plays exactly as a world on the normal heap") {
    CHECK(heap::hooksInstalled());
    ContentDatabase content;
    content.reload();
    const auto inputs = playInputs(5, 120 * 30);
    for (const char* id : {"sylvanox_01", "pyralis_02", "noyau_03"}) {
        auto level = content.loadLevel(id);
        CHECK(level.has_value());
        if (!level) continue;
        AudioSystem audio;
        ResourceManager res;
        res.init(nullptr, &audio);
        WorldServices services{res, audio, content, nullptr, 0.0f, 0.0f};
        GameWorld normal(services, WorldMode::Play);
        normal.setKeysPerSlot(true);
        normal.load(*level);
        normal.seedGameplay(levelSeed(level->id));
        normal.setQuiet(true);
        tas::ArenaWorld arena(content, *level);
        CHECK(arena.digest() == simulationStateHash(normal));
        int diff = -1;
        for (size_t i = 0; i < inputs.size() && diff < 0; ++i) {
            stepWorld(normal, inputs[i], kSimDt);
            arena.step(inputs[i]);
            if ((i + 1) % 60 == 0 && arena.digest() != simulationStateHash(normal)) diff = static_cast<int>(i);
        }
        if (diff >= 0) std::printf("  %s : écart au pas %d\n", id, diff);
        CHECK(diff < 0);
    }
}

TEST_CASE("savestate: the first level of every planet is restored exactly (digests and bytes)") {
    ContentDatabase content;
    content.reload();
    const auto inputs = playInputs(17, 120 * 25);
    const auto other = playInputs(71, 120 * 25);
    int levels = 0, problems = 0;
    size_t largest = 0;
    for (const PlanetData* planet : content.campaignPlanets())
        for (const std::string& id : {planet->levels.empty() ? std::string() : planet->levels.front()}) {
            if (id.empty()) continue;
            auto level = content.loadLevel(id);
            CHECK(level.has_value());
            if (!level) continue;
            ++levels;
            tas::ArenaWorld w(content, *level);
            const std::string why = checkRestore(w, inputs, other, 120 * 10, 120 * 5);
            largest = std::max(largest, w.stateBytes());
            if (!why.empty()) {
                std::printf("  %s : %s\n", id.c_str(), why.c_str());
                ++problems;
            }
        }
    std::printf("  plus grand état sauvegardé : %.1f Mo\n", static_cast<double>(largest) / (1024.0 * 1024.0));
    CHECK(levels >= 5);
    CHECK(problems == 0);
}

// A boss fight (the giga boss of the Noyau: hit parts, phases, orbiting platforms, events that
// break floors and switch on dangers): restored exactly in the middle of its fight.
TEST_CASE("savestate: a boss fight is restored exactly") {
    ContentDatabase content;
    content.reload();
    const auto inputs = playInputs(23, 120 * 12);
    const auto other = playInputs(32, 120 * 12);
    int fights = 0, problems = 0;
    const std::string id = "noyau_03";
    auto level = content.loadLevel(id);
    CHECK(level.has_value());
    if (level) {
            for (const LevelObject& o : level->objects) {
                if (o.type != "enemy" || fights > 0) continue;
                const EnemyDefinition* def = content.enemyFor(o.propStr("enemy"), *level);
                if (!def || !def->isBoss) continue;
                Rect zone;
                for (const LevelObject& z : level->objects)
                    if (z.type == "combat_zone" && (z.propStr("waves").find(o.id) != std::string::npos || z.rect.contains(o.rect.center()))) zone = z.rect;
                if (zone.w <= 0.0f) continue;
                tas::ArenaWorld w(content, *level);
                bool started = w.inside([&](GameWorld& g) {
                    auto* spawn = dynamic_cast<EnemySpawnEntity*>(g.entity(o.id));
                    Enemy* boss = spawn ? spawn->enemy(g) : nullptr;
                    if (!boss) return false;
                    g.startAt({zone.x + 18.0f * 16.0f, zone.bottom() - 14.0f * 16.0f});  // as the determinism tests
                    g.startBoss(*boss);
                    jumpBossToPhase(g, *boss, 4);
                    return true;
                });
                if (!started) continue;
                ++fights;
                // A fight from its start: no intro to skip.
                std::vector<InputFrame> in = inputs, oth = other;
                in[0].flags = oth[0].flags = 0;
                const std::string why = checkRestore(w, in, oth, 120 * 3, 120 * 5);
                if (!why.empty()) {
                    std::printf("  %s, %s : %s\n", id.c_str(), def->id.c_str(), why.c_str());
                    ++problems;
                }
            }
        }
    CHECK(fights == 1);
    CHECK(problems == 0);
}
