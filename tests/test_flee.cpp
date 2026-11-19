// 2.76.0: an enemy that flees when weakened never gets out of reach (fleeMaxDistance): within its limit
// of where its flight began, flying never more than 2 tiles above it, cornered at that limit (it faces
// the player and can be hit). A replay of before (the rules of 15) flees as it did, without a limit.

#include <cmath>
#include <cstdio>
#include <memory>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

ContentDatabase& content() {
    static ContentDatabase* c = nullptr;
    if (!c) {
        c = new ContentDatabase();
        c->reload();
    }
    return *c;
}

/// A wide, tall room, the player on the floor, a ghost weakened above her: how far it gets in 12 s.
struct Flight {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    Enemy* ghost = nullptr;
    Vec2 start;
    float farthest = 0.0f, highest = 0.0f;
    bool hitAtTheEnd = false;
    Flight(const char* enemy, int rules) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_flee", "Test", "sylvanox");
        l.intro.steps.clear();
        for (auto& o : l.objects)
            if (o.type == "solid") o.rect = {0, 800, 4000, 160};
        WorldServices s{res, audio, content(), nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->setSimulationVersion(rules);
        w->seedGameplay(11);
        w->load(l);
        w->silent = true;
        w->player().spawn(*w, {1960, 800}, false, false);
        for (int i = 0; i < 10; ++i) w->fixedUpdate({}, 1.0f / 120.0f);
        ghost = w->spawnEnemy(enemy, {2040, 740}, true, "ghost");
        if (!ghost) return;
        ghost->alert(*w);
        // Weakened under its flight's threshold (alive).
        DamageInfo d;
        d.amount = ghost->maxHp() * (1.0f - ghost->def().fleeHealthRatio) / ghost->def().multiplier(DamageType::Contact) + 0.05f;
        d.type = DamageType::Contact;
        d.fromPlayer = true;
        d.source = w->player().center();
        ghost->takeDamage(*w, d);
        for (int i = 0; i < 60; ++i) w->fixedUpdate({}, 1.0f / 120.0f);  // its hurt flash, then its flight
        start = ghost->center();
        for (int i = 0; i < 12 * 120; ++i) {
            // The player walks toward it, slowly (it keeps fleeing her).
            PlayerInput in;
            const float dir = ghost->center().x > w->player().center().x ? 1.0f : -1.0f;
            if (i % 3 == 0) {
                in.held[static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft)] = true;
                in.move.x = dir;
            }
            w->fixedUpdate(in, 1.0f / 120.0f);
            if (!ghost->alive()) return;
            farthest = std::max(farthest, distance(ghost->center(), start));
            highest = std::max(highest, start.y - ghost->center().y);
        }
        DamageInfo hit;
        hit.amount = 0.01f;
        hit.type = DamageType::Contact;
        hit.fromPlayer = true;
        hit.source = ghost->center();
        hitAtTheEnd = w->damageEnemies(Rect::fromCenter(ghost->center(), {4.0f, 4.0f}), hit, nullptr) > 0;
    }
};

}  // namespace

TEST_CASE("flee (2.76.0): a fleeing ghost stays within reach, cornered at its limit, and can be hit") {
    CHECK(kSimulationVersion >= 16);
    for (const char* id : {"spectre", "wisp", "fire_wisp", "dragonnet", "etincelle"}) {
        const EnemyDefinition* d = content().enemy(id);
        CHECK(d != nullptr);
        if (!d) continue;
        CHECK(d->fleeHealthRatio > 0.0f && d->fleeMaxDistance == EnemyDefinition::kFleeMaxDistance);
        Flight f(id, kSimulationVersion);
        CHECK(f.ghost != nullptr);
        if (!f.ghost) continue;
        // Its anchor is where its flight began (a few px before `start`): a small margin for that.
        if (f.farthest > d->fleeMaxDistance + 12.0f || f.highest > EnemyDefinition::kFleeRise + 12.0f)
            std::printf("  %s : %.0f px de son départ, %.0f px plus haut\n", id, f.farthest, f.highest);
        CHECK(f.farthest <= d->fleeMaxDistance + 12.0f);
        CHECK(f.highest <= EnemyDefinition::kFleeRise + 12.0f);
        CHECK(f.ghost->alive());
        CHECK(f.hitAtTheEnd);
    }
}

TEST_CASE("flee (2.76.0): with the rules of 15 (a replay of before), it flees as it did, without a limit") {
    Flight before("spectre", 15), now("spectre", kSimulationVersion);
    CHECK(before.ghost && now.ghost);
    if (!before.ghost || !now.ghost) return;
    std::printf("  spectre : %.0f px avec les règles de 15, %.0f px avec celles de %d\n", before.farthest, now.farthest, kSimulationVersion);
    CHECK(before.farthest > now.ghost->def().fleeMaxDistance + 40.0f);  // the old flight, beyond any limit
    CHECK(now.farthest <= now.ghost->def().fleeMaxDistance + 12.0f);
}

TEST_CASE("flee (2.76.0): its limit is read, kept and written only when it is not the default (the fingerprints of before)") {
    const EnemyDefinition* d = content().enemy("spectre");
    CHECK(d != nullptr);
    if (!d) return;
    Json j = d->toJson();
    CHECK(!j.contains("fleeMaxDistance"));  // the default: the definition (and the levels' fingerprints) as before
    EnemyDefinition e = *d;
    e.fleeMaxDistance = 120.0f;
    Json k = e.toJson();
    CHECK(k.value("fleeMaxDistance", 0.0f) == 120.0f);
    CHECK(EnemyDefinition::fromJson(k).fleeMaxDistance == 120.0f);
    k["fleeMaxDistance"] = 5000.0f;
    CHECK(EnemyDefinition::fromJson(k).fleeMaxDistance == 480.0f);
}
