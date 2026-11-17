// Le Kraken des Abysses (2.53): the third giga boss of Les Confins and « La Fosse des Abysses ».
// It swims (flying movement) and fights with the attack types of before: its lashes dive through
// Nova (the dragon's « Piqué »), it comes down to spread ink along the floor, lobs clouds of
// ink, stirs currents and whirlpools, floods the arena with dark water, and its tentacles burst
// out of the floor on both sides of her. Right under it, or beside it, is no shelter.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Paths.h"
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

const EnemyDefinition& kraken() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("kraken");
    CHECK(d != nullptr);
    return d ? *d : none;
}

/// A closed test room (floor y 800, ceiling at 260), the player standing at x 700.
struct Room {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit Room(uint64_t seed = 7) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_kraken", "Test", "pyralis");
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
        w->seedGameplay(seed);
        w->load(l);
        w->silent = true;
        w->player().spawn(*w, {700, kFloor}, false, false);
        for (int i = 0; i < 30; ++i) step();
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    Player& p() { return w->player(); }
    Enemy* spawn(Vec2 feet) {
        Enemy* e = w->spawnEnemy("kraken", feet, true, "boss");
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

/// confins_03 (« La Fosse des Abysses ») in a world, the fight started at `phase`, the player in
/// the middle of the arena (the floor that stays when the crust gives way over the trenches).
struct Trench {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Enemy* boss = nullptr;
    explicit Trench(int phase) {
        res.init(nullptr, &audio);
        level = content().loadLevel("confins_03");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("confins_03"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "kraken") bossObject = o.id;
        for (const LevelObject& o : level->objects)
            if (o.type == "combat_zone" && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty() && zone.w > 0.0f);
        auto* sp = dynamic_cast<EnemySpawnEntity*>(w->entity(bossObject));
        boss = sp ? sp->enemy(*w) : nullptr;
        CHECK(boss != nullptr);
        if (!boss) return;
        w->startAt({zone.x + 48.0f * 16.0f, zone.bottom() - 4.0f * 16.0f});
        for (int i = 0; i < 10; ++i) step();
        w->startBoss(*boss);
        if (phase > 0) jumpBossToPhase(*w, *boss, phase);
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
};

/// Attacks that come down on a player standing right under it, or rise under her feet.
bool comesDown(AttackType t) {
    return t == AttackType::Dive || t == AttackType::Perch || t == AttackType::CloudShot || t == AttackType::Rain ||
           t == AttackType::Cage || t == AttackType::Eruption || t == AttackType::Flood;
}
}  // namespace

// ===================================================================== data

TEST_CASE("kraken: a giga boss of eight phases, eight lights, its tentacles and its tides") {
    const EnemyDefinition& d = kraken();
    REQUIRE(d.phases.size() == 8);
    CHECK(d.isBoss && d.builtin);
    CHECK(d.name == "Kraken");
    CHECK(d.bossTitle == "LE KRAKEN DES ABYSSES");
    CHECK(d.flying);
    CHECK(!d.canTeleport);  // it swims
    CHECK(d.scale > 1.2f);
    std::set<std::string> lights, sheets;
    std::set<AttackType> used;
    for (size_t i = 0; i < d.phases.size(); ++i) {
        const BossPhase& p = d.phases[i];
        CHECK_NEAR(p.hpThreshold, 1.0 - 0.125 * static_cast<double>(i), 1e-6);
        CHECK(!p.name.empty() && !p.enterText.empty());
        CHECK(p.lightColor.a > 0);
        lights.insert(p.lightColor.toHex());
        sheets.insert(p.animation);
        // Something that comes down on a player right under it, in every phase.
        bool down = false;
        for (const auto& id : p.attacks) {
            const AttackDef* a = d.attack(id);
            REQUIRE(a != nullptr);
            used.insert(a->type);
            down = down || comesDown(a->type);
        }
        CHECK(down);
        // The rage comes in its last phase only (Singularis' rule).
        CHECK((p.enrageTime > 0.0f) == (i == 7));
    }
    CHECK(lights.size() == 8);
    CHECK(sheets.size() == 8);
    for (const std::string& s : sheets) CHECK(fs::exists(Paths::assetsDir() / s));
    // Its identity: the lash that dives, ink on the ground and in clouds, currents, whirlpools,
    // tides, tentacles out of the floor, sonar rings, the jet of its beak.
    for (AttackType t : {AttackType::Dive, AttackType::Perch, AttackType::CloudShot, AttackType::Gust, AttackType::Vortex,
                         AttackType::Well, AttackType::Flood, AttackType::Cage, AttackType::Ring, AttackType::Eruption,
                         AttackType::Laser, AttackType::Summon}) {
        if (!used.count(t)) std::printf("  type absent : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    for (const AttackDef& a : d.attacks) {
        if (a.type == AttackType::Cage) CHECK(a.look == "tentacle");
        if (a.type == AttackType::Flood) CHECK(a.look == "tide");
        if (a.type == AttackType::CloudShot || a.type == AttackType::Perch) CHECK(a.look == "ink");
        if (a.type == AttackType::Summon) {
            const EnemyDefinition* c = content().enemy(a.summonId);
            REQUIRE(c != nullptr);
            CHECK(!c->isBoss);
            CHECK(c->name == "Photophore abyssal");
        }
    }
    // The first phase already lashes down and raises its tentacles.
    std::set<AttackType> first;
    for (const auto& id : d.phases[0].attacks) first.insert(d.attack(id)->type);
    CHECK(first.count(AttackType::Dive) == 1);
    CHECK(first.count(AttackType::Cage) == 1);
    // Lashes three by three in the last phase, ink on both sides of its landing.
    int most = 0;
    for (const auto& id : d.phases[7].attacks) {
        const AttackDef* a = d.attack(id);
        if (a->type == AttackType::Dive) most = std::max(most, a->count);
        if (a->type == AttackType::Perch) CHECK(a->arms == 2);
    }
    CHECK(most == 3);
    // Its last wave: never chosen in a phase.
    REQUIRE(!d.finalAttacks.empty());
    CHECK(!d.finalText.empty());
    for (const auto& id : d.finalAttacks) {
        REQUIRE(d.attack(id) != nullptr);
        for (const BossPhase& p : d.phases) CHECK(std::find(p.attacks.begin(), p.attacks.end(), id) == p.attacks.end());
    }
    CHECK(bossDifficulty(d).stars == 10);
}

TEST_CASE("kraken: « La Fosse des Abysses », the last level of Les Confins, the fight alone") {
    const PlanetData* confins = content().planet("confins");
    REQUIRE(confins != nullptr);
    REQUIRE(confins->levels.size() >= 3);
    CHECK(confins->levels[2] == "confins_03");  // 2.56: « Le Nid de Cendres » after it
    auto level = content().loadLevel("confins_03");
    REQUIRE(level.has_value());
    CHECK(level->name == "La Fosse des Abysses");
    CHECK(level->extreme);
    CHECK(level->bossMusic == "music/kraken_boss.ogg");
    int bosses = 0, checkpoints = 0, abyss = 0;
    bool music = false;
    for (const LevelObject& o : level->objects) {
        if (o.type == "enemy" && o.propStr("enemy") == "kraken") ++bosses;
        if (o.type == "checkpoint") ++checkpoints;
        if (o.type == "damage_zone" && o.propStr("style") == "abyss") ++abyss;
        if (o.type == "combat_zone" && o.propStr("music") == "music/kraken_boss.ogg") music = true;
    }
    CHECK(bosses == 1);
    CHECK(checkpoints == 0);
    CHECK(abyss == 6);  // two trenches of dark water, two curtains then two more
    CHECK(music);
    CHECK(fs::exists(Paths::assetsDir() / "music" / "kraken_boss.ogg"));
}

// ===================================================================== tentacles, tides

TEST_CASE("kraken: its tentacles burst out of the floor on both sides of Nova, ink drips between them") {
    Room r;
    Enemy* e = r.spawn({1000.0f, kFloor - 220.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "etau"));
    const float px = r.p().center().x;
    while (e->state() == EnemyState::Telegraph) r.step();
    const auto& walls = r.w->hazards().walls();
    REQUIRE(walls.size() == 2);
    for (const auto& wl : walls) {
        CHECK(wl.look == "tentacle");
        CHECK(std::abs(wl.rect.bottom() - kFloor) < 1.0f);  // out of the floor, under the swimming Kraken
    }
    CHECK(std::min(walls[0].rect.center().x, walls[1].rect.center().x) < px);
    CHECK(std::max(walls[0].rect.center().x, walls[1].rect.center().x) > px);
    // Standing still between them: the ink falling from above hits her.
    int start = r.p().health;
    for (int i = 0; i < 120 * 4; ++i) r.step();
    CHECK(r.p().health < start);
    CHECK(r.w->hazards().walls().empty());
}

TEST_CASE("kraken: its tide of dark water floods the floor of its arena, holds, then goes down") {
    // Its second phase, in its arena: the tide rises from the bottom of the trenches.
    Trench m(1);
    REQUIRE(m.boss != nullptr);
    const float floorY = m.zone.bottom() - 4.0f * 16.0f;  // the floor of the arena (the trenches are 4 tiles deep)
    REQUIRE(m.boss->forceAttack(*m.w, "maree"));
    float top = 1e9f;
    bool tide = false;
    int start = m.w->player().health;
    for (int i = 0; i < 120 * 8; ++i) {
        m.step();
        for (const auto& f : m.w->hazards().floods()) {
            top = std::min(top, f.surfaceY());
            tide = tide || f.look == "tide";
        }
    }
    CHECK(tide);
    // The low tide: over the floor (Nova standing there is hurt), the lowest ledges (5 tiles up)
    // stay dry.
    CHECK(top < floorY - 50.0f);
    CHECK(top > floorY - 5.0f * 16.0f + 4.0f);
    CHECK(m.w->player().health < start);
    CHECK(m.w->hazards().floods().empty());
}

// ================================================================= the fight

TEST_CASE("kraken: right under it or beside it is no shelter, in every phase") {
    // The player stands still for 20 s, the Kraken right above her, then beside her.
    for (int phase = 0; phase < 8; ++phase) {
        for (bool beside : {false, true}) {
            Trench m(phase);
            REQUIRE(m.boss != nullptr);
            Vec2 pc = m.w->player().center();
            m.boss->box = Rect::fromCenter({pc.x + (beside ? 230.0f : 20.0f), pc.y - (beside ? 120.0f : 210.0f)}, m.boss->box.size());
            m.boss->prevBox = m.boss->box;
            int hits = 0, lashes = 0;
            std::set<std::string> by;
            for (int i = 0; i < 20 * 120; ++i) {
                int before = m.w->player().health;
                m.step();
                if (m.w->player().health < before) {
                    ++hits;
                    by.insert(m.boss->currentAttackName());
                }
                if (m.boss->diving()) ++lashes;
                m.w->player().health = m.w->player().maxHealth();
            }
            std::printf("  phase %d : %d coups en 20 s %s Kraken (%zu attaques différentes)\n", phase + 1, hits,
                        beside ? "à côté du" : "sous le", by.size());
            CHECK(hits >= (phase == 0 ? 5 : 4));
            if (phase == 0 && !beside) CHECK(lashes > 0);
        }
    }
}

TEST_CASE("kraken: its attacks are chosen in the fight, it stays in its arena") {
    std::set<AttackType> used;
    std::set<std::string> names;
    for (int phase = 0; phase < 8; ++phase) {
        Trench m(phase);
        REQUIRE(m.boss != nullptr);
        float dir = 1.0f;
        EnemyState last = EnemyState::Idle;
        for (int i = 0; i < 25 * 120; ++i) {
            // A player who keeps moving and jumping on the floor that stays.
            float x = m.w->player().feet().x;
            float lo = m.zone.x + (phase >= 4 ? 40.0f : 12.0f) * 16.0f, hi = m.zone.x + (phase >= 4 ? 56.0f : 84.0f) * 16.0f;
            if (x > hi) dir = -1.0f;
            if (x < lo) dir = 1.0f;
            PlayerInput in = run(dir);
            if (i % 90 == 0) in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
            m.step(in);
            m.w->player().health = m.w->player().maxHealth();
            if (!m.w->player().alive()) break;
            if (m.boss->state() == EnemyState::Telegraph && last != EnemyState::Telegraph) {
                names.insert(m.boss->currentAttackName());
                for (const AttackDef& a : m.boss->def().attacks)
                    if (a.name == m.boss->currentAttackName()) used.insert(a.type);
            }
            last = m.boss->state();
            CHECK(m.zone.expanded(8.0f).containsRect(m.boss->box));
        }
    }
    for (AttackType t : {AttackType::Dive, AttackType::Perch, AttackType::CloudShot, AttackType::Gust, AttackType::Cage,
                         AttackType::Flood, AttackType::Vortex, AttackType::Well}) {
        if (!used.count(t)) std::printf("  jamais choisie : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    std::printf("  %zu attaques différentes choisies sur les huit phases\n", names.size());
    CHECK(names.size() >= 18);
}
