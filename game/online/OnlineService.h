#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/DeviceIdentity.h"
#include "game/online/LatencyMeter.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/LevelRatings.h"
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
        enum class Kind { Info, Success, Warning, Error, Record, Friend, Message, Workshop };
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
    /// 2.72: the round trip to the server in milliseconds (« En ligne - 25ms »): the median of
    /// the last light requests timed on the network thread (LatencyMeter); -1 while none was
    /// measured or when the server is not reachable now.
    int latencyMs() const { return state() == OnlineState::Online ? latency_.ms() : -1; }
    const OnlineConfig& config() const { return config_; }
    const OnlineSettings& settings() const { return settings_; }
    bool usable() const;  // enabled, configured and compiled with HTTP
    bool loggedIn() const { return !tokens_.refreshToken.empty(); }
    const Json& profile() const { return profile_; }
    std::string playerId() const { return tokens_.playerId; }
    /// The account has the administrator role (2.40, from the server's profile): only shows the
    /// ADMINISTRATION entry; the server checks the role again on every admin request.
    bool isAdmin() const { return loggedIn() && profile_.is_object() && jstr(profile_, "role") == "admin"; }
    std::string username() const { return tokens_.username; }
    /// 2.47: this computer (its random id, its name) and the account it is bound to: one
    /// account per computer, an account usable on its own computer only (docs/online/DEVICES.md).
    const DeviceIdentity& device() const { return device_; }
    /// Renames this computer (here, and on the server when connected).
    void renameDevice(const std::string& name, ApiCallback cb = {});
    /// Called after a login or a registration (the session is open), and after a logout.
    std::function<void()> onSessionStarted;
    std::function<void()> onLoggedOut;
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
    /// Friends of the player (last known list).
    int friendCount() const { return static_cast<int>(friendIds_.size()); }
    const std::set<std::string>& friendIds() const { return friendIds_; }
    /// Reads the friend list now. It is also read every minute while connected: a new
    /// request, or a request of yours that was accepted, is notified once.
    void refreshFriends(ApiCallback cb = {});
    /// A screen that loaded GET /friends itself shares the answer (badges, notifications).
    void noteFriends(const Json& data);

    // --------------------------------------------------------------- social
    /// Unread messages of the player's conversations (last known), for the badges.
    int unreadMessages() const { return unreadMessages_; }
    /// The same, split between the private conversations and the groups.
    int unreadDirect() const { return unreadDirect_; }
    int unreadGroups() const { return unreadGroups_; }
    /// Unread messages of one conversation (last known; 0 when none).
    int unreadIn(const std::string& conversationId) const;
    /// Changes each time the counters above change (screens refresh their lists).
    int unreadRevision() const { return unreadRevision_; }
    /// A conversation read on screen: its counter drops to 0 at once, before the next read.
    void noteConversationRead(const std::string& conversationId);
    /// Reads GET /social/unread now. It is also read every 30 seconds while connected
    /// (the first time a second after the connection): each new message is notified
    /// once, except in the conversation open on screen.
    void refreshUnread(ApiCallback cb = {});
    /// The conversation shown by the SOCIAL screen ("" when none).
    void setOpenConversation(const std::string& id) { openConversation_ = id; }
    const std::string& openConversation() const { return openConversation_; }
    /// A screen that loaded GET /social/unread itself shares the answer.
    void noteUnread(const Json& data);

    // ---------------------------------------------------------- clans (2.66)
    /// The player's clan as last read (GET /clans/me: {clan, invitations, applications}), from the
    /// network or, offline, from the cache; null before the first answer and without an account.
    const Json& myClan() const { return myClan_; }
    /// The tag of the player's clan ("" without one, or while it is unknown).
    std::string myClanTag() const;
    /// The invitations to a clan the player received (last known), for the badge of CLANS.
    int clanInvitations() const;
    /// Reads GET /clans/me now (kept in the cache for offline display). Also read a moment after
    /// each login, then every five minutes while connected.
    void refreshMyClan(ApiCallback cb = {});
    /// A screen that read GET /clans/me, or changed the clan (created, joined, left…), shares it.
    void noteMyClan(const Json& data);

    // -------------------------------------------------------- notifications
    /// Unread notifications of the Cosmic Online centre (last known), for the badges.
    int unreadNotifications() const { return unreadNotifications_; }
    /// Reads the newest unread notifications now (also every minute while connected): the
    /// new ones are announced once by a toast.
    void refreshNotifications(ApiCallback cb = {});
    /// A screen that loaded GET /notifications (or marked some read) shares the counter.
    void noteNotificationsUnread(int unread) { unreadNotifications_ = std::max(0, unread); }

    // ------------------------------------------------------------ time played
    /// Time played in a level (official id, community level id, or "other"), counted on
    /// the player's online profile. Kept on disk until sent (only while logged in; the
    /// server caps it by the real time elapsed between two reports).
    void notePlaytime(const std::string& levelKey, double seconds);
    /// The achievements unlocked in this game (ids). Sent to the profile when the player is
    /// connected (and again after a change or another login): shown as declared by the game.
    void setAchievements(std::vector<std::string> ids);
    /// 2.50: while the progression of an old account migrated here waits to be restored, the
    /// list is ADDED to the account's (`merge: true`) instead of replacing it: the achievements
    /// the migration brought stay on the profile until the restoration brings them here too.
    void setAchievementsMerge(bool merge) { achievementsMerge_ = merge; }
    /// What the player wears (2.23): category → equipped item id (suit, trail, frame, title,
    /// victory). Sent to the profile once the achievements of the same player are there (the
    /// server checks the ids against them), again after each change or another login, a
    /// minute later after a failure; never while offline or logged out.
    void setCosmetics(std::map<std::string, std::string> equipped);
    /// Body of PUT /players/me/cosmetics for these ids (tests).
    static Json cosmeticsBody(const std::map<std::string, std::string>& equipped);
    /// Profile badges (2.41): the achievement ids shown on the profile, in their order (only
    /// those the game's own stars allow, shownBadges()). Sent like the items worn: once the
    /// achievements of the same player are there (the server counts the stars and checks the
    /// ids itself), again after each change or another login; never while offline or logged
    /// out (the choice stays in the Collection).
    void setBadges(std::vector<std::string> ids);
    /// Where the badges stand online: Local (logged out, offline, not sent yet), Published
    /// (the profile shows them), Refused (the server said no: badgesError(), not sent again
    /// until the choice or the achievements change).
    enum class BadgesState { Local, Published, Refused };
    BadgesState badgesState() const;
    const std::string& badgesError() const { return badgesError_; }
    /// Seconds waiting to be sent (tests, settings).
    double pendingPlaytime() const;
    void flushPlaytime(ApiCallback cb = {});

    // ------------------------------------------------------ campaign ratings
    /// Players' ratings of the official levels (2.19): the last averages received, kept on
    /// disk (online/level_ratings.json) and shown offline as received earlier. Read from the
    /// server only while logged in.
    const LevelRatingCache& levelRatings() const { return levelRatings_; }
    /// Reads GET /levels/ratings (logged in only; at most every 10 minutes unless `force`).
    void refreshLevelRatings(bool force = false, ApiCallback cb = {});
    /// A screen received a rating view (GET / PUT / DELETE /levels/{id}/rating): cached.
    void noteLevelRating(const Json& view);

    // ------------------------------------------------------ community levels
    /// Community level installed from ONLINE LEVELS under this local id: {id, version,
    /// level_hash, title, verified} (null when it is not one).
    const Json* communityInstall(const std::string& localLevelId) const;
    void noteCommunityInstall(const std::string& localLevelId, const Json& info);
    /// Every one (local id → info): copied for a search off the main thread (co-op lobby).
    const Json& communityInstalls() const { return communityInstalls_; }
    /// A completed run of an installed community level (replay attached): sent to its
    /// leaderboard; the result is notified (rank, or why the time was refused).
    /// An attempt of an EXTRÊME level's « course à la vérification »: declared, then its
    /// replay sent; the server computes the score (the game's value is only an estimate).
    struct RaceAttempt {
        std::string kind;       // "official" or "community"
        std::string levelId;    // official level id, or the community level's online id
        std::string levelHash;  // official: the ranked fingerprint
        int version = 0;        // community: the published version
        std::string title, attemptId;
        int64_t timeMs = 0;
        bool finished = false;
        std::vector<int> bosses;  // per mille of health removed, per boss of the layout
        std::vector<int> waves;   // per ambush of the layout
        float estimate = 0.0f;
    };
    void submitRaceAttempt(RaceAttempt a, std::vector<uint8_t> replay);
    /// 2.62: a team's attempt (Duo / Trio, docs/COOP.md § 6): `mode` "online", "lan" or "same_pc",
    /// `players` their account ids by seat; the replay is a team replay (format 5). The team is
    /// ranked once each player confirmed it (confirmCoopAttempt, from their own game).
    void submitCoopAttempt(RaceAttempt a, const std::string& mode, std::vector<std::string> players, std::vector<uint8_t> replay);
    void confirmCoopAttempt(const std::string& attemptId);
    void submitCommunityRun(const std::string& localLevelId, int64_t timeMs, int deaths, const std::string& attemptId,
                            std::vector<uint8_t> replay);
    /// PUT / POST of raw bytes with the session (packages, replays).
    void sendBytes(const std::string& method, const std::string& path, std::string bytes, ApiCallback cb, int timeoutMs = 90000);

    // ----------------------------------------------------------------- data
    /// GET with the session when logged in. On network failure the last cached answer is
    /// delivered instead, flagged `fromCache` with its age.
    void get(const std::string& pathAndQuery, ApiCallback cb, bool useCache = true);
    /// `timeoutMs`: 0 = the default (15 s); long server operations (backups) give more.
    void send(const std::string& method, const std::string& path, const Json& body, ApiCallback cb, int timeoutMs = 0);
    /// The same with a body already written out as JSON text (2.55: the copy of the progression,
    /// written out off the main thread).
    void sendJsonText(const std::string& method, const std::string& path, std::string json, ApiCallback cb, int timeoutMs = 0);
    /// `received` (2.63.5): the bytes of the body received so far, updated by the transfer's
    /// thread (a progress bar), when given.
    void getBinary(const std::string& path, std::function<void(const HttpResponse&)> cb, size_t maxBytes = 2 * 1024 * 1024,
                   std::shared_ptr<std::atomic<size_t>> received = nullptr);
    /// POST of a JSON body whose answer is a file (the encrypted backups, ADMINISTRATION ›
    /// Sauvegardes): the session is renewed first when needed; errors come back as JSON.
    void postForBinary(const std::string& path, const Json& body, std::function<void(const HttpResponse&)> cb, size_t maxBytes,
                       int timeoutMs = 600000);
    std::optional<OnlineCache::Entry> cached(const std::string& pathAndQuery);
    size_t clearCache();
    void checkConnection(ApiCallback cb = {});
    /// 2.62: a fresh access token for a connection the game opens itself (the WebSocket of a
    /// team's relay): `cb(token, serverUrl)` once renewed (an empty token when not logged in).
    void freshAccess(std::function<void(const std::string& token, const std::string& serverUrl)> cb);
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
    /// Something the achievements count happened online ("race_verified"), set by the game.
    std::function<void(const std::string&)> onAchievementEvent;

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
    LatencyMeter latency_;  // 2.72: the samples of latencyMs()
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
    std::set<std::string> friendIds_;
    /// Per player: ids seen in the last friend list (to notify only what is new), and
    /// under "<id>/messages" the date of the newest message already notified.
    Json friendNotices_ = Json::object();
    void saveFriendNotices();

    Json communityInstalls_ = Json::object();
    void saveCommunityInstalls();

    LevelRatingCache levelRatings_;
    bool ratingsBusy_ = false;
    double ratingsReadAt_ = -1e9;  // steady seconds of the last successful read
    void saveLevelRatings();

    float socialTimer_ = 1.0f;
    bool socialBusy_ = false;
    int unreadMessages_ = 0;
    int unreadDirect_ = 0;
    int unreadGroups_ = 0;
    int unreadRevision_ = 0;
    std::map<std::string, int> unreadByConversation_;
    std::set<std::string> unreadGroupIds_;  // which of them are groups
    std::string openConversation_;

    Json myClan_;  // 2.66: GET /clans/me
    bool clanBusy_ = false;
    float clanTimer_ = 2.0f;

    float notificationsTimer_ = 4.0f;
    bool notificationsBusy_ = false;
    int unreadNotifications_ = 0;
    void noteNotifications(const Json& data);

    /// {"player": id, "levels": {key: seconds}} (online/playtime.json).
    Json playtime_ = Json::object();
    float playtimeTimer_ = 20.0f;
    DeviceIdentity device_;  // 2.47 (online/device.json)
    std::vector<std::string> achievements_;
    bool achievementsMerge_ = false;
    std::string achievementsSentFor_;  // player whose profile has the current list
    bool achievementsBusy_ = false;
    float achievementsTimer_ = 5.0f;
    unsigned achievementsGen_ = 0;  // a change while a list is on its way: sent again
    std::map<std::string, std::string> cosmetics_;
    std::string cosmeticsSentFor_;  // player whose profile wears the current items
    bool cosmeticsBusy_ = false;
    float cosmeticsTimer_ = 3.0f;
    unsigned cosmeticsGen_ = 0;
    std::vector<std::string> badges_;
    bool badgesSet_ = false;      // the game said what to show (else nothing is sent)
    std::string badgesSentFor_;   // player whose profile has the current badges (or refused them)
    std::string badgesError_;     // the server's refusal ("" when accepted)
    bool badgesBusy_ = false;
    float badgesTimer_ = 3.0f;
    unsigned badgesGen_ = 0;
    bool playtimeBusy_ = false;
    void savePlaytime();

    std::deque<Notification> notifications_;
    std::vector<RankChange> rankChanges_;
    Json knownRanks_ = Json::object();
};

}  // namespace cosmic
