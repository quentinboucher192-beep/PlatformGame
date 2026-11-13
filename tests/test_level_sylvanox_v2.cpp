// Traversal checks of the Sylvanox levels 02 ("La Canopée des Spores") and 03 ("Le Cœur de la
// Machine"): every mandatory move is achievable with the intended powers, arenas lock and open,
// bosses wake, go through their phases, die and open the way, and the exits complete the levels.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/Entity.h"
#include "game/gameplay/GameWorld.h"
#include "game/powers/Powers.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
constexpr float T = 16.0f;
constexpr float DT = 1.0f / 120.0f;

struct Run {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    bool god = true;  // traversal checks: enemies must not knock the pilot around
    Run(const char* levelId, float tx, float ty, std::initializer_list<const char*> powers, bool startPowers = false) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel(levelId);
        CHECK(level.has_value());
        level->intro.steps.clear();
        if (!startPowers) level->startPowers.clear();
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->startAt({tx * T, ty * T});
        for (const char* p : powers) w->player().grantPower(*content.power(p));
        for (int i = 0; i < 20; ++i) w->fixedUpdate(PlayerInput{}, DT);  // settle
    }
    Player& p() { return w->player(); }
    int stepCount = 0;
    void trace() {
        static const bool on = std::getenv("SYLDBG") != nullptr;
        static const int every = std::getenv("SYLDBG_EVERY") ? std::atoi(std::getenv("SYLDBG_EVERY")) : 15;
        if (!on || (stepCount++ % every) != 0) return;
        dump();
    }
    void dump() {
        const Player& q = w->player();
        std::printf("  t=%5d x=%7.2f feet=%6.2f v=(%6.1f,%6.1f) g=%d glide=%d alive=%d hp=%d\n", stepCount, q.box.center().x / 16.0f,
                    q.box.bottom() / 16.0f, q.vel.x, q.vel.y, q.grounded() ? 1 : 0, q.gliding() ? 1 : 0, q.alive() ? 1 : 0,
                    q.health);
    }
    void play(int steps, const std::function<PlayerInput(Player&, int)>& ctrl) {
        for (int i = 0; i < steps; ++i) {
            if (god) p().setInvulnerable(0.5f);
            w->fixedUpdate(ctrl(p(), i), DT);
            trace();
        }
    }
    /// Plays until `done` returns true (or the step budget runs out); returns the steps used.
    int until(int maxSteps, const std::function<bool(Player&)>& done, const std::function<PlayerInput(Player&, int)>& ctrl) {
        for (int i = 0; i < maxSteps; ++i) {
            if (done(p())) {
                if (std::getenv("SYLDBG")) std::printf("  until: done after %d steps\n", i), dump();
                return i;
            }
            if (god) p().setInvulnerable(0.5f);
            w->fixedUpdate(ctrl(p(), i), DT);
            trace();
        }
        if (std::getenv("SYLDBG")) std::printf("  until: budget exhausted\n"), dump();
        return maxSteps;
    }
    int activeEnemies() {
        int n = 0;
        for (auto& e : w->enemies()) n += e->alive() && !e->dormant() ? 1 : 0;
        return n;
    }
    void killActive(float x0, float x1) {
        DamageInfo d;
        d.amount = 100;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        for (auto& e : w->enemies())
            if (e->alive() && !e->dormant() && e->center().x >= x0 * T && e->center().x <= x1 * T) {
                d.source = e->center();
                e->takeDamage(*w, d);
            }
    }
};

/// dir: -1 left, 0 none, 1 right.
PlayerInput in(int dir, std::initializer_list<Action> pressed = {}, std::initializer_list<Action> held = {}) {
    PlayerInput i;
    if (dir > 0) {
        i.held[static_cast<int>(Action::MoveRight)] = true;
        i.move.x = 1.0f;
    } else if (dir < 0) {
        i.held[static_cast<int>(Action::MoveLeft)] = true;
        i.move.x = -1.0f;
    }
    for (Action a : pressed) i.pressed[static_cast<int>(a)] = i.held[static_cast<int>(a)] = true;
    for (Action a : held) i.held[static_cast<int>(a)] = true;
    if (i.held[static_cast<int>(Action::MoveDown)]) i.move.y = 1.0f;
    if (i.held[static_cast<int>(Action::MoveUp)]) i.move.y = -1.0f;
    return i;
}

float cx(Player& p) { return p.box.center().x / T; }
float feet(Player& p) { return p.box.bottom() / T; }
bool standingOn(Player& p, float groundTile, float x0, float x1) {
    return p.grounded() && std::abs(p.box.bottom() - groundTile * T) < 3.0f && cx(p) > x0 && cx(p) < x1;
}

/// Running jump off a ledge edge: jump when the right side passes `edgeX`, optional double jump
/// near the apex, then glide (jump held) while drifting right until `stopX` (tile, centre).
struct GlideJump {
    float edgeX;
    bool doubleJump = false;
    float stopX = 1e9f;
    bool glide = true;
    int phase = 0;
    PlayerInput step(Player& p) {
        int dir = cx(p) < stopX ? 1 : 0;
        if (phase == 0) {
            if (p.box.right() > edgeX * T && p.grounded()) {
                phase = 1;
                return in(dir, {Action::Jump});
            }
            return in(dir);
        }
        if (phase == 1 && doubleJump && p.vel.y > -40.0f) {
            phase = 2;
            return in(dir, {Action::Jump});
        }
        return glide ? in(dir, {}, {Action::Jump}) : in(dir);
    }
};
/// Rightward grapple pilot: grabs every new anchor ahead (skipping `skip` anchors), glides between
/// them, double jumps after the release when `dj` is set, and folds the wings past `foldX`.
struct GrapplePilot {
    float lastX = -1e9f;
    float foldX = 1e9f;
    bool dj = true;
    std::vector<Vec2> skip;
    int pulls = 0;
    bool wasPulling = false;
    int sinceRelease = 999;
    bool startedPull = false;
    Vec2 last{-1e9f, -1e9f};
    PlayerInput step(Player& p, GameWorld& w) {
        bool pulling = p.movementPower() != nullptr;
        if (wasPulling && !pulling) sinceRelease = 0;
        if (pulling) startedPull = true;
        wasPulling = pulling;
        ++sinceRelease;
        if (pulling) return in(0);
        const LevelEntity* t = GrapplePower::findTarget(p, w, 170.0f);
        if (t) {
            Vec2 c = t->box.center();
            bool skipped = false;
            for (Vec2 s : skip) skipped |= distance(s * T, c) < 8.0f;
            if (!skipped && distance(c, last) > 8.0f && c.x > lastX - 4.0f && c.x > p.center().x - 24.0f) {
                lastX = c.x;
                last = c;
                ++pulls;
                return in(1, {Action::Interact});
            }
        }
        int dir = cx(p) < foldX ? 1 : 0;
        if (dj && sinceRelease == 12) return in(dir, {Action::Jump});
        if (p.grounded()) return in(dir);
        return cx(p) < foldX ? in(dir, {}, {Action::Jump}) : in(0);
    }
};
}  // namespace

// =========================================================================== sylvanox_02

TEST_CASE("sylvanox_02: zone 1 mushroom bounce reaches the upper canopy") {
    Run r("sylvanox_02", 61.6f, 61, {});
    int phase = 0;
    r.until(900, [](Player& p) { return standingOn(p, 50, 68.5f, 88); }, [&](Player& p, int) {
        if (phase == 0 && !p.grounded() && p.vel.y < -200.0f) phase = 1;  // launched by the pad
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return in(1, {Action::Jump});
        }
        return in(1);
    });
    CHECK(phase >= 1);
    CHECK(standingOn(r.p(), 50, 68.5f, 88));
}

TEST_CASE("sylvanox_02: zone 1 the wave dissolves the energy barrier") {
    Run r("sylvanox_02", 106, 47, {"wave"});
    r.play(120, [](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 110.0f * T + 1.0f);  // blocked
    r.play(60, [](Player&, int i) { return i == 0 ? in(0, {Action::Wave}) : PlayerInput{}; });
    r.play(120, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 111.5f);
}

TEST_CASE("sylvanox_02: zone 1 relic vault behind the phase barrier under the bridge") {
    Run r("sylvanox_02", 54, 58, {"teleport"});
    // walk off the bridge end and land on the root ledge below
    r.until(400, [](Player& p) { return standingOn(p, 64, 50, 61); }, [](Player&, int) { return in(1); });
    CHECK(standingOn(r.p(), 64, 50, 61));
    // walk left against the barrier, then blink through it
    r.until(400, [](Player& p) { return cx(p) < 51.0f && p.grounded(); }, [](Player&, int) { return in(-1); });
    r.play(60, [](Player&, int) { return in(-1); });
    CHECK(r.p().box.x >= 50.0f * T - 1.0f);
    int relics = r.w->stats().relics;
    r.play(3, [](Player&, int i) { return i == 0 ? in(-1, {Action::Teleport}) : in(-1); });
    CHECK(cx(r.p()) < 49.0f);
    r.play(120, [](Player&, int i) { return i < 50 ? in(1) : PlayerInput{}; });
    CHECK(r.w->stats().relics == relics + 1);
    // and back out
    r.play(200, [](Player&, int i) { return i == 100 ? in(1, {Action::Teleport}) : in(1); });
    CHECK(cx(r.p()) > 50.0f);
}

TEST_CASE("sylvanox_02: zone 2 glide tablet then gentle glide onto the mushroom cap") {
    Run r("sylvanox_02", 113, 47, {});
    CHECK(!r.p().hasPower("glide"));
    GlideJump g{200.0f};  // never jumps: just walks off the ledge, then glides
    g.phase = 3;
    g.stopX = 144.5f;
    bool hadGlide = false;
    r.until(1500, [](Player& p) { return p.grounded() && cx(p) > 129.0f; }, [&](Player& p, int) {
        hadGlide |= p.hasPower("glide");
        return g.step(p);
    });
    CHECK(hadGlide);
    CHECK(standingOn(r.p(), 53, 141, 149));
}

TEST_CASE("sylvanox_02: zone 2 the basin updraft lifts the glider to the belvedere") {
    Run r("sylvanox_02", 150, 62, {"glide"});
    int phase = 0;
    r.until(2400, [](Player& p) { return standingOn(p, 34, 160.3f, 176); }, [&](Player& p, int) {
        if (phase == 0) {
            if (cx(p) > 157.6f) {
                phase = 1;
                return in(0, {Action::Jump});
            }
            return in(1);
        }
        if (cx(p) > 161.0f) return in(0);  // above the belvedere: fold the wings
        if (p.box.bottom() < 32.0f * T) return in(1, {}, {Action::Jump});
        return in(p.vel.x > 20.0f ? -1 : 0, {}, {Action::Jump});
    });
    CHECK(standingOn(r.p(), 34, 160.3f, 176));
}

TEST_CASE("sylvanox_02: zone 2 the first gap needs the glide") {
    {
        Run r("sylvanox_02", 170, 34, {"glide"});
        GlideJump g{175.6f};
        r.until(2400, [](Player& p) { return p.grounded() && cx(p) > 190.0f; }, [&](Player& p, int) { return g.step(p); });
        CHECK(r.p().alive());
        CHECK(standingOn(r.p(), 40, 200, 218));
    }
    {
        // best effort without the glide: jump, double jump, dash
        Run r("sylvanox_02", 170, 34, {"dash"});
        int phase = 0;
        r.god = false;
        r.play(900, [&](Player& p, int) {
            if (phase == 0 && p.box.right() > 175.6f * T) {
                phase = 1;
                return in(1, {Action::Jump});
            }
            if (phase == 1 && p.vel.y > -40.0f) {
                phase = 2;
                return in(1, {Action::Jump}, {Action::Jump});
            }
            if (phase == 2 && p.vel.y > -20.0f) {
                phase = 3;
                return in(1, {Action::Dash});
            }
            return in(1, {}, {Action::Jump});
        });
        CHECK(!(r.p().alive() && standingOn(r.p(), 40, 200, 218)));
    }
}

TEST_CASE("sylvanox_02: zone 2 the abyss updraft lifts the glider to the windy peaks") {
    Run r("sylvanox_02", 212, 40, {"glide"});
    GlideJump g{217.6f};
    bool rising = false;
    r.until(3000, [](Player& p) { return standingOn(p, 30, 252, 268); }, [&](Player& p, int) {
        if (g.phase == 0) return g.step(p);
        if (!rising && cx(p) > 233.2f) rising = true;
        if (rising && p.box.bottom() > 20.0f * T) return in(p.vel.x > 20.0f ? -1 : 0, {}, {Action::Jump});
        return in(1, {}, {Action::Jump});
    });
    CHECK(rising);
    CHECK(standingOn(r.p(), 30, 252, 268));
}

TEST_CASE("sylvanox_02: zone 3 hollow tree arena locks, three waves, then opens") {
    Run r("sylvanox_02", 284, 52, {"wave"});
    r.play(60, [](Player&, int) { return in(1); });
    CHECK(r.activeEnemies() > 0);
    for (int round = 0; round < 12; ++round) {
        r.killActive(276, 328);
        r.play(130, [](Player&, int) { return PlayerInput{}; });
    }
    r.play(700, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 326.0f);
}

TEST_CASE("sylvanox_02: zone 3 heart chamber: climb the bark vines, shield through the plasma") {
    Run r("sylvanox_02", 333, 52, {"shield"});
    int hearts = r.p().maxHealth();
    // to the vines, then climb
    r.until(300, [](Player& p) { return cx(p) < 330.6f; }, [](Player&, int) { return in(-1); });
    r.until(1500, [](Player& p) { return standingOn(p, 26, 328, 333); }, [](Player&, int) {
        return in(0, {}, {Action::MoveUp});
    });
    CHECK(standingOn(r.p(), 26, 328, 333));
    r.play(200, [](Player&, int) { return in(-1); });
    CHECK(cx(r.p()) > 327.9f);  // the plasma curtain blocks without the shield
    CHECK(r.p().maxHealth() == hearts);
    r.play(400, [](Player&, int i) { return i == 0 ? in(0, {Action::Shield}) : in(-1); });
    CHECK(r.p().maxHealth() == hearts + 1);
}

TEST_CASE("sylvanox_02: zone 4 dash through the crystal curtain between the stumps") {
    Run r("sylvanox_02", 361.5f, 52, {"dash"});
    int phase = 0;
    r.until(900, [](Player& p) { return standingOn(p, 52, 375, 383); }, [&](Player& p, int) {
        if (phase == 0 && p.box.right() > 364.6f * T) {
            phase = 1;
            return in(1, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return in(1, {Action::Jump}, {Action::Jump});
        }
        if (phase == 2 && p.box.right() > 367.6f * T) {
            phase = 3;
            return in(1, {Action::Dash});
        }
        return in(1, {}, {Action::Jump});
    });
    CHECK(phase == 3);
    CHECK(standingOn(r.p(), 52, 375, 383));
}

TEST_CASE("sylvanox_02: zone 4 lower route glide past the spore cloud reaches the merge") {
    for (bool dj : {false, true}) {
        // the spore column across the gap slows the glide: a single jump ends on the catch net
        Run r("sylvanox_02", 379, 52, {"glide", "dash"});
        GlideJump g{382.6f, dj};
        r.until(2400, [](Player& p) { return p.grounded() && cx(p) > 384.0f; }, [&](Player& p, int) { return g.step(p); });
        CHECK(r.p().alive());
        if (!standingOn(r.p(), 53, 400, 418)) {
            // slowed by the spore column: caught by the crumbling root at the foot of the stump
            CHECK(standingOn(r.p(), 55, 396.5f, 400.5f));
            r.until(400, [](Player& p) { return standingOn(p, 53, 400, 418); }, [](Player& p, int) {
                return p.grounded() ? in(1, {Action::Jump}) : in(1, {}, {Action::Jump});
            });
        }
        CHECK(standingOn(r.p(), 53, 400, 418));
    }
}

TEST_CASE("sylvanox_02: zone 4 upper route: bounce, updraft, great branch and the crystal nook") {
    Run r("sylvanox_02", 336, 52, {"glide", "dash"});
    int phase = 0;
    r.until(4000, [](Player& p) { return standingOn(p, 12, 360.5f, 392); }, [&](Player& p, int) {
        if (phase == 0) {  // hop onto the bounce pad
            if (!p.grounded() && p.vel.y < -320.0f) phase = 1;
            if (p.grounded() && cx(p) > 338.6f) return in(0, {Action::Jump});
            return in(p.grounded() ? 1 : 0, {}, {Action::Jump});
        }
        if (phase == 1) {  // ride the bounce, glide right into the updraft column
            if (cx(p) > 351.2f && !p.grounded()) phase = 2;
            if (p.vel.y < 0.0f) return in(0, {}, {Action::Jump});
            return in(1, {}, {Action::Jump});
        }
        if (p.box.bottom() > 9.5f * T) return in(p.vel.x > 20.0f ? -1 : (cx(p) < 351.0f ? 1 : 0), {}, {Action::Jump});
        return in(1, {}, {Action::Jump});
    });
    CHECK(standingOn(r.p(), 12, 360.5f, 392));
    // along the branch to the nook: dash through its crystal wall
    int relics = r.w->stats().relics;
    r.p().teleportTo({390.0f * T, 12.0f * T});
    r.play(30, [](Player&, int) { return PlayerInput{}; });
    r.play(120, [](Player&, int i) { return i == 10 ? in(1, {Action::Dash}) : in(1); });
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("sylvanox_02: boss Spore Queen wakes, three phases, dies and opens the way") {
    Run r("sylvanox_02", 450, 58, {"wave"});
    r.play(300, [](Player&, int) { return in(1); });
    Enemy* boss = r.w->activeBoss();
    CHECK(boss != nullptr);
    CHECK(boss && boss->def().id == "spore_queen");
    int maxPhase = 0;
    for (int hit = 0; hit < 300 && boss && boss->alive(); ++hit) {
        DamageInfo d;
        d.amount = 2;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        d.source = boss->center() - Vec2{30, 0};
        boss->takeDamage(*r.w, d);
        r.play(30, [](Player& p, int) {
            p.health = 8;
            return PlayerInput{};
        });
        maxPhase = std::max(maxPhase, boss->phaseIndex());
        boss = r.w->activeBoss();
    }
    CHECK(maxPhase == 2);
    CHECK(r.w->activeBoss() == nullptr);
    r.play(900, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 504.0f);
}

TEST_CASE("sylvanox_02: zone 6 updraft chimney to the crown, exit completes the level") {
    Run r("sylvanox_02", 508, 58, {"glide"});
    int phase = 0;
    r.until(3000, [](Player& p) { return p.grounded() && feet(p) < 26.5f; }, [&](Player& p, int) {
        if (phase == 0) {
            if (cx(p) > 513.6f) {
                phase = 1;
                return in(0, {Action::Jump});
            }
            return in(1);
        }
        if (cx(p) > 517.5f) return in(1);
        if (p.box.bottom() > 25.0f * T) return in(p.vel.x > 20.0f ? -1 : 0, {}, {Action::Jump});
        return in(1, {}, {Action::Jump});
    });
    CHECK(feet(r.p()) < 26.5f);
    r.play(400, [](Player&, int) { return in(1); });
    CHECK(r.w->state() == WorldState::Outro || r.w->state() == WorldState::Completed);
    r.play(1800, [](Player&, int) { return PlayerInput{}; });
    CHECK(r.w->state() == WorldState::Completed);
}

// =========================================================================== sylvanox_03

TEST_CASE("sylvanox_03: zone 1 the receptor opens the facade door") {
    Run r("sylvanox_03", 29, 22, {"wave"});
    r.play(30, [](Player&, int i) { return i < 3 ? in(1) : PlayerInput{}; });
    r.play(60, [](Player&, int i) { return i == 0 ? in(0, {Action::Wave}) : PlayerInput{}; });
    r.play(240, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 41.0f);
    // and the door really was closed
    Run r2("sylvanox_03", 34, 22, {});
    r2.play(200, [](Player&, int) { return in(1); });
    CHECK(r2.p().box.right() <= 37.0f * T + 1.0f);
}

TEST_CASE("sylvanox_03: zone 1 the lift carries Nova down to hall 2") {
    Run r("sylvanox_03", 55, 22, {});
    r.until(400, [](Player& p) { return cx(p) > 59.6f; }, [](Player&, int) { return in(1); });
    r.until(1500, [](Player& p) { return p.grounded() && feet(p) > 39.5f; }, [](Player&, int) { return PlayerInput{}; });
    CHECK(feet(r.p()) > 39.5f && feet(r.p()) < 40.6f);
    r.play(240, [](Player&, int) { return in(1); });
    CHECK(standingOn(r.p(), 40, 64, 94));
}

TEST_CASE("sylvanox_03: zone 1 glide slalom down the laser shaft without a hit") {
    Run r("sylvanox_03", 91, 40, {"glide"});
    r.god = false;
    int hp = r.p().health;
    r.until(3000, [](Player& p) { return p.grounded() && feet(p) > 63.0f; }, [](Player& p, int) {
        float f = feet(p);
        float target = f < 47.2f ? 100.6f : (f < 54.2f ? 95.4f : 100.8f);
        if (p.grounded()) return in(1);
        int dir = cx(p) < target - 0.3f ? 1 : (cx(p) > target + 0.3f ? -1 : 0);
        return in(dir, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(r.p().health == hp);
    CHECK(standingOn(r.p(), 64, 94, 102));
    r.play(300, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 108.0f);  // out into the anchor hall
}

TEST_CASE("sylvanox_03: zone 1 crystal pocket in the laser shaft (glide then dash)") {
    Run r("sylvanox_03", 100.6f, 55, {"glide", "dash"});
    int relics = r.w->stats().relics;
    bool dashed = false;
    r.play(400, [&](Player& p, int) {
        if (!dashed && feet(p) > 58.4f) {
            dashed = true;
            return in(1, {Action::Dash}, {Action::Jump});
        }
        return in(dashed ? 1 : 0, {}, {Action::Jump});
    });
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("sylvanox_03: zone 2 grapple tablet and the tutorial anchors up to the high ledge") {
    Run r("sylvanox_03", 112, 64, {});
    CHECK(!r.p().hasPower("grapple"));
    GrapplePilot g;
    g.skip = {{146, 37.4f}};
    g.foldX = 139.0f;
    r.until(3000, [](Player& p) { return standingOn(p, 50, 137, 150); }, [&](Player& p, int) {
        if (!p.hasPower("grapple")) return in(1);
        if (p.grounded() && feet(p) > 63.0f && cx(p) < 122.0f && g.pulls == 0) return in(1);
        return g.step(p, *r.w);
    });
    CHECK(r.p().hasPower("grapple"));
    CHECK(g.pulls >= 2);
    CHECK(standingOn(r.p(), 50, 137, 150));
}

TEST_CASE("sylvanox_03: zone 2 the high ledge is out of reach without the grapple") {
    Run r("sylvanox_03", 121, 64, {"dash", "impulse", "glide"});
    int phase = 0;
    bool reached = false;
    r.play(900, [&](Player& p, int) {
        reached |= standingOn(p, 50, 137, 150);
        if (phase == 0 && p.box.right() > 123.6f * T) {
            phase = 1;
            return in(1, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return in(1, {Action::Jump}, {Action::Jump});
        }
        if (phase == 2 && p.vel.y > -30.0f) {
            phase = 3;
            return in(1, {Action::Impulse}, {Action::Jump});
        }
        return in(1, {}, {Action::Jump});
    });
    CHECK(!reached);
}

TEST_CASE("sylvanox_03: zone 2 hidden anchor up to the shelf (relic)") {
    Run r("sylvanox_03", 145, 50, {"grapple"});
    int relics = r.w->stats().relics;
    int phase = 0, wait = 0;
    r.until(900, [](Player& p) { return standingOn(p, 36, 141, 151); }, [&](Player& p, int) {
        if (phase == 0) {
            phase = 1;
            return in(0, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -60.0f && GrapplePower::findTarget(p, *r.w, 170.0f)) {
            phase = 2;
            return in(0, {Action::Interact}, {Action::Jump});
        }
        if (phase == 2 && p.movementPower() != nullptr) phase = 3;
        if (phase == 3 && p.movementPower() == nullptr) phase = 4, wait = 0;
        if (phase == 4 && ++wait == 10) {
            phase = 5;
            return in(0, {Action::Jump});
        }
        return in(0, {}, {Action::Jump});
    });
    CHECK(standingOn(r.p(), 36, 141, 151));
    r.play(120, [](Player&, int) { return in(-1); });
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("sylvanox_03: zone 2 rising anchor chain over the abyss to the balcony") {
    Run r("sylvanox_03", 147, 50, {"grapple", "glide"});
    GrapplePilot g;
    g.skip = {{146, 37.4f}};
    g.foldX = 187.0f;
    r.until(4000, [](Player& p) { return standingOn(p, 33, 186, 202); }, [&](Player& p, int) { return g.step(p, *r.w); });
    CHECK(g.pulls >= 4);
    CHECK(standingOn(r.p(), 33, 186, 202));
}

TEST_CASE("sylvanox_03: zone 3 foundry arena locks, three waves, then opens") {
    Run r("sylvanox_03", 213, 52, {"wave"});
    r.play(60, [](Player&, int) { return in(1); });
    CHECK(r.activeEnemies() > 0);
    for (int round = 0; round < 12; ++round) {  // burrowers dig out of reach for a moment
        r.killActive(205, 262);
        r.play(130, [](Player&, int) { return PlayerInput{}; });
    }
    r.play(700, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 257.0f);
}

TEST_CASE("sylvanox_03: zone 4 upper route: anchors over the acid, walls and lasers") {
    Run r("sylvanox_03", 274, 52, {"grapple", "glide"});
    GrapplePilot g;
    g.skip = {{304, 30.5f}, {336, 44}};
    g.foldX = 352.0f;
    r.until(6000, [](Player& p) { return p.grounded() && cx(p) > 347.0f && feet(p) < 52.5f; },
            [&](Player& p, int) { return g.step(p, *r.w); });
    CHECK(r.p().alive());
    CHECK(g.pulls >= 4);
    CHECK(r.p().grounded() && cx(r.p()) > 347.0f && feet(r.p()) < 52.5f);
}

TEST_CASE("sylvanox_03: zone 4 heart vault in the gallery ceiling (grapple + shield)") {
    Run r("sylvanox_03", 305, 44, {"grapple", "shield"});
    int hearts = r.p().maxHealth();
    int phase = 0, wait = 0;
    r.play(8, [](Player&, int) { return in(-1); });  // face the wall, at the ledge's left end
    r.play(30, [](Player&, int) { return PlayerInput{}; });
    r.until(900, [](Player& p) { return standingOn(p, 29, 302, 310); }, [&](Player& p, int) {
        if (phase == 0) {
            phase = 1;
            return in(0, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -60.0f && GrapplePower::findTarget(p, *r.w, 170.0f)) {
            phase = 2;
            return in(0, {Action::Interact}, {Action::Jump});
        }
        if (phase == 2 && p.movementPower() != nullptr) phase = 3;
        if (phase == 3 && p.movementPower() == nullptr) phase = 4, wait = 0;
        if (phase == 4 && ++wait == 10) {
            phase = 5;
            return in(0, {Action::Jump});
        }
        return in(0, {}, {Action::Jump});
    });
    CHECK(standingOn(r.p(), 29, 302, 310));
    r.play(120, [](Player&, int) { return in(1); });
    CHECK(r.p().maxHealth() == hearts);  // plasma curtain
    r.play(200, [](Player&, int i) { return i == 0 ? in(0, {Action::Shield}) : in(1); });
    CHECK(r.p().maxHealth() == hearts + 1);
}

TEST_CASE("sylvanox_03: zone 4 lower route: phase barrier, coolant pit, plasma, anchors up") {
    Run r("sylvanox_03", 263, 52, {"teleport", "shield", "grapple", "dash"});
    // through the hatch, to the phase barrier
    r.until(600, [](Player& p) { return p.grounded() && feet(p) > 69.0f && cx(p) > 273.5f; },
            [](Player&, int) { return in(1); });
    r.play(60, [](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 276.0f * T + 1.0f);
    r.play(30, [](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : in(1); });
    CHECK(cx(r.p()) > 277.0f);
    // the coolant pit: jump, double jump, dash
    int phase = 0;
    r.until(900, [](Player& p) { return p.grounded() && cx(p) > 296.5f; }, [&](Player& p, int) {
        if (phase == 0 && p.box.right() > 283.6f * T && p.grounded()) {
            phase = 1;
            return in(1, {Action::Jump});
        }
        if (phase == 1 && p.vel.y > -40.0f) {
            phase = 2;
            return in(1, {Action::Jump}, {Action::Jump});
        }
        if (phase == 2 && p.vel.y > 0.0f) {
            phase = 3;
            return in(1, {Action::Dash});
        }
        return in(1, {}, {Action::Jump});
    });
    CHECK(r.p().alive());
    CHECK(cx(r.p()) > 296.5f);
    // plasma curtain
    r.play(120, [](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 302.0f * T + 1.0f);
    r.play(160, [](Player&, int i) { return i == 0 ? in(0, {Action::Shield}) : in(1); });
    CHECK(cx(r.p()) > 303.0f);
    // along the corridor to the exit shaft, then up its anchors
    r.until(1500, [](Player& p) { return cx(p) > 351.2f; }, [](Player&, int) { return in(1); });
    GrapplePilot g;
    g.foldX = 356.0f;
    r.until(1500, [](Player& p) { return p.grounded() && feet(p) < 52.5f; }, [&](Player& p, int) {
        PlayerInput i = g.step(p, *r.w);
        if (p.box.bottom() > 52.0f * T && !p.grounded()) i.move.x = 0, i.held[static_cast<int>(Action::MoveRight)] = false;
        return i;
    });
    CHECK(g.pulls >= 2);
    CHECK(standingOn(r.p(), 52, 346, 372));
}

TEST_CASE("sylvanox_03: zone 4 phase vault at the end of the corridor (relic)") {
    Run r("sylvanox_03", 353.5f, 70, {"teleport"});
    int relics = r.w->stats().relics;
    r.play(120, [](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 356.0f * T + 1.0f);
    r.play(120, [](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : in(1); });
    CHECK(r.w->stats().relics == relics + 1);
}

TEST_CASE("sylvanox_03: zone 4 airlock: plasma curtain, anchor over the pit, phase barrier") {
    Run r("sylvanox_03", 366, 52, {"shield", "grapple", "teleport", "glide"});
    r.play(150, [](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 371.0f * T + 1.0f);
    r.play(30, [](Player&, int i) { return i == 0 ? in(0, {Action::Shield}) : in(1); });
    GrapplePilot g;
    g.dj = false;
    g.foldX = 383.0f;
    r.until(1200, [](Player& p) { return standingOn(p, 52, 382, 389); }, [&](Player& p, int) { return g.step(p, *r.w); });
    CHECK(standingOn(r.p(), 52, 382, 389));
    r.play(80, [](Player&, int) { return in(1); });
    CHECK(r.p().box.right() <= 389.0f * T + 1.0f);
    r.play(30, [](Player&, int i) { return i == 0 ? in(1, {Action::Teleport}) : in(1); });
    CHECK(cx(r.p()) > 390.0f);
    // down the shaft to the core hall checkpoint
    r.until(1500, [](Player& p) { return p.grounded() && feet(p) > 73.0f; }, [](Player&, int) { return in(1); });
    CHECK(standingOn(r.p(), 74, 394, 404));
}

TEST_CASE("sylvanox_03: boss Prime Sentinel wakes, three phases, dies and opens the way") {
    Run r("sylvanox_03", 397, 74, {"wave"});
    r.play(300, [](Player&, int) { return in(1); });
    if (std::getenv("SYLDBG"))
        for (auto& e : r.w->enemies())
            if (e->center().x > 396 * T)
                std::printf("  enemy %s uid=%u obj=%s by=%u at (%.1f,%.1f) alive=%d dormant=%d engaged=%d\n", e->def().id.c_str(), e->uid(), e->objectId().c_str(), e->summonedBy,
                            e->center().x / T, e->center().y / T, e->alive(), e->dormant(), e->engaged());
    Enemy* boss = r.w->activeBoss();
    CHECK(boss != nullptr);
    CHECK(boss && boss->def().id == "prime_sentinel");
    int maxPhase = 0;
    for (int hit = 0; hit < 300 && boss && boss->alive(); ++hit) {
        DamageInfo d;
        d.amount = 2;
        d.type = DamageType::Wave;
        d.fromPlayer = true;
        d.source = boss->center() - Vec2{30, 0};
        boss->takeDamage(*r.w, d);
        r.play(30, [](Player& p, int) {
            p.health = 8;
            return PlayerInput{};
        });
        maxPhase = std::max(maxPhase, boss->phaseIndex());
        boss = r.w->activeBoss();
    }
    CHECK(maxPhase == 2);
    CHECK(r.w->activeBoss() == nullptr);
    r.play(1200, [](Player&, int) { return in(1); });
    CHECK(cx(r.p()) > 453.5f);
}

TEST_CASE("sylvanox_03: zone 6 the lift rises to the exit, which completes the level") {
    Run r("sylvanox_03", 456, 74, {});
    r.until(400, [](Player& p) { return cx(p) > 460.6f; }, [](Player&, int) { return in(1); });
    r.until(2000, [](Player& p) { return p.grounded() && feet(p) < 50.5f; }, [](Player&, int) { return PlayerInput{}; });
    CHECK(feet(r.p()) < 50.5f);
    r.play(500, [](Player&, int) { return in(1); });
    CHECK(r.w->state() == WorldState::Outro || r.w->state() == WorldState::Completed);
    r.play(1800, [](Player&, int) { return PlayerInput{}; });
    CHECK(r.w->state() == WorldState::Completed);
}

TEST_CASE("sylvanox_03: zone 6 anchors are another way up to the exit terrace") {
    Run r("sylvanox_03", 457, 74, {"grapple"});
    GrapplePilot g;
    g.foldX = 470.0f;
    r.until(2000, [](Player& p) { return standingOn(p, 50, 463, 482); }, [&](Player& p, int) {
        PlayerInput i = g.step(p, *r.w);
        if (p.box.bottom() > 50.0f * T && !p.grounded() && g.pulls < 3) i.move.x = 0, i.held[static_cast<int>(Action::MoveRight)] = false;
        if (p.grounded() && g.pulls == 0) i.move.x = 0, i.held[static_cast<int>(Action::MoveRight)] = false;
        return i;
    });
    CHECK(g.pulls >= 3);
    CHECK(standingOn(r.p(), 50, 463, 482));
}

TEST_CASE("sylvanox_02: zone 2 without the glide, the basin vines lead back to the tablet") {
    Run r("sylvanox_02", 131, 62, {});
    r.until(300, [](Player& p) { return cx(p) < 128.9f; }, [](Player&, int) { return in(-1); });
    r.until(1500, [](Player& p) { return p.box.bottom() < 46.5f * T; }, [](Player&, int) {
        return in(0, {}, {Action::MoveUp});
    });
    r.play(200, [](Player&, int) { return in(-1, {}, {Action::MoveUp}); });
    CHECK(standingOn(r.p(), 47, 112, 128));
}
