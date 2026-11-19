// Pyrrhax, le Dragon d'Obsidienne (2.48), and the attacks of its own (2.50): « Piqué » (a line
// shown, held still, then a dash through Nova and beyond), « Atterrissage » (it lands where she
// stands, breathes a lingering cloud of fire along the ground, is exposed while landed),
// « Boule de feu à nuage » (a burning cloud where the fireball bursts) and « Rafale d'ailes »
// (its wings push her away: running against them still wins). Right under the dragon is no
// shelter any more (« si je reste en dessous il m'attaque pas beaucoup »).

#include <cmath>
#include <cstdio>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/BossDifficulty.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "tests/TestFramework.h"

using namespace cosmic;

// A check that ends the test case when it fails (what follows needs it).
#define REQUIRE(cond)        \
    do {                     \
        CHECK(cond);         \
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

const EnemyDefinition& dragon() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("pyrrhax");
    CHECK(d != nullptr);
    return d ? *d : none;
}

/// A closed test room (floor y 800, ceiling at 260), the dragon right above the player.
struct Room {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    /// `rules`: the version of the simulation (0: the game's own).
    explicit Room(uint64_t seed = 7, int rules = 0) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_pyrrhax", "Test", "pyralis");
        l.intro.steps.clear();
        for (auto& o : l.objects)
            if (o.type == "solid") o.rect = {0, 800, 2400, 160};
        LevelObject o;
        o.id = "ceiling";
        o.type = "solid";
        o.layer = "terrain";
        o.rect = {0, 260, 2400, 32};
        o.props = {{"material", "obsidian"}};
        l.objects.push_back(o);
        WorldServices s{res, audio, content(), nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        if (rules > 0) w->setSimulationVersion(rules);
        w->seedGameplay(seed);
        w->load(l);
        w->silent = true;
        w->player().spawn(*w, {700, kFloor}, false, false);
        for (int i = 0; i < 30; ++i) step();
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    Player& p() { return w->player(); }
    /// The dragon, its feet at `feet`, engaged.
    Enemy* spawn(Vec2 feet) {
        Enemy* e = w->spawnEnemy("pyrrhax", feet, true, "boss");
        if (e) e->alert(*w);
        step();
        return e;
    }
};

PlayerInput run(float dir, bool dash = false) {
    PlayerInput in;
    in.held[static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft)] = true;
    in.move.x = dir;
    if (dash) in.pressed[static_cast<int>(Action::Dash)] = in.held[static_cast<int>(Action::Dash)] = true;
    return in;
}

/// confins_01 (« La Gueule du Dragon ») in a world, the fight started at `phase`.
struct Maw {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Enemy* boss = nullptr;
    explicit Maw(int phase) {
        res.init(nullptr, &audio);
        level = content().loadLevel("confins_01");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("confins_01"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "pyrrhax") bossObject = o.id;
        for (const LevelObject& o : level->objects)
            if (o.type == "combat_zone" && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty() && zone.w > 0.0f);
        auto* sp = dynamic_cast<EnemySpawnEntity*>(w->entity(bossObject));
        boss = sp ? sp->enemy(*w) : nullptr;
        CHECK(boss != nullptr);
        if (!boss) return;
        // On the floor that stays (the crust over the basin gives way at the 5th phase).
        w->startAt({zone.x + (phase >= 4 ? 10.0f : 48.0f) * 16.0f, zone.bottom() - 4.0f * 16.0f});
        for (int i = 0; i < 10; ++i) step();
        w->startBoss(*boss);
        if (phase > 0) jumpBossToPhase(*w, *boss, phase);
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
};

std::set<AttackType> dragonTypes() { return {AttackType::Dive, AttackType::Perch, AttackType::CloudShot, AttackType::Gust}; }
}  // namespace

// ===================================================================== data

TEST_CASE("pyrrhax: four attack types of its own, named in French, saved only where used") {
    std::set<std::string> ids, labels;
    for (int i = 0; i < static_cast<int>(AttackType::Count); ++i) {
        AttackType t = static_cast<AttackType>(i);
        CHECK(ids.insert(attackTypeId(t)).second);
        CHECK(labels.insert(attackTypeLabel(t)).second);
    }
    for (AttackType t : dragonTypes()) {
        AttackDef a;
        a.type = t;
        CHECK(a.isDragonType() && !a.isGuardianType());
        CHECK(enumFromId(std::string(attackTypeId(t)), &attackTypeId, AttackType::Melee) == t);
    }
    // Round trip of what they use.
    AttackDef a;
    a.type = AttackType::Perch;
    a.lungeSpeed = 640.0f;
    a.radius = 320.0f;
    a.height = 44.0f;
    a.duration = 3.5f;
    a.arms = 2;
    a.vulnerability = 1.5f;
    a.count = 3;
    a.gravity = 520.0f;
    AttackDef b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Perch && b.arms == 2 && b.count == 3);
    CHECK_NEAR(b.lungeSpeed, 640.0, 1e-6);
    CHECK_NEAR(b.radius, 320.0, 1e-6);
    CHECK_NEAR(b.height, 44.0, 1e-6);
    CHECK_NEAR(b.duration, 3.5, 1e-6);
    CHECK_NEAR(b.vulnerability, 1.5, 1e-6);
    CHECK_NEAR(b.gravity, 520.0, 1e-6);
    // A push is never as fast as Nova runs (150 px/s).
    a.type = AttackType::Gust;
    Json j = a.toJson();
    j["pull"] = 500;
    CHECK(AttackDef::fromJson(j).pull < 150.0f);
    // The attacks of before are written as before (their levels keep their fingerprints).
    for (const EnemyDefinition* d : content().enemies())
        for (const AttackDef& x : d->attacks) {
            if (x.isDragonType() || x.isGuardianType() || x.isGolemType()) continue;  // 2.52: the golem's shell too
            CHECK(!x.toJson().contains("vulnerability"));
        }
}

TEST_CASE("pyrrhax: its new attacks spread over its eight phases, a dive from the first") {
    const EnemyDefinition& d = dragon();
    REQUIRE(d.phases.size() == 8);
    CHECK(d.flying);
    std::set<AttackType> used;
    for (size_t i = 0; i < d.phases.size(); ++i) {
        bool comesDown = false;  // something that comes down on a player right under it
        for (const auto& id : d.phases[i].attacks) {
            const AttackDef* a = d.attack(id);
            REQUIRE(a != nullptr);
            if (a->isDragonType()) used.insert(a->type);
            if (a->type == AttackType::Dive || a->type == AttackType::Perch || a->type == AttackType::Rain ||
                a->type == AttackType::CloudShot)
                comesDown = true;
        }
        CHECK(comesDown);
    }
    CHECK(used == dragonTypes());
    // The first phase dives already (the fight is never passive under it).
    bool dives = false;
    for (const auto& id : d.phases[0].attacks) dives = dives || d.attack(id)->type == AttackType::Dive;
    CHECK(dives);
    // Chained dives later (2 then 3), a landing breathing on both sides in the last phase.
    int most = 0;
    for (const auto& id : d.phases[7].attacks) {
        const AttackDef* a = d.attack(id);
        if (a->type == AttackType::Dive) most = std::max(most, a->count);
        if (a->type == AttackType::Perch) CHECK(a->arms == 2);
    }
    CHECK(most == 3);
    // Its last fire: a last round of dives among its final attacks (never chosen in a phase).
    bool lastDives = false;
    for (const auto& id : d.finalAttacks) {
        REQUIRE(d.attack(id) != nullptr);
        lastDives = lastDives || d.attack(id)->type == AttackType::Dive;
    }
    CHECK(lastDives);
    // Still a giga boss.
    CHECK(bossDifficulty(d).stars == 10);
}

// ===================================================================== the dive

TEST_CASE("pyrrhax: the dive shows its line, holds it still, then strikes through and beyond") {
    Room r;
    Enemy* e = r.spawn({1000.0f, kFloor - 260.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "pique"));
    int start = r.p().health;
    int lockedSteps = 0;
    bool dashed = false, hit = false;
    Vec2 mark, dir, prey;
    float closest = 1e9f, beyond = 0.0f;
    for (int i = 0; i < 120 * 4; ++i) {
        if (e->state() == EnemyState::Telegraph) {
            REQUIRE(e->attackPath().size() >= 2);
            if (e->diveLocked()) ++lockedSteps;
            mark = e->attackMark();
            dir = (e->attackPath()[1] - e->attackPath()[0]).normalized();
            prey = r.p().center();
        }
        r.step();
        if (r.p().health < start) hit = true;
        if (e->diving()) {
            dashed = true;
            closest = std::min(closest, distance(e->center(), mark));
            beyond = std::max(beyond, (e->center().x - mark.x) * dir.x + (e->center().y - mark.y) * dir.y);
        }
        if (dashed && e->state() == EnemyState::Recover) break;
    }
    // The line holds still long enough to step out of it (at least 0.4 s before the dash).
    CHECK(lockedSteps >= static_cast<int>(0.4f * 120.0f));
    // Aimed at the player (who stood still), steep (it comes down from above).
    CHECK(distance(mark, prey) < 40.0f);
    CHECK(dir.y > 0.5f);
    CHECK(dashed);
    CHECK(hit);
    // Through the point (its body over it) and beyond it, skimming the ground once there.
    CHECK(closest < 45.0f);
    CHECK(beyond > 60.0f);
    // Then it climbs back to its hunting height.
    CHECK(e->state() == EnemyState::Recover);
    CHECK(e->box.bottom() < kFloor - 100.0f);
}

// With the rules of 16 (its fight of 2.76.0, where only its red parts hurt). 2.76.2: with those of 17 its whole body
// hurts on contact (the player's rule) and its wings, 160 px on each side, reach a player who only ran out of its lane.
TEST_CASE("pyrrhax: a dive can be dodged once its line holds still (rules of 16)") {
    Room r(7, 16);
    Enemy* e = r.spawn({700.0f, kFloor - 190.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "pique"));
    int start = r.p().health;
    bool dashed = false;
    bool dashedOnce = false;
    for (int i = 0; i < 120 * 4; ++i) {
        // Out of its lane as soon as it holds still: running, with a dash.
        PlayerInput in;
        if (e->diveLocked() || dashed) {
            in = run(1.0f, !dashedOnce);
            dashedOnce = true;
        }
        r.step(in);
        dashed = dashed || e->diving();
        if (dashed && e->state() == EnemyState::Recover) break;
    }
    CHECK(dashed);
    CHECK(r.p().health == start);
}

TEST_CASE("pyrrhax: the double dive aims again before each dash") {
    Room r;
    Enemy* e = r.spawn({900.0f, kFloor - 200.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "double_pique"));
    int dashes = 0;
    bool wasDiving = false;
    for (int i = 0; i < 120 * 8 && e->state() != EnemyState::Recover; ++i) {
        r.step();
        if (e->diving() && !wasDiving) ++dashes;
        wasDiving = e->diving();
        r.p().health = r.p().maxHealth();
    }
    CHECK(dashes == 2);
    CHECK(e->state() == EnemyState::Recover);
}

// ===================================================================== landing

TEST_CASE("pyrrhax: it lands where Nova stands, breathes a lasting cloud of fire, is exposed while landed") {
    Room r;
    Enemy* e = r.spawn({1100.0f, kFloor - 200.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "atterrissage"));
    // Its mark: on the floor, where the player stands.
    CHECK(std::abs(e->attackMark().y - kFloor) < 1.0f);
    CHECK(std::abs(e->attackMark().x - r.p().center().x) < 60.0f);
    // The player steps back out of its way, then stays still.
    int start = r.p().health;
    bool perched = false, tookMore = false;
    float cloudLife = 0.0f;
    float cloudLeft = 0.0f, cloudRight = 0.0f;
    for (int i = 0; i < 120 * 8; ++i) {
        r.step(i < 50 ? run(-1.0f) : PlayerInput{});
        if (e->perched() && !perched) {
            perched = true;
            CHECK(std::abs(e->box.bottom() - kFloor) < 1.0f);
            // A blow while it is landed weighs more than the same blow in the air.
            float before = e->hp();
            DamageInfo d;
            d.amount = 10.0f;
            d.type = DamageType::Projectile;
            d.source = e->center() - Vec2{30.0f, 0.0f};
            d.fromPlayer = true;
            e->takeDamage(*r.w, d);
            tookMore = before - e->hp() > 14.0f;
        }
        for (const auto& c : r.w->hazards().clouds())
            if (c.owner == e->uid() && c.ground) {
                cloudLife += kDt;
                cloudLeft = c.area.x;
                cloudRight = c.area.right();
                break;
            }
    }
    CHECK(perched);
    CHECK(tookMore);
    // The cloud stayed a few seconds along the ground, toward the player (on its left).
    CHECK(cloudLife > 3.0f);
    CHECK(cloudRight - cloudLeft > 200.0f);
    CHECK(cloudLeft < r.p().center().x);
    // The player standing in it got burnt.
    CHECK(r.p().health < start);
    // It took off again.
    CHECK(!e->perched());
    CHECK(e->box.bottom() < kFloor - 100.0f);
}

// ============================================================ fireballs, gust

TEST_CASE("pyrrhax: its fireballs leave burning clouds where they burst") {
    Room r;
    Enemy* e = r.spawn({1150.0f, kFloor - 200.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "nuees"));
    r.step();
    // Their impacts are shown on the ground during the wind-up.
    REQUIRE(e->attackPath().size() == 3);
    for (const Vec2& m : e->attackPath()) CHECK(std::abs(m.y - kFloor) < 1.0f);
    int start = r.p().health;
    size_t most = 0;
    for (int i = 0; i < 120 * 4; ++i) {
        r.step();
        size_t n = 0;
        for (const auto& c : r.w->hazards().clouds())
            if (c.owner == e->uid()) ++n;
        most = std::max(most, n);
    }
    CHECK(most == 3);
    CHECK(r.p().health < start);
}

TEST_CASE("pyrrhax: the gust of its wings pushes Nova away, but running against it still wins") {
    for (bool against : {false, true}) {
        Room r;
        Enemy* e = r.spawn({950.0f, kFloor - 160.0f});
        REQUIRE(e != nullptr);
        REQUIRE(e->forceAttack(*r.w, "rafale"));
        while (e->state() == EnemyState::Telegraph) r.step();
        float x0 = r.p().feet().x;
        bool blown = false;
        for (int i = 0; i < 120 * 2; ++i) {
            r.step(against ? run(1.0f) : PlayerInput{});
            r.p().health = r.p().maxHealth();
            blown = blown || r.w->hazards().hasWind(e->uid());
            if (against && r.p().feet().x > 880.0f) break;  // never into its body
        }
        CHECK(blown);
        if (against) CHECK(r.p().feet().x > x0 + 40.0f);
        else CHECK(r.p().feet().x < x0 - 60.0f);
    }
}

// ================================================================= the fight

TEST_CASE("pyrrhax: right under the dragon is no shelter, in every phase") {
    // The player stands still under it for 20 s in its arena: it comes down on her.
    for (int phase = 0; phase < 8; ++phase) {
        Maw m(phase);
        REQUIRE(m.boss != nullptr);
        // The dragon right above her.
        Vec2 pc = m.w->player().center();
        m.boss->box = Rect::fromCenter({pc.x + 20.0f, pc.y - 210.0f}, m.boss->box.size());
        m.boss->prevBox = m.boss->box;
        int hits = 0, dives = 0;
        std::set<std::string> by;
        for (int i = 0; i < 20 * 120; ++i) {
            int before = m.w->player().health;
            m.step();
            if (m.w->player().health < before) {
                ++hits;
                by.insert(m.boss->currentAttackName());
            }
            if (m.boss->diving()) ++dives;
            m.w->player().health = m.w->player().maxHealth();
        }
        std::printf("  phase %d : %d coups en 20 s sous le dragon (%zu attaques différentes)\n", phase + 1, hits, by.size());
        // Before 2.50 the dragon stayed up there: 2 to 4 blows in most phases (it hovered over
        // her with breaths and fans aimed past her); now it dives, lands, rains on her.
        CHECK(hits >= (phase == 0 ? 5 : 4));
        if (phase == 0) CHECK(dives > 0);
    }
}

TEST_CASE("pyrrhax: every new attack is chosen in the fight, the dragon stays in its arena") {
    std::set<AttackType> used;
    for (int phase = 0; phase < 8; ++phase) {
        Maw m(phase);
        REQUIRE(m.boss != nullptr);
        float dir = 1.0f;
        EnemyState last = EnemyState::Idle;
        for (int i = 0; i < 25 * 120; ++i) {
            // A player who keeps moving and jumping on the floor that stays.
            float x = m.w->player().feet().x;
            float lo = m.zone.x + (phase >= 4 ? 3.0f : 10.0f) * 16.0f, hi = m.zone.x + (phase >= 4 ? 20.0f : 86.0f) * 16.0f;
            if (x > hi) dir = -1.0f;
            if (x < lo) dir = 1.0f;
            PlayerInput in = run(dir);
            if (i % 90 == 0) in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
            m.step(in);
            m.w->player().health = m.w->player().maxHealth();
            if (!m.w->player().alive()) break;
            if (m.boss->state() == EnemyState::Telegraph && last != EnemyState::Telegraph)
                for (const AttackDef& a : m.boss->def().attacks)
                    if (a.name == m.boss->currentAttackName()) used.insert(a.type);
            last = m.boss->state();
            CHECK(m.zone.expanded(8.0f).containsRect(m.boss->box));
        }
    }
    for (AttackType t : dragonTypes()) {
        if (!used.count(t)) std::printf("  jamais choisie : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
}
