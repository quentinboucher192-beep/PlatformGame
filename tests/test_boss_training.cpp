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
    CHECK(!b->spawn);  // the usual start
    CHECK(back.find("aurorax") == nullptr);
    // « Lieu d'apparition » (2.41): the place picked in the arena is kept too.
    c.spawn = Vec2{9032.0f, 768.0f};
    p.set("thornwarden", c);
    back.fromJson(p.toJson());
    b = back.find("thornwarden");
    CHECK(b != nullptr && b->spawn && b->spawn->x == 9032.0f && b->spawn->y == 768.0f);
    // A damaged value: the usual start.
    Json j = p.toJson();
    j["bosses"]["thornwarden"]["spawn"] = "ici";
    back.fromJson(j);
    b = back.find("thornwarden");
    CHECK(b != nullptr && !b->spawn && b->hearts == 4);
}

// ------------------------------------------------------------ « Lieu d'apparition » (2.41)

namespace {

/// A boss's own level, as its training loads it.
struct BossLevel {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::optional<LevelData> level;
    BossTrainingSetup setup;
    BossLevel(const std::string& levelId, const std::string& bossId, int phase = -1) {
        content.reload();
        res.init(nullptr, &audio);
        level = content.loadLevel(levelId);
        if (!level) return;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == bossId) setup.bossObject = o.id;
        setup.bossId = bossId;
        setup.levelId = levelId;
        setup.hearts = 5;
        setup.slots = 2;
        setup.phase = phase;
    }
    std::unique_ptr<GameWorld> world() {
        WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
        auto w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(42);
        return w;
    }
    /// The places of its arena, from a world of its own (as the settings screen does).
    std::optional<TrainingArena> arena() {
        if (!level) return std::nullopt;
        auto w = world();
        return probeTrainingArena(*w, setup);
    }
};

bool near(Vec2 a, Vec2 b) { return std::abs(a.x - b.x) < 1.0f && std::abs(a.y - b.y) < 1.0f; }

/// Every place proposed, checked on the arena as the probe left it: the body inside the arena,
/// on firm ground, in no wall, in no hazard nor pickup, clear of the boss. -1: no place at all.
int badSpots(BossLevel& lv, int phase) {
    lv.setup.phase = phase;
    auto w = lv.world();
    auto a = probeTrainingArena(*w, lv.setup);
    if (!a || a->spots.empty()) return -1;
    const Vec2 size = w->player().config().size;
    int bad = 0;
    for (const TrainingSpot& s : a->spots) {
        const Rect body = Rect::fromBottomCenter(s.feet, size);
        bool ok = a->zone.containsRect(body) && !w->physics().overlapsSolid(body);
        const Collider* g = w->physics().groundBelow(body);
        ok = ok && g && g->enabled && !g->dynamic && std::abs(g->box.y - s.feet.y) < 0.5f &&
             !(g->tags & (ColliderTag::Moving | ColliderTag::Bounce | ColliderTag::Door | ColliderTag::Breakable));
        ok = ok && !body.intersects(a->boss.expanded(TrainingArena::kBossMargin - 1.0f));
        if (const Enemy* b = bossTrainingEnemy(*w, lv.setup)) ok = ok && !body.intersects(b->box);
        for (const auto& e : w->entities()) {
            const std::string& t = e->type();
            const bool hurts = t == "spikes" || t == "liquid" || t == "laser" || (t == "damage_zone" && e->active);
            const bool sets = t == "health_orb" || t == "energy_orb" || t == "door" || t == "checkpoint" || t == "power_unlock";
            if ((hurts || (sets && e->active && e->visible)) && (body.intersects(e->box) || body.intersects(e->overlapBox()))) ok = false;
        }
        if (!ok) ++bad;
    }
    return bad;
}

}  // namespace

TEST_CASE("boss training: the places to appear in an arena are on firm ground, out of walls, hazards and the boss") {
    // A guardian (Thornwarden), every phase and its last one.
    {
        BossLevel lv("sylvanox_01", "thornwarden");
        CHECK(lv.level.has_value());
        if (!lv.level) return;
        CHECK(badSpots(lv, -1) == 0);
        CHECK(badSpots(lv, 2) == 0);
        lv.setup.phase = -1;
        auto a = lv.arena();
        CHECK(a.has_value());
        if (!a) return;
        CHECK(a->spots.size() >= 20);              // its floor and its platforms
        CHECK(!a->zone.contains(a->usualStart));  // the usual start is outside, before the arena
        bool ground = false, ledge = false;
        for (const TrainingSpot& s : a->spots) (s.ledge ? ledge : ground) = true;
        CHECK(ground && ledge);
        // The one proposed first: on the floor, as far from the boss as the floor goes.
        const int k = a->suggested();
        CHECK(k >= 0);
        if (k < 0) return;
        const Vec2 from = a->spots[static_cast<size_t>(k)].feet;
        CHECK(!a->spots[static_cast<size_t>(k)].ledge);
        for (const TrainingSpot& s : a->spots) CHECK(s.feet.y <= from.y + 0.5f);
        // The arrows: along the floor one place at a time, up onto a platform, back down.
        const int side = from.x < a->boss.center().x ? 1 : -1;  // towards the boss
        const int n = a->step(k, side, 0);
        CHECK(n >= 0);
        if (n < 0) return;
        CHECK(std::abs(a->spots[static_cast<size_t>(n)].feet.x - from.x - static_cast<float>(side) * TrainingArena::kStep) < 0.5f);
        CHECK(std::abs(a->spots[static_cast<size_t>(n)].feet.y - from.y) < 0.5f);
        int up = -1;
        for (int i = n, guard = 0; i >= 0 && up < 0 && guard < 200; ++guard) {
            up = a->step(i, 0, -1);
            if (up < 0) i = a->step(i, side, 0);
        }
        CHECK(up >= 0);
        if (up >= 0) {
            CHECK(a->spots[static_cast<size_t>(up)].feet.y < from.y);
            const int down = a->step(up, 0, 1);
            CHECK(down >= 0 && a->spots[static_cast<size_t>(down)].feet.y > a->spots[static_cast<size_t>(up)].feet.y);
        }
        CHECK(a->find(from) == k);
        CHECK(a->find(from + Vec2{3.0f, 0.0f}) == -1);
        CHECK(a->nearest(from + Vec2{3.0f, -a->body.y * 0.5f}) == k);
    }
    // Singularis: its arena changes with the phases (its floor collapses into plasma at the
    // 5th, the plasma closes in on both sides at the 8th).
    {
        BossLevel lv("noyau_03", "singularis");
        CHECK(lv.level.has_value());
        if (!lv.level) return;
        for (int phase : {-1, 2, 4, 7}) CHECK(badSpots(lv, phase) == 0);
        // Positions from the arena's start (metadata.rooms, boss room: 47 tiles before 2.50, when
        // the tablets of every power made the way to it longer).
        float dx = 0.0f;
        for (const auto& r : lv.level->metadata.value("rooms", Json::array()))
            if (r.value("type", std::string()) == "boss") dx = (r.value("x", 47.0f) - 47.0f) * 16.0f;  // 16 px tiles
        auto onCollapsingFloor = [dx](const TrainingArena& a) {
            return std::any_of(a.spots.begin(), a.spots.end(),
                               [dx](const TrainingSpot& s) { return s.feet.x > 1120.0f + dx && s.feet.x < 1952.0f + dx && s.feet.y >= 639.0f; });
        };
        auto inSides = [dx](const TrainingArena& a) {
            return std::any_of(a.spots.begin(), a.spots.end(),
                               [dx](const TrainingSpot& s) { return s.feet.x < 960.0f + dx || s.feet.x > 2112.0f + dx; });
        };
        lv.setup.phase = -1;
        auto whole = lv.arena();
        CHECK(whole && onCollapsingFloor(*whole) && inSides(*whole));
        lv.setup.phase = 4;  // « Effondrement »: the floor is gone, plasma under it
        auto collapsed = lv.arena();
        CHECK(collapsed && !collapsed->spots.empty() && !onCollapsingFloor(*collapsed) && inSides(*collapsed));
        lv.setup.phase = 7;  // « Supernova »: the plasma closes both sides of the arena
        auto supernova = lv.arena();
        CHECK(supernova && !supernova->spots.empty() && !onCollapsingFloor(*supernova) && !inSides(*supernova));
    }
}

TEST_CASE("boss training: a run started at a place picked in the arena starts there, facing the boss") {
    BossLevel lv("sylvanox_01", "thornwarden");
    CHECK(lv.level.has_value());
    if (!lv.level) return;
    for (const LevelObject& o : lv.level->objects)
        if (o.type == "checkpoint" && o.propStr("label") == "Sanctuaire") {
            lv.setup.checkpointId = o.id;
            lv.setup.spawn = o.rect.bottomCenter();
        }
    auto a = lv.arena();
    CHECK(a && !a->spots.empty());
    if (!a || a->spots.empty()) return;
    for (int side : {-1, 1}) {
        // A place of the floor on that side of the boss.
        int k = -1;
        for (size_t i = 0; i < a->spots.size() && k < 0; ++i)
            if (!a->spots[i].ledge && (a->spots[i].feet.x - a->boss.center().x) * static_cast<float>(side) > 0.0f) k = static_cast<int>(i);
        CHECK(k >= 0);
        if (k < 0) continue;
        const Vec2 spot = a->spots[static_cast<size_t>(k)].feet;
        BossTrainingSetup s = lv.setup;
        s.arenaSpawn = spot;
        auto w = lv.world();
        CHECK(applyBossTraining(*w, s));
        const Player& p = w->player();
        CHECK(near(p.feet(), spot));
        CHECK(p.facing * static_cast<float>(side) < 0.0f);  // the boss is on the other side
        CHECK(near(w->respawnPoint(), spot));              // where the world would bring back
        CHECK(w->checkpointId().empty());                  // the checkpoint before the arena: not reached
        CHECK(p.health == 5 && p.powerSlots() == 2);
        // Standing there, on firm ground: the arena wakes its boss.
        for (int i = 0; i < 60; ++i) w->fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
        CHECK(p.grounded());
        CHECK(near(p.feet(), spot));
        const Enemy* b = bossTrainingEnemy(*w, s);
        CHECK(b && b->alive() && !b->dormant());
        // A fall ends the training: nobody comes back into the arena behind its end screen.
        w->player().kill(*w);
        for (int i = 0; i < 240; ++i) w->fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
        CHECK(w->stats().deaths == 1);
        CHECK(w->playerHidden());
        // « Recommencer »: the same start state, the same place, facing the boss again.
        auto again = lv.world();
        CHECK(applyBossTraining(*again, s));
        CHECK(near(again->player().feet(), spot) && again->player().facing * static_cast<float>(side) < 0.0f);
    }
    // A run that is not a training still comes back after a fall (to the place it started).
    {
        auto w = lv.world();
        w->startAt(a->spots.front().feet, false);
        w->player().kill(*w);
        for (int i = 0; i < 240; ++i) w->fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
        CHECK(!w->playerHidden());
        CHECK(near(w->respawnPoint(), a->spots.front().feet));
    }
    // A place that cannot hold the player (inside the floor): the usual start, the checkpoint.
    {
        BossTrainingSetup s = lv.setup;
        s.arenaSpawn = a->spots.front().feet + Vec2{0.0f, 40.0f};
        auto w = lv.world();
        CHECK(applyBossTraining(*w, s));
        CHECK(std::abs(w->player().feet().x - lv.setup.spawn.x) < 2.0f);
        CHECK(w->checkpointId() == lv.setup.checkpointId);
    }
}
