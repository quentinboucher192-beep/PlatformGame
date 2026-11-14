// The Stellar Core (2.7): the star at the centre of the system, opened once the three planets
// are completed. "L'Arène des Échos" chains the twelve guardians without any healing and with
// one life; "Le Cœur de l'Étoile" is the final level of the plan (maquette NiveauFinal).

#include <algorithm>
#include <set>
#include <string>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/progression/Progression.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
bool isBossObject(const LevelObject& o) { return o.type == "enemy" && o.prop<bool>("boss", false); }
}  // namespace

TEST_CASE("stellar core: the star opens once the three planets are completed") {
    ContentDatabase content;
    content.reload();
    const PlanetData* star = content.planet("noyau");
    CHECK(star != nullptr);
    if (!star) return;
    CHECK(star->levels.size() == 2);
    Progression p;
    p.unlockAll = false;
    CHECK(!p.planetUnlocked(*star, content));
    for (const char* planet : {"sylvanox", "glacior"})
        for (const auto& id : content.planet(planet)->levels) p.markCompleted(id);
    CHECK(!p.planetUnlocked(*star, content));  // Pyralis is still to be done
    for (const auto& id : content.planet("pyralis")->levels) p.markCompleted(id);
    CHECK(p.planetUnlocked(*star, content));
}

TEST_CASE("stellar core: the boss rush chains the twelve guardians, no healing, one life") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("noyau_01");
    CHECK(level.has_value());
    if (!level) return;
    std::set<std::string> originals;
    for (const auto& o : level->objects) {
        CHECK(o.type != "health_orb");
        CHECK(o.type != "heart");
        CHECK(o.type != "checkpoint");
        if (!isBossObject(o)) continue;
        const EnemyDefinition* d = content.enemy(o.propStr("enemy"));
        CHECK(d != nullptr);
        if (!d) continue;
        CHECK(!d->variantOf.empty());  // an echo
        originals.insert(d->variantOf);
        for (const auto& drop : d->drops) CHECK(drop.type != "health_orb");
    }
    CHECK(originals.size() == 12);
    for (const EnemyDefinition* e : content.enemies())
        if (e->isBoss && e->variantOf.empty() && e->id != "astrarque") CHECK(originals.count(e->id) == 1);
    CHECK(level->timerMode == TimerMode::Challenge);
    CHECK(level->challengeMaxDeaths == 0);
    CHECK(level->powerSlots() == 4);
    CHECK(analyzeLevel(*level, content).ok());
}

TEST_CASE("stellar core: a fallen echo leaves energy, never health") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    LevelData l = LevelData::makeEmpty("test_echo", "Test", "sylvanox");
    l.intro.steps.clear();
    WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
    GameWorld w(s, WorldMode::Arena);
    w.load(l);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
    Enemy* e = w.spawnEnemy("echo_thornwarden", w.player().feet() + Vec2{420, 0}, true);
    CHECK(e != nullptr);
    if (!e) return;
    for (int i = 0; i < 2; ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
    e->kill(w);
    int healthOrbs = 0, energyOrbs = 0;  // the most seen at once (the player is far away)
    for (int i = 0; i < 240; ++i) {
        w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
        int h = 0, en = 0;
        for (const auto& ent : w.entities()) {
            h += ent->type() == "health_orb" ? 1 : 0;
            en += ent->type() == "energy_orb" ? 1 : 0;
        }
        healthOrbs = std::max(healthOrbs, h);
        energyOrbs = std::max(energyOrbs, en);
    }
    CHECK(healthOrbs == 0);
    CHECK(energyOrbs >= 1);
}

TEST_CASE("stellar core: the final level follows the plan") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel("noyau_02");
    CHECK(level.has_value());
    if (!level) return;
    std::set<std::string> bossIds;
    int ambushes = 0, checkpoints = 0, tablets = 0;
    for (const auto& o : level->objects) {
        if (isBossObject(o)) bossIds.insert(o.propStr("enemy"));
        if (o.type == "checkpoint") ++checkpoints;
        if (o.type == "power_unlock") ++tablets;
        if (o.type == "combat_zone") {
            bool boss = false;
            for (const auto& b : level->objects)
                if (isBossObject(b) && o.propStr("waves").find(b.id) != std::string::npos) boss = true;
            ambushes += boss ? 0 : 1;
        }
    }
    CHECK(bossIds.size() == 4);
    CHECK(bossIds.count("astrarque") == 1);
    CHECK(bossIds.count("echo_prime_sentinel") == 1);
    CHECK(bossIds.count("echo_frost_hydra") == 1);
    CHECK(bossIds.count("echo_forge_core") == 1);
    CHECK(ambushes == 5);
    CHECK(checkpoints >= 7);
    CHECK(tablets >= 8);  // the threshold's eight powers, plus the stations
    CHECK(level->powerSlots() == 4);
    CHECK(analyzeLevel(*level, content).ok());
}

TEST_CASE("stellar core: the Astrarque") {
    ContentDatabase content;
    content.reload();
    const EnemyDefinition* a = content.enemy("astrarque");
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->isBoss);
    CHECK(a->phases.size() == 3);
    CHECK_NEAR(a->enrageTime, 90.0f, 0.01f);
    CHECK(a->tint != Color::white());
    CHECK(a->barIcon == "star");
    for (const auto& d : a->drops) CHECK(d.type != "health_orb");
}
