// Boomerang chargé (2.50): hold the power's key to charge, release to throw; the blade spins on
// the first enemy it touches (1 + 1 every 0,3 s, to that enemy only) for 0,4 s (tap) to 2,0 s
// (full charge), then comes back. Energy 25, paid at the release.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/powers/PowerKeys.h"
#include "game/powers/PowerStats.h"
#include "game/powers/Powers.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;
constexpr int kFull = 144;  // steps of a full charge (1,2 s), counted after the press

struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    World() {
        content.reload();
        res.init(nullptr, &audio);
    }
    GameWorld& make(const LevelData& level, bool keysPerSlot = false) {
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->silent = true;
        w->setKeysPerSlot(keysPerSlot);
        w->load(level);
        return *w;
    }
};

LevelData flat(std::vector<std::string> powers) {
    LevelData l = LevelData::makeEmpty("test_boomerang_charge", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(powers);
    l.maxPowers = 0;
    return l;
}

PlayerInput input(Action a, bool pressed, bool held, float moveX = 0.0f) {
    PlayerInput in;
    in.pressed[static_cast<int>(a)] = pressed;
    in.held[static_cast<int>(a)] = held || pressed;
    in.move.x = moveX;
    if (moveX > 0.0f) in.held[static_cast<int>(Action::MoveRight)] = true;
    return in;
}

/// One step through the recorded form of the inputs (what a replay plays back).
void step(GameWorld& w, const PlayerInput& in = PlayerInput{}) { stepWorld(w, InputFrame::fromPlayerInput(in), kDt); }

void steps(GameWorld& w, int n) {
    for (int i = 0; i < n; ++i) step(w);
}

/// Presses the key, holds it `held` more steps, then releases it (one more step).
void chargeAndThrow(GameWorld& w, int held, Action key = Action::Special) {
    for (int i = 0; i <= held; ++i) step(w, input(key, i == 0, true));
    step(w);
}

BoomerangChargePower* power(GameWorld& w) {
    for (const auto& p : w.player().powers())
        if (auto* b = dynamic_cast<BoomerangChargePower*>(p.get())) return b;
    return nullptr;
}

std::string statOf(const std::vector<PowerStat>& stats, const std::string& key) {
    for (const auto& s : stats)
        if (s.key == key) return s.value;
    return {};
}

/// Width of a PNG asset, read from its header (0 if unreadable).
int pngWidth(const ResourceManager& res, const std::string& rel) {
    auto path = res.resolve(rel);
    if (!path) return 0;
    std::ifstream f(*path, std::ios::binary);
    unsigned char h[24] = {};
    if (!f.read(reinterpret_cast<char*>(h), sizeof h)) return 0;
    return (h[16] << 24) | (h[17] << 16) | (h[18] << 8) | h[19];
}

/// A golem (12 PV, no weakness to the blade) far enough not to see Nova.
Enemy* golem(GameWorld& w, float dx) {
    Enemy* e = w.spawnEnemy("golem", w.player().feet() + Vec2{dx, 0.0f}, true, {}, 0.0f);
    return e;
}
}  // namespace

TEST_CASE("boomerang chargé: its definition and its sheet") {
    World env;
    const PowerDefinition* d = env.content.power("boomerang_charge");
    CHECK(d != nullptr);
    if (!d) return;
    CHECK(PowerRegistry::instance().has(d->behavior));
    CHECK(d->name == "Boomerang chargé");
    CHECK_NEAR(d->energyCost, 25.0f, 0.001f);
    CHECK_NEAR(d->cooldown, 0.3f, 0.001f);
    CHECK(d->icon == 18);
    CHECK(d->input == Action::Special);
    CHECK(powerTriggerInfo(*d).kind == PowerTrigger::SlotKey);  // held on the key of its slot
    CHECK(env.res.exists(d->sound));
    CHECK(env.res.exists("sounds/boomerang_return.wav"));
    // Its own icon cell, inside the sheet.
    CHECK(pngWidth(env.res, "ui/icons_powers.png") >= 32 * (d->icon + 1));
    for (const PowerDefinition* o : env.content.powers())
        if (o->id != d->id) CHECK(o->icon != d->icon);
    auto unlock = env.res.animation("animations/power_unlock.json");
    CHECK(unlock != nullptr && unlock->clip("boomerang_charge") != nullptr);
    // Suggested key and button: free of any command and of every other power.
    const InputBindings base = InputBindings::defaults();
    CHECK(baseCommandOfKey(base, d->suggestedKey).empty());
    CHECK(baseCommandOfPad(base, d->suggestedPad).empty());
    for (const PowerDefinition* o : env.content.powers())
        if (o->id != d->id) {
            CHECK(o->suggestedKey != d->suggestedKey);
            CHECK(!(o->suggestedPad == d->suggestedPad));
        }
    // The sheet: energy 25, charge max 1,2 s, rotation 0,4 → 2,0 s, damage 1 + 1 every 0,3 s,
    // range 260 → 360 px, in that order.
    auto stats = powerStats(*d);
    CHECK(statOf(stats, "energy") == "25");
    CHECK(statOf(stats, "chargeMax") == "1,2 s");
    CHECK(statOf(stats, "spinTimes") == "0,4 → 2,0 s");
    CHECK(statOf(stats, "damage") == "1 + 1 toutes les 0,3 s");
    CHECK(statOf(stats, "range") == "260 → 360 px");
    CHECK(statOf(stats, "speed") == "600 px/s");
    CHECK(statOf(stats, "cooldown") == "0,3 s");
    std::vector<std::string> order;
    for (const auto& s : stats) order.push_back(s.key);
    CHECK(order.size() >= 5);
    if (order.size() >= 5) CHECK((std::vector<std::string>(order.begin(), order.begin() + 5) ==
                                  std::vector<std::string>{"energy", "chargeMax", "spinTimes", "damage", "range"}));
}

TEST_CASE("boomerang chargé: the charge sets the spin (0,4 / 0,9 / 1,5 / 2,0 s) and the blows (1 / 3 / 5 / 6)") {
    struct Tier {
        const char* name;
        int held;       // steps the key is held
        float spin;     // seconds spinning on the target
        int ticks;      // blows after the impact
        float range;
    };
    const Tier tiers[] = {{"tap", 0, 0.4f, 1, 260.0f},
                          {"1/3", kFull / 3, 0.9f, 3, 260.0f + 100.0f / 3.0f},
                          {"2/3", kFull * 2 / 3, 1.5f, 5, 260.0f + 200.0f / 3.0f},
                          {"full", kFull, 2.0f, 6, 360.0f}};
    for (const Tier& t : tiers) {
        World env;
        GameWorld& w = env.make(flat({"boomerang_charge"}));
        steps(w, 40);
        w.player().setInvulnerable(60.0f);
        Enemy* e = golem(w, 240.0f);
        CHECK(e != nullptr);
        steps(w, 2);
        auto* b = power(w);
        CHECK(b != nullptr);
        if (!b || !e) continue;
        w.player().facing = 1;
        const float hp = e->hp();
        chargeAndThrow(w, t.held);
        CHECK(b->flying());
        CHECK_NEAR(b->lastCharge(), static_cast<float>(t.held) / kFull, 0.001f);
        CHECK_NEAR(b->throwSpin(), t.spin, 0.001f);
        CHECK_NEAR(b->throwRange(), t.range, 0.05f);
        for (int i = 0; i < 1200 && b->flying(); ++i) step(w);
        std::printf("  %-4s : rotation %.3f s, %d coups, PV %.1f -> %.1f\n", t.name, b->spinElapsed(), b->ticks(), hp, e->hp());
        CHECK(!b->flying());
        CHECK(b->caught() == 1);
        CHECK(b->target() == e->uid());
        CHECK(b->ticks() == t.ticks);
        CHECK(std::abs(b->spinElapsed() - t.spin) < 0.002f);  // to the step
        CHECK_NEAR(hp - e->hp(), 1.0f + static_cast<float>(t.ticks), 0.001f);  // impact + blows
        CHECK(b->hitsBack() == 1);  // only the target, never hit again on the way back
    }
}

TEST_CASE("boomerang chargé: 25 energy paid at the release; with less, nothing is thrown") {
    World env;
    GameWorld& w = env.make(flat({"boomerang_charge"}));
    steps(w, 40);
    auto* b = power(w);
    CHECK(b != nullptr);
    if (!b) return;
    // Charging costs nothing: the energy goes at the release.
    const float full = w.player().energy;
    for (int i = 0; i <= kFull / 2; ++i) step(w, input(Action::Special, i == 0, true));
    CHECK(b->charging());
    CHECK(b->active());
    CHECK_NEAR(b->chargeRatio(), 0.5f, 0.001f);
    CHECK_NEAR(w.player().energy, full, 0.001f);
    step(w);
    CHECK(b->flying());
    CHECK_NEAR(full - w.player().energy, 25.0f, 0.01f);
    for (int i = 0; i < 1200 && b->flying(); ++i) step(w);
    CHECK(b->caught() == 1);
    steps(w, 60);
    // Under 25: the gauge fills (grey) but the release throws nothing and costs nothing.
    w.player().energy = 0.0f;
    for (int i = 0; i <= kFull / 2; ++i) step(w, input(Action::Special, i == 0, true));
    CHECK(b->charging());
    CHECK_NEAR(b->chargeRatio(), 0.5f, 0.001f);
    const float before = w.player().energy;
    CHECK(before < 25.0f);
    step(w);
    CHECK(!b->charging());
    CHECK(!b->flying());
    CHECK(w.player().energy >= before - 0.001f);
    CHECK(b->caught() == 1);
}

TEST_CASE("boomerang chargé: Nova moves freely while charging, and the slot key charges it") {
    World env;
    GameWorld& w = env.make(flat({"boomerang_charge"}), true);  // keys per slot (every run since 2.24)
    steps(w, 40);
    auto* b = power(w);
    CHECK(b != nullptr);
    if (!b) return;
    CHECK(b->key() == Action::PowerSlot1);
    const float x0 = w.player().center().x;
    for (int i = 0; i < 60; ++i) step(w, input(Action::PowerSlot1, i == 0, true, 1.0f));
    CHECK(b->charging());
    CHECK(w.player().center().x > x0 + 40.0f);  // she ran while the gauge filled
    // Releasing the key of the slot throws it.
    step(w);
    CHECK(b->flying());
}

TEST_CASE("boomerang chargé: only its target takes the blows of the spinning blade") {
    World env;
    GameWorld& w = env.make(flat({"boomerang_charge"}));
    steps(w, 40);
    w.player().setInvulnerable(60.0f);
    Enemy* first = golem(w, 240.0f);
    Enemy* other = golem(w, 254.0f);  // overlapping it, behind: the blade spins against it too
    CHECK(first != nullptr && other != nullptr);
    steps(w, 2);
    auto* b = power(w);
    CHECK(b != nullptr);
    if (!b || !first || !other) return;
    w.player().facing = 1;
    const float hpFirst = first->hp(), hpOther = other->hp();
    chargeAndThrow(w, kFull);
    bool touched = false;
    for (int i = 0; i < 1200 && b->phase() != BoomerangChargePower::Phase::Back; ++i) {
        step(w);
        if (b->phase() == BoomerangChargePower::Phase::Spin)
            touched = touched || (b->bladePos() - other->center()).length() < 10.0f + other->box.w * 0.5f;
    }
    CHECK(touched);
    CHECK(b->target() == first->uid());
    CHECK(b->ticks() == 6);
    CHECK_NEAR(hpFirst - first->hp(), 7.0f, 0.001f);
    CHECK_NEAR(hpOther - other->hp(), 0.0f, 0.001f);  // nothing during the spin
    for (int i = 0; i < 1200 && b->flying(); ++i) step(w);
    CHECK(b->caught() == 1);
    CHECK(hpOther - other->hp() <= 1.001f);  // at most the one blow of the way back
}

TEST_CASE("boomerang chargé: a boss's weakness counts on every blow") {
    World env;
    GameWorld& w = env.make(flat({"boomerang_charge"}));
    steps(w, 40);
    w.player().setInvulnerable(60.0f);
    Enemy* boss = w.spawnEnemy("spore_queen", w.player().feet() + Vec2{200.0f, 0.0f}, true, {}, 0.0f);
    CHECK(boss != nullptr);
    steps(w, 2);
    auto* b = power(w);
    CHECK(b != nullptr);
    if (!b || !boss) return;
    CHECK(boss->def().isBoss);
    CHECK_NEAR(boss->def().multiplier(DamageType::Wave), 1.2f, 0.001f);
    w.player().facing = 1;
    const float hp = boss->hp();
    chargeAndThrow(w, kFull);
    for (int i = 0; i < 1200 && b->flying(); ++i) step(w);
    CHECK(b->target() == boss->uid());
    CHECK(b->ticks() == 6);
    CHECK_NEAR(hp - boss->hp(), 7.0f * 1.2f, 0.01f);
}

TEST_CASE("boomerang chargé: a target that dies ends the spin at once and the blade comes back") {
    World env;
    GameWorld& w = env.make(flat({"boomerang_charge"}));
    steps(w, 40);
    w.player().setInvulnerable(60.0f);
    Enemy* e = w.spawnEnemy("glowmite", w.player().feet() + Vec2{150.0f, 0.0f}, true, {}, 0.0f);  // 2 PV
    CHECK(e != nullptr);
    steps(w, 2);
    auto* b = power(w);
    CHECK(b != nullptr);
    if (!b || !e) return;
    w.player().facing = 1;
    const uint32_t uid = e->uid();
    chargeAndThrow(w, kFull);
    CHECK_NEAR(b->throwSpin(), 2.0f, 0.001f);
    bool backAtDeath = false;
    for (int i = 0; i < 600 && b->phase() != BoomerangChargePower::Phase::Back; ++i) {
        step(w);
        Enemy* t = w.findEnemy(uid);
        if (!t || !t->alive()) {
            backAtDeath = b->phase() == BoomerangChargePower::Phase::Back;  // the very step it died
            break;
        }
    }
    CHECK(backAtDeath);
    CHECK(w.stats().kills == 1);
    CHECK(b->ticks() == 1);  // impact, then the first blow at 0,3 s: dead
    CHECK(std::abs(b->spinElapsed() - 0.3f) < 0.6f * kDt);  // ended with that blow, far from the 2 s
    for (int i = 0; i < 1200 && b->flying(); ++i) step(w);
    CHECK(b->caught() == 1);
}

TEST_CASE("boomerang chargé: touching nothing, it goes to its range (260 → 360 px) and comes back") {
    for (int held : {0, kFull}) {
        World env;
        GameWorld& w = env.make(flat({"boomerang_charge"}));
        steps(w, 40);
        auto* b = power(w);
        CHECK(b != nullptr);
        if (!b) return;
        w.player().facing = 1;
        const float x0 = w.player().center().x + 10.0f;  // where the blade leaves her hand
        chargeAndThrow(w, held);
        const float range = held == 0 ? 260.0f : 360.0f;
        float farthest = 0.0f;
        bool back = false;
        for (int i = 0; i < 1200 && b->flying(); ++i) {
            farthest = std::max(farthest, b->bladePos().x - x0);
            back = back || b->phase() == BoomerangChargePower::Phase::Back;
            CHECK(b->phase() != BoomerangChargePower::Phase::Spin);
            step(w);
        }
        std::printf("  charge %d pas : %.1f px\n", held, farthest);
        CHECK(back);
        CHECK(b->caught() == 1);
        CHECK(b->target() == 0);
        CHECK(farthest > range - 8.0f && farthest < range + 2.0f);
    }
}

TEST_CASE("boomerang chargé: one blade at a time, the recharge starts when it is caught") {
    World env;
    GameWorld& w = env.make(flat({"boomerang_charge"}));
    steps(w, 40);
    auto* b = power(w);
    CHECK(b != nullptr);
    if (!b) return;
    w.player().facing = 1;
    chargeAndThrow(w, 0);
    CHECK(b->flying());
    // While it flies, holding the key charges nothing and releasing throws nothing.
    float energy = w.player().energy;
    for (int i = 0; i < 30; ++i) step(w, input(Action::Special, i == 0, true));
    CHECK(!b->charging());
    step(w);
    CHECK(w.player().energy >= energy - 0.001f);
    int guard = 0;
    while (b->flying() && guard++ < 1200) step(w);
    CHECK(b->caught() == 1);
    CHECK_NEAR(b->cooldownRemaining(), 0.3f - kDt, 0.02f);
    // Still held at the catch: the charge waits for the recharge, then starts.
    step(w, input(Action::Special, true, true));
    CHECK(!b->charging());
    for (int i = 0; i < 40; ++i) step(w, input(Action::Special, false, true));
    CHECK(b->charging());
    step(w);
    CHECK(b->flying());
    for (int i = 0; i < 1200 && b->flying(); ++i) step(w);
    CHECK(b->caught() == 2);
}

TEST_CASE("boomerang chargé: two runs with the same inputs stay identical") {
    auto run = [](std::vector<uint64_t>& digests) {
        World env;
        GameWorld& w = env.make(flat({"boomerang_charge"}), true);
        steps(w, 40);
        golem(w, 230.0f);
        w.spawnEnemy("glowmite", w.player().feet() + Vec2{120.0f, 0.0f}, true, {}, 30.0f);
        for (int i = 0; i < 900; ++i) {
            PlayerInput in;
            if ((i >= 10 && i < 10 + kFull) || (i >= 500 && i < 540)) in = input(Action::PowerSlot1, i == 10 || i == 500, true);
            if (i > 700 && i < 760) in.move.x = 1.0f;
            step(w, in);
            if (i % 30 == 0) digests.push_back(simulationStateHash(w));
        }
        digests.push_back(simulationStateHash(w));
    };
    std::vector<uint64_t> a, b;
    run(a);
    run(b);
    CHECK(a == b);
}
