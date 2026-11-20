#include "game/coop/CoopProtocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "game/online/LevelFingerprint.h"
#include "packaging/Sha256.h"

namespace cosmic::coopnet {

namespace {
enum Bits : uint8_t { Held = 1, Pressed = 2, Released = 4, MoveX = 8, MoveY = 16, Flags = 32, Choice = 64 };
// 2.65: Vacant (raids), set by the relay only like Standby.
constexpr uint8_t kKnownFlags = InputFrame::SkipCinematic | InputFrame::Standby | InputFrame::Vacant;

bool validChoice(uint8_t c) { return c == 0 || c == InputFrame::kChoiceLeave || (c >= 1 && c <= 8); }
}  // namespace

void writeFrames(std::vector<uint8_t>& out, const InputFrame* frames, size_t count) {
    Writer w{out};
    InputFrame prev;
    for (size_t i = 0; i < count; ++i) {
        const InputFrame& f = frames[i];
        uint8_t mask = 0;
        if (f.held != prev.held) mask |= Held;
        if (f.pressed) mask |= Pressed;
        if (f.released) mask |= Released;
        if (f.moveX != prev.moveX) mask |= MoveX;
        if (f.moveY != prev.moveY) mask |= MoveY;
        if (f.flags) mask |= Flags;
        if (f.choice) mask |= Choice;
        w.u8(mask);
        if (mask & Held) w.u32(f.held);
        if (mask & Pressed) w.u32(f.pressed);
        if (mask & Released) w.u32(f.released);
        if (mask & MoveX) w.u8(static_cast<uint8_t>(f.moveX));
        if (mask & MoveY) w.u8(static_cast<uint8_t>(f.moveY));
        if (mask & Flags) w.u8(f.flags);
        if (mask & Choice) w.u8(f.choice);
        prev = f;
    }
}

bool readFrames(const uint8_t*& p, const uint8_t* end, InputFrame* out, size_t count) {
    Reader r{p, end};
    InputFrame prev;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t mask = r.u8();
        if (!r.ok || (mask & 0x80)) return false;
        InputFrame f;
        f.held = (mask & Held) ? r.u32() : prev.held;
        f.pressed = (mask & Pressed) ? r.u32() : 0;
        f.released = (mask & Released) ? r.u32() : 0;
        f.moveX = (mask & MoveX) ? static_cast<int8_t>(r.u8()) : prev.moveX;
        f.moveY = (mask & MoveY) ? static_cast<int8_t>(r.u8()) : prev.moveY;
        f.flags = (mask & Flags) ? r.u8() : 0;
        f.choice = (mask & Choice) ? r.u8() : 0;
        if (!r.ok) return false;
        if ((f.held | f.pressed | f.released) & ~kActionMask) return false;
        if ((f.flags & ~kKnownFlags) || !validChoice(f.choice)) return false;
        if (f.moveX == -128 || f.moveY == -128) return false;  // the stick is -127..127
        out[i] = f;
        prev = f;
    }
    p = r.p;
    return true;
}

bool validClientFrame(const InputFrame& f) {
    if ((f.held | f.pressed | f.released) & ~kActionMask) return false;
    if (f.flags & ~static_cast<uint8_t>(InputFrame::SkipCinematic)) return false;  // never Standby
    return validChoice(f.choice) && f.moveX != -128 && f.moveY != -128;
}

namespace {
std::vector<uint8_t> msg(Msg t) { return {static_cast<uint8_t>(t)}; }
}  // namespace

std::vector<uint8_t> hello(uint8_t wantedSeat, const std::string& name, const std::string& account, int rules) {
    auto o = msg(Msg::Hello);
    Writer w{o};
    w.u8(kProtocol);
    w.u8(wantedSeat);
    w.str(name, 24);
    w.str(account, 64);  // 2.80: always (empty: none), the rules after it
    w.u8(static_cast<uint8_t>(rules < 0 ? kCoopSimulationVersion : rules));
    return o;
}

std::vector<uint8_t> input(uint32_t firstTick, const InputFrame* frames, size_t count) {
    auto o = msg(Msg::Input);
    Writer w{o};
    count = std::min<size_t>(count, kMaxBatch);
    w.u32(firstTick);
    w.u8(static_cast<uint8_t>(count));
    writeFrames(o, frames, count);
    return o;
}

std::vector<uint8_t> ping(uint32_t clockMs, uint16_t lastRoundTripMs) {
    auto o = msg(Msg::Ping);
    Writer w{o};
    w.u32(clockMs);
    w.u16(lastRoundTripMs);
    return o;
}

std::vector<uint8_t> ready(bool on) {
    auto o = msg(Msg::Ready);
    Writer{o}.u8(on ? 1 : 0);
    return o;
}

std::vector<uint8_t> start(uint8_t players) {
    auto o = msg(Msg::Start);
    Writer{o}.u8(players);
    return o;
}

std::vector<uint8_t> catchupDone(uint32_t tick) {
    auto o = msg(Msg::CatchupDone);
    Writer{o}.u32(tick);
    return o;
}

std::vector<uint8_t> leave() { return msg(Msg::Leave); }

std::vector<uint8_t> welcome(uint8_t seat) {
    auto o = msg(Msg::Welcome);
    Writer w{o};
    w.u8(seat);
    w.u8(kProtocol);
    return o;
}

std::vector<uint8_t> lobby(const std::string& json) {
    auto o = msg(Msg::Lobby);
    o.insert(o.end(), json.begin(), json.end());
    return o;
}

std::vector<uint8_t> go(const GoInfo& g) {
    auto o = msg(Msg::Go);
    Writer w{o};
    w.u64(g.seed);
    w.u8(g.players);
    w.u8(g.delay);
    w.str(g.level, 64);
    w.str(g.hash, 64);
    w.str(g.attempt, 64);
    return o;
}

std::vector<uint8_t> framesMsg(uint32_t firstTick, const InputFrame* frames, size_t count, int players) {
    auto o = msg(Msg::Frames);
    Writer w{o};
    count = std::min<size_t>(count, kMaxBatch);
    w.u32(firstTick);
    w.u16(static_cast<uint16_t>(count));
    // Seat by seat, each seat's frames against its previous one: the long runs of the same
    // input of a player cost one byte per step.
    std::vector<InputFrame> seat(count);
    for (int s = 0; s < players; ++s) {
        for (size_t i = 0; i < count; ++i) seat[i] = frames[i * static_cast<size_t>(players) + static_cast<size_t>(s)];
        writeFrames(o, seat.data(), count);
    }
    return o;
}

std::vector<uint8_t> pong(uint32_t clockMs) {
    auto o = msg(Msg::Pong);
    Writer{o}.u32(clockMs);
    return o;
}

std::vector<uint8_t> rejoin(uint8_t seat, uint32_t tick) {
    auto o = msg(Msg::Rejoin);
    Writer w{o};
    w.u8(seat);
    w.u32(tick);
    return o;
}

std::vector<uint8_t> end(EndReason r) {
    auto o = msg(Msg::End);
    Writer{o}.u8(static_cast<uint8_t>(r));
    return o;
}

std::vector<uint8_t> raidState(uint32_t tick, uint32_t mask) {
    auto o = msg(Msg::RaidState);
    Writer w{o};
    w.u32(tick);
    w.u32(mask);
    return o;
}

std::vector<uint8_t> testSeats(uint32_t tick, const TestSeats& seats) {
    std::vector<uint8_t> o{static_cast<uint8_t>(Msg::TestSeats)};
    Writer w{o};
    w.u32(tick);
    w.u8(static_cast<uint8_t>(seats.size()));
    for (const TestSeat& s : seats) {
        w.u8(s.group);
        w.u8(s.seat);
        w.u8(s.flags);
    }
    return o;
}

bool readTestSeats(const uint8_t* data, size_t size, uint32_t& tick, TestSeats& out) {
    Reader r{data, data + size};
    tick = r.u32();
    const uint8_t n = r.u8();
    if (!r.ok || n > kMaxPlayers) return false;
    out = TestSeats{};
    for (uint8_t k = 0; k < n; ++k) {
        TestSeat s;
        s.group = r.u8();
        s.seat = r.u8();
        s.flags = r.u8();
        if (!r.ok || (s.placed() && s.seat >= kMaxPlayers) || (s.flags & ~1u)) return false;
        out[k] = s;
    }
    return r.done();
}

std::vector<uint8_t> testOrder(uint8_t kind, uint16_t x, uint16_t y) {
    std::vector<uint8_t> o{static_cast<uint8_t>(Msg::TestOrder)};
    Writer w{o};
    w.u8(kind);
    w.u16(x);
    w.u16(y);
    return o;
}

bool readTestOrder(const uint8_t* data, size_t size, uint32_t& tick, uint32_t& packed) {
    Reader r{data, data + size};
    tick = r.u32();
    const uint32_t kind = r.u8(), x = r.u16(), y = r.u16();
    if (!r.done() || kind < 1 || kind > 6 || x > 2047 || y > 1023) return false;
    packed = kind | (x << 3) | (y << 14);
    return true;
}

std::vector<uint8_t> raidState(uint32_t tick, uint32_t mask, uint32_t heart) {
    auto o = raidState(tick, mask);
    Writer{o}.u32(heart);
    return o;
}

std::vector<uint8_t> packageAsk(uint32_t offset) {
    auto o = msg(Msg::PackageAsk);
    Writer{o}.u32(offset);
    return o;
}

std::vector<uint8_t> packagePart(uint32_t total, uint32_t offset, const std::array<uint8_t, 32>& sha, const uint8_t* data, size_t n) {
    auto o = msg(Msg::PackagePart);
    Writer w{o};
    w.u32(total);
    w.u32(offset);
    o.insert(o.end(), sha.begin(), sha.end());
    if (data && n > 0) o.insert(o.end(), data, data + n);
    return o;
}

bool PackageReceiver::accept(const uint8_t* data, size_t size) {
    if (failed() || unavailable_ || complete()) return false;
    Reader r{data, data + size};
    const uint32_t total = r.u32();
    const uint32_t offset = r.u32();
    std::array<uint8_t, 32> sha{};
    if (r.need(32)) {
        std::copy(r.p, r.p + 32, sha.begin());
        r.p += 32;
    }
    if (!r.ok) {
        error_ = "Le niveau envoyé par l'hôte est illisible : il n'est pas installé.";
        return false;
    }
    const size_t n = static_cast<size_t>(r.end - r.p);
    if (total == 0) {
        unavailable_ = true;  // the host keeps no package of its level
        return false;
    }
    if (total > kMaxPackage) {
        error_ = "Le niveau de l'hôte est trop volumineux (16 Mo au plus) : il n'est pas reçu.";
        return false;
    }
    if (total_ == 0) {
        total_ = total;
        sha_ = sha;
        bytes_.reserve(total);
    }
    if (total != total_ || sha != sha_ || offset != bytes_.size() || n == 0 || n > kPackageChunk || bytes_.size() + n > total_) {
        error_ = "Le niveau envoyé par l'hôte est incohérent : il n'est pas installé.";
        bytes_.clear();
        return false;
    }
    bytes_.insert(bytes_.end(), r.p, r.end);
    return true;
}

std::string PackageReceiver::sha256() const {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    for (uint8_t b : sha_) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 15]);
    }
    return s;
}

bool packageMatches(const std::vector<uint8_t>& bytes, const std::string& sha256Hex) {
    if (bytes.empty() || bytes.size() > kMaxPackage || sha256Hex.size() != 64) return false;
    std::string want = sha256Hex;
    for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return Sha256::hex(bytes) == want;
}

int inputDelayFor(int worstRoundTripMs) {
    const double steps = std::ceil((std::max(0, worstRoundTripMs) + 16) / (1000.0 / 120.0));
    return std::clamp(static_cast<int>(steps), 3, 15);
}

}  // namespace cosmic::coopnet
