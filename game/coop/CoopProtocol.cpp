#include "game/coop/CoopProtocol.h"

#include <algorithm>
#include <cmath>

namespace cosmic::coopnet {

namespace {
enum Bits : uint8_t { Held = 1, Pressed = 2, Released = 4, MoveX = 8, MoveY = 16, Flags = 32, Choice = 64 };
constexpr uint8_t kKnownFlags = InputFrame::SkipCinematic | InputFrame::Standby;

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

std::vector<uint8_t> hello(uint8_t wantedSeat, const std::string& name, const std::string& account) {
    auto o = msg(Msg::Hello);
    Writer w{o};
    w.u8(kProtocol);
    w.u8(wantedSeat);
    w.str(name, 24);
    if (!account.empty()) w.str(account, 64);
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

int inputDelayFor(int worstRoundTripMs) {
    const double steps = std::ceil((std::max(0, worstRoundTripMs) + 16) / (1000.0 / 120.0));
    return std::clamp(static_cast<int>(steps), 3, 15);
}

}  // namespace cosmic::coopnet
