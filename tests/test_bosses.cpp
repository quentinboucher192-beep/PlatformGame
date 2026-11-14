// Every boss fights in an arena: its attacks (including rain and eruptions) run without
// problems, the player can be hurt and slowed, phases change as it loses health and it dies.

#include <cstdlib>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;

LevelData arena() {
    LevelData l = LevelData::makeEmpty("test_boss_arena", "Arène", "sylvanox");
    l.intro.steps.clear();
    // Wide floor with a high ceiling, like the boss arenas of the levels.
    for (auto& o : l.objects)
        if (o.type == "solid") o.rect = {0, 800, 2400, 160};
    LevelObject ceiling;
    ceiling.id = "ceiling";
    ceiling.type = "solid";
    ceiling.layer = "terrain";
    ceiling.rect = {0, 340, 2400, 32};
    ceiling.props = {{"material", "ancient_metal"}};
    l.objects.push_back(ceiling);
    return l;
}
}  // namespace

TEST_CASE("bosses: every boss attacks, changes phase and dies") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    int tested = 0;
    for (const EnemyDefinition* def : content.enemies()) {
        if (!def->isBoss || !def->builtin) continue;
        ++tested;
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        GameWorld w(s, WorldMode::Arena);
        CHECK(w.load(arena()));
        for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, kDt);
        Vec2 feet = w.player().feet() + Vec2{260.0f, 0.0f};
        if (def->flying) feet.y -= 140.0f;
        Enemy* boss = w.spawnEnemy(def->id, feet, true);
        CHECK(boss != nullptr);
        std::set<std::string> used;
        bool hurt = false;
        int phases = 0;
        // Fight: the player keeps full health so that every attack gets a chance to play.
        for (int step = 0; step < 120 * 40; ++step) {
            int before = w.player().health;
            w.fixedUpdate(PlayerInput{}, kDt);
            if (w.player().health < before) hurt = true;
            if (!w.player().alive()) {
                w.player().spawn(w, {160, 800}, false, false);
            }
            w.player().health = w.player().maxHealth();
            if (boss->state() == EnemyState::Telegraph || boss->state() == EnemyState::Attack)
                used.insert(boss->currentAttackName());
            // Wear it down through its phases (never below a quarter of its health here).
            if (step % 700 == 699 && boss->hpRatio() > 0.26f) {
                DamageInfo d;
                d.amount = boss->maxHp() * 0.15f;
                d.type = DamageType::ChargeShot;
                d.source = boss->center() - Vec2{20, 0};
                d.fromPlayer = true;
                boss->takeDamage(w, d);
            }
            phases = std::max(phases, boss->phaseIndex() + 1);
        }
        if (used.size() < 3 || !hurt || phases < static_cast<int>(def->phases.size()))
            std::cout << "  " << def->id << " : " << used.size() << " attaques, blessé=" << hurt << ", phases=" << phases
                      << "/" << def->phases.size() << "\n";
        CHECK(used.size() >= 3);
        CHECK(hurt);
        CHECK(phases == static_cast<int>(def->phases.size()));
        // Finish it (the enemy object is destroyed once its death animation is over).
        uint32_t uid = boss->uid();
        auto find = [&]() -> Enemy* {
            for (auto& e : w.enemies())
                if (e->uid() == uid) return e.get();
            return nullptr;
        };
        for (int i = 0; i < 10; ++i) {
            Enemy* b = find();
            if (!b || !b->alive()) break;
            DamageInfo d;
            d.amount = b->maxHp();
            d.type = DamageType::ChargeShot;
            d.source = b->center();
            d.fromPlayer = true;
            b->takeDamage(w, d);
            for (int k = 0; k < 240; ++k) w.fixedUpdate(PlayerInput{}, kDt);
        }
        Enemy* b = find();
        CHECK(b == nullptr || !b->alive());
    }
    CHECK(tested >= 9);
}

namespace {
/// Arena with a high ledge out of melee reach (to test anti-camping).
LevelData arenaWithLedge() {
    LevelData l = arena();
    LevelObject ledge;
    ledge.id = "ledge";
    ledge.type = "solid";
    ledge.layer = "terrain";
    ledge.rect = {80, 640, 160, 16};
    ledge.props = {{"material", "ancient_metal"}};
    l.objects.push_back(ledge);
    return l;
}

struct FightStats {
    int hits = 0;
    int attacks = 0;
    std::set<std::string> used;
    bool enraged = false;
};

/// The player keeps moving (runs from one side to the other and jumps) or stays on the
/// ledge; the boss is never damaged. Counts the hits the player takes.
FightStats fight(const EnemyDefinition& def, bool camp, float seconds) {
    static ContentDatabase* content = nullptr;
    static ResourceManager* res = nullptr;
    static AudioSystem* audio = nullptr;
    if (!content) {
        content = new ContentDatabase();
        content->reload();
        audio = new AudioSystem();
        res = new ResourceManager();
        res->init(nullptr, audio);
    }
    WorldServices s{*res, *audio, *content, nullptr, 1.0f, 1.0f};
    GameWorld w(s, WorldMode::Arena);
    FightStats st;
    if (!w.load(arenaWithLedge())) return st;
    w.seedGameplay(1234);
    if (camp) w.player().spawn(w, {160, 640}, false, false);
    else w.player().spawn(w, {700, 800}, false, false);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    Vec2 feet{1200.0f, 800.0f};
    if (def.flying) feet.y -= 140.0f;
    Enemy* boss = w.spawnEnemy(def.id, feet, true);
    if (!boss) return st;
    boss->alert(w);
    EnemyState last = EnemyState::Idle;
    float dir = 1.0f;
    for (int step = 0; step < static_cast<int>(seconds * 120.0f); ++step) {
        PlayerInput in;
        if (!camp) {
            float x = w.player().feet().x;
            if (x > 1500.0f) dir = -1.0f;
            if (x < 400.0f) dir = 1.0f;
            in.held[static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft)] = true;
            in.move.x = dir;
            if (step % 80 == 0) in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
        }
        int before = w.player().health;
        w.fixedUpdate(in, kDt);
        if (w.player().health < before) {
            ++st.hits;
            if (std::getenv("COSMIC_DEBUG_BOSS") && def.id == std::getenv("COSMIC_DEBUG_BOSS"))
                std::printf("    HIT t=%.2f par %s (%s)\n", step / 120.0, boss->currentAttackName().c_str(), camp ? "perché" : "mobile");
        }
        w.player().health = w.player().maxHealth();
        if (!w.player().alive()) w.player().spawn(w, camp ? Vec2{160, 640} : Vec2{700, 800}, false, false);
        if (boss->state() == EnemyState::Telegraph && last != EnemyState::Telegraph) {
            ++st.attacks;
            st.used.insert(boss->currentAttackName());
        }
        last = boss->state();
        st.enraged = st.enraged || boss->enraged();
        if (std::getenv("COSMIC_DEBUG_BOSS") && def.id == std::getenv("COSMIC_DEBUG_BOSS") && camp && step % 30 == 0)
            std::printf("    t=%.2f boss=(%.0f,%.0f) %s %s joueur=(%.0f,%.0f) pv=%d\n", step / 120.0, boss->center().x, boss->box.bottom(),
                        enemyStateName(boss->state()), boss->currentAttackName().c_str(), w.player().feet().x, w.player().feet().y,
                        w.player().health);
    }
    return st;
}
}  // namespace

TEST_CASE("bosses: they read the fight — moving players get hit, campers get punished, long fights enrage") {
    ContentDatabase content;
    content.reload();
    struct Row {
        std::string id;
        FightStats moving, camping, late;
    };
    std::vector<Row> rows;
    for (const EnemyDefinition* def : content.enemies()) {
        if (!def->isBoss || !def->builtin) continue;
        // "Long" = 90 s, or longer for a boss whose rage comes later (the Astrarque: 90 s).
        float longFight = std::max(90.0f, def->enrageTime + 10.0f);
        Row r{def->id, fight(*def, false, 40.0f), fight(*def, true, 20.0f), fight(*def, false, longFight)};
        std::printf("  %-15s mobile : %2d coups / 40 s, %2d attaques (%zu diff.) | perché : %d coups / 20 s | 90 s : %d coups%s\n", r.id.c_str(),
                    r.moving.hits, r.moving.attacks, r.moving.used.size(), r.camping.hits, r.late.hits, r.late.enraged ? ", rage" : "");
        rows.push_back(r);
    }
    for (const Row& r : rows) {
        CHECK(r.moving.attacks >= 14);     // never idle for long
        CHECK(r.moving.used.size() >= 3);  // varied (phase 1 only here)
        CHECK(r.moving.hits >= 4);         // aimed where the player goes
        CHECK(r.camping.hits >= 2);        // no safe spot
        CHECK(r.late.enraged);             // a fight that drags on enrages it
    }
}
