// 2.50 « Migrer vers un nouveau compte »: the game against the server's own shapes
// (server/src/accountMigration.ts, devices.ts): whether the server offers it (asked once per
// account, an older server answering 404), the progression to restore (its download checked,
// added to this computer's saves once, the saves kept before), the admin's preview, and the
// requests the game makes around it (an expiring session renewed before a private download,
// the achievements added while a migrated progression waits).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
#include "game/online/AccountMigration.h"
#include "game/online/OnlineService.h"
#include "game/online/ProgressMerge.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/PlayStats.h"
#include "game/records/Records.h"
#include "packaging/Sha256.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path tempDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() /
                 ("cosmic_migration_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

HttpResponse apiError(int status, const std::string& code, const std::string& message) {
    return jsonResponse(status, {{"ok", false}, {"error", {{"code", code}, {"message", message}}}, {"request_id", "t"}});
}

/// The restore of GET /players/me/progress-restore exactly as restoreJson() writes it.
Json serverRestore() {
    return {{"from_username", "Orion"},
            {"created_at", "2026-11-16T10:00:00.000Z"},
            {"postponed_at", nullptr},
            {"copy",
             {{"created_at", "2026-11-15T20:00:00.000Z"},
              {"game_version", "2.50.0"},
              {"size", 1234},
              {"device_name", "PC du salon"},
              {"summary", {{"levels", 3}, {"achievements", 2}, {"stars", 5}, {"play_seconds", 600}, {"bosses", 1}}}}}};
}

/// A scripted Cosmic Online server (2.50, or older) on the network thread.
struct MigServer {
    std::mutex m;
    bool hasMigration = true;  // false: a server older than 2.50 (404 « Route inconnue. »)
    bool down = false;
    bool expiredSessions = false;  // the login's access token is already expired
    bool closed = false;           // the account was migrated to another one (closed)
    int counter = 0, migrationCalls = 0, restoreCalls = 0, refreshCalls = 0;
    std::string access = "access-0-aaaaaaaaaaaaaaaaaaaa", refresh = "refresh-0-bbbbbbbbbbbbbbbbbbbb";
    std::string probeAuth, fileAuth;
    Json restore;  // null: nothing to restore
    std::string file;
    std::vector<Json> achievementBodies;

    Json session(bool expired) {
        ++counter;
        access = "access-" + std::to_string(counter) + "-aaaaaaaaaaaaaaaaaaaa";
        refresh = "refresh-" + std::to_string(counter) + "-bbbbbbbbbbbbbbbbbbbb";
        return {{"access_token", access},
                {"access_expires_at", expired ? "2001-01-01T00:00:00.000Z" : "2099-01-01T00:00:00.000Z"},
                {"refresh_token", refresh},
                {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}};
    }
    static Json player() { return {{"id", "22222222-2222-4222-8222-222222222222"}, {"username", "OrionNew"}}; }
    std::string authOf(const HttpRequest& rq) const {
        for (const auto& [k, v] : rq.headers)
            if (k == "Authorization") return v;
        return {};
    }

    HttpResponse handle(const HttpRequest& rq) {
        std::lock_guard<std::mutex> lock(m);
        const std::string path = rq.url.substr(rq.url.find("/api/"));
        const bool probe = path == "/api/v1/players/me/migration" && rq.method == "GET";
        if (probe) ++migrationCalls;  // asked, even when the server cannot be reached
        if (down) {
            HttpResponse r;
            r.error = NetError::Connect;
            return r;
        }
        if (path == "/api/v1/health") return okData({{"status", "ok"}});
        if (path == "/api/v1/auth/login") {
            if (closed) return apiError(403, "ACCOUNT_CLOSED", "Ce compte a été migré vers un autre compte : il est fermé.");
            return okData({{"player", player()}, {"session", session(expiredSessions)}});
        }
        if (path == "/api/v1/auth/refresh") {
            ++refreshCalls;
            if (Json::parse(rq.body)["refresh_token"] != refresh) return apiError(401, "SESSION_REVOKED", "Session révoquée.");
            return okData({{"session", session(false)}});
        }
        const bool authed = authOf(rq) == "Bearer " + access;
        if (probe) {
            probeAuth = authOf(rq);
            if (!hasMigration) return apiError(404, "NOT_FOUND", "Route inconnue.");
            if (!authed) return apiError(401, "UNAUTHORIZED", "Connexion requise.");
            return okData({{"active", false}, {"expires_at", nullptr}});
        }
        if (path == "/api/v1/players/me/progress-restore" && rq.method == "GET") {
            ++restoreCalls;
            if (!authed) return apiError(401, "UNAUTHORIZED", "Connexion requise.");
            return okData({{"restore", restore}});
        }
        if (path == "/api/v1/players/me/progress-restore/file") {
            fileAuth = authOf(rq);
            if (!authed || restore.is_null()) return apiError(404, "RESTORE_NOT_FOUND", "Aucune progression à restaurer.");
            HttpResponse r;
            r.status = 200;
            r.body = file;
            r.headers["content-type"] = "application/json; charset=utf-8";
            r.headers["x-progress-sha256"] = Sha256::hex(file);
            return r;
        }
        if (path == "/api/v1/players/me/achievements" && rq.method == "PUT") {
            if (!authed) return apiError(401, "TOKEN_EXPIRED", "Session expirée.");
            achievementBodies.push_back(Json::parse(rq.body));
            return okData({{"achievements", {{"stars", 0}}}});
        }
        if (!authed) return apiError(401, "TOKEN_EXPIRED", "Session expirée.");
        if (path == "/api/v1/auth/me") return okData({{"player", player()}});
        return apiError(404, "NOT_FOUND", "Route inconnue.");
    }
};

class MigTransport final : public IHttpTransport {
public:
    explicit MigTransport(std::shared_ptr<MigServer> s) : s_(std::move(s)) {}
    HttpResponse perform(const HttpRequest& rq) override { return s_->handle(rq); }
    const char* name() const override { return "fake"; }

private:
    std::shared_ptr<MigServer> s_;
};

template <typename Pred>
bool pump(OnlineService& svc, MigrationState* state, Pred done, int timeoutMs = 3000, float dt = 0.016f) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(dt);
        if (state) state->update(svc, dt);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    svc.update(dt);
    if (state) state->update(svc, dt);
    return done();
}

struct Harness {
    std::shared_ptr<MigServer> server = std::make_shared<MigServer>();
    fs::path dir;
    OnlineService svc;
    explicit Harness(const std::string& name) : dir(tempDir(name)) { svc.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<MigTransport>(server), dir); }
    ~Harness() {
        svc.shutdown();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    bool login() {
        bool done = false, ok = false;
        svc.login("OrionNew", "good password", true, [&](const ApiResult& r) {
            done = true;
            ok = r.ok;
        });
        pump(svc, nullptr, [&] { return done; });
        return ok;
    }
};

/// The saves of a computer, made by the game's own classes (what progressCopyBody() sends).
Json savesOf(const std::vector<std::string>& levels, double playSeconds, const std::string& levelId, float bestTime, int bossKills,
             const std::vector<std::string>& achievementsUnlocked, const std::vector<std::string>& heard) {
    Progression p;
    Json pj = {{"completedLevels", levels}, {"playTimeSeconds", playSeconds}, {"levelPlayTime", {{levelId, playSeconds}}}, {"lastLevel", levelId}};
    p.fromJson(pj);
    RecordsManager rec;
    LevelRecord& r = rec.getOrCreate(levelId);
    r.attempts = 4;
    r.completions = 2;
    r.bestTime = bestTime;
    r.bestSplits = {bestTime * 0.5f};
    r.levelHash = "h1";
    BossStats bosses;
    bosses.at("astrarque").kills = bossKills;
    bosses.at("astrarque").fights = bossKills + 1;
    AchievementBook book;
    Json unlocked = Json::object();
    for (const auto& a : achievementsUnlocked) unlocked[a] = "2026-11-01 10:00";
    book.fromJson(Json{{"unlocked", unlocked}, {"counters", {{"jumps", 10}}}});
    CollectionBook col;
    col.fromJson(Json{{"heard", heard}, {"equipped", {{"suit", "suit_standard"}}}});
    return {{"progress", p.toJson()}, {"records", rec.toJson()}, {"boss_stats", bosses.toJson()}, {"achievements", book.toJson()}, {"collection", col.toJson()}};
}

/// The file the server keeps for a copy (devices.ts putProgressCopy): JSON.stringify of
/// {format, game_version, player_id, created_at, summary, files}.
std::string serverCopyFile(const Json& files) {
    Json f = {{"format", "cosmic.progress_copy"},
              {"game_version", "2.50.0"},
              {"player_id", "11111111-1111-4111-8111-111111111111"},
              {"created_at", "2026-11-15T20:00:00.000Z"},
              {"summary", {{"levels", 3}, {"achievements", 2}, {"stars", 5}, {"play_seconds", 600}, {"bosses", 1}}},
              {"files", files}};
    return f.dump();
}

}  // namespace

TEST_CASE("migration: offered by a 2.50 server, asked once per account, then the progression waiting is read") {
    Harness h("offered");
    MigrationState st;
    st.update(h.svc, 0.016f);
    CHECK(!st.available());
    CHECK(h.server->migrationCalls == 0);  // logged out: nothing asked
    h.server->restore = serverRestore();
    CHECK(h.login());
    CHECK(pump(h.svc, &st, [&] { return st.available() && st.pendingRestore().is_object(); }));
    CHECK(h.server->probeAuth == "Bearer " + h.server->access);  // asked with the session (after the login)
    CHECK(jstr(st.pendingRestore(), "from_username") == "Orion");
    CHECK(jget<int>(jfield(jfield(st.pendingRestore(), "copy", Json()), "summary", Json()), "levels", 0) == 3);
    for (int i = 0; i < 200; ++i) st.update(h.svc, 1.0f);
    CHECK(h.server->migrationCalls == 1);
    // Restored elsewhere: read again, nothing waits any more.
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->restore = Json();
    }
    bool read = false;
    st.refreshRestore(h.svc, [&] { read = true; });
    CHECK(pump(h.svc, &st, [&] { return read; }));
    CHECK(st.pendingRestore().is_null());
    // Logged out: forgotten (nothing about the migration shown).
    bool out = false;
    h.svc.logout(false, [&](const ApiResult&) { out = true; });
    CHECK(pump(h.svc, &st, [&] { return out; }));
    st.update(h.svc, 0.016f);
    CHECK(!st.available());
}

TEST_CASE("migration: a server older than 2.50 (404) never shows it, and is asked only once") {
    Harness h("older");
    h.server->hasMigration = false;
    MigrationState st;
    CHECK(h.login());
    CHECK(pump(h.svc, &st, [&] { return h.server->migrationCalls == 1; }));
    // Five simulated minutes: never asked again.
    pump(h.svc, &st, [] { return false; }, 300, 1.0f);
    CHECK(!st.available());
    CHECK(h.server->migrationCalls == 1);
    CHECK(h.server->restoreCalls == 0);
}

TEST_CASE("migration: an unreachable server is asked again a minute later, never blocking") {
    Harness h("unreachable");
    MigrationState st;
    CHECK(h.login());
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->down = true;
    }
    const auto t0 = std::chrono::steady_clock::now();
    st.update(h.svc, 0.016f);  // returns at once: the request goes on the network thread
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200));
    CHECK(pump(h.svc, &st, [&] { return h.server->migrationCalls == 1 && h.svc.state() == OnlineState::Offline; }));
    CHECK(!st.available());
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->down = false;
    }
    // Back online (the service's own reconnection), then asked again after the wait.
    CHECK(pump(h.svc, &st, [&] { return st.available(); }, 5000, 2.0f));
    CHECK(h.server->migrationCalls == 2);
}

TEST_CASE("migration: a saved session resumed at startup asks too") {
    auto server = std::make_shared<MigServer>();
    fs::path dir = tempDir("resumed");
    {
        OnlineService svc;
        svc.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<MigTransport>(server), dir);
        bool done = false;
        svc.login("OrionNew", "good password", true, [&](const ApiResult&) { done = true; });
        pump(svc, nullptr, [&] { return done; });
        svc.shutdown();
    }
    {
        OnlineService svc;  // the next start: the session is resumed, no login
        svc.init(OnlineSettings{}, "http://127.0.0.1:9", std::make_unique<MigTransport>(server), dir);
        MigrationState st;
        CHECK(pump(svc, &st, [&] { return st.available(); }));
        svc.shutdown();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("migration: the download of the progression to restore is checked") {
    const std::string body = serverCopyFile(savesOf({"sylvanox_01"}, 60.0, "sylvanox_01", 50.0f, 1, {"first_steps"}, {}));
    HttpResponse ok;
    ok.status = 200;
    ok.body = body;
    ok.headers["x-progress-sha256"] = Sha256::hex(body);
    RestoreDownload d = readRestoreDownload(ok);
    CHECK(d.ok);
    CHECK(d.sha256 == Sha256::hex(body));
    CHECK(d.gameVersion == "2.50.0");
    CHECK(d.files.contains("progress") && d.files.contains("collection"));
    // Damaged on the way: refused.
    HttpResponse cut = ok;
    cut.body = body.substr(0, body.size() - 3);
    d = readRestoreDownload(cut);
    CHECK(!d.ok && !d.error.empty());
    // Not a copy of the progression.
    HttpResponse other;
    other.status = 200;
    other.body = Json{{"format", "cosmic.level"}, {"files", Json::object()}}.dump();
    CHECK(!readRestoreDownload(other).ok);
    // Nothing waits any more: the server's French message.
    d = readRestoreDownload(apiError(404, "RESTORE_NOT_FOUND", "Aucune progression à restaurer."));
    CHECK(!d.ok && d.gone && d.error == "Aucune progression à restaurer.");
    // No network.
    HttpResponse net;
    net.error = NetError::Timeout;
    d = readRestoreDownload(net);
    CHECK(!d.ok && !d.gone && !d.error.empty());
}

TEST_CASE("migration: the admin preview's problems in French, LAST_ADMIN only when the source is closed") {
    const Json problems = Json::array({"LAST_ADMIN"});
    CHECK(migrationBlockingProblems(problems, false).empty());
    CHECK(migrationBlockingProblems(problems, true).size() == 1);
    const Json all = Json::array({"SOURCE_CLOSED", "TARGET_NOT_FOUND", "SAME_ACCOUNT", "TARGET_INACTIVE", "SOURCE_INACTIVE"});
    CHECK(migrationBlockingProblems(all, false).size() == 5);
    for (const Json& p : all) CHECK(migrationProblemText(p.get<std::string>()) != migrationProblemText("SOMETHING_NEW"));
    CHECK(migrationProblemText("TARGET_NOT_FOUND") == "Compte cible introuvable.");
    CHECK(migrationBlockingProblems(Json(), true).empty());
}

TEST_CASE("migration: the old computer's copy, as the server keeps it, is added to this computer's saves") {
    const Json mine = savesOf({"sylvanox_01", "sylvanox_02"}, 100.0, "sylvanox_01", 80.0f, 1, {"first_steps"}, {"music/a.ogg"});
    const Json theirs = savesOf({"sylvanox_02", "glacior_01"}, 50.0, "sylvanox_01", 70.0f, 2, {"first_steps", "boss_hunter"}, {"music/b.ogg"});
    HttpResponse r;
    r.status = 200;
    r.body = serverCopyFile(theirs);
    r.headers["x-progress-sha256"] = Sha256::hex(r.body);
    const RestoreDownload d = readRestoreDownload(r);
    CHECK(d.ok);
    const Json merged = mergeProgressCopyFiles(mine, d.files);
    for (const std::string& part : progressCopyParts()) CHECK(merged.contains(part) && merged[part].is_object());
    Progression p;
    p.fromJson(merged["progress"]);
    CHECK(p.completedCount() == 3);
    CHECK_NEAR(p.playTimeSeconds, 150.0, 1e-6);
    CHECK_NEAR(p.levelPlayTime("sylvanox_01"), 150.0, 1e-6);
    RecordsManager rec;
    rec.fromJson(merged["records"]);
    const LevelRecord* lr = rec.get("sylvanox_01");
    CHECK(lr && lr->bestTime == 70.0f && lr->attempts == 8 && lr->completions == 4);
    BossStats bosses;
    bosses.fromJson(merged["boss_stats"]);
    CHECK(bosses.find("astrarque") && bosses.find("astrarque")->kills == 3);
    AchievementBook book;
    book.fromJson(merged["achievements"]);
    CHECK(book.unlockedCount() == 2 && book.unlocked("boss_hunter"));
    CHECK(book.stats().counter("jumps") == 20);
    CollectionBook col;
    col.fromJson(merged["collection"]);
    CHECK(col.heard.size() == 2);
    // A copy without one of the parts (an older game): that part stays as it is here.
    Json partial = d.files;
    partial.erase("boss_stats");
    const Json m2 = mergeProgressCopyFiles(mine, partial);
    CHECK(m2["boss_stats"] == mine["boss_stats"]);
}

TEST_CASE("migration: the saves are kept as they are before a restoration, a copy is restored once") {
    fs::path dir = tempDir("backup");
    fs::path saves = dir / "saves";
    fs::create_directories(saves);
    CHECK(FileUtil::writeTextAtomic(saves / "progress.json", "{\"format\":\"cosmic.progress\",\"version\":1,\"data\":{}}"));
    CHECK(FileUtil::writeTextAtomic(saves / "records.json", "{\"x\":1}"));
    CHECK(FileUtil::writeTextAtomic(saves / "stats.json", "{\"format\":\"cosmic.stats\",\"version\":1,\"data\":{\"seq\":4}}"));
    fs::path kept;
    std::string err;
    CHECK(backupSaveFiles(saves, "2026-11-16 21:54", &kept, &err));
    CHECK(kept.filename().string() == "avant_restauration_2026-11-16-21-54");
    CHECK(*FileUtil::readText(kept / "progress.json") == "{\"format\":\"cosmic.progress\",\"version\":1,\"data\":{}}");
    CHECK(*FileUtil::readText(kept / "records.json") == "{\"x\":1}");
    CHECK(*FileUtil::readText(kept / "stats.json") == "{\"format\":\"cosmic.stats\",\"version\":1,\"data\":{\"seq\":4}}");  // 2.55
    CHECK(!fs::exists(kept / "collection.json"));  // none here: nothing to keep
    // The same minute again: another folder, the first one untouched.
    fs::path kept2;
    CHECK(backupSaveFiles(saves, "2026-11-16 21:54", &kept2, &err));
    CHECK(kept2 != kept && fs::exists(kept2 / "records.json"));
    // A copy is restored once.
    const fs::path registry = saves / "restored_copies.json";
    const std::string sha(64, 'a');
    CHECK(!progressCopyRestored(registry, sha));
    CHECK(noteProgressCopyRestored(registry, sha));
    CHECK(progressCopyRestored(registry, sha));
    CHECK(!progressCopyRestored(registry, std::string(64, 'b')));
    CHECK(!progressCopyRestored(registry, ""));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("migration: the statistics travel in the copy, merged without counting twice; the same copy restored twice adds nothing") {
    // 2.55: stats.json is one of the parts of a copy (sent, kept before a restoration, restored).
    const auto& parts = progressCopyParts();
    CHECK(std::find(parts.begin(), parts.end(), "stats") != parts.end());
    const int today = daysFromCivil(2026, 11, 17);
    PlayStatsData mineStats, theirStats;
    mineStats.at(today - 1).playSec = 1200.0;
    mineStats.at(today - 1).deaths = 3;
    theirStats.at(today - 1).playSec = 600.0;
    theirStats.at(today - 1).deaths = 8;
    theirStats.at(today - 30).playSec = 900.0;
    BossAttemptRec r;
    r.day = today - 30;
    r.id = 42;
    r.removed = 0.5f;
    r.attack = "Piqué";
    BossHistory& b = theirStats.bosses["pyrrhax"];
    b.phases = 1;
    b.attempts = 1;
    b.reached = {1};
    b.defeats = {1};
    b.fatal["Piqué"] = 1;
    b.recent = {r};
    Json mine = savesOf({"sylvanox_01"}, 100.0, "sylvanox_01", 80.0f, 1, {}, {});
    Json theirs = savesOf({"sylvanox_02"}, 50.0, "sylvanox_01", 70.0f, 2, {}, {});
    mine["stats"] = mineStats.toJson();
    theirs["stats"] = theirStats.toJson();
    HttpResponse resp;
    resp.status = 200;
    resp.body = serverCopyFile(theirs);
    resp.headers["x-progress-sha256"] = Sha256::hex(resp.body);
    const RestoreDownload d = readRestoreDownload(resp);
    CHECK(d.ok);
    const Json merged = mergeProgressCopyFiles(mine, d.files, today);
    const PlayStatsData m = PlayStatsData::fromJson(merged["stats"]);
    CHECK(m.find(today - 1) && m.find(today - 1)->c.deaths == 8);  // the highest of both sides, not 11
    CHECK_NEAR(m.find(today - 1)->c.playSec, 1200.0, 1e-6);         // not 1800
    CHECK(m.find(today - 30) != nullptr);
    CHECK(m.bosses.at("pyrrhax").attempts == 1 && m.bosses.at("pyrrhax").recent.at(0).id == 42);
    // Merged a second time (a restoration noted nowhere, the game closed before): the statistics
    // stay the same. The other parts are guarded by saves/restored_copies.json (tested below).
    const Json twice = mergeProgressCopyFiles(merged, d.files, today);
    CHECK(twice["stats"] == merged["stats"]);
    // A copy of an older game (no statistics): this computer's stay as they are.
    Json older = d.files;
    older.erase("stats");
    CHECK(mergeProgressCopyFiles(mine, older, today)["stats"] == mine["stats"]);
    // A damaged part (not an object): ignored.
    older["stats"] = Json::array({1, 2, 3});
    CHECK(mergeProgressCopyFiles(mine, older, today)["stats"] == mine["stats"]);
}

TEST_CASE("migration: a private download renews an expiring session first") {
    Harness h("download");
    h.server->expiredSessions = true;
    h.server->restore = serverRestore();
    h.server->file = serverCopyFile(savesOf({"sylvanox_01"}, 10.0, "sylvanox_01", 30.0f, 0, {}, {}));
    CHECK(h.login());
    bool done = false;
    RestoreDownload d;
    h.svc.getBinary("/api/v1/players/me/progress-restore/file", [&](const HttpResponse& r) {
        d = readRestoreDownload(r);
        done = true;
    });
    CHECK(pump(h.svc, nullptr, [&] { return done; }));
    CHECK(h.server->refreshCalls >= 1);
    CHECK(h.server->fileAuth == "Bearer " + h.server->access);
    CHECK(d.ok && d.files.contains("records"));
}

TEST_CASE("migration: the achievements are added to the account's while a migrated progression waits") {
    Harness h("achievements");
    CHECK(h.login());
    h.svc.setAchievements({"first_steps"});
    CHECK(pump(h.svc, nullptr, [&] { return h.server->achievementBodies.size() == 1; }, 3000, 1.0f));
    CHECK(!h.server->achievementBodies[0].contains("merge"));  // the list of this computer replaces it
    h.svc.setAchievementsMerge(true);
    h.svc.setAchievements({"first_steps", "boss_hunter"});
    CHECK(pump(h.svc, nullptr, [&] { return h.server->achievementBodies.size() == 2; }, 3000, 1.0f));
    CHECK(h.server->achievementBodies[1].value("merge", false) == true);
}

TEST_CASE("migration: the old computer is free again once its account was migrated and closed") {
    Harness h("closed");
    CHECK(h.login());
    CHECK(h.svc.device().boundUsername == "OrionNew");
    bool out = false;
    h.svc.logout(false, [&](const ApiResult&) { out = true; });
    CHECK(pump(h.svc, nullptr, [&] { return out; }));
    CHECK(h.svc.device().boundUsername == "OrionNew");  // logged out: still this computer's account
    {
        std::lock_guard<std::mutex> lock(h.server->m);
        h.server->closed = true;
    }
    // Another pseudonym refused as closed: this computer's account stays.
    bool done = false;
    ApiResult res;
    h.svc.login("Someone", "good password", true, [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, nullptr, [&] { return done; }));
    CHECK(res.code == "ACCOUNT_CLOSED" && h.svc.device().boundUsername == "OrionNew");
    // Its own pseudonym (any case): the server says it is closed, the computer is released.
    done = false;
    h.svc.login("orionnew", "good password", true, [&](const ApiResult& r) {
        res = r;
        done = true;
    });
    CHECK(pump(h.svc, nullptr, [&] { return done; }));
    CHECK(!res.ok && res.message == "Ce compte a été migré vers un autre compte : il est fermé.");
    CHECK(h.svc.device().boundUsername.empty() && h.svc.device().boundPlayer.empty());
    // Kept on disk too.
    auto saved = FileUtil::loadJson(h.dir / "online" / "device.json");
    CHECK(saved && jstr(*saved, "bound_username").empty() && jstr(*saved, "bound_player").empty());
}
