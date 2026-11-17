#pragma once

// 2.62: the relay of a team on the local network (docs/COOP.md § 4): one PC of the team runs it
// (LanHost) and every game, its own included, connects to it. The same rules as the online
// relay (server/src/coop.ts, CoopRoom): it orders the inputs, gives each step's team frame to
// everyone, and decides alone, for everyone, that an absent player's frames are standby frames.
// Pure logic: the transport (sockets) and the clock are given by its owner.

#include <cstdint>
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/coop/CoopProtocol.h"

namespace cosmic {

class CoopRelay {
public:
    /// `send(conn, bytes)`: a message to one connection.
    using Send = std::function<void(int conn, const std::vector<uint8_t>& bytes)>;
    using Close = std::function<void(int conn)>;

    CoopRelay(Send send, Close close) : send_(std::move(send)), close_(std::move(close)) {}

    /// The level the host chose (lobby): its id, its fingerprint, its name; 2.63.5: its source
    /// ("campaign" or "community") and its published version (community).
    void setLevel(const std::string& id, const std::string& hash, const std::string& title, const std::string& source = "campaign",
                  int version = 0);
    /// 2.63.5: the package of a community level (kMaxPackage at most; null: none kept on this
    /// PC), sent part by part, in the lobby, to a game that does not have the level (PACKAGE_ASK).
    void setPackage(std::shared_ptr<const std::vector<uint8_t>> bytes);
    /// 2.65: a raid instance (docs/RAIDS.md): 8 seats; any seat starts it (one player is
    /// enough); a seat nobody holds is vacant (InputFrame::Vacant frames) and a player may take
    /// it while the game runs (catch-up, then REJOIN); a seat absent for 60 s becomes vacant.
    void setRaid(bool on) { raid_ = on; }
    bool raid() const { return raid_; }
    /// 2.65 (raids): the branch's state (objectives done). Once started, it applies from the
    /// first step not given yet, for everyone (RAID_STATE), and is kept for the catch-ups.
    void setRaidState(uint32_t mask);
    void connected(int conn, uint32_t nowMs);
    void disconnected(int conn, uint32_t nowMs);
    void received(int conn, const uint8_t* data, size_t size, uint32_t nowMs);
    /// Silences, broadcast of the steps completed (call often, e.g. every 4 ms).
    void poll(uint32_t nowMs);

    bool started() const { return started_; }
    uint32_t nextTick() const { return next_; }
    int players() const { return players_; }
    int seatOf(int conn) const;
    bool absent(int seat) const { return seat >= 0 && seat < kSeats && seats_[seat].absent; }
    bool vacant(int seat) const { return seat >= 0 && seat < kSeats && seats_[seat].vacant; }
    /// The lobby as JSON (also what LOBBY carries).
    std::string lobbyJson() const;

    static constexpr int kSeats = coopnet::kMaxPlayers;

private:
    struct Seat {
        int conn = -1;  // -1: nobody (or disconnected)
        std::string name;
        std::string account;  // as the game said (never trusted: the ranking asks each player)
        bool ready = false;
        int rttMs = -1;
        uint32_t lastSeen = 0;
        bool absent = false;
        uint32_t absentFrom = 0;
        uint32_t rejoinAt = 0;  // 0: not rejoining
        bool vacant = false;    // 2.65 (raids): nobody holds it (Vacant frames)
        std::deque<InputFrame> pending;  // inputs for steps next_, next_ + 1...
        std::deque<bool> have;
    };
    int maxSeats() const { return raid_ ? coopnet::kMaxPlayers : coopnet::kMaxCoopPlayers; }
    void hello(int conn, coopnet::Reader& r, uint32_t now);
    void input(int seat, coopnet::Reader& r);
    void markAbsent(int seat);
    void broadcastLobby();
    void startGame();
    void sendHistory(int conn);
    void flush();
    void drop(int conn);
    void sendAll(const std::vector<uint8_t>& bytes);

    Send send_;
    Close close_;
    Seat seats_[kSeats];
    std::vector<int> waiting_;  // connections before their HELLO
    std::string level_, hash_, title_, source_ = "campaign";
    int version_ = 0;
    std::shared_ptr<const std::vector<uint8_t>> package_;
    std::array<uint8_t, 32> packageSha_{};
    bool started_ = false;
    int players_ = 0;
    int delay_ = 3;
    uint64_t seed_ = 0;
    std::string attempt_;
    uint32_t next_ = 0;
    std::vector<InputFrame> history_;  // every step given, players frames each
    uint32_t flushed_ = 0;             // steps already sent
    bool raid_ = false;
    uint32_t raidMask_ = 0;
    std::vector<std::pair<uint32_t, uint32_t>> raidStates_;  // (step, mask), in order
};

}  // namespace cosmic
