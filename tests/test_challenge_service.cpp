// Daily challenges, the online side of the game (game/online/DailyChallenges.h): reading the
// challenge (never trusted blindly), the server's clock, the cache that keeps it playable
// offline until its end, the local results and streak, and the runs waiting for the
// connection (sent with their format 4 replay once it is there), against a scripted server
// on the real network thread.

#include <algorithm>
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

/// A challenge of `kind` for `period` ("2026-11-16", "2026-W47", "2026-11"), as a 2.33 server
/// writes it.
Json periodJson(ChallengeKind kind, const std::string& period, const std::string& level = "glacior_02") {
    int64_t first = *challenge_period::firstDay(kind, period);
    int64_t next = kind == ChallengeKind::Daily    ? first + 1
                   : kind == ChallengeKind::Weekly ? first + 7
                                                   : *challenge_period::firstDay(kind, challenge_period::addMonths(period, 1));
    std::string id = std::string(challengeKindId(kind)) + "-" + period;
    Json rules = {{"hearts", 3}, {"slots", 2}, {"powers", {"dash"}}, {"stations", true}, {"checkpoints", true}};
    auto r = ChallengeRules::fromJson(id, rules);
    return {{"id", id},
            {"kind", challengeKindId(kind)},
            {"period", period},
            {"date", challenge_period::dateOfDay(first)},
            {"starts_at", iso(first * 86400000LL)},
            {"ends_at", iso(next * 86400000LL)},
            {"source", "auto"},
            {"level", {{"source", "campaign"}, {"level_id", level}, {"level_version", 8}, {"level_hash", std::string(64, 'b')}, {"title", "Grottes"}, {"author", nullptr}}},
            {"rules", rules},
            {"rules_hash", r->hash()},
            {"tiers", {{"silver_ms", 185000}, {"gold_ms", 172000}, {"gold_no_damage", false}}}};
}

int64_t todayNumber() { return ChallengeService::localNowMs() / 86400000LL; }

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
    bool legacy = false;    // a 2.32 server: no /challenges/current, no calendar, no streaks per kind
    int64_t serverNow = 0;  // the server's clock (ms)
    Json current;           // the day's
    Json weekly, monthly;   // 2.33
    std::string endedCode;  // POST runs answered with this error ("" = accepted)
    bool lateRuns = false;  // POST runs: the run is « hors délai »
    int posts = 0, uploads = 0;
    // « Plus loin » (2.34): the attempts declared and their replays.
    int attemptPosts = 0, attemptUploads = 0;
    Json lastAttempt;
    std::vector<uint8_t> lastAttemptReplay;
    double bestProgress = -1.0;  // /challenges/me: my best attempt on `current` (no finished run)
    Json verified;               // /challenges/me (2.34): the rewards' counts (null: a 2.33 server)
    std::string playerId = "11111111-1111-4111-8111-111111111111";  // the account that logs in
    std::vector<std::string> paths;  // every GET, in order
    Json lastBody;
    std::vector<uint8_t> lastReplay;
    // The calendar (2.33) and the list of the days' challenges (2.32), newest first.
    Json calendar = Json::object();
    Json days = Json::array();
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
        if (rq.method == "GET") paths.push_back(path);
        if (path == "/api/v1/health") return okData({{"status", "ok"}});
        if (path == "/api/v1/auth/login")
            return okData({{"player", {{"id", playerId}, {"username", "Nova"}, {"country_code", "FR"}}},
                           {"session", {{"access_token", access}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                                        {"refresh_token", "refresh-token-00000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}}}});
        if (path == "/api/v1/challenges/current" && !legacy)
            return okData({{"server_time", iso(serverNow)},
                           {"daily", current},
                           {"weekly", weekly},
                           {"monthly", monthly},
                           {"previous", {{"daily", nullptr}, {"weekly", nullptr}, {"monthly", nullptr}}}});
        if (path == "/api/v1/challenges/daily/current") return okData({{"server_time", iso(serverNow)}, {"challenge", current}, {"previous", nullptr}});
        if (path.rfind("/api/v1/challenges/calendar?month=", 0) == 0 && !legacy) {
            Json c = calendar;
            c["month"] = path.substr(path.find('=') + 1);
            return okData(c);
        }
        if (path.rfind("/api/v1/challenges/daily?", 0) == 0) {
            // before=YYYY-MM-DD&limit=N: the days strictly before, newest first.
            std::string before = path.substr(path.find("before=") + 7, 10);
            int limit = std::atoi(path.c_str() + path.find("limit=") + 6);
            Json out = Json::array();
            for (const Json& d : days)
                if (d["date"].get<std::string>() < before && static_cast<int>(out.size()) < limit) out.push_back(d);
            return okData({{"challenges", out}, {"next_before", out.empty() ? Json() : out.back()["date"]}});
        }
        if (path.rfind("/api/v1/challenges/", 0) == 0 && path.find("/runs") != std::string::npos && rq.method == "POST") {
            ++posts;
            lastBody = Json::parse(rq.body);
            if (!endedCode.empty()) return apiError(409, endedCode);
            return okData({{"duplicate", false},
                           {"run", {{"id", "run-1"}, {"status", "PENDING"}, {"tier", "gold"}, {"time_ms", lastBody["time_ms"]}, {"late", lateRuns}}},
                           {"replay", {{"required", true}, {"accepted", true}, {"upload_path", "/api/v1/challenges/runs/run-1/replay"}}}},
                          201);
        }
        if (path.rfind("/api/v1/challenges/", 0) == 0 && path.find("/attempts") != std::string::npos && rq.method == "POST") {
            ++attemptPosts;
            lastAttempt = Json::parse(rq.body);
            std::string id = lastAttempt["attempt_id"];
            return okData({{"duplicate", false},
                           {"attempt", {{"attempt_id", id}, {"status", "PENDING"}, {"progress", nullptr}}},
                           {"replay", {{"required", true}, {"upload_path", "/api/v1/challenges/attempts/" + id + "/replay"}}}},
                          201);
        }
        if (path.rfind("/api/v1/challenges/attempts/", 0) == 0 && rq.method == "PUT") {
            ++attemptUploads;
            lastAttemptReplay.assign(rq.body.begin(), rq.body.end());
            auto rep = ReplayData::decode(lastAttemptReplay);
            if (!rep || rep->challengeRules.empty()) return apiError(422, "REPLAY_MALFORMED");
            return okData({{"attempt", {{"attempt_id", rep->attemptId}, {"status", "VERIFIED"}, {"progress", 40.9}, {"finished", false}, {"late", false}, {"rank", 3}, {"reasons", Json::array()}}},
                           {"new_best", true},
                           {"best_progress", 40.9}});
        }
        if (path == "/api/v1/challenges/runs/run-1/replay" && rq.method == "PUT") {
            ++uploads;
            lastReplay.assign(rq.body.begin(), rq.body.end());
            auto rep = ReplayData::decode(lastReplay);
            if (!rep || rep->challengeRules.empty()) return apiError(422, "REPLAY_MALFORMED");
            return okData({{"run", {{"id", "run-1"}, {"status", "VERIFIED"}, {"tier", "gold"}, {"late", lateRuns}}}});
        }
        if (path.rfind("/api/v1/challenges/me", 0) == 0) {
            // (Read also after a run just verified, 2.34: `current` may be unset in a test.)
            const std::string cid = jstr(current, "id"), cdate = jstr(current, "date");
            Json data = {{"streak", {{"current", 4}, {"best", 6}, {"last_date", ChallengeService::utcDate(serverNow)}}},
                         {"results", Json::array({{{"challenge_id", cid}, {"date", cdate},
                                                   {"best_time_ms", 70000}, {"tier", "gold"}, {"status", "VERIFIED"}}})}};
            if (bestProgress >= 0.0)
                data["results"] = Json::array({{{"challenge_id", cid}, {"date", cdate}, {"best_time_ms", nullptr},
                                                {"tier", nullptr}, {"status", nullptr}, {"late", false}, {"best_progress", bestProgress}}});
            if (verified.is_object()) data["verified"] = verified;
            if (!legacy && weekly.is_object())
                data["streaks"] = {{"daily", {{"current", 4}, {"best", 6}, {"last_period", ChallengeService::utcDate(serverNow)}}},
                                   {"weekly", {{"current", 2}, {"best", 3}, {"last_period", weekly["period"]}}},
                                   {"monthly", nullptr}};
            return okData(data);
        }
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
    // My result of the day as the server knows it (also a run made on another computer).
    auto sr = rig.challenges.serverResult(c->id);
    CHECK(sr.has_value());
    CHECK(sr->bestMs == 70000);
    CHECK(sr->tier == ChallengeTier::Gold);
    CHECK(sr->status == ChallengeRunState::Verified);
    // Another account: nothing of it.
    rig.online.logout(false);
    CHECK(rig.pump([&] { return !rig.online.loggedIn(); }));
    CHECK(!rig.challenges.serverResult(c->id).has_value());
    CHECK(!rig.challenges.streak().fromServer);
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

// ------------------------------------------------------------------ 2.33: weeks, months, calendar, « hors délai »

TEST_CASE("challenge service: periods of the three kinds (ISO weeks, months)") {
    using namespace challenge_period;
    CHECK(ofDay(ChallengeKind::Weekly, *dayNumber("2026-11-16")) == "2026-W47");  // a Monday
    CHECK(ofDay(ChallengeKind::Weekly, *dayNumber("2026-11-22")) == "2026-W47");  // its Sunday
    CHECK(ofDay(ChallengeKind::Weekly, *dayNumber("2026-01-01")) == "2026-W01");  // a Thursday
    CHECK(ofDay(ChallengeKind::Weekly, *dayNumber("2027-01-01")) == "2026-W53");  // a Friday: the year before
    CHECK(ofDay(ChallengeKind::Weekly, *dayNumber("2024-12-30")) == "2025-W01");  // a Monday: the year after
    CHECK(ofDay(ChallengeKind::Monthly, *dayNumber("2026-11-30")) == "2026-11");
    CHECK(valid(ChallengeKind::Weekly, "2026-W53"));
    CHECK(!valid(ChallengeKind::Weekly, "2025-W53"));  // 2025 has 52 weeks
    CHECK(!valid(ChallengeKind::Weekly, "2026-47"));
    CHECK(!valid(ChallengeKind::Daily, "2026-02-30"));
    CHECK(!valid(ChallengeKind::Monthly, "2026-13"));
    CHECK(dateOfDay(*firstDay(ChallengeKind::Weekly, "2026-W47")) == "2026-11-16");
    CHECK(dateOfDay(*firstDay(ChallengeKind::Weekly, "2025-W01")) == "2024-12-30");
    CHECK(*index(ChallengeKind::Weekly, "2026-W53") + 1 == *index(ChallengeKind::Weekly, "2027-W01"));
    CHECK(*index(ChallengeKind::Monthly, "2026-12") + 1 == *index(ChallengeKind::Monthly, "2027-01"));
    CHECK(*index(ChallengeKind::Daily, "2026-03-01") == *index(ChallengeKind::Daily, "2026-02-28") + 1);
    CHECK(addMonths("2026-12", 1) == "2027-01");
    CHECK(addMonths("2026-01", -1) == "2025-12");
    CHECK(daysInMonth("2028-02") == 29);
    CHECK(daysInMonth("2026-11") == 30);
    CHECK(weekday(*dayNumber("2026-11-16")) == 0);
    CHECK(weekday(*dayNumber("2026-11-15")) == 6);
}

TEST_CASE("challenge service: a week's and a month's challenge are read strictly, a 2.32 one is a day's") {
    std::string err;
    auto w = DailyChallenge::fromJson(periodJson(ChallengeKind::Weekly, "2026-W47"), &err);
    CHECK(w.has_value());
    CHECK(w->kind == ChallengeKind::Weekly);
    CHECK(w->period == "2026-W47");
    CHECK(w->date == "2026-11-16");
    CHECK(w->endsAtMs - w->startsAtMs == 7 * 86400000LL);
    // The canonical rules carry the challenge's id, whatever its kind.
    CHECK(w->rules.canonical().rfind("{\"challenge\":\"weekly-2026-W47\"", 0) == 0);
    auto mo = DailyChallenge::fromJson(periodJson(ChallengeKind::Monthly, "2026-02"));
    CHECK(mo.has_value());
    CHECK(mo->kind == ChallengeKind::Monthly);
    CHECK(mo->endsAtMs - mo->startsAtMs == 28 * 86400000LL);
    // A 2.32 challenge (no kind, no period): the day's.
    Json old = challengeJson(*challenge_period::dayNumber("2026-11-16") * 86400000LL);
    old.erase("kind");
    auto d = DailyChallenge::fromJson(old);
    CHECK(d.has_value());
    CHECK(d->kind == ChallengeKind::Daily);
    CHECK(d->period == "2026-11-16");
    // Refused: an unknown kind, an id that is not « kind-period », dates that are not the period's.
    Json bad = periodJson(ChallengeKind::Weekly, "2026-W47");
    bad["kind"] = "yearly";
    CHECK(!DailyChallenge::fromJson(bad).has_value());
    bad = periodJson(ChallengeKind::Weekly, "2026-W47");
    bad["period"] = "2026-W48";
    CHECK(!DailyChallenge::fromJson(bad).has_value());
    bad = periodJson(ChallengeKind::Weekly, "2026-W47");
    bad["ends_at"] = iso((*challenge_period::dayNumber("2026-11-24")) * 86400000LL);
    CHECK(!DailyChallenge::fromJson(bad).has_value());
    bad = periodJson(ChallengeKind::Monthly, "2026-11");
    bad["id"] = "monthly-2026-12";
    CHECK(!DailyChallenge::fromJson(bad).has_value());
    bad = periodJson(ChallengeKind::Weekly, "2026-W47");
    bad["kind"] = "daily";
    CHECK(!DailyChallenge::fromJson(bad).has_value());
}

TEST_CASE("challenge service: /challenges/current gives the three kinds; a 2.32 server falls back to the day's route") {
    const int64_t today = todayNumber();
    {
        Rig rig("current3");
        rig.server->serverNow = ChallengeService::localNowMs();
        rig.server->current = periodJson(ChallengeKind::Daily, challenge_period::ofDay(ChallengeKind::Daily, today));
        rig.server->weekly = periodJson(ChallengeKind::Weekly, challenge_period::ofDay(ChallengeKind::Weekly, today));
        rig.server->monthly = periodJson(ChallengeKind::Monthly, challenge_period::ofDay(ChallengeKind::Monthly, today));
        rig.challenges.refresh(true);
        CHECK(rig.pump([&] { return rig.challenges.fetchState() == ChallengeService::Fetch::Ok; }));
        CHECK(!rig.challenges.legacyServer());
        CHECK(rig.challenges.current(ChallengeKind::Daily) && rig.challenges.current(ChallengeKind::Daily)->kind == ChallengeKind::Daily);
        CHECK(rig.challenges.current(ChallengeKind::Weekly) && rig.challenges.current(ChallengeKind::Weekly)->id == rig.server->weekly["id"].get<std::string>());
        CHECK(rig.challenges.current(ChallengeKind::Monthly) && rig.challenges.current(ChallengeKind::Monthly)->id == rig.server->monthly["id"].get<std::string>());
        std::lock_guard<std::mutex> lock(rig.server->m);
        CHECK(std::find(rig.server->paths.begin(), rig.server->paths.end(), "/api/v1/challenges/daily/current") == rig.server->paths.end());
    }
    {
        // Offline afterwards: each kind stays playable from the cache until its end.
        Rig rig("current3off");
        rig.server->down = true;
        rig.server->current = periodJson(ChallengeKind::Daily, challenge_period::ofDay(ChallengeKind::Daily, today));
        rig.challenges.note(periodJson(ChallengeKind::Weekly, challenge_period::ofDay(ChallengeKind::Weekly, today)));
        rig.challenges.note(periodJson(ChallengeKind::Monthly, challenge_period::ofDay(ChallengeKind::Monthly, today)));
        ChallengeService again;
        again.init(rig.online, rig.dir / "saves" / "challenges");
        CHECK(again.current(ChallengeKind::Weekly) != nullptr);
        CHECK(again.current(ChallengeKind::Monthly) != nullptr);
        CHECK(again.current(ChallengeKind::Daily) == nullptr);
        // Past the end of the week: no longer the current one.
        int64_t endOfWeek = again.current(ChallengeKind::Weekly)->endsAtMs;
        again.setServerOffsetForTests(endOfWeek + 1000 - ChallengeService::localNowMs());
        CHECK(again.current(ChallengeKind::Weekly) == nullptr);
    }
    {
        // A server still at 2.32: 404 on /challenges/current, its route of the day instead.
        Rig rig("legacy");
        rig.server->legacy = true;
        rig.server->serverNow = ChallengeService::localNowMs();
        rig.server->current = challengeJson(today * 86400000LL);
        rig.challenges.refresh(true);
        CHECK(rig.pump([&] { return rig.challenges.fetchState() == ChallengeService::Fetch::Ok; }));
        CHECK(rig.challenges.legacyServer());
        CHECK(rig.challenges.current(ChallengeKind::Daily) != nullptr);
        CHECK(rig.challenges.current(ChallengeKind::Weekly) == nullptr);
        std::lock_guard<std::mutex> lock(rig.server->m);
        CHECK(rig.server->paths.size() >= 2);
        CHECK(rig.server->paths[0] == "/api/v1/challenges/current");
        CHECK(rig.server->paths[1] == "/api/v1/challenges/daily/current");
    }
}

TEST_CASE("challenge service: the calendar of a month, its first month and my results; a 2.32 server gives its days") {
    {
        Rig rig("calendar");
        CHECK(rig.login());
        Json list = Json::array();
        list.push_back(periodJson(ChallengeKind::Daily, "2025-03-04"));
        list.push_back(periodJson(ChallengeKind::Daily, "2025-03-05", "sylvanox_01"));
        list.push_back(periodJson(ChallengeKind::Weekly, "2025-W10"));
        list.push_back(periodJson(ChallengeKind::Weekly, "2025-W09"));  // Feb 24 → Mar 3: overlaps March
        list.push_back(periodJson(ChallengeKind::Monthly, "2025-03"));
        rig.server->calendar = {{"first_month", "2025-01"},
                                {"challenges", list},
                                {"me", {{"daily-2025-03-04", {{"best_time_ms", 81234}, {"tier", "silver"}, {"status", "VERIFIED"}, {"late", true}, {"best_progress", 100}}},
                                        {"weekly-2025-W10", {{"best_time_ms", 0}, {"tier", nullptr}, {"status", nullptr}, {"late", false}, {"best_progress", 63.4}}}}}};
        rig.challenges.loadCalendar("2025-03");
        using S = ChallengeService::CalendarMonth::State;
        CHECK(rig.pump([&] { return rig.challenges.calendar("2025-03").state == S::Ok; }));
        CHECK(rig.challenges.firstMonth() == "2025-01");
        auto month = rig.challenges.challengesOfMonth("2025-03");
        CHECK(month.size() == 5);
        CHECK(month.front()->id == "weekly-2025-W09");  // by start
        CHECK(rig.challenges.challengesOfMonth("2025-02").size() == 1);  // the week that overlaps
        auto me = rig.challenges.serverResult("daily-2025-03-04");
        CHECK(me.has_value());
        CHECK(me->bestMs == 81234 && me->tier == ChallengeTier::Silver && me->status == ChallengeRunState::Verified && me->late);
        auto wk = rig.challenges.serverResult("weekly-2025-W10");
        CHECK(wk.has_value());
        CHECK_NEAR(wk->bestProgress, 63.4f, 0.01f);
        // The first month is kept (the calendar never goes before it, offline either).
        ChallengeService again;
        again.init(rig.online, rig.dir / "saves" / "challenges");
        CHECK(again.firstMonth() == "2025-01");
        // Offline: the last calendar received, said so.
        rig.server->down = true;
        rig.challenges.loadCalendar("2025-03", true);
        CHECK(rig.pump([&] { return rig.challenges.calendar("2025-03").state == S::Cached; }));
    }
    {
        Rig rig("calendar232");
        rig.server->legacy = true;
        // 2025-02-20 … 2025-03-31, newest first.
        for (int64_t d = *challenge_period::dayNumber("2025-03-31"); d >= *challenge_period::dayNumber("2025-02-20"); --d)
            rig.server->days.push_back(challengeJson(d * 86400000LL));
        using S = ChallengeService::CalendarMonth::State;
        rig.challenges.loadCalendar("2025-03");
        CHECK(rig.pump([&] { return rig.challenges.calendar("2025-03").state == S::Ok; }));
        CHECK(rig.challenges.legacyServer());
        // The 31 days of March (two pages), and February goes on: no first month yet.
        int days = 0;
        for (const DailyChallenge* c : rig.challenges.challengesOfMonth("2025-03")) days += c->kind == ChallengeKind::Daily ? 1 : 0;
        CHECK(days == 31);
        CHECK(rig.challenges.firstMonth().empty());
        rig.challenges.loadCalendar("2025-02");
        CHECK(rig.pump([&] { return rig.challenges.calendar("2025-02").state == S::Ok; }));
        CHECK(rig.challenges.firstMonth() == "2025-02");
        std::lock_guard<std::mutex> lock(rig.server->m);
        CHECK(rig.server->paths.front() == "/api/v1/challenges/calendar?month=2025-03");
    }
}

TEST_CASE("challenge service: a challenge that is over is played « hors délai »: kept, sent, never in the streak") {
    Rig rig("late");
    rig.server->serverNow = ChallengeService::localNowMs();
    rig.server->lateRuns = true;
    CHECK(rig.login());
    rig.challenges.setServerOffsetForTests(0);
    const int64_t today = todayNumber();
    auto yesterday = DailyChallenge::fromJson(periodJson(ChallengeKind::Daily, challenge_period::dateOfDay(today - 2)));
    CHECK(yesterday.has_value());
    CHECK(rig.challenges.ended(*yesterday));
    CHECK(rig.challenges.lateNow(*yesterday));
    ReplayData rep = sampleReplay(*yesterday);
    auto r = rig.challenges.recordRun(*yesterday, 90000, 0, 0, yesterday->level.hash, rep.attemptId, &rep, true);
    CHECK(r.late);
    CHECK(r.newBest);
    CHECK(r.state == ChallengeRunState::Waiting);
    const ChallengeResult* res = rig.challenges.result(yesterday->id);
    CHECK(res && res->bestLate && res->finishes == 1 && res->liveFinishes == 0);
    // Sent like the others (a 2.33 server takes it, « Depuis » only), the server's flag kept.
    CHECK(rig.pump([&] { return rig.challenges.submission(rep.attemptId)->state == ChallengeRunState::Verified; }, 60000));
    CHECK(rig.challenges.submission(rep.attemptId)->late);
    {
        std::lock_guard<std::mutex> lock(rig.server->m);
        CHECK(rig.server->posts == 1);
    }
    // Never in the streak.
    CHECK(rig.challenges.localStreak(ChallengeKind::Daily).current == 0);
    // The leaderboard of a challenge that is over: « Pendant le défi » (default) or « Depuis ».
    CHECK(ChallengeService::leaderboardPath(yesterday->id, "world", false).find("period=") == std::string::npos);
    CHECK(ChallengeService::leaderboardPath(yesterday->id, "world", true).find("&period=all") != std::string::npos);
    // A 2.32 server refuses it (CHALLENGE_ENDED): refused, said why, not sent again.
    rig.server->endedCode = "CHALLENGE_ENDED";
    auto other = DailyChallenge::fromJson(periodJson(ChallengeKind::Daily, challenge_period::dateOfDay(today - 3)));
    ReplayData rep2 = sampleReplay(*other);
    rig.challenges.recordRun(*other, 90000, 0, 0, other->level.hash, rep2.attemptId, &rep2, true);
    CHECK(rig.pump([&] { return rig.challenges.submission(rep2.attemptId)->state == ChallengeRunState::Rejected; }, 10000));
    CHECK(!rig.challenges.submission(rep2.attemptId)->note.empty());
}

TEST_CASE("challenge service: a streak per kind (weeks and months in a row), the server's when read") {
    Rig rig("streaks");
    rig.server->down = true;
    const int64_t today = todayNumber();
    const int64_t monday = today - challenge_period::weekday(today);
    auto lastWeek = DailyChallenge::fromJson(periodJson(ChallengeKind::Weekly, challenge_period::ofDay(ChallengeKind::Weekly, monday - 7)));
    auto thisWeek = DailyChallenge::fromJson(periodJson(ChallengeKind::Weekly, challenge_period::ofDay(ChallengeKind::Weekly, monday)));
    CHECK(lastWeek && thisWeek);
    // Last week's challenge finished while it ran (the clock a week back), then this week's.
    const int64_t local = ChallengeService::localNowMs();
    rig.challenges.setServerOffsetForTests(-7 * 86400000LL);
    CHECK(!rig.challenges.ended(*lastWeek));
    rig.challenges.recordRun(*lastWeek, 99000, 0, 0, lastWeek->level.hash, makeUuidV4(), nullptr, true);
    rig.challenges.setServerOffsetForTests(0);
    CHECK(rig.challenges.localStreak(ChallengeKind::Weekly).current == 1);  // last week's still runs
    rig.challenges.recordRun(*thisWeek, 98000, 0, 0, thisWeek->level.hash, makeUuidV4(), nullptr, true);
    ChallengeService::Streak s = rig.challenges.localStreak(ChallengeKind::Weekly);
    CHECK(s.current == 2);
    CHECK(s.best == 2);
    CHECK(s.lastPeriod == thisWeek->period);
    CHECK(rig.challenges.localStreak(ChallengeKind::Daily).current == 0);  // the kinds are apart
    CHECK(rig.challenges.localStreak(ChallengeKind::Monthly).current == 0);
    // Two weeks later: the streak is broken (its best stays).
    rig.challenges.setServerOffsetForTests(14 * 86400000LL);
    CHECK(rig.challenges.localStreak(ChallengeKind::Weekly).current == 0);
    CHECK(rig.challenges.localStreak(ChallengeKind::Weekly).best == 2);
    rig.challenges.setServerOffsetForTests(0);
    (void)local;
    // Read again from the disk: kind and period kept.
    ChallengeService again;
    again.init(rig.online, rig.dir / "saves" / "challenges");
    again.setServerOffsetForTests(0);
    CHECK(again.result(thisWeek->id) && again.result(thisWeek->id)->kind == ChallengeKind::Weekly);
    CHECK(again.localStreak(ChallengeKind::Weekly).current == 2);
    // The server's streaks per kind (/challenges/me).
    rig.server->down = false;
    rig.server->serverNow = ChallengeService::localNowMs();
    rig.server->current = periodJson(ChallengeKind::Daily, challenge_period::dateOfDay(today));
    rig.server->weekly = periodJson(ChallengeKind::Weekly, thisWeek->period);
    rig.online.checkConnection();
    CHECK(rig.login());
    bool done = false;
    rig.challenges.refreshMine([&](const ApiResult&) { done = true; });
    CHECK(rig.pump([&] { return done; }));
    ChallengeService::Streak w = rig.challenges.streak(ChallengeKind::Weekly);
    CHECK(w.fromServer);
    CHECK(w.best == 3);
    CHECK(rig.challenges.streak(ChallengeKind::Daily).fromServer);
    CHECK(!rig.challenges.streak(ChallengeKind::Monthly).fromServer);  // null: the local one
}

// ------------------------------------------------------------------ 2.34: « Plus loin »

TEST_CASE("challenge service: « Plus loin »: an attempt is sent only when it beats my best one") {
    // One decimal, strictly better; nothing, or a finish, never.
    CHECK(ChallengeService::beatsBestAttempt(63.5f, 63.4f));
    CHECK(!ChallengeService::beatsBestAttempt(63.4f, 63.4f));
    CHECK(!ChallengeService::beatsBestAttempt(63.44f, 63.4f));
    CHECK(!ChallengeService::beatsBestAttempt(0.0f, 0.0f));
    CHECK(ChallengeService::beatsBestAttempt(0.1f, 0.0f));
    CHECK(!ChallengeService::beatsBestAttempt(99.9f, 100.0f));
    // progress_available is read; absent (2.33 server): no « Plus loin ».
    Json j = periodJson(ChallengeKind::Weekly, "2026-W47");
    CHECK(!DailyChallenge::fromJson(j)->progressAvailable);
    j["progress_available"] = true;
    CHECK(DailyChallenge::fromJson(j)->progressAvailable);
    CHECK(ChallengeService::leaderboardPath("weekly-2026-W47", "world", false, true).find("&board=progress") != std::string::npos);
    CHECK(ChallengeService::leaderboardPath("weekly-2026-W47", "world", false, false).find("board=") == std::string::npos);
}

TEST_CASE("challenge service: « Plus loin »: the best unfinished attempt goes with its claim and its format 4 replay") {
    Rig rig("attempts");
    rig.server->serverNow = ChallengeService::localNowMs();
    CHECK(rig.login());
    const int64_t today = todayNumber();
    Json cj = periodJson(ChallengeKind::Weekly, challenge_period::ofDay(ChallengeKind::Weekly, today));
    cj["progress_available"] = true;
    auto c = DailyChallenge::fromJson(cj);
    CHECK(c.has_value());
    ReplayData rep = sampleReplay(*c);
    auto a = rig.challenges.recordAttempt(*c, 41.27f, 52000, {500, 0}, {2}, c->level.hash, rep.attemptId, &rep, true);
    CHECK(a.newBest);
    CHECK_NEAR(a.progress, 41.2f, 0.001f);  // one decimal, never rounded up
    CHECK(a.state == ChallengeRunState::Waiting);
    CHECK(rig.pump([&] { return rig.challenges.submission(rep.attemptId, true)->state == ChallengeRunState::Verified; }, 60000));
    {
        std::lock_guard<std::mutex> lock(rig.server->m);
        CHECK(rig.server->attemptPosts == 1);
        CHECK(rig.server->attemptUploads == 1);
        const Json& b = rig.server->lastAttempt;
        CHECK(b["attempt_id"] == rep.attemptId);
        CHECK(b["time_ms"] == 52000);
        CHECK(b["level_hash"] == c->level.hash);
        CHECK(b["rules_hash"] == c->rulesHash);
        CHECK(b["game_version"] == COSMIC_VERSION_STRING);
        CHECK(b["claim"]["finished"] == false);
        CHECK(b["claim"]["bosses"] == Json::array({500, 0}));
        CHECK(b["claim"]["waves"] == Json::array({2}));
        CHECK(!b.contains("progress"));  // the server computes the score from the replay
        auto up = ReplayData::decode(rig.server->lastAttemptReplay);
        CHECK(up.has_value());
        CHECK(up->challengeRules == c->rules.canonical());
        CHECK(up->attemptId == rep.attemptId);
    }
    CHECK_NEAR(rig.challenges.submission(rep.attemptId, true)->serverProgress, 40.9f, 0.01f);
    CHECK_NEAR(rig.challenges.bestProgress(c->id), 41.2f, 0.001f);
    // Not further: kept nowhere, sent nowhere.
    ReplayData rep2 = sampleReplay(*c);
    auto b = rig.challenges.recordAttempt(*c, 41.2f, 60000, {500, 0}, {2}, c->level.hash, rep2.attemptId, &rep2, true);
    CHECK(!b.newBest);
    CHECK(rig.challenges.submission(rep2.attemptId, true) == nullptr);
    // The server knows a better one (another computer): that one is the best.
    {
        std::lock_guard<std::mutex> lock(rig.server->m);
        rig.server->current = cj;
        rig.server->bestProgress = 70.5;
    }
    bool done = false;
    rig.challenges.refreshMine([&](const ApiResult&) { done = true; });
    CHECK(rig.pump([&] { return done; }));
    CHECK_NEAR(rig.challenges.bestProgress(c->id), 70.5f, 0.001f);
    CHECK(!rig.challenges.recordAttempt(*c, 65.0f, 60000, {}, {}, c->level.hash, makeUuidV4(), &rep2, true).newBest);
    // Finished: 100, no attempt beats it.
    rig.challenges.recordRun(*c, 99000, 0, 0, c->level.hash, makeUuidV4(), nullptr, true);
    CHECK(rig.challenges.bestProgress(c->id) == 100.0f);
    CHECK(!rig.challenges.recordAttempt(*c, 99.9f, 60000, {}, {}, c->level.hash, makeUuidV4(), &rep2, true).newBest);
    // A challenge without route: no attempt at all.
    auto plain = DailyChallenge::fromJson(periodJson(ChallengeKind::Daily, challenge_period::dateOfDay(today)));
    auto p = rig.challenges.recordAttempt(*plain, 50.0f, 1000, {}, {}, plain->level.hash, makeUuidV4(), &rep2, true);
    CHECK(!p.newBest);
    CHECK(rig.challenges.bestProgress(plain->id) == 0.0f);
    std::lock_guard<std::mutex> lock(rig.server->m);
    CHECK(rig.server->attemptPosts == 1);
}

TEST_CASE("challenge service: « Plus loin » without an account: the best attempt stays on this computer") {
    Rig rig("attemptlocal");
    rig.server->down = true;
    Json cj = periodJson(ChallengeKind::Monthly, challenge_period::ofDay(ChallengeKind::Monthly, todayNumber()));
    cj["progress_available"] = true;
    auto c = DailyChallenge::fromJson(cj);
    auto a = rig.challenges.recordAttempt(*c, 12.3f, 9000, {}, {}, c->level.hash, makeUuidV4(), nullptr, true);
    CHECK(a.newBest);
    CHECK(a.state == ChallengeRunState::LocalOnly);
    CHECK(!a.note.empty());
    CHECK(rig.challenges.waitingCount() == 0);
    ChallengeService again;
    again.init(rig.online, rig.dir / "saves" / "challenges");
    CHECK_NEAR(again.bestProgress(c->id), 12.3f, 0.001f);
}

// ------------------------------------------------------------------ 2.34: rewards of the challenges

namespace {

Json verifiedJson(int dailyGold, int weeklyGold, int dailyStreak) {
    return {{"daily", {{"gold", dailyGold}, {"silver", dailyGold + 1}, {"bronze", dailyGold + 3}, {"best_streak", dailyStreak}}},
            {"weekly", {{"gold", weeklyGold}, {"silver", weeklyGold}, {"bronze", weeklyGold}, {"best_streak", weeklyGold}}},
            {"monthly", {{"gold", 0}, {"silver", 0}, {"bronze", 0}, {"best_streak", 0}}}};
}

}  // namespace

TEST_CASE("challenge service: the rewards of the challenges count the server's verified results only, kept for offline play") {
    Rig rig("rewards");
    rig.server->current = challengeJson(todayStart());
    rig.server->serverNow = ChallengeService::localNowMs();
    int changes = 0;
    rig.challenges.onRewardsChanged = [&] { ++changes; };
    // A gold run kept on this computer only counts for nothing.
    auto c = DailyChallenge::fromJson(rig.server->current);
    rig.challenges.recordRun(*c, 70000, 0, 0, c->level.hash, makeUuidV4(), nullptr, false);
    CHECK(!rig.challenges.rewardProgress().known);
    // The server's counts (`verified`), read with my results.
    rig.server->verified = verifiedJson(2, 1, 4);
    CHECK(rig.login());
    auto readMine = [&] {
        bool done = false;
        rig.challenges.refreshMine([&](const ApiResult&) { done = true; });
        CHECK(rig.pump([&] { return done; }));
    };
    readMine();
    const ChallengeRewardProgress& p = rig.challenges.rewardProgress();
    CHECK(p.known);
    CHECK(p.results("daily", 3) == 2);
    CHECK(p.results("any", 3) == 3);
    CHECK(p.results("any", 2) == 4);
    CHECK(p.results("weekly", 1) == 1);
    CHECK(p.results("monthly", 3) == 0);
    CHECK(p.streak("daily") == 4);
    CHECK(changes == 1);
    const int revision = rig.challenges.rewardsRevision();
    // The same counts again: nothing new to announce.
    readMine();
    CHECK(changes == 1);
    CHECK(rig.challenges.rewardsRevision() == revision);
    // One more gold on a week: announced.
    rig.server->verified = verifiedJson(2, 2, 4);
    readMine();
    CHECK(changes == 2);
    CHECK(rig.challenges.rewardProgress().results("weekly", 3) == 2);
    // Kept on this computer: a new start knows them before any reading (offline play).
    {
        ChallengeService again;
        again.init(rig.online, rig.dir / "saves" / "challenges");
        CHECK(again.rewardProgress().known);
        CHECK(again.rewardProgress().results("weekly", 3) == 2);
        CHECK(again.rewardProgress().streak("daily") == 4);
    }
    // Another account logs in: the first one's counts are not its own.
    rig.online.logout(false);
    CHECK(rig.pump([&] { return !rig.online.loggedIn(); }));
    rig.server->playerId = "22222222-2222-4222-8222-222222222222";
    rig.server->verified = Json();
    CHECK(rig.login());
    CHECK(!rig.challenges.rewardProgress().known);
    // A 2.33 server (no `verified`): its results whose status is VERIFIED.
    readMine();
    CHECK(rig.challenges.rewardProgress().known);
    CHECK(rig.challenges.rewardProgress().results("daily", 3) == 1);
    CHECK(rig.challenges.rewardProgress().streak("daily") == 1);
}

TEST_CASE("challenge service: a run just verified reads the rewards again") {
    Rig rig("rewardsrun");
    rig.server->current = challengeJson(todayStart());
    rig.server->serverNow = ChallengeService::localNowMs();
    rig.server->verified = verifiedJson(1, 0, 1);
    CHECK(rig.login());
    auto c = DailyChallenge::fromJson(rig.server->current);
    ReplayData rep = sampleReplay(*c);
    rig.challenges.recordRun(*c, 70000, 0, 0, c->level.hash, rep.attemptId, &rep, true);
    // Sent, its replay verified by the server: /challenges/me follows by itself.
    CHECK(rig.pump([&] { return rig.challenges.rewardProgress().known; }, 60000));
    CHECK(rig.challenges.submission(rep.attemptId)->state == ChallengeRunState::Verified);
    CHECK(rig.challenges.rewardProgress().results("any", 3) == 1);
}
