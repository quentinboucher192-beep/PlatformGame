#include "game/coop/CoopRelay.h"

#include <algorithm>

#include "engine/core/Json.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/Replay.h"
#include "packaging/Sha256.h"

namespace cosmic {

using namespace coopnet;

void CoopRelay::setLevel(const std::string& id, const std::string& hash, const std::string& title, const std::string& source, int version) {
    if (started_) return;
    level_ = id;
    hash_ = hash;
    title_ = title;
    source_ = source == "community" ? "community" : "campaign";
    version_ = version;
    broadcastLobby();
}

void CoopRelay::setPackage(std::shared_ptr<const std::vector<uint8_t>> bytes) {
    if (bytes && (bytes->empty() || bytes->size() > kMaxPackage)) bytes.reset();
    package_ = std::move(bytes);
    packageSha_ = {};
    if (package_) {
        Sha256 sha;
        sha.update(package_->data(), package_->size());
        packageSha_ = sha.finish();
    }
}

int CoopRelay::seatOf(int conn) const {
    for (int s = 0; s < kSeats; ++s)
        if (seats_[s].conn == conn && conn >= 0) return s;
    return -1;
}

void CoopRelay::connected(int conn, uint32_t /*nowMs*/) { waiting_.push_back(conn); }

void CoopRelay::disconnected(int conn, uint32_t /*nowMs*/) {
    waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), conn), waiting_.end());
    const int s = seatOf(conn);
    if (s < 0) return;
    if (started_) {
        markAbsent(s);
        return;
    }
    // In the lobby: the place is free again, the next ones move up (seats 0 .. n-1 filled).
    for (int k = s; k + 1 < kSeats; ++k) seats_[k] = seats_[k + 1];
    seats_[kSeats - 1] = Seat{};
    for (int k = s; k < kSeats; ++k)
        if (seats_[k].conn >= 0) send_(seats_[k].conn, welcome(static_cast<uint8_t>(k)));
    broadcastLobby();
}

void CoopRelay::drop(int conn) {
    if (close_) close_(conn);
    disconnected(conn, 0);
}

void CoopRelay::markAbsent(int seat) {
    Seat& s = seats_[seat];
    if (!s.absent) {
        s.absent = true;
        s.absentFrom = next_;
    }
    s.rejoinAt = 0;
    s.pending.clear();
    s.have.clear();
    s.conn = -1;
}

void CoopRelay::received(int conn, const uint8_t* data, size_t size, uint32_t nowMs) {
    if (size == 0 || size > 64 * 1024) {
        drop(conn);
        return;
    }
    Reader r{data + 1, data + size};
    const auto type = static_cast<Msg>(data[0]);
    const bool waiting = std::find(waiting_.begin(), waiting_.end(), conn) != waiting_.end();
    if (waiting) {
        if (type != Msg::Hello) {
            drop(conn);
            return;
        }
        hello(conn, r, nowMs);
        return;
    }
    const int seat = seatOf(conn);
    if (seat < 0) {
        drop(conn);
        return;
    }
    Seat& s = seats_[seat];
    s.lastSeen = nowMs;
    switch (type) {
        case Msg::Ping: {
            const uint32_t clock = r.u32();
            const uint16_t rtt = r.u16();
            if (!r.done()) return drop(conn);
            s.rttMs = rtt == 0xFFFF ? -1 : rtt;
            send_(conn, pong(clock));
            break;
        }
        case Msg::Ready: {
            const uint8_t on = r.u8();
            if (!r.done() || started_) return drop(conn);
            s.ready = on != 0;
            broadcastLobby();
            break;
        }
        case Msg::Start: {
            r.u8();
            if (!r.done() || started_ || seat != 0) return drop(conn);
            startGame();
            break;
        }
        case Msg::Input: {
            if (!started_) return drop(conn);
            input(seat, r);
            if (!r.ok) return drop(conn);
            flush();
            break;
        }
        case Msg::CatchupDone: {
            r.u32();
            if (!r.done() || !started_ || !s.absent) return drop(conn);
            // Back at a step none has reached yet (every game has sent at most `delay` ahead).
            const uint32_t at = next_ + static_cast<uint32_t>(delay_) + 1;
            if (at - s.absentFrom > static_cast<uint32_t>(GameWorld::kStandbyLimitTicks)) {
                send_(conn, end(EndReason::Refused));
                return drop(conn);
            }
            s.rejoinAt = at;
            sendAll(rejoin(static_cast<uint8_t>(seat), at));
            break;
        }
        case Msg::PackageAsk: {
            // 2.63.5: a part of the community level's package (lobby only, in order, bounded).
            const uint32_t offset = r.u32();
            if (!r.done() || started_) return drop(conn);
            if (!package_) {
                send_(conn, packagePart(0, 0, {}, nullptr, 0));
                break;
            }
            const size_t size = package_->size();
            if (offset >= size) return drop(conn);
            const size_t n = std::min<size_t>(kPackageChunk, size - offset);
            send_(conn, packagePart(static_cast<uint32_t>(size), offset, packageSha_, package_->data() + offset, n));
            break;
        }
        case Msg::Leave:
            return drop(conn);
        default:
            return drop(conn);
    }
}

void CoopRelay::hello(int conn, Reader& r, uint32_t now) {
    const uint8_t protocol = r.u8();
    const uint8_t wanted = r.u8();
    std::string name = r.str(24);
    std::string account;
    if (r.ok && r.p != r.end) account = r.str(64);
    if (!r.done() || protocol != kProtocol) {
        send_(conn, end(EndReason::Refused));
        return drop(conn);
    }
    waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), conn), waiting_.end());
    if (name.empty()) name = "Joueur";
    int seat = -1;
    if (!started_) {
        if (wanted < kSeats && seats_[wanted].conn < 0 && (wanted == 0 || seats_[wanted - 1].conn >= 0)) seat = wanted;
        for (int k = 0; k < kSeats && seat < 0; ++k)
            if (seats_[k].conn < 0) seat = k;
    } else if (wanted < players_ && seats_[wanted].absent && seats_[wanted].conn < 0) {
        seat = wanted;  // coming back: its own seat, still in standby until it caught up
    }
    if (seat < 0) {
        send_(conn, end(EndReason::Refused));
        if (close_) close_(conn);
        return;
    }
    Seat& s = seats_[seat];
    s.conn = conn;
    s.name = name;
    s.account = account;
    s.lastSeen = now;
    send_(conn, welcome(static_cast<uint8_t>(seat)));
    if (started_) {
        GoInfo g{seed_, static_cast<uint8_t>(players_), static_cast<uint8_t>(delay_), level_, hash_, attempt_};
        send_(conn, go(g));
        flush();
        sendHistory(conn);
    } else {
        broadcastLobby();
    }
}

void CoopRelay::input(int seat, Reader& r) {
    Seat& s = seats_[seat];
    const uint32_t first = r.u32();
    const uint8_t n = r.u8();
    if (!r.ok || n == 0) {
        r.ok = false;
        return;
    }
    InputFrame frames[kMaxBatch];
    if (!readFrames(r.p, r.end, frames, n) || r.p != r.end) {
        r.ok = false;
        return;
    }
    for (uint8_t i = 0; i < n; ++i)
        if (!validClientFrame(frames[i])) {
            r.ok = false;
            return;
        }
    if (s.absent && s.rejoinAt == 0) return;  // not back yet: its inputs wait for its step of return
    for (uint8_t i = 0; i < n; ++i) {
        const uint32_t t = first + i;
        if (t < next_ || (s.absent && t < s.rejoinAt)) continue;  // too late, or before its return
        const uint32_t k = t - next_;
        if (k > kMaxAhead) {
            r.ok = false;
            return;
        }
        if (s.pending.size() <= k) {
            s.pending.resize(k + 1);
            s.have.resize(k + 1, false);
        }
        s.pending[k] = frames[i];
        s.have[k] = true;
    }
}

void CoopRelay::startGame() {
    int n = 0;
    int worst = 0;
    for (int k = 0; k < kSeats; ++k)
        if (seats_[k].conn >= 0) {
            ++n;
            worst = std::max(worst, seats_[k].rttMs);
        }
    if (n < 2 || level_.empty()) return;
    for (int k = 0; k < n; ++k)
        if (!seats_[k].ready && k != 0) return;  // everybody ready (the host starts)
    started_ = true;
    players_ = n;
    delay_ = inputDelayFor(worst);
    seed_ = levelSeed(level_);
    attempt_ = makeUuidV4();
    // The first `delay` steps are empty for everybody (no input can reach them in time).
    history_.assign(static_cast<size_t>(delay_ * players_), InputFrame{});
    next_ = static_cast<uint32_t>(delay_);
    flushed_ = 0;
    GoInfo g{seed_, static_cast<uint8_t>(players_), static_cast<uint8_t>(delay_), level_, hash_, attempt_};
    sendAll(go(g));
    flush();
}

void CoopRelay::sendHistory(int conn) {
    // Every step given so far, from the first (the game that comes back plays them again).
    for (uint32_t t = 0; t < next_; t += kMaxBatch) {
        const uint32_t n = std::min<uint32_t>(kMaxBatch, next_ - t);
        send_(conn, framesMsg(t, &history_[static_cast<size_t>(t) * static_cast<size_t>(players_)], n, players_));
    }
}

void CoopRelay::poll(uint32_t nowMs) {
    for (int k = 0; k < kSeats; ++k) {
        Seat& s = seats_[k];
        if (s.conn < 0) continue;
        if (nowMs - s.lastSeen > kSilenceMs) {
            // Silent: absent from the first step not given yet (the same for everyone).
            const int conn = s.conn;
            if (close_) close_(conn);
            disconnected(conn, nowMs);
        }
    }
    flush();
}

void CoopRelay::flush() {
    if (!started_) return;
    // Every step whose inputs are all known (or decided: standby) is given.
    for (;;) {
        if (next_ >= kMaxTicks) {
            sendAll(end(EndReason::TooLong));
            started_ = false;
            return;
        }
        bool complete = true;
        for (int k = 0; k < players_ && complete; ++k) {
            Seat& s = seats_[k];
            if (s.absent && (s.rejoinAt == 0 || next_ < s.rejoinAt)) continue;
            if (s.pending.empty() || !s.have.front()) complete = false;
        }
        if (!complete) break;
        for (int k = 0; k < players_; ++k) {
            Seat& s = seats_[k];
            InputFrame f;
            if (s.absent && (s.rejoinAt == 0 || next_ < s.rejoinAt)) {
                f.flags = InputFrame::Standby;
            } else {
                if (s.absent) {  // its step of return: playing again
                    s.absent = false;
                    s.rejoinAt = 0;
                }
                f = s.pending.front();
            }
            if (!s.pending.empty()) {
                s.pending.pop_front();
                s.have.pop_front();
            }
            history_.push_back(f);
        }
        ++next_;
    }
    while (flushed_ < next_) {
        const uint32_t n = std::min<uint32_t>(kMaxBatch, next_ - flushed_);
        sendAll(framesMsg(flushed_, &history_[static_cast<size_t>(flushed_) * static_cast<size_t>(players_)], n, players_));
        flushed_ += n;
    }
}

std::string CoopRelay::lobbyJson() const {
    Json seats = Json::array();
    for (int k = 0; k < kSeats; ++k) {
        const Seat& s = seats_[k];
        if (s.conn < 0 && !started_) continue;
        seats.push_back({{"seat", k}, {"name", s.name}, {"account", s.account}, {"ready", s.ready || k == 0}, {"ping", s.rttMs},
                         {"connected", s.conn >= 0 && !s.absent}});
    }
    return Json{{"level", level_},     {"hash", hash_},           {"title", title_},
                {"source", source_},   {"version", version_},     {"size", package_ ? package_->size() : size_t{0}},
                {"started", started_}, {"seats", seats}}
        .dump();
}

void CoopRelay::sendAll(const std::vector<uint8_t>& bytes) {
    for (const Seat& s : seats_)
        if (s.conn >= 0) send_(s.conn, bytes);
}

void CoopRelay::broadcastLobby() {
    if (started_) return;
    sendAll(lobby(lobbyJson()));
}

}  // namespace cosmic
