// Traversal checks of the Glacior levels (glacior_01..03): every mandatory move of each level is
// achievable with the intended powers, arenas lock and open, bosses wake / change phase / die,
// and the exit completes the level. Coordinates are in tiles (16 px), y grows downwards.

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float kT = 16.0f;
constexpr float kDT = 1.0f / 120.0f;

using Acts = std::initializer_list<Action>;

PlayerInput gin(int dir, Acts pressed = {}, Acts held = {}) {
    PlayerInput in;
    if (dir > 0) {
        in.held[static_cast<int>(Action::MoveRight)] = true;
        in.move.x = 1.0f;
    } else if (dir < 0) {
        in.held[static_cast<int>(Action::MoveLeft)] = true;
        in.move.x = -1.0f;
    }
    for (Action a : pressed) in.pressed[static_cast<int>(a)] = in.held[static_cast<int>(a)] = true;
    for (Action a : held) in.held[static_cast<int>(a)] = true;
    if (in.held[static_cast<int>(Action::MoveDown)]) in.move.y = 1.0f;
    if (in.held[static_cast<int>(Action::MoveUp)]) in.move.y = -1.0f;
    return in;
}

/// A level loaded without its intro, the player placed at (tx, ty) (feet, tiles).
struct GRun {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    GRun(const char* level, float tx, float ty, std::initializer_list<const char*> powers = {},
         bool invulnerable = false) {
        content.reload();
        res.init(nullptr, &audio);
        auto l = content.loadLevel(level);
        CHECK(l.has_value());
        l->intro.steps.clear();
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*l);
        w->autoPowerChoice = 1;  // full power slots: take the tablet, leave the first power
        // The zone tests start anywhere: they get the powers of the level's stations (2.3 power slots).
        for (const auto& so : w->level().objects)
            if (so.type == "power_unlock" && so.id.rfind("station_", 0) == 0)
                if (const PowerDefinition* sd = w->content().power(so.propStr("power"))) w->player().grantPower(*sd);
        w->startAt({tx * kT, ty * kT});
        for (const char* p : powers) w->player().grantPower(*content.power(p));
        god = invulnerable;
        settle(30);
    }
    bool god = false;
    Player& p() { return w->player(); }
    float x() { return p().box.center().x / kT; }
    float feet() { return p().box.bottom() / kT; }
    void settle(int n) {
        for (int i = 0; i < n; ++i) step(PlayerInput{});
    }
    int stepCount = 0;
    void step(const PlayerInput& in) {
        if (god) p().setInvulnerable(1.0f);
        w->fixedUpdate(in, kDT);
        static const char* tr = std::getenv("GLA_TRACE");
        if (tr && ++stepCount % std::atoi(tr) == 0)
            std::printf("    t=%d x=%.2f feet=%.2f v=(%.0f,%.0f) st=%d g=%d water=%d mp=%d hp=%d\n", stepCount, x(), feet(),
                        p().vel.x, p().vel.y, static_cast<int>(p().state()), p().grounded(), p().inWater,
                        p().movementPower() != nullptr, p().health);
    }
    void play(int steps, const std::function<PlayerInput(Player&, int)>& ctrl) {
        for (int i = 0; i < steps; ++i) step(ctrl(p(), i));
    }
    /// Walks towards x (tiles) on the ground; returns false if it never gets there.
    bool walkTo(float tx, int maxSteps = 1200) {
        for (int i = 0; i < maxSteps; ++i) {
            float dx = tx - x();
            if (std::abs(dx) < 0.25f) {
                for (int k = 0; k < 40 && std::abs(p().vel.x) > 5.0f; ++k) step(PlayerInput{});
                return true;
            }
            step(gin(dx > 0 ? 1 : -1));
        }
        return false;
    }
    /// Multi-jump: jump, then extra jumps near each apex while drifting `dir` once `driftAfter`
    /// jumps were made; holds the direction until landing (or `maxSteps`).
    void multiJump(int jumps, int dir, int driftAfter = 0, int maxSteps = 400, bool glide = false) {
        int done = 0;
        for (int i = 0; i < maxSteps; ++i) {
            int d = done >= driftAfter ? dir : 0;
            if (done == 0) {
                step(gin(d, {Action::Jump}, {Action::Jump}));
                done = 1;
                continue;
            }
            if (done < jumps && p().vel.y > -30.0f) {
                step(gin(d, {Action::Jump}, {Action::Jump}));
                ++done;
                continue;
            }
            bool hold = glide || p().vel.y < 0.0f;
            step(gin(d, {}, hold ? Acts{Action::Jump} : Acts{}));
            if (std::getenv("GLA_TRACE") && i % 6 == 0) std::printf("   mj i=%d done=%d x=%.2f feet=%.2f v=(%.0f,%.0f) st=%d\n", i, done, x(), feet(), p().vel.x, p().vel.y, (int)p().state());
            if (done >= jumps && p().grounded() && i > 20) break;
        }
        settle(10);
    }
    int countAliveAwake() {
        int n = 0;
        for (auto& e : w->enemies()) n += e->alive() && !e->dormant() ? 1 : 0;
        return n;
    }
    void killAwake() {
        DamageInfo d;
        d.amount = 200;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        for (auto& e : w->enemies())
            if (e->alive() && !e->dormant() && !e->def().isBoss) {
                d.source = e->center();
                e->takeDamage(*w, d);
            }
    }
};

/// True when the grapple would pick an anchor ahead of the player (same scoring as GrapplePower).
bool grappleAhead(GameWorld& w, int dir) {
    const Player& p = w.player();
    const LevelEntity* best = nullptr;
    float bestScore = 1e9f;
    Vec2 from = p.center() + Vec2{0, -4};
    for (const auto& e : w.entities()) {
        if (!e->active || e->type() != "grapple_point") continue;
        Vec2 d = e->box.center() - from;
        float dist = d.length();
        if (dist > 170.0f || dist < 18.0f || d.y > 40.0f) continue;
        if (w.physics().raycast(from, e->box.center(), false, ColliderTag::EnemyOnly)) continue;
        float score = dist - (signf(d.x) == p.facing ? 40.0f : 0.0f) + (d.y > 0 ? 30.0f : 0.0f);
        if (score < bestScore) {
            bestScore = score;
            best = e.get();
        }
    }
    return best && (best->box.center().x - p.center().x) * static_cast<float>(dir) > 20.0f;
}

/// Grapple chain bot: jumps, then grapples every anchor ahead until landing past `minX`.
void grappleChain(GRun& r, int dir, float minX, int steps = 900) {
    r.play(steps, [&](Player& p, int i) {
        if (i == 0) return gin(dir, {Action::Jump}, {Action::Jump});
        if (p.grounded() && i > 30 && (p.box.center().x / kT - minX) * dir > 0) return PlayerInput{};
        bool fire = !p.grounded() && p.movementPower() == nullptr && grappleAhead(*r.w, dir);
        return gin(dir, fire ? Acts{Action::Interact} : Acts{}, p.vel.y > 0 ? Acts{Action::Jump} : Acts{});
    });
}

/// Jumps into an updraft column centred on `colX`, glides up until above `topY` (the target ledge
/// top), then drifts to the ledge (lx0..lx1) and drops on it.
void rideUpdraft(GRun& r, float colX, float lx0, float lx1, float topY, int steps = 1200) {
    int dirToCol = colX > r.x() ? 1 : -1;
    r.play(steps, [&](Player& p, int i) {
        float x = p.box.center().x / kT, feet = p.box.bottom() / kT;
        if (i == 0) return gin(dirToCol, {Action::Jump}, {Action::Jump});
        if (p.grounded() && i > 30) return PlayerInput{};
        if (feet > topY - 2.5f) {  // still climbing: stay in the column
            int d = std::abs(x - colX) < 0.4f ? 0 : (x < colX ? 1 : -1);
            return gin(d, {}, {Action::Jump});
        }
        if (x >= lx0 && x <= lx1) return PlayerInput{};  // above the ledge: drop
        return gin(x < lx0 ? 1 : -1, {}, {Action::Jump});
    });
}

/// Clears an arena (combat zone entered at (enterX, y)) wave by wave, then walks right past `exitX`.
void clearArena(GRun& r, float exitX, int waves) {
    for (int wave = 0; wave < waves + 2; ++wave) {
        r.killAwake();
        r.play(200, [&](Player&, int) { return PlayerInput{}; });
    }
    r.god = true;
    r.play(900, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > exitX);
}

/// Boss fight with scripted damage: wakes, goes through every phase, dies, the way opens.
void bossFight(GRun& r, float exitX, int phases) {
    r.god = true;
    r.play(300, [&](Player&, int) { return gin(1); });
    Enemy* boss = r.w->activeBoss();
    CHECK(boss != nullptr);
    if (!boss) return;
    CHECK(boss->def().isBoss);
    int maxPhase = 0;
    for (int hit = 0; hit < 400 && boss && boss->alive(); ++hit) {
        DamageInfo d;
        d.amount = 2;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        d.source = boss->center() - Vec2{30, 0};
        boss->takeDamage(*r.w, d);
        r.play(24, [&](Player& p, int) {
            p.health = std::max(p.health, 5);
            return PlayerInput{};
        });
        maxPhase = std::max(maxPhase, boss->phaseIndex());
        boss = r.w->activeBoss();
    }
    CHECK(maxPhase == phases - 1);
    CHECK(r.w->activeBoss() == nullptr);
    r.killAwake();  // summoned minions
    r.play(1500, [&](Player& p, int) {
        p.health = std::max(p.health, 5);
        return gin(1);
    });
    CHECK(r.x() > exitX);
}

void reachExit(const char* level, float tx, float ty) {
    GRun r(level, tx, ty);
    CHECK(r.w->state() == WorldState::Playing);
    r.god = true;
    r.play(600, [&](Player&, int) { return gin(1); });
    CHECK(r.w->state() == WorldState::Outro || r.w->state() == WorldState::Completed);
    r.play(2400, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.w->state() == WorldState::Completed);
    CHECK(!r.w->timer().running());
}
}  // namespace

// =====================================================================================
// glacior_01 — Les Plaines de Givre
// =====================================================================================

TEST_CASE("glacior_01: ground pound opens the snow cave, the relic is collected, way back up") {
    GRun r("glacior_01", 48, 50);
    r.god = true;
    r.killAwake();
    r.walkTo(51);
    r.play(30, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Jump}, {Action::Jump}) : gin(0, {}, {Action::Jump}); });
    r.step(gin(0, {Action::Attack}, {Action::MoveDown, Action::Attack}));
    r.play(120, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.feet() > 54.5f);  // fell into the cave
    r.walkTo(59.5f);
    CHECK(r.w->stats().relics == 1);
    r.walkTo(51);
    r.multiJump(2, -1, 2);
    CHECK(r.feet() <= 50.05f);
    CHECK(r.p().alive());
}

TEST_CASE("glacior_01: slippery floe crossing over the first chasm") {
    GRun r("glacior_01", 74, 47);
    r.play(400, [&](Player& p, int) {
        float x = p.box.center().x / kT;
        bool edge = p.grounded() && ((x > 75.3f && x < 76.5f) || (x > 83.6f && x < 85.0f));
        return gin(1, edge ? Acts{Action::Jump} : Acts{}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 89.0f);
    CHECK(r.feet() <= 48.05f);
}

TEST_CASE("glacior_01: the tutorial ledge is out of reach of a double jump, the tablet gives the triple jump") {
    {
        GRun r("glacior_01", 134, 48);
        CHECK(!r.p().hasPower("triple_jump"));
        r.multiJump(2, 0);
        CHECK(r.feet() > 47.5f);  // fell back to the floor
    }
    GRun r("glacior_01", 114, 48);
    r.walkTo(126);
    CHECK(r.p().hasPower("triple_jump"));
    r.walkTo(134);
    r.multiJump(3, 1, 3);
    CHECK(r.feet() <= 40.05f);
    r.walkTo(146);
    CHECK(r.feet() <= 40.05f);
    CHECK(r.p().health == r.p().maxHealth());  // the spiky face was not touched
}

TEST_CASE("glacior_01: cliff B is reached by a triple jump over the pit, and the pit has a way out") {
    GRun r("glacior_01", 147, 40, {"triple_jump"});
    r.walkTo(150.5f);
    r.multiJump(3, 1, 2);
    CHECK(r.feet() <= 32.05f);
    CHECK(r.x() > 153.0f);
    // fall into the pit and climb back on the recovery ledge
    GRun r2("glacior_01", 154, 48, {"triple_jump"});
    r2.walkTo(151);
    r2.multiJump(3, -1, 3);
    CHECK(r2.feet() <= 40.05f);
}

TEST_CASE("glacior_01: the windy gorge is crossed by gliding") {
    GRun r("glacior_01", 168, 32);
    r.walkTo(171);
    r.play(900, [&](Player& p, int i) {
        if (i == 0) return gin(1, {Action::Jump}, {Action::Jump});
        if (p.grounded() && i > 30) return PlayerInput{};
        return gin(1, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 207.0f);
    CHECK(r.feet() <= 40.05f);
}

TEST_CASE("glacior_01: riding the first wind column reaches the hidden relic") {
    GRun r("glacior_01", 170, 32);
    rideUpdraft(r, 179.5f, 172.0f, 174.0f, 12.0f);
    CHECK(r.p().alive());
    CHECK(r.feet() <= 12.05f);
    r.walkTo(172.5f);
    CHECK(r.w->stats().relics == 1);
}

TEST_CASE("glacior_01: the lake is crossed with the grapple chain") {
    GRun r("glacior_01", 222, 46);
    r.god = true;
    r.walkTo(223.5f);
    grappleChain(r, 1, 262.0f);
    CHECK(r.p().alive());
    CHECK(r.x() > 262.0f);
    CHECK(r.feet() <= 52.05f);
}

TEST_CASE("glacior_01: a swimmer can leave the icy lake by the beach, the heart heals") {
    GRun r("glacior_01", 250, 61);
    r.play(240, [&](Player&, int) { return gin(-1); });
    CHECK(r.p().maxHealth() == 11);  // crystal heart in the sunken ruin
    r.play(900, [&](Player& p, int i) {
        if (p.grounded() && p.box.center().x / kT > 265.0f) return PlayerInput{};
        return gin(1, i % 20 == 0 ? Acts{Action::Jump} : Acts{}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 264.0f);
    CHECK(r.feet() <= 52.05f);
}

TEST_CASE("glacior_01: the outpost arena locks, then opens when its three waves are cleared") {
    GRun r("glacior_01", 279, 50);
    r.god = true;
    r.play(150, [&](Player&, int) { return gin(1); });
    CHECK(r.countAliveAwake() > 0);
    clearArena(r, 321.0f, 3);
}

TEST_CASE("glacior_01: the Escalier de givre needs three triple jumps") {
    GRun r("glacior_01", 336, 50, {"triple_jump"});
    r.killAwake();  // enemies are not the point here: the spiky face must not be touched
    r.walkTo(344.5f);
    r.multiJump(3, 0);
    CHECK(r.feet() <= 42.05f);
    r.walkTo(343.5f);
    r.multiJump(3, 0);
    CHECK(r.feet() <= 34.05f);
    r.walkTo(345.5f);
    r.multiJump(3, 1, 3);
    CHECK(r.feet() <= 26.05f);
    CHECK(r.x() > 348.0f);
    CHECK(r.p().health == r.p().maxHealth());
}

TEST_CASE("glacior_01: grapple chain over the blizzard gorge") {
    GRun r("glacior_01", 366, 26);
    r.god = true;
    r.walkTo(369.5f);
    grappleChain(r, 1, 398.0f);
    CHECK(r.p().alive());
    CHECK(r.x() > 398.0f);
    CHECK(r.feet() <= 32.05f);
}

TEST_CASE("glacior_01: the bastion phase barrier is crossed by teleport") {
    GRun r("glacior_01", 407, 32);
    r.god = true;
    r.killAwake();
    r.play(120, [&](Player&, int) { return gin(1); });
    CHECK(r.p().box.right() <= 410.0f * kT + 1.0f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Teleport}) : PlayerInput{}; });
    r.settle(30);
    CHECK(r.x() > 411.0f);
}

TEST_CASE("glacior_01: the third relic waits on top of the bastion wind column") {
    GRun r("glacior_01", 418, 32);
    rideUpdraft(r, 421.5f, 427.0f, 430.0f, 8.0f);
    CHECK(r.feet() <= 8.05f);
    r.walkTo(431);
    CHECK(r.w->stats().relics == 1);
}

TEST_CASE("glacior_01: the slippery steps lead down to the boss antechamber") {
    GRun r("glacior_01", 424, 35);
    r.god = true;
    r.killAwake();
    r.play(700, [&](Player& p, int) {
        if (p.box.center().x / kT > 448.0f) return PlayerInput{};
        return gin(1);
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 446.0f);
    CHECK(r.feet() <= 52.05f);
}

TEST_CASE("glacior_01: the Cryowyrm wakes, goes through its phases, dies and opens the way") {
    GRun r("glacior_01", 456, 52);
    bossFight(r, 503.0f, 3);
}

TEST_CASE("glacior_01: reaching the exit plays the outro and completes the level") { reachExit("glacior_01", 516, 46); }

// =====================================================================================
// glacior_02 — Les Grottes de Cristal
// =====================================================================================

TEST_CASE("glacior_02: a single jump clears the pit under the icicle ceiling without damage") {
    GRun r("glacior_02", 43, 44);
    r.killAwake();
    r.walkTo(44.5f);
    r.play(400, [&](Player& p, int i) {
        float x = p.box.center().x / kT;
        if (x > 52.0f && p.grounded()) return PlayerInput{};
        return gin(1, (p.grounded() && x > 45.4f && i > 2) ? Acts{Action::Jump} : Acts{}, {Action::Jump});
    });
    CHECK(r.x() > 50.5f);
    CHECK(r.p().health == r.p().maxHealth());
    // a double jump there hits the icicles
    GRun r2("glacior_02", 48, 44);
    r2.multiJump(2, 0);
    CHECK(r2.p().health < r2.p().maxHealth());
}

TEST_CASE("glacior_02: diving under the rock curtain leads to the far bank") {
    GRun r("glacior_02", 61, 44);
    r.killAwake();
    r.play(1500, [&](Player& p, int i) {
        float x = p.box.center().x / kT;
        if (x > 84.0f && p.grounded()) return PlayerInput{};
        bool up = x > 72.5f && i % 16 == 0;  // swim up once past the curtain
        return gin(1, up ? Acts{Action::Jump} : Acts{}, x > 72.5f ? Acts{Action::Jump} : Acts{Action::MoveDown});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 83.0f);
    CHECK(r.feet() <= 48.05f);
}

TEST_CASE("glacior_02: the underground lake is crossed by the grapple anchors") {
    GRun r("glacior_02", 97, 48);
    r.god = true;
    r.killAwake();
    r.walkTo(99.3f);
    grappleChain(r, 1, 172.5f, 2000);
    CHECK(r.x() > 172.0f);
    CHECK(r.feet() <= 50.05f);
}

TEST_CASE("glacior_02: swimmers climb onto the flush islands") {
    GRun r("glacior_02", 119, 60);
    r.killAwake();
    bool landed = false;
    r.play(900, [&](Player& p, int i) {
        landed = landed || (p.grounded() && !p.inWater);
        if (landed) return PlayerInput{};
        return gin(1, i % 18 == 0 ? Acts{Action::Jump} : Acts{}, p.inWater ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.p().grounded());
    CHECK(r.feet() <= 50.05f);
}

TEST_CASE("glacior_02: the sunken heart is reached by teleporting through the phase barrier") {
    GRun r("glacior_02", 126, 70);
    r.killAwake();
    r.play(60, [&](Player&, int) { return gin(-1); });
    CHECK(r.x() > 124.0f);  // blocked by the barrier
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(-1, {Action::Teleport}) : PlayerInput{}; });
    r.play(200, [&](Player&, int) { return gin(-1); });
    CHECK(r.p().maxHealth() == 11);
    r.settle(200);  // teleport cooldown and energy
    r.walkTo(121.6f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Teleport}) : PlayerInput{}; });
    r.settle(10);
    CHECK(r.x() > 123.5f);
}

TEST_CASE("glacior_02: the grapple well leads up to the Salle des Echos") {
    GRun r("glacior_02", 194, 64);
    r.god = true;
    r.killAwake();
    r.play(1800, [&](Player& p, int i) {
        float x = p.box.center().x / kT;
        if (p.grounded() && x > 208.0f) return PlayerInput{};
        if (p.grounded() && i % 40 == 0) return gin(0, {Action::Jump}, {Action::Jump});
        float feet = p.box.bottom() / kT;
        if (feet < 22.2f) return gin(1);
        if (!p.grounded() && p.movementPower() == nullptr && feet < 27.0f && p.vel.y > -30.0f && p.airJumpsLeft() > 0)
            return gin(1, {Action::Jump}, {Action::Jump});
        bool fire = !p.grounded() && p.movementPower() == nullptr && p.vel.y > -80.0f;
        return gin(0, fire ? Acts{Action::Interact} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.x() > 208.0f);
    CHECK(r.feet() <= 22.05f);
}

TEST_CASE("glacior_02: the Salle des Echos locks, then opens after three waves") {
    GRun r("glacior_02", 208, 22);
    r.god = true;
    r.play(150, [&](Player&, int) { return gin(1); });
    CHECK(r.countAliveAwake() > 0);
    clearArena(r, 249.0f, 3);
}

TEST_CASE("glacior_02: the drop shaft lands safely in the gallery, the niche hides a relic") {
    GRun r("glacior_02", 249, 22);
    r.play(1000, [&](Player& p, int i) {
        float feet = p.box.bottom() / kT;
        if (p.grounded() && feet > 30.0f && i > 30) return PlayerInput{};
        float x = p.box.center().x / kT;
        int dir = feet < 35.3f ? (x < 256.0f ? 1 : 0) : (feet < 37.8f ? -1 : 0);
        return gin(dir, {}, {Action::Jump});
    });
    CHECK(r.w->stats().relics == 1);
    r.play(600, [&](Player& p, int i) {
        if (p.grounded() && i > 30 && p.box.bottom() / kT > 50.0f) return PlayerInput{};
        return gin(1);
    });
    CHECK(r.p().alive());
    CHECK(r.feet() <= 56.05f);
}

TEST_CASE("glacior_02: the crumbling bridge under the icicles, then the dash wall") {
    GRun r("glacior_02", 268, 56);
    r.killAwake();
    r.walkTo(266.0f);
    r.play(700, [&](Player& p, int) {
        float x = p.box.center().x / kT;
        if (x > 293.0f && p.grounded()) return PlayerInput{};
        bool edge = false;
        for (float t : {269.5f, 276.5f, 284.5f}) edge |= p.grounded() && x > t && x < t + 0.5f;
        return gin(1, edge ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 287.5f);
    CHECK(r.p().health == r.p().maxHealth());
    r.killAwake();
    r.walkTo(300);
    r.play(120, [&](Player&, int) { return gin(1); });
    CHECK(r.x() < 302.0f);
    r.play(40, [&](Player&, int i) { return gin(1, i == 0 ? Acts{Action::Dash} : Acts{}); });
    r.play(120, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 304.0f);
}

TEST_CASE("glacior_02: the hall of reflections: grapple across, the receptor opens the door") {
    GRun r("glacior_02", 307, 56);
    r.god = true;
    r.killAwake();
    r.walkTo(309.5f);
    grappleChain(r, 1, 336.5f);
    CHECK(r.x() > 336.0f);
    r.killAwake();
    r.walkTo(337.5f);
    r.play(80, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Wave}) : PlayerInput{}; });
    r.play(300, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 345.0f);
}

TEST_CASE("glacior_02: the plasma alcove relic is reached with the grapple and the shield") {
    GRun r("glacior_02", 307, 56);
    r.god = true;
    r.killAwake();
    r.walkTo(309.5f);
    bool reached = false;
    r.play(1200, [&](Player& p, int i) {
        Vec2 c = p.center() / kT;
        if (i == 0) return gin(1, {Action::Jump}, {Action::Jump});
        if (std::abs(c.x - 332.0f) < 1.0f && std::abs(c.y - 39.0f) < 1.2f) reached = true;
        if (p.grounded()) return PlayerInput{};
        if (reached) {  // released under the ledge: air jumps up through it
            bool jump = p.movementPower() == nullptr && p.vel.y > -30.0f && p.airJumpsLeft() > 0;
            return gin(0, jump ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
        }
        bool fire = p.movementPower() == nullptr && grappleAhead(*r.w, 1);
        return gin(c.x < 331.5f ? 1 : 0, fire ? Acts{Action::Interact} : Acts{}, p.vel.y > 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(reached);
    CHECK(r.feet() <= 34.05f);
    r.walkTo(331.5f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(-1, {Action::Shield}) : PlayerInput{}; });
    r.play(200, [&](Player&, int) { return gin(-1); });
    CHECK(r.w->stats().relics == 1);
}

TEST_CASE("glacior_02: upper route: two phase barriers crossed by teleport") {
    GRun r("glacior_02", 360, 56);
    r.god = true;
    r.killAwake();
    r.play(120, [&](Player&, int) { return gin(1); });
    CHECK(r.p().box.right() <= 364.0f * kT + 1.0f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Teleport}) : PlayerInput{}; });
    r.settle(150);
    r.killAwake();
    r.play(500, [&](Player&, int) { return gin(1); });
    CHECK(r.p().box.right() <= 392.0f * kT + 1.0f);
    CHECK(r.x() > 365.0f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Teleport}) : PlayerInput{}; });
    r.play(300, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 400.0f);
}

TEST_CASE("glacior_02: lower route: ground pound, sunken channel, dash walls, way up to the antechamber") {
    GRun r("glacior_02", 350, 56);
    r.god = true;
    r.killAwake();
    r.walkTo(354);
    r.play(30, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Jump}, {Action::Jump}) : gin(0, {}, {Action::Jump}); });
    r.step(gin(0, {Action::Attack}, {Action::MoveDown, Action::Attack}));
    r.play(200, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.feet() > 58.0f);
    bool dashed1 = false, dashed2 = false;
    r.play(2400, [&](Player& p, int i) {
        float x = p.box.center().x / kT;
        if (x > 401.0f && p.grounded()) return PlayerInput{};
        if (!dashed1 && x > 365.5f) {
            dashed1 = true;
            return gin(1, {Action::Dash});
        }
        if (!dashed2 && x > 381.5f) {
            dashed2 = true;
            return gin(1, {Action::Dash});
        }
        bool up = x > 394.5f && i % 12 == 0;
        return gin(1, up ? Acts{Action::Jump} : Acts{}, x > 394.5f ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.x() > 400.0f);
    CHECK(r.feet() <= 56.05f);
}

TEST_CASE("glacior_02: the Frost Hydra wakes, goes through its phases, dies and opens the way") {
    GRun r("glacior_02", 409, 60);
    bossFight(r, 463.0f, 3);
}

TEST_CASE("glacior_02: the wind shaft carries the player to the exit, the vault relic by teleport") {
    GRun r("glacior_02", 465, 60);
    rideUpdraft(r, 471.0f, 470.5f, 476.0f, 26.0f);
    CHECK(r.feet() <= 26.05f || r.w->state() != WorldState::Playing);
    GRun v("glacior_02", 465, 60);
    bool tele = false;
    v.play(900, [&](Player& p, int i) {
        float feet = p.box.bottom() / kT;
        if (i == 0) return gin(1, {Action::Jump}, {Action::Jump});
        if (!tele && feet < 39.6f && feet > 37.8f) {
            tele = true;
            return gin(1, {Action::Teleport}, {Action::Jump});
        }
        if (tele) return gin(1);
        float x = p.box.center().x / kT;
        return gin(std::abs(x - 472.5f) < 0.4f ? 0 : (x < 472.5f ? 1 : -1), {}, {Action::Jump});
    });
    CHECK(v.w->stats().relics == 1);
}

TEST_CASE("glacior_02: reaching the exit plays the outro and completes the level") { reachExit("glacior_02", 471, 26); }

// =====================================================================================
// glacior_03 — Le Bastion Gelé
// =====================================================================================

TEST_CASE("glacior_03: the broken ice bridge is crossed") {
    GRun r("glacior_03", 22, 56);
    r.killAwake();
    r.play(1500, [&](Player& p, int) {
        float x = p.box.center().x / kT;
        if (x > 78.0f && p.grounded()) return PlayerInput{};
        bool edge = false;
        for (float t : {31.3f, 43.3f, 52.3f, 62.2f}) edge |= p.grounded() && x > t && x < t + 0.6f;
        bool dbl = !p.grounded() && x > 63.0f && x < 66.5f && p.vel.y > -20.0f && p.airJumpsLeft() > 0;
        return gin(1, (edge || dbl) ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 77.0f);
    CHECK(r.feet() <= 56.05f);
}

TEST_CASE("glacior_03: the portcullis lever is on the turret ledge (triple jump), the gate opens") {
    GRun r("glacior_03", 80, 56);
    r.killAwake();
    r.play(200, [&](Player&, int) { return gin(1); });
    CHECK(r.x() < 93.5f);  // closed gate
    r.walkTo(87);
    r.multiJump(3, 0);
    CHECK(r.feet() <= 48.05f);
    r.walkTo(86);
    r.play(10, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Interact}) : PlayerInput{}; });
    r.play(60, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Jump}, {Action::MoveDown}) : PlayerInput{}; });
    CHECK(r.feet() > 55.5f);  // dropped back down through the ledge
    r.play(600, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 100.0f);
}

TEST_CASE("glacior_03: the drawbridge lever starts the bridge over the rampart gap") {
    GRun r("glacior_03", 146, 44);
    r.god = true;
    r.killAwake();
    r.walkTo(148);
    r.play(10, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Interact}) : PlayerInput{}; });
    // wait for the platform, ride it, step off on the far side
    r.play(1200, [&](Player& p, int i) {
        float x = p.box.center().x / kT;
        if (x > 163.0f && p.grounded()) return PlayerInput{};
        bool onPlat = p.grounded() && x > 150.0f;
        if (onPlat) return (i % 2 == 0) ? gin(1) : PlayerInput{};
        // step onto the platform when it is back at the near side
        for (auto& e : r.w->entities())
            if (e->type() == "moving_platform" && e->box.x < 150.3f * kT && x < 150.0f) return gin(1);
        return PlayerInput{};
    });
    CHECK(r.x() > 162.0f);
    CHECK(r.feet() <= 44.05f);
}

TEST_CASE("glacior_03: the rampart cellar hides a crystal heart (ground pound in, triple jump out)") {
    GRun r("glacior_03", 134, 44);
    r.god = true;
    r.killAwake();
    r.walkTo(138);
    r.play(30, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Jump}, {Action::Jump}) : gin(0, {}, {Action::Jump}); });
    r.step(gin(0, {Action::Attack}, {Action::MoveDown, Action::Attack}));
    r.play(150, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.feet() > 51.5f);
    r.walkTo(143.5f);
    CHECK(r.p().maxHealth() == 11);
    r.walkTo(138);
    r.multiJump(3, 1, 3);
    CHECK(r.feet() <= 44.05f);
}

TEST_CASE("glacior_03: the Cour d'Honneur locks, then opens after three waves") {
    GRun r("glacior_03", 167, 44);
    r.god = true;
    r.play(150, [&](Player&, int) { return gin(1); });
    CHECK(r.countAliveAwake() > 0);
    clearArena(r, 215.0f, 3);
}

TEST_CASE("glacior_03: the Tower of Winds, route A: the wind column, then the walkway") {
    GRun r("glacior_03", 216, 44);
    r.killAwake();
    rideUpdraft(r, 218.5f, 217.0f, 221.5f, 14.0f);
    CHECK(r.feet() <= 14.05f);
    r.walkTo(221.5f);
    r.play(600, [&](Player& p, int i) {
        float x = p.box.center().x / kT;
        if (x > 244.0f && p.grounded()) return PlayerInput{};
        return gin(1, (p.grounded() && x > 221.2f && x < 222.0f && i > 2) ? Acts{Action::Jump} : Acts{},
                   p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.x() > 243.0f);
    CHECK(r.feet() <= 16.05f);
}

TEST_CASE("glacior_03: the Tower of Winds, route B: triple jump, lift and anchor") {
    GRun r("glacior_03", 228, 44);
    r.god = true;
    r.killAwake();
    r.walkTo(234);
    r.multiJump(3, 0);
    CHECK(r.feet() <= 36.05f);
    // ride the lift, then grapple the anchor and land on the top ledge
    r.walkTo(231);
    int phase = 0;
    r.play(4000, [&](Player& p, int) {
        float feet = p.box.bottom() / kT, x = p.box.center().x / kT;
        float liftY = 0;
        for (auto& e : r.w->entities())
            if (e->type() == "moving_platform" && e->box.x > 226.0f * kT && e->box.x < 228.0f * kT) liftY = e->box.y / kT;
        if (phase == 0) {  // wait for the lift at the bottom, step on it
            if (p.grounded() && x < 229.8f) phase = 1;
            return liftY > 35.8f ? gin(-1) : PlayerInput{};
        }
        if (phase == 1) {  // ride up
            if (liftY < 24.2f) {
                phase = 2;
                return gin(0, {Action::Jump}, {Action::Jump});
            }
            return PlayerInput{};
        }
        if (phase == 2) {
            if (p.movementPower()) phase = 3;
            return gin(0, p.vel.y > -60.0f ? Acts{Action::Interact} : Acts{}, {Action::Jump});
        }
        if (p.grounded()) return feet < 16.5f && x < 243.0f ? gin(1) : PlayerInput{};
        bool jump = p.movementPower() == nullptr && p.vel.y > -30.0f && p.airJumpsLeft() > 0;
        return gin(feet < 17.0f ? 1 : 0, jump ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.feet() <= 16.05f);
    CHECK(r.x() > 236.0f);
}

TEST_CASE("glacior_03: the tower vault relic is reached by teleport from the wind ledge") {
    GRun r("glacior_03", 219, 14);
    r.settle(30);
    r.walkTo(216.8f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(-1, {Action::Teleport}) : PlayerInput{}; });
    r.play(120, [&](Player&, int) { return gin(-1); });
    CHECK(r.w->stats().relics == 1);
}

TEST_CASE("glacior_03: the chemin de ronde: crumbling walkway, ice and a crystal wall") {
    GRun r("glacior_03", 249, 16);
    r.god = true;
    r.killAwake();
    bool dashed = false;
    r.play(1200, [&](Player& p, int) {
        float x = p.box.center().x / kT;
        if (x > 287.0f && p.grounded()) return PlayerInput{};
        if (!dashed && x > 277.0f && p.grounded()) {
            dashed = true;
            return gin(1, {Action::Dash});
        }
        bool edge = false;
        for (float t : {253.5f, 260.5f, 268.0f}) edge |= p.grounded() && x > t && x < t + 0.5f;
        return gin(1, edge ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 286.0f);
    CHECK(r.feet() <= 16.05f);
}

TEST_CASE("glacior_03: the slab opens into the lever hall; timed door, plasma curtain, phase barrier") {
    GRun r("glacior_03", 294, 16);
    r.god = true;
    r.killAwake();
    r.walkTo(298);
    r.play(30, [&](Player&, int i) { return i == 0 ? gin(0, {Action::Jump}, {Action::Jump}) : gin(0, {}, {Action::Jump}); });
    r.step(gin(0, {Action::Attack}, {Action::MoveDown, Action::Attack}));
    r.play(200, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.feet() > 33.0f);
    r.walkTo(305.5f);
    r.play(150, [&](Player&, int) { return gin(1); });
    CHECK(r.x() < 316.0f);  // the timed door is closed
    r.walkTo(305.5f);
    r.play(30, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Wave}) : PlayerInput{}; });
    r.play(240, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 317.0f);
    r.killAwake();
    r.walkTo(328.5f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Shield}) : PlayerInput{}; });
    r.play(100, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 331.0f);
    r.killAwake();
    r.play(300, [&](Player&, int) { return gin(1); });
    CHECK(r.p().box.right() <= 348.0f * kT + 1.0f);
    r.play(2, [&](Player&, int i) { return i == 0 ? gin(1, {Action::Teleport}) : PlayerInput{}; });
    r.play(120, [&](Player&, int) { return gin(1); });
    CHECK(r.x() > 350.0f);
}

TEST_CASE("glacior_03: the hall niche relic: grapple, then a triple jump onto the ledge") {
    GRun r("glacior_03", 320, 34);
    r.god = true;
    r.killAwake();
    r.walkTo(323);
    bool pulled = false;
    r.play(600, [&](Player& p, int i) {
        if (i == 0) return gin(0, {Action::Jump}, {Action::Jump});
        if (p.movementPower()) pulled = true;
        if (p.grounded() && i > 30) return PlayerInput{};
        if (!pulled) return gin(0, p.vel.y > -100.0f ? Acts{Action::Interact} : Acts{}, {Action::Jump});
        bool jump = p.movementPower() == nullptr && p.vel.y > -30.0f && p.airJumpsLeft() > 0;
        return gin(0, jump ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(pulled);
    CHECK(r.feet() <= 20.05f);
    CHECK(r.w->stats().relics == 1);
}

TEST_CASE("glacior_03: the guardian's gulf: anchors, wind, crystal wall, floe, triple jump, anchor") {
    GRun r("glacior_03", 356, 34);
    r.god = true;
    r.killAwake();
    r.walkTo(357.5f);
    int phase = 0;  // 0 anchors, 1 wind + dash, 2 floe, 3 ledge, 4 last anchor
    r.play(4000, [&](Player& p, int i) {
        Vec2 c = p.center() / kT;
        float feet = p.box.bottom() / kT;
        if (i == 0) return gin(1, {Action::Jump}, {Action::Jump});
        if (phase == 0) {
            if (c.x > 372.5f) phase = 1;
            bool fire = !p.grounded() && p.movementPower() == nullptr && grappleAhead(*r.w, 1) && c.x < 372.0f;
            return gin(1, fire ? Acts{Action::Interact} : Acts{}, p.vel.y > 0 ? Acts{Action::Jump} : Acts{});
        }
        if (phase == 1) {  // glide into the wind column, rise to the crystal wall level, dash through it
            if (c.x > 386.5f) phase = 2;
            if (feet > 22.0f) return gin(std::abs(c.x - 378.0f) < 0.4f ? 0 : (c.x < 378.0f ? 1 : -1), {}, {Action::Jump});
            return gin(1, c.x > 381.5f && c.x < 383.0f ? Acts{Action::Dash} : Acts{}, {Action::Jump});
        }
        if (phase == 2) {  // glide down to the floe
            if (p.grounded()) phase = 3;
            return gin(c.x < 392.0f ? 1 : 0, {}, {Action::Jump});
        }
        if (phase == 3) {  // triple jump to the ledge
            if (p.grounded() && feet < 19.5f) {
                phase = 4;
                return PlayerInput{};
            }
            if (p.grounded()) return c.x < 395.3f ? gin(1) : gin(0, {Action::Jump}, {Action::Jump});
            bool jump = p.vel.y > -30.0f && p.airJumpsLeft() > 0;
            return gin(p.vel.y > 0 ? 1 : 0, jump ? Acts{Action::Jump} : Acts{}, p.vel.y < 0 ? Acts{Action::Jump} : Acts{});
        }
        // last anchor, then land on the antechamber ledge
        if (p.grounded() && c.x > 412.5f) return PlayerInput{};
        if (p.grounded()) return gin(1, c.x > 401.5f ? Acts{Action::Jump} : Acts{}, {Action::Jump});
        bool fire = p.movementPower() == nullptr && grappleAhead(*r.w, 1);
        return gin(1, fire ? Acts{Action::Interact} : Acts{}, p.vel.y > 0 ? Acts{Action::Jump} : Acts{});
    });
    CHECK(phase == 4);
    CHECK(r.x() > 412.0f);
    CHECK(r.feet() <= 24.05f);
}

TEST_CASE("glacior_03: the high road over the gulf hides the third relic") {
    GRun r("glacior_03", 374, 34);
    r.god = true;
    r.killAwake();
    rideUpdraft(r, 378.0f, 389.0f, 393.0f, 6.0f, 2000);
    CHECK(r.feet() <= 6.05f);
    r.walkTo(394);
    CHECK(r.w->stats().relics == 1);
}

TEST_CASE("glacior_03: the Glaciarch wakes, goes through its phases, dies and opens the way") {
    GRun r("glacior_03", 429, 50);
    bossFight(r, 485.0f, 3);
}

TEST_CASE("glacior_03: reaching the last relay plays the ending and completes the level") {
    reachExit("glacior_03", 491, 46);
}
