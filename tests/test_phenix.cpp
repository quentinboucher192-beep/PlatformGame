// Le Phénix Stellaire (2.56): the fourth giga boss of Les Confins and « Le Nid de Cendres ». It
// flies and glides between its blows (« vol plané »), its feathers are its shots, and it has two
// attack types of its own (game/enemies/PhoenixAttacks.cpp): « Colonnes solaires » (columns of
// sunlight fall from the roof onto marks, a march through Nova or pairs closing in from both
// walls: a step aside between two of them is enough) and « Lame d'ailes » (a blade of feathers
// across the arena: low, jump over it; high, stay down). At the end of its 5th phase, « Cendres »,
// it burns to ashes on the floor of its nest (it cannot be hurt), then is reborn with the health
// of its phase. Right under it, or beside it, is no shelter; it never stays in a corner.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/SuitRecolor.h"
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

const EnemyDefinition& phenix() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("phenix");
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
        LevelData l = LevelData::makeEmpty("test_phenix", "Test", "pyralis");
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
        Enemy* e = w->spawnEnemy("phenix", feet, true, "boss");
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

/// confins_04 (« Le Nid de Cendres ») in a world, the fight started at `phase`, the player at
/// `tile` (columns of the arena; 48: the middle, on the nest).
struct Nest {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Enemy* boss = nullptr;
    explicit Nest(int phase, float tile = 48.0f) {
        res.init(nullptr, &audio);
        level = content().loadLevel("confins_04");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("confins_04"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "phenix") bossObject = o.id;
        for (const LevelObject& o : level->objects)
            if (o.type == "combat_zone" && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        CHECK(!bossObject.empty() && zone.w > 0.0f);
        auto* sp = dynamic_cast<EnemySpawnEntity*>(w->entity(bossObject));
        boss = sp ? sp->enemy(*w) : nullptr;
        CHECK(boss != nullptr);
        if (!boss) return;
        w->startAt({zone.x + tile * 16.0f, floorY()});
        for (int i = 0; i < 10; ++i) step();
        w->startBoss(*boss);
        if (phase > 0) jumpBossToPhase(*w, *boss, phase);
    }
    void step(const PlayerInput& in = {}) { w->fixedUpdate(in, kDt); }
    float floorY() const { return zone.bottom() - 4.0f * 16.0f; }  // the beds of embers are 4 tiles deep
};

const AttackDef* chosen(const Enemy& e) {
    for (const AttackDef& a : e.def().attacks)
        if (a.name == e.currentAttackName()) return &a;
    return nullptr;
}

/// Attacks that come down on a player standing right under it, or sweep the floor she stands on.
bool comesDown(AttackType t) {
    return t == AttackType::Dive || t == AttackType::Sunbeam || t == AttackType::CloudShot || t == AttackType::Rain ||
           t == AttackType::Sweep;
}

std::set<AttackType> phoenixTypes() { return {AttackType::Sunbeam, AttackType::Sweep}; }
}  // namespace

// ===================================================================== data

TEST_CASE("phenix: two attack types of its own, named in French, saved only where used") {
    std::set<std::string> ids, labels;
    for (int i = 0; i < static_cast<int>(AttackType::Count); ++i) {
        AttackType t = static_cast<AttackType>(i);
        CHECK(ids.insert(attackTypeId(t)).second);
        CHECK(labels.insert(attackTypeLabel(t)).second);
    }
    for (AttackType t : phoenixTypes()) {
        AttackDef a;
        a.type = t;
        CHECK(a.isPhoenixType() && !a.isGolemType() && !a.isDragonType() && !a.isGuardianType());
        CHECK(enumFromId(std::string(attackTypeId(t)), &attackTypeId, AttackType::Melee) == t);
    }
    CHECK(std::string(attackTypeLabel(AttackType::Sunbeam)) == "Colonnes solaires");
    CHECK(std::string(attackTypeLabel(AttackType::Sweep)) == "Lame d'ailes");
    AttackDef a;
    a.type = AttackType::Sunbeam;
    a.count = 4;
    a.spread = 110.0f;
    a.stagger = 0.24f;
    a.burstInterval = 0.7f;
    a.radius = 22.0f;
    a.duration = 0.35f;
    a.arms = 2;
    AttackDef b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Sunbeam && b.count == 4 && b.arms == 2);
    CHECK_NEAR(b.spread, 110.0, 1e-6);
    CHECK_NEAR(b.stagger, 0.24, 1e-6);
    CHECK_NEAR(b.burstInterval, 0.7, 1e-6);
    CHECK_NEAR(b.radius, 22.0, 1e-6);
    CHECK_NEAR(b.duration, 0.35, 1e-6);
    a = AttackDef{};
    a.type = AttackType::Sweep;
    a.count = 2;
    a.projectile = "feather";
    a.projectileSpeed = 330.0f;
    a.height = 30.0f;
    a.projectileRadius = 6.0f;
    b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Sweep && b.count == 2 && b.projectile == "feather");
    CHECK_NEAR(b.projectileSpeed, 330.0, 1e-6);
    CHECK_NEAR(b.height, 30.0, 1e-6);
    // The rebirth and the ashes of a phase: written only when set (the phases of before keep
    // their fingerprints).
    BossPhase ph;
    CHECK(!ph.toJson().contains("rebirth"));
    CHECK(!ph.toJson().contains("ashes"));
    ph.rebirth = 0.5f;
    ph.ashes = true;
    BossPhase back = BossPhase::fromJson(ph.toJson());
    CHECK_NEAR(back.rebirth, 0.5, 1e-6);
    CHECK(back.ashes);
    for (const EnemyDefinition* d : content().enemies()) {
        if (d->id == "phenix") continue;
        for (const BossPhase& p : d->phases) {
            CHECK(p.rebirth == 0.0f);
            CHECK(!p.ashes);
        }
        for (const AttackDef& x : d->attacks) CHECK(!x.isPhoenixType());
    }
}

TEST_CASE("phenix: a giga boss of eight phases, eight lights, its feathers, its columns, its blades and its rebirth") {
    const EnemyDefinition& d = phenix();
    REQUIRE(d.phases.size() == 8);
    CHECK(d.isBoss && d.builtin);
    CHECK(d.name == "Phénix");
    CHECK(d.bossTitle == "LE PHÉNIX STELLAIRE");
    CHECK(d.flying);
    CHECK(!d.canTeleport);  // it flies and glides
    CHECK(d.scale > 1.2f);
    std::set<std::string> lights, sheets;
    std::set<AttackType> used;
    int feathers = 0;
    for (size_t i = 0; i < d.phases.size(); ++i) {
        const BossPhase& p = d.phases[i];
        CHECK_NEAR(p.hpThreshold, 1.0 - 0.125 * static_cast<double>(i), 1e-6);
        CHECK(!p.name.empty() && !p.enterText.empty());
        CHECK(p.lightColor.a > 0);
        lights.insert(p.lightColor.toHex());
        sheets.insert(p.animation);
        bool down = false;
        for (const auto& id : p.attacks) {
            const AttackDef* a = d.attack(id);
            REQUIRE(a != nullptr);
            used.insert(a->type);
            down = down || comesDown(a->type);
            if (a->projectile == "feather") ++feathers;
        }
        CHECK(down);  // something that comes down on a player right under it, in every phase
        CHECK((p.enrageTime > 0.0f) == (i == 7));  // the rage in its last phase only
        // « Cendres » (the 5th): its ashes veil the arena and it is reborn with the health of
        // its phase; no other phase.
        CHECK((p.rebirth > 0.0f) == (i == 4));
        CHECK(p.ashes == (i == 4));
    }
    CHECK(d.phases[4].name == "Cendres");
    CHECK(d.phases[5].name == "Renaissance");
    CHECK_NEAR(d.phases[4].rebirth, d.phases[4].hpThreshold, 1e-6);
    CHECK(lights.size() == 8);
    CHECK(sheets.size() == 8);
    for (const std::string& s : sheets) CHECK(fs::exists(Paths::assetsDir() / s));
    CHECK(feathers >= 10);
    // Its identity: its two types, the dive, its feathers in fans, rain, homing and spiral, the
    // wind of its wings, the breath of a star, rings, the crown of feathers, the nebula, its ash.
    for (AttackType t : {AttackType::Sunbeam, AttackType::Sweep, AttackType::Dive, AttackType::Spread, AttackType::Rain,
                         AttackType::Projectile, AttackType::Spiral, AttackType::Gust, AttackType::Laser, AttackType::Ring,
                         AttackType::Orbit, AttackType::Prism, AttackType::Well, AttackType::Vortex, AttackType::CloudShot,
                         AttackType::Summon}) {
        if (!used.count(t)) std::printf("  type absent : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    // Not the dragon's: it never lands nor floods its arena.
    CHECK(used.count(AttackType::Perch) == 0);
    CHECK(used.count(AttackType::Flood) == 0);
    for (const AttackDef& a : d.attacks) {
        if (a.type == AttackType::CloudShot) CHECK(a.look == "ash");
        if (a.type == AttackType::Summon) {
            const EnemyDefinition* c = content().enemy(a.summonId);
            REQUIRE(c != nullptr);
            CHECK(!c->isBoss);
            CHECK(c->name == "Étincelle du Phénix");
        }
    }
    // The first phase already marches its columns, throws its blade and dives.
    std::set<AttackType> first;
    for (const auto& id : d.phases[0].attacks) first.insert(d.attack(id)->type);
    for (AttackType t : {AttackType::Sunbeam, AttackType::Sweep, AttackType::Dive}) CHECK(first.count(t) == 1);
    // Comets three by three and blades two by two in the last phase, columns from both walls.
    int dives = 0, blades = 0, arms = 0;
    for (const auto& id : d.phases[7].attacks) {
        const AttackDef* a = d.attack(id);
        if (a->type == AttackType::Dive) dives = std::max(dives, a->count);
        if (a->type == AttackType::Sweep) blades = std::max(blades, a->count);
        if (a->type == AttackType::Sunbeam) arms = std::max(arms, a->arms);
    }
    CHECK(dives == 3);
    CHECK(blades == 2);
    CHECK(arms == 2);
    // Its last dawn: never chosen in a phase.
    REQUIRE(!d.finalAttacks.empty());
    CHECK(!d.finalText.empty());
    for (const auto& id : d.finalAttacks) {
        REQUIRE(d.attack(id) != nullptr);
        for (const BossPhase& p : d.phases) CHECK(std::find(p.attacks.begin(), p.attacks.end(), id) == p.attacks.end());
    }
    CHECK(bossDifficulty(d).stars == 10);
}

TEST_CASE("phenix: its sheets are anchored at the tips of its plumes (nothing drawn under its feet)") {
    // The pivot is the bottom of the art (2.56, as the Kraken's in 2.53): low over the floor it
    // never goes through it nor under the HUD.
    for (int k = 1; k <= 8; ++k) {
        const std::string id = k == 1 ? std::string("phenix") : "phenix_p" + std::to_string(k);
        auto loaded = FileUtil::loadJson(Paths::assetsDir() / "animations" / (id + ".json"));
        REQUIRE(loaded.has_value());
        const Json& j = *loaded;
        const int fw = j.value("frameWidth", 0), fh = j.value("frameHeight", 0);
        const int py = j["pivot"][1].get<int>();
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        REQUIRE(loadRgbaImage(Paths::assetsDir() / j.value("texture", std::string()), px, w, h));
        REQUIRE(fw > 0 && fh > 0 && w % fw == 0 && h % fh == 0);
        int lowest = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (px[(static_cast<size_t>(y) * w + x) * 4 + 3] > 40) lowest = std::max(lowest, y % fh);
        if (lowest > py) std::printf("  %s : art jusqu'à y %d, pivot %d\n", id.c_str(), lowest, py);
        CHECK(lowest <= py);
        CHECK(py - lowest <= 6);  // and not floating far above it
    }
    // Its box reaches its pivot: 64x96 sheet pixels, x1.25.
    CHECK_NEAR(phenix().size.x, 80.0, 1e-6);
    CHECK_NEAR(phenix().size.y, 120.0, 1e-6);
}

TEST_CASE("phenix: « Le Nid de Cendres », the last level of Les Confins, the fight alone") {
    const PlanetData* confins = content().planet("confins");
    REQUIRE(confins != nullptr);
    REQUIRE(!confins->levels.empty());
    CHECK(confins->levels.back() == "confins_04");
    auto level = content().loadLevel("confins_04");
    REQUIRE(level.has_value());
    CHECK(level->name == "Le Nid de Cendres");
    CHECK(level->extreme);
    CHECK(level->bossMusic == "music/phenix_boss.ogg");
    int bosses = 0, checkpoints = 0, embers = 0, perches = 0;
    bool music = false;
    for (const LevelObject& o : level->objects) {
        if (o.type == "enemy" && o.propStr("enemy") == "phenix") ++bosses;
        if (o.type == "checkpoint") ++checkpoints;
        if (o.type == "damage_zone" && o.propStr("style") == "embers") ++embers;
        if (o.type == "crumble_platform" && o.propStr("style") == "ember") {
            ++perches;
            CHECK(o.prop<float>("respawn", 0.0f) > 0.0f);  // reborn from their ashes
        }
        if (o.type == "combat_zone" && o.propStr("music") == "music/phenix_boss.ogg") music = true;
    }
    CHECK(bosses == 1);
    CHECK(checkpoints == 0);
    CHECK(embers == 6);  // two beds of embers, two curtains then two more
    CHECK(perches == 6);  // the ledges at 10 and 15 tiles
    CHECK(music);
    CHECK(fs::exists(Paths::assetsDir() / "music" / "phenix_boss.ogg"));
}

TEST_CASE("phenix: its perches of embers burn under Nova, then are reborn from their ashes; it flies through them") {
    Nest m(0);
    REQUIRE(m.boss != nullptr);
    CrumblePlatformEntity* perch = nullptr;
    for (const LevelObject& o : m.level->objects)
        if (o.type == "crumble_platform" && o.propStr("style") == "ember" && !perch)
            perch = dynamic_cast<CrumblePlatformEntity*>(m.w->entity(o.id));
    REQUIRE(perch != nullptr);
    // Nova put on it: it gives way, then comes back.
    Player& p = m.w->player();
    p.box = Rect::fromBottomCenter({perch->box.center().x, perch->box.y}, p.box.size());
    p.prevBox = p.box;
    p.vel = {};
    float fell = -1.0f, back = -1.0f;
    for (int i = 0; i < 120 * 6; ++i) {
        m.step();
        p.health = p.maxHealth();
        const bool solid = m.w->physics().overlapsSolid(perch->box.expanded(-2.0f), MoveOptions{});
        if (fell < 0.0f && !solid) fell = static_cast<float>(i) * kDt;
        if (fell >= 0.0f && back < 0.0f && solid) back = static_cast<float>(i) * kDt;
    }
    std::printf("  perchoir : brûle après %.2f s, renaît %.2f s plus tard\n", fell, back - fell);
    CHECK(fell > 0.0f && fell < 1.5f);
    CHECK(back > fell);
}

// ================================================================= its columns

TEST_CASE("phenix: its columns of sunlight fall from the roof onto marks, one after the other, through Nova") {
    Room r;
    Enemy* e = r.spawn({1000.0f, kFloor - 220.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "colonnes"));
    const float px = r.p().center().x;
    std::vector<Vec2> marks;
    while (e->state() == EnemyState::Telegraph) {
        marks = e->attackPath();
        r.step();
    }
    REQUIRE(marks.size() == 5);
    // A march from its side through her: the middle one on her, the gap between them kept.
    CHECK(std::abs(marks[2].x - px) < 20.0f);
    for (size_t k = 1; k < marks.size(); ++k) CHECK(std::abs(std::abs(marks[k].x - marks[k - 1].x) - 92.0f) < 1.0f);
    CHECK(marks.front().x > marks.back().x);  // from the boss's side (it is on her right)
    const auto& cracks = r.w->hazards().cracks();
    REQUIRE(cracks.size() == 5);
    float lastDelay = 0.0f;
    for (const auto& c : cracks) {
        CHECK(c.sun);
        CHECK(std::abs(c.at.y - kFloor) < 1.0f);  // down to the floor
        CHECK(std::abs(c.fromY - 292.0f) < 1.0f);  // from the roof
        CHECK(c.delay >= lastDelay);  // one after the other
        lastDelay = c.delay;
    }
    // Standing still under it: hit.
    int start = r.p().health;
    for (int i = 0; i < 120 * 3; ++i) r.step();
    CHECK(r.p().health < start);
    CHECK(r.w->hazards().cracks().empty());
}

TEST_CASE("phenix: a step aside between two columns is enough") {
    for (const char* id : {"colonnes", "midi"}) {
        Room r;
        Enemy* e = r.spawn({1000.0f, kFloor - 220.0f});
        REQUIRE(e != nullptr);
        REQUIRE(e->forceAttack(*r.w, id));
        while (e->state() == EnemyState::Telegraph) r.step();
        e->setDormant();  // only its columns from now on
        const float aim = e->attackMark().x;
        const float gap = e->def().attack(id)->spread;
        int start = r.p().health;
        bool running = true;
        for (int i = 0; i < 120 * 4; ++i) {
            // Half a gap to the left of where the middle column falls.
            if (running && r.p().center().x <= aim - gap * 0.5f + 6.0f) running = false;
            r.step(running ? run(-1.0f) : PlayerInput{});
        }
        CHECK(r.w->hazards().cracks().empty());
        std::printf("  %s : Nova à %.0f px de la colonne du milieu, %d coup(s)\n", id, aim - r.p().center().x, start - r.p().health);
        CHECK(r.p().health == start);
    }
}

// ================================================================= its blades

TEST_CASE("phenix: its blade of feathers crosses the arena low (jump it) or high (stay down)") {
    bool seenLow = false, seenHigh = false;
    for (uint64_t seed = 1; seed <= 12 && !(seenLow && seenHigh); ++seed) {
        for (int jumpIt = 0; jumpIt < 2; ++jumpIt) {
            Room r(seed);
            Enemy* e = r.spawn({1000.0f, kFloor - 220.0f});
            REQUIRE(e != nullptr);
            REQUIRE(e->forceAttack(*r.w, "lame"));
            const bool low = e->laneLow();
            const Rect lane = e->lane();
            // The lane: along the floor she stands on, or just above her head.
            if (low) {
                CHECK(std::abs(lane.bottom() - kFloor) < 1.0f);
                CHECK(lane.h <= 32.0f);
            } else {
                CHECK(lane.bottom() < r.p().box.y - 8.0f);  // over her head, standing
                CHECK(lane.y > kFloor - 100.0f);            // a jump reaches it
            }
            int start = r.p().health;
            bool jumped = false;
            int hold = 0;
            while (e->state() == EnemyState::Telegraph) r.step();
            r.step();
            e->setDormant();  // only its blade from now on
            for (int i = 0; i < 120 * 5; ++i) {
                // The feather of the blade nearest to her: jump ~0.3 s before it reaches her.
                float eta = 1e9f;
                for (const Projectile& pr : r.w->projectiles())
                    if (pr.clip == "feather" && !pr.fromPlayer && std::abs(pr.vel.x) > 1.0f) {
                        const float t = (r.p().center().x - pr.pos.x) / pr.vel.x;
                        if (t > 0.0f) eta = std::min(eta, t);
                    }
                PlayerInput in;
                if (jumpIt && !jumped && eta < (low ? 0.3f : 0.12f)) {
                    jumped = true;
                    hold = 30;
                }
                if (hold > 0) {
                    --hold;
                    in = jump();
                    if (hold < 29) in.pressed[static_cast<int>(Action::Jump)] = false;
                }
                r.step(in);
            }
            const bool hit = r.p().health < start;
            std::printf("  graine %llu, lame %s, %s : %s\n", static_cast<unsigned long long>(seed), low ? "basse" : "haute",
                        jumpIt ? "saut" : "immobile", hit ? "touchée" : "esquivée");
            // Low: standing still is hit, the jump clears it. High: standing still is safe, a jump into it is hit.
            CHECK(hit == (low != (jumpIt == 1)));
            (low ? seenLow : seenHigh) = true;
        }
    }
    CHECK(seenLow);
    CHECK(seenHigh);
}

TEST_CASE("phenix: two blades, one low then one high, the second shown before it comes") {
    Room r(3);
    Enemy* e = r.spawn({1000.0f, kFloor - 220.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "lame_double"));
    std::set<bool> lanes;
    lanes.insert(e->laneLow());
    while (e->state() == EnemyState::Telegraph) r.step();
    r.step();
    lanes.insert(e->laneLow());  // the next lane, shown while the first blade flies
    int blades = 0;
    float lastX = -1e9f;
    for (int i = 0; i < 120 * 3; ++i) {
        r.step();
        for (const Projectile& pr : r.w->projectiles())
            if (pr.clip == "feather" && pr.age < kDt * 1.5f && std::abs(pr.pos.x - lastX) > 0.5f) {
                ++blades;
                lastX = pr.pos.x;
            }
        r.p().health = r.p().maxHealth();
    }
    CHECK(lanes.size() == 2);
    CHECK(blades >= 1);
}

// ================================================================= its rebirth

TEST_CASE("phenix: at the end of « Cendres » it burns to ashes on its nest, cannot be hurt, then is reborn with its phase's health") {
    Nest m(4);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    CHECK(b.phaseIndex() == 4);
    CHECK(b.ashVeil());  // its ashes veil the arena in this phase
    // Its health right above the end of the phase, then a hit.
    b.setWounds(b.maxHp() * 0.376f, 4);
    DamageInfo d;
    d.amount = 3.0f;
    d.type = DamageType::ChargeShot;
    bool hurt = false;
    for (int i = 0; i < 360 && !hurt; ++i) {  // once it can be hurt (its appearance over)
        m.step();
        d.source = b.center() + Vec2{-40.0f, 0.0f};
        hurt = b.takeDamage(*m.w, d);
    }
    REQUIRE(hurt);
    m.step();
    REQUIRE(b.inAshes());
    CHECK(b.phaseIndex() == 4);
    bool lay = false, refused = true, onFloor = false;
    float lowest = 0.0f, ashTime = 0.0f;
    int steps = 0;
    while (b.inAshes() && steps < 120 * 10) {
        m.step();
        ++steps;
        m.w->player().health = m.w->player().maxHealth();
        if (b.ashesLying()) {
            lay = true;
            ashTime += kDt;
            onFloor = onFloor || std::abs(b.box.bottom() - m.floorY()) < 2.0f;
            if (b.takeDamage(*m.w, d)) refused = false;  // nothing hurts its ashes
        }
        lowest = std::max(lowest, b.box.bottom());
    }
    std::printf("  cendres : %.1f s au sol, renaissance après %.1f s, PV %.0f / %.0f\n", ashTime, steps * kDt, b.hp(), b.maxHp());
    CHECK(lay);
    CHECK(refused);
    CHECK(onFloor);  // its ashes lie on the floor of its nest (not in a bed of embers)
    CHECK(lowest <= m.floorY() + 1.0f);
    CHECK(!b.inAshes());
    // Reborn: the health of its phase given back, the next phase begun.
    CHECK(b.phaseIndex() == 5);
    CHECK_NEAR(b.hp(), b.maxHp() * 0.5f, 0.5f);
    CHECK(!b.ashVeil());
    // Phase 6 then lasts until 25 %: a boss training of it starts with the health it is reborn with.
    Nest t(5);
    REQUIRE(t.boss != nullptr);
    CHECK(t.boss->phaseIndex() == 5);
    CHECK_NEAR(t.boss->hp(), t.boss->maxHp() * 0.5f, 0.5f);
}

TEST_CASE("phenix: its rebirth burns around its ashes (a ring shown before), a step away is enough") {
    for (int away = 0; away < 2; ++away) {
        Nest m(4);
        REQUIRE(m.boss != nullptr);
        Enemy& b = *m.boss;
        b.setWounds(b.maxHp() * 0.376f, 4);
        DamageInfo d;
        d.amount = 3.0f;
        d.type = DamageType::ChargeShot;
        bool hurt = false;
        for (int i = 0; i < 360 && !hurt; ++i) {
            m.step();
            d.source = b.center();
            hurt = b.takeDamage(*m.w, d);
        }
        REQUIRE(hurt);
        m.step();
        REQUIRE(b.inAshes());
        // Nova next to its ashes, or 160 px away; no stray shot (only the rebirth counts).
        bool placed = false;
        int hits = 0;
        while (b.inAshes()) {
            if (b.ashesLying() && !placed) {
                placed = true;
                Player& p = m.w->player();
                p.box = Rect::fromBottomCenter({b.ashSpot().x + (away ? 160.0f : 40.0f), m.floorY()}, p.box.size());
                p.prevBox = p.box;
                p.vel = {};
            }
            m.w->projectiles().clear();
            const int before = m.w->player().health;
            m.step();
            if (b.rising() && m.w->player().health < before) ++hits;
            m.w->player().health = m.w->player().maxHealth();
        }
        CHECK(placed);
        CHECK(hits == (away ? 0 : 1));
    }
}

// ================================================================= the fight

TEST_CASE("phenix: right under it or beside it is no shelter, in every phase") {
    // The player stands still for 20 s, the Phénix right above her, then beside her.
    for (int phase = 0; phase < 8; ++phase) {
        for (bool beside : {false, true}) {
            Nest m(phase);
            REQUIRE(m.boss != nullptr);
            Vec2 pc = m.w->player().center();
            m.boss->box = Rect::fromCenter({pc.x + (beside ? 230.0f : 20.0f), pc.y - (beside ? 120.0f : 210.0f)}, m.boss->box.size());
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
            std::printf("  phase %d : %d coups en 20 s %s Phénix (%zu attaques différentes)\n", phase + 1, hits,
                        beside ? "à côté du" : "sous le", by.size());
            CHECK(hits >= (phase == 0 ? 5 : 4));
            if (phase == 0 && !beside) CHECK(dives > 0);
        }
    }
}

TEST_CASE("phenix: in its nest it fights, glides, never stays in a corner nor against a wall, uses its new attacks") {
    std::set<AttackType> used;
    std::set<std::string> names;
    for (int still = 0; still < 2; ++still)
        for (int phase = 0; phase < 8; ++phase) {
            // A still player stands somewhere new each phase: the middle, near a wall, on a
            // ledge's level, at the edge of a bed of embers.
            float tile = 48.0f;
            if (still) {
                const float spots[8] = {48.0f, 8.0f, 88.0f, 30.0f, 26.0f, 70.0f, 52.0f, 12.0f};
                tile = spots[phase];
            }
            Nest m(phase, tile);
            REQUIRE(m.boss != nullptr);
            float dir = 1.0f;
            float wall = 0.0f, worstWall = 0.0f, roof = 0.0f, worstRoof = 0.0f;
            float minX = 1e9f, maxX = -1e9f;
            bool inside = true;
            EnemyState last = EnemyState::Idle;
            for (int i = 0; i < 30 * 120; ++i) {
                PlayerInput in;
                if (!still) {
                    float x = m.w->player().feet().x;
                    float lo = m.zone.x + (phase >= 4 ? 26.0f : 8.0f) * 16.0f, hi = m.zone.x + (phase >= 4 ? 70.0f : 88.0f) * 16.0f;
                    if (x > hi) dir = -1.0f;
                    if (x < lo) dir = 1.0f;
                    in = run(dir);
                    if (i % 90 == 0) in = jump(in);
                }
                m.step(in);
                m.w->player().health = m.w->player().maxHealth();
                if (!m.w->player().alive()) break;
                Enemy* b = m.boss;
                if (!b->alive()) break;
                if (b->state() == EnemyState::Telegraph && last != EnemyState::Telegraph)
                    if (const AttackDef* a = chosen(*b)) {
                        used.insert(a->type);
                        names.insert(a->name);
                    }
                last = b->state();
                inside = inside && m.zone.expanded(8.0f).containsRect(b->box);
                const bool atWall = b->box.x < m.zone.x + 48.0f || b->box.right() > m.zone.right() - 48.0f;
                const bool atRoof = b->box.y < m.zone.y + 24.0f;
                wall = atWall ? wall + kDt : 0.0f;
                roof = atRoof ? roof + kDt : 0.0f;
                worstWall = std::max(worstWall, wall);
                worstRoof = std::max(worstRoof, roof);
                minX = std::min(minX, b->center().x);
                maxX = std::max(maxX, b->center().x);
            }
            std::printf("  phase %d, joueur %s : contre un mur %.1f s au plus, sous la voûte %.1f s, de x %.0f à %.0f\n", phase + 1,
                        still ? "immobile" : "mobile", worstWall, worstRoof, minX - m.zone.x, maxX - m.zone.x);
            CHECK(inside);
            CHECK(worstWall < 2.0f);
            CHECK(worstRoof < 2.0f);
            // It glides: from one side of her to the other.
            CHECK(maxX - minX > 300.0f);
        }
    for (AttackType t : {AttackType::Sunbeam, AttackType::Sweep, AttackType::Dive, AttackType::CloudShot, AttackType::Gust,
                         AttackType::Rain, AttackType::Vortex, AttackType::Well}) {
        if (!used.count(t)) std::printf("  jamais choisie : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    std::printf("  %zu attaques différentes choisies sur les huit phases\n", names.size());
    CHECK(names.size() >= 18);
}
