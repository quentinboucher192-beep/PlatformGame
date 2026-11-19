// Monolithe, le Golem Primordial (2.49), its brain and its attacks of 2.52 (« trop bizarre le
// Monolithe, des fois il reste en bas à droite et à gauche de l'arène ») : « Carapace roulante »
// (it curls into its rune shell and rolls at Nova, bouncing off the walls; armoured curled, dizzy
// after; a double jump clears it), « Tremblement de terre » (cracks burst under a player who
// stands still, rocks fall from the roof on marked spots) and « Bond titanesque » (out of sight,
// its shadow hunts her, it falls on her, its shockwaves run along the floor and can be jumped).
// In its arena it walks to its fighting distance on the firm floor between the pits: never
// parked in a corner nor at the edge of a pit, never in a pit.

#include <cmath>
#include <cstdio>
#include <optional>
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

const EnemyDefinition& golem() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("monolithe");
    CHECK(d != nullptr);
    return d ? *d : none;
}

std::set<AttackType> golemTypes() { return {AttackType::Shell, AttackType::Quake, AttackType::Skyfall}; }

/// A closed test room: floor y 800 from x 300 to 1700 between two walls, a ceiling at 260.
struct Room {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit Room(float playerX = 700.0f, uint64_t seed = 7) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_monolithe", "Test", "pyralis");
        l.intro.steps.clear();
        for (auto& o : l.objects)
            if (o.type == "solid") o.rect = {0, 800, 2400, 160};
        auto solid = [&](const char* id, Rect r) {
            LevelObject o;
            o.id = id;
            o.type = "solid";
            o.layer = "terrain";
            o.rect = r;
            o.props = {{"material", "obsidian"}};
            l.objects.push_back(o);
        };
        solid("ceiling", {0, 260, 2400, 32});
        solid("wall_l", {268, 260, 32, 540});
        solid("wall_r", {1700, 260, 32, 540});
        WorldServices s{res, audio, content(), nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->seedGameplay(seed);
        w->load(l);
        w->silent = true;
        w->player().spawn(*w, {playerX, kFloor}, false, false);
        for (int i = 0; i < 30; ++i) step();
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    Player& p() { return w->player(); }
    Enemy* spawn(Vec2 feet) {
        Enemy* e = w->spawnEnemy("monolithe", feet, true, "boss");
        if (e) e->alert(*w);
        step();
        return e;
    }
};

PlayerInput run(float dir) {
    PlayerInput in;
    in.held[static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft)] = true;
    in.move.x = dir;
    return in;
}

PlayerInput jump(PlayerInput in = {}) {
    in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
    return in;
}

/// confins_02 (« Le Cœur de Pierre ») in a world, the fight started at `phase`, the player at
/// `tile` (columns of the arena).
struct Heart {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Enemy* boss = nullptr;
    Heart(int phase, float tile) {
        res.init(nullptr, &audio);
        level = content().loadLevel("confins_02");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("confins_02"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "monolithe") bossObject = o.id;
        for (const LevelObject& o : level->objects)
            if (o.type == "combat_zone" && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty() && zone.w > 0.0f);
        auto* sp = dynamic_cast<EnemySpawnEntity*>(w->entity(bossObject));
        boss = sp ? sp->enemy(*w) : nullptr;
        CHECK(boss != nullptr);
        if (!boss) return;
        w->startAt({zone.x + tile * 16.0f, zone.bottom() - 4.0f * 16.0f});
        for (int i = 0; i < 10; ++i) step();
        w->startBoss(*boss);
        if (phase > 0) jumpBossToPhase(*w, *boss, phase);
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    float floorY() const { return zone.bottom() - 4.0f * 16.0f; }
};

const AttackDef* chosen(const Enemy& e) {
    for (const AttackDef& a : e.def().attacks)
        if (a.name == e.currentAttackName()) return &a;
    return nullptr;
}
}  // namespace

// ===================================================================== data

TEST_CASE("monolithe: three attack types of its own, named in French, saved only where used") {
    std::set<std::string> ids, labels;
    for (int i = 0; i < static_cast<int>(AttackType::Count); ++i) {
        AttackType t = static_cast<AttackType>(i);
        CHECK(ids.insert(attackTypeId(t)).second);
        CHECK(labels.insert(attackTypeLabel(t)).second);
    }
    for (AttackType t : golemTypes()) {
        AttackDef a;
        a.type = t;
        CHECK(a.isGolemType() && !a.isDragonType() && !a.isGuardianType());
        CHECK(enumFromId(std::string(attackTypeId(t)), &attackTypeId, AttackType::Melee) == t);
    }
    // Round trips of what they use.
    AttackDef a;
    a.type = AttackType::Shell;
    a.lungeSpeed = 430.0f;
    a.bounces = 2;
    a.duration = 1.3f;
    a.vulnerability = 1.5f;
    a.armor = 0.35f;
    AttackDef b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Shell && b.bounces == 2);
    CHECK_NEAR(b.lungeSpeed, 430.0, 1e-6);
    CHECK_NEAR(b.duration, 1.3, 1e-6);
    CHECK_NEAR(b.vulnerability, 1.5, 1e-6);
    CHECK_NEAR(b.armor, 0.35, 1e-6);
    a = AttackDef{};
    a.type = AttackType::Quake;
    a.count = 5;
    a.spread = 150.0f;
    a.stagger = 0.75f;
    a.arms = 3;
    a.radius = 22.0f;
    a.height = 46.0f;
    a.burstInterval = 0.85f;
    a.projectileRadius = 13.0f;
    b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Quake && b.count == 5 && b.arms == 3);
    CHECK_NEAR(b.spread, 150.0, 1e-6);
    CHECK_NEAR(b.stagger, 0.75, 1e-6);
    CHECK_NEAR(b.radius, 22.0, 1e-6);
    CHECK_NEAR(b.height, 46.0, 1e-6);
    CHECK_NEAR(b.burstInterval, 0.85, 1e-6);
    CHECK_NEAR(b.projectileRadius, 13.0, 1e-6);
    a = AttackDef{};
    a.type = AttackType::Skyfall;
    a.count = 2;
    a.duration = 0.95f;
    a.projectileSpeed = 300.0f;
    a.radius = 26.0f;
    b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Skyfall && b.count == 2);
    CHECK_NEAR(b.duration, 0.95, 1e-6);
    CHECK_NEAR(b.projectileSpeed, 300.0, 1e-6);
    CHECK_NEAR(b.radius, 26.0, 1e-6);
    // Armour never above normal damage.
    Json j = a.toJson();
    j["armor"] = 3;
    CHECK(AttackDef::fromJson(j).armor <= 1.0f);
    // The attacks of before are written as before (their levels keep their fingerprints).
    for (const EnemyDefinition* d : content().enemies())
        for (const AttackDef& x : d->attacks) {
            if (x.isGolemType()) continue;
            CHECK(!x.toJson().contains("armor"));
        }
}

TEST_CASE("monolithe: its new attacks over its eight phases with the old ones, from the first phase") {
    const EnemyDefinition& d = golem();
    REQUIRE(d.phases.size() == 8);
    CHECK(!d.flying);
    std::set<AttackType> used, old;
    for (const auto& ph : d.phases)
        for (const auto& id : ph.attacks) {
            const AttackDef* a = d.attack(id);
            REQUIRE(a != nullptr);
            (a->isGolemType() ? used : old).insert(a->type);
        }
    CHECK(used == golemTypes());
    // Its good old blows stay: fists, glare, rocks, rings, magma, geysers, runes, core, cage, escort.
    CHECK(old.size() >= 12);
    // The fight starts strong: its shell and its titanic leap in the first phase.
    std::set<AttackType> first;
    for (const auto& id : d.phases[0].attacks) first.insert(d.attack(id)->type);
    CHECK(first.count(AttackType::Shell) == 1);
    CHECK(first.count(AttackType::Skyfall) == 1);
    // Its last stand has a last quake.
    bool lastQuake = false;
    for (const auto& id : d.finalAttacks) {
        REQUIRE(d.attack(id) != nullptr);
        lastQuake = lastQuake || d.attack(id)->type == AttackType::Quake;
    }
    CHECK(lastQuake);
    // Still a giga boss.
    CHECK(bossDifficulty(d).stars == 10);
}

// ===================================================================== the shell

TEST_CASE("monolithe: the shell rolls at Nova, bounces off the wall, armoured curled, dizzy after") {
    Room r(700.0f);
    Enemy* e = r.spawn({1200.0f, kFloor});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "carapace"));
    // Its lane is shown during the wind-up, toward the player first.
    REQUIRE(e->attackPath().size() >= 3);
    CHECK(e->attackPath()[1].x < e->attackPath()[0].x);
    int start = r.p().health;
    bool rolled = false, bounced = false, dizzy = false, curledLess = false, dizzyMore = false;
    float lastVx = 0.0f;
    for (int i = 0; i < 120 * 10 && e->state() != EnemyState::Recover; ++i) {
        r.step();
        if (e->rolling()) {
            rolled = true;
            CHECK(e->box.h < 80.0f);  // a boulder a double jump clears
            if (lastVx < -50.0f && e->vel.x > 50.0f) bounced = true;
            lastVx = e->vel.x;
            if (!curledLess) {
                float before = e->hp();
                DamageInfo d;
                d.amount = 10.0f;
                d.type = DamageType::Projectile;
                d.source = e->center() - Vec2{30.0f, 0.0f};
                d.fromPlayer = true;
                e->takeDamage(*r.w, d);
                curledLess = before - e->hp() > 0.0f && before - e->hp() < 5.0f;
            }
        }
        if (e->dizzy() && !dizzy) {
            dizzy = true;
            CHECK(e->box.h > 150.0f);  // standing again
            float before = e->hp();
            DamageInfo d;
            d.amount = 10.0f;
            d.type = DamageType::Projectile;
            d.source = e->center() - Vec2{30.0f, 0.0f};
            d.fromPlayer = true;
            e->takeDamage(*r.w, d);
            dizzyMore = before - e->hp() > 14.0f;
        }
    }
    CHECK(rolled);
    CHECK(bounced);
    CHECK(curledLess);
    CHECK(dizzy);
    CHECK(dizzyMore);
    // It ran over the player standing still.
    CHECK(r.p().health < start);
    // It stops on its floor, away from the walls.
    CHECK(e->center().x > 300.0f + 100.0f && e->center().x < 1700.0f - 100.0f);
    CHECK(std::abs(e->box.bottom() - kFloor) < 1.0f);
}

TEST_CASE("monolithe: a double jump clears the rolling shell") {
    Room r(800.0f);
    Enemy* e = r.spawn({1250.0f, kFloor});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "carapace"));
    int start = r.p().health;
    int passes = 0;
    float side = signf(e->center().x - r.p().center().x);
    bool jumping = false, doubled = false;
    for (int i = 0; i < 120 * 10 && e->state() != EnemyState::Recover; ++i) {
        PlayerInput in;
        if (jumping && doubled && r.p().grounded()) jumping = false;
        if (jumping) in.held[static_cast<int>(Action::Jump)] = true;  // held: a full jump
        if (e->rolling()) {
            const float gap = std::abs(e->center().x - r.p().center().x) - e->box.w * 0.5f;
            const bool coming = (e->vel.x < 0.0f) == (e->center().x > r.p().center().x);
            if (!jumping && coming && gap < 170.0f && r.p().grounded()) {
                in = jump();
                jumping = true;
                doubled = false;
            } else if (jumping && !doubled && !r.p().grounded() && r.p().vel.y > -30.0f) {
                in = jump();  // the double jump at the top of the first
                doubled = true;
            }
        }
        r.step(in);
        float now = signf(e->center().x - r.p().center().x);
        if (now != side && now != 0.0f) {
            ++passes;
            side = now;
        }
        // Once it has rolled past her (its whole body beyond her), the jump has done its job.
        if (passes >= 1 && std::abs(e->center().x - r.p().center().x) > e->box.w * 0.5f + 12.0f) break;
    }
    CHECK(passes >= 1);
    CHECK(r.p().health == start);
}

// ===================================================================== the quake

TEST_CASE("monolithe: the quake's cracks burst under a player who stands still, a runner gets away") {
    int still = 0, runner = 0;
    for (bool moving : {false, true}) {
        Room r(800.0f);
        Enemy* e = r.spawn({1400.0f, kFloor});
        REQUIRE(e != nullptr);
        REQUIRE(e->forceAttack(*r.w, "seisme"));
        int hits = 0;
        bool cracks = false, rocks = false, underHer = false;
        float dir = -1.0f;
        for (int i = 0; i < 120 * 6; ++i) {
            PlayerInput in;
            if (moving) {
                float x = r.p().feet().x;
                if (x < 400.0f) dir = 1.0f;
                if (x > 1150.0f) dir = -1.0f;
                in = run(dir);
            }
            int before = r.p().health;
            r.step(in);
            if (r.p().health < before) ++hits;
            r.p().health = r.p().maxHealth();
            for (const auto& c : r.w->hazards().cracks()) {
                if (c.owner != e->uid()) continue;
                (c.rock ? rocks : cracks) = true;
                if (!c.rock && std::abs(c.at.x - r.p().center().x) < 4.0f && !moving) underHer = true;
            }
        }
        CHECK(cracks);
        CHECK(rocks);
        if (!moving) CHECK(underHer);
        (moving ? runner : still) = hits;
    }
    std::printf("  séisme : %d coups au joueur immobile, %d au joueur qui court\n", still, runner);
    CHECK(still >= 2);
    CHECK(runner < still);
    CHECK(runner <= 1);
}

// ============================================================ the titanic leap

TEST_CASE("monolithe: the titanic leap goes out of sight, its shadow hunts Nova, it falls on her") {
    Room r(900.0f);
    Enemy* e = r.spawn({1300.0f, kFloor});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "bond_titan"));
    int start = r.p().health;
    bool hidden = false, locked = false, landed = false, invulnerable = false;
    float shadowAtLock = 0.0f, playerAtLock = 0.0f;
    for (int i = 0; i < 120 * 6 && e->state() != EnemyState::Recover; ++i) {
        // She walks a little during its flight, then stays.
        r.step(i < 90 ? run(-1.0f) : PlayerInput{});
        if (e->outOfSight()) {
            hidden = true;
            CHECK(e->box.bottom() < kFloor - 400.0f);  // high above the floor (above an arena's roof)
            if (!invulnerable) {
                DamageInfo d;
                d.amount = 10.0f;
                d.type = DamageType::ChargeShot;
                d.source = e->center();
                d.fromPlayer = true;
                invulnerable = !e->takeDamage(*r.w, d);
            }
        }
        if (e->shadowLocked() && !locked) {
            locked = true;
            shadowAtLock = e->shadow().x;
            playerAtLock = r.p().center().x;
        }
        if (hidden && !e->skyborne() && !landed) {
            landed = true;
            CHECK(std::abs(e->box.bottom() - kFloor) < 1.0f);
        }
    }
    CHECK(hidden);
    CHECK(invulnerable);
    CHECK(locked);
    CHECK(std::abs(shadowAtLock - playerAtLock) < 40.0f);  // its shadow found her
    CHECK(landed);
    CHECK(r.p().health < start);  // crushed: she stood under it
    CHECK(std::abs(e->center().x - playerAtLock) < 40.0f);
}

TEST_CASE("monolithe: the shockwaves of its landing run along the floor and can be jumped") {
    for (bool jumper : {false, true}) {
        Room r(700.0f);
        Enemy* e = r.spawn({1150.0f, kFloor});
        REQUIRE(e != nullptr);
        REQUIRE(e->forceAttack(*r.w, "bond_titan"));
        int start = r.p().health;
        bool waves = false;
        // The player steps out of the shadow's way, then waits away from it.
        for (int i = 0; i < 120 * 6; ++i) {
            PlayerInput in;
            const Vec2 pc = r.p().center();
            if (e->skyborne() && std::abs(e->shadow().x - pc.x) < 150.0f) in = run(pc.x < e->shadow().x ? -1.0f : 1.0f);
            if (jumper) {
                if (!r.p().grounded()) in.held[static_cast<int>(Action::Jump)] = true;  // a full jump
                for (const auto& p : r.w->projectiles())
                    if (p.clip == "shockwave" && !p.fromPlayer && std::abs(p.pos.x - pc.x) < 34.0f && (p.vel.x > 0.0f) == (p.pos.x < pc.x) &&
                        r.p().grounded())
                        in = jump(in);
            }
            r.step(in);
            for (const auto& p : r.w->projectiles()) waves = waves || (p.clip == "shockwave" && !p.fromPlayer);
            if (e->state() == EnemyState::Recover && i > 120 * 4) break;
        }
        CHECK(waves);
        if (jumper) CHECK(r.p().health == start);
        else CHECK(r.p().health < start);
    }
}

// ================================================================= the fight

TEST_CASE("monolithe: in its arena it fights, never camps a corner nor a pit edge, never falls, uses its new attacks") {
    std::set<AttackType> used;
    for (int still = 0; still < 2; ++still)
        for (int phase = 0; phase < 8; ++phase) {
            // A still player stands somewhere new each phase: middle, crust near a wall (before
            // the 5th phase), at the edge of a pit (after), under a ledge.
            float tile = 48.0f;
            if (still) {
                const float spots[8] = {48.0f, 10.0f, 86.0f, 30.0f, 18.0f, 78.0f, 52.0f, 20.0f};
                tile = spots[phase];
            }
            Heart h(phase, tile);
            REQUIRE(h.boss != nullptr);
            const float lo = h.zone.x + 16.0f * 16.0f, hi = h.zone.x + 80.0f * 16.0f;  // the firm floor between the pits
            float dir = 1.0f;
            float edge = 0.0f, worstEdge = 0.0f, corner = 0.0f, worstCorner = 0.0f;
            float minX = 1e9f, maxX = -1e9f;
            bool inPit = false;
            EnemyState last = EnemyState::Idle;
            for (int i = 0; i < 30 * 120; ++i) {
                PlayerInput in;
                if (!still) {
                    float x = h.w->player().feet().x;
                    float a = h.zone.x + (phase >= 4 ? 20.0f : 6.0f) * 16.0f, b = h.zone.x + (phase >= 4 ? 76.0f : 90.0f) * 16.0f;
                    if (x > b) dir = -1.0f;
                    if (x < a) dir = 1.0f;
                    in = run(dir);
                    if (i % 90 == 0) in = jump(in);
                }
                h.step(in);
                h.w->player().health = h.w->player().maxHealth();
                if (!h.w->player().alive()) break;
                Enemy* b = h.boss;
                if (!b->alive()) break;
                if (b->state() == EnemyState::Telegraph && last != EnemyState::Telegraph)
                    if (const AttackDef* a = chosen(*b)) used.insert(a->type);
                last = b->state();
                if (!b->skyborne()) {
                    // Never in a pit: its feet never below the floor.
                    if (b->box.bottom() > h.floorY() + 2.0f) inPit = true;
                    const bool nearEdge = b->box.x < lo + 40.0f || b->box.right() > hi - 40.0f;
                    const bool inCorner = b->box.x < h.zone.x + 64.0f || b->box.right() > h.zone.right() - 64.0f;
                    edge = nearEdge ? edge + kDt : 0.0f;
                    corner = inCorner ? corner + kDt : 0.0f;
                    worstEdge = std::max(worstEdge, edge);
                    worstCorner = std::max(worstCorner, corner);
                    minX = std::min(minX, b->center().x);
                    maxX = std::max(maxX, b->center().x);
                }
            }
            std::printf("  phase %d, joueur %s : bord %.1f s au plus, coin %.1f s, de x %.0f à %.0f%s\n", phase + 1,
                        still ? "immobile" : "mobile", worstEdge, worstCorner, minX - h.zone.x, maxX - h.zone.x, inPit ? ", DANS UNE FOSSE" : "");
            CHECK(!inPit);
            CHECK(worstEdge < 3.0f);
            CHECK(worstCorner < 2.0f);
            // A fighter: it follows a moving player over its floor (a still one, it faces from its
            // distance, ~170 px, never from the edge). 2.76.2: by its parts its body no longer throws
            // her back, she runs through it; in its phase of spells (« Or ») it follows her ~150 px.
            if (!still) CHECK(maxX - minX > 128.0f);
        }
    for (AttackType t : golemTypes()) {
        if (!used.count(t)) std::printf("  jamais choisie : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
}
