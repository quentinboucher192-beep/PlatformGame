// Cosmic Online 2.19: the players' ratings of the official levels on the game's side. The
// averages come only from the server (parsed strictly), are kept on disk for the campaign
// and are never presented as fresh once loaded from there; nothing is read from the server
// without an account, and nothing is invented without a cache.

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

#include "engine/core/FileUtil.h"
#include "game/online/LevelRatings.h"
#include "game/online/OnlineService.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path ratingsDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() /
                 ("cosmic_ratings_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

Json ratingsList() {
    return {{"levels", Json::array({{{"level_id", "sylvanox_01"}, {"average", 4.3}, {"count", 128}},
                                    {{"level_id", "glacior_02"}, {"average", 2.0}, {"count", 1}}})},
            {"my_ratings", {{"sylvanox_01", 4}}}};
}

/// A minimal Cosmic Online server: login, profile and the ratings of the levels.
struct RatingsServer {
    std::mutex m;
    bool down = false;
    int ratingsCalls = 0;
    const std::string access = "access-ratings-000000000000000";

    static HttpResponse reply(int status, const Json& body) {
        HttpResponse r;
        r.status = status;
        r.body = body.dump();
        r.headers["content-type"] = "application/json";
        return r;
    }
    static HttpResponse ok(const Json& data) { return reply(200, {{"ok", true}, {"data", data}, {"request_id", "t"}}); }

    HttpResponse handle(const HttpRequest& rq) {
        std::lock_guard<std::mutex> lock(m);
        std::string path = rq.url.substr(rq.url.find("/api/"));
        if (down) {
            HttpResponse r;
            r.error = NetError::Connect;
            r.errorMessage = "refused";
            return r;
        }
        bool authed = false;
        for (const auto& [k, v] : rq.headers) authed |= k == "Authorization" && v == "Bearer " + access;
        Json player = {{"id", "22222222-2222-4222-8222-222222222222"}, {"username", "Vega"}};
        if (path == "/api/v1/auth/login")
            return ok({{"player", player},
                       {"session",
                        {{"access_token", access}, {"access_expires_at", "2099-01-01T00:00:00.000Z"}, {"refresh_token", "refresh-ratings-0000000000"},
                         {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}}}});
        if (path == "/api/v1/levels/ratings") {
            ++ratingsCalls;
            if (!authed) return reply(401, {{"ok", false}, {"error", {{"code", "UNAUTHORIZED"}, {"message", "?"}}}});
            return ok(ratingsList());
        }
        if (path == "/api/v1/auth/me") return ok({{"player", player}});
        return reply(404, {{"ok", false}, {"error", {{"code", "NOT_FOUND"}, {"message", "Route inconnue."}}}});
    }
};

class RatingsTransport final : public IHttpTransport {
public:
    explicit RatingsTransport(std::shared_ptr<RatingsServer> s) : s_(std::move(s)) {}
    HttpResponse perform(const HttpRequest& rq) override { return s_->handle(rq); }
    const char* name() const override { return "fake"; }

private:
    std::shared_ptr<RatingsServer> s_;
};

template <typename Pred>
bool pumpUntil(OnlineService& svc, Pred done, int timeoutMs = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(0.016f);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    svc.update(0.016f);
    return done();
}

}  // namespace

TEST_CASE("level ratings: texts in French, one decimal") {
    CHECK(LevelRatingCache::averageText(4.3) == "4,3");
    CHECK(LevelRatingCache::averageText(4.25) == "4,3");
    CHECK(LevelRatingCache::averageText(5.0) == "5,0");
    CHECK(LevelRatingCache::countText(0) == "aucune note");
    CHECK(LevelRatingCache::countText(1) == "1 note");
    CHECK(LevelRatingCache::countText(128) == "128 notes");
}

TEST_CASE("level ratings: answers of the server are parsed strictly") {
    LevelRatingCache c;
    // Nothing received: nothing to show (never a made-up value).
    CHECK(!c.get("sylvanox_01", "p1").has_value());
    CHECK(!c.known());
    CHECK(!c.applyList(Json::object(), "https://s", "p1", 100));
    CHECK(!c.applyList({{"levels", "nope"}}, "https://s", "p1", 100));
    Json data = ratingsList();
    data["levels"].push_back({{"level_id", "bad id!"}, {"average", 3.0}, {"count", 2}});
    data["levels"].push_back({{"level_id", "too_high"}, {"average", 7.5}, {"count", 2}});
    data["levels"].push_back({{"level_id", "negative"}, {"average", 3.0}, {"count", -4}});
    data["levels"].push_back({{"level_id", "no_average"}, {"count", 3}});
    data["my_ratings"]["glacior_02"] = 9;  // out of range: ignored
    CHECK(c.applyList(data, "https://s", "p1", 1000));
    CHECK(c.known());
    CHECK(c.fresh());
    auto a = c.get("sylvanox_01", "p1");
    CHECK(a.has_value());
    CHECK_NEAR(a->average, 4.3, 1e-9);
    CHECK(a->count == 128);
    CHECK(a->myStars == 4);
    CHECK(c.get("glacior_02", "p1")->myStars == 0);
    for (const char* bad : {"too_high", "negative", "no_average", "bad id!"}) CHECK(c.get(bad, "p1")->count == 0);
    // A level absent from a received list has no rating yet.
    CHECK(c.get("pyralis_03", "p1")->count == 0);
    // Another account never sees the first one's stars.
    CHECK(c.get("sylvanox_01", "p2")->myStars == 0);
    CHECK(c.get("sylvanox_01", "")->myStars == 0);
    // One level's view (after rating it).
    Json view = {{"level_id", "sylvanox_01"}, {"rating", {{"average", 4.4}, {"count", 129}}}, {"my_stars", 5}};
    CHECK(c.applyOne(view, "https://s", "p1"));
    CHECK(c.get("sylvanox_01", "p1")->count == 129);
    CHECK(c.get("sylvanox_01", "p1")->myStars == 5);
    Json removed = {{"level_id", "glacior_02"}, {"rating", {{"average", nullptr}, {"count", 0}}}, {"my_stars", nullptr}};
    CHECK(c.applyOne(removed, "https://s", "p1"));
    CHECK(c.get("glacior_02", "p1")->count == 0);
    CHECK(!c.applyOne({{"level_id", "x"}, {"rating", {{"average", 0.2}, {"count", 3}}}}, "https://s", "p1"));
    // Saved and loaded again: same values, never fresh, only for the same server.
    Json saved = c.toJson();
    LevelRatingCache back;
    CHECK(back.fromJson(saved, "https://s"));
    CHECK(!back.fresh());
    CHECK(back.known());
    CHECK(back.receivedAt() == 1000);
    CHECK(back.get("sylvanox_01", "p1")->count == 129);
    CHECK(back.get("sylvanox_01", "p1")->myStars == 5);
    LevelRatingCache other;
    CHECK(!other.fromJson(saved, "https://another"));
    CHECK(!other.get("sylvanox_01", "p1").has_value());
    // A list from another server replaces everything.
    CHECK(back.applyList({{"levels", Json::array()}, {"my_ratings", nullptr}}, "https://another", "p1", 2000));
    CHECK(back.get("sylvanox_01", "p1")->count == 0);
}

TEST_CASE("level ratings: read only when logged in, kept on disk, stale offline") {
    auto server = std::make_shared<RatingsServer>();
    fs::path dir = ratingsDir("service");
    const std::string url = "http://127.0.0.1:9";
    {
        OnlineService svc;
        svc.init(OnlineSettings{}, url, std::make_unique<RatingsTransport>(server), dir);
        // Without an account nothing is sent.
        bool done = false;
        ApiResult res;
        svc.refreshLevelRatings(true, [&](const ApiResult& r) {
            res = r;
            done = true;
        });
        CHECK(done && res.code == "NOT_LOGGED_IN");
        CHECK(server->ratingsCalls == 0);
        CHECK(!svc.levelRatings().get("sylvanox_01", svc.playerId()).has_value());
        done = false;
        svc.login("Vega", "secret password", false, [&](const ApiResult& r) {
            res = r;
            done = true;
        });
        CHECK(pumpUntil(svc, [&] { return done; }));
        CHECK(svc.loggedIn());
        done = false;
        svc.refreshLevelRatings(false, [&](const ApiResult& r) {
            res = r;
            done = true;
        });
        CHECK(pumpUntil(svc, [&] { return done; }));
        CHECK(res.ok);
        CHECK(svc.levelRatings().fresh());
        auto info = svc.levelRatings().get("sylvanox_01", svc.playerId());
        CHECK(info && info->count == 128 && info->myStars == 4);
        // Read again soon: not sent twice.
        done = false;
        svc.refreshLevelRatings(false, [&](const ApiResult& r) {
            res = r;
            done = true;
        });
        CHECK(done && res.code == "NOT_NEEDED");
        CHECK(server->ratingsCalls == 1);
        // A rating given from a screen updates the cache.
        svc.noteLevelRating({{"level_id", "sylvanox_01"}, {"rating", {{"average", 4.4}, {"count", 129}}}, {"my_stars", 5}});
        CHECK(svc.levelRatings().get("sylvanox_01", svc.playerId())->myStars == 5);
        svc.shutdown();
    }
    CHECK(fs::exists(dir / "online" / "level_ratings.json"));
    {
        // Next start, server unreachable: the last values stay, flagged as received earlier.
        {
            std::lock_guard<std::mutex> lock(server->m);
            server->down = true;
        }
        OnlineService svc;
        svc.init(OnlineSettings{}, url, std::make_unique<RatingsTransport>(server), dir);
        CHECK(svc.levelRatings().known());
        CHECK(!svc.levelRatings().fresh());
        auto info = svc.levelRatings().get("sylvanox_01", "22222222-2222-4222-8222-222222222222");
        CHECK(info && info->count == 129 && info->myStars == 5);
        // « Effacer le cache en ligne » forgets them.
        svc.clearCache();
        CHECK(!svc.levelRatings().get("sylvanox_01", "").has_value());
        CHECK(!fs::exists(dir / "online" / "level_ratings.json"));
        svc.shutdown();
    }
    {
        // Saved for another server: ignored.
        {
            std::lock_guard<std::mutex> lock(server->m);
            server->down = false;
        }
        LevelRatingCache elsewhere;
        CHECK(elsewhere.applyList(ratingsList(), "https://elsewhere.example", "p", 50));
        FileUtil::writeTextAtomic(dir / "online" / "level_ratings.json", elsewhere.toJson().dump());
        OnlineService svc;
        svc.init(OnlineSettings{}, url, std::make_unique<RatingsTransport>(server), dir);
        CHECK(!svc.levelRatings().known());
        svc.shutdown();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
