#include "game/online/LiveSession.h"

#include <algorithm>
#include <random>

#include "engine/core/FileUtil.h"
#include "game/online/OnlineService.h"

namespace cosmic {

// ------------------------------------------------------------------ LiveOps

namespace LiveOps {

std::vector<Json> diff(const Json& before, const Json& after) {
    std::vector<Json> ops;
    auto byId = [](const Json& doc) {
        std::map<std::string, const Json*> m;
        if (doc.contains("objects") && doc["objects"].is_array())
            for (const auto& o : doc["objects"])
                if (o.is_object() && o.contains("id") && o["id"].is_string()) m[o["id"].get<std::string>()] = &o;
        return m;
    };
    auto a = byId(before), b = byId(after);
    // Deleted, then changed, then added (in the document's order).
    for (const auto& [id, o] : a)
        if (!b.count(id)) ops.push_back({{"t", "del"}, {"id", id}});
    if (after.contains("objects") && after["objects"].is_array())
        for (const auto& o : after["objects"]) {
            if (!o.is_object() || !o.contains("id") || !o["id"].is_string()) continue;
            std::string id = o["id"].get<std::string>();
            auto it = a.find(id);
            if (it == a.end()) {
                ops.push_back({{"t", "add"}, {"o", o}});
                continue;
            }
            const Json& old = *it->second;
            if (old == o) continue;
            Json f = Json::object();
            for (auto kv = o.begin(); kv != o.end(); ++kv)
                if (kv.key() != "id" && (!old.contains(kv.key()) || old[kv.key()] != kv.value())) f[kv.key()] = kv.value();
            for (auto kv = old.begin(); kv != old.end(); ++kv)
                if (kv.key() != "id" && !o.contains(kv.key())) f[kv.key()] = nullptr;
            if (!f.empty()) ops.push_back({{"t", "set"}, {"id", id}, {"f", f}});
        }
    // Other keys of the document, whole.
    std::set<std::string> keys;
    if (before.is_object())
        for (auto kv = before.begin(); kv != before.end(); ++kv) keys.insert(kv.key());
    if (after.is_object())
        for (auto kv = after.begin(); kv != after.end(); ++kv) keys.insert(kv.key());
    for (const auto& k : keys) {
        if (k == "objects" || k == "id") continue;
        bool inA = before.contains(k), inB = after.contains(k);
        if (inA && inB && before[k] == after[k]) continue;
        ops.push_back({{"t", "doc"}, {"k", k}, {"v", inB ? after[k] : Json(nullptr)}});
    }
    return ops;
}

void apply(Json& doc, const Json& op) {
    if (!doc.is_object() || !op.is_object()) return;
    if (!doc.contains("objects") || !doc["objects"].is_array()) doc["objects"] = Json::array();
    Json& list = doc["objects"];
    std::string t = jstr(op, "t");
    auto find = [&](const std::string& id) -> Json* {
        for (auto& o : list)
            if (o.is_object() && jstr(o, "id") == id) return &o;
        return nullptr;
    };
    if (t == "add" && op.contains("o")) {
        list.push_back(op["o"]);
    } else if (t == "set") {
        Json* o = find(jstr(op, "id"));
        if (o && op.contains("f") && op["f"].is_object())
            for (auto kv = op["f"].begin(); kv != op["f"].end(); ++kv) {
                if (kv.value().is_null()) o->erase(kv.key());
                else (*o)[kv.key()] = kv.value();
            }
    } else if (t == "del") {
        std::string id = jstr(op, "id");
        for (size_t i = 0; i < list.size(); ++i)
            if (jstr(list[i], "id") == id) {
                list.erase(i);
                break;
            }
    } else if (t == "doc") {
        std::string k = jstr(op, "k");
        if (k.empty() || k == "objects" || k == "id") return;
        if (!op.contains("v") || op["v"].is_null()) doc.erase(k);
        else doc[k] = op["v"];
    }
}

std::string touched(const Json& op) {
    std::string t = jstr(op, "t");
    if (t == "doc") return "d:" + jstr(op, "k");
    if (t == "add") return "o:" + jstr(op.value("o", Json::object()), "id");
    return "o:" + jstr(op, "id");
}

}  // namespace LiveOps

// -------------------------------------------------------------- LiveSession

namespace {

Color parseColor(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return Color::white();
    auto v = std::strtoul(hex.c_str() + 1, nullptr, 16);
    return Color{static_cast<uint8_t>((v >> 16) & 255), static_cast<uint8_t>((v >> 8) & 255), static_cast<uint8_t>(v & 255), 255};
}

std::string randomHex(int n) {
    static std::mt19937_64 rng{std::random_device{}()};
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < n; ++i) s.push_back(hex[rng() & 15]);
    return s;
}

}  // namespace

LiveSession::LiveSession(OnlineService& online, std::string projectId, fs::path queueFile)
    : online_(online), projectId_(std::move(projectId)), queueFile_(std::move(queueFile)) {
    loadQueue();
}

LiveSession::~LiveSession() { leave(); }

std::string LiveSession::myId() const { return online_.playerId(); }

const LiveSession::Participant* LiveSession::participant(const std::string& id) const {
    for (const auto& p : participants_)
        if (p.id == id) return &p;
    return nullptr;
}

void LiveSession::setSelection(const std::set<std::string>& ids) {
    selection_.assign(ids.begin(), ids.end());
    if (selection_.size() > 200) selection_.resize(200);
}

void LiveSession::join() {
    if (joinBusy_ || left_) return;
    joinBusy_ = true;
    state_ = State::Joining;
    Json body = Json::object();
    if (viewCenter_) body["view"] = {{"x", viewCenter_->x}, {"y", viewCenter_->y}, {"zoom", viewZoom_}};
    online_.send("POST", "/api/v1/projects/" + projectId_ + "/live/join", body,
                 OnlineService::guard(life_, [this](const ApiResult& r) { onJoin(r); }));
}

void LiveSession::onJoin(const ApiResult& r) {
    joinBusy_ = false;
    if (!r.ok) return failed(r);
    Json doc = r.data.value("doc", Json::object());
    // My edits not confirmed yet (made offline) stay visible: they are sent again.
    for (auto& q : queue_) {
        LiveOps::apply(doc, q.op);
        q.sent = false;
    }
    applied_ = jget<int>(r.data, "seq", 0);
    revision_ = jget<int>(r.data, "revision", 0);
    epoch_ = jget<int>(r.data, "epoch", 0);
    acked_.clear();
    remote_.clear();
    joined_ = std::move(doc);
    presenceKnown_ = false;
    absorb(r.data);
    state_ = State::Live;
    error_.clear();
    beatTimer_ = 0.6f;
}

void LiveSession::failed(const ApiResult& r) {
    if (r.networkFailure() || r.status >= 500 || r.status == 429) {
        state_ = state_ == State::Joining ? State::Joining : State::Offline;
        error_ = r.message;
        retryTimer_ = 3.0f;
        return;
    }
    // No access any more (removed from the project, role changed, project deleted).
    state_ = State::Failed;
    error_ = r.message;
}

void LiveSession::leave() {
    if (left_) return;
    left_ = true;
    saveQueue();
    if (state_ == State::Live) online_.send("POST", "/api/v1/projects/" + projectId_ + "/live/leave", Json::object(), [](const ApiResult&) {});
}

void LiveSession::update(float dt) {
    if (left_ || state_ == State::Failed) return;
    if (state_ == State::Joining) {
        retryTimer_ -= dt;
        if (!joinBusy_ && retryTimer_ <= 0.0f) {
            retryTimer_ = 3.0f;
            join();
        }
        return;
    }
    beatTimer_ -= dt;
    if (beatTimer_ <= 0.0f && !beatBusy_) {
        beatTimer_ = state_ == State::Live ? 0.6f : 3.0f;
        heartbeat();
    }
    if (state_ == State::Live && !opsBusy_ && !queue_.empty()) flush();
}

void LiveSession::heartbeat() {
    beatBusy_ = true;
    Json body = {{"since_seq", applied_}, {"epoch", epoch_}, {"selection", selection_}, {"activity", activity_}};
    body["cursor"] = cursor_ ? Json{{"x", cursor_->x}, {"y", cursor_->y}} : Json(nullptr);
    if (viewCenter_) body["view"] = {{"x", viewCenter_->x}, {"y", viewCenter_->y}, {"zoom", viewZoom_}};
    online_.send("POST", "/api/v1/projects/" + projectId_ + "/live/heartbeat", body, OnlineService::guard(life_, [this](const ApiResult& r) {
                     beatBusy_ = false;
                     if (!r.ok) return failed(r);
                     if (state_ == State::Offline) {
                         // Back online: the room may have restarted, join again (my edits are kept).
                         state_ = State::Joining;
                         retryTimer_ = 0.0f;
                         return;
                     }
                     if (jget<bool>(r.data, "throttled", false)) return;
                     if (jget<bool>(r.data, "resync", false)) {
                         state_ = State::Joining;
                         retryTimer_ = 0.0f;
                         return;
                     }
                     revision_ = jget<int>(r.data, "revision", revision_);
                     absorb(r.data);
                 }));
}

void LiveSession::absorb(const Json& data) {
    if (data.contains("participants") && data["participants"].is_array()) {
        std::set<std::string> before;
        for (const auto& p : participants_)
            if (!p.me) before.insert(p.id);
        participants_.clear();
        for (const auto& p : data["participants"]) {
            Participant x;
            x.id = jstr(p, "id");
            x.name = jstr(p, "username");
            x.avatar = jstr(p, "avatar", "avatar_01");
            x.role = jstr(p, "role");
            x.activity = jstr(p, "activity");
            x.color = parseColor(jstr(p, "color"));
            x.me = jget<bool>(p, "me", false);
            x.idleSeconds = jget<double>(p, "idle_ms", 0.0) / 1000.0;
            if (p.contains("cursor") && p["cursor"].is_object()) x.cursor = Vec2{jget<float>(p["cursor"], "x", 0), jget<float>(p["cursor"], "y", 0)};
            if (p.contains("view") && p["view"].is_object()) {
                x.viewCenter = Vec2{jget<float>(p["view"], "x", 0), jget<float>(p["view"], "y", 0)};
                x.viewZoom = jget<float>(p["view"], "zoom", 1.0f);
            }
            if (p.contains("selection") && p["selection"].is_array())
                for (const auto& s : p["selection"])
                    if (s.is_string()) x.selection.push_back(s.get<std::string>());
            known_[x.id] = {x.name, x.color};
            participants_.push_back(std::move(x));
        }
        // Arrivals and departures since the last presence (not on the first one after joining).
        std::set<std::string> now;
        for (const auto& p : participants_)
            if (!p.me) now.insert(p.id);
        if (presenceKnown_) {
            for (const auto& id : now)
                if (!before.count(id)) events_.push_back({Event::Kind::Joined, id, known_[id].first, known_[id].second, 0});
            for (const auto& id : before)
                if (!now.count(id)) events_.push_back({Event::Kind::Left, id, known_[id].first, known_[id].second, 0});
        }
        presenceKnown_ = true;
    }
    if (data.contains("locks") && data["locks"].is_object()) {
        locks_.clear();
        const std::string me = myId();
        for (auto it = data["locks"].begin(); it != data["locks"].end(); ++it)
            if (it.value().is_string() && it.value().get<std::string>() != me) locks_[it.key()] = it.value().get<std::string>();
    }
    if (data.contains("ops") && data["ops"].is_array()) readOps(data["ops"]);
}

void LiveSession::readOps(const Json& ops) {
    std::vector<std::pair<std::string, int>> byAuthor;  // in the order of their first edit
    for (const auto& o : ops) {
        int seq = jget<int>(o, "seq", 0);
        if (seq <= applied_) continue;
        std::string id = jstr(o, "id");
        if (acked_.erase(id)) {
            // Mine, already applied here.
        } else if (auto it = std::find_if(queue_.begin(), queue_.end(), [&](const Queued& q) { return q.id == id; }); it != queue_.end()) {
            queue_.erase(it);  // mine, accepted although its answer was lost
            saveQueue();
        } else if (o.contains("op")) {
            remote_.push_back(o["op"]);
            std::string author = jstr(o, "author");
            auto it = std::find_if(byAuthor.begin(), byAuthor.end(), [&](const auto& a) { return a.first == author; });
            if (it == byAuthor.end()) byAuthor.push_back({author, 1});
            else ++it->second;
        }
        applied_ = seq;
    }
    for (const auto& [author, n] : byAuthor) {
        auto k = known_.find(author);
        events_.push_back({Event::Kind::Edits, author, k != known_.end() ? k->second.first : std::string(), k != known_.end() ? k->second.second : Color::white(), n});
    }
}

std::vector<LiveSession::Event> LiveSession::takeEvents() {
    std::vector<Event> e;
    e.swap(events_);
    return e;
}

void LiveSession::submit(const std::vector<Json>& ops) {
    for (const auto& op : ops) {
        Queued q;
        q.id = "c" + std::to_string(++counter_) + "-" + randomHex(16);
        q.base = applied_;
        q.op = op;
        queue_.push_back(std::move(q));
    }
    if (!ops.empty()) saveQueue();
}

void LiveSession::flush() {
    Json list = Json::array();
    std::vector<std::string> ids;
    for (auto& q : queue_) {
        if (list.size() >= 50) break;
        q.sent = true;
        list.push_back({{"id", q.id}, {"base_seq", q.base}, {"op", q.op}});
        ids.push_back(q.id);
    }
    opsBusy_ = true;
    online_.send("POST", "/api/v1/projects/" + projectId_ + "/live/ops", {{"ops", list}}, OnlineService::guard(life_, [this, ids](const ApiResult& r) {
                     opsBusy_ = false;
                     if (!r.ok) {
                         for (auto& q : queue_)
                             if (std::find(ids.begin(), ids.end(), q.id) != ids.end()) q.sent = false;
                         if (r.status == 403) {
                             // My role no longer allows editing: every pending edit is refused.
                             for (auto& q : queue_) rejections_.push_back({"forbidden", "", q.op, Json()});
                             queue_.clear();
                             saveQueue();
                         }
                         return failed(r);
                     }
                     for (const auto& res : r.data.value("results", Json::array())) {
                         std::string id = jstr(res, "id");
                         auto it = std::find_if(queue_.begin(), queue_.end(), [&](const Queued& q) { return q.id == id; });
                         if (it == queue_.end()) continue;  // already confirmed by a heartbeat
                         std::string status = jstr(res, "status");
                         if (status == "ok") {
                             int seq = jget<int>(res, "seq", 0);
                             if (seq > applied_) acked_[id] = seq;
                         } else {
                             rejections_.push_back({status, jstr(res, "by"), it->op, res.value("current", Json())});
                         }
                         queue_.erase(it);
                     }
                     saveQueue();
                 }));
}

std::optional<Json> LiveSession::takeJoinedDocument() {
    auto d = std::move(joined_);
    joined_.reset();
    return d;
}

std::vector<Json> LiveSession::takeRemoteOps() {
    std::vector<Json> out;
    out.swap(remote_);
    return out;
}

std::vector<LiveSession::Rejection> LiveSession::takeRejections() {
    std::vector<Rejection> out;
    out.swap(rejections_);
    return out;
}

void LiveSession::saveQueue() const {
    if (queueFile_.empty()) return;
    Json list = Json::array();
    for (const auto& q : queue_) list.push_back({{"id", q.id}, {"base", q.base}, {"op", q.op}});
    std::error_code ec;
    fs::create_directories(queueFile_.parent_path(), ec);
    if (list.empty()) fs::remove(queueFile_, ec);
    else FileUtil::writeTextAtomic(queueFile_, list.dump());
}

void LiveSession::loadQueue() {
    if (queueFile_.empty()) return;
    auto t = FileUtil::readText(queueFile_);
    if (!t) return;
    Json j = Json::parse(*t, nullptr, false);
    if (!j.is_array()) return;
    for (const auto& q : j) {
        if (!q.is_object() || !q.contains("op")) continue;
        queue_.push_back({jstr(q, "id"), jget<int>(q, "base", 0), q["op"], false});
    }
}

}  // namespace cosmic
