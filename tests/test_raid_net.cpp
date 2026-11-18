// 2.74: clan raids « Asterion », phase 7 (docs/RAIDS.md §15 « Fiabilité, charge et anti-triche »): an
// instance of 8 over a simulated network (latency, jitter, TCP retransmissions that hold a whole
// link, inputs out of order, a lost link that comes back within 60 s, the starter leaving for good,
// its seat freed after 60 s and taken by a late join): every game gets the same frames, every input
// lands on its step. The relay alone with nobody in play stands still (before: standby frames given
// to nobody up to the 2 h limit). A lost link is told apart from the relay's END (the raids connect
// again by themselves), the reconnection's schedule and its French messages. And the cost of a step
// with 8 players in the heaviest zones of the titan, against the 120 Hz budget (headless benchmark).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopRelay.h"
#include "game/coop/CoopTransport.h"
#include "game/coop/NetCoopLink.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/Reconnect.h"
#include "tests/TestFramework.h"

using namespace cosmic;
using namespace cosmic::coopnet;

namespace {

/// Seat k's input at step t: distinct for every seat and step (a frame tells who sent it, and when).
InputFrame netInput(int seat, uint32_t t) {
    InputFrame f;
    f.held = 1u << static_cast<int>((t / 40 + static_cast<uint32_t>(seat)) % 2 == 0 ? Action::MoveRight : Action::MoveLeft);
    f.moveX = static_cast<int8_t>(static_cast<int>((t * 7 + static_cast<uint32_t>(seat) * 31) % 255) - 127);
    f.moveY = static_cast<int8_t>(seat);
    return f;
}

/// A network of TCP-like links: each message arrives after the latency and a jitter, in order on its
/// link (a message held back holds those behind it: a lost packet is a retransmission 200 ms later),
/// or the link dies and what was in flight is lost.
struct SimNet {
    struct Pipe {
        std::deque<std::pair<double, std::vector<uint8_t>>> q;
        double last = 0.0;
    };
    double now = 0.0;
    double latency = 40.0, jitter = 30.0, lossRate = 0.001;
    std::mt19937 rng{20261118};
    std::map<int, Pipe> up, down;  // game → relay, relay → game (by connection)
    std::set<int> closedByRelay;
    CoopRelay relay{[this](int c, const std::vector<uint8_t>& b) { push(down[c], b); }, [this](int c) { closedByRelay.insert(c); }};

    void push(Pipe& p, const std::vector<uint8_t>& b) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        double at = now + latency + u(rng) * jitter;
        if (u(rng) < lossRate) at += 200.0;  // a lost packet: TCP sends it again
        at = std::max(at, p.last);
        p.last = at;
        p.q.emplace_back(at, b);
    }
    void cut(int conn) {
        up.erase(conn);
        down.erase(conn);
    }
};

/// A game of the instance: it simulates step t when the relay gave it, and sends its input for
/// t + delay first (the lockstep of RaidGameScene); a game that comes back replays the history, says
/// so (CATCHUP_DONE) and plays again from the step the relay chose.
struct SimGame {
    int conn = -1;
    int seat = -1;
    bool online = false, go = false, sawLobby = false, late = false, caughtUp = false, ended = false;
    uint8_t delay = 0;
    uint32_t step = 0, rejoinAt = 0, sentUpTo = 0;
    bool sentAny = false;
    std::vector<std::vector<InputFrame>> frames;
    std::map<uint32_t, InputFrame> sent;  // what this game sent (its step → its input)
    uint32_t stalls = 0, held = UINT32_MAX;

    void handle(const std::vector<uint8_t>& m) {
        Reader r{m.data() + 1, m.data() + m.size()};
        switch (static_cast<Msg>(m[0])) {
            case Msg::Welcome: seat = r.u8(); break;
            case Msg::Lobby: sawLobby = true; break;
            case Msg::Go:
                r.u64();
                r.u8();
                delay = r.u8();
                late = !go && !sawLobby;
                go = true;
                break;
            case Msg::Frames: {
                const uint32_t first = r.u32();
                const uint16_t n = r.u16();
                CHECK(first == frames.size());  // in order, never twice
                std::vector<std::vector<InputFrame>> seats(8, std::vector<InputFrame>(n));
                for (int s = 0; s < 8; ++s) CHECK(readFrames(r.p, r.end, seats[static_cast<size_t>(s)].data(), n));
                for (uint16_t i = 0; i < n; ++i) {
                    std::vector<InputFrame> step(8);
                    for (int s = 0; s < 8; ++s) step[static_cast<size_t>(s)] = seats[static_cast<size_t>(s)][i];
                    frames.push_back(step);
                }
                break;
            }
            case Msg::Rejoin: {
                const uint8_t s = r.u8();
                const uint32_t at = r.u32();
                if (s == seat) rejoinAt = at;
                break;
            }
            case Msg::End: ended = true; break;
            default: break;
        }
    }
};

struct Instance {
    SimNet net;
    std::vector<SimGame> games;
    int nextConn = 1;

    void join(SimGame& g, uint8_t wanted, bool lobby) {
        g = SimGame{};
        g.conn = nextConn++;
        g.online = true;
        net.relay.connected(g.conn, static_cast<uint32_t>(net.now));
        net.push(net.up[g.conn], hello(wanted, "Joueur"));
        if (lobby) net.push(net.up[g.conn], ping(0, static_cast<uint16_t>(2 * (net.latency + net.jitter))));
    }
    /// The link dies (a cut): what was in flight is lost; the relay notices it (its socket closes).
    void cut(SimGame& g) {
        net.cut(g.conn);
        g.online = false;
        net.relay.disconnected(g.conn, static_cast<uint32_t>(net.now));
    }
    void send(SimGame& g, const std::vector<uint8_t>& m) {
        if (g.online) net.push(net.up[g.conn], m);
    }

    /// One frame of a game (every 1/120 s), as RaidGameScene::fixedUpdate: its input for t + delay,
    /// then step t when given (up to 4 steps when it is behind).
    void tick(SimGame& g) {
        if (!g.online || !g.go || g.ended) return;
        if (g.late && !g.caughtUp) {
            // The history replayed off the main thread (what came so far), then CATCHUP_DONE; its
            // inputs go from then on, the relay keeps those from the step of return it chooses.
            if (!g.frames.empty()) {
                g.step = static_cast<uint32_t>(g.frames.size());
                g.caughtUp = true;
                send(g, catchupDone(g.step - 1));
            }
            return;
        }
        const int steps = g.frames.size() > g.step + g.delay + 6u ? 4 : 1;
        for (int k = 0; k < steps; ++k) {
            const uint32_t target = g.step + g.delay;
            if (!g.sentAny || target > g.sentUpTo) {
                g.sentAny = true;
                g.sentUpTo = target;
                const InputFrame f = netInput(g.seat, target);
                if (!g.late || g.rejoinAt == 0 || target >= g.rejoinAt) g.sent[target] = f;
                if (target % 50 == 7 && g.held == UINT32_MAX) {
                    g.held = target;  // out of order: this one goes after the next one
                } else {
                    send(g, input(target, &f, 1));
                    if (g.held != UINT32_MAX) {
                        const InputFrame h = netInput(g.seat, g.held);
                        send(g, input(g.held, &h, 1));
                        g.held = UINT32_MAX;
                    }
                }
            }
            if (g.step >= g.frames.size()) {
                ++g.stalls;
                break;
            }
            ++g.step;
        }
    }

    /// Runs the network and the games until `untilMs`.
    void run(double untilMs) {
        double nextPoll = net.now;
        std::vector<double> nextTick(games.size(), net.now);
        while (net.now < untilMs) {
            net.now += 1.0;
            // Deliveries due (to the relay, then to the games).
            for (auto& [conn, pipe] : net.up)
                while (!pipe.q.empty() && pipe.q.front().first <= net.now) {
                    const auto m = std::move(pipe.q.front().second);
                    pipe.q.pop_front();
                    net.relay.received(conn, m.data(), m.size(), static_cast<uint32_t>(net.now));
                }
            for (auto& g : games) {
                if (!g.online) continue;
                auto it = net.down.find(g.conn);
                if (it == net.down.end()) continue;
                while (!it->second.q.empty() && it->second.q.front().first <= net.now) {
                    g.handle(it->second.q.front().second);
                    it->second.q.pop_front();
                }
            }
            if (net.now >= nextPoll) {
                net.relay.poll(static_cast<uint32_t>(net.now));
                nextPoll += 4.0;
            }
            if (nextTick.size() < games.size()) nextTick.resize(games.size(), net.now);
            for (size_t k = 0; k < games.size(); ++k)
                if (net.now >= nextTick[k]) {
                    tick(games[k]);
                    nextTick[k] += 1000.0 / 120.0;
                }
            // Twice a second, every game says it is there (PING): never silent 3 s.
            if (static_cast<int>(net.now) % 500 == 0)
                for (auto& g : games)
                    if (g.online && g.go) send(g, ping(static_cast<uint32_t>(net.now), 100));
        }
    }
};

/// A transport that gives scripted messages once, then reports the link closed.
class ScriptedTransport : public CoopTransport {
public:
    explicit ScriptedTransport(std::vector<std::vector<uint8_t>> script, bool connects = true) : script_(std::move(script)), connects_(connects) {}
    bool connect(std::string& error) override {
        if (!connects_) error = "Impossible de joindre le relais (test).";
        return connects_;
    }
    bool send(const std::vector<uint8_t>&) override { return true; }
    bool receive(std::deque<std::vector<uint8_t>>& out, int waitMs) override {
        if (given_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            return false;  // the link died
        }
        for (auto& m : script_) out.push_back(m);
        given_ = true;
        return true;
    }
    void close() override {}

private:
    std::vector<std::vector<uint8_t>> script_;
    bool connects_;
    bool given_ = false;
};

/// Updates a link (as the screen does each frame) until `done` or 3 s.
template <class F>
void pump(NetCoopLink& link, F done) {
    for (int i = 0; i < 600 && !done(); ++i) {
        link.update(1.0f / 120.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

GoInfo raidGo() {
    GoInfo g;
    g.players = 8;
    g.delay = 5;
    g.level = raid::kBranchLevel;
    g.hash = "2.1";
    g.attempt = "test";
    return g;
}

}  // namespace

TEST_CASE("raid net: 8 games, latency, jitter, retransmissions, inputs out of order, a lost link, a seat freed and taken again") {
    Instance in;
    in.net.relay.setRaid(true);
    in.net.relay.setLevel(raid::kBranchLevel, "2.1", "Asterion · Bras droit", "raid");
    in.games.reserve(10);  // references to the games stay valid
    in.games.resize(8);
    for (auto& g : in.games) in.join(g, 255, true);
    in.run(400.0);
    for (const auto& g : in.games) CHECK(g.seat >= 0);
    in.send(in.games[0], start(8));  // any seat starts a raid
    in.run(5000.0);
    CHECK(in.net.relay.started());
    // Game 3's link dies (its packets in flight lost); it comes back 2 s later on a new link.
    SimGame& g3 = in.games[3];
    const int seat3 = g3.seat;
    in.cut(g3);
    const uint32_t absentFrom = in.net.relay.nextTick();
    in.run(7000.0);
    CHECK(in.net.relay.absent(seat3));
    in.join(g3, static_cast<uint8_t>(seat3), false);
    in.run(9000.0);
    CHECK(g3.seat == seat3);
    CHECK(g3.late);
    CHECK(g3.rejoinAt > absentFrom);
    CHECK(!in.net.relay.absent(seat3));
    // The starter leaves for good (LEAVE): nobody is the host, the others go on.
    SimGame& g0 = in.games[0];
    const int seat0 = g0.seat;
    in.send(g0, leave());
    in.run(9300.0);
    g0.online = false;
    in.net.cut(g0.conn);
    const uint32_t gone = in.net.relay.nextTick();
    // 60 s of steps later its seat is free; a late join takes it.
    in.run(9300.0 + 64000.0);
    CHECK(in.net.relay.nextTick() > gone + static_cast<uint32_t>(GameWorld::kStandbyLimitTicks) + 20);
    CHECK(in.net.relay.vacant(seat0));
    in.games.emplace_back();
    SimGame& g8 = in.games.back();
    in.join(g8, 255, false);
    in.run(9300.0 + 67000.0);
    CHECK(g8.seat == seat0);
    CHECK(g8.rejoinAt > 0);
    in.run(9300.0 + 68000.0);

    // Every game in play has the same frames (the common part).
    const SimGame& ref = in.games[1];
    for (const SimGame& g : in.games) {
        if (!g.online) continue;
        const size_t n = std::min(g.frames.size(), ref.frames.size());
        CHECK(n > 7200);
        for (size_t t = 0; t < n; ++t) CHECK(g.frames[t] == ref.frames[t]);
    }
    // Every input landed on its own step, for its seat; the other steps are standby or vacant.
    const auto& f = ref.frames;
    for (const SimGame* g : {&in.games[1], &in.games[2], &in.games[5], &in.games[7]})
        for (const auto& [t, input] : g->sent)
            if (t < f.size()) CHECK(f[t][static_cast<size_t>(g->seat)] == input);
    for (uint32_t t = g3.rejoinAt; t < f.size(); ++t) CHECK(f[t][static_cast<size_t>(seat3)] == netInput(seat3, t));
    CHECK(f[g3.rejoinAt - 1][static_cast<size_t>(seat3)].flags == InputFrame::Standby);
    CHECK(f[gone + 100][static_cast<size_t>(seat0)].flags == InputFrame::Standby);
    CHECK(f[gone + static_cast<uint32_t>(GameWorld::kStandbyLimitTicks) + 10][static_cast<size_t>(seat0)].flags == InputFrame::Vacant);
    CHECK(f[g8.rejoinAt - 1][static_cast<size_t>(seat0)].flags == InputFrame::Vacant);
    CHECK(f[g8.rejoinAt + 5][static_cast<size_t>(seat0)] == netInput(seat0, g8.rejoinAt + 5));
    // The lockstep kept up with the clock (stalls: steps a game waited for its frame).
    const double elapsedSteps = (in.net.now - 400.0) * 120.0 / 1000.0;
    CHECK(static_cast<double>(ref.step) > elapsedSteps * 0.9);
    std::printf("       raid net: %u steps, input delay %u, stalls %u (%.2f %%)\n", ref.step, static_cast<unsigned>(ref.delay), ref.stalls, 100.0 * ref.stalls / std::max(1.0, elapsedSteps));
}

TEST_CASE("raid net: nobody in play, the relay stands still (before 2.74: standby frames to nobody up to 2 h); the return plays again") {
    std::map<int, std::vector<std::vector<uint8_t>>> got;
    CoopRelay relay{[&](int c, const std::vector<uint8_t>& b) { got[c].push_back(b); }, [](int) {}};
    relay.setRaid(true);
    relay.setLevel(raid::kBranchLevel, "2.1", "Asterion · Bras droit", "raid");
    relay.connected(1, 0);
    auto send = [&](int c, const std::vector<uint8_t>& m, uint32_t now = 0) { relay.received(c, m.data(), m.size(), now); };
    send(1, hello(255, "Nova"));
    send(1, start(1));
    const uint32_t delay = relay.nextTick();
    for (uint32_t t = delay; t < 300; ++t) {
        const InputFrame f = netInput(0, t);
        send(1, input(t, &f, 1));
    }
    CHECK(relay.nextTick() == 300);
    relay.disconnected(1, 0);
    for (uint32_t ms = 0; ms < 50000; ms += 4) relay.poll(ms);
    CHECK(relay.started());
    CHECK(relay.nextTick() == 300);  // before: 864 000, the game over
    // Back: the history, then its return; the steps up to it are given at once (nobody else plays).
    relay.connected(2, 50000);
    send(2, hello(0, "Nova"), 50000);
    CHECK(relay.seatOf(2) == 0);
    send(2, catchupDone(299), 50000);
    const uint32_t at = 300 + delay + 1;
    CHECK(relay.nextTick() == at);
    const InputFrame f = netInput(0, at);
    send(2, input(at, &f, 1), 50000);
    CHECK(relay.nextTick() == at + 1);
    CHECK(!relay.absent(0));
}

TEST_CASE("raid net: a lost link is told apart from the relay's END (the raids connect again), in French") {
    auto frames = [] {
        std::vector<InputFrame> f(8 * 10);
        return framesMsg(0, f.data(), 10, 8);
    };
    {
        NetCoopLink link(std::make_unique<ScriptedTransport>(std::vector<std::vector<uint8_t>>{welcome(2), go(raidGo()), frames()}), "Nova");
        link.start();
        pump(link, [&] { return !link.endReason().empty(); });
        CHECK(link.connected());
        CHECK(link.go().has_value());
        CHECK(link.available() == 10);
        CHECK(link.connectionLost());
        CHECK(link.endReason() == "La connexion au relais est perdue.");
        link.close();
    }
    {
        // The relay refuses (its END comes just before the socket closes): its reason, not a lost link.
        NetCoopLink link(std::make_unique<ScriptedTransport>(std::vector<std::vector<uint8_t>>{welcome(2), go(raidGo()), end(EndReason::Refused)}), "Nova");
        link.start();
        pump(link, [&] { return !link.endReason().empty(); });
        CHECK(!link.connectionLost());
        CHECK(link.endReason().find("refuse") != std::string::npos);
        link.close();
    }
    {
        NetCoopLink link(std::make_unique<ScriptedTransport>(std::vector<std::vector<uint8_t>>{}, false), "Nova");
        link.start();
        pump(link, [&] { return !link.error().empty(); });
        CHECK(!link.connectionLost());
        CHECK(link.error() == "Impossible de joindre le relais (test).");
        link.close();
    }
}

TEST_CASE("raid net: the reconnection connects again at once, then 1, 2, 4, 8 s apart, for the 60 s the seat is kept") {
    raid::Reconnect r;
    CHECK(!r.active());
    CHECK(!r.due(0.0f));
    r.lost(10.0f);
    CHECK(r.active());
    CHECK(r.due(10.0f));  // at once
    CHECK(!r.due(10.5f)); // one attempt at a time
    std::vector<float> at{10.0f};
    float now = 10.0f;
    while (!r.expired(now)) {
        r.failed(now + 0.25f);  // each attempt fails a quarter second after it started
        now += 0.25f;
        while (!r.due(now) && !r.expired(now)) now += 0.05f;
        if (!r.expired(now)) at.push_back(now);
    }
    CHECK(at.size() >= 9);
    CHECK_NEAR(at[1] - at[0], 1.25, 0.11);
    CHECK_NEAR(at[2] - at[1], 2.25, 0.11);
    CHECK_NEAR(at[3] - at[2], 4.25, 0.11);
    CHECK_NEAR(at[4] - at[3], 8.25, 0.11);
    CHECK_NEAR(at[5] - at[4], 8.25, 0.11);
    CHECK(at.back() < 70.0f);
    CHECK(r.expired(70.0f));
    CHECK(r.remaining(70.0f) == 0.0f);
    r.back();
    r.lost(100.0f);
    CHECK(r.due(100.0f));
    CHECK(r.message(130.0f) == "Liaison avec le relais perdue : reconnexion automatique (essai 1). Votre place est gardée encore 30 s.");
    CHECK(raid::Reconnect::gaveUp().find("60 s") != std::string::npos);
    r.back();
    CHECK(!r.active());
}

TEST_CASE("raid performance: a step with 8 players in the heaviest zones stays within the 120 Hz budget (headless)") {
    // Recorded threshold (docs/RAIDS.md §15): at 120 Hz a step has 8.33 ms; the simulation of the
    // whole instance must take less than half of it on average (the other half for the drawing),
    // and its slowest 1 % less than the whole of it.
    constexpr double kBudgetMs = 1000.0 / 120.0;
    ContentDatabase content;
    content.reload();
    AudioSystem audio;
    ResourceManager res;
    res.init(nullptr, &audio);
    struct Case {
        const char* part;
        int zone;
    };
    for (const Case c : {Case{"bras_droit", 7}, Case{"torse", 7}, Case{"jambe_gauche", 7}}) {
        const raid::Branch* b = raid::Branch::builtin(c.part);
        CHECK(b != nullptr);
        raid::RaidRun run(WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}, *b, raid::kMaxInstancePlayers);
        CHECK(run.begin(0, c.zone));
        const size_t enemies = run.world().enemies().size();
        std::vector<InputFrame> prev(8), team(8);
        std::vector<double> ms;
        constexpr uint32_t kSteps = 1800;  // 15 s of play
        ms.reserve(kSteps);
        for (uint32_t t = 0; t < kSteps; ++t) {
            for (int k = 0; k < 8; ++k) {
                InputFrame f;
                const bool right = (t / (90 + 11 * static_cast<uint32_t>(k))) % 3 != 2;
                f.held = 1u << static_cast<int>(right ? Action::MoveRight : Action::MoveLeft);
                if ((t + static_cast<uint32_t>(k) * 17) % 53 < 9) f.held |= 1u << static_cast<int>(Action::Jump);
                if ((t + static_cast<uint32_t>(k) * 7) % 37 < 3) f.held |= 1u << static_cast<int>(Action::Attack);
                f.moveX = right ? 127 : -127;
                f.pressed = f.held & ~prev[static_cast<size_t>(k)].held;
                f.released = prev[static_cast<size_t>(k)].held & ~f.held;
                prev[static_cast<size_t>(k)] = f;
                team[static_cast<size_t>(k)] = f;
            }
            const auto t0 = std::chrono::steady_clock::now();
            run.step(team.data(), 8, 0);
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        double sum = 0.0;
        for (double v : ms) sum += v;
        const double mean = sum / static_cast<double>(ms.size());
        std::sort(ms.begin(), ms.end());
        const double p99 = ms[ms.size() * 99 / 100];
        const double worst = ms.back();
        std::printf("       raid step, %s zone %d: %zu enemies, mean %.3f ms, p99 %.3f ms, max %.3f ms (budget %.2f ms)\n", c.part, c.zone, enemies, mean, p99,
                    worst, kBudgetMs);
        CHECK(enemies >= 100);
        CHECK(mean < kBudgetMs * 0.5);
        CHECK(p99 < kBudgetMs);
    }
}
