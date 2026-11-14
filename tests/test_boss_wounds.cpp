// Bosses keep their wounds when the player dies in their arena (2.5): life and phase.

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include <algorithm>
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;

struct Arena {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::string bossId;
    explicit Arena(Json zoneProps) {
        content.reload();
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_boss_wounds", "Test", "sylvanox");
        l.intro.steps.clear();
        l.timerMode = TimerMode::Unlimited;
        auto add = [&](const std::string& type, Rect r, Json props) {
            LevelObject o;
            o.id = "t" + std::to_string(l.objects.size() + 1);
            o.type = type;
            o.layer = "gameplay";
            o.rect = r;
            o.props = std::move(props);
            l.objects.push_back(o);
        };
        add("enemy", {420, 760, 40, 40}, {{"enemy", "thornwarden"}, {"boss", true}, {"dormant", true}});
        bossId = l.objects.back().id;
        zoneProps["waves"] = bossId;
        zoneProps["lockCamera"] = false;
        zoneProps["announce"] = false;
        add("combat_zone", {260, 600, 400, 200}, zoneProps);
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(l);
        w->startAt({160, 800});
    }
    Enemy* boss() {
        for (const auto& e : w->enemies())
            if (e->objectId() == bossId && e->alive()) return e.get();
        return nullptr;
    }
    void steps(int n) {
        for (int i = 0; i < n; ++i) w->fixedUpdate(PlayerInput{}, kDt);
    }
    /// Walks into the arena, wounds the boss down to `ratio` of its life, dies, comes back.
    void woundThenDie(float ratio) {
        steps(30);
        w->player().teleportTo({300, 800});
        steps(30);
        Enemy* b = boss();
        CHECK(b != nullptr);
        if (!b) return;
        // Small guard-breaking hits until the boss is down to `ratio` (the player is kept alive).
        for (int i = 0; i < 6000 && b->hpRatio() > ratio; ++i) {
            DamageInfo d;
            d.amount = b->maxHp() * 0.04f;
            d.type = DamageType::Impulse;
            d.source = b->center();
            d.fromPlayer = true;
            b->takeDamage(*w, d);
            w->player().health = std::max(w->player().health, 3);
            steps(1);
        }
        steps(240);  // phase transition
        w->player().kill(*w);
        for (int i = 0; i < 900 && !w->player().alive(); ++i) steps(1);
        steps(30);
    }
};
}  // namespace

TEST_CASE("bosses: dying in the arena does not heal the boss") {
    Arena a(Json::object());
    a.woundThenDie(0.5f);
    CHECK(a.w->player().alive());
    // Back in the arena: the same boss, still wounded, still in its second phase.
    a.w->player().teleportTo({300, 800});
    a.steps(30);
    Enemy* b = a.boss();
    CHECK(b != nullptr);
    if (b) {
        CHECK(b->hpRatio() < 0.55f);
        CHECK(b->hpRatio() > 0.3f);
        CHECK(b->phaseIndex() == 1);
    }
}

TEST_CASE("bosses: an arena can still heal its boss when the designer wants it") {
    Arena a(Json{{"bossKeepsDamage", false}});
    a.woundThenDie(0.5f);
    a.w->player().teleportTo({300, 800});
    a.steps(30);
    Enemy* b = a.boss();
    CHECK(b != nullptr);
    if (b) {
        CHECK(b->hpRatio() > 0.99f);
        CHECK(b->phaseIndex() == 0);
    }
}
