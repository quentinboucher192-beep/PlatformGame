// The "Épreuves" (fourth level of each planet, tools/levelgen/challenge.py): the tightest
// rooms are played with the real engine in every one of them — the 12-tile gap (double jump
// + dash), the zigzag shaft (one-way ledges every 4 tiles) and the glide over the chasm.

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
const char* kLevels[] = {"sylvanox_04", "pyralis_04", "glacior_04"};

struct Trial {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    LevelData level;
    std::unique_ptr<GameWorld> w;

    explicit Trial(const char* id) {
        content.reload();
        res.init(nullptr, &audio);
        auto l = content.loadLevel(id);
        CHECK(l.has_value());
        level = *l;
        level.intro.steps.clear();
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(level);
    }
    /// Room of this type (first one), from the generator's metadata.
    bool room(const char* type, float& x, float& floor) const {
        if (!level.metadata.contains("rooms")) return false;
        for (const auto& r : level.metadata["rooms"])
            if (r.value("type", "") == type) {
                x = r.value("x", 0.0f);
                floor = r.value("floor", 0.0f);
                return true;
            }
        return false;
    }
    void spawn(float tx, float ty) {
        w->startAt({tx * T, ty * T});
        for (int i = 0; i < 20; ++i) step(PlayerInput{});
    }
    Player& p() { return w->player(); }
    void step(const PlayerInput& in) {
        w->fixedUpdate(in, DT);
        // The trials test the rooms, not the fights.
        for (auto& e : w->enemies())
            if (e->alive() && !e->dormant()) e->kill(*w);
        p().setInvulnerable(1.0f);
    }
    float x() { return p().feet().x / T; }
    float y() { return p().feet().y / T; }
};

PlayerInput right(bool jumpPressed = false, bool jumpHeld = false) {
    PlayerInput in;
    in.held[static_cast<int>(Action::MoveRight)] = true;
    in.move.x = 1.0f;
    if (jumpPressed) in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
    if (jumpHeld) in.held[static_cast<int>(Action::Jump)] = true;
    return in;
}
PlayerInput toward(float dir) {
    PlayerInput in;
    if (dir > 0.1f) in.held[static_cast<int>(Action::MoveRight)] = true;
    if (dir < -0.1f) in.held[static_cast<int>(Action::MoveLeft)] = true;
    in.move.x = dir > 0.1f ? 1.0f : (dir < -0.1f ? -1.0f : 0.0f);
    return in;
}
}  // namespace

TEST_CASE("challenges: the 12-tile gap is crossed with a double jump and a dash") {
    for (const char* id : kLevels) {
        Trial t(id);
        float rx = 0, f = 0;
        CHECK(t.room("dash_gap", rx, f));
        t.spawn(rx + 2, f);
        int phase = 0;
        for (int i = 0; i < 600; ++i) {
            PlayerInput in = right();
            if (phase == 0 && t.x() > rx + 5.6f) {
                in = right(true);
                phase = 1;
            } else if (phase == 1) {
                in = right(false, true);
                if (t.p().vel.y > -40.0f) {
                    in = right(true);
                    phase = 2;
                }
            } else if (phase == 2) {
                in = right(false, true);
                if (t.p().vel.y > 20.0f) {
                    in = right();
                    in.pressed[static_cast<int>(Action::Dash)] = in.held[static_cast<int>(Action::Dash)] = true;
                    phase = 3;
                }
            }
            t.step(in);
            if (phase == 3 && t.p().grounded()) break;
        }
        std::printf("  %s : arrivée x=%.1f (rive opposée à %.0f)\n", id, t.x(), rx + 18);
        CHECK(t.p().grounded());
        CHECK(t.x() > rx + 18.0f);
        CHECK(t.y() < f + 0.5f);
    }
}

TEST_CASE("challenges: the zigzag shaft is climbed ledge by ledge") {
    for (const char* id : kLevels) {
        Trial t(id);
        float rx = 0, f = 0;
        CHECK(t.room("climb", rx, f));
        t.spawn(rx + 4, f);
        // Ledges: left (x+1..x+6) at f-4 and f-12, right (x+6..x+11) at f-8 and f-16.
        // Each hop: walk to the take-off point on the current support, then jump + double
        // jump toward the next ledge.
        struct Hop {
            float takeoff, aim, ledge;  // aim: centre of the next ledge (steered during the jump)
        };
        const Hop hops[4] = {{rx + 4.0f, rx + 3.5f, f - 4}, {rx + 5.2f, rx + 8.5f, f - 8}, {rx + 6.8f, rx + 3.5f, f - 12},
                             {rx + 5.2f, rx + 8.5f, f - 16}};
        int k = 0, jumpTick = -1;
        for (int i = 0; i < 6000 && k < 4; ++i) {
            const Hop& h = hops[k];
            PlayerInput in;
            if (jumpTick < 0) {
                float dx = h.takeoff - t.x();
                in = toward(std::abs(dx) > 0.25f ? dx : 0.0f);
                if (t.p().grounded() && std::abs(dx) <= 0.25f) jumpTick = 0;
            } else {
                float d = h.aim - t.x();
                in = toward(std::abs(d) > 0.3f ? d : 0.0f);
                if (jumpTick == 0 || jumpTick == 30) in.pressed[static_cast<int>(Action::Jump)] = true;
                if (jumpTick < 60) in.held[static_cast<int>(Action::Jump)] = true;
                ++jumpTick;
                if (jumpTick > 20 && t.p().grounded()) {
                    if (std::abs(t.y() - h.ledge) < 0.3f) ++k;
                    jumpTick = -1;
                }
            }
            t.step(in);
            if (std::getenv("COSMIC_DEBUG_CLIMB") && i % 10 == 0)
                std::printf("    t=%d k=%d jt=%d x=%.2f y=%.2f g=%d st=%d\n", i, k, jumpTick, t.x(), t.y(), t.p().grounded(),
                            static_cast<int>(t.p().state()));
        }
        // Then out to the right on the top floor.
        for (int i = 0; i < 300 && t.x() < rx + 14.5f; ++i) t.step(right());
        for (int i = 0; i < 30; ++i) t.step(PlayerInput{});
        std::printf("  %s : corniches %d / 4, x=%.1f y=%.1f\n", id, k, t.x(), t.y());
        CHECK(k == 4);
        CHECK(t.x() > rx + 13.0f);
        CHECK(std::abs(t.y() - (f - 16)) < 0.5f);
    }
}

TEST_CASE("challenges: the chasm is crossed by gliding from the high ledge") {
    for (const char* id : kLevels) {
        Trial t(id);
        float rx = 0, f = 0;
        if (!t.room("glide", rx, f)) continue;  // not every trial has this room
        float high = std::max(20.0f, f - 10.0f);
        t.spawn(rx + 11, high);
        int phase = 0;
        for (int i = 0; i < 1500; ++i) {
            PlayerInput in = right();
            if (phase == 0 && t.x() > rx + 13.3f) {
                in = right(true);
                phase = 1;
            } else if (phase == 1) {
                in = right(false, true);
                if (t.p().vel.y > -30.0f) {
                    in = right(true);
                    phase = 2;
                }
            } else if (phase == 2) {
                // Hold jump while falling: glide.
                in = right(false, t.p().vel.y > 0.0f || i % 2 == 0);
                if (t.p().vel.y > 0.0f) phase = 3;
            } else if (phase == 3) {
                in = right(false, true);
            }
            t.step(in);
            if (phase == 3 && t.p().grounded()) break;
        }
        std::printf("  %s : arrivée x=%.1f (rive à %.0f)\n", id, t.x(), rx + 48);
        CHECK(t.p().grounded());
        CHECK(t.x() > rx + 48.0f);
    }
}

TEST_CASE("challenges: the guardian's arena opens once it falls, then the exit completes the trial") {
    for (const char* id : kLevels) {
        Trial t(id);
        float rx = 0, f = 0;
        CHECK(t.room("boss", rx, f));
        t.spawn(rx - 2, f);
        bool sawBoss = false;
        for (int i = 0; i < 120 * 40 && t.w->state() != WorldState::Completed; ++i) {
            for (const auto& e : t.w->enemies())
                if (e->def().isBoss) sawBoss = true;
            t.step(right());
        }
        std::printf("  %s : boss vu %d, état %d, x=%.1f\n", id, sawBoss, static_cast<int>(t.w->state()), t.x());
        CHECK(sawBoss);
        CHECK(t.w->state() == WorldState::Completed);
    }
}
