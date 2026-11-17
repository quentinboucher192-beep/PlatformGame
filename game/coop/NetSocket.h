#pragma once

// 2.62: plain sockets for the cooperation on the local network (docs/COOP.md § 4): TCP for the
// relay of one PC (each message preceded by its length), UDP to find the games of the network.
// BSD sockets, Winsock on Windows. Non-blocking: only the network threads use them.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace cosmic::net {

using Fd = intptr_t;
constexpr Fd kNoSocket = -1;
constexpr uint16_t kLanPort = 47620;       // TCP: the relay of the PC that hosts
constexpr uint16_t kDiscoveryPort = 47621;  // UDP: « COSMIC-COOP? »
constexpr size_t kMaxMessage = 64 * 1024;

/// Winsock started (once); always true elsewhere.
bool startup();
void closeSocket(Fd fd);
/// A TCP socket listening on every interface (non-blocking); kNoSocket on failure.
Fd tcpListen(uint16_t port);
/// A connection waiting on `listener` (non-blocking), kNoSocket if none.
Fd tcpAccept(Fd listener, std::string* peer = nullptr);
/// Connects to `host:port` within `timeoutMs` (then non-blocking); kNoSocket on failure.
Fd tcpConnect(const std::string& host, uint16_t port, int timeoutMs);
/// A UDP socket bound to `port` (0: any), able to broadcast.
Fd udpSocket(uint16_t port);
bool udpBroadcast(Fd fd, uint16_t port, const void* data, size_t size);
bool udpSendTo(Fd fd, uint32_t ipv4, uint16_t port, const void* data, size_t size);
/// A datagram waiting (non-blocking): its size, 0 if none, -1 on error.
int udpReceive(Fd fd, void* buf, size_t cap, uint32_t* ipv4, uint16_t* port);
/// Waits until one of `fds` can be read or `timeoutMs` passed.
void waitReadable(const std::vector<Fd>& fds, int timeoutMs);
std::string ipv4Text(uint32_t ipv4);

/// A TCP connection carrying whole messages: u32 length (little-endian) then the bytes.
class FramedConn {
public:
    explicit FramedConn(Fd fd = kNoSocket) : fd_(fd) {}
    ~FramedConn() { close(); }
    FramedConn(const FramedConn&) = delete;
    FramedConn& operator=(const FramedConn&) = delete;
    Fd fd() const { return fd_; }
    bool open() const { return fd_ != kNoSocket; }
    void close();
    /// Queues a message and writes what the socket takes now.
    void send(const std::vector<uint8_t>& msg);
    /// Writes what waits; false when the connection is broken.
    bool flush();
    /// Reads what came; complete messages go to `out`. False when closed or malformed.
    bool receive(std::deque<std::vector<uint8_t>>& out);

private:
    Fd fd_;
    std::vector<uint8_t> in_, out_;
};

}  // namespace cosmic::net
