// Singularis, le Cœur Effondré (2.33): the giga boss of the Noyau stellaire and its level,
// « Le Cœur d'une Étoile Mourante » (noyau_03). Eight phases (one every 12.5 % of its health),
// a sheet per phase, rage counted in its last phase, its last light to survive, two prisms that
// cross; its giant arena answers the fight (debris in orbit, the crust floor collapsing into the
// plasma, curtains of plasma closing in, health orbs shown at phases 3, 5 and 7) and is followed
// by the camera at its widest. Everything like the classic guardians, except an echo in
// L'Arène des Échos.

#include <cmath>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/camera/Camera.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "game/progression/Progression.h"
#include "game/race/Race.h"
#include "tests/TestFramework.h"

using namespace cosmic;

// A check that ends the test case when it fails (what follows needs it).
#define REQUIRE(cond)       \
    do {                    \
        CHECK(cond);        \
        if (!(cond)) return; \
    } while (0)

namespace {
constexpr float kDt = 1.0f / 120.0f;
constexpr float kFloor = 800.0f;

ContentDatabase& content() {
    static ContentDatabase* c = nullptr;
    if (!c) {
        c = new ContentDatabase();
        c->reload();
    }
    return *c;
}

const EnemyDefinition& giga() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("singularis");
    CHECK(d != nullptr);
    return d ? *d : none;
}

/// A closed test room (floor y 800, ceiling, walls) as in the guardians' tests.
struct Room {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit Room(uint64_t seed = 7) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_singularis", "Test", "noyau");
        l.intro.steps.clear();
        LevelObject o;
        o.id = "ceiling";
        o.type = "solid";
        o.layer = "terrain";
        o.rect = {0, 260, 2400, 32};
        o.props = {{"material", "obsidian"}};
        l.objects.push_back(o);
        WorldServices s{res, audio, content(), nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->seedGameplay(seed);
        w->load(l);
        w->silent = true;
        for (int i = 0; i < 30; ++i) step();
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    Player& p() { return w->player(); }
    Enemy* spawn(EnemyDefinition d, Vec2 feet, const std::string& objectId = "boss") {
        Enemy* e = w->spawnEnemy(d, feet, true, objectId);
        step();
        return e;
    }
};

/// noyau_03 in a world, the player in front of Singularis's arena.
struct Arena {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Arena() {
        res.init(nullptr, &audio);
        level = content().loadLevel("noyau_03");
        CHECK(level.has_value());
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("noyau_03"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "singularis") bossObject = o.id;
        for (const LevelObject& o : level->objects)
            if (o.type == "combat_zone" && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty());
        CHECK(zone.w > 0.0f);
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    Enemy* boss() {
        auto* sp = dynamic_cast<EnemySpawnEntity*>(w->entity(bossObject));
        return sp ? sp->enemy(*w) : nullptr;
    }
    /// The player on the west floor of the arena, the fight started.
    Enemy* fight() {
        w->startAt({zone.x + 8.0f * 16.0f, zone.bottom() - 4.0f * 16.0f});
        for (int i = 0; i < 10; ++i) step();
        Enemy* b = boss();
        CHECK(b != nullptr);
        if (b) w->startBoss(*b);
        for (int i = 0; i < 90; ++i) step();
        return b;
    }
    std::vector<const LevelObject*> objects(const std::string& type, const char* prop = nullptr) const {
        std::vector<const LevelObject*> out;
        for (const LevelObject& o : level->objects)
            if (o.type == type && zone.contains(o.rect.center()) && (!prop || o.prop<bool>(prop, false))) out.push_back(&o);
        return out;
    }
};
}  // namespace

// ===================================================================== data

TEST_CASE("singularis: a giga boss of eight phases, one every 12.5 % of its health") {
    const EnemyDefinition& d = giga();
    CHECK(d.isBoss && d.builtin && d.flying);
    // About six guardians' health (the guardians of Aethra: 95 to 140), contact 2.
    float guardians = 0.0f;
    for (const char* id : {"mycelia", "aurorax", "pyrogyre", "nyxara"}) guardians += content().enemy(id)->hp / 4.0f;
    CHECK(d.hp >= 5.5f * guardians && d.hp <= 7.0f * guardians);
    CHECK(d.contactDamage == 2.0f);
    REQUIRE(d.phases.size() == 8);
    std::set<std::string> sheets;
    for (size_t i = 0; i < 8; ++i) {
        const BossPhase& ph = d.phases[i];
        CHECK_NEAR(ph.hpThreshold, 1.0 - 0.125 * static_cast<double>(i), 1e-4);
        CHECK(ph.combo == 3);
        CHECK(ph.telegraphMultiplier < 0.9f);
        if (i > 0) CHECK(ph.telegraphMultiplier < d.phases[i - 1].telegraphMultiplier);
        CHECK(!ph.enterText.empty());
        CHECK(ph.lightColor.a > 0);
        // Its own sheet per phase, all of them shipped.
        CHECK(sheets.insert(ph.animation).second);
        CHECK(fs::exists(Paths::assetsDir() / ph.animation));
        for (const auto& aid : ph.attacks) CHECK(d.attack(aid) != nullptr);
    }
    CHECK(d.phases[0].animation == d.animation);
    // The last phase: everything but mirrors and the escort, rage after 40 s in it.
    for (const auto& aid : d.phases[7].attacks) {
        CHECK(d.attack(aid)->type != AttackType::Mirror);
        CHECK(d.attack(aid)->type != AttackType::Summon);
    }
    CHECK(d.phases[7].attacks.size() >= 10);
    CHECK_NEAR(d.phases[7].enrageTime, 40.0, 1e-6);
    CHECK(d.enrageTime == 0.0f);
    // Its escort (phase 7) and its last light (never chosen in a phase).
    CHECK(d.phases[6].summonOnEnter);
    CHECK(content().enemy("eclat_stellaire") != nullptr);
    REQUIRE(!d.finalAttacks.empty());
    for (const auto& aid : d.finalAttacks) {
        CHECK(d.attack(aid) != nullptr);
        for (const BossPhase& ph : d.phases)
            for (const auto& x : ph.attacks) CHECK(x != aid);
    }
    // Crossed prisms: two arms.
    const AttackDef* prisms = d.attack("prismes");
    REQUIRE(prisms != nullptr);
    CHECK(prisms->type == AttackType::Prism && prisms->arms == 2);
}

TEST_CASE("singularis: its new settings round-trip, its looks stay out of the fingerprints") {
    const EnemyDefinition& d = giga();
    EnemyDefinition back = EnemyDefinition::fromJson(d.toJson());
    REQUIRE(back.phases.size() == d.phases.size());
    for (size_t i = 0; i < d.phases.size(); ++i) {
        CHECK(back.phases[i].animation == d.phases[i].animation);
        CHECK(back.phases[i].lightColor == d.phases[i].lightColor);
        CHECK(back.phases[i].enterSound == d.phases[i].enterSound);
        CHECK_NEAR(back.phases[i].enrageTime, d.phases[i].enrageTime, 1e-6);
    }
    CHECK(back.finalAttacks == d.finalAttacks);
    CHECK(back.finalText == d.finalText);
    CHECK(back.attack("maree")->look == "plasma");
    CHECK(back.attack("cage")->look == "star");
    // The fingerprint keeps what plays (rage, last stand), not the looks.
    Json g = gameplayEnemyJson(d);
    CHECK(g["boss"].contains("finalAttacks"));
    CHECK(g["boss"]["phases"][7].contains("enrageTime"));
    for (const auto& ph : g["boss"]["phases"]) {
        CHECK(!ph.contains("animation"));
        CHECK(!ph.contains("lightColor"));
        CHECK(!ph.contains("enterSound"));
    }
    for (const auto& a : g["attacks"]) CHECK(!a.contains("look"));
    // A phase of before writes nothing new (its level keeps its fingerprint).
    Json old = content().enemy("nyxara")->toJson();
    for (const auto& ph : old["boss"]["phases"])
        for (const char* k : {"enrageTime", "animation", "lightColor", "enterSound"}) CHECK(!ph.contains(k));
    CHECK(!old["boss"].contains("finalAttacks"));
}

TEST_CASE("singularis: no echo in L'Arène des Échos") {
    CHECK(content().enemy("echo_singularis") == nullptr);
    auto rush = content().loadLevel("noyau_01");
    REQUIRE(rush.has_value());
    for (const LevelObject& o : rush->objects)
        if (o.type == "enemy") {
            CHECK(o.propStr("enemy").find("singularis") == std::string::npos);
        }
}

TEST_CASE("singularis: Le Cœur d'une Étoile Mourante, EXTRÊME, third level of the Noyau") {
    const PlanetData* p = content().planet("noyau");
    REQUIRE(p != nullptr);
    REQUIRE(p->levels.size() == 3);
    CHECK(p->levels[2] == "noyau_03");
    auto l = content().loadLevel("noyau_03");
    REQUIRE(l.has_value());
    CHECK(l->extreme);
    CHECK(l->oneLife());
    for (const LevelObject& o : l->objects) CHECK(o.type != "checkpoint");
    int bosses = 0;
    for (const LevelObject& o : l->objects)
        if (o.type == "enemy" && o.prop<bool>("boss", false)) {
            ++bosses;
            CHECK(o.propStr("enemy") == "singularis");
        }
    CHECK(bosses == 1);
    // Opened by finishing Le Cœur de l'Étoile.
    Progression prog;
    const LevelEntry* e = content().level("noyau_03");
    REQUIRE(e != nullptr);
    CHECK(!prog.levelUnlocked(*e, content()));
    prog.markCompleted("noyau_02");
    CHECK(prog.levelUnlocked(*e, content()));
}

TEST_CASE("singularis: its level has a verification race like the other EXTRÊME levels") {
    auto l = content().loadLevel("noyau_03");
    REQUIRE(l.has_value());
    RaceLayout race = buildRaceLayout(*l, content(), true);
    REQUIRE(race.bosses.size() == 1);
    CHECK(race.bosses[0].name == "Singularis");
    CHECK(race.ambushes.empty());
    CHECK(race.route.size() >= 2);
    // Ranked like the others: a fingerprint of the level and of everything it uses.
    LevelFingerprints fps;
    const LevelFingerprint* fp = fps.get(content(), "noyau_03");
    REQUIRE(fp != nullptr);
    CHECK(fp->hash.size() == 64);
}

// ===================================================================== the fight

TEST_CASE("singularis: out of health, its last light must be survived, then it dies") {
    Room r;
    r.w->startAt({300.0f, kFloor});
    EnemyDefinition d = giga();
    d.contactDamage = 0.0f;
    Enemy* e = r.spawn(d, {1200.0f, kFloor - 120.0f});
    REQUIRE(e != nullptr);
    e->alert(*r.w);
    for (int i = 0; i < 60; ++i) r.step();
    e->setWounds(1.0f, 7);
    DamageInfo hit;
    hit.amount = 50.0f;
    hit.type = DamageType::Projectile;
    hit.source = e->center() - Vec2{40.0f, 0.0f};
    for (int i = 0; i < 120 && !e->lastStand(); ++i) {
        e->takeDamage(*r.w, hit);
        r.step();
    }
    CHECK(e->lastStand());
    CHECK(e->alive());
    CHECK(e->hp() <= 0.0f);
    // Nothing hurts it any more.
    for (int i = 0; i < 40; ++i) r.step();
    CHECK(!e->takeDamage(*r.w, hit));
    // Its final attacks one after the other, then its end.
    std::set<std::string> seen;
    float t = 0.0f;
    while (e->alive() && t < 40.0f) {
        if (!e->currentAttackName().empty()) seen.insert(e->currentAttackName());
        r.step();
        t += kDt;
    }
    CHECK(!e->alive());
    CHECK(t > 4.0f);
    CHECK(seen.count("Dernière lumière") == 1);
    CHECK(seen.count("Pluie de cendres stellaires") == 1);
}

TEST_CASE("singularis: its last phase enrages after 40 s in it, and the level hears it") {
    Room r;
    r.w->startAt({300.0f, kFloor});
    EnemyDefinition d = giga();
    d.contactDamage = 0.0f;
    for (auto& a : d.attacks) a.damage = 0.0f;  // the player stays in the fight
    Enemy* e = r.spawn(d, {1200.0f, kFloor - 120.0f}, "boss_giga");
    REQUIRE(e != nullptr);
    std::vector<std::string> heard;
    r.w->eventObserver = [&](const std::string& source, const std::string& trigger) {
        if (source == "boss_giga") heard.push_back(trigger);
    };
    e->alert(*r.w);
    e->setWounds(e->maxHp() * 0.1f, 7);
    for (int i = 0; i < static_cast<int>(38.0f / kDt); ++i) r.step();
    CHECK(!e->enraged());
    for (int i = 0; i < static_cast<int>(4.0f / kDt) && !e->enraged(); ++i) r.step();
    CHECK(e->enraged());
    CHECK(std::count(heard.begin(), heard.end(), "bossEnrage") == 1);
}

TEST_CASE("singularis: its crossed prisms trace two beams up both sides") {
    Room r;
    r.w->startAt({1150.0f, kFloor});
    EnemyDefinition d = giga();
    d.contactDamage = 0.0f;
    Enemy* e = r.spawn(d, {1200.0f, kFloor - 160.0f});
    REQUIRE(e != nullptr);
    e->setWounds(e->maxHp() * 0.2f, 6);
    REQUIRE(e->forceAttack(*r.w, "prismes"));
    CHECK(e->state() == EnemyState::Telegraph);
    const auto& a = e->attackPath();
    const auto& b = e->attackPath2();
    REQUIRE(a.size() >= 2);
    REQUIRE(b.size() >= 2);
    CHECK((a[1].x - a[0].x) * (b[1].x - b[0].x) < 0.0f);  // one each way: they cross
    CHECK(a[1].y < a[0].y && b[1].y < b[0].y);              // both go up first
    // A guardian's prism keeps a single beam.
    EnemyDefinition au = *content().enemy("aurorax");
    Enemy* x = r.spawn(au, {600.0f, kFloor});
    REQUIRE(x->forceAttack(*r.w, "prism"));
    CHECK(x->attackPath().size() >= 2);
    CHECK(x->attackPath2().empty());
}

// ===================================================================== the arena

TEST_CASE("singularis: its arena answers the fight, phase after phase") {
    Arena a;
    auto debris = a.objects("moving_platform", "orbit");
    auto crust = a.objects("crumble_platform", "onEvent");
    auto orbs = a.objects("health_orb", "hidden");
    std::vector<const LevelObject*> zones = a.objects("damage_zone");
    REQUIRE(debris.size() == 3);
    REQUIRE(crust.size() >= 10);
    REQUIRE(orbs.size() == 3);
    REQUIRE(zones.size() == 5);  // the basin and four curtains
    Enemy* b = a.fight();
    REQUIRE(b != nullptr);
    // Phases 1-2: the whole floor, the debris parked, the orbs hidden, no plasma.
    const Rect floorProbe = Rect::fromCenter(crust[5]->rect.center(), {8.0f, 8.0f});
    CHECK(a.w->physics().overlapsSolid(floorProbe));
    Vec2 parked = a.w->entity(debris[0]->id)->box.pos();
    for (int i = 0; i < 120; ++i) a.step();
    CHECK(distance(parked, a.w->entity(debris[0]->id)->box.pos()) < 0.5f);
    for (const LevelObject* o : orbs) CHECK(!a.w->entity(o->id)->active);
    for (const LevelObject* z : zones) CHECK(!a.w->entity(z->id)->active);
    // Phase 3: the debris in orbit (on their ellipse), the first orb shown.
    jumpBossToPhase(*a.w, *b, 2);
    for (int i = 0; i < 240; ++i) a.step();
    LevelEntity* dp = a.w->entity(debris[0]->id);
    CHECK(distance(parked, dp->box.pos()) > 20.0f);
    {
        Vec2 c = debris[0]->rect.center();
        float rx = std::abs(debris[0]->prop<float>("dx", 0.0f)), ry = std::abs(debris[0]->prop<float>("dy", 0.0f));
        Vec2 q = dp->box.center() - c;
        CHECK_NEAR(q.x * q.x / (rx * rx) + q.y * q.y / (ry * ry), 1.0, 0.02);
    }
    int shown = 0;
    for (const LevelObject* o : orbs) shown += a.w->entity(o->id)->active ? 1 : 0;
    CHECK(shown == 1);
    // Phase 5: the crust floor collapses into the plasma of the basin.
    CHECK(a.w->physics().overlapsSolid(floorProbe));
    jumpBossToPhase(*a.w, *b, 4);
    for (int i = 0; i < 30; ++i) a.step();
    CHECK(!a.w->physics().overlapsSolid(floorProbe));
    int plasma = 0;
    for (const LevelObject* z : zones) plasma += a.w->entity(z->id)->active ? 1 : 0;
    CHECK(plasma == 1);
    // Phase 7, then 8: the last orb, then the first curtains; its rage closes them further.
    jumpBossToPhase(*a.w, *b, 7);
    for (int i = 0; i < 30; ++i) a.step();
    shown = 0;
    for (const LevelObject* o : orbs) shown += a.w->entity(o->id)->active ? 1 : 0;
    CHECK(shown == 3);
    plasma = 0;
    for (const LevelObject* z : zones) plasma += a.w->entity(z->id)->active ? 1 : 0;
    CHECK(plasma == 3);
    a.w->emit(a.bossObject, "bossEnrage");
    plasma = 0;
    for (const LevelObject* z : zones) plasma += a.w->entity(z->id)->active ? 1 : 0;
    CHECK(plasma == 5);
}

TEST_CASE("singularis: the plasma hurts and throws Nova back up, never kills at once") {
    Arena a;
    Enemy* b = a.fight();
    REQUIRE(b != nullptr);
    jumpBossToPhase(*a.w, *b, 4);
    for (int i = 0; i < 30; ++i) a.step();
    // Nova dropped into the basin.
    auto crust = a.objects("crumble_platform", "onEvent");
    Vec2 feet = crust[6]->rect.center() + Vec2{0.0f, 3.5f * 16.0f};
    a.w->startAt(feet);
    int before = a.w->player().health;
    float minY = a.w->player().box.y;
    for (int i = 0; i < 60; ++i) {
        a.step();
        minY = std::min(minY, a.w->player().box.y);
    }
    CHECK(a.w->player().alive());
    CHECK(a.w->player().health == before - 2);
    CHECK(minY < feet.y - 60.0f);  // thrown well up
}

TEST_CASE("singularis: the camera follows Nova at its widest in the giant arena, leaning to the boss") {
    // Other boss arenas fit on screen (the camera shows them whole): unchanged.
    Camera cam;
    cam.view(1600, 900, 1.0f);
    Arena a;
    Rect giant{a.zone.x - 24.0f, a.zone.y - 24.0f, a.zone.w + 48.0f, a.zone.h + 64.0f};
    CHECK(cam.fitZoom(giant) < Camera::kMinFitZoom);
    auto star = content().loadLevel("noyau_02");
    REQUIRE(star.has_value());
    for (const LevelObject& o : star->objects)
        if (o.type == "combat_zone" && o.propStr("waves").find("o") != std::string::npos && o.rect.w < 1200.0f)
            CHECK(cam.fitZoom({o.rect.x - 24.0f, o.rect.y - 24.0f, o.rect.w + 48.0f, o.rect.h + 64.0f}) >= Camera::kMinFitZoom);
    // In the fight: the widest view, Nova inside it, the framing pulled toward the boss.
    Enemy* b = a.fight();
    REQUIRE(b != nullptr);
    a.w->camera().view(1600, 900, 1.0f);
    for (int i = 0; i < 240; ++i) a.step();
    CHECK_NEAR(a.w->camera().zoom(), Camera::kMinFitZoom, 0.02);
    Rect vis = a.w->camera().visibleArea(1600, 900);
    CHECK(vis.contains(a.w->player().center()));
    float px = a.w->player().center().x, cx = a.w->camera().center().x;
    if (b->center().x > px + 200.0f) CHECK(cx > px + 40.0f);
    if (b->center().x < px - 200.0f) CHECK(cx < px - 40.0f);
}

TEST_CASE("singularis: its fight plays the same twice") {
    auto runOnce = [](uint64_t seed) -> uint64_t {
        Arena a;
        Enemy* b = a.fight();
        if (!b) return uint64_t{0};
        jumpBossToPhase(*a.w, *b, 4);
        uint64_t h = 0;
        for (int i = 0; i < 20 * 120; ++i) {
            PlayerInput in;
            int k = (i / 90 + static_cast<int>(seed)) % 4;
            in.move = {k == 0 ? -1.0f : (k == 2 ? 1.0f : 0.0f), 0.0f};
            if (k == 0) in.held[static_cast<int>(Action::MoveLeft)] = true;
            if (k == 2) in.held[static_cast<int>(Action::MoveRight)] = true;
            if (i % 50 == 0) in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
            a.step(in);
            if ((i + 1) % 120 == 0) h = h * 1099511628211ull ^ simulationStateHash(*a.w);
        }
        return h;
    };
    CHECK(runOnce(3) == runOnce(3));
    CHECK(runOnce(3) != runOnce(5));
}
