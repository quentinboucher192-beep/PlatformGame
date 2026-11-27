#include "tools/tas/Output.h"

#include <cmath>
#include <memory>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "game/online/OnlineRecord.h"
#include "tools/tas/Search.h"

namespace cosmic::tas {

namespace {

/// A world on the normal heap, as the game sets one up for a run of a campaign level.
struct PlainWorld {
    AudioSystem audio;
    ResourceManager res;
    std::unique_ptr<GameWorld> world;
    PlainWorld(const ContentDatabase& content, const LevelData& level, bool keysPerSlot) {
        res.init(nullptr, &audio);
        WorldServices services{res, audio, content, nullptr, 1.0f, 1.0f};
        world = std::make_unique<GameWorld>(services, WorldMode::Play);
        world->setKeysPerSlot(keysPerSlot);
        world->load(level);
        world->seedGameplay(levelSeed(level.id));
    }
};

int64_t ms(float seconds) { return static_cast<int64_t>(std::llround(static_cast<double>(seconds) * 1000.0)); }

}  // namespace

std::string toolVersion() {
    return std::string("cosmic_tas 1 / ") + COSMIC_VERSION_STRING + " / sim " + std::to_string(kSimulationVersion);
}

Verified recordAndVerify(const ContentDatabase& content, const LevelData& level, const LevelFingerprint& fp, RunCategory category,
                         const std::vector<InputFrame>& inputs, int64_t explored) {
    Verified v;
    // 1. The run recorded by the game's recorder, as GameScene records an official run.
    PlainWorld live(content, level, true);
    GameWorld& w = *live.world;
    ReplayRecorder rec;
    rec.begin(level.id, fp.hash, makeUuidV4(), w.gameplaySeed());
    std::vector<TasSplit> splits;
    w.eventObserver = [&](const std::string& src, const std::string& trig) {
        rec.onWorldEvent(src, trig);
        if (trig == "checkpoint") splits.push_back({src, ms(w.timer().elapsed())});
    };
    std::vector<Vec2> route;
    auto sampleRoute = [&] {
        if (!w.timer().running() && route.empty()) return;
        const float t = static_cast<float>(route.size()) * static_cast<float>(kTasRouteIntervalMs) / 1000.0f;
        if (w.timer().elapsed() + 1e-4f >= t && w.player().alive()) route.push_back(w.player().feet());
    };
    for (const InputFrame& f : inputs) {
        rec.recordInput(f);
        stepWorld(w, f, kSimDt);
        rec.tick(w);
        if (w.timer().running()) sampleRoute();
    }
    if (w.state() != WorldState::Outro && w.state() != WorldState::Completed) {
        v.why = "la course n'atteint pas la sortie";
        return v;
    }
    route.push_back(w.player().feet());  // where the clock stopped
    const float clock = w.timer().elapsed();
    // The outro to its end, as a player watches or skips it (the clock has stopped).
    int cinematic = 0;
    for (int i = 0; i < 120 * 120 && w.state() != WorldState::Completed; ++i) {
        const InputFrame f = playerFrame(w, InputFrame{}, cinematic);
        rec.recordInput(f);
        stepWorld(w, f, kSimDt);
        rec.tick(w);
    }
    if (w.state() != WorldState::Completed) {
        v.why = "le niveau ne se termine pas après la sortie";
        return v;
    }
    rec.finish(w, w.timer().elapsed());
    ReplayData replay = rec.take();
    const RunStats stats = w.stats();
    const int64_t timeMs = ms(w.timer().elapsed());
    if (timeMs != ms(clock)) {
        v.why = "le chrono a bougé après la sortie";
        return v;
    }

    // 2. The replay as it is written, played again by the normal replay playback.
    const std::vector<uint8_t> bytes = replay.encode();
    std::string err;
    auto back = ReplayData::decode(bytes, &err);
    if (!back || !back->exact()) {
        v.why = "replay illisible : " + err;
        return v;
    }
    PlainWorld again(content, level, back->keysPerSlot);
    ReplaySimulator sim(*back, *again.world);
    while (sim.step()) {
    }
    if (sim.desynced()) {
        v.why = "le replay se désynchronise au pas " + std::to_string(sim.desyncTick());
        return v;
    }
    const GameWorld& g = *again.world;
    if (!sim.finished() || g.state() != WorldState::Completed) {
        v.why = "le replay ne termine pas le niveau";
        return v;
    }
    if (ms(g.timer().elapsed()) != timeMs || back->durationMs != static_cast<uint32_t>(timeMs)) {
        v.why = "le replay ne donne pas le temps annoncé";
        return v;
    }
    // 3. The category, by the rules of the records.
    RunCandidate run;
    run.levelId = level.id;
    run.levelVersion = fp.version;
    run.levelHash = fp.hash;
    run.timeMs = timeMs;
    run.deaths = g.stats().deaths;
    run.damageTaken = g.stats().damageTaken;
    run.collectibles = g.stats().collectibles();
    run.totalCollectibles = g.stats().totalCollectibles();
    run.kills = g.stats().kills;
    run.fromStart = true;
    run.hasReplay = true;
    bool eligible = false;
    for (RunCategory c : eligibleCategories(run)) eligible = eligible || c == category;
    if (!eligible) {
        v.why = std::string("la course ne remplit pas la catégorie ") + categoryLabel(category);
        return v;
    }
    if (g.stats().deaths != stats.deaths || g.stats().damageTaken != stats.damageTaken || g.stats().collectibles() != stats.collectibles()) {
        v.why = "le replay ne refait pas la même course";
        return v;
    }

    TasRecord& r = v.record;
    r.levelId = level.id;
    r.levelVersion = fp.version;
    r.levelHash = fp.hash;
    r.simVersion = kSimulationVersion;
    r.category = category;
    r.timeMs = timeMs;
    r.splits = std::move(splits);
    r.explored = explored;
    r.computedAt = FileUtil::nowString().substr(0, 10);
    r.tool = toolVersion();
    r.route = std::move(route);
    r.deaths = run.deaths;
    r.damageTaken = run.damageTaken;
    r.collectibles = run.collectibles;
    r.totalCollectibles = run.totalCollectibles;
    r.replay = tasReplayFileName(category);
    v.replay = std::move(*back);
    v.bytes = bytes;
    v.ok = true;
    return v;
}

bool readSeedReplay(const std::string& path, const std::string& levelId, std::vector<InputFrame>& out, std::string& why) {
    auto bytes = FileUtil::readBinary(Paths::fromUtf8(path));
    if (!bytes) {
        why = "fichier illisible";
        return false;
    }
    auto r = ReplayData::decode(*bytes, &why);
    if (!r) return false;
    if (!r->exact() || r->levelId != levelId || !r->team.empty() || !r->challengeRules.empty() || !r->keysPerSlot) {
        why = "pas un replay exact d'une course seule de ce niveau (touches par emplacement)";
        return false;
    }
    out = r->inputs;
    return true;
}

}  // namespace cosmic::tas
