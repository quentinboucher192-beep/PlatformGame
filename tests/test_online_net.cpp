// Cosmic Online over real sockets.
//  * The platform HTTP client (libcurl / WinHTTP) against a tiny local TCP server that
//    misbehaves on purpose: refused connection, silence (timeout), connection cut in the
//    middle of a body, non-HTTP garbage, oversized answer, cancellation.
//  * Integration with a real Cosmic Online server (local `wrangler dev` or staging) when
//    COSMIC_ONLINE_TEST_URL and COSMIC_ONLINE_TEST_ADMIN_TOKEN are set: two players play a
//    level, their runs are verified from their replays and ranked by the D1 database.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineService.h"
#include "tests/TestFramework.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void closeSock(socket_t s) { closesocket(s); }
static const socket_t kBadSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static void closeSock(socket_t s) { ::close(s); }
static const socket_t kBadSocket = -1;
#endif

using namespace cosmic;

namespace {

/// One-thread TCP server on 127.0.0.1 with a scripted behaviour per connection.
class LocalServer {
public:
    enum class Mode { Respond, Silent, CutBody, Garbage, Huge };

    explicit LocalServer(Mode mode, std::string response = {}) : mode_(mode), response_(std::move(response)) {
#if defined(_WIN32)
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        ::bind(listen_, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        ::listen(listen_, 8);
        socklen_t len = sizeof(a);
        ::getsockname(listen_, reinterpret_cast<sockaddr*>(&a), &len);
        port_ = ntohs(a.sin_port);
        thread_ = std::thread([this] { run(); });
    }
    ~LocalServer() { stop(); }

    void stop() {
        if (stopped_.exchange(true)) return;
#if !defined(_WIN32)
        ::shutdown(listen_, SHUT_RDWR);  // wakes up the blocking accept()
#endif
        closeSock(listen_);
        if (thread_.joinable()) thread_.join();
    }
    int port() const { return port_; }
    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    std::string lastRequest() {
        std::lock_guard<std::mutex> lock(m_);
        return lastRequest_;
    }

private:
    void run() {
        while (!stopped_) {
            socket_t c = ::accept(listen_, nullptr, nullptr);
            if (c == kBadSocket) return;
            std::string req;
            char buf[4096];
            size_t need = std::string::npos;
            while (req.size() < 1 << 20) {
                int n = static_cast<int>(::recv(c, buf, sizeof(buf), 0));
                if (n <= 0) break;
                req.append(buf, static_cast<size_t>(n));
                size_t end = req.find("\r\n\r\n");
                if (end != std::string::npos && need == std::string::npos) {
                    size_t cl = req.find("Content-Length: ");
                    if (cl == std::string::npos) cl = req.find("content-length: ");
                    need = end + 4 + (cl != std::string::npos && cl < end ? std::strtoul(req.c_str() + cl + 16, nullptr, 10) : 0);
                }
                if (need != std::string::npos && req.size() >= need) break;
            }
            {
                std::lock_guard<std::mutex> lock(m_);
                lastRequest_ = req;
            }
            auto sendAll = [&](const std::string& s) { ::send(c, s.data(), static_cast<int>(s.size()), 0); };
            switch (mode_) {
                case Mode::Respond: sendAll(response_); break;
                case Mode::Silent:
                    for (int i = 0; i < 300 && !stopped_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    break;
                case Mode::CutBody: sendAll("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 1000\r\n\r\n{\"ok\":true,"); break;
                case Mode::Garbage: sendAll("THIS IS NOT HTTP AT ALL\r\n\r\n"); break;
                case Mode::Huge: {
                    std::string body(3 * 1024 * 1024, 'x');
                    sendAll("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n");
                    sendAll(body);
                    break;
                }
            }
            closeSock(c);
        }
    }

    Mode mode_;
    std::string response_;
    socket_t listen_ = kBadSocket;
    int port_ = 0;
    std::atomic<bool> stopped_{false};
    std::thread thread_;
    std::mutex m_;
    std::string lastRequest_;
};

std::string httpOk(const std::string& json) {
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Test: yes\r\nContent-Length: " + std::to_string(json.size()) +
           "\r\nConnection: close\r\n\r\n" + json;
}

bool hasNetworkClient() { return std::string(makePlatformTransport()->name()) != "aucun"; }

}  // namespace

TEST_CASE("online net: the HTTP client sends requests and reads answers") {
    if (!hasNetworkClient()) return;
    LocalServer srv(LocalServer::Mode::Respond, httpOk("{\"ok\":true,\"data\":{\"hello\":\"monde\"}}"));
    auto t = makePlatformTransport();
    HttpRequest rq;
    rq.method = "POST";
    rq.url = srv.url("/api/v1/echo?x=1");
    rq.headers = {{"Content-Type", "application/json"}, {"Authorization", "Bearer abc"}};
    rq.body = "{\"a\":1}";
    HttpResponse r = t->perform(rq);
    CHECK(!r.networkFailure());
    CHECK(r.status == 200);
    CHECK(r.header("x-test") == "yes");
    ApiResult api = parseApiResponse(r);
    CHECK(api.ok && api.data["hello"] == "monde");
    std::string req = srv.lastRequest();
    CHECK(req.rfind("POST /api/v1/echo?x=1 HTTP/1.1", 0) == 0);
    CHECK(req.find("Authorization: Bearer abc") != std::string::npos);
    CHECK(req.find("{\"a\":1}") != std::string::npos);
}

TEST_CASE("online net: refused connections, timeouts, cut and garbage answers are network errors") {
    if (!hasNetworkClient()) return;
    auto t = makePlatformTransport();
    int deadPort;
    {
        LocalServer tmp(LocalServer::Mode::Silent);
        deadPort = tmp.port();
    }
    HttpRequest rq;
    rq.url = "http://127.0.0.1:" + std::to_string(deadPort) + "/api/v1/health";
    rq.timeoutMs = 2000;
    HttpResponse refused = t->perform(rq);
    CHECK(refused.networkFailure());
    CHECK(refused.status == 0);

    LocalServer silent(LocalServer::Mode::Silent);
    rq.url = silent.url("/api/v1/health");
    rq.timeoutMs = 500;
    auto t0 = std::chrono::steady_clock::now();
    HttpResponse timeout = t->perform(rq);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(timeout.networkFailure());
    if (timeout.error != NetError::Timeout) std::cout << "  délai : " << netErrorLabel(timeout.error) << " " << timeout.errorMessage << " (" << secs << " s)\n";
    CHECK(timeout.error == NetError::Timeout);
    // libcurl and Windows stop at the deadline (~0.5 s); Wine's WinHTTP only returns when the
    // silent server gives up (3 s): the bound stays below that whatever the platform.
    CHECK(secs < 3.9);

    LocalServer cut(LocalServer::Mode::CutBody);
    rq.url = cut.url("/api/v1/health");
    rq.timeoutMs = 3000;
    HttpResponse partial = t->perform(rq);
    CHECK(partial.networkFailure() || !parseApiResponse(partial).ok);

    LocalServer garbage(LocalServer::Mode::Garbage);
    rq.url = garbage.url("/api/v1/health");
    HttpResponse junk = t->perform(rq);
    CHECK(!parseApiResponse(junk).ok);

    LocalServer huge(LocalServer::Mode::Huge);
    rq.url = huge.url("/api/v1/health");
    rq.maxResponseBytes = 1024 * 1024;
    HttpResponse big = t->perform(rq);
    CHECK(big.networkFailure());
    CHECK(big.error == NetError::TooLarge);

    // Plain HTTP is refused for anything but the local machine.
    rq.url = "http://example.com/api/v1/health";
    HttpResponse insecure = t->perform(rq);
    CHECK(insecure.networkFailure());
}

TEST_CASE("online net: a running transfer can be cancelled") {
    if (!hasNetworkClient()) return;
    LocalServer silent(LocalServer::Mode::Silent);
    OnlineClient client(makePlatformTransport());
    HttpRequest rq;
    rq.url = silent.url("/api/v1/health");
    rq.timeoutMs = 2000;
    std::atomic<bool> done{false};
    NetError err = NetError::None;
    auto id = client.send(rq, [&](const HttpResponse& r) {
        err = r.error;
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto t0 = std::chrono::steady_clock::now();
    client.cancel(id);
    while (!done && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4)) {
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(done);
    CHECK(err == NetError::Cancelled);
}

// ------------------------------------------------------------- live server

namespace {

const char* envOr(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? v : nullptr;
}

LevelData liveCorridor() {
    LevelData l = LevelData::makeEmpty("online_test_corridor", "Couloir de test", "sylvanox");
    l.intro.steps.clear();
    l.referenceTime = 20.0f;
    l.metadata["purpose"] = "integration test of Cosmic Online";
    return l;
}

ReplayData playLive(const ContentDatabase& content, const LevelData& level, const std::string& hash, float waitSeconds) {
    AudioSystem audio;
    ResourceManager res;
    res.init(nullptr, &audio);
    WorldServices ws{res, audio, content, nullptr, 0.0f, 0.0f};
    GameWorld w(ws, WorldMode::Play);
    w.load(level);
    ReplayRecorder rec;
    rec.begin(level.id, hash, makeUuidV4());
    w.eventObserver = [&](const std::string& s, const std::string& t) { rec.onWorldEvent(s, t); };
    float t = 0.0f;
    for (int i = 0; i < 120 * 60 && w.state() != WorldState::Completed; ++i) {
        PlayerInput in;
        if (t > waitSeconds) in.move.x = 1.0f;
        w.fixedUpdate(in, 1.0f / 120.0f);
        rec.tick(w);
        if (w.timer().running()) t += 1.0f / 120.0f;
    }
    CHECK(w.state() == WorldState::Completed);
    rec.finish(w, w.timer().elapsed());
    return rec.take();
}

template <typename Pred>
bool pumpLive(OnlineService& svc, Pred done, int timeoutMs = 20000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(0.016f);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

ApiResult callSync(OnlineService& svc, const std::function<void(ApiCallback)>& start) {
    ApiResult out;
    bool done = false;
    start([&](const ApiResult& r) {
        out = r;
        done = true;
    });
    pumpLive(svc, [&] { return done; });
    return out;
}

}  // namespace

TEST_CASE("online live: two players compare their runs through the real server") {
    const char* url = envOr("COSMIC_ONLINE_TEST_URL");
    const char* admin = envOr("COSMIC_ONLINE_TEST_ADMIN_TOKEN");
    if (!url || !admin || !hasNetworkClient()) {
        std::cout << "  (ignoré : définir COSMIC_ONLINE_TEST_URL et COSMIC_ONLINE_TEST_ADMIN_TOKEN pour tester un vrai serveur)\n";
        return;
    }
    ContentDatabase content;
    content.reload();
    LevelData level = liveCorridor();
    auto fp = fingerprintLevelJson(level.toJson());
    CHECK(fp.has_value());
    // Register the test level revision (operator route, admin token from the environment).
    {
        Json body = {{"levels", Json::array({onlineManifestEntry(content, level, *fp)})}, {"game_version", COSMIC_VERSION_STRING}};
        HttpRequest rq;
        rq.method = "POST";
        rq.url = std::string(url) + "/api/v1/admin/levels";
        rq.headers = {{"Content-Type", "application/json"}, {"Authorization", std::string("Bearer ") + admin}};
        rq.body = body.dump();
        ApiResult r = parseApiResponse(makePlatformTransport()->perform(rq));
        if (!r.ok) std::cout << "  enregistrement du niveau : " << r.code << " " << r.message << "\n";
        CHECK(r.ok);
    }
    std::string suffix = std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    struct Run {
        std::string name;
        std::string country;
        float wait;
        int64_t timeMs = 0;
    };
    std::vector<Run> runs = {{"Nova" + suffix, "FR", 0.0f}, {"Orion" + suffix, "JP", 2.0f}};
    fs::path base = fs::temp_directory_path() / ("cosmic_live_" + suffix);
    for (auto& run : runs) {
        OnlineService svc;
        OnlineSettings s;
        svc.init(s, url, nullptr, base / run.name);
        Json form = {{"username", run.name},           {"password", "integration test password"}, {"country_code", run.country},
                     {"location_visibility", "globe"}, {"accept_tos", true},                      {"tos_version", svc.config().tosVersion}};
        ApiResult reg = callSync(svc, [&](ApiCallback cb) { svc.registerAccount(form, false, cb); });
        if (!reg.ok) std::cout << "  inscription : " << reg.code << " " << reg.message << "\n";
        CHECK(reg.ok);
        ReplayData rep = playLive(content, level, fp->hash, run.wait);
        run.timeMs = rep.durationMs;
        RunCandidate c;
        c.levelId = level.id;
        c.levelVersion = fp->version;
        c.levelHash = fp->hash;
        c.timeMs = rep.durationMs;
        c.hasReplay = true;
        OnlineAttempt* a = svc.recordRun(c, rep.attemptId, &rep);
        CHECK(a != nullptr);
        CHECK(pumpLive(svc, [&] { return !svc.syncing() && svc.attempts().find(rep.attemptId)->replayUploaded; }));
        const OnlineAttempt* done = svc.attempts().find(rep.attemptId);
        for (const auto& sub : done->submissions) {
            if (sub.status != RecordStatus::Verified) std::cout << "  " << categoryId(sub.category) << " : " << recordStatusId(sub.status) << " " << sub.note << "\n";
            CHECK(sub.status == RecordStatus::Verified);
        }
        // Submitting the same attempt again is idempotent.
        ApiResult dup = callSync(svc, [&](ApiCallback cb) { svc.send("POST", "/api/v1/records/submit", done->submitBody(RunCategory::AnyPercent), cb); });
        CHECK(dup.ok && dup.data["duplicate"] == true);
        // Another revision of the level is refused, never mixed.
        Json other = done->submitBody(RunCategory::AnyPercent);
        other["level_hash"] = std::string(64, '0');
        other["attempt_id"] = makeUuidV4();
        ApiResult mismatch = callSync(svc, [&](ApiCallback cb) { svc.send("POST", "/api/v1/records/submit", other, cb); });
        CHECK(!mismatch.ok && mismatch.code == "LEVEL_VERSION_MISMATCH");
        svc.shutdown();
    }
    CHECK(runs[0].timeMs < runs[1].timeMs);
    // The level board (from D1) ranks both players; the globe shows them at their countries.
    OnlineService viewer;
    viewer.init(OnlineSettings{}, url, nullptr, base / "viewer");
    ApiResult board = callSync(viewer, [&](ApiCallback cb) { viewer.get("/api/v1/leaderboards/level/online_test_corridor?limit=100", cb, false); });
    CHECK(board.ok);
    int rankNova = 0, rankOrion = 0;
    for (const auto& e : board.data["entries"]) {
        std::string n = e["player"]["username"].get<std::string>();
        if (n == runs[0].name) {
            rankNova = e["rank"].get<int>();
            CHECK(e["time_ms"].get<int64_t>() == runs[0].timeMs);
        }
        if (n == runs[1].name) rankOrion = e["rank"].get<int>();
    }
    CHECK(rankNova > 0 && rankOrion > rankNova);
    ApiResult globe = callSync(viewer, [&](ApiCallback cb) { viewer.get("/api/v1/globe/records?level_id=online_test_corridor&limit=1000", cb, false); });
    CHECK(globe.ok);
    int found = 0;
    for (const auto& m : globe.data["records"]) {
        std::string n = m["player_name"].get<std::string>();
        if (n == runs[0].name) {
            ++found;
            CHECK(m["country_code"] == "FR");
            CHECK_NEAR(m["latitude"].get<double>(), 46.5, 1.0);
        }
        if (n == runs[1].name) {
            ++found;
            CHECK(m["country_code"] == "JP");
        }
    }
    CHECK(found == 2);
    viewer.shutdown();
    std::error_code ec;
    fs::remove_all(base, ec);
}

// Local development helper (not a check): fills a development server with real runs of
// the corridor level by several test accounts, so that the ranking screen and the globe can
// be tried with data that went through the whole pipeline (replays analysed by the server).
// Never point it at production.
TEST_CASE("online live: populate a development server with real runs") {
    const char* url = envOr("COSMIC_ONLINE_TEST_URL");
    const char* admin = envOr("COSMIC_ONLINE_TEST_ADMIN_TOKEN");
    const char* populate = envOr("COSMIC_ONLINE_TEST_POPULATE");
    if (!url || !admin || !populate || !hasNetworkClient()) return;
    ContentDatabase content;
    content.reload();
    LevelData level = liveCorridor();
    auto fp = fingerprintLevelJson(level.toJson());
    {
        Json body = {{"levels", Json::array({onlineManifestEntry(content, level, *fp)})}, {"game_version", COSMIC_VERSION_STRING}};
        HttpRequest rq;
        rq.method = "POST";
        rq.url = std::string(url) + "/api/v1/admin/levels";
        rq.headers = {{"Content-Type", "application/json"}, {"Authorization", std::string("Bearer ") + admin}};
        rq.body = body.dump();
        CHECK(parseApiResponse(makePlatformTransport()->perform(rq)).ok);
    }
    struct P {
        const char* name;
        const char* country;
        const char* visibility;
        float wait;
    };
    const P players[] = {
        {"Astra", "FR", "globe", 1.2f},  {"Kepler", "DE", "globe", 0.4f},  {"Lumen", "FR", "globe", 2.6f},   {"Nadia", "MA", "globe", 3.1f},
        {"Hoshi", "JP", "globe", 0.7f},  {"Ravi", "IN", "globe", 4.0f},    {"Lucia", "ES", "globe", 1.9f},   {"Tupac", "PE", "globe", 5.2f},
        {"Mia", "AU", "globe", 2.2f},    {"Jabu", "ZA", "globe", 6.1f},    {"Ana", "BR", "globe", 3.5f},     {"Leo", "US", "globe", 0.9f},
        {"Sofia", "IT", "country", 2.9f}, {"Erik", "SE", "globe", 4.6f},   {"Chen", "CN", "globe", 1.5f},    {"Olu", "NG", "globe", 7.0f},
        {"Maya", "CA", "globe", 3.8f},   {"Kai", "NZ", "globe", 5.6f},     {"Zara", "GB", "globe", 2.4f},    {"Noor", "EG", "hidden", 1.1f},
        {"Ines", "FR", "globe", 6.6f},   {"Diego", "MX", "globe", 4.3f},   {"Yuki", "JP", "globe", 3.3f},    {"Aiko", "KR", "globe", 5.0f},
    };
    const char* sessionDir = envOr("COSMIC_ONLINE_TEST_SESSION_DIR");
    // The demo user folder also gets the level, to watch the replays in the game.
    if (sessionDir) FileUtil::saveJson(fs::path(sessionDir) / "levels" / (level.id + ".json"), level.toJson());
    for (const P& pl : players) {
        OnlineService svc;
        bool keep = sessionDir && std::string(pl.name) == "Astra";
        fs::path dir = keep ? fs::path(sessionDir) : fs::temp_directory_path() / (std::string("cosmic_populate_") + pl.name);
        svc.init(OnlineSettings{}, url, nullptr, dir);
        Json form = {{"username", pl.name}, {"password", "development only password"}, {"country_code", pl.country},
                     {"location_visibility", pl.visibility}, {"accept_tos", true}, {"tos_version", svc.config().tosVersion}};
        ApiResult reg = callSync(svc, [&](ApiCallback cb) { svc.registerAccount(form, keep, cb); });
        if (!reg.ok) {
            ApiResult in = callSync(svc, [&](ApiCallback cb) { svc.login(pl.name, "development only password", keep, cb); });
            CHECK(in.ok);
        }
        char avatar[16];
        std::snprintf(avatar, sizeof(avatar), "avatar_%02d", static_cast<int>(&pl - players) % 16 + 1);
        callSync(svc, [&](ApiCallback cb) { svc.updateProfile({{"avatar", avatar}}, cb); });
        if (std::getenv("COSMIC_ONLINE_TEST_PROFILES_ONLY")) {
            svc.shutdown();
            continue;
        }
        ReplayData rep = playLive(content, level, fp->hash, pl.wait);
        RunCandidate c;
        c.levelId = level.id;
        c.levelVersion = fp->version;
        c.levelHash = fp->hash;
        c.timeMs = rep.durationMs;
        c.hasReplay = true;
        if (svc.recordRun(c, rep.attemptId, &rep))
            pumpLive(svc, [&] { return !svc.syncing() && svc.attempts().find(rep.attemptId)->replayUploaded; });
        svc.shutdown();
        if (!keep) {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    }
}
