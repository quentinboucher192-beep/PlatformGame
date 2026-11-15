// Powers of the 2.19 sanctuaries: Lame boomerang, Nova de givre, Leurre holographique,
// Balise de rappel. Each one as the spec describes it (energy, recharge, behaviour), and
// two identical runs using them stay identical.

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/powers/PowerStats.h"
#include "game/powers/Powers.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kDt = 1.0f / 120.0f;

struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    World() {
        content.reload();
        res.init(nullptr, &audio);
    }
    GameWorld& make(const LevelData& level) {
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->silent = true;
        w->load(level);
        return *w;
    }
};

LevelData flat(std::vector<std::string> powers) {
    LevelData l = LevelData::makeEmpty("test_sanctuary_powers", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(powers);
    l.maxPowers = 0;
    return l;
}

void addObject(LevelData& l, const std::string& id, const std::string& type, Rect r, Json props = Json::object()) {
    LevelObject o;
    o.id = id;
    o.type = type;
    o.layer = "gameplay";
    o.rect = r;
    o.props = std::move(props);
    l.objects.push_back(o);
}

PlayerInput press(Action a) {
    PlayerInput in;
    in.held[static_cast<int>(a)] = in.pressed[static_cast<int>(a)] = true;
    return in;
}

PlayerInput hold(Action a, float moveX = 0.0f) {
    PlayerInput in;
    in.held[static_cast<int>(a)] = true;
    in.move.x = moveX;
    return in;
}

void steps(GameWorld& w, int n, const PlayerInput& in = PlayerInput{}) {
    for (int i = 0; i < n; ++i) w.fixedUpdate(in, kDt);
}

template <typename T>
T* powerOf(GameWorld& w) {
    for (const auto& p : w.player().powers())
        if (auto* t = dynamic_cast<T*>(p.get())) return t;
    return nullptr;
}
}  // namespace

TEST_CASE("sanctuary powers: definitions follow the spec (energy, recharge, button, icon, tablet, sounds)") {
    World env;
    struct Want {
        const char* id;
        float energy;
        float cooldown;
        int icon;
    };
    std::set<int> icons;
    auto unlock = env.res.animation("animations/power_unlock.json");
    CHECK(unlock != nullptr);
    for (const Want& want : {Want{"boomerang", 12, 0.25f, 14}, Want{"frost_nova", 30, 6, 15}, Want{"decoy", 25, 8, 16},
                             Want{"recall", 15, 0.3f, 17}}) {
        const PowerDefinition* d = env.content.power(want.id);
        CHECK(d != nullptr);
        if (!d) continue;
        CHECK(PowerRegistry::instance().has(d->behavior));
        CHECK_NEAR(d->energyCost, want.energy, 0.001f);
        CHECK_NEAR(d->cooldown, want.cooldown, 0.001f);
        CHECK(d->icon == want.icon);
        CHECK(d->input == Action::Special);  // an active power with its slot, like the 2.5 ones
        icons.insert(d->icon);
        if (!d->sound.empty()) CHECK(env.res.exists(d->sound));
        if (unlock) CHECK(unlock->clip(want.id) != nullptr);
    }
    CHECK(icons.size() == 4);
    for (const char* s : {"boomerang_throw", "boomerang_return", "frost_nova", "decoy_spawn", "recall_set", "recall_warp"})
        CHECK(env.res.exists(std::string("sounds/") + s + ".wav"));
    auto fx = env.res.animation("animations/power_fx.json");
    CHECK(fx != nullptr);
    if (fx) CHECK(fx->clip("boomerang") != nullptr && fx->clip("beacon") != nullptr);
    // The recall's sheet says what each press costs.
    if (const PowerDefinition* r = env.content.power("recall")) {
        auto stats = powerStats(*r);
        bool place = false;
        for (const auto& s : stats) place = place || (s.key == "placeCost" && s.value == "gratuit");
        CHECK(place);
        CHECK(stats.front().value == "15");
    }
}

TEST_CASE("sanctuary powers: the boomerang goes about 260 px, comes back and hits on both legs") {
    World env;
    GameWorld& w = env.make(flat({"boomerang"}));
    steps(w, 40);
    Enemy* e = w.spawnEnemy("glowmite", w.player().feet() + Vec2{150, 0}, true, {}, 1.0f);
    CHECK(e != nullptr);
    steps(w, 2);
    w.player().facing = 1;
    float energy = w.player().energy;
    Vec2 start = w.player().center();
    w.fixedUpdate(press(Action::Special), kDt);
    auto* b = powerOf<BoomerangPower>(w);
    CHECK(b != nullptr);
    if (!b || !e) return;
    CHECK(b->flying());
    CHECK_NEAR(energy - w.player().energy, 12.0f, 0.01f);
    // Only one blade at a time: pressing again while it flies does nothing.
    steps(w, 30);
    float before = w.player().energy;
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(w.player().energy >= before - 0.001f);
    float farthest = 0.0f;
    bool returned = false;
    for (int i = 0; i < 600 && b->flying(); ++i) {
        farthest = std::max(farthest, b->bladePos().x - start.x);
        returned = returned || b->returning();
        w.fixedUpdate(PlayerInput{}, kDt);
    }
    CHECK(returned);
    CHECK(!b->flying());
    CHECK(b->caught() == 1);
    CHECK(farthest > 240.0f && farthest < 285.0f);
    // The glowmite (2 HP) took one blow on the way out and one on the way back.
    CHECK(b->hitsOut() == 1);
    CHECK(b->hitsBack() == 1);
    CHECK(w.stats().kills == 1);
}

TEST_CASE("sanctuary powers: a wall sends the blade back early") {
    World env;
    LevelData l = flat({"boomerang"});
    addObject(l, "wall", "solid", {265, 700, 40, 100}, {{"material", "ruin_stone"}});
    GameWorld& w = env.make(l);
    steps(w, 40);
    w.player().facing = 1;
    w.fixedUpdate(press(Action::Special), kDt);
    auto* b = powerOf<BoomerangPower>(w);
    CHECK(b != nullptr);
    if (!b) return;
    float farthest = 0.0f;
    for (int i = 0; i < 600 && b->flying(); ++i) {
        farthest = std::max(farthest, b->bladePos().x);
        w.fixedUpdate(PlayerInput{}, kDt);
    }
    CHECK(b->caught() == 1);
    CHECK(farthest < 266.0f);
}

TEST_CASE("sanctuary powers: the boomerang brings back a shard and switches a receptor on") {
    World env;
    LevelData l = flat({"boomerang"});
    addObject(l, "shard_far", "shard", {365, 774, 12, 12});
    addObject(l, "rec", "receptor", {280, 770, 16, 30});
    GameWorld& w = env.make(l);
    std::set<std::string> events;
    w.eventObserver = [&](const std::string& source, const std::string& trigger) { events.insert(source + ":" + trigger); };
    steps(w, 40);
    CHECK(w.stats().shards == 0);
    w.player().facing = 1;
    w.fixedUpdate(press(Action::Special), kDt);
    steps(w, 400);
    CHECK(events.count("rec:activate") == 1);
    CHECK(w.stats().shards == 1);  // picked up by Nova when the blade came back
    CHECK(w.player().center().x < 200.0f);  // she never went to fetch it
}

TEST_CASE("sanctuary powers: the frost nova freezes enemies into platforms, then they thaw") {
    World env;
    GameWorld& w = env.make(flat({"frost_nova"}));
    steps(w, 40);
    Vec2 feet = w.player().feet();
    Enemy* e = w.spawnEnemy("bulwark", feet + Vec2{70, 0}, true, {}, 1.0f);
    Enemy* distant = w.spawnEnemy("glowmite", feet + Vec2{400, 0}, true, {}, 1.0f);
    steps(w, 2);
    CHECK(e != nullptr && distant != nullptr);
    if (!e || !distant) return;
    float energy = w.player().energy;
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK_NEAR(energy - w.player().energy, 30.0f, 0.01f);
    CHECK(e->frozen());
    CHECK(!distant->frozen());  // out of the 110 px radius
    // Frozen: it does not move.
    Rect box = e->box;
    steps(w, 60);
    CHECK(e->frozen());
    CHECK(std::abs(e->box.x - box.x) < 0.01f);
    // A platform: Nova dropped above it lands on its top.
    w.player().teleportTo({e->box.center().x, e->box.y - 30.0f});
    steps(w, 50);
    CHECK(w.player().grounded());
    CHECK(std::abs(w.player().box.bottom() - e->box.y) < 1.0f);
    // The recharge (6 s): a second nova now does nothing.
    float now = w.player().energy;
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(w.player().energy >= now - 0.001f);
    // Thawed after 1.5 s: the platform is gone with the ice.
    steps(w, 120);
    CHECK(!e->frozen());
    w.player().teleportTo({e->box.center().x, e->box.y - 30.0f});
    w.player().setInvulnerable(2.0f);
    steps(w, 40);
    CHECK(w.player().box.bottom() > e->box.y + 2.0f);
}

TEST_CASE("sanctuary powers: the frost nova only slows a boss and shatters the enemy shots in its radius") {
    World env;
    GameWorld& w = env.make(flat({"frost_nova"}));
    steps(w, 40);
    Vec2 c = w.player().center();
    Enemy* boss = w.spawnEnemy("thornwarden", w.player().feet() + Vec2{90, 0}, true, {}, 1.0f);
    Projectile inside, away;
    inside.pos = inside.prevPos = c + Vec2{60, -20};
    inside.vel = {-40, 0};
    inside.life = 10.0f;
    away.pos = away.prevPos = c + Vec2{300, -60};
    away.vel = {0, 0};
    away.life = 10.0f;
    w.spawnProjectile(inside);
    w.spawnProjectile(away);
    steps(w, 1);
    CHECK(w.projectiles().size() == 2);
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(boss != nullptr);
    if (boss) {
        CHECK(boss->slowed());
        CHECK(!boss->frozen());
    }
    auto* nova = powerOf<FrostNovaPower>(w);
    CHECK(nova != nullptr);
    if (nova) CHECK(nova->lastShattered() == 1);
    CHECK(w.projectiles().size() == 1);
}

TEST_CASE("sanctuary powers: the enemies aim at the decoy, and an attack breaks it") {
    World env;
    GameWorld& w = env.make(flat({"decoy"}));
    steps(w, 40);
    Enemy* e = w.spawnEnemy("glowmite", w.player().feet() + Vec2{100, 0}, true, {}, 1.0f);
    steps(w, 2);
    float energy = w.player().energy;
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK_NEAR(energy - w.player().energy, 25.0f, 0.01f);
    CHECK(w.decoyActive());
    Vec2 decoy = w.decoyPos();
    CHECK(distance(w.enemyAim(), decoy) < 0.01f);
    // Nova runs off to the left; the glowmite goes for the hologram, not for her.
    for (int i = 0; i < 90; ++i) w.fixedUpdate(hold(Action::MoveLeft, -1.0f), kDt);
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(distance(w.enemyAim(), decoy) < 0.01f || !w.decoyActive());
    bool broken = false;
    for (int i = 0; i < 300 && !broken; ++i) {
        w.fixedUpdate(PlayerInput{}, kDt);
        broken = w.decoysBroken() == 1;
    }
    CHECK(broken);
    CHECK(!w.decoyActive());
    CHECK(w.stats().damageTaken == 0);
    CHECK(distance(w.enemyAim(), w.player().center()) < 0.01f);  // back to Nova
}

TEST_CASE("sanctuary powers: an enemy shot breaks the decoy; untouched it lasts 3 s") {
    World env;
    GameWorld& w = env.make(flat({"decoy"}));
    steps(w, 40);
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(w.decoyActive());
    steps(w, 2 * 120);
    CHECK(w.decoyActive());
    steps(w, 130);
    CHECK(!w.decoyActive());
    CHECK(w.decoysBroken() == 0);
    // Recharge 8 s, then a new decoy; an enemy shot reaching it breaks it.
    steps(w, 5 * 120);
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(w.decoyActive());
    w.player().teleportTo(w.player().feet() + Vec2{-120, 0});  // Nova left her hologram behind
    Projectile shot;
    shot.pos = shot.prevPos = w.decoyPos() + Vec2{40, 0};
    shot.vel = {-200, 0};
    shot.life = 3.0f;
    w.spawnProjectile(shot);
    steps(w, 40);
    CHECK(w.decoysBroken() == 1);
    CHECK(!w.decoyActive());
}

TEST_CASE("sanctuary powers: the recall beacon brings Nova back within 5 s, not after") {
    World env;
    GameWorld& w = env.make(flat({"recall"}));
    steps(w, 40);
    auto* r = powerOf<RecallPower>(w);
    CHECK(r != nullptr);
    if (!r) return;
    Vec2 home = w.player().feet();
    float energy = w.player().energy;
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(r->beaconPlaced());
    CHECK_NEAR(w.player().energy, energy, 0.01f);  // placing it is free
    for (int i = 0; i < 120; ++i) w.fixedUpdate(hold(Action::MoveRight, 1.0f), kDt);
    CHECK(w.player().feet().x > home.x + 100.0f);
    CHECK(std::abs(w.player().vel.x) > 50.0f);
    energy = w.player().energy;
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(r->warps() == 1);
    CHECK(!r->beaconPlaced());
    CHECK(std::abs(w.player().feet().x - home.x) < 0.5f);
    CHECK(std::abs(w.player().vel.x) < 0.01f);
    CHECK(w.player().invulnerable());
    CHECK_NEAR(energy - w.player().energy, 15.0f, 0.01f);
    // A beacon left more than 5 s: the next press drops a new one instead.
    steps(w, 60);
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(r->beaconPlaced());
    Vec2 second = r->beaconFeet();
    steps(w, 60);
    for (int i = 0; i < 120; ++i) w.fixedUpdate(hold(Action::MoveRight, 1.0f), kDt);
    steps(w, 5 * 120 - 180 + 10);  // the window is over
    CHECK(!r->beaconPlaced());
    Vec2 here = w.player().feet();
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(r->warps() == 1);
    CHECK(std::abs(w.player().feet().x - here.x) < 2.0f);
    CHECK(r->beaconPlaced());
    CHECK(distance(r->beaconFeet(), second) > 50.0f);
    // Not enough energy to come back: nothing happens, the beacon stays.
    w.player().energy = 5.0f;
    steps(w, 40);
    w.fixedUpdate(press(Action::Special), kDt);
    CHECK(r->warps() == 1);
    CHECK(r->beaconPlaced());
}

TEST_CASE("sanctuary powers: two identical runs with each new power stay identical") {
    for (const char* power : {"boomerang", "frost_nova", "decoy", "recall"}) {
        std::vector<uint64_t> digests[2];
        for (int run = 0; run < 2; ++run) {
            World env;
            LevelData l = flat({power, "dash"});
            l.id = "test_sanctuary_det";
            addObject(l, "e1", "enemy", {300, 788, 18, 12}, {{"enemy", "glowmite"}, {"patrol", 40}});
            addObject(l, "e2", "enemy", {420, 776, 16, 24}, {{"enemy", "bulwark"}, {"patrol", 30}});
            addObject(l, "e3", "enemy", {560, 780, 16, 20}, {{"enemy", "sporeflower"}});
            addObject(l, "s1", "shard", {380, 770, 12, 12});
            WorldServices s{env.res, env.audio, env.content, nullptr, 0.0f, 1.0f};
            GameWorld w(s, WorldMode::Play);
            w.silent = true;
            w.load(l);
            w.seedGameplay(levelSeed(l.id));
            Random rng(0x219);
            InputFrame prev;
            for (int i = 0; i < 1800; ++i) {
                InputFrame f;
                int dir = (i / 90) % 3 == 2 ? -1 : 1;
                f.held = static_cast<uint16_t>(1u << static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft));
                f.moveX = static_cast<int8_t>(dir * 127);
                if (rng.chance(0.03f)) f.held |= 1u << static_cast<int>(Action::Special);
                if (rng.chance(0.05f)) f.held |= 1u << static_cast<int>(Action::Jump);
                if (rng.chance(0.02f)) f.held |= 1u << static_cast<int>(Action::Attack);
                f.pressed = static_cast<uint16_t>(f.held & ~prev.held);
                f.released = static_cast<uint16_t>(prev.held & ~f.held);
                prev = f;
                stepWorld(w, f, kSimDt);
                if (i % 60 == 59) digests[run].push_back(simulationStateHash(w));
            }
            CHECK(w.stats().powersUsed.count(power) == 1);
        }
        CHECK(digests[0] == digests[1]);
        if (digests[0] != digests[1]) std::printf("  désynchronisation : %s\n", power);
    }
}
