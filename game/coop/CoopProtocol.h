#pragma once

// 2.62: the messages of a team's relay (docs/COOP.md § 4), the same online (the CoopRoom
// Durable Object, server/src/coop.ts) and on the local network (CoopRelay on one PC). Binary,
// little-endian, one type byte then its content. Inputs are compact: each frame is written
// against the previous one of the same player in the message (a step where nothing changed
// takes one byte).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"

namespace cosmic::coopnet {

constexpr uint8_t kProtocol = 1;
constexpr int kMaxPlayers = 3;
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

enum class Msg : uint8_t {
    Hello = 1,        // game → relay: u8 protocol, u8 wanted seat (255: any), str name, [str account]
    Input = 2,        // game → relay: u32 first tick, u8 n, n frames
    Ping = 3,         // game → relay: u32 game clock (ms), u16 its last round trip (0xFFFF: none)
    Ready = 4,        // game → relay: u8 ready
    Start = 5,        // game → relay (host, lobby): u8 players
    CatchupDone = 6,  // game → relay: u32 last step simulated
    Leave = 7,        // game → relay
    Welcome = 10,     // relay → game: u8 seat, u8 protocol
    Lobby = 11,       // relay → game: JSON text
    Go = 12,          // relay → game: u64 seed, u8 players, u8 delay, str level, str hash, str attempt
    Frames = 13,      // relay → game: u32 first tick, u16 n, n × players frames
    Pong = 14,        // relay → game: u32 game clock echoed
    Rejoin = 15,      // relay → game: u8 seat, u32 tick
    End = 16,         // relay → game: u8 reason
};

enum class EndReason : uint8_t { Finished = 0, HostLeft = 1, TooLong = 2, Refused = 3 };

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
std::vector<uint8_t> hello(uint8_t wantedSeat, const std::string& name, const std::string& account = {});
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

/// The input delay for the worst round trip measured (docs/COOP.md: 3 to 15 steps).
int inputDelayFor(int worstRoundTripMs);

}  // namespace cosmic::coopnet
