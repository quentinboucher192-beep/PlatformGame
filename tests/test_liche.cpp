// La Liche des Catacombes (2.57): the fifth giga boss of Les Confins and « La Crypte des Noms ». It
// floats over the altars of its crypt and teleports from one to another (the next altar shown
// before it appears there), its souls and its bones are its shots, and it has two spells of its
// own (game/enemies/LicheAttacks.cpp): « Runes funestes » (runes traced on the floor Nova stands on:
// armed, one bursts when she steps on it, every rune left bursts at its end: a step between two of
// them, or a jump over one, is enough) and « Maléfice » (a circle of runes closes around her: still
// inside, her jumps grow heavy for a while; a step out is enough). At the end of its 4th phase,
// « Spectres », it hides its soul in its phylactery (a crystal set on one of its altars): it cannot
// be hurt until the crystal is broken. Right under it, or beside it, is no shelter; it never stays
// against a wall.

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

const EnemyDefinition& liche() {
    static const EnemyDefinition none;
    const EnemyDefinition* d = content().enemy("liche");
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
        LevelData l = LevelData::makeEmpty("test_liche", "Test", "pyralis");
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
        Enemy* e = w->spawnEnemy("liche", feet, true, "boss");
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

/// confins_05 (« La Crypte des Noms ») in a world, the fight started at `phase`, the player at
/// `tile` (columns of the arena; 48: the middle).
struct Crypt {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::optional<LevelData> level;
    std::string bossObject;
    Rect zone;
    Enemy* boss = nullptr;
    explicit Crypt(int phase, float tile = 48.0f) {
        res.init(nullptr, &audio);
        level = content().loadLevel("confins_05");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed("confins_05"));
        w->silent = true;
        for (const LevelObject& o : level->objects)
            if (o.type == "enemy" && o.propStr("enemy") == "liche") bossObject = o.id;
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
    float floorY() const { return zone.bottom() - 4.0f * 16.0f; }  // the ossuaries are 4 tiles deep
    /// Hurts the boss once it can be (its appearance, a teleport over), `tries` steps at most.
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

/// Attacks that come down on a player standing right under it, or close in around her.
bool comesDown(AttackType t) {
    return t == AttackType::Glyph || t == AttackType::Hex || t == AttackType::Rain || t == AttackType::Eruption ||
           t == AttackType::Cage;
}

std::set<AttackType> licheTypes() { return {AttackType::Glyph, AttackType::Hex}; }

/// The height of a jump from the floor (px), the player held still before and after.
float jumpHeight(Room& r) {
    for (int i = 0; i < 120; ++i) r.step();  // up again after a blow
    const float y0 = r.p().box.bottom();
    float top = y0;
    for (int i = 0; i < 120; ++i) {
        PlayerInput in;
        if (i < 70) {  // pressed once, held to the apex: no jump cut, no double jump
            in = jump();
            if (i > 0) in.pressed[static_cast<int>(Action::Jump)] = false;
        }
        r.step(in);
        top = std::min(top, r.p().box.bottom());
    }
    return y0 - top;
}
}  // namespace

// ===================================================================== data

TEST_CASE("liche: two spells of its own, named in French, saved only where used") {
    std::set<std::string> ids, labels;
    for (int i = 0; i < static_cast<int>(AttackType::Count); ++i) {
        AttackType t = static_cast<AttackType>(i);
        CHECK(ids.insert(attackTypeId(t)).second);
        CHECK(labels.insert(attackTypeLabel(t)).second);
    }
    for (AttackType t : licheTypes()) {
        AttackDef a;
        a.type = t;
        CHECK(a.isLicheType() && !a.isPhoenixType() && !a.isGolemType() && !a.isDragonType() && !a.isGuardianType());
        CHECK(enumFromId(std::string(attackTypeId(t)), &attackTypeId, AttackType::Melee) == t);
    }
    CHECK(std::string(attackTypeLabel(AttackType::Glyph)) == "Runes funestes");
    CHECK(std::string(attackTypeLabel(AttackType::Hex)) == "Maléfice");
    AttackDef a;
    a.type = AttackType::Glyph;
    a.count = 5;
    a.spread = 72.0f;
    a.burstInterval = 0.7f;
    a.stagger = 0.1f;
    a.duration = 4.0f;
    a.radius = 18.0f;
    a.height = 56.0f;
    a.arms = 2;
    AttackDef b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Glyph && b.count == 5 && b.arms == 2);
    CHECK_NEAR(b.spread, 72.0, 1e-6);
    CHECK_NEAR(b.burstInterval, 0.7, 1e-6);
    CHECK_NEAR(b.stagger, 0.1, 1e-6);
    CHECK_NEAR(b.duration, 4.0, 1e-6);
    CHECK_NEAR(b.height, 56.0, 1e-6);
    a = AttackDef{};
    a.type = AttackType::Hex;
    a.radius = 72.0f;
    a.duration = 5.0f;
    a.curse = 0.7f;
    b = AttackDef::fromJson(a.toJson());
    CHECK(b.type == AttackType::Hex);
    CHECK_NEAR(b.radius, 72.0, 1e-6);
    CHECK_NEAR(b.curse, 0.7, 1e-6);
    // The phylactery of a phase: written only when set (the phases of before keep their
    // fingerprints); the curse of an attack only for its spells.
    BossPhase ph;
    CHECK(!ph.toJson().contains("phylactery"));
    ph.phylactery = "phylactere";
    CHECK(BossPhase::fromJson(ph.toJson()).phylactery == "phylactere");
    for (const EnemyDefinition* d : content().enemies()) {
        if (d->id == "liche") continue;
        for (const BossPhase& p : d->phases) CHECK(p.phylactery.empty());
        for (const AttackDef& x : d->attacks) {
            CHECK(!x.isLicheType());
            CHECK(!x.toJson().contains("curse"));
        }
    }
}

TEST_CASE("liche: a giga boss of eight phases, eight soul fires, its souls, its bones, its runes, its curse and its phylactery") {
    const EnemyDefinition& d = liche();
    REQUIRE(d.phases.size() == 8);
    CHECK(d.isBoss && d.builtin);
    CHECK(d.name == "Liche");
    CHECK(d.bossTitle == "LA LICHE DES CATACOMBES");
    CHECK(d.flying);
    CHECK(!d.canTeleport);  // it teleports between its altars with its own brain
    CHECK(d.scale > 1.2f);
    std::set<std::string> lights, sheets;
    std::set<AttackType> used;
    int souls = 0, bones = 0;
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
            if (a->projectile == "soul") ++souls;
            if (a->projectile == "bone") ++bones;
        }
        CHECK(down);  // something that comes down on a player right under it, in every phase
        CHECK((p.enrageTime > 0.0f) == (i == 7));  // the rage in its last phase only
        CHECK(!p.phylactery.empty() == (i == 3));  // its phylactery at the end of « Spectres » only
        CHECK(p.rebirth == 0.0f && !p.ashes);       // not the Phénix's
    }
    CHECK(d.phases[3].name == "Spectres");
    CHECK(d.phases[4].name == "Phylactère brisé");
    const EnemyDefinition* crystal = content().enemy(d.phases[3].phylactery);
    REQUIRE(crystal != nullptr);
    CHECK(!crystal->isBoss);
    CHECK(crystal->name == "Phylactère de la Liche");
    CHECK(crystal->attacks.empty());
    CHECK(crystal->contactDamage == 0.0f);
    CHECK(lights.size() == 8);
    CHECK(sheets.size() == 8);
    for (const std::string& s : sheets) CHECK(fs::exists(Paths::assetsDir() / s));
    CHECK(souls >= 8);
    CHECK(bones >= 6);
    // Its identity: its two spells, its walls of bones, its souls in fans, homing, in a spiral and
    // around it, its rays, its knell, the rain and the bursts of bones, the embrace and the well of
    // souls, its spectres.
    for (AttackType t : {AttackType::Glyph, AttackType::Hex, AttackType::Cage, AttackType::Spread, AttackType::Projectile,
                         AttackType::Arc, AttackType::Laser, AttackType::Rain, AttackType::Eruption, AttackType::Ring,
                         AttackType::Spiral, AttackType::Orbit, AttackType::Vortex, AttackType::Well, AttackType::Summon}) {
        if (!used.count(t)) std::printf("  type absent : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    // Not another giga boss's: it never dives, lands, rolls, leaps out of sight, floods its crypt,
    // calls columns of sunlight, throws blades, nor splits into doubles.
    for (AttackType t : {AttackType::Dive, AttackType::Perch, AttackType::Shell, AttackType::Skyfall, AttackType::Flood,
                         AttackType::Sunbeam, AttackType::Sweep, AttackType::Mirror, AttackType::Prism}) CHECK(used.count(t) == 0);
    for (const AttackDef& a : d.attacks) {
        if (a.type == AttackType::Cage) CHECK(a.look == "bone");
        if (a.type == AttackType::Summon) {
            const EnemyDefinition* c = content().enemy(a.summonId);
            REQUIRE(c != nullptr);
            CHECK(!c->isBoss);
            CHECK(c->name == "Spectre de la Liche");
        }
    }
    // The first phase already traces its runes; its curse from the third.
    std::set<AttackType> first, third;
    for (const auto& id : d.phases[0].attacks) first.insert(d.attack(id)->type);
    for (const auto& id : d.phases[2].attacks) third.insert(d.attack(id)->type);
    CHECK(first.count(AttackType::Glyph) == 1);
    CHECK(third.count(AttackType::Hex) == 1);
    // Its last spell: never chosen in a phase.
    REQUIRE(!d.finalAttacks.empty());
    CHECK(!d.finalText.empty());
    for (const auto& id : d.finalAttacks) {
        REQUIRE(d.attack(id) != nullptr);
        for (const BossPhase& p : d.phases) CHECK(std::find(p.attacks.begin(), p.attacks.end(), id) == p.attacks.end());
    }
    CHECK(bossDifficulty(d).stars == 10);
}

TEST_CASE("liche: its sheets are anchored at the tips of the tails of its robe (nothing drawn under its feet)") {
    // The pivot is the bottom of the art (2.57, as the Kraken's and the Phénix's): low over the
    // floor it never goes through it nor under the HUD.
    for (int k = 1; k <= 8; ++k) {
        const std::string id = k == 1 ? std::string("liche") : "liche_p" + std::to_string(k);
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
    // Its box reaches its pivot: 56x160 sheet pixels, x1.25.
    CHECK_NEAR(liche().size.x, 70.0, 1e-6);
    CHECK_NEAR(liche().size.y, 200.0, 1e-6);
}

TEST_CASE("liche: « La Crypte des Noms », the last level of Les Confins, the fight alone among its altars") {
    const PlanetData* confins = content().planet("confins");
    REQUIRE(confins != nullptr);
    REQUIRE(!confins->levels.empty());
    CHECK(confins->levels.back() == "confins_05");
    auto level = content().loadLevel("confins_05");
    REQUIRE(level.has_value());
    CHECK(level->name == "La Crypte des Noms");
    CHECK(level->extreme);
    CHECK(level->bossMusic == "music/liche_boss.ogg");
    int bosses = 0, checkpoints = 0, souls = 0, altars = 0;
    bool music = false;
    for (const LevelObject& o : level->objects) {
        if (o.type == "enemy" && o.propStr("enemy") == "liche") ++bosses;
        if (o.type == "checkpoint") ++checkpoints;
        if (o.type == "damage_zone" && o.propStr("style") == "souls") ++souls;
        if (o.type == "altar") ++altars;
        if (o.type == "combat_zone" && o.propStr("music") == "music/liche_boss.ogg") music = true;
    }
    CHECK(bosses == 1);
    CHECK(checkpoints == 0);
    CHECK(souls == 6);   // two ossuaries, two curtains then two more
    CHECK(altars == 4);  // two on the floor, two on the ledges at 5 tiles
    CHECK(music);
    CHECK(fs::exists(Paths::assetsDir() / "music" / "liche_boss.ogg"));
}

// ================================================================= its altars

TEST_CASE("liche: it floats over its altars and teleports from one to another, the next one shown before") {
    Crypt m(0);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    std::set<int> visited;
    int teleports = 0, shown = 0;
    float overAltar = 0.0f, total = 0.0f;
    EnemyState last = b.state();
    float dir = 1.0f;
    for (int i = 0; i < 60 * 120; ++i) {
        float x = m.w->player().feet().x;
        if (x > m.zone.x + 80.0f * 16.0f) dir = -1.0f;
        if (x < m.zone.x + 16.0f * 16.0f) dir = 1.0f;
        PlayerInput in = run(dir);
        if (i % 90 == 0) in = jump(in);
        m.step(in);
        m.w->player().health = m.w->player().maxHealth();
        if (b.state() == EnemyState::TeleportOut) {
            if (last != EnemyState::TeleportOut) ++teleports;
            if (b.altarNext() >= 0 && b.altarNext() != b.altarIndex()) ++shown;
        }
        last = b.state();
        if (b.altarIndex() >= 0) visited.insert(b.altarIndex());
        if ((b.state() == EnemyState::Chase || b.state() == EnemyState::Recover) && b.altarIndex() >= 0) {
            total += kDt;
            const Vec2 a = b.altars()[static_cast<size_t>(b.altarIndex())];
            if (std::abs(b.box.center().x - a.x) < 60.0f && b.box.bottom() < a.y && b.box.bottom() > a.y - 140.0f) overAltar += kDt;
        }
    }
    std::printf("  autels : %zu visités, %d téléportations, au-dessus de son autel %.0f %% du temps libre\n", visited.size(), teleports,
                100.0f * overAltar / std::max(0.01f, total));
    CHECK(b.altars().size() == 4);
    CHECK(visited.size() >= 3);
    CHECK(teleports >= 6);
    CHECK(shown > 0);
    CHECK(overAltar > total * 0.6f);
    // It cannot be hurt while it fades from one altar to another (and comes back).
    for (int i = 0; i < 120 * 20 && b.state() != EnemyState::TeleportOut; ++i) m.step();
    REQUIRE(b.state() == EnemyState::TeleportOut);
    DamageInfo d;
    d.amount = 5.0f;
    d.type = DamageType::ChargeShot;
    d.source = b.center();
    CHECK(!b.takeDamage(*m.w, d));
}

// ================================================================= its runes

TEST_CASE("liche: its runes are traced on her floor, then armed: standing on one is hit, a step between two is enough") {
    for (int away = 0; away < 2; ++away) {
        Room r;
        Enemy* e = r.spawn({1100.0f, kFloor - 60.0f});
        REQUIRE(e != nullptr);
        REQUIRE(e->forceAttack(*r.w, "runes"));
        const float px = r.p().center().x;
        std::vector<Vec2> marks;
        while (e->state() == EnemyState::Telegraph) {
            marks = e->attackPath();
            r.step();
        }
        REQUIRE(marks.size() == 5);
        // A row through her on her floor, the gap kept.
        CHECK(std::abs(marks[2].x - px) < 20.0f);
        for (size_t k = 1; k < marks.size(); ++k) CHECK(std::abs(marks[k].x - marks[k - 1].x - 72.0f) < 1.0f);
        for (const Vec2& mk : marks) CHECK(std::abs(mk.y - kFloor) < 1.0f);
        r.step();
        e->setDormant();  // only its runes from now on
        const auto& runes = r.w->hazards().runes();
        REQUIRE(runes.size() == 5);
        for (const auto& rn : runes) CHECK(!rn.armed());  // traced, not yet armed
        int start = r.p().health;
        bool running = away == 1;
        const float target = marks[2].x + 36.0f;  // half a gap to the right of the middle one
        float armedAt = -1.0f;
        for (int i = 0; i < 120 * 6; ++i) {
            if (running && r.p().center().x >= target - 2.0f) running = false;
            r.step(running ? run(1.0f) : PlayerInput{});
            if (armedAt < 0.0f && !r.w->hazards().runes().empty() && r.w->hazards().runes()[0].armed()) armedAt = static_cast<float>(i) * kDt;
        }
        std::printf("  runes, joueur %s : armées après %.2f s, %d coup(s)\n", away ? "entre deux runes" : "immobile", armedAt, start - r.p().health);
        CHECK(armedAt > 0.3f);
        CHECK(r.w->hazards().runes().empty());  // all burst at their end
        CHECK((r.p().health < start) == (away == 0));
    }
}

TEST_CASE("liche: an armed rune is cleared by a jump") {
    Room r;
    Enemy* e = r.spawn({1100.0f, kFloor - 60.0f});
    REQUIRE(e != nullptr);
    REQUIRE(e->forceAttack(*r.w, "runes"));
    std::vector<Vec2> marks;
    while (e->state() == EnemyState::Telegraph) {
        marks = e->attackPath();
        r.step();
    }
    REQUIRE(marks.size() == 5);
    r.step();
    e->setDormant();
    // Out of the row first (to the left of the first rune, before they are armed), then across the
    // whole row once armed.
    Player& p = r.p();
    p.box = Rect::fromBottomCenter({marks.front().x - 60.0f, kFloor}, p.box.size());
    p.prevBox = p.box;
    p.vel = {};
    auto allArmed = [&] {
        for (const auto& rn : r.w->hazards().runes())
            if (!rn.armed()) return false;
        return !r.w->hazards().runes().empty();
    };
    for (int i = 0; i < 360 && !allArmed(); ++i) r.step();
    REQUIRE(r.w->hazards().runes().size() == 5);
    CHECK(allArmed());
    const int start = r.p().health;
    // Run right, a short hop over each rune (the jump button tapped).
    int hold = 0;
    for (int i = 0; i < 120 * 3 && r.p().center().x < marks.back().x + 40.0f; ++i) {
        PlayerInput in = run(1.0f);
        float next = 1e9f;
        for (const auto& rn : r.w->hazards().runes())
            if (rn.at.x + rn.half > r.p().box.x) next = std::min(next, rn.at.x - rn.half - r.p().box.right());
        if (hold == 0 && r.p().grounded() && next < 6.0f) hold = 8;
        if (hold > 0) {
            --hold;
            in = jump(in);
            if (hold < 7) in.pressed[static_cast<int>(Action::Jump)] = false;
        }
        r.step(in);
    }
    std::printf("  rangée de runes franchie en sautant : %d coup(s)\n", start - r.p().health);
    CHECK(r.p().center().x > marks.back().x);
    CHECK(r.p().health == start);
}

// ================================================================= its curse

TEST_CASE("liche: its curse: still inside the circle when it closes, her jumps grow heavy for a while; a step out is enough") {
    for (int away = 0; away < 2; ++away) {
        Room r;
        const float before = jumpHeight(r);
        Enemy* e = r.spawn({1150.0f, kFloor - 60.0f});
        REQUIRE(e != nullptr);
        REQUIRE(e->forceAttack(*r.w, "malefice"));
        const Vec2 mark = e->attackMark();
        CHECK(distance(mark, r.p().center()) < 20.0f);  // around her
        CHECK_NEAR(e->ringRadius(), 72.0, 1e-3);
        bool running = away == 1;
        int steps = 0;
        while ((e->state() == EnemyState::Telegraph || e->state() == EnemyState::Attack) && steps < 600) {
            if (running && r.p().center().x < mark.x - e->ringRadius() - 12.0f) running = false;
            r.step(running ? run(-1.0f) : PlayerInput{});
            ++steps;
        }
        e->setDormant();
        std::printf("  maléfice, joueur %s : %s\n", away ? "sorti du cercle" : "immobile", r.p().cursed() ? "maudite" : "épargnée");
        CHECK(r.p().cursed() == (away == 0));
        CHECK(e->hexLanded() == (away == 0));
        if (away == 0) {
            const float cursedJump = jumpHeight(r);
            std::printf("  saut : %.1f px, maudite %.1f px (%.0f %%)\n", before, cursedJump, 100.0f * cursedJump / before);
            CHECK(cursedJump < before * 0.8f);
            CHECK(cursedJump > before * 0.6f);
            // It is lifted after its time.
            for (int i = 0; i < 120 * 6; ++i) r.step();
            CHECK(!r.p().cursed());
            CHECK(std::abs(jumpHeight(r) - before) < 1.0f);
        }
    }
}

// ================================================================= its phylactery

TEST_CASE("liche: at the end of « Spectres » it hides its soul in its phylactery on an altar: it cannot be hurt until the crystal breaks") {
    Crypt m(3);
    REQUIRE(m.boss != nullptr);
    Enemy& b = *m.boss;
    CHECK(b.phaseIndex() == 3);
    CHECK(!b.warded());
    // Its health right above the end of the phase, then a hit.
    b.setWounds(b.maxHp() * 0.502f, 3);
    REQUIRE(m.hurtBoss(3.0f));
    m.step();
    REQUIRE(b.warded());
    CHECK(b.phaseIndex() == 3);
    const Enemy* crystal = m.w->findEnemy(b.phylactery());
    REQUIRE(crystal != nullptr);
    CHECK(crystal->def().id == "phylactere");
    REQUIRE(b.phylacteryAltar() >= 0);
    const Vec2 altar = b.altars()[static_cast<size_t>(b.phylacteryAltar())];
    CHECK(std::abs(crystal->box.bottom() - altar.y) < 2.0f);  // on its altar
    CHECK(std::abs(altar.y - m.floorY() + 2.5f * 16.0f) < 2.0f);  // an altar of the floor
    // Nothing hurts it while the crystal stands, for 10 s; it keeps fighting.
    DamageInfo d;
    d.amount = 5.0f;
    d.type = DamageType::ChargeShot;
    bool refused = true;
    std::set<std::string> spells;
    const float hp = b.hp();
    for (int i = 0; i < 120 * 10; ++i) {
        m.step();
        m.w->player().health = m.w->player().maxHealth();
        d.source = b.center();
        if (b.takeDamage(*m.w, d)) refused = false;
        if (b.state() == EnemyState::Telegraph) spells.insert(b.currentAttackName());
    }
    CHECK(refused);
    CHECK(b.hp() == hp);
    CHECK(b.warded());
    CHECK(b.phaseIndex() == 3);
    CHECK(!spells.empty());
    // It teleports on, but never to the crystal's altar.
    // The crystal broken: the next phase begins and it can be hurt again.
    Enemy* c = m.w->findEnemy(b.phylactery());
    REQUIRE(c != nullptr);
    DamageInfo hit;
    hit.amount = 4.0f;
    hit.type = DamageType::Melee;
    int blows = 0;
    for (int i = 0; i < 2000 && c->alive(); ++i) {
        hit.source = c->center() + Vec2{-20.0f, 0.0f};
        if (c->takeDamage(*m.w, hit)) ++blows;
        m.step();
        m.w->player().health = m.w->player().maxHealth();
    }
    for (int i = 0; i < 4; ++i) m.step();
    std::printf("  phylactère brisé en %d coups de 4\n", blows);
    CHECK(blows >= 5);
    CHECK(!b.warded());
    CHECK(b.phaseIndex() == 4);
    CHECK(m.hurtBoss(1.0f));
    // A boss training of « Phylactère brisé » starts with its soul back.
    Crypt t(4);
    REQUIRE(t.boss != nullptr);
    CHECK(t.boss->phaseIndex() == 4);
    CHECK(!t.boss->warded());
    CHECK(t.hurtBoss(1.0f));
}

// ================================================================= the fight

TEST_CASE("liche: right under it or beside it is no shelter, in every phase") {
    // The player stands still for 20 s, the Liche right above her, then beside her.
    for (int phase = 0; phase < 8; ++phase) {
        for (bool beside : {false, true}) {
            Crypt m(phase);
            REQUIRE(m.boss != nullptr);
            Vec2 pc = m.w->player().center();
            m.boss->box = Rect::fromCenter({pc.x + (beside ? 200.0f : 10.0f), pc.y - (beside ? 110.0f : 160.0f)}, m.boss->box.size());
            m.boss->prevBox = m.boss->box;
            int hits = 0;
            std::set<std::string> by;
            for (int i = 0; i < 20 * 120; ++i) {
                int before = m.w->player().health;
                m.step();
                if (m.w->player().health < before) {
                    ++hits;
                    by.insert(m.boss->currentAttackName());
                }
                m.w->player().health = m.w->player().maxHealth();
            }
            std::printf("  phase %d : %d coups en 20 s %s la Liche (%zu attaques différentes)\n", phase + 1, hits,
                        beside ? "à côté de" : "sous", by.size());
            CHECK(hits >= 4);
        }
    }
}

TEST_CASE("liche: in its crypt it fights, floats from altar to altar, never stays against a wall nor under the roof, uses its spells") {
    std::set<AttackType> used;
    std::set<std::string> names;
    for (int still = 0; still < 2; ++still)
        for (int phase = 0; phase < 8; ++phase) {
            // A still player stands somewhere new each phase: the middle, near a wall, under an
            // altar of a ledge, by an altar of the floor, at the edge of an ossuary.
            float tile = 48.0f;
            if (still) {
                const float spots[8] = {48.0f, 8.0f, 88.0f, 10.0f, 37.0f, 59.0f, 86.0f, 24.0f};
                tile = spots[phase];
            }
            Crypt m(phase, tile);
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
                    float lo = m.zone.x + (phase >= 4 ? 24.0f : 8.0f) * 16.0f, hi = m.zone.x + (phase >= 4 ? 72.0f : 88.0f) * 16.0f;
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
            // It goes from altar to altar: from one side of her to the other.
            CHECK(maxX - minX > 300.0f);
        }
    for (AttackType t : {AttackType::Glyph, AttackType::Hex, AttackType::Cage, AttackType::Laser, AttackType::Rain,
                         AttackType::Eruption, AttackType::Vortex, AttackType::Well, AttackType::Summon}) {
        if (!used.count(t)) std::printf("  jamais choisie : %s\n", attackTypeId(t));
        CHECK(used.count(t) == 1);
    }
    std::printf("  %zu attaques différentes choisies sur les huit phases\n", names.size());
    CHECK(names.size() >= 18);
}
