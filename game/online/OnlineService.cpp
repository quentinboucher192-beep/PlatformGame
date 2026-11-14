#include "game/online/OnlineService.h"

#include <algorithm>
#include <cctype>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

namespace {
constexpr float kMaxRetryDelay = 300.0f;
constexpr float kStatusPollSeconds = 90.0f;
constexpr float kFriendsPollSeconds = 60.0f;
constexpr float kSocialPollSeconds = 30.0f;

ApiResult localError(const std::string& code, const std::string& message) {
    ApiResult r;
    r.code = code;
    r.message = message;
    return r;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}
}  // namespace

OnlineService::OnlineService() = default;

OnlineService::~OnlineService() { shutdown(); }

void OnlineService::init(const OnlineSettings& settings, const std::string& serverOverride, std::unique_ptr<IHttpTransport> transport,
                         fs::path userDir) {
    settings_ = settings;
    userDir_ = userDir.empty() ? Paths::userDir() : userDir;
    config_ = OnlineConfig::load(serverOverride, settings.serverOverride);
    if (!transport) transport = makePlatformTransport();
    unsupported_ = std::string(transport->name()) == "aucun";
    client_ = std::make_unique<OnlineClient>(std::move(transport));
    store_ = std::make_unique<SessionStore>(userDir_ / "online" / "session.json");
    cache_ = OnlineCache(userDir_ / "online" / "cache");
    attempts_ = AttemptStore(userDir_ / "saves" / "online_attempts.json", userDir_ / "online" / "replays");
    attempts_.load();
    loadKnownRanks();
    loadFriendNotices();
    if (auto t = FileUtil::readText(userDir_ / "online" / "community_installs.json")) {
        Json j = Json::parse(*t, nullptr, false);
        if (j.is_object()) communityInstalls_ = j;
    }
    initialized_ = true;
    Log::info("Cosmic Online : serveur {} ({}), client HTTP {}", config_.serverUrl.empty() ? "non configuré" : config_.serverUrl,
              config_.source, client_->transportName());
    // Automatic reconnection only with a saved session and the player's consent.
    if (usable() && settings_.autoLogin && hasSavedSession()) resumeSavedSession();
}

void OnlineService::setSettings(const OnlineSettings& s) {
    bool serverChanged = s.serverOverride != settings_.serverOverride;
    settings_ = s;
    if (serverChanged) {
        config_ = OnlineConfig::load({}, s.serverOverride);
        tokens_.clear();
        profile_ = Json();
    }
    if (!s.enabled && client_) client_->cancelAll();
}

void OnlineService::shutdown() {
    if (!initialized_) return;
    if (client_) client_->cancelAll();
    client_.reset();  // joins the network thread (transfers are aborted)
    attempts_.save();
    initialized_ = false;
}

bool OnlineService::usable() const { return initialized_ && settings_.enabled && !config_.serverUrl.empty() && !unsupported_; }

OnlineState OnlineService::state() const {
    if (!settings_.enabled) return OnlineState::Disabled;
    if (unsupported_) return OnlineState::Unsupported;
    if (config_.serverUrl.empty()) return OnlineState::NotConfigured;
    return netState_;
}

ApiResult OnlineService::unusableResult() const {
    switch (state()) {
        case OnlineState::Disabled: return localError("ONLINE_DISABLED", "Cosmic Online est désactivé dans les paramètres.");
        case OnlineState::NotConfigured:
            return localError("NOT_CONFIGURED", "Aucun serveur Cosmic Online n'est configuré pour cette version du jeu.");
        case OnlineState::Unsupported: return localError("UNSUPPORTED", "Cette version du jeu a été compilée sans client HTTP.");
        default: return localError("UNAVAILABLE", "Service en ligne indisponible.");
    }
}

// ------------------------------------------------------------------- update

void OnlineService::update(float dt) {
    if (!initialized_ || !client_) return;
    client_->poll();
    if (!usable()) return;
    // Reconnection with exponential backoff, only when there is something to do.
    if (netState_ == OnlineState::Offline && !checking_ && (loggedIn() || unsentCount() > 0)) {
        retryTimer_ -= dt;
        if (retryTimer_ <= 0.0f) {
            retryTimer_ = retryDelay_;
            retryDelay_ = std::min(kMaxRetryDelay, retryDelay_ * 2.0f);
            checkConnection([this](const ApiResult& r) {
                if (!r.ok) return;
                if (loggedIn() && profile_.is_null()) refreshProfile();
                if (loggedIn() && settings_.autoSync) syncNow();
            });
        }
    }
    // Friend requests: read on connection, then every minute.
    if (netState_ == OnlineState::Online && loggedIn() && !friendsBusy_) {
        friendsTimer_ -= dt;
        if (friendsTimer_ <= 0.0f) {
            friendsTimer_ = kFriendsPollSeconds;
            refreshFriends();
        }
    }
    // Unread messages: read on connection, then every 30 seconds.
    if (netState_ == OnlineState::Online && loggedIn() && !socialBusy_) {
        socialTimer_ -= dt;
        if (socialTimer_ <= 0.0f) {
            socialTimer_ = kSocialPollSeconds;
            refreshUnread();
        }
    }
    // PENDING records are re-checked from time to time while connected.
    if (netState_ == OnlineState::Online && loggedIn() && settings_.autoSync && !syncing_) {
        pollTimer_ += dt;
        if (pollTimer_ >= kStatusPollSeconds) {
            pollTimer_ = 0.0f;
            if (pendingVerificationCount() > 0 || unsentCount() > 0) syncNow();
        }
    }
}

void OnlineService::noteResult(const ApiResult& r) {
    if (r.net == NetError::Cancelled) return;
    if (r.networkFailure() || r.status >= 500 || r.code == "MALFORMED_RESPONSE") {
        if (netState_ != OnlineState::Offline) {
            Log::warn("Cosmic Online hors ligne : {}", r.message);
            retryTimer_ = retryDelay_ = 15.0f;
        }
        netState_ = OnlineState::Offline;
        lastError_ = r.message;
        return;
    }
    if (netState_ == OnlineState::Offline && r.status > 0) notify(Notification::Kind::Success, "Connexion rétablie", "Cosmic Online est de nouveau accessible.");
    netState_ = OnlineState::Online;
    retryDelay_ = 15.0f;
    if (r.ok) lastError_.clear();
}

void OnlineService::checkConnection(ApiCallback cb) {
    if (!usable()) {
        if (cb) cb(unusableResult());
        return;
    }
    checking_ = true;
    if (netState_ == OnlineState::Idle) netState_ = OnlineState::Connecting;
    call("GET", "/api/v1/health", {}, {}, false, [this, cb](const ApiResult& r) {
        checking_ = false;
        if (cb) cb(r);
    });
}

// --------------------------------------------------------------------- core

void OnlineService::call(const std::string& method, const std::string& path, std::string body, const std::string& contentType, bool auth,
                         ApiCallback cb, bool retried, int timeoutMs) {
    if (!usable()) {
        if (cb) cb(unusableResult());
        return;
    }
    if (auth && !loggedIn()) {
        if (cb) cb(localError("NOT_LOGGED_IN", "Connectez-vous à Cosmic Online."));
        return;
    }
    if (auth && tokens_.accessExpiring()) {
        withRefresh([this, method, path, body, contentType, cb, timeoutMs](const ApiResult& r) {
            if (!r.ok) {
                if (cb) cb(r);
                return;
            }
            call(method, path, body, contentType, true, cb, true, timeoutMs);
        });
        return;
    }
    HttpRequest rq;
    rq.method = method;
    rq.url = config_.serverUrl + path;
    rq.headers.push_back({"Accept", "application/json"});
    rq.headers.push_back({"X-Cosmic-Client", "CosmicPlatformer/" COSMIC_VERSION_STRING});
    if (!contentType.empty()) rq.headers.push_back({"Content-Type", contentType});
    if (auth) rq.headers.push_back({"Authorization", "Bearer " + tokens_.accessToken});
    rq.body = std::move(body);
    rq.timeoutMs = timeoutMs > 0 ? timeoutMs : (method == "GET" ? 10000 : 15000);
    std::string bodyCopy = rq.body;
    client_->send(std::move(rq), [this, method, path, bodyCopy, contentType, auth, cb, retried, timeoutMs](const HttpResponse& http) {
        ApiResult r = parseApiResponse(http);
        noteResult(r);
        if (!r.ok && r.status == 401 && auth && (r.code == "TOKEN_EXPIRED" || r.code == "INVALID_TOKEN")) {
            if (!retried) {
                // The access token died early (server restart, clock skew): refresh once.
                tokens_.accessToken.clear();
                withRefresh([this, method, path, bodyCopy, contentType, cb, timeoutMs](const ApiResult& rr) {
                    if (!rr.ok) {
                        if (cb) cb(rr);
                        return;
                    }
                    call(method, path, bodyCopy, contentType, true, cb, true, timeoutMs);
                });
                return;
            }
            sessionLost("Votre session n'est plus valide.");
        }
        if (cb) cb(r);
    });
}

void OnlineService::withRefresh(std::function<void(const ApiResult&)> then) {
    afterRefresh_.push_back(std::move(then));
    if (refreshing_) return;
    if (tokens_.refreshToken.empty()) {
        auto waiters = std::move(afterRefresh_);
        afterRefresh_.clear();
        ApiResult r = localError("NOT_LOGGED_IN", "Connectez-vous à Cosmic Online.");
        for (auto& w : waiters) w(r);
        return;
    }
    refreshing_ = true;
    Json body = {{"refresh_token", tokens_.refreshToken}};
    call("POST", "/api/v1/auth/refresh", body.dump(), "application/json", false, [this](const ApiResult& r) {
        refreshing_ = false;
        ApiResult outcome = r;
        if (r.ok && r.data.contains("session") && tokens_.fromApi(r.data["session"])) {
            if (remember_) store_->save(tokens_, config_.serverUrl);
        } else if (r.ok) {
            outcome = localError("MALFORMED_RESPONSE", "Réponse de renouvellement de session incomplète.");
        } else if (r.status == 401 || r.status == 403) {
            sessionLost(r.code == "SESSION_REVOKED" ? "Session révoquée par sécurité : reconnectez-vous."
                                                    : "Votre session a expiré : reconnectez-vous.");
        }
        auto waiters = std::move(afterRefresh_);
        afterRefresh_.clear();
        for (auto& w : waiters) w(outcome);
    });
}

void OnlineService::sessionLost(const std::string& why) {
    if (!loggedIn()) return;
    resetFriends();
    tokens_.clear();
    profile_ = Json();
    store_->erase();
    notify(Notification::Kind::Warning, "Déconnecté de Cosmic Online", why);
}

void OnlineService::applySession(const Json& data, bool remember) {
    remember_ = remember;
    resetFriends();
    if (data.contains("session")) tokens_.fromApi(data["session"]);
    if (data.contains("player") && data["player"].is_object()) {
        profile_ = data["player"];
        tokens_.playerId = jstr(profile_, "id");
        tokens_.username = jstr(profile_, "username");
    }
    if (remember) store_->save(tokens_, config_.serverUrl);
    else store_->erase();
}

// ------------------------------------------------------------------ account

void OnlineService::registerAccount(const Json& form, bool remember, ApiCallback cb) {
    call("POST", "/api/v1/auth/register", form.dump(), "application/json", false, [this, remember, cb](const ApiResult& r) {
        if (r.ok) {
            applySession(r.data, remember);
            notify(Notification::Kind::Success, "Compte créé", "Bienvenue dans Cosmic Online, " + tokens_.username + " !");
        }
        if (cb) cb(r);
    });
}

void OnlineService::login(const std::string& username, const std::string& password, bool remember, ApiCallback cb) {
    Json body = {{"username", username}, {"password", password}};
    call("POST", "/api/v1/auth/login", body.dump(), "application/json", false, [this, remember, cb](const ApiResult& r) {
        if (r.ok) {
            applySession(r.data, remember);
            notify(Notification::Kind::Success, "Connecté", "Connecté en tant que " + tokens_.username + ".");
            if (settings_.autoSync && unsentCount() > 0) syncNow();
        }
        if (cb) cb(r);
    });
}

void OnlineService::logout(bool allDevices, ApiCallback cb) {
    if (!loggedIn()) {
        if (cb) cb(localError("NOT_LOGGED_IN", "Non connecté."));
        return;
    }
    Json body = {{"all", allDevices}};
    auto finish = [this, cb](const ApiResult& r) {
        // Locally the session ends whatever the server answered.
        resetFriends();
        tokens_.clear();
        profile_ = Json();
        store_->erase();
        if (cb) cb(r);
    };
    if (!usable()) {
        finish(localError("OFFLINE", "Déconnexion locale."));
        return;
    }
    call("POST", "/api/v1/auth/logout", body.dump(), "application/json", true, finish);
}

bool OnlineService::hasSavedSession() const { return store_ && store_->load(config_.serverUrl).has_value(); }

void OnlineService::resumeSavedSession(ApiCallback cb) {
    auto saved = store_ ? store_->load(config_.serverUrl) : std::nullopt;
    if (!saved) {
        if (cb) cb(localError("NO_SESSION", "Aucune session enregistrée."));
        return;
    }
    tokens_ = *saved;
    remember_ = true;
    netState_ = OnlineState::Connecting;
    withRefresh([this, cb](const ApiResult& r) {
        if (!r.ok) {
            if (cb) cb(r);
            return;
        }
        refreshProfile([this, cb](const ApiResult& me) {
            if (me.ok && settings_.autoSync && (unsentCount() > 0 || pendingVerificationCount() > 0)) syncNow();
            if (cb) cb(me);
        });
    });
}

void OnlineService::refreshProfile(ApiCallback cb) {
    call("GET", "/api/v1/auth/me", {}, {}, true, [this, cb](const ApiResult& r) {
        if (r.ok && r.data.contains("player")) {
            profile_ = r.data["player"];
            tokens_.playerId = jstr(profile_, "id");
            tokens_.username = jstr(profile_, "username");
            if (remember_) store_->save(tokens_, config_.serverUrl);
        }
        if (cb) cb(r);
    });
}

void OnlineService::updateProfile(const Json& patch, ApiCallback cb) {
    call("PATCH", "/api/v1/players/me", patch.dump(), "application/json", true, [this, cb](const ApiResult& r) {
        if (r.ok && r.data.contains("player")) {
            profile_ = r.data["player"];
            tokens_.username = jstr(profile_, "username", tokens_.username);
        }
        if (cb) cb(r);
    });
}

void OnlineService::deleteAccount(const std::string& password, ApiCallback cb) {
    Json body = {{"password", password}, {"confirm", "DELETE"}};
    std::string owner = tokens_.playerId;
    call("DELETE", "/api/v1/players/me", body.dump(), "application/json", true, [this, owner, cb](const ApiResult& r) {
        if (r.ok) {
            resetFriends();
            tokens_.clear();
            profile_ = Json();
            store_->erase();
            attempts_.removeOwner(owner);
            saveAttempts();
            cache_.clear();
            notify(Notification::Kind::Info, "Compte supprimé", "Votre compte et vos données en ligne ont été effacés.");
        }
        if (cb) cb(r);
    });
}

void OnlineService::exportData(ApiCallback cb) { call("GET", "/api/v1/players/me/export", {}, {}, true, cb); }

// --------------------------------------------------------------------- data

std::string OnlineService::cacheKey(const std::string& pathAndQuery) const {
    return config_.serverUrl + "|" + tokens_.playerId + "|" + pathAndQuery;
}

void OnlineService::get(const std::string& pathAndQuery, ApiCallback cb, bool useCache) {
    std::string key = cacheKey(pathAndQuery);
    call("GET", pathAndQuery, {}, {}, loggedIn(), [this, key, cb, useCache](const ApiResult& r) {
        if (r.ok) {
            if (useCache) cache_.put(key, r.data, r.meta);
            if (cb) cb(r);
            return;
        }
        bool unreachable = r.networkFailure() || r.status >= 500 || r.code == "MALFORMED_RESPONSE";
        if (useCache && unreachable && r.net != NetError::Cancelled) {
            if (auto e = cache_.get(key)) {
                ApiResult c;
                c.ok = true;
                c.status = 0;
                c.data = e->data;
                c.meta = e->meta;
                c.fromCache = true;
                c.cacheAgeSeconds = e->ageSeconds();
                c.message = r.message;
                if (cb) cb(c);
                return;
            }
        }
        if (cb) cb(r);
    });
}

void OnlineService::send(const std::string& method, const std::string& path, const Json& body, ApiCallback cb) {
    call(method, path, body.is_null() ? std::string() : body.dump(), body.is_null() ? std::string() : "application/json", true, cb);
}

void OnlineService::sendBytes(const std::string& method, const std::string& path, std::string bytes, ApiCallback cb, int timeoutMs) {
    call(method, path, std::move(bytes), "application/octet-stream", true, std::move(cb), false, timeoutMs);
}

// ------------------------------------------------------------ community levels

const Json* OnlineService::communityInstall(const std::string& localLevelId) const {
    auto it = communityInstalls_.find(localLevelId);
    return it != communityInstalls_.end() && it->is_object() ? &*it : nullptr;
}

void OnlineService::noteCommunityInstall(const std::string& localLevelId, const Json& info) {
    communityInstalls_[localLevelId] = info;
    saveCommunityInstalls();
}

void OnlineService::saveCommunityInstalls() {
    std::error_code ec;
    fs::create_directories(userDir_ / "online", ec);
    FileUtil::writeTextAtomic(userDir_ / "online" / "community_installs.json", communityInstalls_.dump(1));
}

void OnlineService::submitCommunityRun(const std::string& localLevelId, int64_t timeMs, int deaths, const std::string& attemptId,
                                       std::vector<uint8_t> replay) {
    const Json* info = communityInstall(localLevelId);
    if (!info) return;
    std::string title = jstr(*info, "title", localLevelId);
    if (!jget<bool>(*info, "verified", false)) {
        notify(Notification::Kind::Warning, "Temps non classé",
               "Votre copie de « " + title + " » diffère de la version publiée : retéléchargez-la depuis ONLINE LEVELS.");
        return;
    }
    if (!loggedIn() || !usable()) {
        notify(Notification::Kind::Info, "Temps non envoyé", "Connectez-vous à Cosmic Online pour classer vos temps sur « " + title + " ».");
        return;
    }
    std::string id = jstr(*info, "id");
    Json body = {{"version", jget<int>(*info, "version", 1)}, {"attempt_id", attemptId}, {"time_ms", timeMs}, {"deaths", deaths}};
    send("POST", "/api/v1/community/levels/" + id + "/records", body,
         [this, title, replay = std::move(replay)](const ApiResult& r) mutable {
             if (!r.ok) {
                 notify(Notification::Kind::Warning, "Temps non envoyé", r.message);
                 return;
             }
             std::string upload = jstr(r.data, "upload");
             if (upload.rfind("/api/v1/community/records/", 0) != 0) return;
             sendBytes("PUT", upload, std::string(replay.begin(), replay.end()), [this, title](const ApiResult& u) {
                 if (!u.ok) {
                     notify(Notification::Kind::Warning, "Replay non envoyé", u.message);
                     return;
                 }
                 const Json& rec = u.data.value("record", Json::object());
                 if (jstr(rec, "status") == "REPLAY_OK") {
                     int rank = u.data.contains("me") && u.data["me"].is_object() ? jget<int>(u.data["me"], "rank", 0) : 0;
                     notify(Notification::Kind::Record, "Temps classé", "« " + title + " » : " + (rank > 0 ? "rang " + std::to_string(rank) : "temps enregistré") + ".");
                 } else {
                     std::string why;
                     for (const auto& x : rec.value("reasons", Json::array()))
                         if (x.is_string()) why += (why.empty() ? "" : ", ") + x.get<std::string>();
                     notify(Notification::Kind::Warning, "Temps refusé", "Replay non conforme" + (why.empty() ? std::string{} : " : " + why) + ".");
                 }
             });
         });
}

void OnlineService::getBinary(const std::string& path, std::function<void(const HttpResponse&)> cb, size_t maxBytes) {
    if (!usable()) {
        HttpResponse r;
        r.error = NetError::Unsupported;
        cb(r);
        return;
    }
    HttpRequest rq;
    rq.url = config_.serverUrl + path;
    if (loggedIn() && !tokens_.accessExpiring()) rq.headers.push_back({"Authorization", "Bearer " + tokens_.accessToken});
    rq.timeoutMs = maxBytes > (2u << 20) ? 90000 : 20000;
    rq.maxResponseBytes = maxBytes;
    client_->send(std::move(rq), [cb](const HttpResponse& r) { cb(r); });
}

std::optional<OnlineCache::Entry> OnlineService::cached(const std::string& pathAndQuery) { return cache_.get(cacheKey(pathAndQuery)); }

size_t OnlineService::clearCache() { return cache_.clear(); }

// ------------------------------------------------------------------ records

void OnlineService::saveAttempts() {
    if (!attempts_.save()) Log::warn("Cosmic Online : impossible d'enregistrer la file des performances");
}

OnlineAttempt* OnlineService::recordRun(const RunCandidate& run, const std::string& attemptId, const ReplayData* replay) {
    RunCandidate c = run;
    c.hasReplay = replay != nullptr && !replay->frames.empty();
    OnlineAttempt* a = attempts_.add(c, attemptId, loggedIn() ? tokens_.playerId : std::string());
    if (!a) return nullptr;
    if (c.hasReplay) {
        std::vector<uint8_t> bytes = replay->encode();
        std::error_code ec;
        fs::create_directories(attempts_.replayPath(attemptId).parent_path(), ec);
        if (bytes.empty() || !FileUtil::writeBinary(attempts_.replayPath(attemptId), bytes)) {
            a->hasReplay = false;
            for (auto it = a->submissions.begin(); it != a->submissions.end();)
                it = it->category == RunCategory::Speedrun ? a->submissions.erase(it) : it + 1;
        }
    }
    saveAttempts();
    if (usable() && loggedIn() && settings_.autoSync) syncNow();
    return attempts_.find(attemptId);
}

int OnlineService::unsentCount() const {
    if (!loggedIn()) return 0;
    return attempts_.unsentCount(tokens_.playerId);
}

int OnlineService::pendingVerificationCount() const {
    int n = 0;
    for (const auto& a : attempts_.all()) {
        if (a.ownerId != tokens_.playerId) continue;
        for (const auto& s : a.submissions)
            if (s.status == RecordStatus::Pending && !s.recordId.empty()) ++n;
    }
    return n;
}

void OnlineService::syncNow(bool claimUnowned) {
    if (!usable() || !loggedIn()) return;
    syncClaim_ = syncClaim_ || claimUnowned;
    if (syncing_) return;
    syncing_ = true;
    syncSentSomething_ = false;
    syncPolled_.clear();
    syncStep();
}

void OnlineService::applyRecordJson(OnlineAttempt& a, const Json& record, const Json& ranking) {
    RunCategory cat;
    if (!categoryFromId(jstr(record, "category"), cat)) return;
    OnlineSubmission* s = a.submission(cat);
    if (!s) return;
    RecordStatus before = s->status;
    s->recordId = jstr(record, "id", s->recordId);
    s->status = recordStatusFromServer(jstr(record, "verification_status"));
    std::string note = jstr(record, "rejection_reason");
    if (note.empty()) note = jstr(record, "verification_notes");
    s->note = note;
    if (ranking.is_object()) {
        int prev = jget<int>(ranking, "previous_rank", 0);
        int rank = jget<int>(ranking, "rank", 0);
        if (s->status == RecordStatus::Verified) {
            s->previousRank = prev;
            s->rank = rank;
        }
        if (s->status == RecordStatus::Verified && jget<bool>(ranking, "personal_best_improved", false))
            rankChanges_.push_back({a.levelId, cat, prev, rank, a.timeMs, true});
    }
    if (before != s->status) {
        std::string what = std::string(categoryLabel(cat)) + " · " + a.levelId + " · " + formatRunTime(a.timeMs);
        if (s->status == RecordStatus::Verified)
            notify(Notification::Kind::Record, "Record vérifié", what + (s->rank > 0 ? " · n°" + std::to_string(s->rank) + " mondial" : ""));
        else if (s->status == RecordStatus::Rejected)
            notify(Notification::Kind::Error, "Record refusé", what + (s->note.empty() ? "" : " · " + s->note));
    }
}

void OnlineService::syncStep() {
    if (!usable() || !loggedIn()) {
        finishSync();
        return;
    }
    const std::string me = tokens_.playerId;
    // 1. Submissions not sent yet.
    for (auto& a : attempts_.all()) {
        bool owned = a.ownerId == me || (syncClaim_ && a.ownerId.empty());
        if (!owned) continue;
        for (auto& s : a.submissions) {
            if (s.status != RecordStatus::Local || s.blocked) continue;
            a.ownerId = me;  // claimed by the explicit SUBMIT RECORD
            std::string attemptId = a.attemptId;
            RunCategory cat = s.category;
            call("POST", "/api/v1/records/submit", a.submitBody(cat).dump(), "application/json", true, [this, attemptId, cat](const ApiResult& r) {
                OnlineAttempt* at = attempts_.find(attemptId);
                if (!at) return syncStep();
                OnlineSubmission* sub = at->submission(cat);
                if (r.ok && r.data.contains("record")) {
                    syncSentSomething_ = true;
                    applyRecordJson(*at, r.data["record"], r.data.value("ranking", Json()));
                    saveAttempts();
                    return syncStep();
                }
                if (r.networkFailure() || r.status >= 500 || r.status == 429 || r.status == 401 || r.code == "NOT_LOGGED_IN" ||
                    r.code == "MALFORMED_RESPONSE") {
                    // Kept LOCAL: sent again when the connection / session comes back.
                    return finishSync();
                }
                if (r.code == "LEVEL_VERSION_PENDING") {
                    // The game is newer than the server's level list: kept LOCAL, sent later.
                    if (sub) sub->note = "Révision du niveau pas encore enregistrée sur le serveur : envoi automatique plus tard.";
                    saveAttempts();
                    return finishSync();
                }
                if (sub) {
                    // Refused for good by the server (version, category rules...).
                    sub->blocked = true;
                    sub->note = r.message;
                    if (r.code == "LEVEL_VERSION_MISMATCH" || r.code == "LEVEL_VERSION_RETIRED")
                        sub->note = "Version du niveau différente de celle du serveur : performance conservée en local.";
                    notify(Notification::Kind::Warning, "Performance non envoyée", sub->note);
                }
                saveAttempts();
                syncStep();
            });
            return;
        }
    }
    // 2. Replays of submitted attempts.
    for (auto& a : attempts_.all()) {
        if (a.ownerId != me || !a.hasReplay || a.replayUploaded) continue;
        const OnlineSubmission* target = nullptr;
        for (const auto& s : a.submissions)
            if (!s.recordId.empty() && s.status != RecordStatus::Rejected) target = &s;
        if (!target) continue;
        auto bytes = FileUtil::readBinary(attempts_.replayPath(a.attemptId));
        if (!bytes) {
            a.replayUploaded = true;  // file lost: nothing to upload
            continue;
        }
        std::string attemptId = a.attemptId;
        call("PUT", "/api/v1/records/" + target->recordId + "/replay", std::string(bytes->begin(), bytes->end()), "application/octet-stream", true,
             [this, attemptId](const ApiResult& r) {
                 OnlineAttempt* at = attempts_.find(attemptId);
                 if (!at) return syncStep();
                 if (r.ok) {
                     at->replayUploaded = true;
                     syncSentSomething_ = true;
                     if (r.data.contains("records") && r.data["records"].is_array())
                         for (const auto& item : r.data["records"]) applyRecordJson(*at, item.value("record", Json()), item.value("ranking", Json()));
                     saveAttempts();
                     return syncStep();
                 }
                 if (r.networkFailure() || r.status >= 500 || r.status == 429 || r.status == 401 || r.code == "MALFORMED_RESPONSE")
                     return finishSync();
                 // REPLAY_EXISTS, REPLAY_MALFORMED, RECORD_REJECTED...: do not retry forever.
                 at->replayUploaded = true;
                 if (r.code != "REPLAY_EXISTS") notify(Notification::Kind::Warning, "Replay refusé", r.message);
                 saveAttempts();
                 syncStep();
             },
             false, 30000);
        return;
    }
    // 3. Statuses of records waiting for verification.
    for (auto& a : attempts_.all()) {
        if (a.ownerId != me) continue;
        for (auto& s : a.submissions) {
            if (s.status != RecordStatus::Pending || s.recordId.empty() || syncPolled_.count(s.recordId)) continue;
            syncPolled_.insert(s.recordId);
            std::string attemptId = a.attemptId;
            call("GET", "/api/v1/records/" + s.recordId, {}, {}, true, [this, attemptId](const ApiResult& r) {
                OnlineAttempt* at = attempts_.find(attemptId);
                if (r.ok && at && r.data.contains("record")) {
                    Json ranking = Json::object();
                    if (r.data.contains("rank") && r.data["rank"].is_number()) ranking["rank"] = r.data["rank"];
                    applyRecordJson(*at, r.data["record"], ranking);
                    saveAttempts();
                } else if (r.networkFailure() || r.status >= 500) {
                    return finishSync();
                }
                syncStep();
            });
            return;
        }
    }
    finishSync();
}

void OnlineService::finishSync() {
    if (!syncing_) return;
    syncing_ = false;
    syncClaim_ = false;
    saveAttempts();
    if (syncSentSomething_) notify(Notification::Kind::Info, "Synchronisation terminée", "Vos performances ont été envoyées à Cosmic Online.");
}

// ------------------------------------------------------------------ friends

void OnlineService::checkUsername(const std::string& username, ApiCallback cb) {
    call("GET", "/api/v1/auth/username-available?username=" + urlEncode(username), {}, {}, false, std::move(cb));
}

void OnlineService::resetFriends() {
    pendingFriendRequests_ = 0;
    friendsTimer_ = 3.0f;
    unreadMessages_ = 0;
    socialTimer_ = 5.0f;
    openConversation_.clear();
}

void OnlineService::saveFriendNotices() {
    std::error_code ec;
    fs::create_directories(userDir_ / "online", ec);
    FileUtil::writeTextAtomic(userDir_ / "online" / "friend_notices.json", friendNotices_.dump());
}

void OnlineService::loadFriendNotices() {
    if (auto t = FileUtil::readText(userDir_ / "online" / "friend_notices.json")) {
        Json j = Json::parse(*t, nullptr, false);
        if (j.is_object()) friendNotices_ = j;
    }
}

void OnlineService::refreshFriends(ApiCallback cb) {
    if (!loggedIn() || !usable()) {
        if (cb) cb(localError("NOT_LOGGED_IN", "Non connecté."));
        return;
    }
    friendsBusy_ = true;
    std::string owner = tokens_.playerId;
    call("GET", "/api/v1/friends", {}, {}, true, [this, owner, cb](const ApiResult& r) {
        friendsBusy_ = false;
        if (r.ok && owner == tokens_.playerId) noteFriends(r.data);
        if (cb) cb(r);
    });
}

void OnlineService::noteFriends(const Json& data) {
    const std::string me = tokens_.playerId;
    if (me.empty() || !data.is_object()) return;
    auto entries = [&](const char* key) {
        std::vector<std::pair<std::string, std::string>> v;  // id, username
        if (auto it = data.find(key); it != data.end() && it->is_array())
            for (const auto& f : *it)
                if (f.contains("player") && f["player"].is_object())
                    v.emplace_back(jstr(f["player"], "id"), jstr(f["player"], "username"));
        return v;
    };
    auto idSet = [](const Json& list) {
        std::set<std::string> s;
        if (list.is_array())
            for (const auto& x : list)
                if (x.is_string()) s.insert(x.get<std::string>());
        return s;
    };
    auto incoming = entries("incoming");
    auto outgoing = entries("outgoing");
    auto friends = entries("friends");
    Json& known = friendNotices_[me];
    const bool primed = known.is_object();
    std::set<std::string> seenIncoming = primed ? idSet(known.value("incoming", Json::array())) : std::set<std::string>{};
    std::set<std::string> wasOutgoing = primed ? idSet(known.value("outgoing", Json::array())) : std::set<std::string>{};

    std::vector<std::string> newRequests;
    for (const auto& [id, name] : incoming)
        if (!seenIncoming.count(id)) newRequests.push_back(name);
    if (newRequests.size() == 1)
        notify(Notification::Kind::Friend, "Demande d'ami", newRequests.front() + " veut devenir votre ami (ONLINE → SOCIAL → Amis).");
    else if (newRequests.size() > 1)
        notify(Notification::Kind::Friend, "Demandes d'amis",
               std::to_string(newRequests.size()) + " joueurs veulent devenir vos amis (ONLINE → SOCIAL → Amis).");
    for (const auto& [id, name] : friends)
        if (wasOutgoing.count(id)) notify(Notification::Kind::Friend, "Demande acceptée", name + " a accepté votre demande d'ami.");

    Json in = Json::array(), out = Json::array();
    for (const auto& e : incoming) in.push_back(e.first);
    for (const auto& e : outgoing) out.push_back(e.first);
    known = {{"incoming", in}, {"outgoing", out}};
    pendingFriendRequests_ = static_cast<int>(incoming.size());
    saveFriendNotices();
}

// ------------------------------------------------------------------- social

void OnlineService::refreshUnread(ApiCallback cb) {
    if (!loggedIn() || !usable()) {
        if (cb) cb(localError("NOT_LOGGED_IN", "Non connecté."));
        return;
    }
    socialBusy_ = true;
    std::string owner = tokens_.playerId;
    call("GET", "/api/v1/social/unread", {}, {}, true, [this, owner, cb](const ApiResult& r) {
        socialBusy_ = false;
        if (r.ok && owner == tokens_.playerId) noteUnread(r.data);
        if (cb) cb(r);
    });
}

void OnlineService::noteUnread(const Json& data) {
    const std::string me = tokens_.playerId;
    if (me.empty() || !data.is_object()) return;
    unreadMessages_ = std::max(0, jget<int>(data, "total", 0));
    const std::string key = me + "/messages";
    std::string seen = friendNotices_.contains(key) && friendNotices_[key].is_string() ? friendNotices_[key].get<std::string>() : std::string{};
    std::string newest = seen;
    struct Fresh {
        std::string sender, body, where;
    };
    std::vector<Fresh> fresh;
    if (auto it = data.find("latest"); it != data.end() && it->is_array())
        for (const auto& m : *it) {
            std::string at = jstr(m, "created_at");
            if (at.empty() || at <= seen) continue;  // ISO-8601 UTC dates compare as strings
            newest = std::max(newest, at);
            if (jstr(m, "conversation_id") == openConversation_) continue;  // already on screen
            std::string where = jstr(m, "kind") == "group" ? jstr(m, "name") : std::string{};
            fresh.push_back({jstr(m, "sender"), jstr(m, "body"), where});
        }
    if (fresh.size() == 1) {
        const Fresh& f = fresh.front();
        std::string body = f.body;
        if (body.size() > 110) {
            size_t cut = 107;  // never inside a UTF-8 sequence
            while (cut > 0 && (static_cast<unsigned char>(body[cut]) & 0xC0) == 0x80) --cut;
            body = body.substr(0, cut) + "…";
        }
        for (char& c : body)
            if (c == '\n') c = ' ';
        notify(Notification::Kind::Message, f.where.empty() ? "Message de " + f.sender : f.sender + " · " + f.where, body);
    } else if (fresh.size() > 1) {
        std::vector<std::string> names;
        for (const auto& f : fresh)
            if (std::find(names.begin(), names.end(), f.sender) == names.end()) names.push_back(f.sender);
        std::string who = names.front();
        for (size_t i = 1; i < names.size() && i < 3; ++i) who += ", " + names[i];
        if (names.size() > 3) who += "…";
        notify(Notification::Kind::Message, std::to_string(fresh.size()) + " nouveaux messages", "De " + who + " (ONLINE → SOCIAL).");
    }
    if (newest != seen) {
        friendNotices_[key] = newest;
        saveFriendNotices();
    }
}

// ---------------------------------------------------------------- interface

void OnlineService::loadKnownRanks() {
    if (auto t = FileUtil::readText(userDir_ / "online" / "known_ranks.json")) {
        Json j = Json::parse(*t, nullptr, false);
        if (j.is_object()) knownRanks_ = j;
    }
}

void OnlineService::observeBoard(const std::string& boardKey, int myRank, int total) {
    std::string key = tokens_.playerId + "|" + boardKey;
    Json prev = knownRanks_.contains(key) ? knownRanks_[key] : Json();
    if (prev.is_object()) {
        int oldRank = jget<int>(prev, "rank", 0);
        int oldTotal = jget<int>(prev, "total", 0);
        if (myRank > 0 && oldRank > 0 && myRank > oldRank)
            notify(Notification::Kind::Warning, "Record battu",
                   "Un autre joueur vous a dépassé : n°" + std::to_string(oldRank) + " → n°" + std::to_string(myRank) + ".");
        if (total > oldTotal && oldTotal > 0)
            notify(Notification::Kind::Info, "Classement",
                   std::to_string(total - oldTotal) + (total - oldTotal > 1 ? " nouveaux joueurs" : " nouveau joueur") + " dans ce classement.");
    }
    knownRanks_[key] = {{"rank", myRank}, {"total", total}};
    std::error_code ec;
    fs::create_directories(userDir_ / "online", ec);
    FileUtil::writeTextAtomic(userDir_ / "online" / "known_ranks.json", knownRanks_.dump());
}

void OnlineService::notify(Notification::Kind kind, std::string title, std::string text) {
    notifications_.push_back({kind, std::move(title), std::move(text)});
    // Notifications wait during a run: keep enough of them.
    while (notifications_.size() > 30) notifications_.pop_front();
}

std::vector<OnlineService::Notification> OnlineService::takeNotifications() {
    std::vector<Notification> out(notifications_.begin(), notifications_.end());
    notifications_.clear();
    return out;
}

std::vector<OnlineService::RankChange> OnlineService::takeRankChanges() {
    std::vector<RankChange> out;
    out.swap(rankChanges_);
    return out;
}

}  // namespace cosmic

// Helper used by screens building query strings.
namespace cosmic::online {
std::string encodeQueryValue(const std::string& v) { return urlEncode(v); }
}  // namespace cosmic::online
