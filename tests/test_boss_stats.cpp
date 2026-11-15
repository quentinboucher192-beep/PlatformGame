// Boss statistics (2.17): victories, defeats, fights and phases without being hit, kept on
// this computer and saved; the tracker reads the world without touching the simulation.

#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/Damage.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/player/Player.h"
#include "game/records/BossStats.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct Arena {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    BossStats stats;
    BossFightTracker tracker;
    Arena() {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("boss_stats_test", "Test", "sylvanox");
        l.intro.steps.clear();
        Vec2 start{200, 400};
        for (const auto& o : l.objects)
            if (o.type == "player_start") start = o.rect.center();
        LevelObject b;
        b.id = "boss_1";
        b.type = "enemy";
        b.rect = {start.x + 260, start.y - 80, 96, 96};
        b.props = {{"enemy", "thornwarden"}, {"boss", true}};
        l.objects.push_back(b);
        WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(l);
        w->eventObserver = [this](const std::string& src, const std::string& trig) { tracker.onWorldEvent(src, trig); };
        w->startAt(start);
        if (auto* es = dynamic_cast<EnemySpawnEntity*>(w->entity("boss_1"))) es->handleAction(*w, "activate", Json::object());
    }
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            w->fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
            tracker.step(*w, stats);
        }
    }
    Enemy* boss() { return w->activeBoss(); }
    /// Takes the boss just under a health ratio (the next phase), then lets it change.
    void woundTo(float ratio) {
        Enemy* e = boss();
        if (!e) return;
        DamageInfo d;
        d.fromPlayer = true;
        d.amount = std::max(0.0f, e->hp() - e->maxHp() * ratio + 0.5f);
        e->takeDamage(*w, d);
        run(240);
    }
    void keepPlayerSafe() {
        // The test drives the fight: the player stays out of reach.
        w->player().box.x = 40.0f;
    }
};

}  // namespace

TEST_CASE("boss stats: every phase and the whole fight without a hit") {
    Arena a;
    a.run(2);
    CHECK(a.boss() != nullptr);
    if (!a.boss()) return;
    size_t phases = a.boss()->def().phases.size();
    CHECK(phases >= 2);
    for (size_t p = 1; p < phases; ++p) {
        a.keepPlayerSafe();
        a.woundTo(a.boss() ? a.boss()->def().phases[p].hpThreshold - 0.02f : 0.0f);
    }
    int noHitEvents = 0;
    for (const auto& e : a.tracker.takeEvents()) noHitEvents += e.kind == BossFightTracker::Event::Kind::PhaseNoHit ? 1 : 0;
    CHECK(noHitEvents == static_cast<int>(phases) - 1);
    a.keepPlayerSafe();
    if (Enemy* e = a.boss()) e->kill(*a.w);
    a.run(4);
    const BossRecord* r = a.stats.find("thornwarden");
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->fights == 1);
    CHECK(r->kills == 1);
    CHECK(r->noHitKills == 1);
    CHECK(static_cast<int>(r->phaseClears.size()) == static_cast<int>(phases));
    for (size_t p = 0; p < phases; ++p) {
        CHECK(r->phaseClears[p] == 1);
        CHECK(r->phaseNoHit[p] == 1);
    }
    CHECK(r->bestKill > 0.0f);
    CHECK(a.tracker.kills().size() == 1);
    CHECK(a.tracker.kills()[0].noHit);
}

TEST_CASE("boss stats: a hit spoils its phase and the whole fight, a death is a defeat") {
    Arena a;
    a.run(2);
    CHECK(a.boss() != nullptr);
    if (!a.boss()) return;
    // Hit during the first phase.
    DamageInfo hit;
    hit.amount = 1;
    hit.ignoreShield = true;
    a.w->player().hurt(*a.w, hit);
    a.keepPlayerSafe();
    a.woundTo(a.boss()->def().phases[1].hpThreshold - 0.02f);
    a.keepPlayerSafe();
    if (Enemy* e = a.boss()) e->kill(*a.w);
    a.run(4);
    const BossRecord* r = a.stats.find("thornwarden");
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->kills == 1);
    CHECK(r->noHitKills == 0);
    CHECK(r->phaseClears[0] == 1);
    CHECK(r->phaseNoHit[0] == 0);
    CHECK(r->phaseNoHit[1] == 1);  // the second phase was clean
    // Saved and read back.
    BossStats copy;
    copy.fromJson(a.stats.toJson());
    const BossRecord* back = copy.find("thornwarden");
    CHECK(back && back->kills == 1 && back->phaseNoHit[1] == 1 && back->phaseNames.size() == r->phaseNames.size());

    Arena d;
    d.run(2);
    d.w->player().kill(*d.w);
    d.run(30);
    const BossRecord* rd = d.stats.find("thornwarden");
    CHECK(rd && rd->defeats == 1 && rd->kills == 0);
}

TEST_CASE("boss stats: killed by the boss, back and a clean finish is not « sans dégât »") {
    // Reported in 2.18: die against a boss, come back, finish it without a hit — the game
    // announced « vaincu sans dégât » (and the resumed phase « sans dégât »).
    Arena a;
    a.run(2);
    CHECK(a.boss() != nullptr);
    if (!a.boss()) return;
    a.keepPlayerSafe();
    a.woundTo(a.boss()->def().phases[1].hpThreshold - 0.02f);  // now in the second phase
    int phaseAtDeath = a.boss() ? a.boss()->phaseIndex() : 1;
    a.tracker.takeEvents();
    a.w->player().kill(*a.w);
    // Back after the respawn; the boss keeps its wounds and its phase.
    for (int i = 0; i < 12 && !(a.w->player().alive() && a.boss() && a.boss()->alive()); ++i) {
        a.run(60);
        if (!a.boss())
            if (auto* es = dynamic_cast<EnemySpawnEntity*>(a.w->entity("boss_1"))) es->handleAction(*a.w, "activate", Json::object());
    }
    CHECK(a.boss() != nullptr);
    if (!a.boss()) return;
    // The respawn animation ends, then the fight resumes (as when the player walks back in).
    for (int i = 0; i < 6 && a.w->player().state() == PlayerState::Respawn; ++i) a.run(60);
    a.run(30);
    a.keepPlayerSafe();
    if (Enemy* e = a.boss()) e->kill(*a.w);
    a.run(4);
    const BossRecord* r = a.stats.find("thornwarden");
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->kills == 1);
    CHECK(r->defeats >= 1);
    CHECK(r->noHitKills == 0);
    CHECK(r->phaseNoHit[static_cast<size_t>(phaseAtDeath)] == 0);  // the phase it killed me in
    bool announced = false;
    for (const auto& e : a.tracker.takeEvents())
        if ((e.kind == BossFightTracker::Event::Kind::Kill && e.noHit) ||
            (e.kind == BossFightTracker::Event::Kind::PhaseNoHit && e.phase == phaseAtDeath))
            announced = true;
    CHECK(!announced);
}
