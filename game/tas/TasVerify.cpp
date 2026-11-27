#include "game/tas/TasVerify.h"

#include <cmath>
#include <memory>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
#include "game/online/CampaignRevisions.h"
#include "game/online/EnemyRevisions.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineRecord.h"
#include "game/online/Replay.h"
#include "game/tas/TasFile.h"

namespace cosmic {

namespace {

int64_t ms(float seconds) { return static_cast<int64_t>(std::llround(static_cast<double>(seconds) * 1000.0)); }

}  // namespace

TasCheck verifyTasReplay(const ContentDatabase& content, const LevelData& level, const TasRecord& record, const ReplayData& replay,
                         const std::atomic<bool>* cancel) {
    TasCheck c;
    auto refuse = [&](std::string why) {
        c.why = std::move(why);
        return c;
    };
    if (level.id != record.levelId) return refuse("pas le niveau de la course");
    if (!replay.exact() || replay.levelId != record.levelId || replay.levelHash != record.levelHash) return refuse("replay d'un autre niveau");
    if (replay.simVersion != record.simVersion || record.simVersion != kSimulationVersion) return refuse("replay d'une autre version de la simulation");
    if (replay.teamSize() > 1 || !replay.challengeRules.empty() || !replay.keysPerSlot) return refuse("pas une course seule, touches par emplacement");
    if (replay.inputs.size() > kTasMaxSteps) return refuse("replay trop long");
    if (replay.durationMs != static_cast<uint32_t>(std::max<int64_t>(0, record.timeMs))) return refuse("le replay n'annonce pas le temps de la course");

    // A world on the normal heap, as the replay viewer plays a replay of a campaign level.
    AudioSystem audio;
    ResourceManager res;
    res.init(nullptr, &audio);
    WorldServices services{res, audio, content, nullptr, 1.0f, 1.0f};
    auto world = std::make_unique<GameWorld>(services, WorldMode::Play);
    world->setKeysPerSlot(replay.keysPerSlot);
    world->load(level);
    world->seedGameplay(levelSeed(level.id));
    world->silent = true;
    ReplaySimulator sim(replay, *world);
    while ((!cancel || !cancel->load(std::memory_order_relaxed)) && sim.step()) {
    }
    if (cancel && cancel->load()) return refuse("vérification interrompue");
    if (sim.desynced()) return refuse("le replay se désynchronise au pas " + std::to_string(sim.desyncTick()));
    const GameWorld& g = *world;
    if (!sim.finished() || g.state() != WorldState::Completed) return refuse("le replay ne termine pas le niveau");
    c.timeMs = ms(g.timer().elapsed());
    if (c.timeMs != record.timeMs) return refuse("le replay ne donne pas le temps annoncé");
    c.deaths = g.stats().deaths;
    c.damageTaken = g.stats().damageTaken;
    c.collectibles = g.stats().collectibles();
    c.totalCollectibles = g.stats().totalCollectibles();
    // The category, by the rules of the records.
    RunCandidate run;
    run.levelId = level.id;
    run.levelVersion = record.levelVersion;
    run.levelHash = record.levelHash;
    run.timeMs = c.timeMs;
    run.deaths = c.deaths;
    run.damageTaken = c.damageTaken;
    run.collectibles = c.collectibles;
    run.totalCollectibles = c.totalCollectibles;
    run.kills = g.stats().kills;
    run.fromStart = true;
    run.hasReplay = true;
    bool eligible = false;
    for (RunCategory cat : eligibleCategories(run)) eligible = eligible || cat == record.category;
    if (!eligible) return refuse(std::string("la course ne remplit pas la catégorie ") + categoryLabel(record.category));
    if (c.deaths != record.deaths || c.damageTaken != record.damageTaken || c.collectibles != record.collectibles)
        return refuse("le replay ne refait pas la même course");
    c.ok = true;
    return c;
}

void applyOnlineRevisions(ContentDatabase& content, const fs::path& userDir, const std::string& server) {
    if (server.empty()) return;
    // The enemy revisions first: a campaign revision is bound to them (as GameServices does).
    EnemyRevisionStore enemies(userDir / "online" / "enemies");
    enemies.load();
    enemies.applyTo(content, server, COSMIC_VERSION_STRING);
    CampaignRevisionStore campaign(userDir / "online" / "campaign");
    campaign.load();
    for (const auto& [levelId, file] : campaign.currentFiles(content, server, COSMIC_VERSION_STRING))
        if (!content.setCampaignRevision(levelId, file)) content.setCampaignRevision(levelId, {});
}

}  // namespace cosmic
