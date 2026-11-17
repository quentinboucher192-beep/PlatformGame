#include "game/coop/CoopTransport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

#include "game/coop/NetSocket.h"
#include "game/online/HttpTransport.h"

#if defined(COSMIC_HTTP_WINHTTP)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <mutex>
#elif defined(COSMIC_HTTP_CURL)
#include <curl/curl.h>
#if !defined(_WIN32)
#include <sys/select.h>
#endif
#endif

namespace cosmic {

// ------------------------------------------------------------------ websocket frames

namespace ws {

std::vector<uint8_t> clientFrame(const std::vector<uint8_t>& payload, uint8_t opcode, uint32_t mask) {
    std::vector<uint8_t> f;
    f.reserve(payload.size() + 14);
    f.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0F)));
    const size_t n = payload.size();
    if (n < 126) {
        f.push_back(static_cast<uint8_t>(0x80 | n));
    } else if (n <= 0xFFFF) {
        f.push_back(0x80 | 126);
        f.push_back(static_cast<uint8_t>(n >> 8));
        f.push_back(static_cast<uint8_t>(n));
    } else {
        f.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) f.push_back(static_cast<uint8_t>(static_cast<uint64_t>(n) >> (8 * i)));
    }
    const uint8_t m[4] = {static_cast<uint8_t>(mask >> 24), static_cast<uint8_t>(mask >> 16), static_cast<uint8_t>(mask >> 8), static_cast<uint8_t>(mask)};
    f.insert(f.end(), m, m + 4);
    for (size_t i = 0; i < n; ++i) f.push_back(static_cast<uint8_t>(payload[i] ^ m[i & 3]));
    return f;
}

bool readFrames(std::vector<uint8_t>& in, std::vector<uint8_t>& partial, std::deque<std::vector<uint8_t>>& out,
                std::vector<std::vector<uint8_t>>& pongs) {
    size_t at = 0;
    for (;;) {
        if (in.size() - at < 2) break;
        const uint8_t b0 = in[at], b1 = in[at + 1];
        const bool fin = (b0 & 0x80) != 0;
        const uint8_t op = b0 & 0x0F;
        if (b1 & 0x80) return false;  // a server never masks
        uint64_t len = b1 & 0x7F;
        size_t head = 2;
        if (len == 126) {
            if (in.size() - at < 4) break;
            len = (static_cast<uint64_t>(in[at + 2]) << 8) | in[at + 3];
            head = 4;
        } else if (len == 127) {
            if (in.size() - at < 10) break;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | in[at + 2 + static_cast<size_t>(i)];
            head = 10;
        }
        if (len > net::kMaxMessage) return false;
        if (in.size() - at - head < len) break;
        const uint8_t* p = in.data() + at + head;
        if (op == 0x8) return false;  // closed
        if (op == 0x9) {
            pongs.emplace_back(p, p + len);
        } else if (op == 0x1 || op == 0x2 || op == 0x0) {
            partial.insert(partial.end(), p, p + len);
            if (partial.size() > net::kMaxMessage) return false;
            if (fin) {
                out.push_back(std::move(partial));
                partial.clear();
            }
        }
        at += head + static_cast<size_t>(len);
    }
    in.erase(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(at));
    return true;
}

}  // namespace ws

namespace {

// ------------------------------------------------------------------ local network

class LanTransport : public CoopTransport {
public:
    LanTransport(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {}
    bool connect(std::string& error) override {
        net::Fd fd = net::tcpConnect(host_, port_, 4000);
        if (fd == net::kNoSocket) {
            error = "Impossible de joindre " + host_ + " sur le réseau local.";
            return false;
        }
        conn_ = std::make_unique<net::FramedConn>(fd);
        return true;
    }
    bool send(const std::vector<uint8_t>& msg) override {
        if (!conn_) return false;
        conn_->send(msg);
        return conn_->open();
    }
    bool receive(std::deque<std::vector<uint8_t>>& out, int waitMs) override {
        if (!conn_ || !conn_->open()) return false;
        net::waitReadable({conn_->fd()}, waitMs);
        return conn_->receive(out) && conn_->flush();
    }
    void close() override {
        if (conn_) conn_->close();
    }

private:
    std::string host_;
    uint16_t port_;
    std::unique_ptr<net::FramedConn> conn_;
};

uint32_t randomMask() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return rng();
}

std::string base64(const uint8_t* p, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = static_cast<uint32_t>(p[i]) << 16;
        if (i + 1 < n) v |= static_cast<uint32_t>(p[i + 1]) << 8;
        if (i + 2 < n) v |= p[i + 2];
        o.push_back(t[(v >> 18) & 63]);
        o.push_back(t[(v >> 12) & 63]);
        o.push_back(i + 1 < n ? t[(v >> 6) & 63] : '=');
        o.push_back(i + 2 < n ? t[v & 63] : '=');
    }
    return o;
}

#if defined(COSMIC_HTTP_WINHTTP)

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// WinHTTP's WebSocket (Windows 8 and later): a thread reads (the receive call blocks), the
// network thread writes.
class WsTransport : public CoopTransport {
public:
    WsTransport(std::string url, std::string token) : url_(std::move(url)), token_(std::move(token)) {}
    ~WsTransport() override { close(); }
    bool connect(std::string& error) override {
        ParsedUrl u;
        if (!parseHttpUrl(url_, u) || (!u.https && !isLoopbackHost(u.host))) {
            error = "Adresse du serveur invalide.";
            return false;
        }
        session_ = WinHttpOpen(L"CosmicPlatformer", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) session_ = WinHttpOpen(L"CosmicPlatformer", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) return fail(error);
        WinHttpSetTimeouts(session_, 8000, 8000, 8000, 8000);
        connect_ = WinHttpConnect(session_, widen(u.host).c_str(), static_cast<INTERNET_PORT>(u.port), 0);
        if (!connect_) return fail(error);
        HINTERNET req = WinHttpOpenRequest(connect_, L"GET", widen(u.pathAndQuery).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) return fail(error);
        WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
        const std::wstring auth = L"Authorization: Bearer " + widen(token_);
        WinHttpAddRequestHeaders(req, auth.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
        DWORD status = 0, size = sizeof status;
        const bool sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                          WinHttpReceiveResponse(req, nullptr) &&
                          WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                              WINHTTP_NO_HEADER_INDEX);
        if (!sent || status != 101) {
            WinHttpCloseHandle(req);
            error = status == 404 ? "Ce salon n'existe plus ou ne vous est pas ouvert." : "Le serveur refuse la connexion au salon.";
            return false;
        }
        socket_ = WinHttpWebSocketCompleteUpgrade(req, 0);
        WinHttpCloseHandle(req);
        if (!socket_) return fail(error);
        open_ = true;
        reader_ = std::thread([this] { readLoop(); });
        return true;
    }
    bool send(const std::vector<uint8_t>& msg) override {
        if (!open_) return false;
        std::lock_guard<std::mutex> lock(sendMutex_);
        const DWORD r = WinHttpWebSocketSend(socket_, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, const_cast<uint8_t*>(msg.data()),
                                             static_cast<DWORD>(msg.size()));
        if (r != NO_ERROR) open_ = false;
        return open_;
    }
    bool receive(std::deque<std::vector<uint8_t>>& out, int waitMs) override {
        std::unique_lock<std::mutex> lock(inMutex_);
        if (in_.empty() && open_) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(waitMs, 2)));
            lock.lock();
        }
        while (!in_.empty()) {
            out.push_back(std::move(in_.front()));
            in_.pop_front();
        }
        return open_;
    }
    void close() override {
        open_ = false;
        if (socket_) WinHttpWebSocketClose(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        if (reader_.joinable()) reader_.join();
        if (socket_) WinHttpCloseHandle(socket_);
        if (connect_) WinHttpCloseHandle(connect_);
        if (session_) WinHttpCloseHandle(session_);
        socket_ = connect_ = session_ = nullptr;
    }

private:
    bool fail(std::string& error) {
        error = "Connexion au salon impossible (erreur " + std::to_string(GetLastError()) + ").";
        return false;
    }
    void readLoop() {
        std::vector<uint8_t> msg;
        std::vector<uint8_t> buf(16384);
        while (open_) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            if (WinHttpWebSocketReceive(socket_, buf.data(), static_cast<DWORD>(buf.size()), &read, &type) != NO_ERROR) break;
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            msg.insert(msg.end(), buf.begin(), buf.begin() + read);
            if (msg.size() > net::kMaxMessage) break;
            if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                std::lock_guard<std::mutex> lock(inMutex_);
                in_.push_back(std::move(msg));
                msg.clear();
            }
        }
        open_ = false;
    }

    std::string url_, token_;
    HINTERNET session_ = nullptr, connect_ = nullptr, socket_ = nullptr;
    std::atomic<bool> open_{false};
    std::thread reader_;
    std::mutex inMutex_, sendMutex_;
    std::deque<std::vector<uint8_t>> in_;
};

#elif defined(COSMIC_HTTP_CURL)

// libcurl opens the TLS connection (CONNECT_ONLY); the upgrade and the frames (RFC 6455) are
// written here, so that a libcurl built without its WebSocket support is enough.
class WsTransport : public CoopTransport {
public:
    WsTransport(std::string url, std::string token) : url_(std::move(url)), token_(std::move(token)) {}
    ~WsTransport() override { close(); }
    bool connect(std::string& error) override {
        ParsedUrl u;
        if (!parseHttpUrl(url_, u) || (!u.https && !isLoopbackHost(u.host))) {
            error = "Adresse du serveur invalide.";
            return false;
        }
        curl_ = curl_easy_init();
        if (!curl_) return fail(error, "libcurl indisponible");
        curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 1L);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
        if (curl_easy_perform(curl_) != CURLE_OK) return fail(error, "serveur injoignable");
        // The upgrade request.
        uint8_t keyBytes[16];
        for (auto& b : keyBytes) b = static_cast<uint8_t>(randomMask());
        std::string req = "GET " + u.pathAndQuery + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) +
                          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
                          base64(keyBytes, sizeof keyBytes) + "\r\nAuthorization: Bearer " + token_ + "\r\nUser-Agent: CosmicPlatformer\r\n\r\n";
        if (!writeAll(reinterpret_cast<const uint8_t*>(req.data()), req.size())) return fail(error, "envoi impossible");
        // Its answer: « 101 », then the frames.
        std::string head;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (head.find("\r\n\r\n") == std::string::npos) {
            uint8_t b[1024];
            size_t n = 0;
            CURLcode rc = curl_easy_recv(curl_, b, sizeof b, &n);
            if (rc == CURLE_AGAIN) {
                if (std::chrono::steady_clock::now() > deadline) return fail(error, "pas de réponse");
                waitSocket(20);
                continue;
            }
            if (rc != CURLE_OK || n == 0) return fail(error, "connexion fermée");
            head.append(reinterpret_cast<const char*>(b), n);
            if (head.size() > 16384) return fail(error, "réponse invalide");
        }
        const size_t end = head.find("\r\n\r\n") + 4;
        if (head.compare(0, 12, "HTTP/1.1 101") != 0) {
            error = head.find(" 404 ") != std::string::npos ? "Ce salon n'existe plus ou ne vous est pas ouvert."
                                                             : "Le serveur refuse la connexion au salon.";
            return false;
        }
        in_.assign(head.begin() + static_cast<std::ptrdiff_t>(end), head.end());
        open_ = true;
        return true;
    }
    bool send(const std::vector<uint8_t>& msg) override {
        if (!open_) return false;
        const auto frame = ws::clientFrame(msg, 0x2, randomMask());
        if (!writeAll(frame.data(), frame.size())) open_ = false;
        return open_;
    }
    bool receive(std::deque<std::vector<uint8_t>>& out, int waitMs) override {
        if (!open_) return false;
        waitSocket(waitMs);
        for (;;) {
            uint8_t b[16384];
            size_t n = 0;
            CURLcode rc = curl_easy_recv(curl_, b, sizeof b, &n);
            if (rc == CURLE_AGAIN) break;
            if (rc != CURLE_OK || n == 0) {
                open_ = false;
                break;
            }
            in_.insert(in_.end(), b, b + n);
        }
        std::vector<std::vector<uint8_t>> pongs;
        if (!ws::readFrames(in_, partial_, out, pongs)) open_ = false;
        for (auto& p : pongs) {
            const auto frame = ws::clientFrame(p, 0xA, randomMask());
            writeAll(frame.data(), frame.size());
        }
        return open_;
    }
    void close() override {
        if (curl_ && open_) {
            const auto frame = ws::clientFrame({}, 0x8, randomMask());
            writeAll(frame.data(), frame.size());
        }
        open_ = false;
        if (curl_) curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }

private:
    bool fail(std::string& error, const char* why) {
        error = std::string("Connexion au salon impossible : ") + why + ".";
        return false;
    }
    void waitSocket(int ms) {
        curl_socket_t s = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(curl_, CURLINFO_ACTIVESOCKET, &s) != CURLE_OK || s == CURL_SOCKET_BAD) return;
        fd_set r;
        FD_ZERO(&r);
        FD_SET(s, &r);
        timeval tv{ms / 1000, (ms % 1000) * 1000};
        select(static_cast<int>(s) + 1, &r, nullptr, nullptr, &tv);
    }
    bool writeAll(const uint8_t* p, size_t n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (n > 0) {
            size_t sent = 0;
            CURLcode rc = curl_easy_send(curl_, p, n, &sent);
            if (rc == CURLE_AGAIN) {
                if (std::chrono::steady_clock::now() > deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (rc != CURLE_OK) return false;
            p += sent;
            n -= sent;
        }
        return true;
    }

    std::string url_, token_;
    CURL* curl_ = nullptr;
    bool open_ = false;
    std::vector<uint8_t> in_, partial_;
};

#else

class WsTransport : public CoopTransport {
public:
    WsTransport(std::string, std::string) {}
    bool connect(std::string& error) override {
        error = "Cette version du jeu n'a pas de connexion réseau.";
        return false;
    }
    bool send(const std::vector<uint8_t>&) override { return false; }
    bool receive(std::deque<std::vector<uint8_t>>&, int) override { return false; }
    void close() override {}
};

#endif

}  // namespace

std::unique_ptr<CoopTransport> makeLanTransport(const std::string& host, uint16_t port) { return std::make_unique<LanTransport>(host, port); }

std::unique_ptr<CoopTransport> makeWebSocketTransport(const std::string& url, const std::string& token) {
    return std::make_unique<WsTransport>(url, token);
}

}  // namespace cosmic
