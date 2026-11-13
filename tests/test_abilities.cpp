// v2 abilities and mechanics: glide, updraft, triple jump, ground pound, charged shot,
// grapple, crystal hearts, ice friction, slowing hits and exploding projectiles.

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
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
        w->load(level);
        return *w;
    }
};

LevelData flat(std::vector<std::string> powers = {}) {
    LevelData l = LevelData::makeEmpty("test_abilities", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(powers);
    return l;
}

void addObject(LevelData& l, const std::string& type, Rect r, Json props = Json::object()) {
    LevelObject o;
    o.id = "t" + std::to_string(l.objects.size() + 1);
    o.type = type;
    o.layer = "gameplay";
    o.rect = r;
    o.props = std::move(props);
    l.objects.push_back(o);
}

PlayerInput input(std::initializer_list<Action> held, std::initializer_list<Action> pressed = {}) {
    PlayerInput in;
    for (Action a : held) {
        in.held[static_cast<int>(a)] = true;
        if (a == Action::MoveRight) in.move.x = 1;
        if (a == Action::MoveLeft) in.move.x = -1;
        if (a == Action::MoveDown) in.move.y = 1;
        if (a == Action::MoveUp) in.move.y = -1;
    }
    for (Action a : pressed) in.pressed[static_cast<int>(a)] = true;
    return in;
}

void settle(GameWorld& w, int steps = 40) {
    for (int i = 0; i < steps; ++i) w.fixedUpdate(PlayerInput{}, kDt);
}
}  // namespace

TEST_CASE("abilities: ten hearts and a crystal heart adds one") {
    World env;
    LevelData l = flat();
    addObject(l, "heart_container", {200, 770, 16, 16}, {{"hearts", 1}});
    GameWorld& w = env.make(l);
    settle(w);
    CHECK(w.player().maxHealth() == 10);
    CHECK(w.player().health == 10);
    for (int i = 0; i < 120 && w.player().maxHealth() == 10; ++i) w.fixedUpdate(input({Action::MoveRight}), kDt);
    CHECK(w.player().maxHealth() == 11);
    CHECK(w.player().health == 11);
}

TEST_CASE("abilities: glide caps the fall speed and updrafts lift") {
    World env;
    LevelData l = flat({"glide"});
    addObject(l, "updraft", {600, 300, 80, 500}, {{"strength", 230}, {"vent", true}});
    GameWorld& w = env.make(l);
    settle(w);
    CHECK(w.player().hasPower("glide"));
    // Free fall without gliding.
    w.player().teleportTo({165, 300});
    for (int i = 0; i < 70; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    float freeFall = w.player().vel.y;
    // Same fall holding jump: glide.
    w.player().teleportTo({165, 300});
    w.player().vel = {};
    for (int i = 0; i < 70; ++i) w.fixedUpdate(input({Action::Jump}), kDt);
    CHECK(w.player().gliding());
    CHECK(freeFall > 200.0f);
    CHECK(w.player().vel.y < 70.0f);
    // Inside the updraft a gliding player rises.
    w.player().teleportTo({640, 600});
    w.player().vel = {0, 40};
    float y0 = w.player().box.y;
    for (int i = 0; i < 60; ++i) w.fixedUpdate(input({Action::Jump}), kDt);
    CHECK(w.player().vel.y < 0.0f);
    CHECK(w.player().box.y < y0 - 20.0f);
}

TEST_CASE("abilities: triple jump gives a second air jump") {
    World env;
    GameWorld& w = env.make(flat({"triple_jump"}));
    settle(w);
    CHECK(w.player().airJumpCount() == 2);
    w.fixedUpdate(input({Action::Jump}, {Action::Jump}), kDt);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    w.fixedUpdate(input({Action::Jump}, {Action::Jump}), kDt);
    CHECK(w.player().airJumpsLeft() == 1);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    w.fixedUpdate(input({Action::Jump}, {Action::Jump}), kDt);
    CHECK(w.player().airJumpsLeft() == 0);
    CHECK(w.player().vel.y < 0.0f);
}

TEST_CASE("abilities: ground pound breaks a cracked floor and hits enemies") {
    World env;
    LevelData l = flat({"ground_pound"});
    addObject(l, "power_gate", {120, 640, 96, 16}, {{"power", "ground_pound"}});
    GameWorld& w = env.make(l);
    settle(w);
    Enemy* e = w.spawnEnemy("glowmite", {200, 800}, true);
    CHECK(e != nullptr);
    w.player().teleportTo({165, 560});
    for (int i = 0; i < 120; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    CHECK(w.player().grounded());
    CHECK(w.player().feet().y < 641.0f);  // standing on the cracked floor
    w.player().launch({0, -260}, true);
    for (int i = 0; i < 20; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    w.fixedUpdate(input({Action::MoveDown, Action::Attack}, {Action::Attack}), kDt);
    CHECK(w.player().movementPower() != nullptr);
    for (int i = 0; i < 120; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    CHECK(w.player().feet().y > 795.0f);  // fell through to the ground below
    CHECK(!e->alive() || e->hp() < e->def().hp);
}

TEST_CASE("abilities: charged shot fires after holding attack") {
    World env;
    GameWorld& w = env.make(flat({"charge_shot"}));
    settle(w);
    float energy = w.player().energy;
    w.fixedUpdate(input({Action::Attack}, {Action::Attack}), kDt);
    for (int i = 0; i < 130; ++i) w.fixedUpdate(input({Action::Attack}), kDt);
    bool charged = false;
    for (const auto& p : w.projectiles()) charged |= p.clip == "charge_shot";
    CHECK(!charged);  // nothing while holding
    w.fixedUpdate(PlayerInput{}, kDt);
    w.fixedUpdate(PlayerInput{}, kDt);
    for (const auto& p : w.projectiles()) charged |= p.clip == "charge_shot" && p.fromPlayer;
    CHECK(charged);
    CHECK(w.player().energy < energy);
    // A quick tap only performs the melee attack.
    size_t before = w.projectiles().size();
    for (int i = 0; i < 60; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    w.fixedUpdate(input({Action::Attack}, {Action::Attack}), kDt);
    w.fixedUpdate(PlayerInput{}, kDt);
    size_t shots = 0;
    for (const auto& p : w.projectiles()) shots += p.clip == "charge_shot" ? 1 : 0;
    CHECK(shots <= before);
}

TEST_CASE("abilities: grapple pulls the player to an anchor") {
    World env;
    LevelData l = flat({"grapple"});
    addObject(l, "grapple_point", {268, 668, 24, 24});
    GameWorld& w = env.make(l);
    settle(w);
    Vec2 start = w.player().center();
    w.fixedUpdate(input({Action::Interact}, {Action::Interact}), kDt);
    CHECK(w.player().movementPower() != nullptr);
    float minDist = 1e9f;
    for (int i = 0; i < 90; ++i) {
        w.fixedUpdate(PlayerInput{}, kDt);
        minDist = std::min(minDist, distance(w.player().center(), {280, 680}));
    }
    CHECK(minDist < 24.0f);
    CHECK(w.player().center().y < start.y - 60.0f || !w.player().grounded());
}

TEST_CASE("abilities: ice is slippery and frost slows") {
    World env;
    GameWorld& w = env.make(flat());
    settle(w);
    auto runAndStop = [&](float friction) {
        w.player().teleportTo({165, 800});
        settle(w, 20);
        if (Collider* g = w.physics().get(w.player().groundCollider())) g->friction = friction;
        for (int i = 0; i < 90; ++i) w.fixedUpdate(input({Action::MoveRight}), kDt);
        float x0 = w.player().box.x;
        for (int i = 0; i < 120; ++i) w.fixedUpdate(PlayerInput{}, kDt);
        return w.player().box.x - x0;
    };
    float normal = runAndStop(1.0f);
    float ice = runAndStop(0.25f);
    CHECK(ice > normal * 2.0f);
    if (Collider* g = w.physics().get(w.player().groundCollider())) g->friction = 1.0f;
    // A slowing hit (ice attacks, webs) reduces the run speed for a while.
    DamageInfo d;
    d.amount = 1;
    d.slow = 0.5f;
    d.slowDuration = 2.0f;
    d.source = w.player().center() + Vec2{-20, 0};
    w.player().setInvulnerable(0.0f);
    CHECK(w.damagePlayer(d));
    CHECK(w.player().slowed());
    for (int i = 0; i < 160; ++i) w.fixedUpdate(input({Action::MoveRight}), kDt);
    CHECK(std::abs(w.player().vel.x) < w.player().config().maxRunSpeed * 0.6f);
}

TEST_CASE("abilities: bombs explode on impact") {
    World env;
    GameWorld& w = env.make(flat());
    settle(w);
    int hp = w.player().health;
    Projectile p;
    p.pos = p.prevPos = w.player().center() + Vec2{0, -60};
    p.vel = {0, 40};
    p.gravity = 600;
    p.damage = 2;
    p.clip = "bomb";
    p.explosionRadius = 30;
    w.spawnProjectile(p);
    for (int i = 0; i < 90; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    CHECK(w.player().health == hp - 2);
}

TEST_CASE("arenas: starting inside a combat zone never duplicates its enemies") {
    World env;
    LevelData l = flat();
    addObject(l, "enemy", {300, 790, 14, 10}, {{"enemy", "glowmite"}, {"dormant", true}, {"patrol", 32}});
    std::string enemyId = l.objects.back().id;
    addObject(l, "combat_zone", {0, 600, 600, 200}, {{"waves", enemyId}, {"doors", ""}, {"lockCamera", false}, {"announce", false}});
    GameWorld& w = env.make(l);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, kDt);
    int copies = 0;
    for (const auto& e : w.enemies()) copies += e->objectId() == enemyId ? 1 : 0;
    CHECK(copies == 1);
    for (const auto& e : w.enemies())
        if (e->objectId() == enemyId) CHECK(!e->dormant());
}

TEST_CASE("hazards: side spikes push the player away from their base") {
    for (int rot : {90, 270}) {
        World env;
        LevelData l = flat();
        // Vertical strip of spikes standing in the open, pointing right (90) or left (270).
        addObject(l, "spikes", {300, 736, 16, 64}, {{"damage", 1}});
        l.objects.back().rotation = static_cast<float>(rot);
        GameWorld& w = env.make(l);
        settle(w);
        w.player().teleportTo({308, 800});
        bool pushed = false;
        for (int i = 0; i < 10 && !pushed; ++i) {
            int hp = w.player().health;
            w.fixedUpdate(PlayerInput{}, kDt);
            if (w.player().health < hp) {
                pushed = true;
                CHECK(w.player().vel.x * (rot == 90 ? 1.0f : -1.0f) > 100.0f);
            }
        }
        CHECK(pushed);
    }
}
