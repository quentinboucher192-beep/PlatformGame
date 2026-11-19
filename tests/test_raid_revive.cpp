// 2.75 (docs/RAIDS.md §20, docs/COOP.md §1): the players pass through each other (raids and
// cooperation 2), a player's fall never moves nor hurts anybody else, a player down comes back only once
// revived (as many times as wanted in a raid, 2 per attempt in cooperation), the whole group down is a
// WIPE (no restart: spectators), the rising liquids span the view, the armory's choices replay exactly,
// and the administrator's test counts its simulated players without the administrator (0 to 7).

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/camera/Camera.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidTest.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

constexpr int R = static_cast<int>(Action::MoveRight);
constexpr int J = static_cast<int>(Action::Jump);

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

InputFrame right(bool jump = false) {
    InputFrame f;
    f.held |= 1u << R;
    f.moveX = 127;
    if (jump) f.held |= 1u << J;
    return f;
}

/// The frames of a raid step: `players` seats held, the others vacant; press / release edges.
struct Seats {
    std::vector<InputFrame> prev = std::vector<InputFrame>(raid::kMaxInstancePlayers);
    std::vector<InputFrame> frames(int players, std::vector<InputFrame> held) {
        held.resize(raid::kMaxInstancePlayers);
        for (int k = 0; k < raid::kMaxInstancePlayers; ++k) {
            InputFrame& f = held[static_cast<size_t>(k)];
            if (k >= players) {
                f = InputFrame{};
                f.flags = InputFrame::Vacant;
                continue;
            }
            f.pressed = f.held & ~prev[static_cast<size_t>(k)].held;
            f.released = prev[static_cast<size_t>(k)].held & ~f.held;
            prev[static_cast<size_t>(k)] = f;
        }
        return held;
    }
};

void killSeat(GameWorld& w, int i) {
    GameWorld::PlayerScope s(w, i);
    w.playerAt(i).kill(w);
}

std::string hex(uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

/// A team of the cooperation (2 or 3) on a flat test level.
std::unique_ptr<GameWorld> coopWorld(Svc& s, int n) {
    LevelData l = LevelData::makeEmpty("test_coop", "Test", "sylvanox");
    l.intro.steps.clear();
    auto w = std::make_unique<GameWorld>(s.ws(), WorldMode::Play);
    w->setTeamSize(n);
    w->setKeysPerSlot(true);
    w->load(l);
    w->seedGameplay(levelSeed("test_coop"));
    return w;
}

void idleTeam(GameWorld& w, int ticks) {
    std::vector<InputFrame> f(static_cast<size_t>(w.teamSize()));
    for (int t = 0; t < ticks; ++t) stepTeam(w, f.data(), static_cast<int>(f.size()), kSimDt);
}

/// 2.77 (rules 3): seat 0 holds « Interagir » (a revival is held).
InputFrame interact() {
    InputFrame f;
    f.held = 1u << static_cast<int>(Action::Interact);
    return f;
}
void holdTeam(GameWorld& w, int ticks) {
    std::vector<InputFrame> f(static_cast<size_t>(w.teamSize()));
    f[0] = interact();
    for (int t = 0; t < ticks; ++t) {
        f[0].pressed = t == 0 ? f[0].held : 0u;
        stepTeam(w, f.data(), static_cast<int>(f.size()), kSimDt);
    }
}

}  // namespace

TEST_CASE("2.75: a player's fall moves nobody else; they come back only once revived (cooperation 2 and 3)") {
    Svc s;
    for (int n : {2, 3}) {
        auto w = coopWorld(s, n);
        idleTeam(*w, 120);
        const float ground = w->playerAt(0).feet().y;
        for (int i = 0; i < n; ++i) w->playerAt(i).teleportTo({600.0f + 200.0f * static_cast<float>(i), ground});
        idleTeam(*w, 30);
        std::vector<Vec2> at;
        std::vector<int> hearts;
        for (int i = 0; i < n; ++i) {
            at.push_back(w->playerAt(i).feet());
            hearts.push_back(w->playerAt(i).health);
        }
        killSeat(*w, n - 1);
        idleTeam(*w, 1);
        for (int i = 0; i + 1 < n; ++i) {
            CHECK(std::abs(w->playerAt(i).feet().x - at[static_cast<size_t>(i)].x) < 0.01f);
            CHECK(w->playerAt(i).health == hearts[static_cast<size_t>(i)]);
        }
        idleTeam(*w, 300);  // its death, then its soul: never back by itself
        CHECK(w->member(n - 1).presence == GameWorld::Presence::Down);
        for (int i = 0; i + 1 < n; ++i) CHECK(std::abs(w->playerAt(i).feet().x - at[static_cast<size_t>(i)].x) < 0.01f);
        CHECK(w->state() == WorldState::Playing);
        // An ally near the soul, « Interagir » held 2 s (2.77): back, near it (2 revivals per attempt in cooperation).
        w->playerAt(0).teleportTo(w->member(n - 1).soul + Vec2{-20.0f, 0.0f});
        holdTeam(*w, 280);
        CHECK(w->member(n - 1).presence == GameWorld::Presence::Playing);
        CHECK(std::abs(w->playerAt(n - 1).feet().x - w->member(n - 1).soul.x) < 80.0f);
        CHECK(w->revivesLeft() == GameWorld::kTeamRevives - 1);
        CHECK(!w->revivesUnlimited());
        CHECK(!w->solidTeammates());  // 2.75: cooperation 2, the players pass through each other
    }
}

TEST_CASE("2.75 raid test: a simulated player falls at step N: the administrator is untouched at N+1, the bot comes back only revived") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    CHECK(b != nullptr);
    if (!b) return;
    raid::RaidRun::Test setup;
    setup.on = true;
    setup.invincible = true;  // the administrator only (2.75): the simulated players fall like players
    setup.players = 2;
    raid::TestSession session(s.ws(), *b, setup);
    CHECK(session.begin(1));
    GameWorld& w = session.run().world();
    for (int t = 0; t < 240; ++t) session.step(InputFrame{});
    const Vec2 admin = w.playerAt(0).feet();
    const int hearts = w.playerAt(0).health;
    // The bot into the void (before 2.75, an invincible administrator was sent back to the start).
    w.playerAt(1).teleportTo({w.playerAt(1).feet().x, w.level().effectiveKillY() + 64.0f});
    session.step(InputFrame{});
    CHECK(std::abs(w.playerAt(0).feet().x - admin.x) < 0.01f);
    CHECK(std::abs(w.playerAt(0).feet().y - admin.y) < 0.01f);
    CHECK(w.playerAt(0).health == hearts);
    for (int t = 0; t < 200; ++t) session.step(InputFrame{});
    CHECK(w.member(1).presence == GameWorld::Presence::Down);
    CHECK(std::abs(w.playerAt(0).feet().x - admin.x) < 0.01f);
    CHECK(session.run().zone() == 1);
    CHECK(!session.run().wiped());
    // The administrator stays near the soul: the bot is back (raids: as many revivals as wanted).
    w.playerAt(0).teleportTo(w.member(1).soul + Vec2{-16.0f, 0.0f});
    for (int t = 0; t < 300; ++t) session.step(interact());  // 2.77: « Interagir » held
    CHECK(w.member(1).presence == GameWorld::Presence::Playing);
    CHECK(w.member(1).revived == 1);
}

TEST_CASE("2.75 raid: unlimited revivals, the players pass through each other, a wipe makes spectators (no restart)") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    Seats seats;
    auto step = [&](std::vector<InputFrame> held = {}) {
        auto f = seats.frames(2, std::move(held));
        run.step(f.data(), static_cast<int>(f.size()), 0);
    };
    for (int t = 0; t < 180; ++t) step();
    GameWorld& w = run.world();
    CHECK(w.revivesUnlimited());
    CHECK(!w.solidTeammates());
    // Pass through: seat 1 walks through seat 0 without stopping or pushing it.
    const float ground = w.playerAt(0).feet().y;
    w.playerAt(1).teleportTo({w.playerAt(0).feet().x - 70.0f, ground});
    for (int t = 0; t < 30; ++t) step();
    const Vec2 still = w.playerAt(0).feet();
    bool crossed = false;
    for (int t = 0; t < 120; ++t) {
        step({InputFrame{}, right()});
        crossed = crossed || w.playerAt(1).box.intersects(w.playerAt(0).box);
    }
    CHECK(crossed);
    CHECK(std::abs(w.playerAt(0).feet().x - still.x) < 0.5f);
    // Seat 1 down three times, revived three times (more than the cooperation's 2).
    for (int round = 0; round < 3; ++round) {
        killSeat(w, 1);
        for (int t = 0; t < 200; ++t) step();
        CHECK(w.member(1).presence == GameWorld::Presence::Down);
        w.playerAt(0).teleportTo(w.member(1).soul + Vec2{-16.0f, 0.0f});
        for (int t = 0; t < 300; ++t) step({interact()});  // 2.77: « Interagir » held
        CHECK(w.member(1).presence == GameWorld::Presence::Playing);
    }
    CHECK(w.revivalsDone() == 3);
    CHECK(w.revivesLeft() == GameWorld::kTeamRevives);
    // Everybody down: the group is wiped; its zone is not loaded again, the world stays as it fell.
    const int serial = run.zoneSerial();
    killSeat(w, 0);
    killSeat(w, 1);
    bool wipeReported = false;
    for (int t = 0; t < 400 && !run.wiped(); ++t) {
        step();
        for (const raid::Report& r : run.takeReports()) wipeReported = wipeReported || r.kind == raid::Report::Kind::Wipe;
    }
    CHECK(run.wiped());
    CHECK(wipeReported);
    CHECK(run.wipedZone() == 1);
    CHECK(run.zoneSerial() == serial);
    const uint64_t h = simulationStateHash(run.world());
    for (int t = 0; t < 120; ++t) step({right(), right()});
    CHECK(simulationStateHash(run.world()) == h);  // spectators: nothing they send plays
    CHECK(run.takeReports().empty());
    CHECK(run.world().member(0).presence == GameWorld::Presence::Down);
}

TEST_CASE("2.75 raid lockstep: 2 and 8 seats, the same digest twice; reference digests") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    if (!b) return;
    struct Golden {
        int players;
        const char* digest;
    };
    const Golden golden[] = {{2, "9d7eed54fb827234"}, {8, "f079efa8afb34d0a"}};
    int mismatches = 0;
    for (const Golden& g : golden) {
        uint64_t got[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
            CHECK(run.begin(0, 1));
            Seats seats;
            for (uint32_t t = 0; t < 1500; ++t) {
                std::vector<InputFrame> held(raid::kMaxInstancePlayers);
                for (int p = 0; p < g.players; ++p) held[static_cast<size_t>(p)] = right((t + 11 * static_cast<uint32_t>(p)) % 50 < 9);
                auto f = seats.frames(g.players, held);
                run.step(f.data(), static_cast<int>(f.size()), 0);
            }
            got[k] = run.hash();
        }
        CHECK(got[0] == got[1]);
        if (hex(got[0]) != g.digest) {
            std::printf("  raid à %d places (1500 pas) : %s (attendu %s)\n", g.players, hex(got[0]).c_str(), g.digest);
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    CHECK(kCoopSimulationVersion == 3);  // 2.77: rules 3 (souls on the last safe place, revivals held)
}

TEST_CASE("2.75: every rising liquid of the raid fills the view's width, or its basin wall to wall") {
    Svc s;
    const float view = Camera::kViewWidth / Camera::kMinFitZoom;
    int liquids = 0;
    for (const raid::Branch* b : raid::Branch::all()) {
        for (const raid::ZoneInfo& z : b->zones) {
            const raid::ZoneRules* rules = b->zoneRules(z.id);
            if (!rules || !rules->magma.present) continue;
            GameWorld w(s.ws(), WorldMode::Play);
            CHECK(w.load(*b->level(z.id)));
            const LevelEntity* e = w.entity(rules->magma.zone);
            CHECK(e != nullptr);
            if (!e) continue;
            ++liquids;
            const Rect box = e->box;
            const bool wide = box.w + 0.5f >= view;
            // Wall to wall: solid ground right beside each end, up to the liquid's surface.
            MoveOptions o;
            const bool left = w.physics().overlapsSolid(Rect{box.x - 8.0f, box.y + 2.0f, 4.0f, 4.0f}, o);
            const bool rightWall = w.physics().overlapsSolid(Rect{box.right() + 4.0f, box.y + 2.0f, 4.0f, 4.0f}, o);
            if (!wide && !(left && rightWall))
                std::printf("  %s zone %d : liquide de %.0f px (vue %.0f px), murs %d/%d\n", b->part.c_str(), z.id, box.w, view, left, rightWall);
            CHECK(wide || (left && rightWall));
            CHECK(e->drawnOverActors());  // in front of the players and the enemies, whatever its style
        }
    }
    CHECK(liquids >= 14);
}

TEST_CASE("2.75 armory: each branch's « vestiaire » before its zones; a power chosen there replays exactly and is carried on") {
    Svc s;
    int armories = 0;
    for (const raid::Branch* b : raid::Branch::all()) armories += b->hasArmory() ? 1 : 0;
    CHECK(armories == static_cast<int>(raid::Branch::all().size()));
    const raid::Branch* b = raid::Branch::builtin();
    if (!b || !b->hasArmory()) return;
    CHECK(b->zone(0) != nullptr);
    auto play = [&](std::string& power, int& zoneAfter, uint64_t& digest) {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        CHECK(run.begin(0, raid::RaidRun::kArmory));
        CHECK(run.zone() == 0);
        CHECK(run.takeReports().empty());  // the armory is never reported
        Seats seats;
        bool chosen = false;
        for (uint32_t t = 0; t < 120 * 40 && run.zone() == 0; ++t) {
            std::vector<InputFrame> held(1);
            InputFrame f = right(t % 40 < 22);  // 2.77: the tablets are up on their ledges: full jumps take them
            if (const auto& offer = run.world().powerOffer(); offer && offer->player == 0) {
                // The first tablet's power takes the first slot (or its key's); every other one is left.
                f = InputFrame{};
                f.choice = chosen ? InputFrame::kChoiceLeave : static_cast<uint8_t>(1);
                if (!chosen) power = offer->powerId;
                chosen = true;
            }
            held[0] = f;
            auto fr = seats.frames(1, held);
            run.step(fr.data(), static_cast<int>(fr.size()), 0);
        }
        zoneAfter = run.zone();
        CHECK(!power.empty());
        CHECK(run.world().playerAt(0).hasPower(power));  // carried from the armory into the route
        digest = run.hash();
    };
    std::string p1, p2;
    int z1 = -1, z2 = -1;
    uint64_t d1 = 0, d2 = 0;
    play(p1, z1, d1);
    play(p2, z2, d2);
    CHECK(z1 == b->startZone);  // the « Prêts » plate, then the passage to the start zone
    CHECK(p1 == p2);
    CHECK(d1 == d2);
}

TEST_CASE("2.75 administrator's test: 0 to 7 simulated players besides the administrator") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    if (!b) return;
    for (int bots : {0, 7}) {
        uint64_t digests[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            raid::RaidRun::Test setup;
            setup.on = true;
            setup.invincible = true;
            setup.players = 1 + bots;
            raid::TestSession session(s.ws(), *b, setup);
            CHECK(session.begin(1));
            CHECK(session.players() == 1 + bots);
            for (uint32_t t = 0; t < 600; ++t) session.step(t % 120 < 60 ? right() : InputFrame{});
            int inPlay = 0;
            for (int i = 0; i < session.run().world().teamSize(); ++i)
                inPlay += session.run().world().member(i).presence != GameWorld::Presence::Vacant;
            CHECK(inPlay == 1 + bots);
            digests[k] = session.hash();
        }
        CHECK(digests[0] == digests[1]);
    }
}

TEST_CASE("2.75 display: this computer's players opaque, the others semi-transparent") {
    GameRenderer r;
    CHECK(r.opaquePlayers() == 0xFFFFFFFFu);  // a replay, a level: everyone as before
    r.setOpaquePlayers(1u << 2);
    CHECK(r.opaquePlayers() == 4u);
    CHECK(GameRenderer::kOtherPlayerOpacity > 0.2f);
    CHECK(GameRenderer::kOtherPlayerOpacity < 0.8f);
}
