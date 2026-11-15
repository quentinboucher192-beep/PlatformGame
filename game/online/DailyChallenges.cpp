#include "game/online/DailyChallenges.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/online/OnlineService.h"
#include "game/online/OnlineSession.h"
#include "game/online/Replay.h"
#include "saves/SaveFile.h"

namespace cosmic {

namespace {

constexpr double kRefreshSeconds = 300.0;      // the current challenge, at most every 5 minutes
constexpr double kRefreshMissingSeconds = 60.0;  // when none is known (or it just ended)
constexpr float kRetrySeconds = 30.0f;         // a send that failed on the network
constexpr float kStatusPollSeconds = 60.0f;    // runs waiting for the server's decision
constexpr size_t kMaxCached = 120;
constexpr size_t kMaxSubmissions = 200;

bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

/// Safe as a file name and in a URL path: letters, digits, '-' and '_' only.
bool safeId(const std::string& s, size_t max = 64) {
    if (s.empty() || s.size() > max) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

bool isDate(const std::string& s) {
    int y, m, d;
    return s.size() == 10 && std::sscanf(s.c_str(), "%4d-%2d-%2d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

std::optional<int64_t> isoMs(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return std::nullopt;
    auto t = parseIsoUtc(j[key].get<std::string>());
    if (!t) return std::nullopt;
    return std::chrono::duration_cast<std::chrono::milliseconds>(t->time_since_epoch()).count();
}

/// Days since 1970-01-01 of a "YYYY-MM-DD" date (days_from_civil).
std::optional<int64_t> dayNumber(const std::string& date) {
    int y, m, d;
    if (!isDate(date) || std::sscanf(date.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return std::nullopt;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

SaveFile resultsFile() { return SaveFile("challenge_results", ChallengeService::kVersion); }
SaveFile pendingFile() { return SaveFile("challenge_runs", ChallengeService::kVersion); }

bool unreachable(const ApiResult& r) {
    return r.networkFailure() || r.status >= 500 || r.status == 429 || r.status == 401 || r.code == "NOT_LOGGED_IN" ||
           r.code == "MALFORMED_RESPONSE" || r.code == "UNAVAILABLE";
}

}  // namespace

// ------------------------------------------------------------------ tiers, states

const char* challengeTierLabel(ChallengeTier t) {
    switch (t) {
        case ChallengeTier::Bronze: return "Bronze";
        case ChallengeTier::Silver: return "Argent";
        case ChallengeTier::Gold: return "Or";
        case ChallengeTier::None: break;
    }
    return "—";
}

ChallengeTier challengeTierFromJson(const Json& v) {
    if (v.is_number_integer()) {
        int n = v.get<int>();
        return n >= 1 && n <= 3 ? static_cast<ChallengeTier>(n) : ChallengeTier::None;
    }
    if (!v.is_string()) return ChallengeTier::None;
    std::string s = v.get<std::string>();
    for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (s == "bronze") return ChallengeTier::Bronze;
    if (s == "silver") return ChallengeTier::Silver;
    if (s == "gold") return ChallengeTier::Gold;
    return ChallengeTier::None;
}

const char* challengeRunStateLabel(ChallengeRunState s) {
    switch (s) {
        case ChallengeRunState::LocalOnly: return "Gardé sur ce PC";
        case ChallengeRunState::Waiting: return "Hors ligne : à l'envoi";
        case ChallengeRunState::Sent: return "Envoyé";
        case ChallengeRunState::Pending: return "En attente de contrôle";
        case ChallengeRunState::Verified: return "Vérifié";
        case ChallengeRunState::Rejected: return "Refusé";
    }
    return "";
}

ChallengeRunState challengeRunStateFromServer(const std::string& status) {
    if (status == "VERIFIED") return ChallengeRunState::Verified;
    if (status == "REJECTED") return ChallengeRunState::Rejected;
    return ChallengeRunState::Pending;
}

// ------------------------------------------------------------------ the challenge

ChallengeTier DailyChallenge::tierFor(int64_t timeMs, int damageTaken) const {
    if (timeMs <= 0) return ChallengeTier::None;
    if (goldMs > 0 && timeMs <= goldMs && (!goldNoDamage || damageTaken == 0)) return ChallengeTier::Gold;
    if (silverMs > 0 && timeMs <= silverMs) return ChallengeTier::Silver;
    return ChallengeTier::Bronze;
}

std::optional<DailyChallenge> DailyChallenge::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<DailyChallenge> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("défi absent");
    DailyChallenge c;
    c.id = jstr(j, "id");
    c.kind = jstr(j, "kind", "daily");
    c.date = jstr(j, "date");
    c.source = jstr(j, "source", "auto");
    if (!safeId(c.id) || !safeId(c.kind, 16) || !isDate(c.date)) return fail("identifiant ou date du défi invalide");
    auto starts = isoMs(j, "starts_at"), ends = isoMs(j, "ends_at");
    if (!starts || !ends || *ends <= *starts) return fail("dates du défi invalides");
    c.startsAtMs = *starts;
    c.endsAtMs = *ends;
    const Json& lv = j.contains("level") ? j["level"] : Json();
    if (!lv.is_object()) return fail("niveau du défi absent");
    c.level.source = jstr(lv, "source");
    c.level.levelId = jstr(lv, "level_id");
    c.level.version = jget<int>(lv, "level_version", 0);
    c.level.hash = jstr(lv, "level_hash");
    c.level.title = jstr(lv, "title");
    if (c.level.title.size() > 160) c.level.title.resize(160);
    if ((c.level.source != "campaign" && c.level.source != "community") || !safeId(c.level.levelId) || c.level.version < 1 ||
        !isHex64(c.level.hash))
        return fail("niveau du défi invalide");
    if (lv.contains("author") && lv["author"].is_object()) {
        c.level.authorId = jstr(lv["author"], "id");
        c.level.authorName = jstr(lv["author"], "username");
        if (c.level.authorName.size() > 64) c.level.authorName.resize(64);
    }
    std::string why;
    auto rules = ChallengeRules::fromJson(c.id, j.contains("rules") ? j["rules"] : Json(), &why);
    if (!rules) {
        if (error) *error = "règles du défi : " + why;
        return std::nullopt;
    }
    c.rules = *rules;
    c.rulesHash = jstr(j, "rules_hash");
    // The rules' fingerprint is computed here too: a challenge whose rules are not the ones it
    // was hashed with is not played (its runs would all be refused).
    if (c.rulesHash != c.rules.hash()) return fail("empreinte des règles différente");
    const Json& tiers = j.contains("tiers") ? j["tiers"] : Json();
    if (!tiers.is_object()) return fail("paliers du défi absents");
    c.silverMs = jget<int64_t>(tiers, "silver_ms", 0);
    c.goldMs = jget<int64_t>(tiers, "gold_ms", 0);
    c.goldNoDamage = jget<bool>(tiers, "gold_no_damage", true);
    if (c.silverMs <= 0 || c.goldMs <= 0 || c.silverMs > 2 * 3600 * 1000LL || c.goldMs > 2 * 3600 * 1000LL) return fail("paliers du défi invalides");
    c.raw = j;
    return c;
}

// ------------------------------------------------------------------ submissions

Json ChallengeSubmission::toJson() const {
    return {{"attempt_id", attemptId},   {"challenge_id", challengeId}, {"owner", ownerId},
            {"time_ms", timeMs},         {"deaths", deaths},            {"damage_taken", damageTaken},
            {"level_hash", levelHash},   {"rules_hash", rulesHash},     {"game_version", gameVersion},
            {"has_replay", hasReplay},   {"replay_uploaded", replayUploaded},
            {"state", static_cast<int>(state)}, {"tier", static_cast<int>(serverTier)},
            {"run_id", runId},           {"upload_path", uploadPath},   {"note", note},
            {"created_at", createdAt}};
}

std::optional<ChallengeSubmission> ChallengeSubmission::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    ChallengeSubmission s;
    s.attemptId = jstr(j, "attempt_id");
    s.challengeId = jstr(j, "challenge_id");
    if (!safeId(s.attemptId) || !safeId(s.challengeId)) return std::nullopt;
    s.ownerId = jstr(j, "owner");
    s.timeMs = jget<int64_t>(j, "time_ms", 0);
    s.deaths = jget<int>(j, "deaths", 0);
    s.damageTaken = jget<int>(j, "damage_taken", 0);
    s.levelHash = jstr(j, "level_hash");
    s.rulesHash = jstr(j, "rules_hash");
    s.gameVersion = jstr(j, "game_version");
    s.hasReplay = jget<bool>(j, "has_replay", false);
    s.replayUploaded = jget<bool>(j, "replay_uploaded", false);
    s.state = static_cast<ChallengeRunState>(std::clamp(jget<int>(j, "state", 1), 0, 5));
    s.serverTier = static_cast<ChallengeTier>(std::clamp(jget<int>(j, "tier", 0), 0, 3));
    s.runId = jstr(j, "run_id");
    s.uploadPath = jstr(j, "upload_path");
    s.note = jstr(j, "note");
    s.createdAt = jstr(j, "created_at");
    return s;
}

Json ChallengeSubmission::body() const {
    return {{"attempt_id", attemptId}, {"time_ms", timeMs},       {"deaths", deaths},           {"damage_taken", damageTaken},
            {"level_hash", levelHash}, {"rules_hash", rulesHash}, {"game_version", gameVersion}, {"has_replay", hasReplay}};
}

// ------------------------------------------------------------------ service

void ChallengeService::init(OnlineService& online, fs::path dir) {
    online_ = &online;
    dir_ = std::move(dir);
    loadAll();
}

int64_t ChallengeService::localNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t ChallengeService::nowMs() const { return localNowMs() + (offsetKnown_ ? offsetMs_ : 0); }

std::string ChallengeService::utcDate(int64_t ms) {
    int64_t z = (ms >= 0 ? ms : ms - 86399999) / 86400000 + 719468;  // civil_from_days
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    char b[48];
    std::snprintf(b, sizeof b, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return b;
}

void ChallengeService::loadAll() {
    challenges_.clear();
    results_.clear();
    submissions_.clear();
    std::error_code ec;
    if (fs::is_directory(dir_, ec)) {
        for (const auto& e : fs::directory_iterator(dir_, ec)) {
            if (!e.is_regular_file() || FileUtil::extensionLower(e.path()) != ".json") continue;
            std::string stem = Paths::toUtf8(e.path().stem());
            if (stem == "state" || stem == "results" || stem == "pending") continue;
            auto j = FileUtil::loadJson(e.path());
            if (!j || !j->is_object()) continue;
            std::string err;
            if (auto c = DailyChallenge::fromJson(j->value("challenge", Json()), &err); c && c->id == stem) challenges_[c->id] = *c;
            else Log::warn("Défis : cache {} ignoré ({})", stem, err);
        }
    }
    if (auto st = FileUtil::loadJson(dir_ / "state.json"); st && st->is_object()) {
        offsetKnown_ = jget<bool>(*st, "offset_known", false);
        offsetMs_ = jget<int64_t>(*st, "offset_ms", 0);
        // A wild value (damaged file): the computer's clock instead.
        if (offsetMs_ > 400LL * 86400000 || offsetMs_ < -400LL * 86400000) offsetKnown_ = false, offsetMs_ = 0;
        if (st->contains("streak") && (*st)["streak"].is_object()) {
            const Json& s = (*st)["streak"];
            serverStreak_ = Streak{std::max(0, jget<int>(s, "current", 0)), std::max(0, jget<int>(s, "best", 0)), jstr(s, "last_date"), true};
            serverStreakFor_ = jstr(*st, "streak_player");
        }
    }
    if (auto d = resultsFile().load(dir_ / "results.json"); d && d->is_object() && d->contains("results") && (*d)["results"].is_object()) {
        for (const auto& [id, v] : (*d)["results"].items()) {
            if (!safeId(id) || !v.is_object()) continue;
            ChallengeResult r;
            r.challengeId = id;
            r.date = jstr(v, "date");
            r.bestMs = std::max<int64_t>(0, jget<int64_t>(v, "best_ms", 0));
            r.tier = static_cast<ChallengeTier>(std::clamp(jget<int>(v, "tier", 0), 0, 3));
            r.deaths = jget<int>(v, "deaths", 0);
            r.damageTaken = jget<int>(v, "damage_taken", 0);
            r.finishes = jget<int>(v, "finishes", 0);
            r.bestAttemptId = jstr(v, "best_attempt");
            r.hasReplay = jget<bool>(v, "has_replay", false);
            if (isDate(r.date)) results_[id] = r;
        }
    }
    if (auto d = pendingFile().load(dir_ / "pending.json"); d && d->is_object() && d->contains("runs") && (*d)["runs"].is_array())
        for (const Json& v : (*d)["runs"])
            if (auto s = ChallengeSubmission::fromJson(v)) submissions_.push_back(*s);
}

void ChallengeService::saveState() const {
    if (dir_.empty()) return;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json st = {{"format", "cosmic.challenge_state"}, {"offset_known", offsetKnown_}, {"offset_ms", offsetMs_}};
    if (serverStreak_) {
        st["streak"] = {{"current", serverStreak_->current}, {"best", serverStreak_->best}, {"last_date", serverStreak_->lastDate}};
        st["streak_player"] = serverStreakFor_;
    }
    FileUtil::writeTextAtomic(dir_ / "state.json", st.dump(1));
}

void ChallengeService::saveResults() const {
    if (dir_.empty()) return;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json all = Json::object();
    for (const auto& [id, r] : results_)
        all[id] = {{"date", r.date},      {"best_ms", r.bestMs},           {"tier", static_cast<int>(r.tier)}, {"deaths", r.deaths},
                   {"damage_taken", r.damageTaken}, {"finishes", r.finishes}, {"best_attempt", r.bestAttemptId},   {"has_replay", r.hasReplay}};
    if (!resultsFile().save(dir_ / "results.json", {{"results", all}})) Log::warn("Défis : impossible d'enregistrer les résultats");
}

void ChallengeService::saveSubmissions() const {
    if (dir_.empty()) return;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json runs = Json::array();
    for (const auto& s : submissions_) runs.push_back(s.toJson());
    if (!pendingFile().save(dir_ / "pending.json", {{"runs", runs}})) Log::warn("Défis : impossible d'enregistrer les parties à envoyer");
}

void ChallengeService::cache(const DailyChallenge& c) {
    auto it = challenges_.find(c.id);
    // A challenge that has begun never changes (contract): the copy already here stays, unless
    // it is not the same (then the server's is kept: it is the one runs are checked against).
    if (it != challenges_.end() && it->second.raw == c.raw) return;
    challenges_[c.id] = c;
    if (dir_.empty()) return;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    FileUtil::writeTextAtomic(dir_ / (c.id + ".json"), Json{{"format", "cosmic.challenge"}, {"version", 1}, {"challenge", c.raw}}.dump(1));
    // The oldest ones go beyond the limit.
    while (challenges_.size() > kMaxCached) {
        auto oldest = std::min_element(challenges_.begin(), challenges_.end(),
                                       [](const auto& a, const auto& b) { return a.second.startsAtMs < b.second.startsAtMs; });
        fs::remove(dir_ / (oldest->first + ".json"), ec);
        challenges_.erase(oldest);
    }
}

const DailyChallenge* ChallengeService::note(const Json& challenge) {
    if (challenge.is_null()) return nullptr;
    std::string err;
    auto c = DailyChallenge::fromJson(challenge, &err);
    if (!c) {
        Log::warn("Défis : défi refusé ({})", err);
        return nullptr;
    }
    cache(*c);
    return &challenges_[c->id];
}

const DailyChallenge* ChallengeService::current() const {
    const int64_t now = nowMs();
    const DailyChallenge* best = nullptr;
    for (const auto& [id, c] : challenges_)
        if (c.startsAtMs <= now && now < c.endsAtMs && (!best || c.startsAtMs > best->startsAtMs)) best = &c;
    return best;
}

const DailyChallenge* ChallengeService::find(const std::string& id) const {
    auto it = challenges_.find(id);
    return it == challenges_.end() ? nullptr : &it->second;
}

std::vector<const DailyChallenge*> ChallengeService::past() const {
    const int64_t now = nowMs();
    std::vector<const DailyChallenge*> out;
    for (const auto& [id, c] : challenges_)
        if (c.endsAtMs <= now) out.push_back(&c);
    std::sort(out.begin(), out.end(), [](const DailyChallenge* a, const DailyChallenge* b) { return a->startsAtMs > b->startsAtMs; });
    return out;
}

void ChallengeService::refresh(bool force) {
    if (!online_ || fetch_ == Fetch::Loading) return;
    if (!online_->usable()) {
        fetch_ = Fetch::Offline;
        fetchMessage_ = "Cosmic Online indisponible";
        return;
    }
    double wait = current() ? kRefreshSeconds : kRefreshMissingSeconds;
    if (!force && clock_ - lastFetch_ < wait) return;
    lastFetch_ = clock_;
    fetch_ = Fetch::Loading;
    online_->get("/api/v1/challenges/daily/current", OnlineService::guard(life_, [this](const ApiResult& r) {
        if (!r.ok) {
            fetch_ = r.networkFailure() || r.status >= 500 ? Fetch::Offline : Fetch::Failed;
            fetchMessage_ = r.message;
            return;
        }
        if (!r.fromCache) {
            // The server's clock: the countdown never relies on this computer's alone.
            if (auto t = parseIsoUtc(jstr(r.data, "server_time"))) {
                int64_t server = std::chrono::duration_cast<std::chrono::milliseconds>(t->time_since_epoch()).count();
                offsetMs_ = server - localNowMs();
                offsetKnown_ = true;
                saveState();
            }
        }
        note(r.data.value("challenge", Json()));
        note(r.data.value("previous", Json()));
        fetch_ = r.fromCache ? Fetch::Offline : Fetch::Ok;
        fetchMessage_ = r.fromCache ? r.message : std::string();
    }), false);
}

void ChallengeService::loadArchives(std::function<void(bool, const std::string&)> cb) {
    if (!online_ || archivesBusy_) return;
    if (!online_->usable()) {
        if (cb) cb(false, "Cosmic Online indisponible");
        return;
    }
    // Older than the oldest one known (the day of the current one when none).
    std::string before = utcDate(nowMs());
    for (const DailyChallenge* c : past())
        if (c->date < before) before = c->date;
    archivesBusy_ = true;
    online_->get("/api/v1/challenges/daily?before=" + before + "&limit=" + std::to_string(kArchivePage),
                 OnlineService::guard(life_, [this, cb](const ApiResult& r) {
                     archivesBusy_ = false;
                     if (!r.ok) {
                         if (cb) cb(false, r.message);
                         return;
                     }
                     const Json& list = r.data.is_array() ? r.data : r.data.value("challenges", Json::array());
                     int n = 0;
                     if (list.is_array())
                         for (const Json& c : list) n += note(c) ? 1 : 0;
                     const Json& next = r.data.is_object() ? r.data.value("next_before", Json()) : Json();
                     archivesDone_ = n == 0 || (r.data.is_object() && r.data.contains("next_before") && next.is_null());
                     if (cb) cb(true, {});
                 }),
                 false);
}

// ------------------------------------------------------------------ results

const ChallengeResult* ChallengeService::result(const std::string& challengeId) const {
    auto it = results_.find(challengeId);
    return it == results_.end() ? nullptr : &it->second;
}

fs::path ChallengeService::bestReplayPath(const std::string& challengeId) const { return dir_ / "replays" / (challengeId + ".cprz"); }

std::optional<ReplayData> ChallengeService::bestReplay(const std::string& challengeId) const {
    const ChallengeResult* r = result(challengeId);
    if (!r || !r->hasReplay || !safeId(challengeId)) return std::nullopt;
    auto bytes = FileUtil::readBinary(bestReplayPath(challengeId));
    if (!bytes || bytes->size() > 4u * 1024 * 1024) return std::nullopt;
    return ReplayData::decode(*bytes);
}

ChallengeService::Streak ChallengeService::localStreak() const {
    std::set<int64_t> days;
    for (const auto& [id, r] : results_)
        if (r.finishes > 0)
            if (auto d = dayNumber(r.date)) days.insert(*d);
    Streak s;
    if (days.empty()) return s;
    // The longest run of days in a row.
    int run = 0;
    int64_t prev = 0;
    for (int64_t d : days) {
        run = run > 0 && d == prev + 1 ? run + 1 : 1;
        prev = d;
        s.best = std::max(s.best, run);
    }
    auto today = dayNumber(utcDate(nowMs()));
    if (!today) return s;
    int64_t from = days.count(*today) ? *today : days.count(*today - 1) ? *today - 1 : -1;
    if (from < 0) return s;
    for (int64_t d = from; days.count(d); --d) ++s.current;
    s.lastDate = utcDate(from * 86400000LL);
    return s;
}

ChallengeService::Streak ChallengeService::streak() const {
    if (serverStreak_ && online_ && online_->loggedIn() && serverStreakFor_ == online_->playerId()) {
        // The server's (read at the last /challenges/me), unless it no longer runs: its last
        // day is before yesterday.
        Streak s = *serverStreak_;
        auto last = dayNumber(s.lastDate), today = dayNumber(utcDate(nowMs()));
        if (last && today && *last < *today - 1) s.current = 0;
        // A run finished here since that reading: the local streak may be further on.
        Streak local = localStreak();
        if (local.current > s.current) return local;
        return s;
    }
    return localStreak();
}

void ChallengeService::refreshMine(ApiCallback cb) {
    if (!online_ || !online_->usable() || !online_->loggedIn() || mineBusy_) {
        if (cb) {
            ApiResult r;
            r.code = "NOT_LOGGED_IN";
            r.message = "Connectez-vous à Cosmic Online.";
            cb(r);
        }
        return;
    }
    mineBusy_ = true;
    std::string who = online_->playerId();
    online_->get("/api/v1/challenges/me?limit=60", OnlineService::guard(life_, [this, cb, who](const ApiResult& r) {
        mineBusy_ = false;
        if (r.ok && online_->playerId() == who) {
            const Json& s = r.data.value("streak", Json::object());
            if (s.is_object()) {
                serverStreak_ = Streak{std::max(0, jget<int>(s, "current", 0)), std::max(0, jget<int>(s, "best", 0)), jstr(s, "last_date"), true};
                serverStreakFor_ = who;
            }
            const Json& results = r.data.value("results", Json::array());
            bool changed = false;
            if (results.is_array())
                for (const Json& x : results) {
                    std::string id = jstr(x, "challenge_id");
                    if (!safeId(id)) continue;
                    ChallengeRunState st = challengeRunStateFromServer(jstr(x, "status"));
                    serverStatus_[id] = st;
                    int64_t best = jget<int64_t>(x, "best_time_ms", 0);
                    // My run of that time: its state is the server's.
                    for (auto& sub : submissions_)
                        if (sub.challengeId == id && sub.ownerId == who && sub.timeMs == best && !sub.runId.empty() && sub.state != st &&
                            (sub.state == ChallengeRunState::Sent || sub.state == ChallengeRunState::Pending)) {
                            sub.state = st;
                            changed = true;
                        }
                }
            if (changed) saveSubmissions();
            if (!r.fromCache) saveState();
        }
        if (cb) cb(r);
    }), true);
}

std::optional<ChallengeRunState> ChallengeService::serverStatus(const std::string& challengeId) const {
    if (!online_ || !online_->loggedIn()) return std::nullopt;
    auto it = serverStatus_.find(challengeId);
    if (it == serverStatus_.end()) return std::nullopt;
    return it->second;
}

// ------------------------------------------------------------------ runs

ChallengeService::RunReport ChallengeService::recordRun(const DailyChallenge& c, int64_t timeMs, int deaths, int damageTaken,
                                                        const std::string& levelHash, const std::string& attemptId,
                                                        const ReplayData* replay, bool sendable, const std::string& whyNot) {
    RunReport rep;
    rep.tier = c.tierFor(timeMs, damageTaken);
    ChallengeResult& r = results_[c.id];
    bool first = r.finishes == 0;
    r.challengeId = c.id;
    r.date = c.date;
    rep.previousBestMs = r.bestMs;
    rep.newBest = r.bestMs == 0 || timeMs < r.bestMs;
    bool betterTier = rep.tier > r.tier;
    ++r.finishes;
    if (betterTier) r.tier = rep.tier;
    if (rep.newBest) {
        r.bestMs = timeMs;
        r.deaths = deaths;
        r.damageTaken = damageTaken;
        r.bestAttemptId = attemptId;
        r.hasReplay = false;
        if (replay && safeId(c.id)) {
            std::vector<uint8_t> bytes = replay->encode();
            std::error_code ec;
            fs::create_directories(bestReplayPath(c.id).parent_path(), ec);
            r.hasReplay = !bytes.empty() && FileUtil::writeBinary(bestReplayPath(c.id), bytes);
        }
    }
    saveResults();
    // Sent: the first run of the day (the streak), a better time or a better tier.
    bool worth = first || rep.newBest || betterTier;
    if (!online_ || !online_->loggedIn()) {
        rep.state = ChallengeRunState::LocalOnly;
        rep.note = "Sans compte : ni classement ni envoi. Connectez-vous à Cosmic Online pour entrer dans le classement du jour.";
        return rep;
    }
    if (!sendable) {
        rep.state = ChallengeRunState::LocalOnly;
        rep.note = whyNot.empty() ? std::string("Non envoyé.") : whyNot;
        return rep;
    }
    if (!worth) {
        rep.state = ChallengeRunState::LocalOnly;
        rep.note = "Pas meilleur que votre temps déjà envoyé : gardé sur ce PC.";
        return rep;
    }
    ChallengeSubmission s;
    s.attemptId = attemptId;
    s.challengeId = c.id;
    s.ownerId = online_->playerId();
    s.timeMs = timeMs;
    s.deaths = deaths;
    s.damageTaken = damageTaken;
    s.levelHash = levelHash;
    s.rulesHash = c.rulesHash;
    s.gameVersion = COSMIC_VERSION_STRING;
    s.createdAt = FileUtil::nowString();
    if (replay && safeId(attemptId)) {
        std::vector<uint8_t> bytes = replay->encode();
        std::error_code ec;
        fs::create_directories(uploadPath(attemptId).parent_path(), ec);
        s.hasReplay = !bytes.empty() && FileUtil::writeBinary(uploadPath(attemptId), bytes);
    }
    s.state = ChallengeRunState::Waiting;
    submissions_.push_back(s);
    // The oldest decided ones go beyond the limit (the waiting ones always stay).
    while (submissions_.size() > kMaxSubmissions) {
        auto it = std::find_if(submissions_.begin(), submissions_.end(), [](const ChallengeSubmission& x) {
            return x.state != ChallengeRunState::Waiting && x.state != ChallengeRunState::Sent;
        });
        if (it == submissions_.end()) break;
        submissions_.erase(it);
    }
    saveSubmissions();
    rep.state = ChallengeRunState::Waiting;
    sendTimer_ = 0.0f;
    flush();
    return rep;
}

const ChallengeSubmission* ChallengeService::submission(const std::string& attemptId) const {
    for (const auto& s : submissions_)
        if (s.attemptId == attemptId) return &s;
    return nullptr;
}

const ChallengeSubmission* ChallengeService::bestSubmission(const std::string& challengeId) const {
    const ChallengeResult* r = result(challengeId);
    return r ? submission(r->bestAttemptId) : nullptr;
}

int ChallengeService::waitingCount() const {
    if (!online_ || !online_->loggedIn()) return 0;
    int n = 0;
    for (const auto& s : submissions_)
        n += s.ownerId == online_->playerId() && s.state == ChallengeRunState::Waiting ? 1 : 0;
    return n;
}

fs::path ChallengeService::uploadPath(const std::string& attemptId) const { return dir_ / "upload" / (attemptId + ".cprz"); }

void ChallengeService::applyRun(ChallengeSubmission& s, const Json& run) {
    if (!run.is_object()) return;
    std::string id = jstr(run, "id");
    if (safeId(id)) s.runId = id;
    if (run.contains("status") && run["status"].is_string()) s.state = challengeRunStateFromServer(run["status"].get<std::string>());
    if (run.contains("tier")) s.serverTier = challengeTierFromJson(run["tier"]);
}

void ChallengeService::flush() {
    if (sending_ || !online_ || !online_->usable() || !online_->loggedIn()) return;
    sendNext();
}

void ChallengeService::sendNext() {
    const std::string me = online_->playerId();
    for (auto& s : submissions_) {
        if (s.ownerId != me) continue;
        if (s.state == ChallengeRunState::Sent && s.hasReplay && !s.replayUploaded && !s.runId.empty()) {
            upload(s.attemptId);
            return;
        }
        if (s.state != ChallengeRunState::Waiting) continue;
        sending_ = true;
        std::string attemptId = s.attemptId;
        online_->send("POST", "/api/v1/challenges/" + s.challengeId + "/runs", s.body(), OnlineService::guard(life_, [this, attemptId](const ApiResult& r) {
            sending_ = false;
            ChallengeSubmission* sub = nullptr;
            for (auto& x : submissions_)
                if (x.attemptId == attemptId) sub = &x;
            if (!sub) return;
            if (r.ok) {
                applyRun(*sub, r.data.value("run", Json()));
                const Json& rp = r.data.value("replay", Json::object());
                std::string up = rp.is_object() ? jstr(rp, "upload_path") : std::string();
                bool required = rp.is_object() && jget<bool>(rp, "required", false);
                bool accepted = rp.is_object() && jget<bool>(rp, "accepted", true);
                if (sub->hasReplay && !sub->replayUploaded && accepted && up.rfind("/api/v1/challenges/runs/", 0) == 0 && up.find("..") == std::string::npos &&
                    sub->state != ChallengeRunState::Rejected) {
                    sub->uploadPath = up;
                    sub->state = ChallengeRunState::Sent;  // until its replay is there
                } else if (required && !sub->hasReplay) {
                    sub->note = "Replay manquant : le temps ne pourra pas être vérifié.";
                }
                saveSubmissions();
                sendTimer_ = 0.0f;
                return;
            }
            if (unreachable(r) || r.code == "CHALLENGE_NOT_STARTED") {
                // Kept: sent again when the connection (or the server's day) comes.
                sendTimer_ = kRetrySeconds;
                return;
            }
            sub->state = ChallengeRunState::Rejected;
            if (r.code == "CHALLENGE_ENDED") sub->note = "Envoyé trop tard : le défi était terminé.";
            else if (r.code == "RULES_MISMATCH") sub->note = "Règles différentes de celles du défi.";
            else if (r.code == "LEVEL_MISMATCH") sub->note = "Version du niveau différente de celle du défi.";
            else if (r.code == "GAME_VERSION_UNSUPPORTED") sub->note = "Version du jeu trop ancienne : mettez le jeu à jour.";
            else if (r.code == "CHALLENGE_NOT_FOUND") sub->note = "Défi inconnu du serveur.";
            else sub->note = r.message;
            saveSubmissions();
            online_->notify(OnlineService::Notification::Kind::Warning, "Défi : temps refusé", sub->note);
            sendTimer_ = 0.0f;
        }));
        return;
    }
}

void ChallengeService::upload(const std::string& attemptId) {
    ChallengeSubmission* s = nullptr;
    for (auto& x : submissions_)
        if (x.attemptId == attemptId) s = &x;
    if (!s) return;
    auto bytes = FileUtil::readBinary(uploadPath(attemptId));
    if (!bytes) {
        // The file is gone: nothing to upload (the server keeps the run without its proof).
        s->replayUploaded = true;
        s->state = ChallengeRunState::Pending;
        saveSubmissions();
        return;
    }
    sending_ = true;
    online_->sendBytes("PUT", s->uploadPath, std::string(bytes->begin(), bytes->end()), OnlineService::guard(life_, [this, attemptId](const ApiResult& r) {
        sending_ = false;
        ChallengeSubmission* sub = nullptr;
        for (auto& x : submissions_)
            if (x.attemptId == attemptId) sub = &x;
        if (!sub) return;
        if (!r.ok && unreachable(r)) {
            sendTimer_ = kRetrySeconds;
            return;
        }
        sub->replayUploaded = true;
        std::error_code ec;
        fs::remove(uploadPath(attemptId), ec);
        if (r.ok) {
            applyRun(*sub, r.data.value("run", Json()));
            if (sub->state == ChallengeRunState::Sent) sub->state = ChallengeRunState::Pending;
            mineTimer_ = 0.0f;  // its status soon
        } else if (r.code != "REPLAY_EXISTS") {
            sub->state = ChallengeRunState::Rejected;
            sub->note = r.message.empty() ? std::string("Replay refusé.") : r.message;
            online_->notify(OnlineService::Notification::Kind::Warning, "Défi : replay refusé", sub->note);
        } else if (sub->state == ChallengeRunState::Sent) {
            sub->state = ChallengeRunState::Pending;
        }
        saveSubmissions();
        sendTimer_ = 0.0f;
    }), 30000);
}

void ChallengeService::update(float dt) {
    clock_ += dt;
    if (!online_ || !online_->usable()) return;
    // A challenge that just ended: the next one is asked for (once a minute at most).
    if (fetch_ != Fetch::Never && fetch_ != Fetch::Loading && !current() && clock_ - lastFetch_ > kRefreshMissingSeconds) refresh();
    if (!online_->loggedIn()) return;
    sendTimer_ -= dt;
    bool offline = online_->state() == OnlineState::Offline;
    // The connection is back: what waits goes at once.
    if (wasOffline_ && online_->state() == OnlineState::Online) sendTimer_ = 0.0f;
    wasOffline_ = offline;
    if (!sending_ && sendTimer_ <= 0.0f) {
        bool any = false;
        for (const auto& s : submissions_)
            any = any || (s.ownerId == online_->playerId() &&
                          (s.state == ChallengeRunState::Waiting || (s.state == ChallengeRunState::Sent && !s.replayUploaded)));
        if (any) {
            sendTimer_ = offline ? kRetrySeconds : 1.0f;
            sendNext();
        }
    }
    // Runs waiting for the server's decision: their status from time to time.
    mineTimer_ -= dt;
    if (!offline && mineTimer_ <= 0.0f) {
        mineTimer_ = kStatusPollSeconds;
        bool waiting = false;
        for (const auto& s : submissions_)
            waiting = waiting || (s.ownerId == online_->playerId() && (s.state == ChallengeRunState::Sent || s.state == ChallengeRunState::Pending));
        if (waiting) refreshMine();
    }
}

// ------------------------------------------------------------------ leaderboard, ghost

void ChallengeService::leaderboard(const std::string& challengeId, const std::string& scope, ApiCallback cb) {
    if (!online_ || !safeId(challengeId)) return;
    online_->get("/api/v1/challenges/" + challengeId + "/leaderboard?scope=" + online::encodeQueryValue(scope) + "&limit=50", std::move(cb), true);
}

fs::path ChallengeService::runReplayPath(const std::string& challengeId, const std::string& runId) const {
    return dir_ / "ghosts" / (challengeId + "-" + runId + ".cprz");
}

void ChallengeService::downloadRunReplay(const std::string& challengeId, const std::string& runId,
                                         std::function<void(std::optional<fs::path>, const std::string&)> cb) {
    if (!online_ || !safeId(challengeId) || !safeId(runId)) {
        if (cb) cb(std::nullopt, "Replay inconnu.");
        return;
    }
    fs::path file = runReplayPath(challengeId, runId);
    if (FileUtil::exists(file)) {
        if (cb) cb(file, {});
        return;
    }
    online_->getBinary("/api/v1/challenges/runs/" + runId + "/replay",
                       [cb, file, alive = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                           if (!alive.lock()) return;
                           if (r.networkFailure() || r.status != 200) {
                               std::string why = r.networkFailure() ? std::string("Connexion impossible.")
                                                 : r.status == 403   ? std::string("Profil privé : pas de fantôme.")
                                                                     : parseApiResponse(r).message;
                               if (cb) cb(std::nullopt, why);
                               return;
                           }
                           std::vector<uint8_t> bytes(r.body.begin(), r.body.end());
                           std::string err;
                           if (!ReplayData::decode(bytes, &err)) {
                               if (cb) cb(std::nullopt, "Replay illisible : " + err);
                               return;
                           }
                           std::error_code ec;
                           fs::create_directories(file.parent_path(), ec);
                           if (!FileUtil::writeBinary(file, bytes)) {
                               if (cb) cb(std::nullopt, "Écriture impossible.");
                               return;
                           }
                           if (cb) cb(file, {});
                       },
                       4u << 20);
}

}  // namespace cosmic
