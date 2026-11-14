// Live editing of collaborative projects (client side): the operations computed from two
// states of a level and applied like the server does, and a live session against a
// scripted room (join, my operations confirmed once, the others' operations applied in
// order, refusals reported with the server's state, edits kept on disk while offline).

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include "game/levels/LevelData.h"
#include "game/online/LiveSession.h"
#include "game/online/OnlineService.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path liveDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("cosmic_live_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

HttpResponse ok(const Json& data, int status = 200) {
    HttpResponse r;
    r.status = status;
    r.body = Json{{"ok", true}, {"data", data}, {"request_id", "t"}}.dump();
    r.headers["content-type"] = "application/json";
    return r;
}

/// A project room: one document, the operations in order, refusals like the server's.
struct Room {
    std::mutex m;
    bool down = false;
    Json doc;
    int seq = 0;
    std::vector<Json> log;                                 // {seq, id, author, op}
    std::map<std::string, std::pair<int, std::string>> touched;  // key -> seq, author
    std::map<std::string, std::string> locks;
    int opsRequests = 0;
    int epoch = 0, joins = 0;  // epoch: changes when the document is replaced (restore)

    Json accept(const std::string& author, const std::string& id, int base, const Json& op) {
        for (const auto& l : log)
            if (jstr(l, "id") == id) return {{"id", id}, {"status", "ok"}, {"seq", l["seq"]}};
        std::string key = LiveOps::touched(op);
        std::string objectId = key.substr(2);
        if (key[0] == 'o' && locks.count(objectId) && locks[objectId] != author) return {{"id", id}, {"status", "locked"}, {"by", "Orion"}};
        auto t = touched.find(key);
        if (t != touched.end() && t->second.first > base && t->second.second != author) {
            Json current;
            for (const auto& o : doc["objects"])
                if (jstr(o, "id") == objectId) current = o;
            return {{"id", id}, {"status", "conflict"}, {"by", "Orion"}, {"seq", t->second.first}, {"current", current}};
        }
        LiveOps::apply(doc, op);
        ++seq;
        touched[key] = {seq, author};
        log.push_back({{"seq", seq}, {"id", id}, {"author", author}, {"op", op}});
        return {{"id", id}, {"status", "ok"}, {"seq", seq}};
    }

    HttpResponse handle(const HttpRequest& rq) {
        std::lock_guard<std::mutex> lock(m);
        std::string path = rq.url.substr(rq.url.find("/api/"));
        if (down) {
            HttpResponse r;
            r.error = NetError::Connect;
            r.errorMessage = "refused";
            return r;
        }
        if (path == "/api/v1/auth/login")
            return ok({{"player", {{"id", "me-0000"}, {"username", "Nova"}}},
                       {"session", {{"access_token", "a-000000000000000000000000"}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                                    {"refresh_token", "r-000000000000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}}}});
        Json body = rq.body.empty() ? Json::object() : Json::parse(rq.body, nullptr, false);
        auto presence = [&] {
            Json locksJson = Json::object();
            for (const auto& [k, v] : locks) locksJson[k] = v;
            return Json{{"participants", Json::array({{{"id", "me-0000"}, {"username", "Nova"}, {"color", "#39F0D8"}, {"me", true}},
                                                      {{"id", "orion-01"}, {"username", "Orion"}, {"color", "#FFB347"}, {"cursor", {{"x", 40}, {"y", 20}}},
                                                       {"selection", Json::array({"o1"})}, {"activity", "déplace Bloc"}}})},
                        {"locks", locksJson}};
        };
        if (path.find("/live/join") != std::string::npos) {
            Json d = presence();
            d["doc"] = doc;
            d["seq"] = seq;
            d["revision"] = 1;
            d["epoch"] = epoch;
            ++joins;
            return ok(d);
        }
        if (path.find("/live/heartbeat") != std::string::npos) {
            int since = jget<int>(body, "since_seq", 0);
            Json ops = Json::array();
            for (const auto& l : log)
                if (jget<int>(l, "seq", 0) > since) ops.push_back(l);
            Json d = presence();
            d["ops"] = ops;
            d["seq"] = seq;
            d["revision"] = 1;
            d["epoch"] = epoch;
            d["resync"] = jget<int>(body, "epoch", -1) != epoch;
            return ok(d);
        }
        if (path.find("/live/ops") != std::string::npos) {
            ++opsRequests;
            Json results = Json::array();
            for (const auto& item : body["ops"]) results.push_back(accept("me-0000", jstr(item, "id"), jget<int>(item, "base_seq", 0), item["op"]));
            return ok({{"results", results}, {"seq", seq}});
        }
        if (path.find("/live/leave") != std::string::npos) return ok({{"left", true}});
        HttpResponse r;
        r.status = 404;
        r.body = R"({"ok":false,"error":{"code":"NOT_FOUND","message":"x"}})";
        return r;
    }
};

class RoomTransport final : public IHttpTransport {
public:
    explicit RoomTransport(std::shared_ptr<Room> r) : r_(std::move(r)) {}
    HttpResponse perform(const HttpRequest& rq) override { return r_->handle(rq); }
    const char* name() const override { return "fake"; }

private:
    std::shared_ptr<Room> r_;
};

template <typename P>
bool spin(OnlineService& svc, LiveSession& live, P done, int ms = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(0.05f);
        live.update(0.05f);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

Json baseDoc() {
    return Json{{"id", "projet_x"}, {"name", "Projet"}, {"layers", Json::array({{{"id", "gameplay"}}})},
                {"objects", Json::array({{{"id", "o1"}, {"type", "solid"}, {"x", 0}}, {{"id", "o2"}, {"type", "oneway"}, {"x", 10}}})}};
}

}  // namespace

TEST_CASE("live: operations between two states, applied like the server") {
    Json a = baseDoc();
    Json b = a;
    b["name"] = "Projet renommé";
    b["objects"][0]["x"] = 32;
    b["objects"][1].erase("x");
    b["objects"].push_back({{"id", "o3"}, {"type", "door"}});
    b["objects"].erase(0);  // o1 removed after all
    auto ops = LiveOps::diff(a, b);
    Json c = a;
    for (const auto& op : ops) LiveOps::apply(c, op);
    // Same objects (order aside) and same keys.
    CHECK(c["name"] == b["name"]);
    CHECK(c["objects"].size() == b["objects"].size());
    for (const auto& o : b["objects"]) {
        bool found = false;
        for (const auto& x : c["objects"]) found |= x == o;
        CHECK(found);
    }
    CHECK(LiveOps::diff(c, b).empty());
    CHECK(LiveOps::touched({{"t", "doc"}, {"k", "name"}}) == "d:name");
    CHECK(LiveOps::touched({{"t", "add"}, {"o", {{"id", "o9"}}}}) == "o:o9");
    // A real level survives the round trip.
    LevelData lvl = LevelData::makeEmpty("x", "Niveau", "sylvanox");
    Json j1 = lvl.toJson();
    lvl.name = "Autre";
    Json j2 = lvl.toJson();
    Json j3 = j1;
    for (const auto& op : LiveOps::diff(j1, j2)) LiveOps::apply(j3, op);
    CHECK(j3 == j2);
}

TEST_CASE("live: a session joins, sends my edits once, applies the others' in order") {
    auto room = std::make_shared<Room>();
    room->doc = baseDoc();
    fs::path dir = liveDir("session");
    OnlineService svc;
    svc.init({}, "http://127.0.0.1:9", std::make_unique<RoomTransport>(room), dir);
    bool logged = false;
    svc.login("Nova", "pw", false, [&](const ApiResult& r) { logged = r.ok; });
    for (int i = 0; i < 3000 && !logged; ++i) {
        svc.update(0.05f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(logged);
    {
        LiveSession live(svc, "p", dir / "queue.json");
        CHECK(spin(svc, live, [&] { return live.state() == LiveSession::State::Live; }));
        auto doc = live.takeJoinedDocument();
        CHECK(doc.has_value());
        CHECK(live.participants().size() == 2u);
        CHECK(live.locks().count("o1") == 0u);  // no lock yet in the room
        // My edit: sent and confirmed once.
        live.submit({Json{{"t", "set"}, {"id", "o2"}, {"f", {{"x", 50}}}}});
        CHECK(spin(svc, live, [&] { return live.pending() == 0; }));
        CHECK(room->seq == 1);
        // Orion's edit arrives through the heartbeat; mine does not come back.
        {
            std::lock_guard<std::mutex> lock(room->m);
            room->accept("orion-01", "orion-op-1", 1, {{"t", "set"}, {"id", "o1"}, {"f", {{"x", 99}}}});
        }
        std::vector<Json> remote;
        CHECK(spin(svc, live, [&] {
            for (auto& o : live.takeRemoteOps()) remote.push_back(o);
            return !remote.empty();
        }));
        CHECK(remote.size() == 1u);
        if (!remote.empty()) CHECK(jstr(remote[0], "id") == "o1");
        CHECK(live.seq() == 2);
        // A stale edit of what Orion changed is refused, with his version.
        {
            std::lock_guard<std::mutex> lock(room->m);
            room->accept("orion-01", "orion-op-2", 2, {{"t", "set"}, {"id", "o1"}, {"f", {{"x", 7}}}});
        }
        live.submit({Json{{"t", "set"}, {"id", "o1"}, {"f", {{"x", 1}}}}});
        std::vector<LiveSession::Rejection> rej;
        CHECK(spin(svc, live, [&] {
            for (auto& r : live.takeRejections()) rej.push_back(r);
            return !rej.empty();
        }));
        if (!rej.empty()) {
            CHECK(rej[0].status == "conflict");
            CHECK(rej[0].by == "Orion");
            CHECK(jget<int>(rej[0].current, "x", 0) == 7);
        }
        // Offline: my edit stays on disk and is sent once the room answers again.
        {
            std::lock_guard<std::mutex> lock(room->m);
            room->down = true;
        }
        live.submit({Json{{"t", "doc"}, {"k", "name"}, {"v", "Hors ligne"}}});
        CHECK(spin(svc, live, [&] { return live.state() == LiveSession::State::Offline; }, 4000));
        CHECK(fs::exists(dir / "queue.json"));
        CHECK(live.pending() == 1u);
    }
    // Next start: the edit made offline is still there and goes through.
    {
        std::lock_guard<std::mutex> lock(room->m);
        room->down = false;
    }
    LiveSession again(svc, "p", dir / "queue.json");
    CHECK(again.pending() == 1u);
    CHECK(spin(svc, again, [&] { return again.state() == LiveSession::State::Live && again.pending() == 0; }, 5000));
    auto doc = again.takeJoinedDocument();
    if (doc) CHECK(jstr(*doc, "name") == "Hors ligne");  // shown at once, before the answer
    CHECK(jstr(room->doc, "name") == "Hors ligne");
    CHECK(!fs::exists(dir / "queue.json"));
    again.leave();
    svc.shutdown();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("live: a document replaced by a restore makes the session rejoin") {
    auto room = std::make_shared<Room>();
    room->doc = baseDoc();
    fs::path dir = liveDir("epoch");
    OnlineService svc;
    svc.init({}, "http://127.0.0.1:9", std::make_unique<RoomTransport>(room), dir);
    bool logged = false;
    svc.login("Nova", "pw", false, [&](const ApiResult& r) { logged = r.ok; });
    for (int i = 0; i < 3000 && !logged; ++i) {
        svc.update(0.05f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(logged);
    LiveSession live(svc, "p", dir / "queue.json");
    CHECK(spin(svc, live, [&] { return live.state() == LiveSession::State::Live; }));
    CHECK(live.takeJoinedDocument().has_value());
    // Heartbeats with the same epoch: no new join.
    CHECK(spin(svc, live, [&] { return false; }, 1500) == false);
    CHECK(room->joins == 1);
    // A restore replaces the document (new epoch): the next heartbeat brings the session back.
    {
        std::lock_guard<std::mutex> lock(room->m);
        room->doc["name"] = "Restaurée";
        room->doc["objects"].erase(1);
        ++room->epoch;
    }
    std::optional<Json> doc;
    CHECK(spin(svc, live, [&] {
        if (auto d = live.takeJoinedDocument()) doc = d;
        return doc.has_value() && live.state() == LiveSession::State::Live;
    }, 4000));
    if (doc) {
        CHECK(jstr(*doc, "name") == "Restaurée");
        CHECK((*doc)["objects"].size() == 1u);
    }
    CHECK(room->joins == 2);
    live.leave();
    svc.shutdown();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
