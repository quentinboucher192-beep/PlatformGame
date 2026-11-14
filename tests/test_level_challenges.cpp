// The "Épreuves" (fourth level of each planet, tools/levelgen/challenge.py), each built on a
// signature mechanic of its planet. Their key rooms are played with the real engine by small
// scripted bots: the timing windows, the levers, the walls broken in mid-air, the floors
// broken with ground pounds, the slippery ice and the slabs that crack at once all leave a
// way through.

#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/Entity.h"
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
    int laserHits = 0;
    int ticks = 0;
    std::map<std::string, float> offSince;  // laser id -> world time it switched off

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
        w->autoPowerChoice = 1;  // full power slots: take the tablet, leave the first power
        // The zone tests start anywhere: they get the powers of the level's stations (2.3 power slots).
        for (const auto& so : w->level().objects)
            if (so.type == "power_unlock" && so.id.rfind("station_", 0) == 0)
                if (const PowerDefinition* sd = w->content().power(so.propStr("power"))) w->player().grantPower(*sd);
    }
    /// n-th room of this type, from the generator's metadata.
    bool room(const char* type, float& x, float& floor, int nth = 0) const {
        if (!level.metadata.contains("rooms")) return false;
        for (const auto& r : level.metadata["rooms"])
            if (r.value("type", "") == type && nth-- == 0) {
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
    static Rect beam(const LevelEntity& e) {
        const LevelObject& o = e.object();
        Vec2 c = o.rect.center();
        bool vertical = std::abs(std::fmod(o.rotation, 180.0f)) > 45.0f;
        return vertical ? Rect::fromCenter(c, {o.rect.h, o.rect.w}) : o.rect;
    }
    void step(const PlayerInput& in) {
        w->fixedUpdate(in, DT);
        ++ticks;
        // The rooms are tested, not the fights.
        for (auto& e : w->enemies())
            if (e->alive() && !e->dormant()) e->kill(*w);
        for (auto& e : w->entities()) {
            if (e->object().type != "laser") continue;
            bool on = e->debugInfo() == "actif";
            if (!on && !offSince.count(e->id())) offSince[e->id()] = w->time();
            if (on) offSince.erase(e->id());
            if (on && beam(*e).expanded(-4.0f).intersects(p().box)) ++laserHits;
        }
        p().setInvulnerable(1.0f);
        if (std::getenv("COSMIC_DEBUG_TRIAL") && ticks % 12 == 0)
            std::printf("    t=%.2f x=%.2f y=%.2f v=(%.0f,%.0f) sol=%d état=%d\n", ticks / 120.0, x(), y(), p().vel.x, p().vel.y,
                        p().grounded(), static_cast<int>(p().state()));
    }
    float x() { return p().feet().x / T; }
    float y() { return p().feet().y / T; }
    /// Seconds the laser over (x, y) tiles has been off (-1 while on).
    float laserOff(float tx, float ty) {
        for (auto& e : w->entities()) {
            if (e->object().type != "laser") continue;
            if (!beam(*e).contains({tx * T, ty * T})) continue;
            auto it = offSince.find(e->id());
            return it == offSince.end() ? -1.0f : w->time() - it->second;
        }
        return 99.0f;
    }
};

PlayerInput move(float dir, bool jumpPressed = false, bool jumpHeld = false) {
    PlayerInput in;
    if (dir > 0.1f) in.held[static_cast<int>(Action::MoveRight)] = true;
    if (dir < -0.1f) in.held[static_cast<int>(Action::MoveLeft)] = true;
    in.move.x = dir > 0.1f ? 1.0f : (dir < -0.1f ? -1.0f : 0.0f);
    if (jumpPressed) in.pressed[static_cast<int>(Action::Jump)] = in.held[static_cast<int>(Action::Jump)] = true;
    if (jumpHeld) in.held[static_cast<int>(Action::Jump)] = true;
    return in;
}
PlayerInput press(Action a, float dir = 0.0f) {
    PlayerInput in = move(dir);
    in.pressed[static_cast<int>(a)] = in.held[static_cast<int>(a)] = true;
    return in;
}

/// Runs right and hops from support to support: at each edge (x in tiles) it jumps, and with
/// `airJumps` > 0 it jumps again at the apex. `glide` holds jump while falling.
bool hopRight(Trial& t, const std::vector<float>& edges, float finish, float finishFloor, int airJumps, bool glide = false,
              int maxTicks = 120 * 30, const std::vector<float>& aims = {}) {
    size_t k = 0;
    int jumpTick = -1, used = 0;
    for (int i = 0; i < maxTicks; ++i) {
        PlayerInput in = move(1.0f);
        if (jumpTick < 0 && k < edges.size() && t.x() > edges[k] - 0.35f && t.p().grounded()) {
            in = move(1.0f, true);
            jumpTick = 0;
            used = 0;
            ++k;
        } else if (jumpTick >= 0) {
            ++jumpTick;
            // Steer to the middle of the next support when one is given (narrow peaks).
            float dir = 1.0f;
            if (k - 1 < aims.size()) {
                float d = aims[k - 1] - t.x();
                dir = std::abs(d) > 0.2f ? d : 0.0f;
            }
            in = move(dir, false, jumpTick < 24 || glide);
            // Air jumps only when still short of the next support (with aims), else at the apex.
            bool shortOf = k - 1 >= aims.size() || aims[k - 1] - t.x() > 1.2f;
            if (used < airJumps && jumpTick > 10 && t.p().vel.y > -30.0f && shortOf) {
                in = move(dir, true);
                ++used;
            }
            if (jumpTick > 8 && t.p().grounded()) jumpTick = -1;
        }
        t.step(in);
        if (t.y() > finishFloor + 2.5f) return false;  // fell
        if (t.x() > finish && t.p().grounded()) return true;
    }
    return false;
}
}  // namespace

// ------------------------------------------------------------------ Sylvanox

TEST_CASE("trials: Sylvanox — the beat halls are crossed one gap per beat, never touching a laser") {
    for (const char* type : {"metronome", "metronome_fast"}) {
        Trial t("sylvanox_04");
        float rx = 0, f = 0;
        CHECK(t.room(type, rx, f));
        const int n = 6;
        const float slab = std::string(type) == "metronome" ? 3.0f : 2.0f;
        // Cross a gap this long after its laser goes off (the fast hall leaves less time on
        // each slab: cross right away).
        const float delta = std::string(type) == "metronome" ? 0.35f : 0.1f;
        t.spawn(rx + 1.5f, f);
        // Edges: start ledge, then the end of every slab; gap i's laser is at its middle.
        std::vector<float> edge, beamX;
        edge.push_back(rx + 2.0f);
        beamX.push_back(rx + 3.0f);
        for (int i = 0; i < n; ++i) {
            float cx = rx + 4.0f + static_cast<float>(i) * (slab + 2.0f);
            edge.push_back(cx + slab);
            if (i + 1 < n) beamX.push_back(cx + slab + 1.0f);
        }
        const float finish = rx + 6.0f + static_cast<float>(n) * (slab + 2.0f) + 1.0f;
        size_t k = 0;
        int jumpTick = -1;
        bool ok = false;
        for (int i = 0; i < 120 * 40 && !ok; ++i) {
            PlayerInput in = move(1.0f);
            if (jumpTick < 0 && k < edge.size()) {
                float d = edge[k] - 0.3f - t.x();
                if (d < 0.05f && t.p().grounded()) {
                    bool open = k >= beamX.size() || t.laserOff(beamX[k], f - 1.0f) >= delta;
                    if (open) {
                        in = move(1.0f, true);
                        jumpTick = 0;
                        ++k;
                    } else {
                        in = move(0.0f);
                    }
                }
            } else if (jumpTick >= 0) {
                // Steer to the middle of the next slab (the far shore after the last one).
                float target = k <= static_cast<size_t>(n) ? rx + 4.0f + static_cast<float>(k - 1) * (slab + 2.0f) + slab * 0.5f : finish;
                float d = target - t.x();
                in = move(std::abs(d) > 0.15f ? d : 0.0f, false, ++jumpTick < 14);
                if (jumpTick > 6 && t.p().grounded()) jumpTick = -1;
            }
            t.step(in);
            if (t.y() > f + 2.0f) break;  // fell into the acid
            ok = t.x() > finish && t.p().grounded();
        }
        std::printf("  %s : arrivée %d, x=%.1f / %.0f, lasers touchés %d\n", type, ok, t.x(), finish, t.laserHits);
        CHECK(ok);
        CHECK(t.laserHits == 0);
    }
}

TEST_CASE("trials: Sylvanox — the hall of levers is crossed by swapping the laser curtains") {
    Trial t("sylvanox_04");
    float rx = 0, f = 0;
    CHECK(t.room("lever_hall", rx, f));
    t.spawn(rx + 1, f);
    // Curtain 1 is closed at the start: the way is barred until the first lever.
    CHECK(t.laserOff(rx + 9.5f, f - 2.0f) < 0.0f);
    std::vector<float> levers;
    for (int i = 0; i < 5; ++i) levers.push_back(rx + 3.0f + i * 6.0f + 4.0f);
    size_t k = 0;
    int wait = 0;
    const float finish = rx + 3.0f + 36.0f + 1.5f;
    bool ok = false;
    for (int i = 0; i < 120 * 30 && !ok; ++i) {
        PlayerInput in = move(1.0f);
        if (k < levers.size() && t.x() > levers[k] - 0.2f) {
            in = wait == 0 ? press(Action::Interact) : move(0.0f);
            if (++wait > 6) {
                ++k;
                wait = 0;
            }
        } else if (t.p().grounded()) {
            // Hop the 1-tile gap under each curtain.
            float g = rx + 3.0f + 6.0f * std::floor((t.x() - (rx + 3.0f) + 0.6f) / 6.0f);
            if (t.x() > g - 0.6f && t.x() < g) in = move(1.0f, true);
        }
        t.step(in);
        if (t.y() > f + 2.0f) break;
        ok = t.x() > finish && t.p().grounded();
    }
    std::printf("  leviers %zu / 5, arrivée %d, x=%.1f, lasers touchés %d\n", k, ok, t.x(), t.laserHits);
    CHECK(ok);
    CHECK(t.laserHits == 0);
}

TEST_CASE("trials: Sylvanox — crystal walls are broken by a dash in mid-air") {
    Trial t("sylvanox_04");
    float rx = 0, f = 0;
    CHECK(t.room("dash_walls", rx, f));
    t.spawn(rx + 2, f);
    float x = rx + 5;
    for (float gap : {8.0f, 9.0f, 9.0f}) {
        float edge = x;
        float landing = x + gap;
        int phase = 0;
        bool landed = false;
        for (int i = 0; i < 1200 && !landed; ++i) {
            PlayerInput in = move(1.0f);
            if (phase == 0 && t.x() > edge - 0.4f && t.p().grounded()) {
                in = move(1.0f, true);
                phase = 1;
            } else if (phase == 1) {
                in = move(1.0f, false, true);
                if (t.p().vel.y > -40.0f) {
                    in = move(1.0f, true);
                    phase = 2;
                }
            } else if (phase == 2) {
                in = move(1.0f, false, true);
                if (t.x() > landing - 3.0f) {
                    in = press(Action::Dash, 1.0f);
                    phase = 3;
                }
            }
            t.step(in);
            landed = phase == 3 && t.p().grounded();
            if (t.y() > f + 2.0f) break;
        }
        std::printf("  mur à %.0f : x=%.1f y=%.1f\n", landing, t.x(), t.y());
        CHECK(landed);
        CHECK(t.x() > landing);
        x = landing + 6.0f;
    }
}

// ------------------------------------------------------------------- Pyralis

TEST_CASE("trials: Pyralis — the crucible is entered by ground-pounding three cracked floors, one after the other") {
    Trial t("pyralis_04");
    float rx = 0, f = 0;
    CHECK(t.room("pound", rx, f));
    t.spawn(rx + 8, f);
    // Where to pound on each level (the cracked slab moves from one side to the other).
    const float spots[3] = {rx + 8.0f, rx + 18.0f, rx + 4.0f};
    for (int level = 0; level < 3; ++level) {
        float before = t.y();
        int jumpTick = -1;
        bool done = false;
        for (int i = 0; i < 120 * 8 && !done; ++i) {
            float dx = spots[level] - t.x();
            PlayerInput in = move(std::abs(dx) > 0.3f ? dx : 0.0f);
            if (level == 1 && t.p().grounded() && t.x() > rx + 10.5f && t.x() < rx + 11.2f && jumpTick < 0) {
                in = move(1.0f, true);  // over the lava pocket
                jumpTick = 0;
            }
            if (jumpTick >= 0 && jumpTick < 40) in.held[static_cast<int>(Action::Jump)] = true;
            if (std::abs(dx) <= 0.3f && t.p().grounded() && jumpTick < 0) {
                in = move(0.0f, true);
                jumpTick = 0;
            } else if (jumpTick >= 0) {
                ++jumpTick;
                if (std::abs(dx) <= 0.6f && jumpTick == 20) {
                    in = press(Action::Attack);
                    in.move.y = 1.0f;
                    in.held[static_cast<int>(Action::MoveDown)] = true;
                }
                if (jumpTick > 30 && t.p().grounded()) jumpTick = -1;
            }
            t.step(in);
            done = t.p().grounded() && t.y() > before + 4.0f;
        }
        std::printf("  dalle %d : y %.1f -> %.1f\n", level + 1, before, t.y());
        CHECK(done);
        CHECK(t.y() < before + 5.5f);  // one floor at a time
    }
    for (int i = 0; i < 600 && t.x() < rx + 23.0f; ++i) t.step(move(1.0f));
    CHECK(t.x() > rx + 23.0f);
    CHECK(std::abs(t.y() - (f + 15)) < 0.5f);
}

TEST_CASE("trials: Pyralis — the heat chimneys carry a glider to the high ledge, under the spiked vault") {
    Trial t("pyralis_04");
    float rx = 0, f = 0;
    CHECK(t.room("vents", rx, f));
    t.spawn(rx + 1.5f, f);
    int hurt = 0;
    bool ok = false;
    for (int i = 0; i < 120 * 12 && !ok; ++i) {
        int before = t.p().health;
        // Glide (hold jump) until above the high ledge, then drop onto it.
        PlayerInput in = move(1.0f, i == 30, i > 30 && t.x() < rx + 37.5f);
        t.step(in);
        if (t.p().health < before) ++hurt;
        if (t.y() > f + 2.5f) break;
        ok = t.x() > rx + 39.0f && t.p().grounded() && std::abs(t.y() - (f - 6)) < 0.5f;
    }
    std::printf("  cheminées : arrivée %d, x=%.1f y=%.1f, blessé %d\n", ok, t.x(), t.y(), hurt);
    CHECK(ok);
}

// ------------------------------------------------------------------- Glacior

TEST_CASE("trials: Glacior — the ice slope, the cracking bridges and the ice peaks are crossed without stopping") {
    {
        Trial t("glacior_04");
        float rx = 0, f = 0;
        CHECK(t.room("ice_slope", rx, f));
        t.spawn(rx + 1, f);
        bool ok = hopRight(t, {rx + 20.0f}, rx + 31.0f, f + 8, 2, true);
        std::printf("  pente : %d (x=%.1f y=%.1f)\n", ok, t.x(), t.y());
        CHECK(ok);
    }
    for (const char* type : {"frail_bridges", "frail_bridges_fast"}) {
        Trial t("glacior_04");
        float rx = 0, f = 0;
        CHECK(t.room(type, rx, f));
        t.spawn(rx + 1, f);
        std::vector<float> edges{rx + 3.0f};
        for (int i = 0; i < 7; ++i) edges.push_back(rx + 7.0f + static_cast<float>(i) * 6.0f + 2.0f);
        float finish = rx + 3.0f + 7 * 6.0f + 3.0f + 1.0f;
        bool ok = hopRight(t, edges, finish, f, 0);
        std::printf("  %s : %d (x=%.1f / %.0f)\n", type, ok, t.x(), finish);
        CHECK(ok);
    }
    {
        Trial t("glacior_04");
        float rx = 0, f = 0;
        CHECK(t.room("ice_peaks", rx, f));
        t.spawn(rx + 1, f);
        std::vector<float> edges{rx + 4.0f}, aims;
        float px = rx + 4.0f;
        for (int i = 0; i < 6; ++i) {
            px += i == 0 ? 5.0f : 6.0f + static_cast<float>(i % 2);
            edges.push_back(px + 1.4f);
            aims.push_back(px + 1.0f);
        }
        aims.push_back(rx + 48.0f);
        bool ok = hopRight(t, edges, rx + 47.0f, f, 2, false, 120 * 30, aims);
        std::printf("  pics : %d (x=%.1f y=%.1f)\n", ok, t.x(), t.y());
        CHECK(ok);
    }
}

// --------------------------------------------------------------------- all

TEST_CASE("trials: the guardian's arena opens once it falls, then the exit completes the trial") {
    for (const char* id : kLevels) {
        Trial t(id);
        float rx = 0, f = 0;
        CHECK(t.room("boss", rx, f));
        t.spawn(rx - 2, f);
        bool sawBoss = false;
        for (int i = 0; i < 120 * 40 && t.w->state() != WorldState::Completed; ++i) {
            for (const auto& e : t.w->enemies())
                if (e->def().isBoss) sawBoss = true;
            t.step(move(1.0f));
        }
        std::printf("  %s : boss vu %d, état %d, x=%.1f\n", id, sawBoss, static_cast<int>(t.w->state()), t.x());
        CHECK(sawBoss);
        CHECK(t.w->state() == WorldState::Completed);
    }
}
