// Automated traversal checks of the Pyralis levels (pyralis_01..03): every mandatory move of
// the main route is achievable with the level's powers, arenas lock then open, bosses wake,
// go through their phases, die and open the way, and the exit completes the level.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float T = 16.0f;
constexpr float DT = 1.0f / 120.0f;

struct PyRun {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    PyRun(const char* id, float tx, float ty, std::initializer_list<const char*> extra = {}) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel(id);
        CHECK(level.has_value());
        level->intro.steps.clear();
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->startAt({tx * T, ty * T});
        x0 = tx;
        for (const char* p : extra) w->player().grantPower(*content.power(p));
        for (int i = 0; i < 20; ++i) w->fixedUpdate(PlayerInput{}, DT);  // settle
    }
    ~PyRun() {
        if (std::getenv("PYTIME"))
            std::printf("  [time] %s from x=%.0f: %.1f s of level time\n", w->level().id.c_str(), x0, w->timer().elapsed());
    }
    float x0 = 0.0f;
    Player& p() { return w->player(); }
    void play(int steps, const std::function<PlayerInput(Player&, int)>& ctrl) {
        for (int i = 0; i < steps; ++i) w->fixedUpdate(ctrl(p(), i), DT);
    }
    /// Same, keeping the player invulnerable (enemies and hazards are not under test).
    void playSafe(int steps, const std::function<PlayerInput(Player&, int)>& ctrl) {
        static const bool trace = std::getenv("PYTRACE") != nullptr;
        for (int i = 0; i < steps; ++i) {
            p().setInvulnerable(1.0f);
            PlayerInput pi = ctrl(p(), i);
            w->fixedUpdate(pi, DT);
            if (trace && i % 12 == 0)
                std::printf("  t%4d x=%7.2f feet=%6.2f v=(%6.1f,%6.1f) g=%d mp=%d st=%d in[R%d L%d J%d I%d]\n", i,
                            p().box.center().x / T, p().box.bottom() / T, p().vel.x, p().vel.y, p().grounded(),
                            p().movementPower() != nullptr, static_cast<int>(p().state()),
                            pi.isHeld(Action::MoveRight), pi.isHeld(Action::MoveLeft), pi.isHeld(Action::Jump),
                            pi.isPressed(Action::Interact));
        }
    }
    float feet() { return p().box.bottom() / T; }
    float x() { return p().box.center().x / T; }
    int aliveAwake() {
        int n = 0;
        for (auto& e : w->enemies()) n += e->alive() && !e->dormant() ? 1 : 0;
        return n;
    }
};

/// dir: -1 left, 0 none, 1 right. down: hold Down.
PlayerInput in(int dir, std::initializer_list<Action> pressed = {}, std::initializer_list<Action> held = {},
               bool down = false) {
    PlayerInput i;
    if (dir > 0) {
        i.held[static_cast<int>(Action::MoveRight)] = true;
        i.move.x = 1.0f;
    } else if (dir < 0) {
        i.held[static_cast<int>(Action::MoveLeft)] = true;
        i.move.x = -1.0f;
    }
    if (down) {
        i.held[static_cast<int>(Action::MoveDown)] = true;
        i.move.y = 1.0f;
    }
    for (Action a : pressed) i.pressed[static_cast<int>(a)] = i.held[static_cast<int>(a)] = true;
    for (Action a : held) i.held[static_cast<int>(a)] = true;
    return i;
}

PlayerInput pound() { return in(0, {Action::Attack}, {}, true); }

/// Jump, then ground pound near the apex; returns once landed (or after max steps).
void jumpAndPound(PyRun& r, int dir = 0) {
    r.playSafe(1, [&](Player&, int) { return in(dir, {Action::Jump}); });
    r.playSafe(30, [&](Player&, int) { return in(dir, {}, {Action::Jump}); });
    r.playSafe(1, [&](Player&, int) { return pound(); });
    r.playSafe(240, [&](Player&, int) { return PlayerInput{}; });
}

/// Kills every awake enemy (arena waves) and lets the arena advance.
void clearWaves(PyRun& r, int waves) {
    for (int wave = 0; wave < waves; ++wave) {
        DamageInfo d;
        d.amount = 100;
        d.type = DamageType::ChargeShot;
        d.fromPlayer = true;
        for (auto& e : r.w->enemies())
            if (e->alive() && !e->dormant()) {
                d.source = e->center() + Vec2{0, -40};
                e->takeDamage(*r.w, d);
                if (e->alive()) e->kill(*r.w);
            }
        r.playSafe(300, [&](Player&, int) { return PlayerInput{}; });
    }
}

/// Damages the boss until it dies; returns the highest phase index reached.
int fightBoss(PyRun& r) {
    Enemy* boss = r.w->activeBoss();
    CHECK(boss != nullptr);
    if (!boss) return -1;
    CHECK(boss->def().isBoss);
    int maxPhase = 0;
    for (int hit = 0; hit < 300 && boss && boss->alive(); ++hit) {
        DamageInfo d;
        d.amount = 2;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        d.source = boss->center() - Vec2{30, 0};
        boss->takeDamage(*r.w, d);
        r.play(30, [&](Player& p, int) {
            p.setInvulnerable(1.0f);
            p.health = std::max(p.health, 5);
            return PlayerInput{};
        });
        maxPhase = std::max(maxPhase, boss->phaseIndex());
        boss = r.w->activeBoss();
    }
    CHECK(r.w->activeBoss() == nullptr);
    return maxPhase;
}

/// Level entity of a given type whose authored rectangle contains (x, y) tiles.
LevelEntity* findEntity(PyRun& r, const char* type, float x, float y) {
    for (const auto& o : r.w->level().objects)
        if (o.type == type && o.rect.contains({x * T, y * T})) return r.w->entity(o.id);
    return nullptr;
}

/// Turns to face `dir` without moving much, then stands still.
void face(PyRun& r, int dir) {
    r.playSafe(2, [&](Player&, int) { return in(dir); });
    r.playSafe(30, [&](Player&, int) { return PlayerInput{}; });
}

/// Holds Attack long enough for a full charge, then releases (fires the charged shot).
void chargeShot(PyRun& r) {
    r.playSafe(1, [&](Player&, int) { return in(0, {Action::Attack}); });
    r.playSafe(115, [&](Player&, int) { return in(0, {}, {Action::Attack}); });
    r.playSafe(150, [&](Player&, int) { return PlayerInput{}; });
}

void waveShot(PyRun& r) {
    r.playSafe(1, [&](Player&, int) { return in(0, {Action::Wave}); });
    r.playSafe(150, [&](Player&, int) { return PlayerInput{}; });
}

/// Runs right, jumps at xJump, hovers in the updraft centred on xCol until the feet are above
/// yTop, then glides right and lets go over xLand. Returns true if the column top was reached.
bool rideUpdraft(PyRun& r, float xJump, float xCol, float yTop, float xLand, int steps = 1800) {
    int phase = 0;
    r.playSafe(steps, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (cx > xJump) {
                phase = 1;
                return in(1, {Action::Jump});
            }
            return in(1);
        }
        if (phase == 1) {
            if (p.box.bottom() < yTop * T) phase = 2;
            return in(cx < xCol - 0.5f ? 1 : (cx > xCol + 0.5f ? -1 : 0), {}, {Action::Jump});
        }
        if (cx > xLand) return PlayerInput{};
        return in(1, {}, {Action::Jump});
    });
    return phase == 2;
}

/// Rides the moving platform found at (px, py) until its right edge passes `edge`, then runs right
/// (jumping) onto the next ground. Stops once grounded beyond `stopX`.
void rideRaft(PyRun& r, float px, float py, float edge, float stopX, bool jump = false, int steps = 2400) {
    LevelEntity* raft = findEntity(r, "moving_platform", px, py);
    CHECK(raft != nullptr);
    if (!raft) return;
    bool go = false;
    r.playSafe(steps, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (!go && raft->box.right() > edge * T) go = true;
        if (p.grounded() && cx > stopX) return PlayerInput{};
        if (!go) {
            // stay centred on the raft
            float rc = raft->box.center().x / T;
            return in(cx < rc - 0.4f ? 1 : (cx > rc + 0.4f ? -1 : 0));
        }
        if (jump && p.grounded() && p.groundCollider() != 0 && r.w->physics().get(p.groundCollider()) &&
            r.w->physics().get(p.groundCollider())->dynamic)
            return in(1, {Action::Jump}, {Action::Jump});
        return in(1, {}, {Action::Jump});
    });
}

/// Fires the grapple at successive anchors (retrying until each pull starts), holding `dir`;
/// stops once grounded beyond stopX. Returns the number of pulls.
int grappleChain(PyRun& r, int pulls, float startX, float stopX, int dir = 1, int steps = 1800,
                 bool doubleJumpAfter = false, int afterDir = 0) {
    int grabs = 0;
    bool wasFree = true, dj = false;
    float releaseX = -1e9f;
    r.playSafe(steps, [&](Player& p, int i) {
        bool free = p.movementPower() == nullptr;
        float cx = p.box.center().x / T;
        if (wasFree && !free) ++grabs;
        if (!wasFree && free) releaseX = cx;
        wasFree = free;
        // Like a player: wait until past the anchor just left before aiming at the next one.
        bool past = grabs == 0 ? (dir > 0 ? p.box.right() > startX * T : p.box.x < startX * T)
                               : (dir > 0 ? cx > releaseX + 3.5f : cx < releaseX - 3.5f) || std::abs(p.vel.x) < 20.0f;
        if (free && grabs < pulls && i % 4 == 0 && past) return in(dir, {Action::Interact});
        int d = (afterDir != 0 && free && grabs == pulls) ? afterDir : dir;
        if (p.grounded() && (d > 0 ? cx > stopX : cx < stopX)) return PlayerInput{};
        if (doubleJumpAfter && free && grabs == pulls && !dj && !p.grounded() && p.vel.y > -40.0f) {
            dj = true;
            return in(d, {Action::Jump}, {Action::Jump});
        }
        return in(d, {}, dj && p.vel.y < 0.0f ? std::initializer_list<Action>{Action::Jump}
                                              : std::initializer_list<Action>{});
    });
    return grabs;
}

void reachExit(PyRun& r, int dir = 1) {
    CHECK(r.w->state() == WorldState::Playing);
    r.playSafe(480, [&](Player&, int) { return in(dir); });
    CHECK(r.w->state() == WorldState::Outro || r.w->state() == WorldState::Completed);
    r.play(1500, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.w->state() == WorldState::Completed);
}
}  // namespace

// =====================================================================================
// pyralis_01 — Les Forges Englouties
// =====================================================================================

TEST_CASE("pyralis_01: the first updraft lifts a gliding player to the high ledge") {
    PyRun r("pyralis_01", 28, 56);
    int phase = 0;
    r.playSafe(900, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (p.box.right() > 30.6f * T) {
                phase = 1;
                return in(1, {Action::Jump});
            }
            return in(1);
        }
        if (phase == 1) {  // glide towards the column, then hover in it
            if (cx > 37.0f) phase = 2;
            return in(1, {}, {Action::Jump});
        }
        if (phase == 2) {
            if (p.box.bottom() < 43.0f * T) phase = 3;
            return in(cx < 37.0f ? 1 : (cx > 38.0f ? -1 : 0), {}, {Action::Jump});
        }
        if (p.grounded() && cx > 47.0f) return PlayerInput{};
        return in(1, {}, cx < 47.0f ? std::initializer_list<Action>{Action::Jump} : std::initializer_list<Action>{});
    });
    CHECK(phase == 3);
    CHECK(r.p().alive());
    CHECK(r.p().grounded());
    CHECK(r.x() > 45.0f);
    CHECK_NEAR(r.feet(), 50.0f, 0.1f);
}

TEST_CASE("pyralis_01: the grapple crosses the lava gap to the quay") {
    PyRun r("pyralis_01", 59, 50);
    bool fired = false;
    r.playSafe(600, [&](Player& p, int) {
        if (!fired && p.box.right() > 61.6f * T) {
            fired = true;
            return in(1, {Action::Interact});
        }
        return in(fired ? 1 : 1);
    });
    CHECK(fired);
    CHECK(r.p().alive());
    CHECK(r.x() > 76.0f);
    CHECK_NEAR(r.feet(), 52.0f, 0.1f);
}

TEST_CASE("pyralis_01: the tablet teaches the ground pound and the cracked floor gives way") {
    PyRun r("pyralis_01", 102, 52);
    CHECK(!r.p().hasPower("ground_pound"));
    r.playSafe(240, [&](Player&, int) { return in(1); });
    CHECK(r.p().hasPower("ground_pound"));
    CHECK(r.x() < 120.0f);           // the machinery wall blocks the upper floor
    CHECK_NEAR(r.feet(), 52.0f, 0.1f);  // walking does not break the slab
    // Stand on the slab, jump, pound.
    r.p().teleportTo({114.5f * T, 52.0f * T});
    r.playSafe(30, [&](Player&, int) { return PlayerInput{}; });
    jumpAndPound(r);
    CHECK(r.p().alive());
    CHECK_NEAR(r.feet(), 63.0f, 0.1f);  // safe landing in the lower gallery
}

TEST_CASE("pyralis_01: the gallery chimney lifts the player back to the surface") {
    PyRun r("pyralis_01", 166, 63);
    int phase = 0;
    r.playSafe(1200, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (cx > 168.5f) {
                phase = 1;
                return in(1, {Action::Jump});
            }
            return in(1);
        }
        if (phase == 1) {
            if (p.box.bottom() < 44.0f * T) phase = 2;
            return in(cx < 170.5f ? 1 : (cx > 171.5f ? -1 : 0), {}, {Action::Jump});
        }
        return in(1, {}, {Action::Jump});
    });
    CHECK(phase == 2);
    CHECK(r.p().grounded());
    CHECK(r.x() > 174.5f);
    CHECK_NEAR(r.feet(), 50.0f, 0.1f);
}

TEST_CASE("pyralis_01: the anvil hall locks, sends three waves, then opens") {
    PyRun r("pyralis_01", 205, 50);
    r.playSafe(60, [&](Player&, int) { return in(1); });
    CHECK(r.aliveAwake() > 0);
    // Doors closed: walking right stops at the exit door.
    r.playSafe(300, [&](Player&, int) { return in(1); });
    CHECK(r.x() < 252.0f);
    clearWaves(r, 3);
    r.playSafe(600, [&](Player&, int) { return in(1); });
    CHECK(r.x() > 255.0f);
}

TEST_CASE("pyralis_01: the tower of vats is descended by ground pounds only") {
    PyRun r("pyralis_01", 263, 50, {"ground_pound"});
    r.playSafe(300, [&](Player&, int) { return in(1); });
    CHECK(r.x() < 284.0f);  // closed tower
    CHECK(r.feet() <= 50.1f);
    r.p().teleportTo({268.5f * T, 50.0f * T});
    r.playSafe(20, [&](Player&, int) { return PlayerInput{}; });
    jumpAndPound(r);
    CHECK_NEAR(r.feet(), 57.0f, 0.1f);
    r.p().teleportTo({277.5f * T, 57.0f * T});
    r.playSafe(20, [&](Player&, int) { return PlayerInput{}; });
    jumpAndPound(r);
    CHECK(r.p().alive());
    CHECK_NEAR(r.feet(), 63.0f, 0.1f);
}

TEST_CASE("pyralis_01: the grapple chain crosses the underground river") {
    PyRun r("pyralis_01", 299, 57);
    int grabs = 0;
    float lastGrab = -1.0f;
    float t = 0.0f;
    r.playSafe(1200, [&](Player& p, int) {
        t += DT;
        bool free = p.movementPower() == nullptr;
        // Fire at the next anchor when free and the previous pull is over.
        if (free && grabs < 3 && t - lastGrab > 0.12f && !(grabs == 0 && p.box.right() < 300.5f * T)) {
            ++grabs;
            lastGrab = t;
            return in(1, {Action::Interact});
        }
        if (p.grounded() && p.box.center().x > 330.0f * T) return PlayerInput{};
        return in(1);
    });
    CHECK(grabs == 3);
    CHECK(r.p().alive());
    CHECK(r.p().grounded());
    CHECK(r.x() > 328.0f);
    CHECK_NEAR(r.feet(), 55.0f, 0.1f);
}

TEST_CASE("pyralis_01: the river chimney leads to the surface") {
    PyRun r("pyralis_01", 343, 55);
    int phase = 0;
    r.playSafe(1500, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (p.box.right() > 345.8f * T) {
                phase = 1;
                return in(1, {Action::Jump});
            }
            return in(1);
        }
        if (phase == 1) {
            if (p.box.bottom() < 30.0f * T) phase = 2;
            return in(cx < 348.5f ? 1 : (cx > 349.5f ? -1 : 0), {}, {Action::Jump});
        }
        if (cx > 355.0f) return PlayerInput{};
        return in(1, {}, {Action::Jump});
    });
    CHECK(phase == 2);
    CHECK(r.p().grounded());
    CHECK(r.x() > 352.0f);
    CHECK_NEAR(r.feet(), 36.0f, 0.1f);
}

TEST_CASE("pyralis_01: the crumbling obsidian bridge can be run across") {
    PyRun r("pyralis_01", 362, 36);
    const float ends[] = {365.8f, 369.8f, 374.8f, 379.8f};
    r.playSafe(900, [&](Player& p, int) {
        for (float e : ends)
            if (p.grounded() && p.box.right() > e * T && p.box.right() < (e + 1.0f) * T) return in(1, {Action::Jump});
        if (p.box.center().x > 386.0f * T) return PlayerInput{};
        return in(1, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 381.0f);
    CHECK_NEAR(r.feet(), 40.0f, 0.1f);
}

TEST_CASE("pyralis_01: the heart vault is entered by a pound and left by the grapple") {
    PyRun r("pyralis_01", 400, 40, {"ground_pound"});
    int hp = r.p().maxHealth();
    jumpAndPound(r);
    CHECK_NEAR(r.feet(), 50.0f, 0.1f);
    CHECK(r.p().maxHealth() == hp + 1);
    int phase = 0;
    r.playSafe(600, [&](Player& p, int) {
        if (phase == 0) {
            phase = 1;
            return in(0, {Action::Interact});
        }
        if (phase == 1 && !p.movementPower()) phase = 2;
        if (phase == 2 && p.vel.y > -30.0f) {
            phase = 3;
            return in(0, {Action::Jump}, {Action::Jump});
        }
        if (phase == 3 && p.vel.y >= 0.0f) phase = 4;
        if (phase == 4 && p.grounded() && p.box.x > 402.5f * T) return PlayerInput{};
        bool up = phase == 3 && p.box.bottom() > 40.2f * T;  // still inside the shaft
        return in(phase >= 3 && !up ? 1 : 0, {}, phase == 3 ? std::initializer_list<Action>{Action::Jump}
                                                           : std::initializer_list<Action>{});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 402.0f);
    CHECK(r.feet() <= 40.1f);
}

TEST_CASE("pyralis_01: the forge corridor needs the dash and the teleport") {
    PyRun r("pyralis_01", 411, 40);
    r.playSafe(120, [&](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 413.0f * T + 1.0f);
    r.playSafe(60, [&](Player&, int i) { return i == 0 ? in(1, {Action::Dash}) : in(1); });
    r.playSafe(200, [&](Player&, int) { return in(1); });
    CHECK(r.x() > 415.0f);
    CHECK(r.p().box.right() <= 420.0f * T + 1.0f);
    r.playSafe(2, [&](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : PlayerInput{}; });
    CHECK(r.x() > 421.0f);
    r.playSafe(300, [&](Player& p, int) { return p.box.center().x > 433.5f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 432.0f);
    CHECK_NEAR(r.feet(), 44.0f, 0.1f);
}

TEST_CASE("pyralis_01: the duct shortcut rejoins the corridor through the grate") {
    PyRun r("pyralis_01", 408, 40, {"ground_pound"});
    jumpAndPound(r);
    CHECK_NEAR(r.feet(), 49.0f, 0.1f);
    int phase = 0;
    r.playSafe(1500, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (cx > 425.0f) {
                phase = 1;
                return in(0, {Action::Jump});
            }
            return in(1);
        }
        if (phase == 1) {
            if (p.box.bottom() < 39.0f * T) phase = 2;
            return in(cx < 425.5f ? 1 : (cx > 426.5f ? -1 : 0), {}, {Action::Jump});
        }
        if (p.grounded() && cx > 429.0f) return PlayerInput{};
        return in(1);
    });
    CHECK(phase == 2);
    CHECK(r.x() > 428.0f);
    CHECK(r.feet() <= 44.1f);
}

TEST_CASE("pyralis_01: Magmaroth wakes, goes through three phases, dies and opens the way") {
    PyRun r("pyralis_01", 444, 50);
    r.playSafe(240, [&](Player&, int) { return in(1); });
    CHECK(fightBoss(r) == 2);
    r.playSafe(900, [&](Player&, int) { return in(1); });
    CHECK(r.x() > 491.0f);
}

TEST_CASE("pyralis_01: the crucible updrafts lead to the exit balcony") {
    PyRun r("pyralis_01", 497, 50);
    int phase = 0;
    r.playSafe(2400, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        float f = p.box.bottom() / T;
        switch (phase) {
            case 0:  // into the first column
                if (cx > 501.5f) {
                    phase = 1;
                    return in(0, {Action::Jump});
                }
                return in(1);
            case 1:
                if (f < 33.0f) phase = 2;
                return in(cx < 501.5f ? 1 : (cx > 502.5f ? -1 : 0), {}, {Action::Jump});
            case 2:  // glide onto the ledge
                if (p.grounded() && cx > 505.5f) phase = 3;
                return in(1, {}, {Action::Jump});
            case 3:
                if (cx > 511.8f) {
                    phase = 4;
                    return in(0, {Action::Jump});
                }
                return in(1);
            case 4:
                if (f < 24.0f) phase = 5;
                return in(cx < 512.5f ? 1 : (cx > 513.5f ? -1 : 0), {}, {Action::Jump});
            default:
                return in(1, {}, {Action::Jump});
        }
    });
    CHECK(phase >= 4);
    CHECK(r.w->state() == WorldState::Outro || r.w->state() == WorldState::Completed ||
          (r.p().grounded() && r.feet() < 26.1f && r.x() > 515.0f));
}

TEST_CASE("pyralis_01: reaching the exit completes the level") {
    PyRun r("pyralis_01", 516, 26);
    reachExit(r);
}

// =====================================================================================
// pyralis_02 — Le Fleuve de Lave
// =====================================================================================

TEST_CASE("pyralis_02: the first raft crosses the river") {
    PyRun r("pyralis_02", 38.5f, 54);
    rideRaft(r, 38, 54.5f, 56.7f, 58.5f);
    CHECK(r.p().alive());
    CHECK(r.x() > 57.0f);
    CHECK_NEAR(r.feet(), 54.0f, 0.1f);
}

TEST_CASE("pyralis_02: the conduit receptor needs the charged shot, then the raft crosses") {
    PyRun r("pyralis_02", 88, 54);
    CHECK(!r.p().hasPower("charge_shot"));
    r.playSafe(80, [&](Player&, int) { return in(1); });
    CHECK(r.p().hasPower("charge_shot"));
    LevelEntity* raft = findEntity(r, "moving_platform", 121, 54.5f);
    CHECK(raft != nullptr);
    if (!raft) return;
    r.p().teleportTo({99.5f * T, 54.0f * T});
    face(r, 1);
    r.p().energy = 100;
    waveShot(r);
    CHECK(raft->debugInfo() == "arrêtée");  // the wave fizzles before the receptor
    r.p().energy = 100;
    chargeShot(r);
    CHECK(raft->debugInfo() == "en mouvement");
    // Board the raft when it docks, ride it to the far quay.
    int phase = 0;
    r.playSafe(2400, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (raft->box.x < 100.2f * T) phase = 1;
            return PlayerInput{};
        }
        if (phase == 1) {
            if (cx > 101.5f) phase = 2;
            return in(1);
        }
        if (phase == 2) {
            if (raft->box.right() > 123.8f * T) phase = 3;
            float rc = raft->box.center().x / T;
            return in(cx < rc - 0.4f ? 1 : (cx > rc + 0.4f ? -1 : 0));
        }
        if (p.grounded() && cx > 127.0f) return PlayerInput{};
        return in(1);
    });
    CHECK(phase == 3);
    CHECK(r.p().alive());
    CHECK(r.x() > 126.0f);
    CHECK_NEAR(r.feet(), 54.0f, 0.1f);
}

TEST_CASE("pyralis_02: a charged shot dissolves the energy barrier") {
    PyRun r("pyralis_02", 137, 54, {"charge_shot"});
    r.playSafe(120, [&](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 141.0f * T + 1.0f);
    r.p().energy = 100;
    chargeShot(r);
    r.playSafe(200, [&](Player& p, int) { return p.box.center().x > 146.0f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 144.0f);
}

TEST_CASE("pyralis_02: charged shots fell a shield-bearer from afar") {
    PyRun r("pyralis_02", 151, 54, {"charge_shot"});
    Enemy* bulwark = nullptr;
    for (auto& e : r.w->enemies())
        if (e->def().id == "forge_bulwark" && std::abs(e->center().x / T - 162.0f) < 3.0f) bulwark = e.get();
    CHECK(bulwark != nullptr);
    if (!bulwark) return;
    face(r, 1);
    float hp0 = bulwark->hp();
    r.p().energy = 100;
    chargeShot(r);
    CHECK(bulwark->hp() <= hp0 - 5.9f);  // 4 x 1.5: the guard is not up at range
    // finish it from behind with a ground pound
    for (int i = 0; i < 3 && bulwark->alive(); ++i) {
        r.p().teleportTo({bulwark->center().x + 30.0f, 54.0f * T});
        r.playSafe(5, [&](Player&, int) { return PlayerInput{}; });
        jumpAndPound(r);
    }
    CHECK(!bulwark->alive());
}

TEST_CASE("pyralis_02: the ferrymen's bastion locks, sends three waves, then opens") {
    PyRun r("pyralis_02", 205, 54, {"charge_shot"});
    r.playSafe(60, [&](Player&, int) { return in(1); });
    CHECK(r.aliveAwake() > 0);
    r.playSafe(300, [&](Player&, int) { return in(1); });
    CHECK(r.x() < 250.0f);
    clearWaves(r, 3);
    r.playSafe(600, [&](Player& p, int) { return p.box.center().x > 256.0f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 253.0f);
}

TEST_CASE("pyralis_02: rafts A and B carry the player to the islet") {
    PyRun r("pyralis_02", 263.5f, 54, {"charge_shot"});
    LevelEntity* a = findEntity(r, "moving_platform", 263, 54.5f);
    LevelEntity* b = findEntity(r, "moving_platform", 285, 54.5f);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    if (!a || !b) return;
    int phase = 0;
    r.playSafe(4000, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        switch (phase) {
            case 0:  // ride raft A to the basalt pile, jump up
                if (a->box.right() > 279.6f * T) {
                    phase = 1;
                    return in(1, {Action::Jump}, {Action::Jump});
                }
                return in(cx < a->box.center().x / T - 0.4f ? 1 : (cx > a->box.center().x / T + 0.4f ? -1 : 0));
            case 1:  // double jump onto the pile
                if (p.vel.y > -40.0f && !p.grounded()) {
                    phase = 2;
                    return in(1, {Action::Jump}, {Action::Jump});
                }
                return in(1, {}, {Action::Jump});
            case 2:
                if (p.grounded() && cx > 281.0f) phase = 3;
                return in(cx < 282.0f ? 1 : 0);
            case 3:  // wait for raft B, hop on
                if (b->box.x < 284.4f * T) phase = 4;
                return PlayerInput{};
            case 4:
                if (p.grounded() && cx > 285.5f) phase = 5;
                return in(1);
            case 5:
                if (b->box.right() > 301.6f * T) phase = 6;
                return in(cx < b->box.center().x / T - 0.4f ? 1 : (cx > b->box.center().x / T + 0.4f ? -1 : 0));
            default:
                if (p.grounded() && cx > 304.0f) return PlayerInput{};
                return in(1);
        }
    });
    CHECK(phase == 6);
    CHECK(r.p().alive());
    CHECK(r.x() > 302.0f);
    CHECK_NEAR(r.feet(), 54.0f, 0.1f);
}

TEST_CASE("pyralis_02: the shield protects from the lava fall over raft B") {
    PyRun r("pyralis_02", 282, 50, {"charge_shot"});
    LevelEntity* b = findEntity(r, "moving_platform", 285, 54.5f);
    CHECK(b != nullptr);
    if (!b) return;
    // Put the player on the raft at its dock, shield up near the fall, let the raft carry him through.
    for (auto& e : r.w->enemies()) e->kill(*r.w);  // only the lava fall is under test
    r.playSafe(120, [&](Player&, int) { return PlayerInput{}; });
    for (int i = 0; i < 2000 && b->box.x > 284.2f * T; ++i) r.w->fixedUpdate(PlayerInput{}, DT);
    r.p().teleportTo({b->box.center().x, b->box.y});
    r.playSafe(130, [&](Player&, int) { return PlayerInput{}; });
    int hp = r.p().health;
    bool shielded = false;
    for (int i = 0; i < 600; ++i) {
        PlayerInput pi;
        float cx = r.p().box.center().x / T;
        float rc = b->box.center().x / T;
        pi = in(cx < rc - 0.4f ? 1 : (cx > rc + 0.4f ? -1 : 0));
        if (!shielded && std::abs(cx - 291.0f) < 2.5f) {
            pi = in(0, {Action::Shield});
            shielded = true;
        }
        r.w->fixedUpdate(pi, DT);
        if (std::getenv("PYTRACE") && i % 12 == 0)
            std::printf("  i%d x=%.2f feet=%.2f hp=%d sh=%d raft=%.2f\n", i, r.x(), r.feet(), r.p().health,
                        r.p().shieldActive(), b->box.x / T);
    }
    CHECK(shielded);
    CHECK(r.p().health == hp);
}

TEST_CASE("pyralis_02: the lift climbs to the aqueduct cliff") {
    PyRun r("pyralis_02", 310, 54, {"charge_shot"});
    LevelEntity* lift = findEntity(r, "moving_platform", 313, 54.5f);
    CHECK(lift != nullptr);
    if (!lift) return;
    int phase = 0;
    r.playSafe(3000, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (lift->box.y > 53.8f * T) phase = 1;
            return PlayerInput{};
        }
        if (phase == 1) {
            if (cx > 313.5f) phase = 2;
            return in(1);
        }
        if (phase == 2) {
            if (lift->box.y < 42.2f * T) phase = 3;
            return PlayerInput{};
        }
        if (p.grounded() && cx > 317.0f) return PlayerInput{};
        return in(1);
    });
    CHECK(phase == 3);
    CHECK(r.x() > 315.0f);
    CHECK_NEAR(r.feet(), 42.0f, 0.1f);
}

TEST_CASE("pyralis_02: the aqueduct receptor needs a charged shot; its raft crosses") {
    PyRun r("pyralis_02", 325.5f, 42, {"charge_shot"});
    LevelEntity* raft = findEntity(r, "moving_platform", 327, 42.5f);
    CHECK(raft != nullptr);
    if (!raft) return;
    face(r, 1);
    r.p().teleportTo({325.6f * T, 42.0f * T});
    r.p().energy = 100;
    waveShot(r);
    CHECK(raft->debugInfo() == "arrêtée");
    r.p().energy = 100;
    chargeShot(r);
    CHECK(raft->debugInfo() == "en mouvement");
    int phase = 0;
    r.playSafe(3000, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (raft->box.x > 330.0f * T) phase = 1;  // left once
            return PlayerInput{};
        }
        if (phase == 1) {
            if (raft->box.x < 326.2f * T) phase = 2;  // back at the dock
            return PlayerInput{};
        }
        if (phase == 2) {
            if (cx > 327.5f) phase = 3;
            return in(1);
        }
        if (phase == 3) {
            if (raft->box.right() > 347.8f * T) phase = 4;
            float rc = raft->box.center().x / T;
            return in(cx < rc - 0.4f ? 1 : (cx > rc + 0.4f ? -1 : 0));
        }
        if (p.grounded() && cx > 350.0f) return PlayerInput{};
        return in(1);
    });
    CHECK(phase == 4);
    CHECK(r.p().alive());
    CHECK(r.x() > 348.5f);
    CHECK_NEAR(r.feet(), 42.0f, 0.1f);
}

TEST_CASE("pyralis_02: the collapsing obsidian bridges can be run across (eruption included)") {
    PyRun r("pyralis_02", 354, 42, {"charge_shot"});
    const float ends[] = {356.8f, 360.8f, 365.8f, 370.8f, 375.8f, 380.8f, 385.8f, 390.8f};
    r.playSafe(1200, [&](Player& p, int) {
        for (float e : ends)
            if (p.grounded() && p.box.right() > e * T && p.box.right() < (e + 1.2f) * T) return in(1, {Action::Jump});
        if (p.grounded() && p.box.center().x > 394.0f * T) return PlayerInput{};
        return in(1, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 392.0f);
    CHECK_NEAR(r.feet(), 44.0f, 0.1f);
}

TEST_CASE("pyralis_02: the islet crypt is entered by a pound and left by its updraft") {
    PyRun r("pyralis_02", 400.5f, 44, {"charge_shot"});
    int relics = r.w->stats().relics;
    jumpAndPound(r);
    CHECK_NEAR(r.feet(), 51.0f, 0.1f);
    r.playSafe(200, [&](Player& p, int) { return p.box.center().x < 403.5f * T ? in(1) : PlayerInput{}; });
    CHECK(r.w->stats().relics == relics + 1);
    CHECK(rideUpdraft(r, 400.0f, 400.5f, 42.0f, 403.0f, 1500) || r.feet() <= 44.1f);
    CHECK(r.p().alive());
    CHECK(r.feet() <= 44.1f);
}

TEST_CASE("pyralis_02: the grapple relays reach the cataract foot") {
    PyRun r("pyralis_02", 406, 44, {"charge_shot"});
    int grabs = 0;
    bool wasFree = true;
    r.playSafe(1500, [&](Player& p, int i) {
        bool free = p.movementPower() == nullptr;
        if (wasFree && !free) ++grabs;
        wasFree = free;
        if (free && grabs < 2 && i % 4 == 0 && !(grabs == 0 && p.box.right() < 407.6f * T))
            return in(1, {Action::Interact});
        if (p.grounded() && p.box.center().x > 432.0f * T) return PlayerInput{};
        return in(1);
    });
    CHECK(grabs == 2);
    CHECK(r.p().alive());
    CHECK(r.x() > 430.0f);
    CHECK_NEAR(r.feet(), 40.0f, 0.1f);
}

TEST_CASE("pyralis_02: the cataract geysers lift the player to the top") {
    PyRun r("pyralis_02", 441, 40, {"charge_shot"});
    CHECK(rideUpdraft(r, 444.2f, 446.0f, 28.0f, 449.5f));
    CHECK_NEAR(r.feet(), 32.0f, 0.1f);
    CHECK(rideUpdraft(r, 455.2f, 457.0f, 20.0f, 461.0f));
    CHECK(r.p().alive());
    CHECK(r.x() > 459.0f);
    CHECK_NEAR(r.feet(), 30.0f, 0.1f);
}

TEST_CASE("pyralis_02: the heart behind the cascade is reached under shield") {
    PyRun r("pyralis_02", 457, 38, {"charge_shot"});
    int maxHp = r.p().maxHealth();
    int phase = 0;
    for (int i = 0; i < 1500; ++i) {
        Player& p = r.p();
        float cx = p.box.center().x / T;
        PlayerInput pi;
        if (phase == 0) {
            pi = in(0, {Action::Jump});
            phase = 1;
        } else if (phase == 1) {
            pi = in(cx < 456.5f ? 1 : (cx > 457.5f ? -1 : 0), {}, {Action::Jump});
            if (p.box.bottom() < 18.6f * T) {
                pi = in(0, {Action::Shield}, {Action::Jump});
                phase = 2;
            }
        } else {
            pi = p.grounded() && cx < 451.0f ? PlayerInput{} : in(-1, {}, {Action::Jump});
        }
        r.w->fixedUpdate(pi, DT);
    }
    CHECK(phase == 2);
    CHECK(r.p().alive());
    CHECK(r.p().maxHealth() == maxHp + 1);
}

TEST_CASE("pyralis_02: Ignivore wakes, goes through three phases, dies and opens the way") {
    PyRun r("pyralis_02", 482, 36, {"charge_shot"});
    r.playSafe(240, [&](Player&, int) { return in(1); });
    CHECK(fightBoss(r) == 2);
    r.playSafe(900, [&](Player& p, int) { return p.box.center().x > 533.0f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 531.0f);
}

TEST_CASE("pyralis_02: the shore cache is behind a phase barrier") {
    PyRun r("pyralis_02", 538, 36, {"charge_shot"});
    int relics = r.w->stats().relics;
    face(r, 1);
    r.playSafe(60, [&](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 540.0f * T + 1.0f);
    r.playSafe(2, [&](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : PlayerInput{}; });
    r.playSafe(120, [&](Player&, int) { return in(1); });
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("pyralis_02: reaching the exit completes the level") {
    PyRun r("pyralis_02", 543, 32, {"charge_shot"});
    reachExit(r);
}

// =====================================================================================
// pyralis_03 — La Fonderie Primordiale
// =====================================================================================

TEST_CASE("pyralis_03: the first crucible is crossed with the grapple") {
    PyRun r("pyralis_03", 31, 50);
    CHECK(grappleChain(r, 1, 31.8f, 43.0f) == 1);
    CHECK(r.p().alive());
    CHECK(r.x() > 42.0f);
}

TEST_CASE("pyralis_03: the start cache is reached by two anchors") {
    PyRun r("pyralis_03", 20, 56);
    int relics = r.w->stats().relics;
    CHECK(grappleChain(r, 2, 21.0f, 11.0f, -1, 1800, true) == 2);
    r.playSafe(200, [&](Player& p, int) { return p.grounded() && p.box.center().x > 6.5f * T ? in(-1) : PlayerInput{}; });
    CHECK(r.p().alive());
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("pyralis_03: the casting chain is crossed platform by platform") {
    PyRun r("pyralis_03", 88, 56);
    std::vector<LevelEntity*> chain;
    for (float x : {94.0f, 100.0f, 106.0f, 112.0f, 118.0f, 124.0f, 130.0f, 136.0f})
        chain.push_back(findEntity(r, "moving_platform", x, 56.5f));
    for (auto* c : chain) CHECK(c != nullptr);
    if (std::find(chain.begin(), chain.end(), nullptr) != chain.end()) return;
    int target = 0;  // index of the platform to land on next (8 = end ledge)
    int jumps = 0;
    r.playSafe(5000, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (target < 8 && p.grounded() && std::abs(p.box.bottom() - chain[target]->box.y) < 2.0f &&
            p.box.right() > chain[target]->box.x + 2.0f && p.box.x < chain[target]->box.right() - 2.0f)
            ++target;  // landed on it
        float goal = target < 8 ? chain[target]->box.center().x / T : 148.0f;
        bool ready = target < 8 ? (target == 0 ? chain[0]->box.x < 93.4f * T : true)
                                : chain[7]->box.right() > 142.6f * T;
        if (p.grounded()) {
            if (target == 8 && cx > 147.0f) return PlayerInput{};
            const float edge = target == 0 ? 90.0f : chain[target - 1]->box.right() / T;
            if (!ready) {  // ride / wait
                float c = target == 0 ? 89.0f : chain[target - 1]->box.center().x / T;
                return in(cx < c - 0.4f ? 1 : (cx > c + 0.4f ? -1 : 0));
            }
            if (p.box.right() > edge * T - 3.0f) {
                ++jumps;
                return in(1, {Action::Jump}, {Action::Jump});
            }
            return in(1);
        }
        // airborne: steer to the target
        return in(cx < goal - 0.3f ? 1 : (cx > goal + 0.3f ? -1 : 0), {},
                  p.vel.y < 0.0f ? std::initializer_list<Action>{Action::Jump} : std::initializer_list<Action>{});
    });
    CHECK(target == 8);
    CHECK(jumps >= 9);
    CHECK(r.p().alive());
    CHECK(r.x() > 146.0f);
    CHECK_NEAR(r.feet(), 56.0f, 0.1f);
}

TEST_CASE("pyralis_03: the housing roof holds the forgotten ingot") {
    PyRun r("pyralis_03", 147, 56);
    int relics = r.w->stats().relics;
    CHECK(grappleChain(r, 3, 147.5f, 150.0f, 1, 1800, true, -1) == 3);
    r.playSafe(2400, [&](Player& p, int) { return p.box.center().x > 91.0f * T ? in(-1) : PlayerInput{}; });
    CHECK(r.p().alive());
    CHECK(r.w->stats().relics == relics + 1);
    CHECK_NEAR(r.feet(), 34.0f, 0.1f);
}

TEST_CASE("pyralis_03: the crucible hall locks, sends three waves, then opens") {
    PyRun r("pyralis_03", 183, 56);
    r.playSafe(60, [&](Player&, int) { return in(1); });
    CHECK(r.aliveAwake() > 0);
    r.playSafe(300, [&](Player&, int) { return in(1); });
    CHECK(r.x() < 230.0f);
    clearWaves(r, 3);
    r.playSafe(600, [&](Player& p, int) { return p.box.center().x > 234.0f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 231.0f);
}

TEST_CASE("pyralis_03: plasma curtains need the shield, the phase barrier the teleport") {
    PyRun r("pyralis_03", 241, 56);
    r.playSafe(120, [&](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 244.0f * T + 1.0f);
    r.playSafe(1, [&](Player&, int) { return in(1, {Action::Shield}); });
    r.playSafe(200, [&](Player& p, int) { return p.box.right() > 249.7f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 245.0f);
    CHECK(r.x() < 250.0f);
    r.playSafe(2, [&](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : PlayerInput{}; });
    r.playSafe(30, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.p().alive());
    CHECK(r.x() > 253.0f);
    CHECK_NEAR(r.feet(), 56.0f, 0.1f);
    // ankle laser, then the second curtain once the shield has recharged
    r.p().teleportTo({270.0f * T, 56.0f * T});
    r.playSafe(600, [&](Player& p, int) { return p.box.right() > 275.5f * T ? PlayerInput{} : in(1); });
    CHECK(r.p().box.right() <= 276.0f * T + 1.0f);
    r.playSafe(1, [&](Player&, int) { return in(1, {Action::Shield}); });
    r.playSafe(200, [&](Player&, int) { return in(1); });
    CHECK(r.x() > 278.0f);
}

TEST_CASE("pyralis_03: the burning alcove heart: glide into the pit, shield through, updraft out") {
    PyRun r("pyralis_03", 249, 56);
    int maxHp = r.p().maxHealth();
    int phase = 0;
    r.playSafe(2400, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        float f = p.box.bottom() / T;
        switch (phase) {
            case 0:  // step into the pit, glide
                if (!p.grounded() && f > 57.0f) phase = 1;
                return in(cx < 251.5f ? 1 : 0, {}, {Action::Jump});
            case 1:
                if (f > 59.0f) {
                    phase = 2;
                    return in(0, {Action::Shield}, {Action::Jump});
                }
                return in(cx < 252.0f ? 1 : 0, {}, {Action::Jump});
            case 2:  // through the curtain, to the heart and the vent
                if (cx > 267.6f) {
                    phase = 3;
                    return in(0, {Action::Jump});
                }
                return in(1);
            case 3:
                if (f < 54.0f) phase = 4;
                return in(cx < 267.5f ? 1 : (cx > 268.5f ? -1 : 0), {}, {Action::Jump});
            default:
                if (p.grounded() && cx > 272.0f) return PlayerInput{};
                return in(1);
        }
    });
    CHECK(phase == 4);
    CHECK(r.p().alive());
    CHECK(r.p().maxHealth() == maxHp + 1);
    CHECK_NEAR(r.feet(), 56.0f, 0.1f);
}

TEST_CASE("pyralis_03: the burning chimneys lift the player to the hanging crucibles") {
    PyRun r("pyralis_03", 287, 56);
    CHECK(rideUpdraft(r, 289.8f, 292.0f, 31.0f, 295.5f));
    CHECK_NEAR(r.feet(), 34.0f, 0.1f);
    CHECK(rideUpdraft(r, 299.7f, 302.0f, 19.0f, 305.5f));
    CHECK(r.p().alive());
    CHECK(r.x() > 304.0f);
    CHECK_NEAR(r.feet(), 22.0f, 0.1f);
}

TEST_CASE("pyralis_03: an air dash breaks into the cog niche at the top of the chimney") {
    PyRun r("pyralis_03", 302, 44);
    int relics = r.w->stats().relics;
    int phase = 0;
    r.playSafe(1800, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            phase = 1;
            return in(0, {Action::Jump});
        }
        if (phase == 1) {
            if (p.box.bottom() < 12.8f * T) {
                phase = 2;
                return in(-1, {Action::Dash});
            }
            return in(cx < 301.5f ? 1 : (cx > 302.5f ? -1 : 0), {}, {Action::Jump});
        }
        return p.grounded() && cx < 297.0f ? PlayerInput{} : in(-1);
    });
    CHECK(phase == 2);
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("pyralis_03: the hanging crucible is crossed by three anchors") {
    PyRun r("pyralis_03", 310, 22);
    CHECK(grappleChain(r, 3, 311.6f, 342.5f) == 3);
    CHECK(r.p().alive());
    CHECK(r.x() > 341.0f);
    CHECK_NEAR(r.feet(), 26.0f, 0.1f);
}

TEST_CASE("pyralis_03: the press is descended by two ground pounds") {
    PyRun r("pyralis_03", 345, 26);
    r.playSafe(300, [&](Player&, int) { return in(1); });
    CHECK(r.x() < 358.0f);
    CHECK(r.feet() <= 26.1f);
    r.p().teleportTo({349.5f * T, 26.0f * T});
    r.playSafe(20, [&](Player&, int) { return PlayerInput{}; });
    jumpAndPound(r);
    CHECK_NEAR(r.feet(), 36.0f, 0.1f);
    r.p().teleportTo({352.0f * T, 36.0f * T});
    r.playSafe(20, [&](Player&, int) { return PlayerInput{}; });
    jumpAndPound(r);
    CHECK(r.p().alive());
    CHECK_NEAR(r.feet(), 46.0f, 0.1f);
}

TEST_CASE("pyralis_03: the press conduit receptor needs a charged shot; its raft crosses") {
    PyRun r("pyralis_03", 353.5f, 46);
    LevelEntity* raft = findEntity(r, "moving_platform", 376, 46.5f);
    CHECK(raft != nullptr);
    if (!raft) return;
    face(r, 1);
    r.p().teleportTo({353.6f * T, 46.0f * T});
    r.p().energy = 100;
    waveShot(r);
    CHECK(raft->debugInfo() == "arrêtée");
    r.p().energy = 100;
    chargeShot(r);
    CHECK(raft->debugInfo() == "en mouvement");
    int phase = 0;
    r.playSafe(3000, [&](Player& p, int) {
        float cx = p.box.center().x / T;
        if (phase == 0) {
            if (raft->box.x < 354.2f * T) phase = 1;
            return PlayerInput{};
        }
        if (phase == 1) {
            if (cx > 355.5f) phase = 2;
            return in(1);
        }
        if (phase == 2) {
            if (raft->box.right() > 377.8f * T) phase = 3;
            float rc = raft->box.center().x / T;
            return in(cx < rc - 0.4f ? 1 : (cx > rc + 0.4f ? -1 : 0));
        }
        if (p.grounded() && cx > 381.0f) return PlayerInput{};
        return in(1);
    });
    CHECK(phase == 3);
    CHECK(r.p().alive());
    CHECK(r.x() > 379.0f);
    CHECK_NEAR(r.feet(), 46.0f, 0.1f);
}

TEST_CASE("pyralis_03: the press chimney leads to the vertical forge") {
    PyRun r("pyralis_03", 385, 46);
    CHECK(rideUpdraft(r, 388.2f, 390.0f, 25.0f, 393.5f));
    CHECK(r.p().alive());
    CHECK_NEAR(r.feet(), 30.0f, 0.1f);
}

TEST_CASE("pyralis_03: the vertical forge needs dash, teleport and grapple") {
    PyRun r("pyralis_03", 396, 30);
    r.playSafe(120, [&](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 401.0f * T + 1.0f);
    r.playSafe(60, [&](Player& p, int i) {
        return i == 0 ? in(1, {Action::Dash}) : (p.box.right() > 409.7f * T ? PlayerInput{} : in(1));
    });
    r.playSafe(200, [&](Player& p, int) { return p.box.right() > 409.7f * T ? PlayerInput{} : in(1); });
    CHECK(r.p().alive());
    CHECK(r.x() > 404.0f);
    CHECK_NEAR(r.feet(), 30.0f, 0.1f);
    r.playSafe(2, [&](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : PlayerInput{}; });
    r.playSafe(30, [&](Player&, int) { return PlayerInput{}; });
    CHECK(r.x() > 413.5f);
    CHECK_NEAR(r.feet(), 30.0f, 0.1f);
    CHECK(grappleChain(r, 2, 422.6f, 439.5f) == 2);
    CHECK(r.p().alive());
    CHECK(r.x() > 438.0f);
    CHECK_NEAR(r.feet(), 34.0f, 0.1f);
}

TEST_CASE("pyralis_03: the Forgeron Primordial wakes, goes through three phases, dies and opens the way") {
    PyRun r("pyralis_03", 450, 40);
    r.playSafe(240, [&](Player&, int) { return in(1); });
    CHECK(fightBoss(r) == 2);
    r.playSafe(900, [&](Player& p, int) { return p.box.center().x > 501.0f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 499.0f);
}

TEST_CASE("pyralis_03: the crucible cliff is climbed with an updraft and a long jump") {
    PyRun r("pyralis_03", 506, 40);
    CHECK(rideUpdraft(r, 512.2f, 514.0f, 22.0f, 517.5f));
    CHECK_NEAR(r.feet(), 28.0f, 0.1f);
    int phase = 0;
    r.playSafe(900, [&](Player& p, int) {
        if (phase == 0 && p.box.right() > 523.7f * T) {
            phase = 1;
            return in(1, {Action::Jump}, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return in(1, {Action::Jump}, {Action::Jump});
        }
        if (p.grounded() && p.box.center().x > 534.0f * T) return PlayerInput{};
        return in(1, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.x() > 532.0f);
    CHECK_NEAR(r.feet(), 24.0f, 0.1f);
}

TEST_CASE("pyralis_03: the hidden second heart: pound, charged shot, shield, teleport, updraft") {
    PyRun r("pyralis_03", 539.5f, 24);
    int maxHp = r.p().maxHealth();
    jumpAndPound(r);
    CHECK_NEAR(r.feet(), 33.0f, 0.1f);
    face(r, 1);
    r.p().energy = 100;
    chargeShot(r);
    r.playSafe(300, [&](Player& p, int) { return p.box.right() > 547.6f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 544.5f);  // energy barrier gone
    r.playSafe(1, [&](Player&, int) { return in(1, {Action::Shield}); });
    r.playSafe(200, [&](Player& p, int) { return p.box.right() > 551.7f * T ? PlayerInput{} : in(1); });
    CHECK(r.x() > 549.0f);  // through the plasma
    r.p().energy = 100;
    r.playSafe(2, [&](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : PlayerInput{}; });
    CHECK(r.x() > 553.0f);
    r.playSafe(120, [&](Player& p, int) { return p.box.center().x > 555.2f * T ? in(-1) : PlayerInput{}; });
    CHECK(r.p().maxHealth() == maxHp + 1);
    CHECK(rideUpdraft(r, 556.5f, 558.0f, 22.0f, 561.0f));
    CHECK(r.p().alive());
    CHECK_NEAR(r.feet(), 24.0f, 0.1f);
}

TEST_CASE("pyralis_03: reaching the exit completes the level") {
    PyRun r("pyralis_03", 566, 24);
    reachExit(r);
}
