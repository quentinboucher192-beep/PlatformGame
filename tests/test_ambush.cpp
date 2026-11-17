// Ambushes (2.62.2, simulation 14): a fall during an ambush does not reset it (it waits as Nova
// left it, its doors open to let her back in), its enemies never get out of its arena, and the
// HUD counts the enemies left.

#include <algorithm>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;

/// A flat level: Nova starts on the left; an arena (x 400 to 1000) with two doors and two
/// waves of two glowmites each.
struct Ambush {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::vector<std::string> wave1, wave2;
    std::string zoneId, doorL, doorR;

    explicit Ambush(bool withBossArena = false) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_ambush", "Test", "sylvanox");
        l.intro.steps.clear();
        l.timerMode = TimerMode::Unlimited;
        auto add = [&](const std::string& type, Rect r, Json props) {
            LevelObject o;
            o.id = "t" + std::to_string(l.objects.size() + 1);
            o.type = type;
            o.layer = "gameplay";
            o.rect = r;
            o.props = std::move(props);
            l.objects.push_back(o);
            return o.id;
        };
        for (float x : {560.0f, 640.0f}) wave1.push_back(add("enemy", {x, 790, 14, 10}, {{"enemy", "glowmite"}, {"dormant", true}, {"patrol", 32}}));
        for (float x : {760.0f, 840.0f}) wave2.push_back(add("enemy", {x, 790, 14, 10}, {{"enemy", "glowmite"}, {"dormant", true}, {"patrol", 32}}));
        doorL = add("door", {392, 736, 16, 64}, {{"open", true}});
        doorR = add("door", {1000, 736, 16, 64}, {{"open", true}});
        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (const auto& id : v) s += (s.empty() ? "" : ",") + id;
            return s;
        };
        zoneId = add("combat_zone", {400, 560, 600, 240},
                     {{"waves", join(wave1) + ";" + join(wave2)}, {"doors", doorL + "," + doorR}, {"lockCamera", false}, {"announce", false}});
        if (withBossArena) {
            std::string boss = add("enemy", {1700, 760, 40, 40}, {{"enemy", "thornwarden"}, {"boss", true}, {"dormant", true}});
            add("combat_zone", {1500, 600, 500, 200}, {{"waves", boss}, {"lockCamera", false}, {"announce", false}});
        }
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(l);
        w->startAt({160, 800});
    }
    CombatZoneEntity* zone() { return dynamic_cast<CombatZoneEntity*>(w->entity(zoneId)); }
    bool doorOpen(const std::string& id) {
        auto* d = dynamic_cast<DoorEntity*>(w->entity(id));
        return d && d->isOpen();
    }
    Enemy* enemy(const std::string& objectId) {
        auto* es = dynamic_cast<EnemySpawnEntity*>(w->entity(objectId));
        return es ? es->enemy(*w) : nullptr;
    }
    bool alive(const std::string& objectId) {
        Enemy* e = enemy(objectId);
        return e && e->alive();
    }
    /// Steps with Nova kept alive (unless `mortal`): the glowmites bite.
    void steps(int n, bool mortal = false) {
        for (int i = 0; i < n; ++i) {
            if (!mortal && w->player().alive()) w->player().health = std::max(w->player().health, 3);
            w->fixedUpdate(PlayerInput{}, kDt);
        }
    }
    void enter() {
        w->player().teleportTo({700, 800});
        steps(30);
    }
    void killAll(const std::vector<std::string>& ids) {
        for (const auto& id : ids)
            if (Enemy* e = enemy(id); e && e->alive()) e->kill(*w);
        steps(2);
    }
    /// Nova falls; returns once she is back (at the start, out of the arena).
    void fall() {
        w->player().kill(*w);
        for (int i = 0; i < 1200 && !w->player().alive(); ++i) steps(1, true);
        steps(120);
    }
};
}  // namespace

TEST_CASE("ambushes: a fall keeps the ambush as it was, Nova takes it up at the same wave") {
    CHECK(kSimulationVersion == 14);
    CHECK(kRankingGeneration == 8);  // the records are kept (the player asked for it)
    Ambush a;
    a.steps(30);
    a.enter();
    CombatZoneEntity* z = a.zone();
    CHECK(z != nullptr);
    if (!z) return;
    CHECK(z->ambush());
    CHECK(z->fighting());
    CHECK(z->currentWave() == 0);
    CHECK(z->enemiesLeft(*a.w) == 4);
    CHECK(!a.doorOpen(a.doorL));
    CHECK(!a.doorOpen(a.doorR));
    // Wave 1 beaten: wave 2 comes.
    a.killAll(a.wave1);
    a.steps(150);
    CHECK(z->currentWave() == 1);
    CHECK(z->enemiesLeft(*a.w) == 2);
    const Enemy* before0 = a.enemy(a.wave2[0]);
    const Enemy* before1 = a.enemy(a.wave2[1]);
    CHECK(before0 != nullptr && before1 != nullptr);
    if (!before0 || !before1) return;
    const uint32_t uid0 = before0->uid(), uid1 = before1->uid();
    CHECK(before0->alive() && !before0->dormant());

    a.fall();
    CHECK(a.w->player().alive());
    CHECK(a.w->player().center().x < 400.0f);  // back at the start, out of the arena
    // The same ambush: still fought, at its second wave, the same two enemies left.
    CHECK(z->fighting());
    CHECK(z->suspended());
    CHECK(z->currentWave() == 1);
    CHECK(z->enemiesLeft(*a.w) == 2);
    CHECK(!a.alive(a.wave1[0]));
    CHECK(!a.alive(a.wave1[1]));
    CHECK(a.enemy(a.wave2[0]) && a.enemy(a.wave2[0])->uid() == uid0 && a.alive(a.wave2[0]));
    CHECK(a.enemy(a.wave2[1]) && a.enemy(a.wave2[1])->uid() == uid1 && a.alive(a.wave2[1]));
    // Its enemies stayed in their arena, its doors let her back in.
    for (const auto& id : a.wave2)
        if (Enemy* e = a.enemy(id)) CHECK(z->box.expanded(24.0f).contains(e->center()));
    CHECK(a.doorOpen(a.doorL));
    CHECK(a.doorOpen(a.doorR));
    CHECK(a.w->activeAmbush() == z);

    // Back in: the doors close behind her, the fight goes on at wave 2 (never wave 1 again).
    a.enter();
    CHECK(!z->suspended());
    CHECK(!a.doorOpen(a.doorL));
    CHECK(!a.doorOpen(a.doorR));
    CHECK(z->currentWave() == 1);
    CHECK(z->enemiesLeft(*a.w) == 2);
    CHECK(!a.alive(a.wave1[0]));
    CHECK(!a.alive(a.wave1[1]));
    a.killAll(a.wave2);
    a.steps(30);
    CHECK(z->cleared());
    CHECK(z->enemiesLeft(*a.w) == 0);
    CHECK(a.doorOpen(a.doorL));
    CHECK(a.w->activeAmbush() == nullptr);
}

TEST_CASE("ambushes: an ambush cleared before a fall stays cleared") {
    Ambush a;
    a.steps(30);
    a.enter();
    a.killAll(a.wave1);
    a.steps(150);
    a.killAll(a.wave2);
    a.steps(30);
    CombatZoneEntity* z = a.zone();
    CHECK(z && z->cleared());
    a.fall();
    a.enter();
    CHECK(z && z->cleared() && !z->fighting());
    for (const auto& id : a.wave1) CHECK(!a.alive(id));
    for (const auto& id : a.wave2) CHECK(!a.alive(id));
    CHECK(a.doorOpen(a.doorL));
    CHECK(a.doorOpen(a.doorR));
}

TEST_CASE("ambushes: an enemy pushed out of the arena is brought back, the ambush can end") {
    Ambush a;
    a.steps(30);
    a.enter();
    CombatZoneEntity* z = a.zone();
    CHECK(z != nullptr);
    if (!z) return;
    Enemy* e = a.enemy(a.wave1[0]);
    Enemy* f = a.enemy(a.wave1[1]);
    CHECK(e && f);
    if (!e || !f) return;
    a.steps(60);  // awake (out of its appearance)
    // One thrown far to the right, past the door; the other under the level.
    e->box.x = 1600.0f;
    e->prevBox = e->box;
    f->box.y = 2000.0f;
    f->prevBox = f->box;
    a.steps(2);
    CHECK(e->alive() && f->alive());
    CHECK(z->box.contains(e->center()));
    CHECK(z->box.contains(f->center()));
    a.steps(60);
    CHECK(z->box.expanded(24.0f).contains(e->center()));
    CHECK(z->box.expanded(24.0f).contains(f->center()));
    a.killAll(a.wave1);
    a.steps(150);
    a.killAll(a.wave2);
    a.steps(30);
    CHECK(z->cleared());
}

TEST_CASE("ambushes: the HUD's count of the enemies left, and which arenas are ambushes") {
    CHECK(Hud::ambushText(4, 1, 1) == "Embuscade : 4 ennemis restants");
    CHECK(Hud::ambushText(1, 1, 1) == "Embuscade : 1 ennemi restant");
    CHECK(Hud::ambushText(5, 2, 3) == "Embuscade : 5 ennemis restants · vague 2 / 3");
    CHECK(Hud::ambushText(1, 3, 3) == "Embuscade : 1 ennemi restant · vague 3 / 3");
    CHECK(std::string(HudLayout::id(HudElement::Ambush)) == "ambush");
    HudLayout fresh;
    CHECK(fresh[HudElement::Ambush].visible);
    CHECK(fresh[HudElement::Ambush].focus);  // a fight's information: kept in « Mode concentré »
    HudLayout back = HudLayout::fromJson(Json{{"ambush", {{"x", 0.2}, {"y", 0.5}, {"scale", 1.5}, {"visible", false}}}});
    CHECK(back[HudElement::Ambush].custom);
    CHECK(!back[HudElement::Ambush].visible);

    Ambush a(true);  // with a boss's arena too: not an ambush (its own rules)
    CHECK(a.w->ambushes().size() == 1);
    CHECK(!a.w->ambushes().empty() && a.w->ambushes().front()->id() == a.zoneId);
    CHECK(a.w->activeAmbush() == nullptr);
    a.steps(30);
    a.enter();
    CombatZoneEntity* z = a.zone();
    CHECK(a.w->activeAmbush() == z);
    CHECK(z && z->enemyCount() == 4 && z->waveCount() == 2);
    CHECK(z && z->fightingEnemies(*a.w).size() == 2);
    a.killAll({a.wave1[0]});
    CHECK(z && z->enemiesLeft(*a.w) == 3);
    CHECK(z && z->fightingEnemies(*a.w).size() == 1);
}

TEST_CASE("ambushes: a run with a fall in an ambush plays out exactly the same again") {
    auto run = [] {
        Ambush a;
        std::vector<uint64_t> digests;
        for (int i = 0; i < 1800; ++i) {
            PlayerInput in;
            if (i < 240) {
                in.held[static_cast<int>(Action::MoveRight)] = true;
                in.move.x = 1;
            }
            if (i == 240) a.w->player().teleportTo({700, 800});
            if (i == 400) a.killAll(a.wave1);
            if (i == 700) a.w->player().kill(*a.w);
            if (i == 1300) a.w->player().teleportTo({700, 800});
            if (i == 1200) {  // pushed out while she is away: brought back
                if (Enemy* e = a.enemy(a.wave2[0])) {
                    e->box.x = 1500.0f;
                    e->prevBox = e->box;
                }
            }
            if (i < 700 && a.w->player().alive()) a.w->player().health = std::max(a.w->player().health, 3);
            if (i > 1300 && a.w->player().alive()) a.w->player().health = std::max(a.w->player().health, 3);
            a.w->fixedUpdate(in, kDt);
            if (i % 60 == 59) digests.push_back(simulationStateHash(*a.w));
        }
        CombatZoneEntity* z = a.zone();
        CHECK(z && z->fighting() && z->currentWave() == 1);
        return digests;
    };
    const auto first = run();
    const auto second = run();
    CHECK(first.size() == 30);
    CHECK(first == second);
}
