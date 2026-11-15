// Daily challenges, the online side of the game (game/online/DailyChallenges.h): reading the
// challenge (never trusted blindly), the server's clock, the cache that keeps it playable
// offline until its end, the local results and streak, and the runs waiting for the
// connection (sent with their format 4 replay once it is there), against a scripted server
// on the real network thread.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "engine/core/FileUtil.h"
#include "game/online/DailyChallenges.h"
#include "game/online/OnlineService.h"
#include "game/online/Replay.h"
#include "packaging/Sha256.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path tempDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("cosmic_challenges_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

std::string iso(int64_t ms) {
    return ChallengeService::utcDate(ms) + "T" + [&] {
        int64_t s = ((ms / 1000) % 86400 + 86400) % 86400;
        char b[16];
        std::snprintf(b, sizeof b, "%02d:%02d:%02d", static_cast<int>(s / 3600), static_cast<int>((s / 60) % 60), static_cast<int>(s % 60));
        return std::string(b);
    }() + "Z";
}

/// A challenge of the day starting at `startMs` (UTC midnight), as the server writes it.
Json challengeJson(int64_t startMs, const std::string& level = "glacior_02") {
    std::string date = ChallengeService::utcDate(startMs);
    std::string id = "daily-" + date;
    Json rules = {{"hearts", 2}, {"slots", 3}, {"powers", {"grapple", "glide", "dash"}}, {"stations", true}, {"checkpoints", false}};
    auto r = ChallengeRules::fromJson(id, rules);
    return {{"id", id},
            {"kind", "daily"},
            {"date", date},
            {"starts_at", iso(startMs)},
            {"ends_at", iso(startMs + 86400000)},
            {"source", "auto"},
            {"level", {{"source", "campaign"}, {"level_id", level}, {"level_version", 8}, {"level_hash", std::string(64, 'a')}, {"title", "Grottes"}, {"author", nullptr}}},
            {"rules", rules},
            {"rules_hash", r->hash()},
            {"tiers", {{"silver_ms", 85000}, {"gold_ms", 72000}, {"gold_no_damage", true}}}};
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

struct ChallengeServer {
    std::mutex m;
    bool down = false;
    int64_t serverNow = 0;  // the server's clock (ms)
    Json current;
    std::string endedCode;  // POST runs answered with this error ("" = accepted)
    int posts = 0, uploads = 0;
    Json lastBody;
    std::vector<uint8_t> lastReplay;
    std::string access = "access-token-000000000000000000000";

    HttpResponse handle(const HttpRequest& rq) {
        std::lock_guard<std::mutex> lock(m);
        std::string path = rq.url.substr(rq.url.find("/api/"));
        if (down) {
            HttpResponse r;
            r.error = NetError::Connect;
            r.errorMessage = "refused";
            return r;
        }
        if (path == "/api/v1/health") return okData({{"status", "ok"}});
        if (path == "/api/v1/auth/login")
            return okData({{"player", {{"id", "11111111-1111-4111-8111-111111111111"}, {"username", "Nova"}, {"country_code", "FR"}}},
                           {"session", {{"access_token", access}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                                        {"refresh_token", "refresh-token-00000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}}}});
        if (path == "/api/v1/challenges/daily/current") return okData({{"server_time", iso(serverNow)}, {"challenge", current}, {"previous", nullptr}});
        if (path.rfind("/api/v1/challenges/daily?", 0) == 0) return okData({{"challenges", Json::array()}, {"next_before", nullptr}});
        if (path.rfind("/api/v1/challenges/", 0) == 0 && path.find("/runs") != std::string::npos && rq.method == "POST") {
            ++posts;
            lastBody = Json::parse(rq.body);
            if (!endedCode.empty()) return apiError(409, endedCode);
            return okData({{"duplicate", false},
                           {"run", {{"id", "run-1"}, {"status", "PENDING"}, {"tier", "gold"}, {"time_ms", lastBody["time_ms"]}}},
                           {"replay", {{"required", true}, {"accepted", true}, {"upload_path", "/api/v1/challenges/runs/run-1/replay"}}}},
                          201);
        }
        if (path == "/api/v1/challenges/runs/run-1/replay" && rq.method == "PUT") {
            ++uploads;
            lastReplay.assign(rq.body.begin(), rq.body.end());
            auto rep = ReplayData::decode(lastReplay);
            if (!rep || rep->challengeRules.empty()) return apiError(422, "REPLAY_MALFORMED");
            return okData({{"run", {{"id", "run-1"}, {"status", "VERIFIED"}, {"tier", "gold"}}}});
        }
        if (path.rfind("/api/v1/challenges/me", 0) == 0)
            return okData({{"streak", {{"current", 4}, {"best", 6}, {"last_date", ChallengeService::utcDate(serverNow)}}},
                           {"results", Json::array({{{"challenge_id", current.value("id", "")}, {"date", current.value("date", "")},
                                                     {"best_time_ms", 70000}, {"tier", "gold"}, {"status", "VERIFIED"}}})}});
        return apiError(404, "NOT_FOUND");
    }
};

class Transport final : public IHttpTransport {
public:
    explicit Transport(std::shared_ptr<ChallengeServer> s) : s_(std::move(s)) {}
    HttpResponse perform(const HttpRequest& rq) override { return s_->handle(rq); }
    const char* name() const override { return "fake"; }

private:
    std::shared_ptr<ChallengeServer> s_;
};

struct Rig {
    std::shared_ptr<ChallengeServer> server = std::make_shared<ChallengeServer>();
    fs::path dir;
    OnlineService online;
    ChallengeService challenges;
    explicit Rig(const std::string& name, fs::path keep = {}) : dir(keep.empty() ? tempDir(name) : keep) {
        online.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<Transport>(server), dir);
        challenges.init(online, dir / "saves" / "challenges");
    }
    ~Rig() { online.shutdown(); }
    template <typename Pred>
    bool pump(Pred done, int timeoutMs = 3000) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < end) {
            online.update(0.05f);
            challenges.update(0.05f);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done();
    }
    bool login() {
        bool done = false, ok = false;
        online.login("Nova", "good password", true, [&](const ApiResult& r) {
            done = true;
            ok = r.ok;
        });
        pump([&] { return done; });
        return ok;
    }
};

int64_t todayStart() {
    int64_t now = ChallengeService::localNowMs();
    return now - now % 86400000;
}

ReplayData sampleReplay(const DailyChallenge& c) {
    ReplayData d;
    d.levelId = c.level.levelId;
    d.levelHash = c.level.hash;
    d.attemptId = makeUuidV4();
    d.frames.push_back({});
    d.keysPerSlot = true;
    d.inputs.assign(120, InputFrame{});
    d.checks.push_back({60, 1});
    d.challengeRules = c.rules.canonical();
    return d;
}

}  // namespace

TEST_CASE("challenge service: a challenge is read strictly, its tiers follow the contract") {
    Json j = challengeJson(todayStart());
    std::string err;
    auto c = DailyChallenge::fromJson(j, &err);
    CHECK(c.has_value());
    CHECK(c->endsAtMs - c->startsAtMs == 86400000);
    CHECK(c->rules.powers == (std::vector<std::string>{"dash", "glide", "grapple"}));
    // Tiers: Or needs the time and (here) no damage; Argent the time; Bronze the end.
    CHECK(c->tierFor(70000, 0) == ChallengeTier::Gold);
    CHECK(c->tierFor(70000, 1) == ChallengeTier::Silver);
    CHECK(c->tierFor(85000, 3) == ChallengeTier::Silver);
    CHECK(c->tierFor(85001, 0) == ChallengeTier::Bronze);
    CHECK(challengeTierFromJson(Json("gold")) == ChallengeTier::Gold);
    CHECK(challengeTierFromJson(Json("silver")) == ChallengeTier::Silver);
    CHECK(challengeTierFromJson(Json()) == ChallengeTier::None);
    // Rules that are not the ones hashed, a bad date, an unsafe id: refused.
    Json bad = j;
    bad["rules"]["hearts"] = 3;
    CHECK(!DailyChallenge::fromJson(bad, &err).has_value());
    bad = j;
    bad["ends_at"] = bad["starts_at"];
    CHECK(!DailyChallenge::fromJson(bad).has_value());
    bad = j;
    bad["id"] = "../../x";
    CHECK(!DailyChallenge::fromJson(bad).has_value());
    bad = j;
    bad["level"]["level_hash"] = "zz";
    CHECK(!DailyChallenge::fromJson(bad).has_value());
}

TEST_CASE("challenge service: the server's clock, the cache and offline play until the end") {
    fs::path dir = tempDir("cache");
    int64_t start = todayStart();
    {
        Rig rig("cache", dir);
        rig.server->current = challengeJson(start);
        // The server is 3 hours ahead of this computer: the countdown follows the server.
        rig.server->serverNow = ChallengeService::localNowMs() + 3 * 3600 * 1000;
        rig.challenges.refresh(true);
        CHECK(rig.pump([&] { return rig.challenges.fetchState() == ChallengeService::Fetch::Ok; }));
        CHECK(rig.challenges.serverTimeKnown());
        CHECK(std::llabs(rig.challenges.nowMs() - rig.server->serverNow) < 5000);
        CHECK(FileUtil::exists(dir / "saves" / "challenges" / (rig.server->current["id"].get<std::string>() + ".json")));
    }
    {
        // Offline: the cached challenge, the saved offset.
        Rig rig("cache", dir);
        rig.server->down = true;
        CHECK(rig.challenges.serverTimeKnown());
        int64_t local = ChallengeService::localNowMs();
        bool beforeEnd = local + 3 * 3600 * 1000 < start + 86400000;
        if (beforeEnd) {
            CHECK(rig.challenges.current() != nullptr);
            CHECK(rig.challenges.current()->id == "daily-" + ChallengeService::utcDate(start));
        }
        rig.challenges.refresh(true);
        CHECK(rig.pump([&] { return rig.challenges.fetchState() == ChallengeService::Fetch::Offline; }));
        // Past its end it is an archive.
        rig.challenges.setServerOffsetForTests(start + 86400000 + 1000 - local);
        CHECK(rig.challenges.current() == nullptr);
        CHECK(rig.challenges.past().size() == 1);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("challenge service: without an account the result stays on this computer, with its streak") {
    Rig rig("local");
    rig.server->down = true;
    auto c = DailyChallenge::fromJson(challengeJson(todayStart()));
    CHECK(c.has_value());
    ReplayData rep = sampleReplay(*c);
    auto r1 = rig.challenges.recordRun(*c, 90000, 1, 2, c->level.hash, rep.attemptId, &rep, true);
    CHECK(r1.newBest);
    CHECK(r1.tier == ChallengeTier::Bronze);
    CHECK(r1.state == ChallengeRunState::LocalOnly);
    CHECK(rig.challenges.waitingCount() == 0);
    auto r2 = rig.challenges.recordRun(*c, 71000, 0, 0, c->level.hash, makeUuidV4(), &rep, true);
    CHECK(r2.newBest);
    CHECK(r2.tier == ChallengeTier::Gold);
    CHECK(r2.previousBestMs == 90000);
    const ChallengeResult* res = rig.challenges.result(c->id);
    CHECK(res && res->bestMs == 71000 && res->tier == ChallengeTier::Gold && res->hasReplay);
    auto best = rig.challenges.bestReplay(c->id);
    CHECK(best.has_value());
    CHECK(best->challengeRules == c->rules.canonical());
    rig.challenges.setServerOffsetForTests(0);
    CHECK(rig.challenges.streak().current == 1);
    CHECK(!rig.challenges.streak().fromServer);
    // Read again from the disk.
    ChallengeService again;
    again.init(rig.online, rig.dir / "saves" / "challenges");
    CHECK(again.result(c->id) && again.result(c->id)->bestMs == 71000);
}

TEST_CASE("challenge service: a run waits for the connection, then goes with its format 4 replay") {
    Rig rig("send");
    int64_t start = todayStart();
    rig.server->current = challengeJson(start);
    rig.server->serverNow = ChallengeService::localNowMs();
    CHECK(rig.login());
    auto c = DailyChallenge::fromJson(rig.server->current);
    ReplayData rep = sampleReplay(*c);
    // Offline when the run ends: kept, « à l'envoi ».
    rig.server->down = true;
    auto r = rig.challenges.recordRun(*c, 70000, 0, 0, c->level.hash, rep.attemptId, &rep, true);
    CHECK(r.state == ChallengeRunState::Waiting);
    rig.pump([&] { return false; }, 300);
    CHECK(rig.challenges.waitingCount() == 1);
    CHECK(rig.challenges.submission(rep.attemptId)->state == ChallengeRunState::Waiting);
    // Back online: declared, then its replay uploaded; the server's decision is kept.
    rig.server->down = false;
    rig.online.checkConnection();
    CHECK(rig.pump([&] { return rig.challenges.submission(rep.attemptId)->state == ChallengeRunState::Verified; }, 60000));
    {
        std::lock_guard<std::mutex> lock(rig.server->m);
        CHECK(rig.server->posts == 1);
        CHECK(rig.server->uploads == 1);
        CHECK(rig.server->lastBody["rules_hash"] == c->rulesHash);
        CHECK(rig.server->lastBody["attempt_id"] == rep.attemptId);
        CHECK(rig.server->lastBody["damage_taken"] == 0);
        auto up = ReplayData::decode(rig.server->lastReplay);
        CHECK(up.has_value());
        CHECK(up->challengeRules == c->rules.canonical());
    }
    CHECK(rig.challenges.submission(rep.attemptId)->serverTier == ChallengeTier::Gold);
    // The server's streak once read.
    bool done = false;
    rig.challenges.refreshMine([&](const ApiResult&) { done = true; });
    CHECK(rig.pump([&] { return done; }));
    CHECK(rig.challenges.streak().fromServer);
    CHECK(rig.challenges.streak().current == 4);
}

TEST_CASE("challenge service: a run refused by the server is not sent again") {
    Rig rig("refused");
    rig.server->current = challengeJson(todayStart());
    rig.server->endedCode = "CHALLENGE_ENDED";
    CHECK(rig.login());
    auto c = DailyChallenge::fromJson(rig.server->current);
    ReplayData rep = sampleReplay(*c);
    rig.challenges.recordRun(*c, 70000, 0, 0, c->level.hash, rep.attemptId, &rep, true);
    CHECK(rig.pump([&] { return rig.challenges.submission(rep.attemptId)->state == ChallengeRunState::Rejected; }, 10000));
    CHECK(!rig.challenges.submission(rep.attemptId)->note.empty());
    rig.pump([&] { return false; }, 300);
    std::lock_guard<std::mutex> lock(rig.server->m);
    CHECK(rig.server->posts == 1);
}
