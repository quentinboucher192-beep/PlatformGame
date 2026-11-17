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

namespace cosmic {

class LanHost {
public:
    ~LanHost() { stop(); }
    /// Starts hosting (`name`: shown to the others). False when the port is taken.
    bool start(const std::string& name, const std::string& levelId, const std::string& levelHash, const std::string& levelTitle);
    void stop();
    bool running() const { return running_; }
    /// The host changes the level before the start.
    void setLevel(const std::string& id, const std::string& hash, const std::string& title);
    uint16_t port() const { return port_; }

private:
    void run();
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::string name_, level_, hash_, title_;
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
