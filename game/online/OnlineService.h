#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineCache.h"
#include "game/online/OnlineClient.h"
#include "game/online/OnlineRecord.h"
#include "game/online/OnlineSession.h"
#include "game/online/OnlineTypes.h"
#include "game/online/Replay.h"
#include "saves/Settings.h"

namespace cosmic {

/// Entry point of Cosmic Online for the rest of the game. Main thread only: every call
/// returns immediately and results arrive through callbacks run by update(). It owns the
/// network worker, the session (with automatic token refresh), the offline cache, the
/// queue of local performances and the in-game notifications. Nothing is sent to the
/// server unless the player uses an online feature or is logged in.
class OnlineService {
public:
    struct Notification {
        enum class Kind { Info, Success, Warning, Error, Record, Friend, Message };
        Kind kind = Kind::Info;
        std::string title;
        std::string text;
    };
    /// A verified record moved the player on a leaderboard (rank animation, globe glow).
    struct RankChange {
        std::string levelId;
        RunCategory category = RunCategory::AnyPercent;
        int previousRank = 0;  // 0 = was not ranked
        int rank = 0;
        int64_t timeMs = 0;
        bool personalBest = false;
    };

    OnlineService();
    ~OnlineService();
    OnlineService(const OnlineService&) = delete;
    OnlineService& operator=(const OnlineService&) = delete;

    /// `transport` null: the platform client (WinHTTP / libcurl). `userDir` holds the
    /// session file, the cache and the replays.
    void init(const OnlineSettings& settings, const std::string& serverOverride = {}, std::unique_ptr<IHttpTransport> transport = nullptr,
              fs::path userDir = {});
    void setSettings(const OnlineSettings& settings);
    /// Main thread, every frame: delivers network results, reconnection, periodic sync.
    void update(float dt);
    void shutdown();

    // ---------------------------------------------------------------- state
    OnlineState state() const;
    const OnlineConfig& config() const { return config_; }
    const OnlineSettings& settings() const { return settings_; }
    bool usable() const;  // enabled, configured and compiled with HTTP
    bool loggedIn() const { return !tokens_.refreshToken.empty(); }
    const Json& profile() const { return profile_; }
    std::string playerId() const { return tokens_.playerId; }
    std::string username() const { return tokens_.username; }
    const std::string& lastError() const { return lastError_; }
    size_t requestsInFlight() const { return client_ ? client_->pending() : 0; }
    const char* transportName() const { return client_ ? client_->transportName() : "aucun"; }

    // -------------------------------------------------------------- account
    void registerAccount(const Json& form, bool remember, ApiCallback cb);
    void login(const std::string& username, const std::string& password, bool remember, ApiCallback cb);
    void logout(bool allDevices, ApiCallback cb = {});
    bool hasSavedSession() const;
    void resumeSavedSession(ApiCallback cb = {});
    void refreshProfile(ApiCallback cb = {});
    void updateProfile(const Json& patch, ApiCallback cb);
    void deleteAccount(const std::string& password, ApiCallback cb);
    void exportData(ApiCallback cb);
    /// Asks the server whether `username` can be registered (same rules as the
    /// registration; never cached). data: {available, reason, message}.
    void checkUsername(const std::string& username, ApiCallback cb);

    // -------------------------------------------------------------- friends
    /// Friend requests received and not answered yet (last known), for the badges.
    int pendingFriendRequests() const { return pendingFriendRequests_; }
    /// Reads the friend list now. It is also read every minute while connected: a new
    /// request, or a request of yours that was accepted, is notified once.
    void refreshFriends(ApiCallback cb = {});
    /// A screen that loaded GET /friends itself shares the answer (badges, notifications).
    void noteFriends(const Json& data);

    // --------------------------------------------------------------- social
    /// Unread messages of the player's conversations (last known), for the badges.
    int unreadMessages() const { return unreadMessages_; }
    /// Reads GET /social/unread now. It is also read every 30 seconds while connected:
    /// each new message is notified once, except in the conversation open on screen.
    void refreshUnread(ApiCallback cb = {});
    /// The conversation shown by the SOCIAL screen ("" when none).
    void setOpenConversation(const std::string& id) { openConversation_ = id; }
    const std::string& openConversation() const { return openConversation_; }
    /// A screen that loaded GET /social/unread itself shares the answer.
    void noteUnread(const Json& data);

    // ------------------------------------------------------ community levels
    /// Community level installed from ONLINE LEVELS under this local id: {id, version,
    /// level_hash, title, verified} (null when it is not one).
    const Json* communityInstall(const std::string& localLevelId) const;
    void noteCommunityInstall(const std::string& localLevelId, const Json& info);
    /// A completed run of an installed community level (replay attached): sent to its
    /// leaderboard; the result is notified (rank, or why the time was refused).
    void submitCommunityRun(const std::string& localLevelId, int64_t timeMs, int deaths, const std::string& attemptId,
                            std::vector<uint8_t> replay);
    /// PUT / POST of raw bytes with the session (packages, replays).
    void sendBytes(const std::string& method, const std::string& path, std::string bytes, ApiCallback cb, int timeoutMs = 90000);

    // ----------------------------------------------------------------- data
    /// GET with the session when logged in. On network failure the last cached answer is
    /// delivered instead, flagged `fromCache` with its age.
    void get(const std::string& pathAndQuery, ApiCallback cb, bool useCache = true);
    void send(const std::string& method, const std::string& path, const Json& body, ApiCallback cb);
    void getBinary(const std::string& path, std::function<void(const HttpResponse&)> cb, size_t maxBytes = 2 * 1024 * 1024);
    std::optional<OnlineCache::Entry> cached(const std::string& pathAndQuery);
    size_t clearCache();
    void checkConnection(ApiCallback cb = {});
    std::string cacheKey(const std::string& pathAndQuery) const;

    // -------------------------------------------------------------- records
    AttemptStore& attempts() { return attempts_; }
    LevelFingerprints& fingerprints() { return fingerprints_; }
    /// Called when a level is completed: keeps the run locally (with its replay) and sends
    /// it if the player is connected and automatic sync is on.
    OnlineAttempt* recordRun(const RunCandidate& run, const std::string& attemptId, const ReplayData* replay);
    /// Sends what is waiting and refreshes PENDING statuses. `claimUnowned` also sends
    /// runs finished while logged out (explicit "SUBMIT RECORD").
    void syncNow(bool claimUnowned = false);
    bool syncing() const { return syncing_; }
    int unsentCount() const;
    int pendingVerificationCount() const;
    /// Tells the service the player's rank seen on a board (notifies a record beaten by
    /// someone else, new players on the board).
    void observeBoard(const std::string& boardKey, int myRank, int total);

    // ------------------------------------------------------------ interface
    std::vector<Notification> takeNotifications();
    std::vector<RankChange> takeRankChanges();
    void notify(Notification::Kind kind, std::string title, std::string text);

    /// Wraps a callback so that it is dropped if `owner` (a screen) no longer exists.
    template <typename F>
    static ApiCallback guard(const std::shared_ptr<int>& owner, F f) {
        std::weak_ptr<int> w = owner;
        return [w, f](const ApiResult& r) {
            if (w.lock()) f(r);
        };
    }

private:
    void call(const std::string& method, const std::string& path, std::string body, const std::string& contentType, bool auth,
              ApiCallback cb, bool retried = false, int timeoutMs = 0);
    void withRefresh(std::function<void(const ApiResult&)> then);
    void sessionLost(const std::string& why);
    void applySession(const Json& data, bool remember);
    void noteResult(const ApiResult& r);
    ApiResult unusableResult() const;
    void syncStep();
    void finishSync();
    void applyRecordJson(OnlineAttempt& a, const Json& record, const Json& ranking);
    void saveAttempts();
    void loadKnownRanks();
    void loadFriendNotices();
    void resetFriends();

    std::unique_ptr<OnlineClient> client_;
    OnlineConfig config_;
    OnlineSettings settings_;
    SessionTokens tokens_;
    std::unique_ptr<SessionStore> store_;
    bool remember_ = true;
    Json profile_;
    OnlineCache cache_;
    AttemptStore attempts_;
    LevelFingerprints fingerprints_;
    fs::path userDir_;
    bool initialized_ = false;
    bool unsupported_ = false;

    OnlineState netState_ = OnlineState::Idle;
    std::string lastError_;
    float retryTimer_ = 0.0f;
    float retryDelay_ = 15.0f;
    float pollTimer_ = 0.0f;
    bool checking_ = false;

    bool refreshing_ = false;
    std::vector<std::function<void(const ApiResult&)>> afterRefresh_;

    bool syncing_ = false;
    bool syncClaim_ = false;
    bool syncSentSomething_ = false;
    std::set<std::string> syncPolled_;

    float friendsTimer_ = 3.0f;
    bool friendsBusy_ = false;
    int pendingFriendRequests_ = 0;
    /// Per player: ids seen in the last friend list (to notify only what is new), and
    /// under "<id>/messages" the date of the newest message already notified.
    Json friendNotices_ = Json::object();
    void saveFriendNotices();

    Json communityInstalls_ = Json::object();
    void saveCommunityInstalls();

    float socialTimer_ = 5.0f;
    bool socialBusy_ = false;
    int unreadMessages_ = 0;
    std::string openConversation_;

    std::deque<Notification> notifications_;
    std::vector<RankChange> rankChanges_;
    Json knownRanks_ = Json::object();
};

}  // namespace cosmic
