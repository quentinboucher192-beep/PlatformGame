// Cosmic Online client: JSON envelopes, cache, session storage, replay format, local
// performance queue, and the service logic (token refresh, offline mode, cache fallback,
// synchronisation after an outage) against a scripted fake server. The fake server runs
// on the real network worker thread, so threading is exercised too. Positive answers are
// never assumed: most scenarios script failures.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <map>
#include <set>
#include <mutex>
#include <thread>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineService.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using namespace cosmic;

namespace {

fs::path tempDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("cosmic_online_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

HttpResponse jsonResponse(int status, const Json& body) {
    HttpResponse r;
    r.status = status;
    r.body = body.dump();
    r.headers["content-type"] = "application/json";
    return r;
}

HttpResponse okData(const Json& data, int status = 200) { return jsonResponse(status, {{"ok", true}, {"data", data}, {"request_id", "t"}}); }

HttpResponse apiError(int status, const std::string& code) {
    return jsonResponse(status, {{"ok", false}, {"error", {{"code", code}, {"message", code}}}, {"request_id", "t"}});
}

/// Scripted Cosmic Online server answering on the network thread.
struct FakeServer {
    std::mutex m;
    bool down = false;
    bool malformed = false;
    int delayMs = 0;
    int counter = 0;
    std::string access = "access-0000000000000000000000";
    std::string refresh = "refresh-000000000000000000000";
    bool refreshRevoked = false;
    int refreshCalls = 0, submitCalls = 0, replayCalls = 0, loginCalls = 0;
    std::vector<std::string> log;
    std::map<std::string, Json> records;
    Json friends = {{"friends", Json::array()}, {"incoming", Json::array()}, {"outgoing", Json::array()}};
    Json unread = {{"total", 0}, {"latest", Json::array()}};
    Json notifications = Json::array();  // unread notifications, newest first
    std::vector<Json> playtimeReports;
    int playtimeStatus = 200;
    std::set<std::string> takenNames = {"orion"};
    int friendsCalls = 0;
    bool levelPending = false;  // the server does not know the game's level revision yet

    static Json friendEntry(const std::string& id, const std::string& name) {
        return {{"player", {{"id", id}, {"username", name}}}, {"since", "2026-11-13T10:00:00.000Z"}};
    }

    Json session() {
        ++counter;
        access = "access-" + std::to_string(counter) + "-aaaaaaaaaaaaaaaaaaaa";
        refresh = "refresh-" + std::to_string(counter) + "-bbbbbbbbbbbbbbbbbbbb";
        return {{"access_token", access}, {"access_expires_at", "2099-01-01T00:00:00.000Z"}, {"refresh_token", refresh},
                {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}};
    }
    static Json player() { return {{"id", "11111111-1111-4111-8111-111111111111"}, {"username", "Nova"}, {"country_code", "FR"}}; }

    bool authorized(const HttpRequest& rq) {
        for (const auto& [k, v] : rq.headers)
            if (k == "Authorization") return v == "Bearer " + access;
        return false;
    }

    HttpResponse handle(const HttpRequest& rq) {
        int delay;
        {
            std::lock_guard<std::mutex> lock(m);
            delay = delayMs;
        }
        if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        std::lock_guard<std::mutex> lock(m);
        std::string path = rq.url.substr(rq.url.find("/api/"));
        log.push_back(rq.method + " " + path);
        if (down) {
            HttpResponse r;
            r.error = NetError::Connect;
            r.errorMessage = "refused";
            return r;
        }
        if (malformed) {
            HttpResponse r;
            r.status = 200;
            r.body = "<html>proxy login</html>";
            return r;
        }
        if (path == "/api/v1/health") return okData({{"status", "ok"}});
        if (path.rfind("/api/v1/auth/username-available?username=", 0) == 0) {
            std::string name = path.substr(path.find('=') + 1);
            std::string lower;
            for (char c : name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            bool taken = takenNames.count(lower) > 0;
            return okData({{"username", name}, {"available", !taken}, {"reason", taken ? Json("taken") : Json()},
                           {"message", taken ? "Ce pseudonyme est déjà utilisé." : "Pseudonyme disponible."}});
        }
        if (path == "/api/v1/auth/login") {
            ++loginCalls;
            Json b = Json::parse(rq.body);
            if (b["password"] != "good password") return apiError(401, "INVALID_CREDENTIALS");
            return okData({{"player", player()}, {"session", session()}});
        }
        if (path == "/api/v1/auth/refresh") {
            ++refreshCalls;
            Json b = Json::parse(rq.body);
            if (refreshRevoked || b["refresh_token"] != refresh) return apiError(401, "SESSION_REVOKED");
            return okData({{"session", session()}});
        }
        if (!authorized(rq)) return apiError(401, "TOKEN_EXPIRED");
        if (path == "/api/v1/auth/me") return okData({{"player", player()}});
        if (path == "/api/v1/social/unread") return okData(unread);
        if (path.rfind("/api/v1/notifications?filter=unread", 0) == 0)
            return okData({{"notifications", notifications}, {"unread", static_cast<int>(notifications.size())}});
        if (path == "/api/v1/players/me/playtime") {
            if (playtimeStatus != 200) return apiError(playtimeStatus, "RATE_LIMITED");
            Json b = Json::parse(rq.body);
            playtimeReports.push_back(b);
            int total = 0;
            for (const auto& x : b["sessions"]) total += x["seconds"].get<int>();
            return okData({{"accepted_seconds", total}});
        }
        if (path == "/api/v1/friends") {
            ++friendsCalls;
            return okData(friends);
        }
        if (path == "/api/v1/records/submit") {
            ++submitCalls;
            if (levelPending) return apiError(409, "LEVEL_VERSION_PENDING");
            Json b = Json::parse(rq.body);
            std::string id = "rec-" + b["attempt_id"].get<std::string>().substr(0, 8) + "-" + b["category"].get<std::string>();
            bool dup = records.count(id) > 0;
            if (!dup) records[id] = {{"id", id}, {"category", b["category"]}, {"verification_status", "PENDING"}, {"attempt_id", b["attempt_id"]}};
            return okData({{"duplicate", dup}, {"record", records[id]}, {"ranking", nullptr}}, dup ? 200 : 201);
        }
        if (path.find("/replay") != std::string::npos && rq.method == "PUT") {
            ++replayCalls;
            std::vector<uint8_t> bytes(rq.body.begin(), rq.body.end());
            auto rep = ReplayData::decode(bytes);
            if (!rep) return apiError(422, "REPLAY_MALFORMED");
            Json items = Json::array();
            for (auto& [id, r] : records) {
                if (r["attempt_id"] != rep->attemptId) continue;
                r["verification_status"] = "VERIFIED";
                items.push_back({{"record", r}, {"ranking", {{"previous_rank", nullptr}, {"rank", 1}, {"total", 1}, {"personal_best_improved", true}}}});
            }
            return okData({{"verification_status", "VERIFIED"}, {"records", items}}, 201);
        }
        if (path.rfind("/api/v1/records/", 0) == 0 && rq.method == "GET") {
            std::string id = path.substr(16);
            if (!records.count(id)) return apiError(404, "RECORD_NOT_FOUND");
            return okData({{"record", records[id]}, {"rank", 1}});
        }
        if (path.rfind("/api/v1/leaderboards/level/", 0) == 0)
            return okData({{"entries", Json::array({{{"rank", 1}, {"time_ms", 61000}}})}, {"me", nullptr}}, 200);
        return apiError(404, "NOT_FOUND");
    }
};

class FakeTransport final : public IHttpTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeServer> s) : s_(std::move(s)) {}
    HttpResponse perform(const HttpRequest& rq) override { return s_->handle(rq); }
    const char* name() const override { return "fake"; }

private:
    std::shared_ptr<FakeServer> s_;
};

template <typename Pred>
bool pump(OnlineService& svc, Pred done, int timeoutMs = 3000, float dt = 0.016f) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(dt);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    svc.update(dt);
    return done();
}

struct Harness {
    std::shared_ptr<FakeServer> server = std::make_shared<FakeServer>();
    fs::path dir;
    OnlineService svc;
    explicit Harness(const std::string& name, OnlineSettings s = {}) : dir(tempDir(name)) {
        svc.init(s, "http://127.0.0.1:9", std::make_unique<FakeTransport>(server), dir);
    }
    ~Harness() {
        svc.shutdown();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    bool login() {
        bool done = false, ok = false;
        svc.login("Nova", "good password", true, [&](const ApiResult& r) {
            done = true;
            ok = r.ok;
        });
        pump(svc, [&] { return done; });
        return ok;
    }
};

/// Corridor level: hold right from the start to the exit (about 14 s).
LevelData corridor() {
    LevelData l = LevelData::makeEmpty("online_corridor", "Couloir", "sylvanox");
    l.intro.steps.clear();
    l.referenceTime = 20.0f;
    return l;
}

/// Plays the corridor with the replay recorder like GameScene does.
ReplayData playCorridor(const ContentDatabase& content, float waitSeconds, float* elapsedOut) {
    AudioSystem audio;
    ResourceManager res;
    res.init(nullptr, &audio);
    WorldServices ws{res, audio, content, nullptr, 0.0f, 0.0f};
    GameWorld w(ws, WorldMode::Play);
    LevelData level = corridor();
    w.load(level);
    auto fp = fingerprintLevelJson(level.toJson());
    ReplayRecorder rec;
    rec.begin(level.id, fp->hash, makeUuidV4(), levelSeed(level.id));
    w.eventObserver = [&](const std::string& s, const std::string& t) { rec.onWorldEvent(s, t); };
    const float dt = 1.0f / 120.0f;
    float t = 0.0f;
    for (int i = 0; i < 120 * 40 && w.state() != WorldState::Completed; ++i) {
        PlayerInput in;
        if (t > waitSeconds) in.move.x = 1.0f;
        w.fixedUpdate(in, dt);
        rec.tick(w);
        if (w.timer().running()) t += dt;
    }
    CHECK(w.state() == WorldState::Completed);
    rec.finish(w, w.timer().elapsed());
    if (elapsedOut) *elapsedOut = w.timer().elapsed();
    return rec.take();
}

RunCandidate candidateFor(const ReplayData& rep, int64_t timeMs) {
    RunCandidate c;
    c.levelId = rep.levelId;
    c.levelHash = rep.levelHash;
    c.timeMs = timeMs;
    c.hasReplay = true;
    return c;
}

}  // namespace

TEST_CASE("online: API envelopes are parsed strictly") {
    ApiResult ok = parseApiResponse(okData({{"x", 1}}));
    CHECK(ok.ok);
    CHECK(ok.data["x"] == 1);
    ApiResult err = parseApiResponse(apiError(409, "ATTEMPT_CONFLICT"));
    CHECK(!err.ok);
    CHECK(err.status == 409);
    CHECK(err.code == "ATTEMPT_CONFLICT");
    // Anything that is not the API envelope is never a success.
    for (const char* body : {"<html>captive portal</html>", "", "{\"ok\":\"yes\"}", "{\"ok\":true}", "[1,2]", "{\"ok\":true,\"data\":1"}) {
        HttpResponse r;
        r.status = 200;
        r.body = body;
        ApiResult p = parseApiResponse(r);
        CHECK(!p.ok);
        CHECK(p.code == "MALFORMED_RESPONSE");
    }
    HttpResponse wrongStatus = okData({{"x", 1}});
    wrongStatus.status = 500;
    CHECK(!parseApiResponse(wrongStatus).ok);
    HttpResponse net;
    net.error = NetError::Timeout;
    ApiResult n = parseApiResponse(net);
    CHECK(!n.ok);
    CHECK(n.networkFailure());
    CHECK(n.code == "NETWORK");
    CHECK(formatRunTime(61234) == "1:01.234");
    CHECK(formatRunTime(3723004) == "1:02:03.004");
    CHECK(online::encodeQueryValue("a b&c=é") == "a%20b%26c%3D%C3%A9");
}

TEST_CASE("online: server addresses and headers are validated") {
    CHECK(OnlineConfig::sanitizeUrl("https://cosmic.example.com/") == "https://cosmic.example.com");
    CHECK(OnlineConfig::sanitizeUrl("http://127.0.0.1:8787") == "http://127.0.0.1:8787");
    CHECK(OnlineConfig::sanitizeUrl("http://localhost:8787") == "http://localhost:8787");
    // Plain HTTP to another machine, credentials or paths in the URL are refused.
    CHECK(OnlineConfig::sanitizeUrl("http://cosmic.example.com").empty());
    CHECK(OnlineConfig::sanitizeUrl("https://user:pass@cosmic.example.com").empty());
    CHECK(OnlineConfig::sanitizeUrl("https://cosmic.example.com/api").empty());
    CHECK(OnlineConfig::sanitizeUrl("ftp://cosmic.example.com").empty());
    CHECK(OnlineConfig::sanitizeUrl("https://cosmic.example.com:99999").empty());
    ParsedUrl u;
    CHECK(parseHttpUrl("https://a.b.c:8443/x/y?z=1", u));
    CHECK(u.https && u.host == "a.b.c" && u.port == 8443 && u.pathAndQuery == "/x/y?z=1");
    std::map<std::string, std::string> h;
    parseHeaderLines("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nRetry-After:  30\r\n\r\n", h);
    CHECK(h["content-type"] == "application/json");
    CHECK(h["retry-after"] == "30");
    CHECK(parseIsoUtc("2026-11-13T10:30:00.000Z").has_value());
    CHECK(!parseIsoUtc("13/11/2026").has_value());
    auto a = parseIsoUtc("1970-01-02T00:00:00Z");
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(a->time_since_epoch()).count() == 86400);
}

TEST_CASE("online: cache keeps answers on disk with their age") {
    fs::path dir = tempDir("cache");
    {
        OnlineCache c(dir);
        c.put("k1", {{"entries", {1, 2, 3}}}, {{"total", 3}});
        auto e = c.get("k1");
        CHECK(e && e->data["entries"].size() == 3);
        CHECK(e->ageSeconds() < 5.0);
    }
    {
        OnlineCache c(dir);  // new instance: read back from disk
        auto e = c.get("k1");
        CHECK(e.has_value());
        CHECK(e->meta["total"] == 3);
        CHECK(!c.get("k2").has_value());
        CHECK(c.clear() == 1);
        CHECK(!c.get("k1").has_value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("online: the saved session is protected and bound to its server") {
    fs::path dir = tempDir("session");
    SessionStore store(dir / "session.json");
    SessionTokens t;
    t.refreshToken = "refresh-secret-value-1234567890";
    t.refreshExpiresIso = "2099-01-01T00:00:00.000Z";
    t.playerId = "p1";
    t.username = "Nova";
    t.accessToken = "never-saved-access-token";
    CHECK(store.save(t, "https://a.example"));
    auto text = FileUtil::readText(dir / "session.json");
    CHECK(text.has_value());
    CHECK(text->find("refresh-secret-value") == std::string::npos);  // encoded / encrypted
    CHECK(text->find("never-saved-access-token") == std::string::npos);
#if !defined(_WIN32)
    struct stat st {};
    CHECK(::stat((dir / "session.json").string().c_str(), &st) == 0);
    CHECK((st.st_mode & 077) == 0);  // readable by the owner only
#endif
    auto back = store.load("https://a.example");
    CHECK(back && back->refreshToken == t.refreshToken && back->username == "Nova");
    CHECK(back->accessToken.empty());
    CHECK(!store.load("https://other.example").has_value());
    t.refreshExpiresIso = "2001-01-01T00:00:00.000Z";
    store.save(t, "https://a.example");
    CHECK(!store.load("https://a.example").has_value());  // expired
    CHECK(!SessionStore::unprotect("garbage").has_value());
    store.erase();
    CHECK(!fs::exists(dir / "session.json"));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("online: replay format round trip and corruption") {
    ReplayData d;
    d.levelId = "sylvanox_01";
    d.levelHash = std::string(64, 'a');
    d.attemptId = makeUuidV4();
    d.durationMs = 2000;
    for (int i = 0; i < 61; ++i) d.frames.push_back({100.0f + i * 3.0f, 500.0f, 1, 2, 10});
    d.events.push_back({10, ReplayEventType::Jump, 0, 0});
    d.events.push_back({60, ReplayEventType::Finish, 0, 0});
    CHECK(d.attemptId.size() == 36 && d.attemptId[14] == '4');
    auto bytes = d.encode();
    CHECK(bytes.size() > 8 && bytes.size() < d.serialize().size());
    std::string err;
    auto back = ReplayData::decode(bytes, &err);
    CHECK(back.has_value());
    CHECK(back->attemptId == d.attemptId && back->levelHash == d.levelHash && back->levelId == d.levelId);
    CHECK(back->frames.size() == 61 && back->events.size() == 2);
    CHECK_NEAR(back->frames[60].x, 280.0, 1e-3);
    CHECK_NEAR(back->sampleAt(0.5f).x, 145.0, 1e-3);
    auto broken = bytes;
    broken[0] = 'X';
    CHECK(!ReplayData::decode(broken, &err));
    auto truncated = bytes;
    truncated.resize(truncated.size() - 5);
    CHECK(!ReplayData::decode(truncated, &err));
    auto bomb = bytes;
    bomb[4] = 0xFF;
    bomb[5] = 0xFF;
    bomb[6] = 0xFF;
    bomb[7] = 0x7F;
    CHECK(!ReplayData::decode(bomb, &err));
    CHECK(replayPowerIndex("teleport") == 4);
    CHECK(std::string(replayPowerId(4)) == "teleport");
}

TEST_CASE("online: the replay recorder follows a real run") {
    ContentDatabase content;
    content.reload();
    float elapsed = 0.0f;
    ReplayData rep = playCorridor(content, 0.0f, &elapsed);
    CHECK(elapsed > 10.0f);
    CHECK(rep.durationMs == static_cast<uint32_t>(std::lround(elapsed * 1000.0f)));
    // About sampleHz samples per second of level time.
    CHECK(std::abs(static_cast<double>(rep.frames.size()) - elapsed * rep.sampleHz) <= 3.0);
    CHECK(rep.countEvents(ReplayEventType::Finish) == 1);
    CHECK(rep.events.back().type == ReplayEventType::Finish);
    CHECK(rep.events.back().frame == rep.frames.size() - 1);
    // Starts at the player start, moves right, ends at the exit.
    CHECK_NEAR(rep.frames.front().x, 165.0, 40.0);
    CHECK(rep.frames.back().x > 2150.0f);
    float maxStep = 0.0f;
    for (size_t i = 1; i < rep.frames.size(); ++i) maxStep = std::max(maxStep, std::abs(rep.frames[i].x - rep.frames[i - 1].x));
    CHECK(maxStep < 20.0f);
}

TEST_CASE("online: the local queue keeps only improvements per category") {
    fs::path dir = tempDir("queue");
    AttemptStore store(dir / "attempts.json", dir / "replays");
    RunCandidate run;
    run.levelId = "sylvanox_01";
    run.levelHash = std::string(64, 'b');
    run.timeMs = 90000;
    run.deaths = 2;
    run.collectibles = 10;
    run.totalCollectibles = 10;
    run.hasReplay = true;
    auto cats = eligibleCategories(run);
    CHECK(cats.size() == 3);  // ANY%, 100%, SPEEDRUN (deaths exclude NO DEATH / NO DAMAGE)
    std::string a1 = makeUuidV4();
    CHECK(store.add(run, a1, "me") != nullptr);
    CHECK(store.unsentCount("me") == 3);
    // Slower and not better in any category: not kept.
    run.timeMs = 95000;
    CHECK(store.add(run, makeUuidV4(), "me") == nullptr);
    // Slower but the first run without death: kept only for NO DEATH / NO DAMAGE.
    run.deaths = 0;
    OnlineAttempt* a2 = store.add(run, makeUuidV4(), "me");
    CHECK(a2 != nullptr);
    CHECK(a2->submissions.size() == 2);
    CHECK(a2->submission(RunCategory::NoDeath) && a2->submission(RunCategory::NoDamage));
    CHECK(store.save());
    AttemptStore again(dir / "attempts.json", dir / "replays");
    again.load();
    CHECK(again.all().size() == 2);
    CHECK(again.find(a1)->submissions.size() == 3);
    CHECK(again.find(a1)->submitBody(RunCategory::AnyPercent)["attempt_id"] == a1);
    CHECK(!again.find(a1)->submitBody(RunCategory::AnyPercent).contains("player_id"));
    CHECK(*again.bestTime(run.levelHash, RunCategory::AnyPercent) == 90000);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("online: nothing is sent when online is disabled or not configured") {
    auto server = std::make_shared<FakeServer>();
    fs::path dir = tempDir("disabled");
    OnlineSettings off;
    off.enabled = false;
    {
        OnlineService svc;
        svc.init(off, "http://127.0.0.1:9", std::make_unique<FakeTransport>(server), dir);
        CHECK(svc.state() == OnlineState::Disabled);
        ApiResult got;
        svc.get("/api/v1/meta", [&](const ApiResult& r) { got = r; });
        CHECK(got.code == "ONLINE_DISABLED");
        svc.shutdown();
    }
    {
        OnlineService svc;
        svc.init(OnlineSettings{}, "http://not-https.example.com", std::make_unique<FakeTransport>(server), dir);
        CHECK(svc.state() == OnlineState::NotConfigured);
        svc.shutdown();
    }
    CHECK(server->log.empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("online: login, session refresh and session loss") {
    Harness h("login");
    bool done = false;
    ApiResult res;
    h.svc.login("Nova", "bad password", true, [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(!res.ok && res.code == "INVALID_CREDENTIALS");
    CHECK(!h.svc.loggedIn());
    CHECK(h.login());
    CHECK(h.svc.loggedIn());
    CHECK(h.svc.username() == "Nova");
    CHECK(h.svc.state() == OnlineState::Online);
    CHECK(h.svc.hasSavedSession());
    // The server forgets the access token: the client refreshes once and retries.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->access = "rotated-away-0000000000000";
    }
    done = false;
    h.svc.refreshProfile([&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(res.ok);
    CHECK(h.server->refreshCalls == 1);
    // A revoked session logs the player out (with a notification), not a crash or a loop.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->access = "rotated-away-again-00000000";
        h.server->refreshRevoked = true;
    }
    h.svc.takeNotifications();
    done = false;
    h.svc.refreshProfile([&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(!res.ok);
    CHECK(!h.svc.loggedIn());
    CHECK(!h.svc.hasSavedSession());
    auto notes = h.svc.takeNotifications();
    CHECK(!notes.empty() && notes.back().title == "Déconnecté de Cosmic Online");
}

TEST_CASE("online: a saved session is resumed at the next start") {
    auto server = std::make_shared<FakeServer>();
    fs::path dir = tempDir("resume");
    {
        OnlineService svc;
        svc.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<FakeTransport>(server), dir);
        bool done = false;
        svc.login("Nova", "good password", true, [&](const ApiResult&) { done = true; });
        pump(svc, [&] { return done; });
        svc.shutdown();
    }
    {
        OnlineService svc;  // auto-login: refresh + profile, without asking the password
        svc.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<FakeTransport>(server), dir);
        CHECK(pump(svc, [&] { return !svc.profile().is_null(); }));
        CHECK(svc.username() == "Nova");
        CHECK(server->loginCalls == 1);
        svc.shutdown();
    }
    {
        OnlineSettings manual;
        manual.autoLogin = false;
        OnlineService svc;
        size_t before = server->log.size();
        svc.init(manual, "http://127.0.0.1:9", std::make_unique<FakeTransport>(server), dir);
        for (int i = 0; i < 20; ++i) svc.update(0.016f);
        CHECK(server->log.size() == before);  // manual login: nothing sent at startup
        svc.shutdown();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("online: network failures fall back to the cache, flagged as stale") {
    Harness h("offline");
    CHECK(h.login());
    ApiResult res;
    bool done = false;
    h.svc.get("/api/v1/leaderboards/level/sylvanox_01", [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(res.ok && !res.fromCache);
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->down = true;
    }
    done = false;
    h.svc.get("/api/v1/leaderboards/level/sylvanox_01", [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(res.ok);
    CHECK(res.fromCache);
    CHECK(res.data["entries"][0]["time_ms"] == 61000);
    CHECK(h.svc.state() == OnlineState::Offline);
    // Without cached data the failure is reported, never turned into a success.
    done = false;
    h.svc.get("/api/v1/leaderboards/level/pyralis_01", [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(!res.ok && res.networkFailure());
    // A malformed answer (captive portal) is not a success either.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->down = false;
        h.server->malformed = true;
    }
    done = false;
    h.svc.get("/api/v1/leaderboards/level/pyralis_01", [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(!res.ok && res.code == "MALFORMED_RESPONSE");
}

TEST_CASE("online: runs finished offline are synchronised when the connection returns") {
    ContentDatabase content;
    content.reload();
    float elapsed = 0.0f;
    ReplayData rep = playCorridor(content, 0.0f, &elapsed);
    Harness h("sync");
    CHECK(h.login());
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->down = true;
    }
    // Make the client notice the outage.
    bool done = false;
    h.svc.checkConnection([&](const ApiResult&) { done = true; });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(h.svc.state() == OnlineState::Offline);
    int64_t ms = rep.durationMs;
    OnlineAttempt* a = h.svc.recordRun(candidateFor(rep, ms), rep.attemptId, &rep);
    CHECK(a != nullptr);
    CHECK(fs::exists(h.svc.attempts().replayPath(rep.attemptId)));
    pump(h.svc, [&] { return !h.svc.syncing(); });
    a = h.svc.attempts().find(rep.attemptId);
    for (const auto& s : a->submissions) CHECK(s.status == RecordStatus::Local);
    CHECK(h.svc.unsentCount() == static_cast<int>(a->submissions.size()));
    // The queue survives a restart of the service.
    AttemptStore reloaded(h.dir / "saves" / "online_attempts.json", h.dir / "online" / "replays");
    reloaded.load();
    CHECK(reloaded.find(rep.attemptId) != nullptr);
    // Connection back: the reconnection timer fires, everything is sent, the replay too.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->down = false;
    }
    h.svc.takeNotifications();
    CHECK(pump(h.svc, [&] {
        const OnlineAttempt* x = h.svc.attempts().find(rep.attemptId);
        return !h.svc.syncing() && x->replayUploaded && h.svc.unsentCount() == 0;
    }, 5000, 5.0f));
    a = h.svc.attempts().find(rep.attemptId);
    for (const auto& s : a->submissions) {
        CHECK(s.status == RecordStatus::Verified);
        CHECK(!s.recordId.empty());
    }
    CHECK(h.server->replayCalls == 1);
    CHECK(h.server->submitCalls == static_cast<int>(a->submissions.size()));
    auto notes = h.svc.takeNotifications();
    bool reconnected = false, verified = false, finished = false;
    for (const auto& n : notes) {
        reconnected |= n.title == "Connexion rétablie";
        verified |= n.title == "Record vérifié";
        finished |= n.title == "Synchronisation terminée";
    }
    CHECK(reconnected && verified && finished);
    auto changes = h.svc.takeRankChanges();
    CHECK(!changes.empty() && changes[0].rank == 1);
    // A second sync sends nothing again (no duplicates).
    h.svc.syncNow();
    pump(h.svc, [&] { return !h.svc.syncing(); });
    CHECK(h.server->submitCalls == static_cast<int>(a->submissions.size()));
}

TEST_CASE("online: runs finished while logged out wait for an explicit submission") {
    ContentDatabase content;
    content.reload();
    ReplayData rep = playCorridor(content, 0.0f, nullptr);
    Harness h("claim");
    h.svc.recordRun(candidateFor(rep, rep.durationMs), rep.attemptId, &rep);
    CHECK(h.login());
    pump(h.svc, [&] { return !h.svc.syncing(); });
    CHECK(h.server->submitCalls == 0);  // not attributed automatically
    h.svc.syncNow(true);                // "SUBMIT RECORD"
    CHECK(pump(h.svc, [&] { return !h.svc.syncing() && h.server->submitCalls > 0; }));
    CHECK(h.svc.attempts().find(rep.attemptId)->ownerId == FakeServer::player()["id"].get<std::string>());
}

TEST_CASE("online: requests never block the main thread") {
    Harness h("async");
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->delayMs = 400;
    }
    auto t0 = std::chrono::steady_clock::now();
    bool done = false;
    h.svc.checkConnection([&](const ApiResult&) { done = true; });
    h.svc.update(0.016f);
    auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(spent < 50);
    CHECK(!done);
    CHECK(h.svc.requestsInFlight() == 1);
    CHECK(pump(h.svc, [&] { return done; }, 3000));
}

TEST_CASE("online: queued requests can be cancelled") {
    auto server = std::make_shared<FakeServer>();
    server->delayMs = 200;
    OnlineClient client(std::make_unique<FakeTransport>(server));
    std::atomic<int> cancelled{0}, completed{0};
    HttpRequest rq;
    rq.url = "http://127.0.0.1:9/api/v1/health";
    for (int i = 0; i < 3; ++i) {
        HttpRequest copy = rq;
        copy.cancel = std::make_shared<std::atomic<bool>>(false);
        client.send(copy, [&](const HttpResponse& r) { (r.error == NetError::Cancelled ? cancelled : completed)++; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    client.cancelAll();
    for (int i = 0; i < 100 && cancelled + completed < 3; ++i) {
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(cancelled + completed == 3);
    CHECK(cancelled >= 2);
}

TEST_CASE("online: username availability is asked to the server") {
    Harness h("username");
    auto ask = [&](const std::string& name) {
        bool done = false;
        Json data;
        h.svc.checkUsername(name, [&](const ApiResult& r) {
            done = true;
            if (r.ok) data = r.data;
        });
        CHECK(pump(h.svc, [&] { return done; }));
        return data;
    };
    CHECK(jget<bool>(ask("Andromeda"), "available", false));
    Json taken = ask("Orion");
    CHECK(!jget<bool>(taken, "available", true));
    CHECK(jstr(taken, "reason") == "taken");
    // The name is URL-encoded and never served from the cache.
    ask("a b&c");
    bool encoded = false;
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        for (const auto& l : h.server->log) encoded |= l.find("username=a%20b%26c") != std::string::npos;
    }
    CHECK(encoded);
    h.server->down = true;
    bool done = false, ok = true;
    h.svc.checkUsername("Andromeda", [&](const ApiResult& r) {
        done = true;
        ok = r.ok;
    });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(!ok);
}

TEST_CASE("online: friend requests are notified once, acceptances too") {
    Harness h("friends");
    CHECK(h.login());
    h.svc.takeNotifications();
    auto countFriendNotices = [&](const std::string& title) {
        int n = 0;
        for (const auto& no : h.svc.takeNotifications())
            if (no.kind == OnlineService::Notification::Kind::Friend && no.title == title) ++n;
        return n;
    };
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->friends["incoming"].push_back(FakeServer::friendEntry("22222222-2222-4222-8222-222222222222", "Vega"));
        h.server->friends["outgoing"].push_back(FakeServer::friendEntry("33333333-3333-4333-8333-333333333333", "Lyra"));
    }
    // Read a few seconds after the connection, without any screen open.
    CHECK(pump(h.svc, [&] { return h.svc.pendingFriendRequests() == 1; }, 3000, 0.5f));
    CHECK(countFriendNotices("Demande d'ami") == 1);
    // The same request is not notified again at the next poll.
    bool done = false;
    h.svc.refreshFriends([&](const ApiResult&) { done = true; });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(countFriendNotices("Demande d'ami") == 0);
    CHECK(h.svc.pendingFriendRequests() == 1);
    // Lyra accepts; the player answers Vega.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->friends["outgoing"] = Json::array();
        h.server->friends["incoming"] = Json::array();
        h.server->friends["friends"].push_back(FakeServer::friendEntry("33333333-3333-4333-8333-333333333333", "Lyra"));
    }
    done = false;
    h.svc.refreshFriends([&](const ApiResult&) { done = true; });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(countFriendNotices("Demande acceptée") == 1);
    CHECK(h.svc.pendingFriendRequests() == 0);

    // What was seen is remembered across restarts: no repeated notification.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->friends["incoming"].push_back(FakeServer::friendEntry("44444444-4444-4444-8444-444444444444", "Altair"));
    }
    done = false;
    h.svc.refreshFriends([&](const ApiResult&) { done = true; });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(countFriendNotices("Demande d'ami") == 1);
    OnlineService again;
    again.init({}, "http://127.0.0.1:9", std::make_unique<FakeTransport>(h.server), h.dir);
    CHECK(pump(again, [&] { return again.loggedIn() && again.pendingFriendRequests() == 1; }, 4000, 0.5f));
    int repeated = 0;
    for (const auto& no : again.takeNotifications()) repeated += no.kind == OnlineService::Notification::Kind::Friend;
    CHECK(repeated == 0);
    again.shutdown();

    // Logging out clears the badge.
    h.svc.logout(false);
    CHECK(pump(h.svc, [&] { return !h.svc.loggedIn(); }));
    CHECK(h.svc.pendingFriendRequests() == 0);
}

TEST_CASE("online: new messages are notified once, never for the conversation on screen") {
    Harness h("messages");
    CHECK(h.login());
    h.svc.takeNotifications();
    auto messageNotices = [&] {
        std::vector<OnlineService::Notification> v;
        for (auto& no : h.svc.takeNotifications())
            if (no.kind == OnlineService::Notification::Kind::Message) v.push_back(no);
        return v;
    };
    auto msg = [](const char* id, const char* conv, const char* at, const char* sender, const char* body, const char* kind = "direct",
                  const char* name = nullptr) {
        Json m = {{"id", id}, {"conversation_id", conv}, {"created_at", at}, {"sender", sender}, {"body", body}, {"kind", kind}};
        m["name"] = name ? Json(name) : Json(nullptr);
        return m;
    };
    auto poll = [&] {
        bool done = false;
        h.svc.refreshUnread([&](const ApiResult&) { done = true; });
        CHECK(pump(h.svc, [&] { return done; }));
    };
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->unread = {{"total", 1}, {"latest", Json::array({msg("m1", "c1", "2026-11-13T10:00:00.000Z", "Orion", "Salut !")})}};
    }
    // Read automatically a few seconds after the connection.
    CHECK(pump(h.svc, [&] { return h.svc.unreadMessages() == 1; }, 3000, 0.5f));
    auto n = messageNotices();
    CHECK(n.size() == 1);
    if (!n.empty()) {
        CHECK(n[0].title == "Message de Orion");
        CHECK(n[0].text == "Salut !");
    }
    poll();
    CHECK(messageNotices().empty());  // same message: no repeat
    // A group message (named after the group) and one in the conversation on screen.
    h.svc.setOpenConversation("c1");
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->unread = {{"total", 3},
                            {"latest", Json::array({msg("m3", "c1", "2026-11-13T10:02:00.000Z", "Orion", "Tu es là ?"),
                                                    msg("m2", "g1", "2026-11-13T10:01:00.000Z", "Lyra", "Session vendredi ?", "group", "Équipe")})}};
    }
    poll();
    n = messageNotices();
    CHECK(n.size() == 1);
    if (!n.empty()) CHECK(n[0].title == "Lyra · Équipe");
    CHECK(h.svc.unreadMessages() == 3);
    // Several at once: one summary. Remembered across restarts.
    h.svc.setOpenConversation({});
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->unread = {{"total", 5},
                            {"latest", Json::array({msg("m5", "c2", "2026-11-13T10:04:00.000Z", "Vega", "GG"),
                                                    msg("m4", "c1", "2026-11-13T10:03:00.000Z", "Orion", std::string(200, 'x').c_str())})}};
    }
    poll();
    n = messageNotices();
    CHECK(n.size() == 1);
    if (!n.empty()) CHECK(n[0].title == "2 nouveaux messages");
    OnlineService again;
    again.init({}, "http://127.0.0.1:9", std::make_unique<FakeTransport>(h.server), h.dir);
    CHECK(pump(again, [&] { return again.loggedIn() && again.unreadMessages() == 5; }, 4000, 0.5f));
    int repeated = 0;
    for (const auto& no : again.takeNotifications()) repeated += no.kind == OnlineService::Notification::Kind::Message;
    CHECK(repeated == 0);
    again.shutdown();
    h.svc.logout(false);
    CHECK(pump(h.svc, [&] { return !h.svc.loggedIn(); }));
    CHECK(h.svc.unreadMessages() == 0);
}

TEST_CASE("online: a run on a revision the server does not know yet is sent later") {
    ContentDatabase content;
    content.reload();
    float elapsed = 0.0f;
    ReplayData rep = playCorridor(content, 0.0f, &elapsed);
    Harness h("pending_revision");
    CHECK(h.login());
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->levelPending = true;
    }
    OnlineAttempt* a = h.svc.recordRun(candidateFor(rep, rep.durationMs), rep.attemptId, &rep);
    CHECK(a != nullptr);
    CHECK(pump(h.svc, [&] { return h.server->submitCalls > 0 && !h.svc.syncing(); }));
    a = h.svc.attempts().find(rep.attemptId);
    // Kept LOCAL (not blocked for good).
    for (const auto& s : a->submissions) {
        CHECK(s.status == RecordStatus::Local);
        CHECK(!s.blocked);
    }
    CHECK(h.svc.unsentCount() > 0);
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->levelPending = false;
    }
    h.svc.syncNow();
    CHECK(pump(h.svc, [&] { return !h.svc.syncing() && h.svc.unsentCount() == 0; }, 5000));
}

TEST_CASE("online: workshop notifications are announced once, the first reading is silent") {
    Harness h("notifications");
    auto note = [](const char* id, const char* at, const char* text) {
        return Json{{"id", id}, {"kind", "new_level"}, {"created_at", at}, {"text", text}, {"read", false}};
    };
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->notifications = Json::array({note("n1", "2026-11-13T10:00:00.000Z", "Nova vient de publier un nouveau niveau : A")});
    }
    CHECK(h.login());
    // Read automatically after the connection; what was already there is not "new".
    CHECK(pump(h.svc, [&] { return h.svc.unreadNotifications() == 1; }, 3000, 0.5f));
    auto workshop = [&] {
        std::vector<OnlineService::Notification> v;
        for (auto& no : h.svc.takeNotifications())
            if (no.kind == OnlineService::Notification::Kind::Workshop) v.push_back(no);
        return v;
    };
    CHECK(workshop().empty());
    auto poll = [&] {
        bool done = false;
        h.svc.refreshNotifications([&](const ApiResult&) { done = true; });
        CHECK(pump(h.svc, [&] { return done; }));
    };
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->notifications.insert(h.server->notifications.begin(), note("n2", "2026-11-13T10:05:00.000Z", "Orion a répondu à votre commentaire"));
    }
    poll();
    auto n = workshop();
    CHECK(n.size() == 1);
    if (!n.empty()) CHECK(n[0].text == "Orion a répondu à votre commentaire");
    CHECK(h.svc.unreadNotifications() == 2);
    poll();
    CHECK(workshop().empty());  // no repeat
    // A screen that marked everything read shares the counter.
    h.svc.noteNotificationsUnread(0);
    CHECK(h.svc.unreadNotifications() == 0);
}

TEST_CASE("online: time played is kept until sent, whole seconds only, never while logged out") {
    Harness h("playtime");
    h.svc.notePlaytime("test_01", 120.0);  // logged out: not linked to any account
    CHECK(h.svc.pendingPlaytime() == 0.0);
    CHECK(h.login());
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->playtimeStatus = 429;
    }
    h.svc.notePlaytime("test_01", 90.5);
    h.svc.notePlaytime("other", 30.0);
    CHECK(std::abs(h.svc.pendingPlaytime() - 120.5) < 1e-6);
    bool done = false;
    h.svc.flushPlaytime([&](const ApiResult&) { done = true; });
    CHECK(pump(h.svc, [&] { return done; }));
    CHECK(std::abs(h.svc.pendingPlaytime() - 120.5) < 1e-6);  // refused for now: kept
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->playtimeStatus = 200;
    }
    // Kept on disk: another start of the game sends it.
    OnlineService again;
    again.init({}, "http://127.0.0.1:9", std::make_unique<FakeTransport>(h.server), h.dir);
    CHECK(pump(again, [&] { return again.loggedIn(); }, 4000, 0.5f));
    CHECK(std::abs(again.pendingPlaytime() - 120.5) < 1e-6);
    CHECK(pump(again, [&] { return again.pendingPlaytime() < 1.0; }, 4000, 1.0f));
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        CHECK(h.server->playtimeReports.size() == 1);
        std::map<std::string, int> sent;
        if (!h.server->playtimeReports.empty())
            for (const auto& x : h.server->playtimeReports[0]["sessions"]) sent[x["level_id"].get<std::string>()] = x["seconds"].get<int>();
        CHECK(sent["test_01"] == 90);
        CHECK(sent["other"] == 30);
    }
    CHECK(std::abs(again.pendingPlaytime() - 0.5) < 1e-6);  // the fraction waits for the next session
    again.shutdown();
}
