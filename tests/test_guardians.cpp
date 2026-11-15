// The guardians of Aethra (2.19): Mycélia, Aurorax, Pyrogyre and Nyxara with their twelve new
// attack types. Every new attack is announced before it can hurt, plays the same twice with
// the same seed, and can be beaten the way the game says (outrun the root, run against the
// pull, stand in the ring's opening, wait on a ledge above the lava...). Their planet, Aethra,
// has four whole levels, each one with its guardian and the tablet of a new power at the start.

#include <cmath>
#include <functional>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/progression/Progression.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;
constexpr float kFloor = 800.0f;

const char* const kGuardians[] = {"mycelia", "aurorax", "pyrogyre", "nyxara"};

ContentDatabase& sharedContent() {
    static ContentDatabase* c = nullptr;
    if (!c) {
        c = new ContentDatabase();
        c->reload();
    }
    return *c;
}

/// A closed test room: the usual floor (y 800), a ceiling and walls (the prism bounces),
/// optional ledges.
struct Room {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    explicit Room(uint64_t seed = 7, std::vector<Rect> ledges = {}) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_guardians", "Test", "aethra");
        l.intro.steps.clear();
        auto add = [&](const std::string& id, const std::string& type, Rect r) {
            LevelObject o;
            o.id = id;
            o.type = type;
            o.layer = "terrain";
            o.rect = r;
            o.props = {{"material", "ruin_stone"}};
            l.objects.push_back(o);
        };
        add("ceiling", "solid", {0, 340, 2400, 32});
        for (size_t i = 0; i < ledges.size(); ++i) add("ledge" + std::to_string(i), "oneway", ledges[i]);
        WorldServices s{res, audio, sharedContent(), nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->seedGameplay(seed);
        w->load(l);
        w->silent = true;
        for (int i = 0; i < 30; ++i) step();
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    Player& p() { return w->player(); }
    void placePlayer(float x, float feetY = kFloor) {
        w->startAt({x, feetY});
        for (int i = 0; i < 20; ++i) step();
    }
    /// A copy of a guardian keeping only `attackId` (no other attack chosen meanwhile, no
    /// contact damage: only the attack under test can hurt).
    Enemy* spawn(const std::string& bossId, const std::string& attackId, Vec2 feet, bool faceLeft = true) {
        EnemyDefinition d = *sharedContent().enemy(bossId);
        std::vector<AttackDef> keep;
        for (const auto& a : d.attacks)
            if (a.id == attackId) {
                keep.push_back(a);
                keep.back().cooldown = 60.0f;
            }
        d.attacks = keep;
        d.contactDamage = 0.0f;
        d.canTeleport = false;
        for (auto& ph : d.phases) ph.attacks = {attackId};
        Enemy* e = w->spawnEnemy(d, feet, faceLeft);
        step();
        return e;
    }
};

PlayerInput run(float dir) {
    PlayerInput in;
    in.move = {dir, 0.0f};
    in.held[static_cast<int>(dir < 0 ? Action::MoveLeft : Action::MoveRight)] = true;
    return in;
}

/// Steps until the enemy is done with its attack (or `maxSeconds`); returns the damage taken.
int playAttack(Room& r, Enemy* e, float maxSeconds, const std::function<PlayerInput()>& input = {},
               bool* hurtDuringTelegraph = nullptr) {
    int start = r.p().health;
    bool attacked = false;
    for (int i = 0; i < static_cast<int>(maxSeconds * 120.0f); ++i) {
        int before = r.p().health;
        bool tele = e->state() == EnemyState::Telegraph;
        r.step(input ? input() : PlayerInput{});
        if (tele && e->state() == EnemyState::Telegraph && r.p().health < before && hurtDuringTelegraph) *hurtDuringTelegraph = true;
        if (e->state() == EnemyState::Attack) attacked = true;
        if (attacked && e->state() != EnemyState::Attack && e->state() != EnemyState::Telegraph) break;
        if (!r.p().alive()) break;
    }
    return start - r.p().health;
}

struct Case {
    const char* boss;
    const char* attack;
    AttackType type;
};
const Case kCases[] = {
    {"mycelia", "tracker", AttackType::Tracker},     {"mycelia", "cage", AttackType::Cage},
    {"mycelia", "inhale", AttackType::Vortex},       {"aurorax", "mirror", AttackType::Mirror},
    {"aurorax", "frozen_lake", AttackType::FrostFloor}, {"aurorax", "prism", AttackType::Prism},
    {"aurorax", "prism_night", AttackType::Prism},   {"pyrogyre", "crown", AttackType::Orbit},
    {"pyrogyre", "supernova", AttackType::Orbit},    {"pyrogyre", "spiral", AttackType::Spiral},
    {"pyrogyre", "tide", AttackType::Flood},         {"pyrogyre", "high_tide", AttackType::Flood},
    {"nyxara", "well", AttackType::Well},            {"nyxara", "shadow_step", AttackType::Blink},
    {"nyxara", "eclipse", AttackType::Ring},
};

Vec2 bossFeet(const char* boss, float x) {
    const EnemyDefinition* d = sharedContent().enemy(boss);
    return {x, d && d->flying ? kFloor - 90.0f : kFloor};
}
}  // namespace

// ===================================================================== data

TEST_CASE("guardians: twelve new attack types, named in French, saved only where used") {
    std::set<std::string> ids, labels;
    for (int i = static_cast<int>(AttackType::Tracker); i < static_cast<int>(AttackType::Count); ++i) {
        AttackType t = static_cast<AttackType>(i);
        CHECK(ids.insert(attackTypeId(t)).second);
        CHECK(labels.insert(attackTypeLabel(t)).second);
        CHECK(enumFromId(std::string(attackTypeId(t)), &attackTypeId, AttackType::Melee) == t);
    }
    CHECK(ids.size() == 12);
    for (const char* id : {"tracker", "cage", "vortex", "mirror", "frost_floor", "prism", "orbit", "spiral", "flood", "well", "blink", "ring"})
        CHECK(ids.count(id) == 1);
    // Round trip of the new parameters.
    AttackDef a;
    a.type = AttackType::Prism;
    a.bounces = 3;
    a.laserLength = 900.0f;
    a.duration = 2.0f;
    a.pull = 70.0f;
    a.radius = 140.0f;
    a.height = 90.0f;
    a.arms = 3;
    a.grip = 0.2f;
    AttackDef b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Prism && b.bounces == 3 && b.arms == 3);
    CHECK_NEAR(b.duration, 2.0, 1e-6);
    CHECK_NEAR(b.pull, 70.0, 1e-6);
    CHECK_NEAR(b.radius, 140.0, 1e-6);
    CHECK_NEAR(b.height, 90.0, 1e-6);
    CHECK_NEAR(b.grip, 0.2, 1e-6);
    CHECK_NEAR(b.laserLength, 900.0, 1e-6);
    // A pull is never as fast as Nova runs (150 px/s): running away always wins.
    Json j = a.toJson();
    j["pull"] = 500;
    CHECK(AttackDef::fromJson(j).pull < 150.0f);
    // The attacks of before are written as before (their levels keep their fingerprints).
    for (const EnemyDefinition* d : sharedContent().enemies())
        for (const AttackDef& x : d->attacks) {
            if (x.isGuardianType()) continue;
            Json xj = x.toJson();
            for (const char* k : {"duration", "pull", "radius", "height", "bounces", "arms", "grip"}) CHECK(!xj.contains(k));
        }
}

TEST_CASE("guardians: the four guardians of Aethra and their echoes") {
    ContentDatabase& c = sharedContent();
    for (const char* id : kGuardians) {
        const EnemyDefinition* d = c.enemy(id);
        CHECK(d != nullptr);
        if (!d) continue;
        CHECK(d->isBoss && d->builtin);
        CHECK(d->hp >= 90.0f && d->hp <= 160.0f);
        CHECK(d->phases.size() == 3);
        CHECK_NEAR(d->phases[1].hpThreshold, 0.66, 1e-4);
        CHECK_NEAR(d->phases[2].hpThreshold, 0.33, 1e-4);
        CHECK(d->animation == std::string("animations/") + id + ".json");
        std::set<AttackType> own;
        for (const AttackDef& a : d->attacks)
            if (a.isGuardianType()) own.insert(a.type);
        CHECK(own.size() == 3);
        // Every attack of a phase exists, and the last phase keeps the new attacks.
        for (const BossPhase& ph : d->phases)
            for (const auto& aid : ph.attacks) CHECK(d->attack(aid) != nullptr);
        const EnemyDefinition* echo = c.enemy(std::string("echo_") + id);
        CHECK(echo != nullptr);
        if (echo) {
            CHECK(echo->variantOf == id);
            CHECK(echo->attacks.size() == d->attacks.size());
            for (const auto& drop : echo->drops) CHECK(drop.type != "health_orb");
        }
    }
    // Nyxara swallows the light in her last phase; the strongest of the four.
    CHECK(c.enemy("nyxara")->darkPhase == 3);
    for (const char* id : {"mycelia", "aurorax", "pyrogyre"}) CHECK(c.enemy("nyxara")->hp > c.enemy(id)->hp);
    // The planche's names.
    CHECK(c.enemy("mycelia")->name == "Mycélia");
    CHECK(c.enemy("aurorax")->bossTitle == "AURORAX, LE MIROIR BORÉAL");
    CHECK(c.enemy("pyrogyre")->attack("tide")->name == "Marée de lave");
    CHECK(c.enemy("nyxara")->attack("eclipse")->name == "Onde d'éclipse");
}

// ============================================================ every attack

TEST_CASE("guardians: every new attack is announced before it hurts, and plays the same twice") {
    for (const Case& k : kCases) {
        uint64_t hashes[2] = {0, 0};
        for (int run = 0; run < 2; ++run) {
            Room r(levelSeed(std::string(k.boss) + k.attack));
            r.placePlayer(1100.0f);
            Enemy* e = r.spawn(k.boss, k.attack, bossFeet(k.boss, 1330.0f));
            CHECK(e != nullptr);
            if (!e) continue;
            CHECK(e->forceAttack(*r.w, k.attack));
            CHECK(e->state() == EnemyState::Telegraph);
            bool hurtEarly = false;
            int hp = r.p().health;
            // During the wind-up nothing hurts.
            while (e->state() == EnemyState::Telegraph) {
                r.step();
                if (e->state() == EnemyState::Telegraph) CHECK(r.p().health == hp);
            }
            CHECK(e->state() == EnemyState::Attack);
            playAttack(r, e, 12.0f, {}, &hurtEarly);
            CHECK(!hurtEarly);
            for (int i = 0; i < 240; ++i) r.step();
            hashes[run] = simulationStateHash(*r.w);
        }
        if (hashes[0] != hashes[1]) std::cout << "  " << k.boss << "/" << k.attack << " : deux parties différentes\n";
        CHECK(hashes[0] == hashes[1]);
    }
}

TEST_CASE("guardians: the hunting root can be outrun, standing still it bursts under you") {
    {
        Room r;
        r.placePlayer(1100.0f);
        Enemy* e = r.spawn("mycelia", "tracker", {1350.0f, kFloor});
        e->forceAttack(*r.w, "tracker");
        CHECK(playAttack(r, e, 6.0f) > 0);
    }
    {
        Room r;
        r.placePlayer(1100.0f);
        Enemy* e = r.spawn("mycelia", "tracker", {1350.0f, kFloor});
        e->forceAttack(*r.w, "tracker");
        CHECK(playAttack(r, e, 6.0f, [] { return run(-1.0f); }) == 0);
    }
}

TEST_CASE("guardians: the cage of thorns closes on both sides, then opens again") {
    Room r;
    r.placePlayer(1000.0f);
    Enemy* e = r.spawn("mycelia", "cage", {1350.0f, kFloor});
    e->forceAttack(*r.w, "cage");
    const float px = r.p().center().x;
    while (e->state() == EnemyState::Telegraph) r.step();
    // Two solid walls on both sides of Nova, the gap of the planche (about 260 px).
    const auto& walls = r.w->hazards().walls();
    CHECK(walls.size() == 2);
    if (walls.size() != 2) return;
    float gap = walls[1].rect.center().x - walls[0].rect.center().x;
    const Vec2 leftWall = walls[0].rect.center();
    CHECK(gap > 230.0f && gap < 290.0f);
    CHECK(walls[0].rect.center().x < px && walls[1].rect.center().x > px);
    MoveOptions opt;
    CHECK(r.w->physics().overlapsSolid(Rect::fromCenter(walls[0].rect.center(), {2, 2}), opt));
    // Running into a wall: it stops her (and pricks); spores fall in between.
    bool spores = false;
    for (int i = 0; i < 120 * 3; ++i) {
        r.step(run(-1.0f));
        CHECK(r.p().box.x >= walls[0].rect.right() - 0.5f);
        spores = spores || !r.w->projectiles().empty();
    }
    CHECK(spores);
    for (int i = 0; i < 120 * 2; ++i) r.step();
    CHECK(r.w->hazards().walls().empty());
    CHECK(!r.w->physics().overlapsSolid(Rect::fromCenter(leftWall, {2, 2}), opt));
}

TEST_CASE("guardians: the breath and the well pull Nova, running away always wins") {
    for (const char* which : {"inhale", "well"}) {
        const char* boss = std::string(which) == "inhale" ? "mycelia" : "nyxara";
        // Standing still: pulled toward the guardian (or its well).
        {
            Room r;
            r.placePlayer(1000.0f);
            Enemy* e = r.spawn(boss, which, bossFeet(boss, 1300.0f));
            e->forceAttack(*r.w, which);
            while (e->state() == EnemyState::Telegraph) r.step();
            float x0 = r.p().center().x;
            for (int i = 0; i < 120; ++i) r.step();
            CHECK(r.p().center().x > x0 + 20.0f);
        }
        // Running away: farther every second.
        {
            Room r;
            r.placePlayer(1000.0f);
            Enemy* e = r.spawn(boss, which, bossFeet(boss, 1300.0f));
            e->forceAttack(*r.w, which);
            while (e->state() == EnemyState::Telegraph) r.step();
            float x0 = r.p().center().x;
            int hp = r.p().health;
            for (int i = 0; i < 120; ++i) r.step(run(-1.0f));
            CHECK(r.p().center().x < x0 - 40.0f);
            CHECK(r.p().health == hp);
        }
    }
    // The well bends Nova's shots toward it.
    Room r;
    r.placePlayer(900.0f);
    Enemy* e = r.spawn("nyxara", "well", bossFeet("nyxara", 1300.0f));
    e->forceAttack(*r.w, "well");
    while (e->state() == EnemyState::Telegraph) r.step();
    CHECK(!r.w->hazards().pulls().empty());
    Vec2 hole = r.w->hazards().pulls().front().center;
    Projectile shot;
    shot.pos = shot.prevPos = hole + Vec2{-200.0f, -70.0f};
    shot.vel = {260.0f, 0.0f};
    shot.fromPlayer = true;
    shot.life = 2.0f;
    r.w->spawnProjectile(shot);
    float vy = 0.0f;
    for (int i = 0; i < 40; ++i) {
        r.step();
        for (const auto& p : r.w->projectiles())
            if (p.fromPlayer) vy = p.vel.y;
    }
    CHECK(vy > 20.0f);  // curving down toward the hole
}

TEST_CASE("guardians: the frozen lake takes the grip away, then gives it back") {
    Room r;
    r.placePlayer(1000.0f);
    const Collider* ground = r.w->physics().groundBelow(r.p().box, r.p().moveOptions());
    CHECK(ground != nullptr);
    uint32_t gid = ground->id;
    CHECK_NEAR(r.w->physics().get(gid)->friction, 1.0, 1e-6);
    Enemy* e = r.spawn("aurorax", "frozen_lake", {1300.0f, kFloor});
    e->forceAttack(*r.w, "frozen_lake");
    while (e->state() == EnemyState::Telegraph) r.step();
    r.step();
    CHECK(r.w->physics().get(gid)->friction < 0.2f);
    // Running then letting go: she slides far (on dry ground she stops within a few pixels).
    for (int i = 0; i < 120; ++i) r.step(run(-1.0f));
    float x0 = r.p().center().x;
    for (int i = 0; i < 120; ++i) r.step();
    float slide = x0 - r.p().center().x;
    CHECK(slide > 25.0f);
    for (int i = 0; i < 120 * 5; ++i) r.step();
    CHECK_NEAR(r.w->physics().get(gid)->friction, 1.0, 1e-6);
    CHECK(r.w->hazards().ice().empty());
}

TEST_CASE("guardians: the lava tide rises to the ledges and goes back down") {
    for (int onLedge = 0; onLedge < 2; ++onLedge) {
        // A ledge 5 tiles up, above the tide (4 tiles).
        Room r(7, {{900.0f, kFloor - 80.0f, 140.0f, 16.0f}});
        if (onLedge) {
            r.w->startAt({970.0f, kFloor - 80.0f});
            for (int i = 0; i < 30; ++i) r.step();
            CHECK(r.p().box.bottom() < kFloor - 70.0f);
        } else {
            r.placePlayer(1150.0f);
        }
        Enemy* e = r.spawn("pyrogyre", "tide", bossFeet("pyrogyre", 1500.0f));
        e->forceAttack(*r.w, "tide");
        int hp = r.p().health;
        float highest = 0.0f;
        for (int i = 0; i < 120 * 9; ++i) {
            r.step();
            for (const auto& f : r.w->hazards().floods()) highest = std::max(highest, f.level());
        }
        CHECK_NEAR(highest, 64.0, 0.5);
        CHECK(r.w->hazards().floods().empty());
        if (onLedge) CHECK(r.p().health == hp);
        else CHECK(r.p().health < hp);
    }
}

TEST_CASE("guardians: the prism traces its bounces before it fires") {
    for (const char* id : {"prism", "prism_night"}) {
        Room r;
        r.placePlayer(1000.0f);
        Enemy* e = r.spawn("aurorax", id, {1300.0f, kFloor});
        e->forceAttack(*r.w, id);
        const int bounces = sharedContent().enemy("aurorax")->attack(id)->bounces;
        const auto& path = e->attackPath();
        CHECK(static_cast<int>(path.size()) == bounces + 2);  // the dotted path is known from the start
        for (const Vec2& p : path) CHECK(r.w->level().bounds.expanded(64).contains(p));
        // Its first segment goes up diagonally.
        if (path.size() >= 2) CHECK(path[1].y < path[0].y - 20.0f && std::abs(path[1].x - path[0].x) > 20.0f);
    }
}

TEST_CASE("guardians: the crown, the spiral and the supernova") {
    {
        Room r;
        r.placePlayer(800.0f);
        Enemy* e = r.spawn("pyrogyre", "spiral", bossFeet("pyrogyre", 1300.0f));
        e->forceAttack(*r.w, "spiral");
        while (e->state() == EnemyState::Telegraph) r.step();
        r.step();
        // Two arms: the first shots leave in opposite directions.
        std::vector<Vec2> v;
        for (const auto& p : r.w->projectiles()) v.push_back(p.vel.normalized());
        CHECK(v.size() == 2);
        if (v.size() == 2) CHECK(v[0].x * v[1].x + v[0].y * v[1].y < -0.95f);
        int shots = 0;
        while (e->state() == EnemyState::Attack) {
            r.step();
            shots = std::max(shots, static_cast<int>(r.w->projectiles().size()));
        }
        CHECK(shots >= 12);
    }
    {
        // Standing inside the crown's sweep: the flames come round (most of the time: the gaps
        // between them are the way through).
        int hits = 0;
        for (float dx : {-110.0f, -90.0f, -70.0f, 70.0f, 90.0f, 110.0f}) {
            Room r;
            r.placePlayer(1300.0f + dx);
            Enemy* e = r.spawn("pyrogyre", "crown", {1300.0f, kFloor - 40.0f});
            e->forceAttack(*r.w, "crown");
            hits += playAttack(r, e, 8.0f) > 0 ? 1 : 0;
            CHECK(r.w->projectiles().empty());  // a crown, not shots
        }
        CHECK(hits >= 3);
    }
    {
        Room r;
        r.placePlayer(800.0f);
        Enemy* e = r.spawn("pyrogyre", "supernova", bossFeet("pyrogyre", 1300.0f));
        e->forceAttack(*r.w, "supernova");
        bool shots = false;
        for (int i = 0; i < 120 * 5; ++i) {
            r.step();
            shots = shots || !r.w->projectiles().empty();
        }
        CHECK(shots);  // the spiral fires through the crown
    }
}

TEST_CASE("guardians: the shadow step lands behind Nova") {
    Room r;
    r.placePlayer(1000.0f);
    for (int i = 0; i < 10; ++i) r.step(run(1.0f));  // facing right
    for (int i = 0; i < 30; ++i) r.step();
    CHECK(r.p().facing > 0.0f);
    Enemy* e = r.spawn("nyxara", "shadow_step", bossFeet("nyxara", 1400.0f));
    e->forceAttack(*r.w, "shadow_step");
    Vec2 mark = e->attackMark();
    CHECK(mark.x < r.p().center().x - 30.0f);  // behind her back
    CHECK_NEAR(mark.y, kFloor, 1.0);
    while (e->state() == EnemyState::Telegraph) r.step();
    CHECK(std::abs(e->center().x - mark.x) < 4.0f);
    // Standing there: hit. (Moving away as soon as the mark appears avoids it: next room.)
    CHECK(playAttack(r, e, 2.0f) > 0);
    Room r2;
    r2.placePlayer(1000.0f);
    for (int i = 0; i < 10; ++i) r2.step(run(1.0f));
    for (int i = 0; i < 30; ++i) r2.step();
    Enemy* e2 = r2.spawn("nyxara", "shadow_step", bossFeet("nyxara", 1400.0f));
    e2->forceAttack(*r2.w, "shadow_step");
    CHECK(playAttack(r2, e2, 2.0f, [] { return run(1.0f); }) == 0);
}

TEST_CASE("guardians: the eclipse ring spares whoever stands in its opening") {
    for (int inGap = 0; inGap < 2; ++inGap) {
        Room r(11);
        r.placePlayer(1000.0f);
        // Nyxara's centre 111 px above Nova's feet line (her centre 100 px above Nova's).
        Enemy* e = r.spawn("nyxara", "eclipse", {1300.0f, kFloor - 111.0f + 26.0f});
        e->forceAttack(*r.w, "eclipse");
        float gap = e->ringGapAngle();
        CHECK(gap > degToRad(20.0f) && gap < degToRad(160.0f));  // a reachable opening, below
        Vec2 c = e->center();
        float dy = kFloor - 11.0f - c.y;
        float x;
        if (inGap) {
            x = c.x + dy / std::tan(gap);
        } else {
            x = gap < kPi * 0.5f ? c.x - 400.0f : c.x + 400.0f;  // far from the opening
        }
        r.w->startAt({x, kFloor});
        CHECK(e->state() == EnemyState::Telegraph);
        int taken = playAttack(r, e, 6.0f);
        if (inGap) CHECK(taken == 0);
        else CHECK(taken > 0);
    }
}

TEST_CASE("guardians: Aurorax's reflections copy it, break in one hit and never lose it health") {
    Room r;
    r.placePlayer(700.0f);
    EnemyDefinition d = *sharedContent().enemy("aurorax");
    d.contactDamage = 0.0f;
    for (auto& a : d.attacks) a.cooldown = 60.0f;
    for (auto& ph : d.phases) ph.attacks = {"mirror"};
    Enemy* boss = r.w->spawnEnemy(d, {1300.0f, kFloor}, true);
    r.step();
    boss->forceAttack(*r.w, "mirror");
    while (boss->state() == EnemyState::Telegraph) r.step();
    r.step();
    std::vector<Enemy*> doubles;
    for (auto& e : r.w->enemies())
        if (e->mirrorOf() == boss->uid()) doubles.push_back(e.get());
    CHECK(doubles.size() == 2);
    CHECK(boss->doubles() == 2);
    for (Enemy* x : doubles) {
        CHECK(!x->def().isBoss);
        CHECK(x->maxHp() <= 1.0f);
        for (const AttackDef& a : x->def().attacks) CHECK(a.type != AttackType::Mirror && a.type != AttackType::FrostFloor);
    }
    // The true one strikes: its doubles strike with it.
    while (boss->state() == EnemyState::Attack || boss->state() == EnemyState::Recover) r.step();
    boss->forceAttack(*r.w, "prism");
    int copying = 0;
    for (Enemy* x : doubles) copying += x->state() == EnemyState::Telegraph ? 1 : 0;
    CHECK(copying == 2);
    // A hit breaks a double; the guardian keeps its health.
    float hp = boss->hp();
    DamageInfo hit;
    hit.amount = 1.0f;
    hit.type = DamageType::Melee;
    hit.source = doubles[0]->center();
    hit.fromPlayer = true;
    CHECK(doubles[0]->takeDamage(*r.w, hit));
    CHECK(!doubles[0]->alive());
    CHECK(boss->hp() == hp);
    for (int i = 0; i < 30; ++i) r.step();
    CHECK(boss->doubles() == 1);
    // The guardian falls: its last double goes with it.
    boss->kill(*r.w);
    for (int i = 0; i < 240; ++i) r.step();
    for (auto& e : r.w->enemies()) CHECK(!e->alive());
}

TEST_CASE("guardians: their effects end with them or when Nova respawns") {
    Room r;
    r.placePlayer(1000.0f);
    Enemy* e = r.spawn("aurorax", "frozen_lake", {1300.0f, kFloor});
    e->forceAttack(*r.w, "frozen_lake");
    while (e->state() == EnemyState::Telegraph) r.step();
    r.step();
    CHECK(!r.w->hazards().ice().empty());
    e->kill(*r.w);
    CHECK(r.w->hazards().empty());
    for (const Collider& c : r.w->physics().colliders()) CHECK(c.friction == 1.0f);
    Room r2;
    r2.placePlayer(1000.0f);
    Enemy* m = r2.spawn("mycelia", "cage", {1300.0f, kFloor});
    m->forceAttack(*r2.w, "cage");
    while (m->state() == EnemyState::Telegraph) r2.step();
    CHECK(r2.w->hazards().walls().size() == 2);
    r2.w->respawnPlayer();
    CHECK(r2.w->hazards().empty());
}

// ==================================================================== Aethra

TEST_CASE("aethra: a fifth planet of four levels, opened by the three planets") {
    ContentDatabase& c = sharedContent();
    const PlanetData* a = c.planet("aethra");
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->name == "Aethra");
    CHECK((a->levels == std::vector<std::string>{"aethra_01", "aethra_02", "aethra_03", "aethra_04"}));
    CHECK(a->unlock == "planets:sylvanox,glacior,pyralis");
    Progression p;
    CHECK(!p.planetUnlocked(*a, c));
    for (const char* planet : {"sylvanox", "glacior", "pyralis"}) {
        CHECK(!p.planetUnlocked(*a, c));
        for (const auto& id : c.planet(planet)->levels) p.markCompleted(id);
    }
    CHECK(p.planetUnlocked(*a, c));
    // The rest of the campaign is untouched: four levels per planet, three in the star (2.33).
    for (const char* planet : {"sylvanox", "glacior", "pyralis"}) CHECK(c.planet(planet)->levels.size() == 4);
    CHECK(c.planet("noyau")->levels.size() == 3);
    CHECK(c.planet("noyau")->unlock == "planets:sylvanox,glacior,pyralis");
}

TEST_CASE("aethra: each level has its guardian, the new power at the start, secrets and checkpoints") {
    ContentDatabase& c = sharedContent();
    struct Expect {
        const char* level;
        const char* name;
        const char* boss;
        const char* power;
    };
    const Expect expect[] = {{"aethra_01", "Le Bosquet Englouti", "mycelia", "boomerang"},
                             {"aethra_02", "Le Lac aux Reflets", "aurorax", "frost_nova"},
                             {"aethra_03", "La Caldeira", "pyrogyre", "decoy"},
                             {"aethra_04", "L'Horizon des événements", "nyxara", "recall"}};
    for (const Expect& x : expect) {
        auto l = c.loadLevel(x.level);
        CHECK(l.has_value());
        if (!l) continue;
        CHECK(l->name == x.name);
        CHECK(l->planet == "aethra");
        CHECK(l->bossMusic == std::string("music/") + x.boss + "_boss.ogg");
        CHECK(l->maxPowers == 4);
        CHECK(static_cast<int>(l->startPowers.size()) <= 3);  // a slot for the new power
        Vec2 start;
        float firstCheckpoint = 1e9f, tablet = -1.0f;
        int checkpoints = 0, bosses = 0, relics = 0, secrets = 0, shards = 0;
        for (const auto& o : l->objects) {
            if (o.type == "player_start") start = o.rect.bottomCenter();
            if (o.type == "checkpoint") {
                ++checkpoints;
                firstCheckpoint = std::min(firstCheckpoint, o.rect.x);
            }
            if (o.type == "enemy" && o.prop<bool>("boss", false)) {
                ++bosses;
                CHECK(o.propStr("enemy") == x.boss);
            }
            if (o.type == "power_unlock" && o.propStr("power") == x.power && o.id.rfind("station_", 0) != 0) tablet = o.rect.x;
            relics += o.type == "relic" ? 1 : 0;
            secrets += o.type == "secret_area" ? 1 : 0;
            shards += o.type == "shard" ? 1 : 0;
        }
        CHECK(bosses == 1);
        CHECK(checkpoints >= 3);
        CHECK(relics >= 1 && secrets >= 1);
        CHECK(shards >= 10);
        // The new power waits at the start, before the first checkpoint.
        CHECK(tablet >= 0.0f && tablet < firstCheckpoint && std::abs(tablet - start.x) < 20 * 16);
        CHECK(l->medals[3].maxTime > 0.0f);
    }
}
