// Boss training (boss statistics → Entraînement): the boss's own level, started at the
// checkpoint before its arena with the hearts, slots and powers chosen, at the phase chosen.
// A training run never counts: a boss beaten in training changes nothing in the
// achievements (counters, unlocks), the boss statistics, the records or the progression —
// each recorder checks the run's training flag (GameWorld::training()).

#include <algorithm>
#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/Damage.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/RunCounters.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/Replay.h"
#include "game/player/Player.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// Thornwarden's level (Sylvanox 1), as the training starts it.
struct Training {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    BossTrainingSetup setup;
    bool applied = false;
    explicit Training(bool training, int phase = -1) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel("sylvanox_01");
        if (!level) return;
        for (const LevelObject& o : level->objects) {
            if (o.type == "enemy" && o.propStr("enemy") == "thornwarden") setup.bossObject = o.id;
            if (o.type == "checkpoint" && o.propStr("label") == "Sanctuaire") {
                setup.checkpointId = o.id;
                setup.spawn = o.rect.bottomCenter();
            }
        }
        setup.bossId = "thornwarden";
        setup.levelId = "sylvanox_01";
        setup.hearts = 3;
        setup.slots = 2;
        setup.powers = {"dash", "shield"};
        setup.phase = phase;
        WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(42);
        if (training) applied = applyBossTraining(*w, setup);
        else w->startAt(setup.spawn);
    }
    Enemy* boss() {
        auto* spawn = dynamic_cast<EnemySpawnEntity*>(w->entity(setup.bossObject));
        return spawn ? spawn->enemy(*w) : nullptr;
    }
    void run(int steps, BossFightTracker& tracker, BossStats& stats) {
        for (int i = 0; i < steps; ++i) {
            w->fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
            tracker.step(*w, stats);
        }
    }
    /// The fight starts (the arena wakes it), then the boss falls.
    void beatBoss(BossFightTracker& tracker, BossStats& stats) {
        if (auto* e = w->entity(setup.bossObject)) e->handleAction(*w, "activate", Json::object());
        run(4, tracker, stats);
        if (Enemy* b = boss()) b->kill(*w);
        run(240, tracker, stats);
    }
};

struct Saves {
    ContentDatabase content;
    AchievementCatalog catalog;
    AchievementBook book{fs::temp_directory_path() / ("cosmic_train_" + makeUuidV4().substr(0, 8) + ".json")};
    RecordsManager records;
    Progression progression;
    BossStats bosses;
    Saves() {
        content.reload();
        std::string err;
        catalog.loadDefault(&err);
    }
    std::vector<std::string> unlocks() {
        AchievementInputs in;
        in.content = &content;
        in.progression = &progression;
        in.records = &records;
        in.bosses = &bosses;
        in.stats = &book.stats();
        for (const PlanetData* p : content.planets())
            for (const LevelEntry* e : content.levelsForPlanet(p->id)) in.campaignLevels.push_back(e->id);
        std::vector<std::string> ids;
        for (const AchievementDef* a : book.evaluate(catalog, in, "2026-11-15 10:00")) ids.push_back(a->id);
        return ids;
    }
    /// What GameScene does with a finished run, through the same recorders.
    void record(const GameWorld& w) {
        noteRunCounters(book.stats(), w, true);
        RunResult r = runResultOf(w, true);
        records.submit(r);
        progression.noteRun(r);
    }
};

}  // namespace

TEST_CASE("boss training: set up with the world (start, hearts, slots, powers), marked as training") {
    Training t(true);
    CHECK(t.applied);
    CHECK(t.w->training());
    const Player& p = t.w->player();
    CHECK(p.maxHealth() == 3);
    CHECK(p.health == 3);
    CHECK(p.powerSlots() == 2);
    CHECK(p.powers().size() == 2);
    CHECK(p.hasPower("dash") && p.hasPower("shield"));
    // At the checkpoint before the arena (« Sanctuaire »), reached.
    CHECK(std::abs(p.feet().x - t.setup.spawn.x) < 2.0f);
    CHECK(t.w->checkpointId() == t.setup.checkpointId);
    // Every phase: the boss starts whole, in its first phase.
    Enemy* b = t.boss();
    CHECK(b != nullptr);
    if (b) {
        CHECK(b->phaseIndex() == 0);
        CHECK(b->hpRatio() > 0.99f);
    }
    CHECK(!bossTrainingWon(*t.w, t.setup));
    // A normal run of the same level is not training.
    Training normal(false);
    CHECK(!normal.w->training());
}

TEST_CASE("boss training: one phase alone starts at its threshold and ends when it is beaten") {
    Training t(true, 1);
    Enemy* b = t.boss();
    CHECK(t.applied && b != nullptr);
    if (!b) return;
    const auto& phases = b->def().phases;
    CHECK(phases.size() >= 3);
    CHECK(b->phaseIndex() == 1);
    CHECK(std::abs(b->hpRatio() - phases[1].hpThreshold) < 0.01f);
    CHECK(!bossTrainingWon(*t.w, t.setup));
    // Wounded into the next phase: this phase is beaten.
    BossFightTracker tracker;
    BossStats stats;
    if (auto* e = t.w->entity(t.setup.bossObject)) e->handleAction(*t.w, "activate", Json::object());
    t.run(120, tracker, stats);  // awake (its appearance lasts a moment)
    CHECK(b->phaseIndex() == 1);
    DamageInfo d;
    d.fromPlayer = true;
    d.amount = b->hp() - b->maxHp() * (phases[2].hpThreshold - 0.02f);
    b->takeDamage(*t.w, d);
    t.run(240, tracker, stats);
    CHECK(t.boss() && t.boss()->phaseIndex() == 2);
    CHECK(bossTrainingWon(*t.w, t.setup));
    // The powers really carried: as in a level since 2.24, each power on a key of its own (two
    // powers of the old special button go together), never more than the slots.
    auto fitted = fitTrainingPowers(t.content, {"boomerang", "decoy", "dash", "shield"}, 2);
    CHECK(fitted == std::vector<std::string>({"boomerang", "decoy"}));
    CHECK(fitTrainingPowers(t.content, {"dash", "dash", "wave"}, 4) == std::vector<std::string>({"dash", "wave"}));
}

TEST_CASE("boss training: a boss beaten in training changes no save (achievements, boss stats, records, progression)") {
    // Training: the same fight, the same recorders, nothing changes.
    {
        Training t(true);
        Saves s;
        BossFightTracker tracker;
        t.beatBoss(tracker, s.bosses);
        CHECK(bossTrainingWon(*t.w, t.setup));
        CHECK(t.w->stats().kills >= 1);
        s.record(*t.w);
        CHECK(s.bosses.find("thornwarden") == nullptr);
        CHECK(s.bosses.all().empty());
        CHECK(tracker.kills().empty());
        CHECK(s.book.stats().counters.empty());
        CHECK(s.book.stats().maxima.empty());
        CHECK(s.book.stats().sets.empty());
        CHECK(s.records.get("sylvanox_01") == nullptr);
        CHECK(!s.progression.levelCompleted("sylvanox_01"));
        CHECK(s.progression.completedCount() == 0);
        CHECK(s.unlocks().empty());
        CHECK(s.book.unlockedCount() == 0);
    }
    // The control: the same fight in a normal run is counted by each of them.
    {
        Training t(false);
        Saves s;
        BossFightTracker tracker;
        t.beatBoss(tracker, s.bosses);
        s.record(*t.w);
        const BossRecord* r = s.bosses.find("thornwarden");
        CHECK(r != nullptr && r->kills == 1);
        CHECK(s.book.stats().counter("kills") >= 1);
        CHECK(s.records.get("sylvanox_01") != nullptr);
        CHECK(s.progression.levelCompleted("sylvanox_01"));
        CHECK(!s.unlocks().empty());
    }
}

TEST_CASE("boss training: the last choices are kept per boss") {
    BossTrainingPrefs p;
    BossTrainingChoice c;
    c.hearts = 4;
    c.slots = 3;
    c.powers = {"dash", "glide"};
    c.phase = 2;
    p.set("thornwarden", c);
    BossTrainingPrefs back;
    back.fromJson(p.toJson());
    const BossTrainingChoice* b = back.find("thornwarden");
    CHECK(b != nullptr);
    if (!b) return;
    CHECK(b->hearts == 4 && b->slots == 3 && b->phase == 2);
    CHECK(b->powers == std::vector<std::string>({"dash", "glide"}));
    CHECK(back.find("aurorax") == nullptr);
}
