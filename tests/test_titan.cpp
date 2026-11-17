// Le Titan Mécanique (2.58): the sixth giga boss of Les Confins and « La Fonderie Engloutie ». A war
// machine that walks (the golem's steps) and fights with the machinery of its foundry: three
// attack types of its own (game/enemies/TitanAttacks.cpp): « Presses hydrauliques » (the presses of
// its foundry come down onto the floor, shown before: every other one, all but one, or a march),
// « Missiles à verrouillage » (reticles follow Nova, lock, a missile falls on each) and « Tapis
// roulants » (its conveyor belts carry her, slower than she runs). Its heat: each attack fills a
// gauge; full, it vents its steam (jets along the ground, then its core bare: more damage). At the
// end of « Laser » it stalls: its valves appear; all turned before it restarts, its boiler bursts,
// otherwise it restarts with a pressure wave.

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
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "tests/TestFramework.h"

using namespace cosmic;

// A check that ends the test case when it fails (what follows needs it). The condition is
// evaluated once (it may act: an attack forced, a blow dealt), again only to report a failure.
#define REQUIRE(cond)                                    \
    do {                                                 \
        const bool required_ = static_cast<bool>(cond); \
        if (!required_) {                                \
            CHECK(cond);                                 \
            return;                                      \
        }                                                \
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

const EnemyDefinition& titan() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("titan");
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
        LevelData l = LevelData::makeEmpty("test_titan", "Test", "pyralis");
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
        Enemy* e = w->spawnEnemy("titan", feet, true, "boss");
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

/// confins_06 (« La Fonderie Engloutie ») in a world, the fight started at `phase`, the player at
/// `tile` (columns of the arena; 48: the middle).
struct Foundry {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Enemy* boss = nullptr;
    explicit Foundry(int phase, float tile = 30.0f) {
        res.init(nullptr, &audio);
        level = content().loadLevel("confins_06");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("confins_06"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "titan") bossObject = o.id;
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
    float floorY() const { return zone.bottom() - 4.0f * 16.0f; }  // the vats are 4 tiles deep
    /// Steps until the boss is between two blows (its rest, or walking), `tries` steps at most.
    bool idle(int tries = 1200) {
        for (int i = 0; i < tries; ++i) {
            if (boss->state() == EnemyState::Chase && !boss->venting() && !boss->stalled()) return true;
            step();
            w->player().health = w->player().maxHealth();
        }
        return false;
    }
    /// Hurts the boss once it can be, `tries` steps at most.
    bool hurtBoss(float amount, int tries = 600) {
        DamageInfo d;
        d.amount = amount;
        d.type = DamageType::ChargeShot;
        for (int i = 0; i < tries; ++i) {
            step();
            w->player().health = w->player().maxHealth();
            d.source = boss->center() + Vec2{-40.0f, 0.0f};
            if (boss->takeDamage(*w, d)) return true;
        }
        return false;
    }
};

const AttackDef* chosen(const Enemy& e) {
    for (const AttackDef& a : e.def().attacks)
        if (a.name == e.currentAttackName()) return &a;
    return nullptr;
}

std::set<AttackType> titanTypes() { return {AttackType::Press, AttackType::Missile, AttackType::Belt}; }
}  // namespace

// ================================================================= its data

TEST_CASE("titan: three attack types of its own, named in French, saved only where used") {
    CHECK(std::string(attackTypeLabel(AttackType::Press)) == "Presses hydrauliques");
    CHECK(std::string(attackTypeLabel(AttackType::Missile)) == "Missiles à verrouillage");
    CHECK(std::string(attackTypeLabel(AttackType::Belt)) == "Tapis roulants");
    for (AttackType t : titanTypes()) {
        AttackDef a;
        a.type = t;
        CHECK(a.isTitanType());
        CHECK(!a.isLicheType() && !a.isPhoenixType() && !a.isGolemType() && !a.isDragonType() && !a.isGuardianType());
        CHECK(enumFromId(attackTypeId(t), &attackTypeId, AttackType::Melee) == t);
        a.count = 3;
        a.arms = 2;
        a.duration = 0.6f;
        a.radius = 40.0f;
        a.pull = 90.0f;
        const AttackDef back = AttackDef::fromJson(a.toJson());
        CHECK(back.type == t);
        CHECK(back.count == 3 && back.arms == 2);
        CHECK_NEAR(back.duration, 0.6, 1e-6);
        CHECK_NEAR(back.radius, 40.0, 1e-6);
        CHECK_NEAR(back.pull, 90.0, 1e-6);
    }
    // The attacks of before are written as before (their levels keep their fingerprints).
    for (const EnemyDefinition* d : content().enemies())
        for (const AttackDef& x : d->attacks) {
            if (x.isTitanType()) continue;
            if (x.isGuardianType() || x.isDragonType() || x.isGolemType() || x.isPhoenixType() || x.isLicheType()) continue;
            Json xj = x.toJson();
            for (const char* k : {"duration", "pull", "radius", "height", "arms"}) CHECK(!xj.contains(k));
        }
    // Its heat and its stall are written only where set: the phases of before keep theirs.
    for (const EnemyDefinition* d : content().enemies()) {
        if (d->id == "titan") continue;
        for (const BossPhase& p : d->phases) {
            Json pj = p.toJson();
            CHECK(!pj.contains("heat"));
            CHECK(!pj.contains("stall"));
        }
    }
    BossPhase p;
    p.heat = 0.25f;
    p.stall = "vanne";
    const BossPhase q = BossPhase::fromJson(p.toJson());
    CHECK_NEAR(q.heat, 0.25, 1e-6);
    CHECK(q.stall == "vanne");
}

TEST_CASE("titan: a giga boss of eight phases, eight lights, its heat, its machinery and its stall") {
    const EnemyDefinition& d = titan();
    REQUIRE(d.phases.size() == 8);
    CHECK(d.isBoss && d.builtin);
    CHECK(d.name == "Titan Mécanique");
    CHECK(d.bossTitle == "LE TITAN MÉCANIQUE");
    CHECK(!d.flying);  // it walks
    CHECK(!d.canTeleport);
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
        CHECK(p.heat >= 0.25f && p.heat <= 0.34f);  // it vents after three or four attacks, in every phase
        bool machinery = false;
        for (const auto& id : p.attacks) {
            const AttackDef* a = d.attack(id);
            REQUIRE(a != nullptr);
            used.insert(a->type);
            machinery = machinery || a->isTitanType();
        }
        CHECK(machinery);  // its foundry works in every phase
        CHECK((p.enrageTime > 0.0f) == (i == 7));
        CHECK(!p.stall.empty() == (i == 3));  // it stalls at the end of « Laser » only
        CHECK(p.rebirth == 0.0f && !p.ashes && p.phylactery.empty());  // not the Phénix's nor the Liche's
    }
    CHECK(d.phases[3].name == "Laser");
    CHECK(d.phases[4].name == "Surchauffe");
    const EnemyDefinition* valve = content().enemy(d.phases[3].stall);
    REQUIRE(valve != nullptr);
    CHECK(!valve->isBoss);
    CHECK(valve->name == "Vanne de pression");
    CHECK(valve->attacks.empty());
    CHECK(valve->contactDamage == 0.0f);
    CHECK(lights.size() == 8);
    CHECK(sheets.size() == 8);
    for (const std::string& s : sheets) CHECK(fs::exists(Paths::assetsDir() / s));
    // Its identity: its machinery, its piston fist, its cannon (bolts, shells, laser), its steam,
    // its electric waves and arcs, its crown of plasma, its drones.
    for (AttackType t : {AttackType::Press, AttackType::Missile, AttackType::Belt, AttackType::Slam, AttackType::Spread,
                         AttackType::Arc, AttackType::Laser, AttackType::Eruption, AttackType::Ring, AttackType::Prism,
                         AttackType::Orbit, AttackType::Summon}) {
        if (!used.count(t)) std::printf("  type absent : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    // Not another giga boss's: it never dives, lands, rolls in a shell, quakes, leaps out of sight,
    // calls columns of sunlight, throws blades, traces runes, curses, pulls her in, floods or freezes.
    for (AttackType t : {AttackType::Dive, AttackType::Perch, AttackType::Shell, AttackType::Quake, AttackType::Skyfall,
                         AttackType::Sunbeam, AttackType::Sweep, AttackType::Glyph, AttackType::Hex, AttackType::Vortex,
                         AttackType::Well, AttackType::Flood, AttackType::FrostFloor, AttackType::Cage, AttackType::Gust,
                         AttackType::CloudShot})
        CHECK(used.count(t) == 0);
    std::set<int> pressPatterns;
    for (const AttackDef& a : d.attacks) {
        if (a.type == AttackType::Press) {
            pressPatterns.insert(a.arms <= 1 ? 1 : a.arms);
            CHECK(a.burstInterval >= 0.9f);  // every press shows where it stamps long enough
        }
        if (a.type == AttackType::Missile) {
            CHECK(a.burstInterval >= 0.8f);  // the first reticle follows her a while
            CHECK(a.duration >= 0.75f);      // then it locks: time to step out
        }
        if (a.type == AttackType::Belt) CHECK(a.pull > 0.0f && a.pull < 130.0f);  // always slower than she runs (150)
        if (a.type == AttackType::Summon) {
            const EnemyDefinition* c = content().enemy(a.summonId);
            REQUIRE(c != nullptr);
            CHECK(!c->isBoss);
            CHECK(c->name == "Drone du Titan");
        }
    }
    CHECK(pressPatterns == std::set<int>({1, 2, 3}));
    // Its last salvo: never chosen in a phase.
    REQUIRE(!d.finalAttacks.empty());
    CHECK(!d.finalText.empty());
    for (const auto& id : d.finalAttacks) {
        REQUIRE(d.attack(id) != nullptr);
        for (const BossPhase& p : d.phases) CHECK(std::find(p.attacks.begin(), p.attacks.end(), id) == p.attacks.end());
    }
}

TEST_CASE("titan: its sheets are anchored at its feet (only the dust and the cracks of its slam go into the floor)") {
    for (int k = 1; k <= 8; ++k) {
        const std::string id = k == 1 ? std::string("titan") : "titan_p" + std::to_string(k);
        auto loaded = FileUtil::loadJson(Paths::assetsDir() / "animations" / (id + ".json"));
        REQUIRE(loaded.has_value());
        const Json& j = *loaded;
        const int fw = j.value("frameWidth", 0), fh = j.value("frameHeight", 0);
        const int py = j["pivot"][1].get<int>();
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        REQUIRE(loadRgbaImage(Paths::assetsDir() / j.value("texture", std::string()), px, w, h));
        REQUIRE(fw > 0 && fh > 0 && w % fw == 0 && h % fh == 0);
        // Where its feet stand in the idle clip (the lowest row of the art), and the lowest of all.
        int lowest = 0, feet = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (px[(static_cast<size_t>(y) * w + x) * 4 + 3] > 40) {
                    lowest = std::max(lowest, y % fh);
                    if (y < fh && x < fw * 8) feet = std::max(feet, y);
                }
        if (lowest > py + 3) std::printf("  %s : art jusqu'à y %d, pivot %d\n", id.c_str(), lowest, py);
        CHECK(lowest <= py + 3);  // dust and glowing cracks, 3 rows in the floor at most
        CHECK(feet <= py && feet >= py - 2);  // standing on its pivot, not floating
    }
    // Its box: 76x148 sheet pixels, x1.25.
    CHECK_NEAR(titan().size.x, 95.0, 1e-6);
    CHECK_NEAR(titan().size.y, 185.0, 1e-6);
}

TEST_CASE("titan: « La Fonderie Engloutie », the last level of Les Confins, the fight alone in its foundry") {
    const PlanetData* confins = content().planet("confins");
    REQUIRE(confins != nullptr);
    REQUIRE(!confins->levels.empty());
    CHECK(confins->levels.back() == "confins_06");
    auto level = content().loadLevel("confins_06");
    REQUIRE(level.has_value());
    CHECK(level->name == "La Fonderie Engloutie");
    CHECK(level->extreme);
    CHECK(level->bossMusic == "music/titan_boss.ogg");
    int bosses = 0, checkpoints = 0, presses = 0, belts = 0, valves = 0;
    bool music = false;
    for (const LevelObject& o : level->objects) {
        if (o.type == "enemy" && o.propStr("enemy") == "titan") ++bosses;
        if (o.type == "checkpoint") ++checkpoints;
        if (o.type == "press") ++presses;
        if (o.type == "conveyor") ++belts;
        if (o.type == "valve_spot") ++valves;
        if (o.type == "combat_zone" && o.propStr("music") == "music/titan_boss.ogg") music = true;
    }
    CHECK(bosses == 1);
    CHECK(checkpoints == 0);
    CHECK(presses == 8);  // side by side over the floor between the vats
    CHECK(belts == 2);
    CHECK(valves == 3);
    CHECK(music);
    CHECK(fs::exists(Paths::assetsDir() / "music" / "titan_boss.ogg"));
    // Its machinery read from its arena.
    Foundry m(0);
    REQUIRE(m.boss != nullptr);
    for (int i = 0; i < 240; ++i) m.step();
    CHECK(m.boss->isTitan());
    CHECK(m.boss->pressSpots().size() == 8);
    CHECK(m.boss->beltStrips().size() == 2);
    CHECK(m.boss->valveSpots().size() == 3);
    for (const auto& s : m.boss->pressSpots()) CHECK(std::abs(s.floor - m.floorY()) < 1.0f);  // they stamp the floor
}

// ================================================================ its presses

TEST_CASE("titan: its presses show where they stamp, then crush; a column that has just risen is safe") {
    Foundry m(0, 26.0f);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    REQUIRE(m.idle());
    REQUIRE(b.forceAttack(*m.w, "presses"));
    // The wind-up: which presses (every other one, the one over her first; never one over it).
    REQUIRE(!b.pressRounds().empty());
    CHECK(b.pressRounds().size() == 2);
    const auto& spots = b.pressSpots();
    const float px = m.w->player().center().x;
    bool hers = false;
    for (int k : b.pressRounds()[0]) {
        REQUIRE(k >= 0);
        const auto& s = spots[static_cast<size_t>(k)];
        CHECK(!(s.x + s.half > b.box.x + 6.0f && s.x - s.half < b.box.right() - 6.0f));
        if (std::abs(s.x - px) <= s.half) hers = true;
    }
    CHECK(hers);
    for (size_t i = 1; i < b.pressRounds()[0].size(); ++i) CHECK(b.pressRounds()[0][i] - b.pressRounds()[0][i - 1] >= 2);
    // Standing still under hers: shown long enough before (stripes), then it crushes once.
    bool warned = false, hit = false;
    float before = -1.0f;
    for (int i = 0; i < 120 * 4 && !hit; ++i) {
        const int hp = m.w->player().health;
        m.step();
        for (const auto& p : m.w->hazards().presses())
            if (!p.safe && std::abs(p.x - px) <= p.half && !p.crushing() && p.age < p.warn) {
                warned = true;
                before = std::max(before, p.warn - p.age);
            }
        if (m.w->player().health < hp) hit = true;
    }
    CHECK(warned);
    CHECK(hit);
    CHECK(before >= 0.85f);
    // Every other one, twice: she steps into a column of the second round at once, then back into
    // hers once its press is up again (before the second round falls): no press reaches her.
    Foundry n(0, 26.0f);
    REQUIRE(n.boss != nullptr);
    REQUIRE(n.idle());
    REQUIRE(n.boss->forceAttack(*n.w, "presses"));
    REQUIRE(n.boss->pressRounds().size() == 2);
    const auto& sp = n.boss->pressSpots();
    const float x0 = n.w->player().center().x;
    int mine = -1, next = -1;
    for (int k : n.boss->pressRounds()[0])
        if (std::abs(sp[static_cast<size_t>(k)].x - x0) <= sp[static_cast<size_t>(k)].half) mine = k;
    for (int k : n.boss->pressRounds()[1])
        if (std::abs(k - mine) == 1 && (next < 0 || std::abs(sp[static_cast<size_t>(k)].x - x0) < std::abs(sp[static_cast<size_t>(next)].x - x0))) next = k;
    REQUIRE(mine >= 0);
    REQUIRE(next >= 0);
    bool back = false, crushedHer = false;
    for (int i = 0; i < 120 * 6; ++i) {
        bool mineUp = false;
        for (const auto& p : n.w->hazards().presses()) {
            if (!p.safe && std::abs(p.x - sp[static_cast<size_t>(mine)].x) < 1.0f && p.age >= p.warn + BossHazards::Press::kFall + p.down) mineUp = true;
            if (p.hit) crushedHer = true;
        }
        back = back || mineUp;
        const float target = sp[static_cast<size_t>(back ? mine : next)].x;
        const float x = n.w->player().center().x;
        PlayerInput in = std::abs(target - x) > 10.0f ? run(target > x ? 1.0f : -1.0f) : PlayerInput{};
        n.step(in);
        n.w->player().health = n.w->player().maxHealth();
        if (back && n.w->hazards().pressCount(n.boss->uid()) == 0) break;
    }
    std::printf("  presses : prévenue %.2f s avant ; une colonne puis l'autre, écrasée : %s\n", before, crushedHer ? "oui" : "non");
    CHECK(back);
    CHECK(!crushedHer);
}

TEST_CASE("titan: all its presses but one: the column spared is lit, close to her; a march, one after the other") {
    Foundry m(3, 30.0f);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    REQUIRE(m.idle());
    REQUIRE(b.forceAttack(*m.w, "sauf_une"));
    REQUIRE(b.pressRounds().size() == 1);
    int spared = -1, falling = 0;
    for (int code : b.pressRounds()[0]) {
        if (code < 0) spared = -1 - code;
        else ++falling;
    }
    REQUIRE(spared >= 0);
    const auto& spots = b.pressSpots();
    int her = 0;
    for (int k = 1; k < static_cast<int>(spots.size()); ++k)
        if (std::abs(spots[static_cast<size_t>(k)].x - m.w->player().center().x) < std::abs(spots[static_cast<size_t>(her)].x - m.w->player().center().x)) her = k;
    CHECK(spared != her);
    CHECK(std::abs(spared - her) <= 2);
    CHECK(falling >= 5);
    // She runs to the green column (shown with the presses' warning) and is never hit.
    bool green = false, crushed = false;
    for (int i = 0; i < 120 * 5; ++i) {
        for (const auto& p : m.w->hazards().presses()) {
            if (p.safe) green = true;
            if (p.hit) crushed = true;
        }
        const float target = spots[static_cast<size_t>(spared)].x;
        const float x = m.w->player().center().x;
        PlayerInput in = std::abs(target - x) > 8.0f ? run(target > x ? 1.0f : -1.0f) : PlayerInput{};
        m.step(in);
        m.w->player().health = m.w->player().maxHealth();
    }
    CHECK(green);
    CHECK(!crushed);
    // A march: one press after the other from its side across the foundry.
    Foundry n(1, 30.0f);
    REQUIRE(n.boss != nullptr);
    REQUIRE(n.idle());
    REQUIRE(n.boss->forceAttack(*n.w, "marche"));
    REQUIRE(n.boss->pressRounds().size() == 1);
    const auto& order = n.boss->pressRounds()[0];
    REQUIRE(order.size() >= 5);
    for (int i = 0; i < 240 && n.w->hazards().pressCount(n.boss->uid()) == 0; ++i) n.step();
    std::vector<float> warns;
    for (const auto& p : n.w->hazards().presses()) warns.push_back(p.warn);
    std::sort(warns.begin(), warns.end());
    for (size_t i = 1; i < warns.size(); ++i) CHECK(warns[i] - warns[i - 1] > 0.5f);  // never two at once
    const float bx = n.boss->center().x;
    for (size_t i = 1; i < order.size(); ++i)
        CHECK(std::abs(n.boss->pressSpots()[static_cast<size_t>(order[i])].x - bx) >= std::abs(n.boss->pressSpots()[static_cast<size_t>(order[i - 1])].x - bx));
}

// ================================================================ its missiles

TEST_CASE("titan: its missiles: the reticles follow Nova, lock, then fall; running is enough") {
    for (bool running : {false, true}) {
        Room r;
        Enemy* b = r.spawn({1500, kFloor});
        REQUIRE(b != nullptr);
        for (int i = 0; i < 60; ++i) r.step();
        REQUIRE(b->forceAttack(*r.w, "missiles"));
        int hits = 0;
        bool locked = false, followed = false;
        float dir = -1.0f;
        for (int i = 0; i < 120 * 5; ++i) {
            PlayerInput in;
            if (running) {
                const float x = r.p().feet().x;
                if (x < 200.0f) dir = 1.0f;
                if (x > 1200.0f) dir = -1.0f;
                in = run(dir);
            }
            r.step(in);
            for (const auto& m : r.w->hazards().missiles()) {
                if (m.locked()) locked = true;
                if (!m.locked() && m.age > 0.3f && distance(m.at, r.p().center()) < 30.0f) followed = true;
                if (m.hit && m.landed() && m.age - m.track - m.flight < kDt * 1.5f) ++hits;
            }
            r.p().health = r.p().maxHealth();
            if (b->state() == EnemyState::Chase && r.w->hazards().missileCount(b->uid()) == 0 && i > 240) break;
        }
        std::printf("  missiles, joueur %s : touché %d fois\n", running ? "en course" : "immobile", hits);
        CHECK(locked);
        CHECK(followed);
        if (running) CHECK(hits == 0);
        else CHECK(hits >= 1);
    }
}

// ================================================================ its belts

TEST_CASE("titan: its belts carry Nova toward it, slower than she runs; a jump frees her") {
    // In its foundry: two belts, each carrying her toward it.
    Foundry m(1, 24.0f);
    REQUIRE(m.boss != nullptr);
    REQUIRE(m.idle());
    REQUIRE(m.boss->forceAttack(*m.w, "tapis"));
    for (int i = 0; i < 120 * 2 && !m.w->hazards().beltsRunning(m.boss->uid()); ++i) m.step();
    REQUIRE(m.w->hazards().beltsRunning(m.boss->uid()));
    CHECK(m.w->hazards().belts().size() == 2);
    for (const auto& bl : m.w->hazards().belts()) {
        const float toward = m.boss->center().x - bl.strip.center().x;
        if (std::abs(toward) > bl.strip.w * 0.5f) CHECK(bl.speed * toward > 0.0f);
        CHECK(std::abs(bl.speed) < 130.0f);
    }
    // In a room (its whole floor is its belt): standing still, carried toward it.
    Room r;
    Enemy* b = r.spawn({1250, kFloor});
    REQUIRE(b != nullptr);
    for (int i = 0; i < 30; ++i) r.step();
    REQUIRE(b->forceAttack(*r.w, "tapis"));
    for (int i = 0; i < 120 * 2 && !r.w->hazards().beltsRunning(b->uid()); ++i) r.step();
    REQUIRE(r.w->hazards().beltsRunning(b->uid()));
    for (int i = 0; i < 160; ++i) r.step();  // its warning, then it runs
    r.p().setInvulnerable(6.0f);
    const float x0 = r.p().center().x;
    for (int i = 0; i < 120; ++i) r.step();
    const float carried = r.p().center().x - x0;
    std::printf("  tapis : emportée de %.0f px en 1 s\n", carried);
    CHECK(carried > 40.0f);
    CHECK(carried < 130.0f);
    // Running against it, she still moves away.
    const float x1 = r.p().center().x;
    for (int i = 0; i < 60; ++i) r.step(run(-1.0f));
    CHECK(r.p().center().x < x1 - 20.0f);
    // In the air it does not carry her.
    for (int i = 0; i < 10; ++i) r.step();
    r.step(jump());
    float drift = 0.0f;
    for (int i = 0; i < 30; ++i) {
        PlayerInput hold;
        hold.held[static_cast<int>(Action::Jump)] = true;
        const float before = r.p().center().x;
        r.step(hold);
        if (!r.p().grounded()) drift += r.p().center().x - before;
    }
    CHECK(std::abs(drift) < 3.0f);
}

// ================================================================ its heat

TEST_CASE("titan: its heat: after a few attacks it vents (jets along the ground on both sides), its core bare") {
    Foundry m(0, 20.0f);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    // Four attacks fill its gauge in « Démarrage ».
    int attacks = 0;
    EnemyState last = b.state();
    bool vented = false;
    float heatSeen = 0.0f;
    for (int i = 0; i < 120 * 90 && !vented; ++i) {
        m.step();
        m.w->player().health = m.w->player().maxHealth();
        if (b.state() == EnemyState::Telegraph && last != EnemyState::Telegraph) ++attacks;
        last = b.state();
        heatSeen = std::max(heatSeen, b.heat());
        vented = b.venting();
    }
    std::printf("  surchauffe après %d attaques\n", attacks);
    REQUIRE(vented);
    CHECK(attacks >= 4);
    CHECK(b.vents() == 1);
    // Its jets along the ground, on both sides of it.
    int left = 0, right = 0;
    for (const auto& s : m.w->hazards().steams()) {
        if (s.dir < 0.0f && s.from.x <= b.center().x) ++left;
        if (s.dir > 0.0f && s.from.x >= b.center().x) ++right;
        CHECK(std::abs(s.from.y - b.box.bottom()) < 2.0f);
        CHECK(s.height <= 32.0f);  // low: a jump clears it
    }
    CHECK(left == 1);
    CHECK(right == 1);
    // Its core bare: it takes 1.8 times the damage; it does not attack while it vents.
    DamageInfo d;
    d.amount = 10.0f;
    d.type = DamageType::Projectile;
    d.source = b.center() + Vec2{-60.0f, 0.0f};
    const float hp = b.hp();
    for (int i = 0; i < 30 && !b.takeDamage(*m.w, d); ++i) m.step();
    CHECK_NEAR(hp - b.hp(), 18.0, 0.01);
    bool attacked = false;
    for (int i = 0; i < 120 * 3 && b.venting(); ++i) {
        m.step();
        m.w->player().health = m.w->player().maxHealth();
        if (b.state() == EnemyState::Telegraph) attacked = true;
    }
    CHECK(!attacked);
    for (int i = 0; i < 120 && b.venting(); ++i) m.step();
    CHECK(!b.venting());
    CHECK(b.heat() < 0.01f);
    // Out of its vents, the same blow does its own damage.
    const float hp2 = b.hp();
    for (int i = 0; i < 120 && !b.takeDamage(*m.w, d); ++i) m.step();
    CHECK_NEAR(hp2 - b.hp(), 10.0, 0.01);
    // A jet of its vents: standing in it at its blast hurts, a step beyond its end is enough.
    for (bool away : {false, true}) {
        Room r;
        Enemy* t = r.spawn({1800, kFloor});
        REQUIRE(t != nullptr);
        BossHazards::Steam s;
        s.owner = t->uid();
        s.from = {r.p().feet().x - (away ? 170.0f : 60.0f), kFloor};
        s.dir = 1.0f;
        s.length = 150.0f;
        r.w->hazards().addSteam(s);
        bool hit = false;
        for (int i = 0; i < 200; ++i) {
            r.step();
            for (const auto& j : r.w->hazards().steams())
                if (j.hit) hit = true;
        }
        CHECK(hit == !away);
    }
}

// ================================================================ its stall

TEST_CASE("titan: at the end of « Laser » it stalls: its valves appear; all turned in time, its boiler bursts") {
    Foundry m(3, 30.0f);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    CHECK(b.phaseIndex() == 3);
    b.setWounds(b.maxHp() * 0.502f, 3);
    REQUIRE(m.hurtBoss(3.0f));
    m.step();
    REQUIRE(b.stalled());
    CHECK(b.phaseIndex() == 3);
    CHECK(b.valvesTotal() == 3);
    CHECK(b.valvesLeft() == 3);
    CHECK(b.stallLeft() > 13.0f);
    for (size_t i = 0; i < b.valves().size(); ++i) {
        const Enemy* v = m.w->findEnemy(b.valves()[i]);
        REQUIRE(v != nullptr);
        CHECK(v->def().id == "vanne");
        bool onSpot = false;
        for (const Vec2& s : b.valveSpots()) onSpot = onSpot || (std::abs(v->box.bottom() - s.y) < 2.0f && std::abs(v->center().x - s.x) < 2.0f);
        CHECK(onSpot);
    }
    // Stalled, nothing hurts it and it does not attack.
    DamageInfo d;
    d.amount = 5.0f;
    d.type = DamageType::ChargeShot;
    d.source = b.center();
    const float hp = b.hp();
    bool attacked = false;
    for (int i = 0; i < 120 * 2; ++i) {
        m.step();
        m.w->player().health = m.w->player().maxHealth();
        CHECK(!b.takeDamage(*m.w, d));
        if (b.state() == EnemyState::Telegraph) attacked = true;
    }
    CHECK(b.hp() == hp);
    CHECK(!attacked);
    // Every valve turned (struck until it breaks): its boiler bursts, « Surchauffe » begins.
    DamageInfo hit;
    hit.amount = 2.0f;
    hit.type = DamageType::Melee;
    int blows = 0;
    for (uint32_t id : std::vector<uint32_t>(b.valves())) {
        Enemy* v = m.w->findEnemy(id);
        REQUIRE(v != nullptr);
        for (int i = 0; i < 600 && v->alive(); ++i) {
            hit.source = v->center() + Vec2{-20.0f, 0.0f};
            if (v->takeDamage(*m.w, hit)) ++blows;
            m.step();
            m.w->player().health = m.w->player().maxHealth();
        }
    }
    for (int i = 0; i < 4; ++i) m.step();
    std::printf("  vannes tournées en %d coups de 2\n", blows);
    CHECK(blows >= 6);
    CHECK(!b.stalled());
    CHECK(b.boilerBurst());
    CHECK(b.phaseIndex() == 4);
    CHECK(b.hp() < hp - b.maxHp() * 0.05f);  // its boiler burst: a heavy blow
    CHECK(b.hpRatio() > d.amount / b.maxHp() + 0.375f);  // never past « Plasma »
    CHECK(m.hurtBoss(1.0f));
}

TEST_CASE("titan: stalled, if its valves are not all turned it restarts with a pressure wave along the floor") {
    Foundry m(3, 30.0f);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    b.setWounds(b.maxHp() * 0.502f, 3);
    REQUIRE(m.hurtBoss(3.0f));
    m.step();
    REQUIRE(b.stalled());
    const float hp = b.hp();
    // One valve turned, the others left.
    Enemy* v = m.w->findEnemy(b.valves().front());
    REQUIRE(v != nullptr);
    v->kill(*m.w);
    for (int i = 0; i < 4; ++i) m.step();
    CHECK(b.valvesLeft() == 2);
    int waves = 0;
    for (int i = 0; i < 120 * 16 && b.stalled(); ++i) {
        m.step();
        m.w->player().health = m.w->player().maxHealth();
    }
    CHECK(!b.stalled());
    CHECK(!b.boilerBurst());
    CHECK(b.phaseIndex() == 4);
    CHECK(b.hp() == hp);
    for (const auto& p : m.w->projectiles())
        if (p.clip == "shockwave" && !p.fromPlayer) ++waves;
    CHECK(waves == 2);  // both ways along the floor
    // Its valves left vanish with its restart.
    for (uint32_t id : b.valves()) CHECK(!(m.w->findEnemy(id) && m.w->findEnemy(id)->alive()));
    // A boss training of « Surchauffe » starts with it running.
    Foundry t(4);
    REQUIRE(t.boss != nullptr);
    CHECK(t.boss->phaseIndex() == 4);
    CHECK(!t.boss->stalled());
    CHECK(t.hurtBoss(1.0f));
}

// ================================================================ the fight

TEST_CASE("titan: in its foundry it fights, walks on its floor, never falls, uses its machinery in every phase") {
    std::set<AttackType> used;
    std::set<std::string> names;
    int vents = 0;
    for (int still = 0; still < 2; ++still)
        for (int phase = 0; phase < 8; ++phase) {
            const float spots[8] = {30.0f, 8.0f, 88.0f, 48.0f, 20.0f, 70.0f, 11.0f, 60.0f};
            Foundry m(phase, still ? spots[phase] : 30.0f);
            REQUIRE(m.boss != nullptr);
            float dir = 1.0f;
            float minX = 1e9f, maxX = -1e9f;
            bool onFloor = true;
            EnemyState last = EnemyState::Idle;
            for (int i = 0; i < 30 * 120; ++i) {
                PlayerInput in;
                if (!still) {
                    float x = m.w->player().feet().x;
                    float lo = m.zone.x + 18.0f * 16.0f, hi = m.zone.x + 78.0f * 16.0f;
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
                if (b->stalled()) break;
                onFloor = onFloor && b->box.bottom() <= m.floorY() + 2.0f && b->box.bottom() > m.floorY() - 40.0f;
                minX = std::min(minX, b->center().x);
                maxX = std::max(maxX, b->center().x);
            }
            vents += m.boss->vents();
            std::printf("  phase %d, joueur %s : de x %.0f à %.0f, %d surchauffes\n", phase + 1, still ? "immobile" : "mobile",
                        (minX - m.zone.x) / 16.0f, (maxX - m.zone.x) / 16.0f, m.boss->vents());
            CHECK(onFloor);
            // It stays on its floor between the vats.
            CHECK(minX >= m.zone.x + 16.0f * 16.0f);
            CHECK(maxX <= m.zone.x + 80.0f * 16.0f);
        }
    for (AttackType t : {AttackType::Press, AttackType::Missile, AttackType::Belt, AttackType::Slam, AttackType::Laser,
                         AttackType::Eruption, AttackType::Summon}) {
        if (!used.count(t)) std::printf("  jamais choisie : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    std::printf("  %zu attaques différentes choisies sur les huit phases, %d surchauffes\n", names.size(), vents);
    CHECK(names.size() >= 15);
    CHECK(vents >= 8);
}
