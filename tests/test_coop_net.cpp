// 2.62: cooperation on the network (docs/COOP.md § 4 and § 5): compact inputs on the wire, the
// relay of the local network (the same rules as the online one: it orders the inputs, gives the
// same frames to everyone, decides the standby frames of an absent player alone, takes a game
// back from a step it chooses), and the team replay (format 5) that plays the team's run again.

#include <map>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopLevels.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopRelay.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "packaging/Sha256.h"
#include "tests/TestFramework.h"

using namespace cosmic;
using namespace cosmic::coopnet;

namespace {

InputFrame right() {
    InputFrame f;
    f.held = 1u << static_cast<int>(Action::MoveRight);
    f.moveX = 127;
    return f;
}

/// What each connection received, message by message.
struct Wire {
    std::map<int, std::vector<std::vector<uint8_t>>> got;
    std::vector<int> closed;
    CoopRelay relay{[this](int c, const std::vector<uint8_t>& b) { got[c].push_back(b); }, [this](int c) { closed.push_back(c); }};

    void send(int conn, const std::vector<uint8_t>& m, uint32_t now = 0) { relay.received(conn, m.data(), m.size(), now); }
    std::vector<std::vector<uint8_t>> of(int conn, Msg t) {
        std::vector<std::vector<uint8_t>> out;
        for (const auto& m : got[conn])
            if (!m.empty() && m[0] == static_cast<uint8_t>(t)) out.push_back(m);
        return out;
    }
    /// Every team frame a connection received, by step (players frames each).
    std::vector<std::vector<InputFrame>> frames(int conn, int players) {
        std::vector<std::vector<InputFrame>> out;
        for (const auto& m : of(conn, Msg::Frames)) {
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

}  // namespace

TEST_CASE("coop net: inputs are compact on the wire and read back exactly") {
    std::vector<InputFrame> frames(240, right());
    frames[100].pressed = 1u << static_cast<int>(Action::Jump);
    frames[100].held |= 1u << static_cast<int>(Action::Jump);
    frames[200] = InputFrame{};
    frames[201].flags = InputFrame::SkipCinematic;
    frames[202].choice = InputFrame::kChoiceLeave;
    std::vector<uint8_t> bytes;
    writeFrames(bytes, frames.data(), frames.size());
    CHECK(bytes.size() < frames.size() + 40);  // a step that changes nothing: one byte
    std::vector<InputFrame> back(frames.size());
    const uint8_t* p = bytes.data();
    CHECK(readFrames(p, bytes.data() + bytes.size(), back.data(), back.size()));
    CHECK(back == frames);
    CHECK(p == bytes.data() + bytes.size());
    // Never trusted: an action bit beyond the recorded ones, an unknown flag, a choice of 9.
    std::vector<uint8_t> bad = {1, 0, 0, 0, 0x80};
    const uint8_t* q = bad.data();
    InputFrame f;
    CHECK(!readFrames(q, bad.data() + bad.size(), &f, 1));
    bad = {32, 4};
    q = bad.data();
    CHECK(!readFrames(q, bad.data() + bad.size(), &f, 1));
    bad = {64, 9};
    q = bad.data();
    CHECK(!readFrames(q, bad.data() + bad.size(), &f, 1));
    // A game never sends Standby (the relay alone decides it).
    InputFrame standby;
    standby.flags = InputFrame::Standby;
    CHECK(!validClientFrame(standby));
    CHECK(validClientFrame(right()));
    // The input delay follows the worst round trip: 3 images at least, 15 at most.
    CHECK(inputDelayFor(0) == 3);
    CHECK(inputDelayFor(40) == 7);
    CHECK(inputDelayFor(5000) == 15);
}

TEST_CASE("coop net: the relay orders the inputs and gives everyone the same frames") {
    Wire w;
    w.relay.setLevel("sylvanox_01", std::string(64, 'a'), "Test");
    w.relay.connected(1, 0);
    w.relay.connected(2, 0);
    w.send(1, hello(255, "Nova", "acc-1"));
    w.send(2, hello(255, "Lyra"));
    CHECK(w.relay.seatOf(1) == 0);
    CHECK(w.relay.seatOf(2) == 1);
    CHECK(w.relay.lobbyJson().find("acc-1") != std::string::npos);
    // Only the host starts, once everybody is ready.
    w.send(1, start(2));
    CHECK(!w.relay.started());
    w.send(2, ready(true));
    w.send(1, start(2));
    CHECK(w.relay.started());
    const auto goMsg = w.of(1, Msg::Go);
    CHECK(goMsg.size() == 1);
    const uint8_t delay = goMsg[0][10];
    CHECK(delay >= 3);
    // The empty first steps, then each step once both inputs came.
    std::vector<InputFrame> a(3, right()), b(2, InputFrame{});
    w.send(1, input(delay, a.data(), a.size()));
    w.relay.poll(10);
    CHECK(w.frames(1, 2).size() == delay);  // player 2 missing: nothing more
    w.send(2, input(delay, b.data(), b.size()));
    const auto fa = w.frames(1, 2), fb = w.frames(2, 2);
    CHECK(fa == fb);
    CHECK(fa.size() == static_cast<size_t>(delay) + 2);
    CHECK(fa[delay][0] == right());
    // A game sending Standby: refused, closed.
    InputFrame s;
    s.flags = InputFrame::Standby;
    w.send(2, input(delay + 2, &s, 1));
    CHECK(!w.closed.empty());
}

TEST_CASE("coop net: an absent player gets standby frames, then comes back at a step the relay chooses") {
    Wire w;
    w.relay.setLevel("sylvanox_01", std::string(64, 'a'), "Test");
    w.relay.connected(1, 0);
    w.relay.connected(2, 0);
    w.send(1, hello(255, "Nova"));
    w.send(2, hello(255, "Lyra"));
    w.send(2, ready(true));
    w.send(1, start(2));
    const uint8_t delay = w.of(1, Msg::Go)[0][10];
    std::vector<InputFrame> a(10, right()), b(4, InputFrame{});
    w.send(1, input(delay, a.data(), a.size()), 0);
    w.send(2, input(delay, b.data(), b.size()), 0);
    CHECK(w.frames(1, 2).size() == static_cast<size_t>(delay) + 4);
    // Player 2 goes silent: absent from the first step not given yet, for everyone.
    w.send(1, ping(100, 20), 2000);
    w.relay.poll(4000);
    CHECK(w.relay.absent(1));
    auto f = w.frames(1, 2);
    CHECK(f.size() == static_cast<size_t>(delay) + 10);
    CHECK(f[delay + 4][1].flags == InputFrame::Standby);
    CHECK(f[delay + 3][1].flags == 0);
    // It comes back: the whole history, then a step of return after the catch-up.
    w.relay.connected(3, 4000);
    w.send(3, hello(1, "Lyra"), 4000);
    CHECK(w.relay.seatOf(3) == 1);
    CHECK(w.frames(3, 2) == w.frames(1, 2));
    w.send(3, catchupDone(static_cast<uint32_t>(f.size()) - 1), 4000);
    const auto rj = w.of(1, Msg::Rejoin);
    CHECK(rj.size() == 1);
    Reader r{rj[0].data() + 1, rj[0].data() + rj[0].size()};
    CHECK(r.u8() == 1);
    const uint32_t at = r.u32();
    CHECK(at > w.relay.nextTick());
    // Until then its frames stay standby frames; from then, its own inputs.
    std::vector<InputFrame> more(40, right());
    w.send(1, input(delay + 10, more.data(), more.size()), 4000);
    std::vector<InputFrame> back(40, right());
    w.send(3, input(at, back.data(), back.size()), 4000);
    f = w.frames(1, 2);
    CHECK(f.size() > at);
    CHECK(f[at - 1][1].flags == InputFrame::Standby);
    CHECK(f[at][1] == right());
    CHECK(!w.relay.absent(1));
}

TEST_CASE("coop net: a team replay (format 5) is read back and plays the team's run again exactly") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    auto level = content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    if (!level) return;
    WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
    auto makeWorld = [&] {
        auto w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->setTeamSize(2);
        w->setKeysPerSlot(true);
        w->load(*level);
        w->seedGameplay(levelSeed("sylvanox_01"));
        return w;
    };
    auto live = makeWorld();
    ReplayRecorder rec;
    rec.begin("sylvanox_01", std::string(64, 'a'), makeUuidV4(), live->gameplaySeed());
    rec.beginTeam(2);
    Random rnd(5);
    InputFrame prev[2];
    int editTick = -1;
    for (int t = 0; t < 120 * 20; ++t) {
        InputFrame f[2];
        for (int k = 0; k < 2; ++k) {
            if (rnd.chance(0.7f)) f[k] = right();
            if (rnd.chance(0.3f)) f[k].held |= 1u << static_cast<int>(Action::Jump);
            f[k].pressed = f[k].held & ~prev[k].held;
            f[k].released = prev[k].held & ~f[k].held;
            prev[k] = f[k];
        }
        if (live->cinematic().active() && live->cinematic().skippable()) f[0].flags |= InputFrame::SkipCinematic;
        // A step where player 2 stands in play: walking left from there changes the run.
        if (editTick < 0 && t >= 300 && live->state() == WorldState::Playing && !live->cinematic().active() &&
            live->member(1).presence == GameWorld::Presence::Playing && live->playerAt(1).grounded())
            editTick = t;
        rec.recordTeamInputs(f, 2);
        stepTeam(*live, f, 2, kSimDt);
        rec.tick(*live);
    }
    rec.finishAttempt(*live, live->timer().elapsed());
    const uint64_t final = simulationStateHash(*live);
    ReplayData data = rec.take();
    CHECK(data.teamSize() == 2);
    auto bytes = data.encode();
    std::string err;
    auto back = ReplayData::decode(bytes, &err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->formatVersion == ReplayData::kFormatTeam);
    CHECK(back->teamSize() == 2);
    CHECK(back->team[0].inputs == data.team[0].inputs);
    CHECK(back->team[0].frames.size() == back->frames.size());
    CHECK(back->coopVersion == 1);
    auto view = makeWorld();
    ReplaySimulator sim(*back, *view);
    while (sim.step()) {
    }
    CHECK(sim.finished());
    CHECK(!sim.desynced());
    CHECK(simulationStateHash(*view) == final);
    // Other inputs of player 2 (half a second walking left): the digests tell.
    ReplayData edited = *back;
    CHECK(editTick >= 0);
    if (editTick < 0) return;
    for (int k = 0; k < 60; ++k) {
        InputFrame& f = edited.team[0].inputs[static_cast<size_t>(editTick + k)];
        f = InputFrame{};
        f.held = 1u << static_cast<int>(Action::MoveLeft);
        f.moveX = -127;
        if (k == 0) f.pressed = f.held;
    }
    auto other = makeWorld();
    ReplaySimulator cheat(edited, *other);
    while (cheat.step()) {
    }
    CHECK(cheat.desynced());
}

// ------------------------------------------------------------- 2.63.5: community levels

namespace {

/// A copy of a level of the game under another id, as a level of this PC (a community install).
fs::path copyLevel(const ContentDatabase& content, const std::string& from, const std::string& id) {
    const LevelEntry* e = content.level(from);
    CHECK(e != nullptr);
    auto j = FileUtil::loadJson(e->path);
    CHECK(j.has_value());
    (*j)["id"] = id;
    fs::create_directories(Paths::userLevelsDir());
    const fs::path file = Paths::userLevelsDir() / (id + ".json");
    CHECK(FileUtil::saveJson(file, *j));
    return file;
}

}  // namespace

TEST_CASE("coop: the lobby offers the community's EXTRÊME levels, in their published version only") {
    ContentDatabase content;
    content.reload();
    const fs::path extreme = copyLevel(content, "confins_01", "coop_comm_extreme");
    const fs::path other = copyLevel(content, "confins_02", "coop_comm_other");
    const fs::path plain = copyLevel(content, "sylvanox_01", "coop_comm_plain");
    content.reload();
    auto lvl = content.loadLevel("coop_comm_extreme");
    CHECK(lvl.has_value() && lvl->extreme && lvl->oneLife());
    const std::string fp = fingerprintGameplay(content, *lvl);
    const std::string id = "0b6c58b4-5d0e-4c3a-9f1e-6a2d9e0c1b11";
    Json installs = Json::object();
    installs["coop_comm_extreme"] = {{"id", id}, {"version", 3}, {"level_hash", fp}, {"title", "Le Gouffre"}, {"author", "Lyra"}};
    // A copy that differs from its published version, and a level that is not EXTRÊME: never offered.
    installs["coop_comm_other"] = {{"id", "1b6c58b4-5d0e-4c3a-9f1e-6a2d9e0c1b11"}, {"version", 1}, {"level_hash", std::string(64, 'e')}, {"title", "Autre"}};
    installs["coop_comm_plain"] = {{"id", "2b6c58b4-5d0e-4c3a-9f1e-6a2d9e0c1b11"}, {"version", 1}, {"level_hash", std::string(64, 'e')}, {"title", "Calme"}};
    const fs::path downloads = Paths::userDir() / "test_tmp" / "coop_downloads";
    fs::create_directories(downloads);
    CHECK(FileUtil::writeBinary(communityPackagePath(downloads, id, 3), std::vector<uint8_t>{1, 2, 3}));
    const auto levels = findCoopLevels(content, installs, downloads);
    int campaign = 0, community = 0;
    bool campaignFirst = true;
    const CoopLevel* found = nullptr;
    for (const CoopLevel& l : levels) {
        if (l.community) {
            ++community;
            if (l.communityId == id) found = &l;
        } else {
            ++campaign;
            if (community > 0) campaignFirst = false;
        }
    }
    CHECK(campaign >= 1);  // the campaign's EXTRÊME levels are still there, first
    CHECK(campaignFirst);
    CHECK(community == 1);
    CHECK(found != nullptr);
    if (found) {
        CHECK(found->id == "coop_comm_extreme");
        CHECK(found->name == "Le Gouffre");
        CHECK(found->author == "Lyra");
        CHECK(found->version == 3);
        CHECK(found->hash == fp);
        CHECK(found->package == communityPackagePath(downloads, id, 3));  // what the host sends on the local network
    }
    // A room's level: the same id, version and fingerprint, or nothing (never another version).
    CHECK(findRoomLevel(levels, id, 3, fp) == found);
    CHECK(findRoomLevel(levels, id, 4, fp) == nullptr);
    CHECK(findRoomLevel(levels, id, 3, std::string(64, 'f')) == nullptr);
    CHECK(findRoomLevel(levels, "1b6c58b4-5d0e-4c3a-9f1e-6a2d9e0c1b11", 1, std::string(64, 'e')) == nullptr);
    std::error_code ec;
    for (const auto& f : {extreme, other, plain}) fs::remove(f, ec);
    fs::remove_all(downloads, ec);
}

TEST_CASE("coop net: the host sends a community level's package, bounded and checked by its SHA-256") {
    Wire w;
    w.relay.setLevel("0b6c58b4-5d0e-4c3a-9f1e-6a2d9e0c1b11", std::string(64, 'a'), "Le Gouffre", "community", 3);
    w.relay.connected(1, 0);
    w.relay.connected(2, 0);
    w.send(1, hello(255, "Nova"));
    w.send(2, hello(255, "Lyra"));
    const Json lobby = Json::parse(w.relay.lobbyJson());
    CHECK(jstr(lobby, "source") == "community");
    CHECK(jget<int>(lobby, "version", 0) == 3);
    // No package kept on the host's PC: it says so (the guest downloads it from Cosmic Online).
    w.send(2, packageAsk(0));
    {
        const auto parts = w.of(2, Msg::PackagePart);
        CHECK(parts.size() == 1);
        PackageReceiver none;
        CHECK(!none.accept(parts[0].data() + 1, parts[0].size() - 1));
        CHECK(none.unavailable());
    }
    // A package of 130 000 bytes: three parts, asked one after the other.
    std::vector<uint8_t> package(130000);
    Random rng(7);
    for (auto& b : package) b = static_cast<uint8_t>(rng.rangeInt(0, 255));
    w.relay.setPackage(std::make_shared<const std::vector<uint8_t>>(package));
    w.got[2].clear();
    PackageReceiver rx;
    w.send(2, packageAsk(0));
    for (int guard = 0; guard < 10 && !rx.complete(); ++guard) {
        const auto parts = w.of(2, Msg::PackagePart);
        CHECK(!parts.empty());
        if (parts.empty()) break;
        CHECK(parts.back().size() <= 64 * 1024);  // one message of the protocol
        CHECK(rx.accept(parts.back().data() + 1, parts.back().size() - 1));
        if (!rx.complete()) w.send(2, packageAsk(rx.nextOffset()));
    }
    CHECK(rx.complete());
    CHECK(rx.total() == package.size());
    CHECK(w.of(2, Msg::PackagePart).size() == 3);
    const std::string sha = rx.sha256();
    CHECK(sha == Sha256::hex(package));
    std::vector<uint8_t> got = rx.take();
    CHECK(got == package);
    CHECK(packageMatches(got, sha));
    // One byte changed on the way: refused before anything is installed.
    got[777] ^= 0x40;
    CHECK(!packageMatches(got, sha));
    CHECK(!packageMatches({}, sha));
    // Never trusted: a part out of order, or a package over 16 MiB.
    const std::array<uint8_t, 32> hash{};
    const std::vector<uint8_t> chunk(100, 1);
    PackageReceiver order;
    auto p0 = packagePart(1000, 0, hash, chunk.data(), chunk.size());
    auto p2 = packagePart(1000, 300, hash, chunk.data(), chunk.size());
    CHECK(order.accept(p0.data() + 1, p0.size() - 1));
    CHECK(!order.accept(p2.data() + 1, p2.size() - 1));
    CHECK(order.failed());
    PackageReceiver big;
    auto huge = packagePart(kMaxPackage + 1, 0, hash, chunk.data(), chunk.size());
    CHECK(!big.accept(huge.data() + 1, huge.size() - 1));
    CHECK(big.failed());
    // An offset beyond the package, or a request once the game started: the connection closes.
    w.send(2, packageAsk(static_cast<uint32_t>(package.size()) + 5));
    CHECK(!w.closed.empty());
}
