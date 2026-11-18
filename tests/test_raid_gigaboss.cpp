// 2.69: clan raids « Asterion », phase 4B (docs/RAIDS.md §13): the gigaboss SKULLMAW (Crâne), DREADFANG
// (Bras gauche) and HEARTBREAKER (Torse) in their lairs (zone 8 of their branch, tools/levelgen/
// raid_skullmaw.py, raid_dreadfang.py, raid_heartbreaker.py), their routines (game/raid/SkullmawRoutine,
// DreadfangRoutine, HeartbreakerRoutine, on raid::GigabossRoutine):
// enormous, a blow on every player at once, their health for 2 to 8 seats, the same digest in
// lockstep at 2 and 8 players (also through the relay), their collective mechanisms (mirrors held at
// the flash, three chains held together, four plates broken together, two ribs pried open at once),
// a player nailed and freed, their fall reported (never by an administrator's test), their sprites
// the generators' output.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopRelay.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/DreadfangRoutine.h"
#include "game/raid/HeartbreakerRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/SkullmawRoutine.h"
#include "tests/TestFramework.h"

using namespace cosmic;
using namespace cosmic::coopnet;

#ifdef _WIN32
#define COSMIC_G4B_NULL "NUL"
#else
#define COSMIC_G4B_NULL "/dev/null"
#endif

namespace {

constexpr int R = static_cast<int>(Action::MoveRight);
constexpr int L = static_cast<int>(Action::MoveLeft);
constexpr int J = static_cast<int>(Action::Jump);
constexpr int A = static_cast<int>(Action::Attack);
constexpr int kLair = 8;

struct Boss {
    const char* part;
    const char* id;
    const char* objective;
};
const Boss kBosses[] = {{"crane", "skullmaw", "crane_skullmaw"}, {"bras_gauche", "dreadfang", "bg_dreadfang"}, {"torse", "heartbreaker", "torse_heartbreaker"}};

InputFrame scripted(int seat, uint32_t t) {
    InputFrame f;
    const uint32_t phase = (t / (60 + 7 * static_cast<uint32_t>(seat))) % 4;
    const int dir = phase == 0 || phase == 1 ? 1 : (phase == 2 ? -1 : 0);
    if (dir > 0) f.held |= 1u << R;
    if (dir < 0) f.held |= 1u << L;
    f.moveX = static_cast<int8_t>(dir * 127);
    if ((t + static_cast<uint32_t>(seat) * 13) % 47 < 8) f.held |= 1u << J;
    if ((t + static_cast<uint32_t>(seat) * 5) % 71 < 3) f.held |= 1u << A;
    return f;
}

struct Edges {
    std::vector<InputFrame> prev;
    explicit Edges(int n) : prev(static_cast<size_t>(n)) {}
    InputFrame operator()(int seat, InputFrame f) {
        InputFrame& p = prev[static_cast<size_t>(seat)];
        f.pressed = f.held & ~p.held;
        f.released = p.held & ~f.held;
        p = f;
        return f;
    }
};

struct Services {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Services() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

const raid::Branch* branchOf(const char* part) {
    const raid::Branch* b = raid::Branch::builtin(part);
    CHECK(b != nullptr);
    return b;
}

/// Frames where seats >= `players` are vacant and the others hold still.
std::vector<InputFrame> still(int players) {
    std::vector<InputFrame> f(raid::kMaxInstancePlayers);
    for (int k = players; k < raid::kMaxInstancePlayers; ++k) f[static_cast<size_t>(k)].flags = InputFrame::Vacant;
    return f;
}

Enemy* bossOf(raid::RaidRun& run) {
    for (auto& e : run.world().enemies())
        if (e->objectId() == run.rules().gigaboss.boss) return e.get();
    return nullptr;
}

/// The players in play put on the floor of the arena, a little in: the gigaboss wakes up.
void intoArena(raid::RaidRun& run, int players) {
    const raid::ArenaInfo& a = run.rules().arena;
    for (int i = 0; i < players; ++i) run.world().playerAt(i).teleportTo({a.entry.x + 160.0f + 40.0f * static_cast<float>(i), a.floor});
}

bool engage(raid::RaidRun& run, int players, int limit = 900) {
    const auto f = still(players);
    for (int t = 0; t < limit && !run.gigaboss().engaged(); ++t) {
        if (t % 30 == 0) intoArena(run, players);
        run.step(f.data(), static_cast<int>(f.size()), 0);
    }
    return run.gigaboss().engaged();
}

raid::RaidRun::Test adminTest(int players, int phase) {
    raid::RaidRun::Test t;  // invincible: only the rules are looked at
    t.on = true;
    t.invincible = true;
    t.players = players;
    t.startPhase = phase;
    return t;
}

void stand(raid::RaidRun& run, int player, const Rect& place) { run.world().playerAt(player).teleportTo({place.center().x, place.bottom()}); }

/// A relay of the local network in raid mode for `b`, its messages kept by connection.
struct Wire {
    std::map<int, std::vector<std::vector<uint8_t>>> got;
    CoopRelay relay{[this](int c, const std::vector<uint8_t>& m) { got[c].push_back(m); }, [](int) {}};
    void send(int conn, const std::vector<uint8_t>& m) { relay.received(conn, m.data(), m.size(), 0); }
    std::vector<std::vector<InputFrame>> frames(int conn, int players) {
        std::vector<std::vector<InputFrame>> out;
        for (const auto& m : got[conn]) {
            if (m.empty() || m[0] != static_cast<uint8_t>(Msg::Frames)) continue;
            Reader r{m.data() + 1, m.data() + m.size()};
            const uint32_t first = r.u32();
            const uint16_t n = r.u16();
            std::vector<std::vector<InputFrame>> seats(static_cast<size_t>(players), std::vector<InputFrame>(n));
            for (int s = 0; s < players; ++s) CHECK(readFrames(r.p, r.end, seats[static_cast<size_t>(s)].data(), n));
            if (out.size() < first + n) out.resize(first + n);
            for (uint16_t i = 0; i < n; ++i)
                for (int s = 0; s < players; ++s) out[first + i].push_back(seats[static_cast<size_t>(s)][i]);
        }
        return out;
    }
};

void playThroughRelay(Wire& w, const raid::Branch& b, int players, uint32_t ticks) {
    w.relay.setRaid(true);
    w.relay.setLevel(b.levelId(), b.levelHash(), "Asterion", "raid");
    for (int c = 1; c <= players; ++c) {
        w.relay.connected(c, 0);
        w.send(c, hello(255, "Joueur " + std::to_string(c)));
    }
    w.send(players, start(1));
    CHECK(w.relay.started());
    const uint32_t delay = w.relay.nextTick();
    Edges edges(players);
    std::vector<InputFrame> one(1);
    for (uint32_t t = delay; t < ticks; ++t)
        for (int c = 1; c <= players; ++c) {
            one[0] = edges(c - 1, scripted(c - 1, t));
            w.send(c, input(t, one.data(), 1));
        }
}

}  // namespace

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): the sprites of Skullmaw, Dreadfang and Heartbreaker are their generators' output") {
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_G4B_NULL " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification des générateurs des gigaboss sautée)\n");
        return;
    }
    for (const Boss& g : kBosses) {
        const std::string cmd = std::string("python3 tools/assetgen/characters/boss_l_") + g.id + ".py --check > " COSMIC_G4B_NULL " 2>&1";
        CHECK(std::system(cmd.c_str()) == 0);
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): three enormous gigaboss in their lairs, three phases, their own rules, their health for 2 to 8 seats") {
    Services s;
    for (const Boss& g : kBosses) {
        const raid::Branch* b = branchOf(g.part);
        const EnemyDefinition* def = s.content.enemy(g.id);
        CHECK(b != nullptr && def != nullptr);
        if (!b || !def) continue;
        CHECK(def->isBoss);
        CHECK(def->phases.size() == 3);
        CHECK(def->raid == "asterion");
        CHECK(def->scale >= 2.5f);
        CHECK(def->size.x >= 300.0f && def->size.y >= 240.0f);  // a giant for 8: several times Nova's size
        const raid::ZoneRules* z = b->zoneRules(kLair);
        CHECK(z != nullptr);
        if (!z) continue;
        CHECK(z->gigaboss.present);
        CHECK(z->gigaboss.routine == g.id);
        CHECK(z->gigaboss.objective == g.objective);
        CHECK(z->arena.present);
        CHECK(z->arena.rect.w >= 6.0f * Camera::kViewWidth);
        CHECK(z->arena.floor > z->arena.rect.y && z->arena.floor <= z->arena.rect.bottom());
        for (int players : {2, 5, 8}) {
            raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
            CHECK(run.begin(0, kLair));
            CHECK(run.gigaboss().present());
            CHECK(run.gigaboss().routine() != nullptr);
            CHECK(engage(run, players));
            const Enemy* boss = bossOf(run);
            CHECK(boss != nullptr);
            if (!boss) continue;
            const float want = def->hp * raid::Gigaboss::healthFactor(run.rules().gigaboss, players);
            CHECK(std::abs(boss->maxHp() - want) < 0.5f);
            CHECK(run.gigaboss().scaledFor() == players);
        }
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): 2 and 8 players, each fight gives the same digest on two computers and through the relay") {
    Services s;
    for (const Boss& g : kBosses) {
        const raid::Branch* b = branchOf(g.part);
        if (!b) continue;
        for (int players : {2, 8}) {
            raid::RaidRun x(s.ws(), *b, raid::kMaxInstancePlayers), y(s.ws(), *b, raid::kMaxInstancePlayers);
            CHECK(x.begin(0, kLair));
            CHECK(y.begin(0, kLair));
            Edges edges(raid::kMaxInstancePlayers);
            std::vector<InputFrame> frame(raid::kMaxInstancePlayers);
            bool same = true;
            for (uint32_t t = 0; t < 2400; ++t) {
                for (int k = 0; k < raid::kMaxInstancePlayers; ++k) {
                    InputFrame f;
                    if (k >= players) f.flags = InputFrame::Vacant;
                    else f = edges(k, scripted(k, t));
                    frame[static_cast<size_t>(k)] = f;
                }
                if (t == 30) {
                    intoArena(x, players);
                    intoArena(y, players);
                }
                x.step(frame.data(), static_cast<int>(frame.size()), 0);
                y.step(frame.data(), static_cast<int>(frame.size()), 0);
                if (t % 30 == 29 && x.hash() != y.hash()) same = false;
            }
            CHECK(same);
            CHECK(x.gigaboss().engaged());
            CHECK(x.contributions().canonical() == y.contributions().canonical());
            Wire w;
            playThroughRelay(w, *b, players, 1200);
            auto play = [&](int conn) {
                raid::RaidRun r(s.ws(), *b, raid::kMaxInstancePlayers);
                CHECK(r.begin(0, kLair));
                const auto fr = w.frames(conn, raid::kMaxInstancePlayers);
                std::vector<uint64_t> digests;
                for (uint32_t t = 0; t < fr.size(); ++t) {
                    if (t == 30) intoArena(r, players);
                    r.step(fr[t].data(), static_cast<int>(fr[t].size()), 0);
                    if (t % 60 == 59) digests.push_back(r.hash());
                }
                return digests;
            };
            const auto d1 = play(1), d2 = play(players);
            CHECK(!d1.empty());
            CHECK(d1 == d2);
        }
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): each strikes every player at once (a gaze, ground claws, a rib trap each)") {
    Services s;
    for (const Boss& g : kBosses) {
        const raid::Branch* b = branchOf(g.part);
        if (!b) continue;
        for (int players : {2, 8}) {
            raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
            run.setTest(adminTest(players, 1));
            CHECK(run.begin(0, kLair));
            CHECK(engage(run, players));
            const auto f = still(players);
            size_t most = 0;
            for (int k = 0; k < 1400; ++k) {
                run.step(f.data(), static_cast<int>(f.size()), 0);
                // Skullmaw: its gazes; Dreadfang and Heartbreaker: the marks of the rules' fists (ground claws, rib traps).
                if (const auto* sk = dynamic_cast<const raid::SkullmawRoutine*>(run.gigaboss().routine())) most = std::max(most, sk->gazes().size());
                else most = std::max(most, run.gigaboss().fists().size());
            }
            CHECK(most == static_cast<size_t>(players));  // one for each player in play, the same step
        }
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): Skullmaw's veil holds until enough mirrors are held at its flash; then it is blind") {
    Services s;
    const raid::Branch* b = branchOf("crane");
    if (!b) return;
    for (int held : {1, 2}) {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        run.setTest(adminTest(3, 2));  // three players: two mirrors needed
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 3));
        const auto f = still(3);
        for (int k = 0; k < 20; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
        const auto* sk = dynamic_cast<const raid::SkullmawRoutine*>(run.gigaboss().routine());
        Enemy* boss = bossOf(run);
        CHECK(sk != nullptr && boss != nullptr);
        if (!sk || !boss) return;
        CHECK(run.gigaboss().phase() == 2);
        CHECK(sk->veil());
        CHECK(sk->mirrorsNeeded() == 2);
        CHECK(sk->mirrors().size() == 4);
        CHECK(boss->raidGuarded());  // its veil: nothing hurts it
        for (int k = 0; k < 2400 && sk->lastFlash() == 0; ++k) {
            for (int p = 0; p < held; ++p) stand(run, p, sk->mirrors()[static_cast<size_t>(p)].area);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(sk->lastFlash() > 0);
        CHECK(sk->lastHeld() == held);
        if (held < 2) {
            CHECK(run.gigaboss().stage() != raid::Gigaboss::Stage::Exposed);  // too few mirrors: its veil holds
            CHECK(boss->raidGuarded());
            continue;
        }
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed);  // blind
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(!boss->raidGuarded());
        CHECK(boss->raidMultiplier() == 2.0f);
        CHECK(run.gigaboss().weakTotal() == 3);  // two eyes and its crystal
        CHECK(sk->gazes().empty());    // blind, it sees nobody
        int mirrors = 0;
        for (const raid::Contribution& c : run.contributions().records()) mirrors += c.vents;
        CHECK(mirrors >= 2);
        Enemy* eye = nullptr;
        for (auto& e : run.world().enemies())
            if (e->alive() && e->def().id == "skullmaw_cristal") eye = e.get();
        CHECK(eye != nullptr);
        if (eye) {
            const float before = boss->hp();
            eye->kill(run.world());
            run.step(f.data(), static_cast<int>(f.size()), 0);
            CHECK(boss->hp() < before - 0.045f * boss->maxHp());  // its share (the rules' weakShare)
            CHECK(run.gigaboss().weakLeft() == 2);
        }
        for (int k = 0; k < 1400 && run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);  // its veil again
        CHECK(run.gigaboss().weakLeft() == 0);
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(boss->raidGuarded());  // its veil again
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): Dreadfang is chained when its three chains are held together; a nailed player is freed") {
    Services s;
    const raid::Branch* b = branchOf("bras_gauche");
    if (!b) return;
    {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        run.setTest(adminTest(3, 2));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 3));
        const auto f = still(3);
        for (int k = 0; k < 20; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
        const auto* df = dynamic_cast<const raid::DreadfangRoutine*>(run.gigaboss().routine());
        Enemy* boss = bossOf(run);
        CHECK(df != nullptr && boss != nullptr);
        if (!df || !boss) return;
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);  // its frost armour
        CHECK(boss->raidGuarded());
        CHECK(run.gigaboss().vents().size() == 3);  // its anchors
        // One chain alone: locked, but the armour holds.
        for (int k = 0; k < 400; ++k) {
            stand(run, 0, run.gigaboss().vents()[0].area);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(run.gigaboss().vents()[0].closed);
        CHECK(!df->chained());
        CHECK(boss->raidGuarded());
        // The three together: pinned to the floor, its knuckles bare.
        for (int k = 0; k < 600 && !df->chained(); ++k) {
            for (int p = 0; p < 3; ++p) stand(run, p, run.gigaboss().vents()[static_cast<size_t>(p)].area);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(df->chained());
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(!boss->raidGuarded());
        CHECK(boss->raidMultiplier() == 2.0f);
        CHECK(run.gigaboss().weakTotal() == 3);
        CHECK(std::abs(boss->box.bottom() - df->storeys().front()) < 1.0f);  // on the floor
    }
    {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        run.setTest(adminTest(2, 2));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 2));
        const auto f = still(2);
        const auto* df = dynamic_cast<const raid::DreadfangRoutine*>(run.gigaboss().routine());
        CHECK(df != nullptr);
        if (!df) return;
        bool nailed = false;
        uint32_t claw = 0;
        int seat = -1;
        for (int k = 0; k < 4000 && !nailed; ++k) {
            run.step(f.data(), static_cast<int>(f.size()), 0);
            for (const auto& p : df->pins())
                if (p.nailed && p.claw) {
                    nailed = true;
                    claw = p.claw;
                    seat = p.seat;
                }
        }
        CHECK(nailed);  // standing still on its mark: nailed
        if (!nailed) return;
        // Held on its mark: it does not move.
        const Vec2 at = run.world().playerAt(seat).feet();
        InputFrame run1 = f[static_cast<size_t>(seat)];
        run1.held |= 1u << R;
        run1.moveX = 127;
        auto frames = f;
        frames[static_cast<size_t>(seat)] = run1;
        for (int k = 0; k < 30; ++k) run.step(frames.data(), static_cast<int>(frames.size()), 0);
        CHECK(std::abs(run.world().playerAt(seat).feet().x - at.x) < 2.0f);
        // The claw broken: freed.
        if (Enemy* e = run.world().findEnemy(claw)) e->kill(run.world());
        run.step(f.data(), static_cast<int>(f.size()), 0);
        bool still_nailed = false;
        for (const auto& p : df->pins()) still_nailed = still_nailed || (p.seat == seat && p.nailed);
        CHECK(!still_nailed);
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): Heartbreaker's four plates fall together or grow back; its ribs pried open from both sides") {
    Services s;
    const raid::Branch* b = branchOf("torse");
    if (!b) return;
    {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        run.setTest(adminTest(4, 1));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 4));
        const auto f = still(4);
        for (int k = 0; k < 10; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
        const auto* hb = dynamic_cast<const raid::HeartbreakerRoutine*>(run.gigaboss().routine());
        Enemy* boss = bossOf(run);
        CHECK(hb != nullptr && boss != nullptr);
        if (!hb || !boss) return;
        CHECK(hb->plated());
        CHECK(hb->platesLeft() == 4);
        CHECK(boss->raidGuarded());
        auto plates = [&]() {
            std::vector<Enemy*> out;
            for (auto& e : run.world().enemies())
                if (e->alive() && e->def().id == "heartbreaker_plaque") out.push_back(e.get());
            return out;
        };
        // One plate alone: the others wait, the broken one grows back.
        plates().front()->kill(run.world());
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(hb->platesLeft() == 3);
        CHECK(hb->windowLeft() > 0.0f);
        for (int k = 0; k < 6000 && hb->platesLeft() < 4; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(hb->platesLeft() == 4);
        run.step(f.data(), static_cast<int>(f.size()), 0);  // the plates grown back are in the world
        CHECK(boss->raidGuarded());
        CHECK(plates().size() == 4);
        // All four together: the armour is gone for good.
        for (Enemy* e : plates()) e->kill(run.world());
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(!hb->plated());
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(!boss->raidGuarded());
    }
    {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        run.setTest(adminTest(2, 2));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 2));
        const auto f = still(2);
        for (int k = 0; k < 10; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
        const auto* hb = dynamic_cast<const raid::HeartbreakerRoutine*>(run.gigaboss().routine());
        Enemy* boss = bossOf(run);
        CHECK(hb != nullptr && boss != nullptr);
        if (!hb || !boss) return;
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);  // its ribs shut
        CHECK(boss->raidGuarded());
        CHECK(run.gigaboss().vents().size() == 2);  // its two ribs
        // One side only: the ribs stay shut.
        for (int k = 0; k < 600; ++k) {
            stand(run, 0, run.gigaboss().vents()[0].area);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(run.gigaboss().stage() != raid::Gigaboss::Stage::Exposed);
        // Both sides at once: the heart is bare.
        for (int k = 0; k < 600 && run.gigaboss().stage() != raid::Gigaboss::Stage::Exposed; ++k) {
            stand(run, 0, run.gigaboss().vents()[0].area);
            stand(run, 1, run.gigaboss().vents()[1].area);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed);
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(!boss->raidGuarded());
        CHECK(boss->raidMultiplier() == 2.5f);
        CHECK(run.gigaboss().weakLeft() == 1);
        int ribs = 0;
        for (const raid::Contribution& c : run.contributions().records()) ribs += c.vents;
        CHECK(ribs >= 2);
    }
}

TEST_CASE("raid gigaboss 4B (Skullmaw, Dreadfang, Heartbreaker): each fall reports its phases, its objective and the contributions; an admin test never reports") {
    Services s;
    for (const Boss& g : kBosses) {
        const raid::Branch* b = branchOf(g.part);
        if (!b) continue;
        for (bool test : {false, true}) {
            raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
            if (test) run.setTest(adminTest(8, 1));
            CHECK(run.begin(0, kLair));
            CHECK(engage(run, 2));
            std::vector<raid::Report> reports;
            for (auto& r : run.takeReports()) reports.push_back(r);
            Enemy* boss = bossOf(run);
            CHECK(boss != nullptr);
            if (!boss) continue;
            const auto f = still(2);
            boss->wound(run.world(), boss->maxHp() * 2.0f);  // its last stand, then its fall
            for (int k = 0; k < 6000 && run.gigaboss().stage() != raid::Gigaboss::Stage::Fallen; ++k) {
                if (k % 60 == 0)
                    for (int i = 0; i < 2; ++i) run.world().playerAt(i).heal(10);
                run.step(f.data(), static_cast<int>(f.size()), 0);
                for (auto& r : run.takeReports()) reports.push_back(r);
            }
            CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Fallen);
            for (int k = 0; k < 10; ++k) {
                run.step(f.data(), static_cast<int>(f.size()), 0);
                for (auto& r : run.takeReports()) reports.push_back(r);
            }
            int present = 0;
            for (const raid::Contribution& c : run.contributions().records()) present += c.gigaboss ? 1 : 0;
            CHECK(present == 2);
            if (test) {
                CHECK(reports.empty());  // never a report: no consensus, no state, no ranking, no reward
                CHECK(run.gigaboss().scaledFor() == 8);
                continue;
            }
            bool fell = false, objective = false, result = false;
            for (const raid::Report& r : reports) {
                fell = fell || (r.kind == raid::Report::Kind::Boss && r.phase == 4 && r.zone == kLair);
                objective = objective || (r.kind == raid::Report::Kind::Objective && r.objective == g.objective);
                if (r.kind == raid::Report::Kind::Result)
                    for (const raid::Contribution& c : r.records) result = result || c.gigaboss;
            }
            CHECK(fell);
            CHECK(objective);
            CHECK(result);
        }
    }
}
