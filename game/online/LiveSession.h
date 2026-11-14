#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"

namespace cosmic {

class OnlineService;
struct ApiResult;

/// Operations on a level document (the JSON of LevelData), exactly as the server applies
/// them (server/src/live.ts): {t:"add", o}, {t:"set", id, f} (null removes a field),
/// {t:"del", id}, {t:"doc", k, v} (a top-level key other than "objects" and "id").
namespace LiveOps {
/// The operations that turn `before` into `after` (objects by id, other keys whole).
std::vector<Json> diff(const Json& before, const Json& after);
void apply(Json& doc, const Json& op);
/// "o:<id>" or "d:<key>": what an operation touches.
std::string touched(const Json& op);
}  // namespace LiveOps

/// Live editing of a collaborative project (Cosmic Online): presence (cursor, view,
/// selection, activity) sent with a heartbeat, the other members' presence and temporary
/// locks received, local edits sent as operations (kept on disk until the server answered,
/// so nothing is lost offline), the other members' operations received in the server's
/// order. A refused operation (conflict, lock, deleted object) is reported to the editor
/// with the server's current state: it is never overwritten silently, nor lost.
/// Main thread only; every request goes through OnlineService (never blocks).
class LiveSession {
public:
    struct Participant {
        std::string id, name, avatar, role, activity;
        Color color = Color::white();
        std::optional<Vec2> cursor;
        std::optional<Vec2> viewCenter;
        float viewZoom = 1.0f;
        std::vector<std::string> selection;
        bool me = false;
        double idleSeconds = 0.0;
    };
    struct Rejection {
        std::string status;  // conflict, locked, not_found, id_taken, invalid
        std::string by;      // who changed or holds it
        Json op;             // my operation
        Json current;        // the server's state of what it touched (null if gone)
    };
    /// What the others did, for the editor's grouped notifications: arrivals, departures,
    /// and the number of their edits received at once (one event per member and batch).
    struct Event {
        enum class Kind { Joined, Left, Edits };
        Kind kind = Kind::Edits;
        std::string id, name;
        Color color = Color::white();
        int count = 0;
    };
    enum class State { Joining, Live, Offline, Failed };

    LiveSession(OnlineService& online, std::string projectId, fs::path queueFile = {});
    ~LiveSession();
    LiveSession(const LiveSession&) = delete;
    LiveSession& operator=(const LiveSession&) = delete;

    /// Asks to join; the document arrives with takeJoinedDocument().
    void join();
    void leave();
    void update(float dt);

    // What I show the others.
    void setCursor(std::optional<Vec2> world) { cursor_ = world; }
    void setView(Vec2 center, float zoom) {
        viewCenter_ = center;
        viewZoom_ = zoom;
    }
    void setSelection(const std::set<std::string>& ids);
    void setActivity(const std::string& text) { activity_ = text.substr(0, 80); }

    /// Local edits (already applied by the editor) to send, in order.
    void submit(const std::vector<Json>& ops);

    /// The document to load after a (re)join, once.
    std::optional<Json> takeJoinedDocument();
    /// Operations of the other members, in the server's order, to apply.
    std::vector<Json> takeRemoteOps();
    std::vector<Rejection> takeRejections();
    std::vector<Event> takeEvents();

    State state() const { return state_; }
    const std::string& error() const { return error_; }
    /// HTTP status that ended the session (404: no longer a member, or the project is gone).
    int failedStatus() const { return failedStatus_; }
    /// My role as the room sees it now (changes when an administrator changes it).
    std::string myRole() const;
    /// Id of the project's latest chat message (0: none): new ones are fetched when it moves.
    int64_t chatSeq() const { return chatSeq_; }
    const std::vector<Participant>& participants() const { return participants_; }
    /// Objects selected by another member: id -> member id.
    const std::map<std::string, std::string>& locks() const { return locks_; }
    const Participant* participant(const std::string& id) const;
    std::string myId() const;
    int seq() const { return applied_; }
    int revision() const { return revision_; }
    /// My operations not confirmed by the server yet.
    size_t pending() const { return queue_.size(); }

private:
    struct Queued {
        std::string id;
        int base = 0;
        Json op;
        bool sent = false;
    };
    void heartbeat();
    void flush();
    void onJoin(const ApiResult& r);
    void absorb(const Json& data);
    void readOps(const Json& ops);
    void saveQueue() const;
    void loadQueue();
    void failed(const ApiResult& r);

    OnlineService& online_;
    std::string projectId_;
    fs::path queueFile_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    State state_ = State::Joining;
    std::string error_;
    bool joinBusy_ = false, beatBusy_ = false, opsBusy_ = false, left_ = false;
    float beatTimer_ = 0.0f, retryTimer_ = 0.0f;
    int applied_ = 0, revision_ = 0, epoch_ = 0, failedStatus_ = 0;
    int64_t chatSeq_ = 0;
    std::optional<Json> joined_;
    std::vector<Json> remote_;
    std::vector<Rejection> rejections_;
    std::vector<Event> events_;
    std::map<std::string, std::pair<std::string, Color>> known_;  // member id -> name, colour (kept after they leave)
    bool presenceKnown_ = false;  // false until the first presence after a (re)join: no arrivals for those already there
    std::vector<Participant> participants_;
    std::map<std::string, std::string> locks_;
    std::deque<Queued> queue_;
    std::map<std::string, int> acked_;  // my operation id -> seq, until the heartbeat brings it back
    std::optional<Vec2> cursor_, viewCenter_;
    float viewZoom_ = 1.0f;
    std::vector<std::string> selection_;
    std::string activity_;
    int counter_ = 0;
};

}  // namespace cosmic
