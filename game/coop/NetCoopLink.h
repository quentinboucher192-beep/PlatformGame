#pragma once

// 2.62: a game of a team on the network (docs/COOP.md § 4): the connection to the relay (the
// online room or a PC of the local network), its lobby, then the lockstep. A thread of its
// own reads and writes the socket; the game takes what came in update() (main thread). The
// team frames are kept for the whole game (a game that comes back plays them again).

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/Json.h"
#include "game/coop/CoopLink.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopTransport.h"

namespace cosmic {

class NetCoopLink : public CoopLink {
public:
    /// `wantedSeat`: the seat to come back to (255: any). `rejoining`: a game that left.
    NetCoopLink(std::unique_ptr<CoopTransport> transport, std::string name, uint8_t wantedSeat = 255, std::string account = {});
    ~NetCoopLink() override;

    void start();
    void close();
    /// Something that must live as long as the connection (the relay of this PC, LanHost).
    void keepAlive(std::shared_ptr<void> p) { keep_ = std::move(p); }

    // ------------------------------------------------------------- lobby
    bool connecting() const { return !connected_ && error_.empty(); }
    bool connected() const { return connected_; }
    const std::string& error() const { return error_; }
    int seat() const { return seat_; }
    /// 2.75 (raids): the seat a relay gives a socket that only watches (a spectator: its group was
    /// wiped, or it watches another group of its branch): seat() is -1, it never sends an input.
    static constexpr uint8_t kWatcherSeat = 0xFE;
    bool watcher() const { return watcher_; }
    /// What the relay says of the lobby (seats, level, ready), null before it said anything.
    const Json& lobby() const { return lobby_; }
    void setReady(bool on);
    /// The host starts the game (everybody ready).
    void startGame(int players);
    /// 2.63.5 (local network): asks the PC that hosts for its community level's package, part
    /// after part (each part asks the next one); package() tells how far it went.
    void requestPackage();
    const coopnet::PackageReceiver& package() const { return package_; }
    bool receivingPackage() const { return askingPackage_; }
    std::vector<uint8_t> takePackage() { return package_.take(); }
    /// The game started (GO came): what it plays.
    const std::optional<coopnet::GoInfo>& go() const { return go_; }
    /// This game joined a game already started: it plays the steps given so far again first.
    bool joinedLate() const override { return joinedLate_; }

    // --------------------------------------------------------------- game
    void pushLocal(uint32_t tick, const std::vector<std::pair<int, InputFrame>>& frames) override;
    bool frameFor(uint32_t tick, std::vector<InputFrame>& out) override;
    int inputDelay() const override { return go_ ? go_->delay : 0; }
    std::string waitingFor() const override;
    int pingMs(int seat) const override;
    void update(float dt) override;
    std::string endReason() const override { return endReason_; }
    /// 2.74: the link died while connected (no END from the relay): a raid instance connects again.
    bool connectionLost() const { return lost_; }
    uint32_t available() const override;
    /// After the catch-up: plays again from the step the relay will give (REJOIN).
    void catchupDone(uint32_t tick) override;
    /// The step from which this game's inputs count again (0: always, or not decided yet).
    uint32_t rejoinAt() const override { return rejoinAt_; }
    bool waitingRejoin() const { return joinedLate_ && rejoinAt_ == 0; }
    /// The end of the attempt (its steps): the online relay notes the inputs played (ranking).
    void result(uint32_t ticks) override;
    uint32_t raidStateAt(uint32_t tick) const override;
    uint32_t raidHeartAt(uint32_t tick) const override;

private:
    void run();
    void post(std::vector<uint8_t> msg);
    void handle(const std::vector<uint8_t>& msg);
    void sendInputs();

    std::unique_ptr<CoopTransport> transport_;
    std::shared_ptr<void> keep_;  // destroyed after the thread (declared first)
    std::string name_;
    uint8_t wantedSeat_;
    std::string account_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> threadDone_{false};
    std::mutex queueMutex_;
    std::deque<std::vector<uint8_t>> inbox_, outbox_;
    std::string threadError_;  // set by the thread (queueMutex_)

    bool connected_ = false;
    std::string error_, endReason_;
    int seat_ = -1;
    bool watcher_ = false;  // 2.75
    Json lobby_;
    std::optional<coopnet::GoInfo> go_;
    bool joinedLate_ = false;
    bool sawLobby_ = false;
    bool lost_ = false;  // 2.74: the link died while connected (not an END)
    coopnet::PackageReceiver package_;
    bool askingPackage_ = false;
    uint32_t rejoinAt_ = 0;
    int rttMs_ = -1;
    uint32_t lastPing_ = 0;
    // Inputs of this game not yet sent (consecutive steps from pendingFrom_).
    uint32_t pendingFrom_ = 0;
    std::vector<InputFrame> pending_;
    // Every team frame given (players frames per step); read by the catch-up thread too.
    mutable std::mutex framesMutex_;
    std::vector<InputFrame> frames_;
    uint32_t ticks_ = 0;
    std::vector<std::pair<uint32_t, uint32_t>> raidStates_;  // 2.65: (step, mask), in order (framesMutex_)
    std::vector<uint32_t> raidHearts_;                       // 2.72: the heart's state of each, 0 when none
};

}  // namespace cosmic
