#pragma once

// 2.62: the messages of a team's relay (docs/COOP.md § 4), the same online (the CoopRoom
// Durable Object, server/src/coop.ts) and on the local network (CoopRelay on one PC). Binary,
// little-endian, one type byte then its content. Inputs are compact: each frame is written
// against the previous one of the same player in the message (a step where nothing changed
// takes one byte).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"

namespace cosmic::coopnet {

constexpr uint8_t kProtocol = 1;
/// 2.65: the seats of a relay: 8 for a raid instance (docs/RAIDS.md); a cooperation room keeps
/// 2 or 3 players (kMaxCoopPlayers).
constexpr int kMaxPlayers = 8;
constexpr int kMaxCoopPlayers = 3;
/// Steps of one INPUT or FRAMES message at most.
constexpr int kMaxBatch = 255;
/// A seat silent this long is absent (its frames become standby frames).
constexpr uint32_t kSilenceMs = 3000;
/// An input for a step this far beyond the last one given is refused.
constexpr uint32_t kMaxAhead = 64;
/// A game lasts 2 hours of steps at most.
constexpr uint32_t kMaxTicks = 120u * 7200u;
/// The recorded actions (InputFrame::kActions bits).
constexpr uint32_t kActionMask = (1u << InputFrame::kActions) - 1u;
/// 2.63.5: a community level's package sent by the PC that hosts on the local network: 16 MiB
/// at most (as a download from Cosmic Online), in parts of 48 KiB (a message stays < 64 KiB).
constexpr uint32_t kMaxPackage = 16u << 20;
constexpr uint32_t kPackageChunk = 48u * 1024u;

enum class Msg : uint8_t {
    Hello = 1,        // game → relay: u8 protocol, u8 wanted seat (255: any), str name, [str account, [u8 rules]]
                      // 2.80: `rules`, the rules of cooperation the game plays (kCoopSimulationVersion; none
                      // before 2.80: 3 at most); a room plays its host's, another game is refused (End Rules)
    Input = 2,        // game → relay: u32 first tick, u8 n, n frames
    Ping = 3,         // game → relay: u32 game clock (ms), u16 its last round trip (0xFFFF: none)
    Ready = 4,        // game → relay: u8 ready
    Start = 5,        // game → relay (host, lobby): u8 players
    CatchupDone = 6,  // game → relay: u32 last step simulated
    Leave = 7,        // game → relay
    PackageAsk = 9,   // game → relay (lobby, 2.63.5): u32 offset in the level's package
    Welcome = 10,     // relay → game: u8 seat, u8 protocol
    Lobby = 11,       // relay → game: JSON text
    Go = 12,          // relay → game: u64 seed, u8 players, u8 delay, str level, str hash, str attempt
    Frames = 13,      // relay → game: u32 first tick, u16 n, n × players frames
    Pong = 14,        // relay → game: u32 game clock echoed
    Rejoin = 15,      // relay → game: u8 seat, u32 tick
    End = 16,         // relay → game: u8 reason
    PackagePart = 17, // relay → game (2.63.5): u32 total (0: none), u32 offset, 32 bytes SHA-256 of the whole, the bytes
    RaidState = 18,   // relay → game (2.65, raids): u32 step from which it applies, u32 objectives done (bits);
                      // 2.72 (phase 6, the Cœur d'Asterion): + u32 the heart's shared state (raid::HeartShared packed)
    TestSeats = 19,   // relay → game (2.80, an administrator's test with invited players, docs/RAIDS.md « Tester avec des joueurs invités »): u32 step
                      // from which it applies, u8 count, then per relay seat u8 group (kTestNone), u8 seat of the test, u8 flags
    TestOrder = 20,   // 2.80, a test's order to its simulated players (2.79's wheel): game → relay (its administrator only):
                      // u8 kind (1..6), u16 x, u16 y (4 px each); relay → game: u32 step, then the same
};

/// 2.63.5: the level of a room. `source` "campaign" (`id`: the level of the game) or
/// "community" (`id`: its Cosmic Online id, `version`: its published version); `hash`: its
/// fingerprint, the same on every PC of the team or the game does not start.
struct RoomLevel {
    std::string source = "campaign";
    std::string id, hash, title;
    int version = 0;
    bool community() const { return source == "community"; }
    bool operator==(const RoomLevel&) const = default;
};

/// 2.80: Removed — an administrator's test: its administrator removed this player.
/// 2.80: Rules — the game plays another version of the rules of cooperation than the room (mixed versions
/// would not play the same run: the game has to be updated).
enum class EndReason : uint8_t { Finished = 0, HostLeft = 1, TooLong = 2, Refused = 3, Removed = 4, Rules = 5 };
/// 2.80: the rules of cooperation a HELLO without them says (a game before 2.80).
constexpr uint8_t kRulesBefore280 = 3;

/// 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): where the player of a relay seat plays in an administrator's test with
/// invited players (TEST_SEATS, decided by the relay alone and given at a step to every computer).
constexpr uint8_t kTestNone = 0xFF;
struct TestSeat {
    uint8_t group = kTestNone;  // the group of the test (an index; kTestNone: nowhere, the seat is vacant or watching)
    uint8_t seat = 0;           // the seat in that group (0: the administrator's place)
    uint8_t flags = 0;          // 1: invincible
    bool placed() const { return group != kTestNone; }
    bool invincible() const { return (flags & 1u) != 0; }
    bool operator==(const TestSeat&) const = default;
};
using TestSeats = std::array<TestSeat, kMaxPlayers>;

/// Writes `count` frames, each against the previous one (the first against an empty frame).
void writeFrames(std::vector<uint8_t>& out, const InputFrame* frames, size_t count);
/// Reads them back; false if the bytes are malformed (truncated, unknown bits).
bool readFrames(const uint8_t*& p, const uint8_t* end, InputFrame* out, size_t count);
/// A frame a game may send: the recorded actions only, SkipCinematic as its only flag, a
/// power choice of 1..8 or 255 (never trusted: the relay checks every one).
bool validClientFrame(const InputFrame& f);

struct Writer {
    std::vector<uint8_t>& o;
    void u8(uint8_t v) { o.push_back(v); }
    void u16(uint16_t v) {
        o.push_back(static_cast<uint8_t>(v));
        o.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void str(const std::string& s, size_t max = 255) {
        const size_t n = s.size() < max ? s.size() : max;
        o.push_back(static_cast<uint8_t>(n));
        o.insert(o.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }
};

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    bool need(size_t n) {
        if (!ok || static_cast<size_t>(end - p) < n) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? *p++ : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = static_cast<uint16_t>(p[0] | (p[1] << 8));
        p += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24);
        p += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32();
        uint64_t hi = u32();
        return lo | (hi << 32);
    }
    std::string str(size_t max = 255) {
        const size_t n = u8();
        if (n > max || !need(n)) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    }
    bool done() const { return ok && p == end; }
};

// --------------------------------------------------------------- messages
/// `account`: the Cosmic Online account of the player ("" : none), shown to the team (the
/// online relay uses the session's own; the ranking asks each player to confirm anyway).
/// 2.80: then the rules of cooperation this game plays (`rules`, kCoopSimulationVersion by default).
std::vector<uint8_t> hello(uint8_t wantedSeat, const std::string& name, const std::string& account = {}, int rules = -1);
std::vector<uint8_t> input(uint32_t firstTick, const InputFrame* frames, size_t count);
std::vector<uint8_t> ping(uint32_t clockMs, uint16_t lastRoundTripMs);
std::vector<uint8_t> ready(bool on);
std::vector<uint8_t> start(uint8_t players);
std::vector<uint8_t> catchupDone(uint32_t tick);
std::vector<uint8_t> leave();
std::vector<uint8_t> welcome(uint8_t seat);
std::vector<uint8_t> lobby(const std::string& json);
struct GoInfo {
    uint64_t seed = 0;
    uint8_t players = 0;
    uint8_t delay = 3;
    std::string level, hash, attempt;
};
std::vector<uint8_t> go(const GoInfo& g);
/// `frames`: `count` steps × `players` frames (step by step, seat by seat).
std::vector<uint8_t> framesMsg(uint32_t firstTick, const InputFrame* frames, size_t count, int players);
std::vector<uint8_t> pong(uint32_t clockMs);
std::vector<uint8_t> rejoin(uint8_t seat, uint32_t tick);
std::vector<uint8_t> end(EndReason r);
/// 2.65 (raids): the branch's state (objectives done, bit i = objective i) from step `tick` on.
std::vector<uint8_t> raidState(uint32_t tick, uint32_t mask);
/// 2.74 (phase 6): the same with the shared state of the Cœur d'Asterion (raid::HeartShared::pack):
/// only the relays of the heart's instances send it (an older game, which cannot play the heart, never gets one).
std::vector<uint8_t> raidState(uint32_t tick, uint32_t mask, uint32_t heart);
/// 2.80: TEST_SEATS from step `tick` on; readTestSeats reads its content (after the type byte), false when
/// malformed (more seats than kMaxPlayers, a seat of the test past them, unknown flags).
std::vector<uint8_t> testSeats(uint32_t tick, const TestSeats& seats);
bool readTestSeats(const uint8_t* data, size_t size, uint32_t& tick, TestSeats& out);
/// 2.80: an order of a test's administrator to the simulated players (raid::bots::OrderKind 1..6, its place in
/// 4-pixel units: x 0..2047, y 0..1023), sent to the relay, which gives it to every game from a step. `packed`
/// as raid::bots::withOrder packs it (kind | x << 3 | y << 14).
std::vector<uint8_t> testOrder(uint8_t kind, uint16_t x, uint16_t y);
bool readTestOrder(const uint8_t* data, size_t size, uint32_t& tick, uint32_t& packed);

std::vector<uint8_t> packageAsk(uint32_t offset);
/// `total` 0: this relay has no package to send (the bytes are then empty).
std::vector<uint8_t> packagePart(uint32_t total, uint32_t offset, const std::array<uint8_t, 32>& sha, const uint8_t* data, size_t n);

/// 2.63.5: a level's package received from the PC that hosts, part by part and in order
/// (PACKAGE_PART). Never trusted: a total over kMaxPackage, a part out of order or too long, a
/// total or a SHA-256 that changes are refused. Its SHA-256 is checked (packageMatches) off
/// the main thread before anything is installed.
class PackageReceiver {
public:
    void reset() { *this = PackageReceiver{}; }
    /// The content of one PACKAGE_PART (after its type byte). False when refused (error()).
    bool accept(const uint8_t* data, size_t size);
    /// Every byte announced is there.
    bool complete() const { return total_ > 0 && bytes_.size() == total_; }
    bool failed() const { return !error_.empty(); }
    /// The host has no package for its level (none kept on its PC).
    bool unavailable() const { return unavailable_; }
    const std::string& error() const { return error_; }
    uint32_t received() const { return static_cast<uint32_t>(bytes_.size()); }
    uint32_t total() const { return total_; }
    uint32_t nextOffset() const { return received(); }
    /// The SHA-256 the host announced (lower-case hex).
    std::string sha256() const;
    std::vector<uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<uint8_t> bytes_;
    uint32_t total_ = 0;
    std::array<uint8_t, 32> sha_{};
    bool unavailable_ = false;
    std::string error_;
};

/// True when `bytes` (kMaxPackage at most) has the SHA-256 `sha256Hex` (hex, any case).
bool packageMatches(const std::vector<uint8_t>& bytes, const std::string& sha256Hex);

/// The input delay for the worst round trip measured (docs/COOP.md: 3 to 15 steps).
int inputDelayFor(int worstRoundTripMs);

}  // namespace cosmic::coopnet
