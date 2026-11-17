#include "game/coop/NetSocket.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace cosmic::net {

namespace {

#if defined(_WIN32)
using RawSocket = SOCKET;
RawSocket raw(Fd fd) { return static_cast<RawSocket>(fd); }
bool wouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void setNonBlocking(RawSocket s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}
constexpr int kSendFlags = 0;
#else
using RawSocket = int;
RawSocket raw(Fd fd) { return static_cast<RawSocket>(fd); }
bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
void setNonBlocking(RawSocket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;  // a peer gone: an error, never SIGPIPE
#else
constexpr int kSendFlags = 0;
#endif
#endif

Fd wrap(RawSocket s) {
#if defined(_WIN32)
    return s == INVALID_SOCKET ? kNoSocket : static_cast<Fd>(s);
#else
    return s < 0 ? kNoSocket : static_cast<Fd>(s);
#endif
}

void noDelay(RawSocket s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

}  // namespace

bool startup() {
#if defined(_WIN32)
    static const bool ok = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

void closeSocket(Fd fd) {
    if (fd == kNoSocket) return;
#if defined(_WIN32)
    closesocket(raw(fd));
#else
    ::close(raw(fd));
#endif
}

Fd tcpListen(uint16_t port) {
    if (!startup()) return kNoSocket;
    RawSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (wrap(s) == kNoSocket) return kNoSocket;
    int one = 1;
#if defined(_WIN32)
    // Windows: SO_REUSEADDR would let a second game take the same port; this one refuses it.
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), sizeof one);
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 8) != 0) {
        closeSocket(wrap(s));
        return kNoSocket;
    }
    setNonBlocking(s);
    return wrap(s);
}

Fd tcpAccept(Fd listener, std::string* peer) {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    RawSocket s = ::accept(raw(listener), reinterpret_cast<sockaddr*>(&a), &len);
    if (wrap(s) == kNoSocket) return kNoSocket;
    setNonBlocking(s);
    noDelay(s);
    if (peer) *peer = ipv4Text(ntohl(a.sin_addr.s_addr));
    return wrap(s);
}

Fd tcpConnect(const std::string& host, uint16_t port, int timeoutMs) {
    if (!startup()) return kNoSocket;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return kNoSocket;
    RawSocket s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (wrap(s) == kNoSocket) {
        freeaddrinfo(res);
        return kNoSocket;
    }
    setNonBlocking(s);
    noDelay(s);
    int r = ::connect(s, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
    freeaddrinfo(res);
    if (r != 0 && !wouldBlock()) {
        closeSocket(wrap(s));
        return kNoSocket;
    }
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (::select(static_cast<int>(s) + 1, nullptr, &w, nullptr, &tv) <= 0) {
        closeSocket(wrap(s));
        return kNoSocket;
    }
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    if (err != 0) {
        closeSocket(wrap(s));
        return kNoSocket;
    }
    return wrap(s);
}

Fd udpSocket(uint16_t port) {
    if (!startup()) return kNoSocket;
    RawSocket s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wrap(s) == kNoSocket) return kNoSocket;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        closeSocket(wrap(s));
        return kNoSocket;
    }
    setNonBlocking(s);
    return wrap(s);
}

bool udpSendTo(Fd fd, uint32_t ipv4, uint16_t port, const void* data, size_t size) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ipv4);
    a.sin_port = htons(port);
    return ::sendto(raw(fd), static_cast<const char*>(data), static_cast<int>(size), 0, reinterpret_cast<sockaddr*>(&a), sizeof a) ==
           static_cast<int>(size);
}

bool udpBroadcast(Fd fd, uint16_t port, const void* data, size_t size) { return udpSendTo(fd, 0xFFFFFFFFu, port, data, size); }

int udpReceive(Fd fd, void* buf, size_t cap, uint32_t* ipv4, uint16_t* port) {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    const int n = static_cast<int>(::recvfrom(raw(fd), static_cast<char*>(buf), static_cast<int>(cap), 0, reinterpret_cast<sockaddr*>(&a), &len));
    if (n < 0) {
#if defined(_WIN32)
        // An answer « port unreachable » to an earlier datagram: nothing to read, not an error.
        if (WSAGetLastError() == WSAECONNRESET) return 0;
#endif
        return wouldBlock() ? 0 : -1;
    }
    if (ipv4) *ipv4 = ntohl(a.sin_addr.s_addr);
    if (port) *port = ntohs(a.sin_port);
    return n;
}

void waitReadable(const std::vector<Fd>& fds, int timeoutMs) {
    fd_set r;
    FD_ZERO(&r);
    int top = 0;
    bool any = false;
    for (Fd f : fds) {
        if (f == kNoSocket) continue;
        any = true;
        FD_SET(raw(f), &r);
        top = std::max(top, static_cast<int>(raw(f)));
    }
    if (!any) {
        // Nothing to wait on (Winsock refuses an empty select): just the time.
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return;
    }
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    ::select(top + 1, &r, nullptr, nullptr, &tv);
}

std::string ipv4Text(uint32_t ip) {
    return std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." +
           std::to_string(ip & 255);
}

// ------------------------------------------------------------------ framed

void FramedConn::close() {
    closeSocket(fd_);
    fd_ = kNoSocket;
    in_.clear();
    out_.clear();
}

void FramedConn::send(const std::vector<uint8_t>& msg) {
    if (fd_ == kNoSocket || msg.empty() || msg.size() > kMaxMessage) return;
    const uint32_t n = static_cast<uint32_t>(msg.size());
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(n >> (8 * i)));
    out_.insert(out_.end(), msg.begin(), msg.end());
    flush();
}

bool FramedConn::flush() {
    while (fd_ != kNoSocket && !out_.empty()) {
        const int n = static_cast<int>(::send(raw(fd_), reinterpret_cast<const char*>(out_.data()), static_cast<int>(out_.size()), kSendFlags));
        if (n < 0) {
            if (wouldBlock()) return true;
            return false;
        }
        out_.erase(out_.begin(), out_.begin() + n);
    }
    // A peer that stops reading: never more than 4 MB queued for it.
    return fd_ != kNoSocket && out_.size() < 4u * 1024u * 1024u;
}

bool FramedConn::receive(std::deque<std::vector<uint8_t>>& out) {
    if (fd_ == kNoSocket) return false;
    uint8_t buf[16384];
    for (;;) {
        const int n = static_cast<int>(::recv(raw(fd_), reinterpret_cast<char*>(buf), sizeof buf, 0));
        if (n == 0) return false;  // closed by the peer
        if (n < 0) {
            if (wouldBlock()) break;
            return false;
        }
        in_.insert(in_.end(), buf, buf + n);
    }
    size_t at = 0;
    while (in_.size() - at >= 4) {
        const uint32_t len = static_cast<uint32_t>(in_[at]) | (static_cast<uint32_t>(in_[at + 1]) << 8) |
                             (static_cast<uint32_t>(in_[at + 2]) << 16) | (static_cast<uint32_t>(in_[at + 3]) << 24);
        if (len == 0 || len > kMaxMessage) return false;  // malformed: never trusted
        if (in_.size() - at - 4 < len) break;
        out.emplace_back(in_.begin() + static_cast<std::ptrdiff_t>(at + 4), in_.begin() + static_cast<std::ptrdiff_t>(at + 4 + len));
        at += 4 + len;
    }
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(at));
    return true;
}

}  // namespace cosmic::net
