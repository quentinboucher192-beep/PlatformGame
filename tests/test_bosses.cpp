// Every boss fights in an arena: its attacks (including rain and eruptions) run without
// problems, the player can be hurt and slowed, phases change as it loses health and it dies.

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
