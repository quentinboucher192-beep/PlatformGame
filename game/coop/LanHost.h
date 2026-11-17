#pragma once

// 2.62: cooperation on the local network without any server (docs/COOP.md § 4). One PC hosts:
// the relay (CoopRelay) runs in a thread of its own on TCP port 47620 and answers the
// searches on UDP port 47621. Every game, the host's own included, connects to it.
// LanSearch finds the games hosted on the network.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/Paths.h"
#include "game/coop/CoopProtocol.h"

namespace cosmic {

class LanHost {
public:
    ~LanHost() { stop(); }
    /// Starts hosting (`name`: shown to the others). False when the port is taken. 2.63.5:
    /// `package`: the .cplanet of a community level kept on this PC ("" : none), sent to a game
    /// of the local network that does not have it (read by the relay's thread, never here).
    bool start(const std::string& name, const coopnet::RoomLevel& level, const fs::path& package = {});
    void stop();
    bool running() const { return running_; }
    /// The host changes the level before the start.
    void setLevel(const coopnet::RoomLevel& level, const fs::path& package = {});
    uint16_t port() const { return port_; }

private:
    void run();
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::string name_;
    coopnet::RoomLevel level_;
    fs::path package_;
    bool levelChanged_ = false;
    uint16_t port_ = 0;
    intptr_t listen_ = -1, udp_ = -1;
};

/// A game hosted on the local network, as its host answered.
struct LanGame {
    std::string address;  // IPv4
    uint16_t port = 0;
    std::string host, title;
    int players = 0;
    bool started = false;
};

class LanSearch {
public:
    ~LanSearch() { stop(); }
    void start();
    void stop();
    std::vector<LanGame> games();

private:
    void run();
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<std::pair<LanGame, uint64_t>> found_;  // with the time it last answered
};

}  // namespace cosmic
