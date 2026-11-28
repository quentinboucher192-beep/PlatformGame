// 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): an administrator's test of Asterion played with invited players, relayed.
// Two computers of the same test — the administrator's, shown, and an invited player's, quiet and
// caught up all at once — step the same relay stream (the real players' frames, the places given at a
// step: the invited player's seat, their absence, « Invincible » ticked mid-test, the administrator
// moving between the groups of the whole raid) and get the same digest at every checkpoint; the seat
// of an invited player is theirs (never their simulated player's) and their invincibility is applied.

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/input/InputTypes.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopTransport.h"
#include "game/coop/NetCoopLink.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"
#include "game/raid/RaidTestRelay.h"
#include "game/raid/bots/BotTypes.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

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

/// A player's input at step t (distinct per relay seat): runs one way then the other, jumps, strikes.
InputFrame input(int seat, uint32_t t, uint32_t& prev) {
    InputFrame f;
    const int dir = ((t + static_cast<uint32_t>(seat) * 170) / 300) % 2 == 0 ? 1 : -1;
    f.held |= 1u << static_cast<int>(dir > 0 ? Action::MoveRight : Action::MoveLeft);
    f.moveX = static_cast<int8_t>(dir * 127);
    if ((t + static_cast<uint32_t>(seat) * 31) % 80 < 12) f.held |= 1u << static_cast<int>(Action::Jump);
    if ((t + static_cast<uint32_t>(seat)) % 45 == 0) f.held |= 1u << static_cast<int>(Action::Attack);
    f.pressed = f.held & ~prev;
    f.released = prev & ~f.held;
    prev = f.held;
    return f;
}

/// What the relay of a test gives: the frames of its 8 seats at each step, and the places from steps on.
struct Stream {
    std::vector<std::vector<InputFrame>> frames;
    std::vector<std::pair<uint32_t, coopnet::TestSeats>> tables;
    coopnet::TestSeats at(uint32_t t) const {
        coopnet::TestSeats out;
        for (const auto& [k, s] : tables)
            if (k <= t) out = s;
        return out;
    }
    /// Every table goes through the protocol, as the relay writes it and the game reads it.
    void checkWire() const {
        for (const auto& [tick, seats] : tables) {
            const std::vector<uint8_t> msg = coopnet::testSeats(tick, seats);
            CHECK(msg[0] == static_cast<uint8_t>(coopnet::Msg::TestSeats));
            uint32_t back = 0;
            coopnet::TestSeats got;
            CHECK(coopnet::readTestSeats(msg.data() + 1, msg.size() - 1, back, got));
            CHECK(back == tick);
            CHECK(got == seats);
        }
    }
};

/// The administrator on relay seat 0 from the start; an invited player on relay seat 1 who comes at
/// `join` (vacant before), is away for 2 s from `away` (standby: their simulated player plays), back.
Stream streamOf(int steps, uint32_t join, uint32_t away) {
    Stream s;
    uint32_t prev[2] = {0, 0};
    for (int t = 0; t < steps; ++t) {
        std::vector<InputFrame> f(raid::kMaxInstancePlayers);
        for (int k = 2; k < raid::kMaxInstancePlayers; ++k) f[static_cast<size_t>(k)].flags = InputFrame::Vacant;
        f[0] = input(0, static_cast<uint32_t>(t), prev[0]);
        const uint32_t u = static_cast<uint32_t>(t);
        if (u < join) f[1].flags = InputFrame::Vacant;
        else if (u >= away && u < away + 240) f[1].flags = InputFrame::Standby;
        else f[1] = input(1, u, prev[1]);
        s.frames.push_back(std::move(f));
    }
    return s;
}

}  // namespace

TEST_CASE("raid test relay (2.80): the settings of a relayed test, as the server keeps them, build the same test") {
    raid::RelayedTestConfig c;
    c.branch = "asterion_bras_droit";
    c.zone = 4;
    c.players = 3;
    c.startPhase = 2;
    c.parts = 0x15u;
    c.relics = 3;
    c.skill = 2;
    c.invincible = false;
    const Json j = c.toJson();
    const auto back = raid::RelayedTestConfig::fromJson("zone", j);
    CHECK(back.has_value());
    CHECK(back->branch == c.branch && back->zone == 4 && back->players == 3 && back->startPhase == 2 && back->parts == 0x15u && back->relics == 3);
    CHECK(back->skill == 2 && !back->invincible && !back->fullRaid);
    raid::RelayedTestConfig r;
    r.fullRaid = true;
    r.raidPlayers = 30;    // §41: seven groups (5, 5, 4, 4, 4, 4, 4)
    r.fullGroups = true;  // 2.77.3: « Joueurs par groupe (vous compris) »
    const auto rb = raid::RelayedTestConfig::fromJson("raid", r.toJson());
    CHECK(rb.has_value() && rb->fullRaid && rb->raidPlayers == 30 && rb->fullGroups && rb->seatsPerGroup() == 4);
    // §41: 14 to 56 players; the settings of before (groups, players per group) are refused.
    Json raidBad = r.toJson();
    raidBad["players"] = 13;
    CHECK(!raid::RelayedTestConfig::fromJson("raid", raidBad).has_value());
    raidBad["players"] = 57;
    CHECK(!raid::RelayedTestConfig::fromJson("raid", raidBad).has_value());
    raidBad.erase("players");
    raidBad["groups"] = 3;
    raidBad["bots"] = 2;
    CHECK(!raid::RelayedTestConfig::fromJson("raid", raidBad).has_value());
    // Out of bounds, or a field missing: nothing (the game never builds a test it cannot trust).
    Json bad = j;
    bad["players"] = 12;
    CHECK(!raid::RelayedTestConfig::fromJson("zone", bad).has_value());
    bad = j;
    bad.erase("skill");
    CHECK(!raid::RelayedTestConfig::fromJson("zone", bad).has_value());
    CHECK(!raid::RelayedTestConfig::fromJson("autre", j).has_value());
    // The fingerprint: the game's version and rules, the titan of every branch (the server compares it).
    const std::string fp = raid::testFingerprint();
    CHECK(fp == raid::testFingerprint());
    CHECK(fp.find(COSMIC_VERSION_STRING) == 0);
    CHECK(fp.size() >= 8 && fp.size() <= 64);
}

TEST_CASE("raid test relay (2.80): one zone — the invited player's seat, away then back, « Invincible » ticked mid-test: two computers, the same digest") {
    Svc svc;
    raid::RelayedTestConfig c;
    c.branch = raid::Branch::builtin()->id;
    c.zone = 1;
    c.players = 4;  // the administrator and three simulated players: one is replaced by the invited player
    c.invincible = true;
    const int steps = 1500;
    Stream s = streamOf(steps, 240, 720);
    coopnet::TestSeats t0;
    t0[0] = {0, 0, 1};  // the administrator, invincible (the test's setting)
    coopnet::TestSeats t1 = t0;
    t1[1] = {0, 1, 0};  // the invited player takes the first simulated player's seat
    coopnet::TestSeats t2 = t1;
    t2[1].flags = 1;  // « Invincible » ticked by the administrator during the test
    s.tables = {{0, t0}, {240, t1}, {480, t2}};
    s.checkWire();

    // The administrator's computer (shown) and the invited one's (quiet, the whole history at once).
    auto a = raid::buildZoneTest(svc.ws(), c);
    auto b = raid::buildZoneTest(svc.ws(), c);
    CHECK(a && b);
    b->run().setQuiet(true);
    // 2.79's orders: « Suivez-moi » given by the administrator at step 1000, by the relay (TEST_ORDER).
    const uint32_t follow = static_cast<uint32_t>(raid::bots::OrderKind::Follow) | (300u << 3) | (100u << 14);
    auto orderAt = [&](int t) { return t == 1000 ? follow : 0u; };
    std::vector<uint64_t> digests;
    for (int t = 0; t < steps; ++t) {
        raid::stepRelayed(*a, s.frames[static_cast<size_t>(t)], s.at(static_cast<uint32_t>(t)), orderAt(t));
        if (t == 1000) CHECK(a->lastOrder().kind == raid::bots::OrderKind::Follow);
        if (t == 300) CHECK(a->humanSeats() == 0x2u);  // the seat is the invited player's
        if (t == 300) CHECK(a->run().invincibleSeats() == 0x1u);
        if (t == 600) CHECK(a->run().invincibleSeats() == 0x3u);  // and now invincible
        if (t == 600) CHECK(a->run().world().invincibleSeats() == 0x3u);
        if (t == 800) CHECK(a->humanSeats() == 0u);  // away: their simulated player plays the seat
        if (t % 100 == 99) digests.push_back(a->hash());
    }
    CHECK(a->humanSeats() == 0x2u);  // back
    for (int t = 0, d = 0; t < steps; ++t) {
        raid::stepRelayed(*b, s.frames[static_cast<size_t>(t)], s.at(static_cast<uint32_t>(t)), orderAt(t));
        if (t % 100 == 99) CHECK(b->hash() == digests[static_cast<size_t>(d++)]);
    }
    CHECK(a->hash() == b->hash());
    CHECK(b->lastOrder().kind == raid::bots::OrderKind::Follow);
    CHECK(a->run().takeReports().empty());  // a test never reports anything
    // Without the invited player (their seat left to its simulated player), another game: the seat was theirs.
    Stream alone = s;
    alone.tables = {{0, t0}};
    auto c2 = raid::buildZoneTest(svc.ws(), c);
    for (int t = 0; t < steps; ++t) raid::stepRelayed(*c2, alone.frames[static_cast<size_t>(t)], alone.at(static_cast<uint32_t>(t)));
    CHECK(c2->hash() != a->hash());
}

TEST_CASE("raid test relay (2.80): the whole raid — an invited player in another group, the administrator changing group then watching: the same digest") {
    Svc svc;
    raid::RelayedTestConfig c;
    c.fullRaid = true;
    c.raidPlayers = 21;   // §41: seven groups of 3
    c.fullGroups = true;  // as ADMINISTRATION › Raids launches it (2.77.3): the administrator's place played where they are not
    c.invincible = false;
    const int steps = 900;
    Stream s = streamOf(steps, 120, 500);
    coopnet::TestSeats t0;
    t0[0] = {0, 0, 0};  // the administrator on group 1's free place
    coopnet::TestSeats t1 = t0;
    t1[1] = {1, 1, 0};  // the invited player in group 2
    coopnet::TestSeats t2 = t1;
    t2[0] = {1, 0, 0};  // the administrator joins group 2
    coopnet::TestSeats t3 = t2;
    t3[1].flags = 1;  // « Invincible »
    coopnet::TestSeats t4 = t3;
    t4[0] = {};  // the administrator watches
    s.tables = {{0, t0}, {120, t1}, {240, t2}, {360, t3}, {600, t4}};
    s.checkWire();
    auto a = raid::buildRaidSimulation(svc.ws(), c);
    auto b = raid::buildRaidSimulation(svc.ws(), c);
    CHECK(a && b);
    std::vector<uint64_t> digests;
    for (int t = 0; t < steps; ++t) {
        raid::stepRelayed(*a, s.frames[static_cast<size_t>(t)], s.at(static_cast<uint32_t>(t)));
        if (t == 200) CHECK(a->seated() && a->viewed() == 0);
        if (t == 200) CHECK(a->group(1).session->humanSeats() == 0x2u);
        if (t == 300) CHECK(a->seated() && a->viewed() == 1);
        if (t == 400) CHECK(a->group(1).session->run().invincibleSeats() == 0x2u);
        if (t == 700) CHECK(!a->seated());
        if (t % 100 == 99) digests.push_back(a->hash());
    }
    for (int t = 0, d = 0; t < steps; ++t) {
        raid::stepRelayed(*b, s.frames[static_cast<size_t>(t)], s.at(static_cast<uint32_t>(t)));
        if (t % 100 == 99) CHECK(b->hash() == digests[static_cast<size_t>(d++)]);
    }
    CHECK(a->hash() == b->hash());
    for (const auto& g : a->groups()) CHECK(g->session->run().takeReports().empty());
}

namespace {

/// The relay of a test as the server writes it (server/src/coop.ts), scripted: what it sends once connected.
class ScriptedRelay final : public CoopTransport {
public:
    explicit ScriptedRelay(std::deque<std::vector<uint8_t>> script) : script_(std::move(script)) {}
    bool connect(std::string&) override { return true; }
    bool send(const std::vector<uint8_t>&) override { return true; }
    bool receive(std::deque<std::vector<uint8_t>>& out, int waitMs) override {
        if (!script_.empty()) {
            out.push_back(std::move(script_.front()));
            script_.pop_front();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(waitMs, 2)));
        }
        return true;
    }
    void close() override {}

private:
    std::deque<std::vector<uint8_t>> script_;
};

/// TEST_SEATS byte for byte as coop.ts testSeatsMessage writes it (type, u32 step, u8 count, then group, seat, flags).
std::vector<uint8_t> serverTable(uint32_t step, const std::vector<std::array<uint8_t, 3>>& seats) {
    std::vector<uint8_t> o{19, static_cast<uint8_t>(step), static_cast<uint8_t>(step >> 8), static_cast<uint8_t>(step >> 16), static_cast<uint8_t>(step >> 24),
                           static_cast<uint8_t>(seats.size())};
    for (const auto& s : seats) o.insert(o.end(), s.begin(), s.end());
    return o;
}

}  // namespace

TEST_CASE("raid test relay (2.80): an invited game reads the relay of a test — GO after its start, the places from their steps, « removed »") {
    const std::array<uint8_t, 3> none{0xFF, 0, 0};
    std::deque<std::vector<uint8_t>> script;
    script.push_back(coopnet::welcome(1));  // a late join: no lobby before GO, so the history is caught up
    coopnet::GoInfo go;
    go.players = raid::kMaxInstancePlayers;
    go.delay = 4;
    go.level = "raid_test";
    go.hash = raid::testFingerprint();
    go.attempt = "a";
    script.push_back(coopnet::go(go));
    script.push_back(serverTable(0, {{0, 0, 1}, none, none, none, none, none, none, none}));
    script.push_back(serverTable(3, {{0, 0, 1}, {0, 1, 0}, none, none, none, none, none, none}));
    script.push_back(serverTable(3, {{0, 0, 1}, {0, 9, 0}, none, none, none, none, none, none}));  // a seat past the 8: ignored
    std::vector<InputFrame> frames(static_cast<size_t>(5 * raid::kMaxInstancePlayers));
    script.push_back(coopnet::framesMsg(0, frames.data(), 5, raid::kMaxInstancePlayers));
    script.push_back(serverTable(5, {{0, 0, 1}, {0, 1, 1}, none, none, none, none, none, none}));
    // TEST_ORDER as coop.ts writes it: type 20, u32 step, u8 kind, u16 x, u16 y (« Attendez ici » at 1200 × 400 px).
    script.push_back({20, 6, 0, 0, 0, 2, 44, 1, 100, 0});
    script.push_back(coopnet::lobby(R"({"seats":[{"seat":0,"name":"Orion"},{"seat":1,"name":"Lyra"}]})"));
    script.push_back(coopnet::end(coopnet::EndReason::Removed));
    NetCoopLink link(std::make_unique<ScriptedRelay>(std::move(script)), "Lyra");
    link.start();
    for (int i = 0; i < 400 && link.endCode() < 0; ++i) {
        link.update(0.01f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(link.go().has_value());
    CHECK(link.go()->hash == raid::testFingerprint());
    CHECK(link.joinedLate());
    CHECK(link.seat() == 1);
    CHECK(link.available() == 5u);
    CHECK(!link.testSeatsAt(0)[1].placed());
    CHECK(link.testSeatsAt(2)[0].placed() && link.testSeatsAt(2)[0].invincible());
    CHECK(link.testSeatsAt(3)[1].placed() && link.testSeatsAt(3)[1].seat == 1 && !link.testSeatsAt(3)[1].invincible());
    CHECK(link.testSeatsAt(4)[1].seat == 1);  // the malformed table changed nothing
    CHECK(link.testSeatsAt(99)[1].invincible());  // « Invincible » from step 5 on
    CHECK(link.testOrderAt(5) == 0u);
    CHECK(link.testOrderAt(6) == (2u | (300u << 3) | (100u << 14)));
    CHECK(raid::withPackedOrder(InputFrame{}, link.testOrderAt(6)) == raid::bots::withOrder(InputFrame{}, raid::bots::OrderKind::Wait, {1200.0f, 400.0f}));
    CHECK(link.endCode() == static_cast<int>(coopnet::EndReason::Removed));
    CHECK(link.endReason() == "L'administrateur vous a retiré du test.");
    link.close();
}
