#include "game/coop/LanHost.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"
#include "game/coop/CoopRelay.h"
#include "game/coop/NetSocket.h"

namespace cosmic {

namespace {
constexpr const char* kAsk = "COSMIC-COOP?";
constexpr const char* kAnswer = "COSMIC-COOP!";
uint32_t nowMs() { return static_cast<uint32_t>(SDL_GetTicks()); }
}  // namespace

bool LanHost::start(const std::string& name, const coopnet::RoomLevel& level, const fs::path& package) {
    stop();
    listen_ = net::tcpListen(net::kLanPort);
    if (listen_ == net::kNoSocket) return false;
    udp_ = net::udpSocket(net::kDiscoveryPort);
    port_ = net::kLanPort;
    name_ = name.substr(0, 24);
    level_ = level;
    package_ = package;
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void LanHost::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    net::closeSocket(listen_);
    net::closeSocket(udp_);
    listen_ = udp_ = net::kNoSocket;
    running_ = false;
}

void LanHost::setLevel(const coopnet::RoomLevel& level, const fs::path& package) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    package_ = package;
    levelChanged_ = true;
}

void LanHost::run() {
    std::map<int, std::unique_ptr<net::FramedConn>> conns;
    int nextId = 1;
    CoopRelay relay(
        [&](int conn, const std::vector<uint8_t>& bytes) {
            auto it = conns.find(conn);
            if (it != conns.end()) it->second->send(bytes);
        },
        [&](int conn) {
            auto it = conns.find(conn);
            if (it != conns.end()) it->second->close();
        });
    std::string title;  // this thread's copy (setLevel changes it from the main thread)
    // The level, and the package of a community level read here (16 MiB at most), never by
    // the game's main thread.
    auto apply = [&](const coopnet::RoomLevel& l, const fs::path& package) {
        relay.setLevel(l.id, l.hash, l.title, l.source, l.version);
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::error_code ec;
        if (l.community() && !package.empty()) {
            const auto size = fs::file_size(package, ec);
            if (!ec && size > 0 && size <= coopnet::kMaxPackage)
                if (auto b = FileUtil::readBinary(package)) bytes = std::make_shared<const std::vector<uint8_t>>(std::move(*b));
        }
        relay.setPackage(std::move(bytes));
        title = l.title;
    };
    {
        coopnet::RoomLevel l;
        fs::path package;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            l = level_;
            package = package_;
        }
        apply(l, package);
    }
    std::deque<std::vector<uint8_t>> msgs;
    while (!stop_) {
        std::vector<net::Fd> fds{listen_, udp_};
        for (auto& [id, c] : conns) fds.push_back(c->fd());
        net::waitReadable(fds, 2);
        const uint32_t now = nowMs();
        {
            std::optional<std::pair<coopnet::RoomLevel, fs::path>> changed;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (levelChanged_) changed.emplace(level_, package_);
                levelChanged_ = false;
            }
            if (changed) apply(changed->first, changed->second);
        }
        // New games.
        for (int k = 0; k < 4; ++k) {
            net::Fd fd = net::tcpAccept(listen_);
            if (fd == net::kNoSocket) break;
            if (conns.size() >= 8) {  // never more than a few games at once
                net::closeSocket(fd);
                continue;
            }
            const int id = nextId++;
            conns[id] = std::make_unique<net::FramedConn>(fd);
            relay.connected(id, now);
        }
        // Searches of the network: who hosts, which level, how many.
        if (udp_ != net::kNoSocket) {
            char buf[256];
            uint32_t ip = 0;
            uint16_t port = 0;
            for (int n; (n = net::udpReceive(udp_, buf, sizeof buf - 1, &ip, &port)) > 0;) {
                buf[n] = 0;
                if (std::string(buf, static_cast<size_t>(n)) != kAsk) continue;
                Json info = Json::parse(relay.lobbyJson(), nullptr, false);
                const int players = info.is_object() ? static_cast<int>(info.value("seats", Json::array()).size()) : 0;
                std::string text = std::string(kAnswer) +
                                   Json{{"host", name_}, {"title", title}, {"port", port_}, {"players", players}, {"started", relay.started()}}.dump();
                net::udpSendTo(udp_, ip, port, text.data(), text.size());
            }
        }
        // Messages of the games.
        for (auto it = conns.begin(); it != conns.end();) {
            net::FramedConn& c = *it->second;
            msgs.clear();
            const bool ok = c.open() && c.receive(msgs);
            for (auto& m : msgs) relay.received(it->first, m.data(), m.size(), now);
            if (!ok || !c.open() || !c.flush()) {
                const int id = it->first;
                it = conns.erase(it);
                relay.disconnected(id, now);
            } else {
                ++it;
            }
        }
        relay.poll(now);
        for (auto& [id, c] : conns) c->flush();
    }
}

// ------------------------------------------------------------------ search

void LanSearch::start() {
    stop();
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void LanSearch::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<LanGame> LanSearch::games() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LanGame> out;
    const uint64_t now = SDL_GetTicks();
    for (const auto& [g, at] : found_)
        if (now - at < 4000) out.push_back(g);
    return out;
}

void LanSearch::run() {
    net::Fd udp = net::udpSocket(0);
    if (udp == net::kNoSocket) return;
    uint64_t lastAsk = 0;
    while (!stop_) {
        const uint64_t now = SDL_GetTicks();
        if (now - lastAsk > 1000) {
            lastAsk = now;
            net::udpBroadcast(udp, net::kDiscoveryPort, kAsk, std::char_traits<char>::length(kAsk));
            net::udpSendTo(udp, 0x7F000001u, net::kDiscoveryPort, kAsk, std::char_traits<char>::length(kAsk));  // this PC too
        }
        net::waitReadable({udp}, 50);
        char buf[512];
        uint32_t ip = 0;
        uint16_t port = 0;
        for (int n; (n = net::udpReceive(udp, buf, sizeof buf - 1, &ip, &port)) > 0;) {
            std::string text(buf, static_cast<size_t>(n));
            if (text.rfind(kAnswer, 0) != 0) continue;
            Json j = Json::parse(text.substr(std::char_traits<char>::length(kAnswer)), nullptr, false);
            if (!j.is_object()) continue;  // never trusted: a malformed answer is ignored
            LanGame g;
            g.address = net::ipv4Text(ip);
            g.port = static_cast<uint16_t>(jget<int>(j, "port", net::kLanPort));
            g.host = jstr(j, "host").substr(0, 24);
            g.title = jstr(j, "title").substr(0, 80);
            g.players = std::clamp(jget<int>(j, "players", 0), 0, 3);
            g.started = jget<bool>(j, "started", false);
            std::lock_guard<std::mutex> lock(mutex_);
            bool known = false;
            for (auto& [f, at] : found_)
                if (f.address == g.address && f.port == g.port) {
                    f = g;
                    at = SDL_GetTicks();
                    known = true;
                }
            if (!known) found_.emplace_back(g, SDL_GetTicks());
        }
    }
    net::closeSocket(udp);
}

}  // namespace cosmic
