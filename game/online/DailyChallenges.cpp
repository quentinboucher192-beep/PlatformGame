#include "game/online/DailyChallenges.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

bool isDate(const std::string& s) { return challenge_period::dayNumber(s).has_value(); }

std::optional<int64_t> isoMs(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return std::nullopt;
    auto t = parseIsoUtc(j[key].get<std::string>());
    if (!t) return std::nullopt;
    return std::chrono::duration_cast<std::chrono::milliseconds>(t->time_since_epoch()).count();
}

bool isMonth(const std::string& s) { return challenge_period::valid(ChallengeKind::Monthly, s); }

int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

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

// ------------------------------------------------------------------ kinds, periods

const char* challengeKindId(ChallengeKind k) {
    switch (k) {
        case ChallengeKind::Weekly: return "weekly";
        case ChallengeKind::Monthly: return "monthly";
        case ChallengeKind::Daily: break;
    }
    return "daily";
}

std::optional<ChallengeKind> challengeKindFromId(const std::string& id) {
    if (id == "daily") return ChallengeKind::Daily;
    if (id == "weekly") return ChallengeKind::Weekly;
    if (id == "monthly") return ChallengeKind::Monthly;
    return std::nullopt;
}

namespace challenge_period {

std::optional<int64_t> dayNumber(const std::string& date) {
    int y = 0, m = 0, d = 0;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-' || std::sscanf(date.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return std::nullopt;
    if (m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    const int yy = y - (m <= 2);  // days_from_civil
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t n = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    // 2026-02-30 is not a day: it must read back the same.
    if (dateOfDay(n) != date) return std::nullopt;
    return n;
}

std::string dateOfDay(int64_t day) { return ChallengeService::utcDate(day * 86400000LL); }

int weekday(int64_t day) { return static_cast<int>(((day % 7) + 7 + 3) % 7); }  // 1970-01-01: a Thursday

std::string ofDay(ChallengeKind kind, int64_t day) {
    std::string date = dateOfDay(day);
    if (kind == ChallengeKind::Daily) return date;
    if (kind == ChallengeKind::Monthly) return date.substr(0, 7);
    // ISO 8601: the week belongs to the year of its Thursday.
    int64_t thursday = day - weekday(day) + 3;
    std::string th = dateOfDay(thursday);
    int64_t jan1 = *dayNumber(th.substr(0, 4) + "-01-01");
    char b[16];
    std::snprintf(b, sizeof b, "%s-W%02d", th.substr(0, 4).c_str(), static_cast<int>((thursday - jan1) / 7 + 1));
    return b;
}

std::optional<int64_t> firstDay(ChallengeKind kind, const std::string& period) {
    if (kind == ChallengeKind::Daily) return dayNumber(period);
    if (kind == ChallengeKind::Monthly) {
        if (period.size() != 7 || period[4] != '-') return std::nullopt;
        return dayNumber(period + "-01");
    }
    int y = 0, w = 0;
    if (period.size() != 8 || period[4] != '-' || period[5] != 'W' || std::sscanf(period.c_str(), "%4d-W%2d", &y, &w) != 2 || w < 1 || w > 53)
        return std::nullopt;
    auto jan4 = dayNumber(period.substr(0, 4) + "-01-04");  // always in week 1
    if (!jan4) return std::nullopt;
    int64_t monday = *jan4 - weekday(*jan4) + static_cast<int64_t>(w - 1) * 7;
    if (ofDay(ChallengeKind::Weekly, monday) != period) return std::nullopt;  // week 53 of a 52-week year
    return monday;
}

bool valid(ChallengeKind kind, const std::string& period) { return firstDay(kind, period).has_value(); }

std::optional<int64_t> index(ChallengeKind kind, const std::string& period) {
    auto first = firstDay(kind, period);
    if (!first) return std::nullopt;
    if (kind == ChallengeKind::Daily) return *first;
    if (kind == ChallengeKind::Weekly) return floorDiv(*first - 4, 7);  // 1970-01-05: a Monday
    int y = 0, m = 0;
    std::sscanf(period.c_str(), "%4d-%2d", &y, &m);
    return static_cast<int64_t>(y) * 12 + (m - 1);
}

std::string addMonths(const std::string& month, int delta) {
    if (!valid(ChallengeKind::Monthly, month)) return {};
    int y = 0, m = 0;
    std::sscanf(month.c_str(), "%4d-%2d", &y, &m);
    int64_t n = static_cast<int64_t>(y) * 12 + (m - 1) + delta;
    if (n < 0 || n >= 10000 * 12) return {};
    char b[16];
    std::snprintf(b, sizeof b, "%04d-%02d", static_cast<int>(n / 12), static_cast<int>(n % 12) + 1);
    return b;
}

int daysInMonth(const std::string& month) {
    auto a = firstDay(ChallengeKind::Monthly, month), b = firstDay(ChallengeKind::Monthly, addMonths(month, 1));
    return a && b ? static_cast<int>(*b - *a) : 0;
}

}  // namespace challenge_period

// ------------------------------------------------------------------ the challenge

ChallengeTier DailyChallenge::tierFor(int64_t timeMs, int damageTaken) const {
    if (timeMs <= 0) return ChallengeTier::None;
    // « Finir le niveau »: the finish is Or (Argent with damage when Or asks for none).
    if (finishObjective) return !goldNoDamage || damageTaken == 0 ? ChallengeTier::Gold : ChallengeTier::Silver;
    if (goldMs > 0 && timeMs <= goldMs && (!goldNoDamage || damageTaken == 0)) return ChallengeTier::Gold;
    if (silverMs > 0 && timeMs <= silverMs) return ChallengeTier::Silver;
    return ChallengeTier::Bronze;
}

ChallengeTier DailyChallenge::tierForProgress(float progress) const {
    if (!finishObjective) return ChallengeTier::None;
    if (progress >= 100.0f) return goldNoDamage ? ChallengeTier::Silver : ChallengeTier::Gold;
    if (progress >= static_cast<float>(silverProgress)) return ChallengeTier::Silver;
    if (progress >= static_cast<float>(bronzeProgress)) return ChallengeTier::Bronze;
    return ChallengeTier::None;
}

std::optional<DailyChallenge> DailyChallenge::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<DailyChallenge> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("défi absent");
    DailyChallenge c;
    c.id = jstr(j, "id");
    // 2.32 servers write only the day's challenges (no kind, no period).
    auto kind = challengeKindFromId(jstr(j, "kind", "daily"));
    if (!kind) return fail("sorte de défi inconnue");
    c.kind = *kind;
    c.source = jstr(j, "source", "auto");
    if (!safeId(c.id)) return fail("identifiant du défi invalide");
    auto starts = isoMs(j, "starts_at"), ends = isoMs(j, "ends_at");
    if (!starts || !ends || *ends <= *starts) return fail("dates du défi invalides");
    c.startsAtMs = *starts;
    c.endsAtMs = *ends;
    // The period: as written, else the day of a 2.32 challenge; its id is « kind-period ».
    c.date = jstr(j, "date", ChallengeService::utcDate(c.startsAtMs));
    c.period = jstr(j, "period", c.kind == ChallengeKind::Daily ? c.date : std::string());
    if (!isDate(c.date) || !challenge_period::valid(c.kind, c.period) || c.id != std::string(c.kindId()) + "-" + c.period)
        return fail("identifiant ou période du défi invalide");
    // Its dates are those of its period (UTC): a day, a Monday to the next one, a month.
    auto first = challenge_period::firstDay(c.kind, c.period);
    int64_t next = c.kind == ChallengeKind::Daily    ? *first + 1
                   : c.kind == ChallengeKind::Weekly ? *first + 7
                                                     : *challenge_period::firstDay(c.kind, challenge_period::addMonths(c.period, 1));
    if (c.startsAtMs != *first * 86400000LL || c.endsAtMs != next * 86400000LL || c.date != challenge_period::dateOfDay(*first))
        return fail("dates du défi différentes de sa période");
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
    // 2.33: the level has a route, its attempts are ranked by how far they go (absent: no).
    c.progressAvailable = jget<bool>(j, "progress_available", false);
    // 2.41: the objective (absent: « Vitesse », as every older challenge). « Finir le niveau »:
    // the shares of the route of Bronze and Argent (10 to 95 %, Argent above Bronze).
    const std::string objective = jstr(j, "objective", "speed");
    if (objective != "speed" && objective != "finish") return fail("objectif du défi inconnu");
    c.finishObjective = objective == "finish";
    if (c.finishObjective) {
        c.bronzeProgress = jget<int>(tiers, "bronze_progress", 50);
        c.silverProgress = jget<int>(tiers, "silver_progress", 80);
        if (c.bronzeProgress < 10 || c.silverProgress > 95 || c.silverProgress <= c.bronzeProgress) return fail("paliers du défi invalides");
    }
    // 2.43: its own rewards (absent: none).
    c.prizes = ChallengePrize::listFromJson(jfield(j, "rewards", Json()));
    c.raw = j;
    return c;
}

std::vector<ChallengePrize> ChallengePrize::listFromJson(const Json& rewards) {
    std::vector<ChallengePrize> out;
    if (!rewards.is_array()) return out;
    auto safe = [](const std::string& s) {
        if (s.empty() || s.size() > 64) return false;
        for (char ch : s)
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == ':')) return false;
        return true;
    };
    auto clean = [](std::string s) {
        if (s.size() > 160) s.resize(160);
        for (char& ch : s)
            if (static_cast<unsigned char>(ch) < 0x20) ch = ' ';
        return s;
    };
    for (const Json& r : rewards) {
        if (!r.is_object() || out.size() >= 6) continue;
        ChallengePrize p;
        p.kind = jstr(r, "kind");
        p.ref = jstr(r, "ref");
        p.condition = jstr(r, "condition");
        p.value = jget<int>(r, "value", 0);
        if ((p.kind != "item" && p.kind != "title" && p.kind != "suit" && !p.creation()) || !safe(p.ref)) continue;
        if (p.condition == "tier") p.value = std::clamp(p.value, 1, 3);
        else if (p.condition == "top") p.value = std::clamp(p.value, 1, 100);
        else if (p.condition == "finished") p.value = 0;
        else continue;
        if (p.title()) {
            const Json& t = jfield(r, "title", Json());
            p.name = clean(jstr(t, "text"));
            p.color = jstr(t, "color", "gold");
            if (p.color != "gold" && p.color != "silver" && p.color != "cyan" && p.color != "violet") p.color = "gold";
            p.category = "title";
            if (p.name.empty() || p.ref.rfind("srv:", 0) != 0) continue;
        } else if (p.suit()) {
            // A suit created on the server (ADMINISTRATION › Créations): its name and colours.
            const Json& s = jfield(r, "suit", Json());
            p.name = clean(jstr(s, "name"));
            p.suitColors = suitColorsFromJson(jfield(s, "colors", Json()));
            p.category = "suit";
            if (p.name.empty() || !p.suitColors || !isServerSuitRef(p.ref)) continue;
        } else if (p.creation()) {
            // 2.59: a trail, a frame or a victory effect created on the server: its name and look.
            const Json& c = jfield(r, "creation", Json());
            p.name = clean(jstr(c, "name"));
            p.creationLook = creationLookFromJson(p.kind, c);
            p.category = p.kind;
            std::string refKind;
            if (p.name.empty() || !p.creationLook || !isServerLookRef(p.ref, &refKind) || refKind != p.kind || jstr(c, "kind") != p.kind) continue;
        } else {
            const Json& it = jfield(r, "item", Json());
            p.name = clean(jstr(it, "name"));
            p.category = jstr(it, "category");
        }
        out.push_back(std::move(p));
    }
    return out;
}

Json ChallengePrize::body() const {
    Json b = {{"kind", kind}, {"ref", ref}, {"condition", condition}};
    if (condition != "finished") b["value"] = value;
    return b;
}

// ------------------------------------------------------------------ submissions

Json ChallengeSubmission::toJson() const {
    Json j = {{"attempt_id", attemptId},   {"challenge_id", challengeId}, {"owner", ownerId},
              {"time_ms", timeMs},         {"deaths", deaths},            {"damage_taken", damageTaken},
              {"level_hash", levelHash},   {"rules_hash", rulesHash},     {"game_version", gameVersion},
              {"has_replay", hasReplay},   {"replay_uploaded", replayUploaded}, {"late", late},
              {"state", static_cast<int>(state)}, {"tier", static_cast<int>(serverTier)},
              {"run_id", runId},           {"upload_path", uploadPath},   {"note", note},
              {"created_at", createdAt}};
    if (attempt) {
        j["attempt"] = true;
        j["bosses"] = bosses;
        j["waves"] = waves;
        j["progress"] = progress;
        j["server_progress"] = serverProgress;
    }
    return j;
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
    s.late = jget<bool>(j, "late", false);
    s.state = static_cast<ChallengeRunState>(std::clamp(jget<int>(j, "state", 1), 0, 5));
    s.serverTier = static_cast<ChallengeTier>(std::clamp(jget<int>(j, "tier", 0), 0, 3));
    s.runId = jstr(j, "run_id");
    s.uploadPath = jstr(j, "upload_path");
    s.note = jstr(j, "note");
    s.createdAt = jstr(j, "created_at");
    s.attempt = jget<bool>(j, "attempt", false);
    if (s.attempt) {
        for (const Json& b : jfield(j, "bosses", Json::array()))
            if (b.is_number_integer()) s.bosses.push_back(std::clamp(b.get<int>(), 0, 1000));
        for (const Json& w : jfield(j, "waves", Json::array()))
            if (w.is_number_integer()) s.waves.push_back(std::max(0, w.get<int>()));
        s.progress = jget<float>(j, "progress", 0.0f);
        s.serverProgress = jget<float>(j, "server_progress", -1.0f);
    }
    return s;
}

Json ChallengeSubmission::body() const {
    if (attempt)
        // The claim only: the server computes the score from the replay, never takes the game's.
        return {{"attempt_id", attemptId},
                {"time_ms", timeMs},
                {"level_hash", levelHash},
                {"rules_hash", rulesHash},
                {"game_version", gameVersion},
                {"claim", {{"finished", false}, {"bosses", bosses}, {"waves", waves}}}};
    return {{"attempt_id", attemptId}, {"time_ms", timeMs},       {"deaths", deaths},           {"damage_taken", damageTaken},
            {"level_hash", levelHash}, {"rules_hash", rulesHash}, {"game_version", gameVersion}, {"has_replay", hasReplay}};
}

std::string ChallengeSubmission::postPath() const { return "/api/v1/challenges/" + challengeId + (attempt ? "/attempts" : "/runs"); }

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
        auto readStreak = [](const Json& s) {
            return Streak{std::max(0, jget<int>(s, "current", 0)), std::max(0, jget<int>(s, "best", 0)),
                          jstr(s, "last_period", jstr(s, "last_date")), true};
        };
        // 2.32 kept the day's streak alone under "streak".
        if (st->contains("streak") && (*st)["streak"].is_object()) serverStreaks_[0] = readStreak((*st)["streak"]);
        if (st->contains("streaks") && (*st)["streaks"].is_object())
            for (int k = 0; k < kChallengeKinds; ++k)
                if (const Json& s = jfield((*st)["streaks"], challengeKindId(static_cast<ChallengeKind>(k))); s.is_object()) serverStreaks_[k] = readStreak(s);
        serverStreakFor_ = jstr(*st, "streak_player");
        if (std::string fm = jstr(*st, "first_month"); isMonth(fm)) firstMonth_ = fm;
        // 2.35: what the server verified for the rewards of the challenges (that account's).
        if (auto v = rewardProgressFromJson(jfield(*st, "verified", Json())); v && !serverStreakFor_.empty()) rewards_ = *v;
        // 2.43: the rewards won at single challenges (that account's).
        if (auto w = WonRewards::fromJson(jfield(*st, "won", Json())); w && !serverStreakFor_.empty()) won_ = *w;
    }
    if (auto d = resultsFile().load(dir_ / "results.json"); d && d->is_object() && d->contains("results") && (*d)["results"].is_object()) {
        for (const auto& [id, v] : (*d)["results"].items()) {
            if (!safeId(id) || !v.is_object()) continue;
            ChallengeResult r;
            r.challengeId = id;
            r.date = jstr(v, "date");
            // 2.32 results are days' challenges.
            r.kind = challengeKindFromId(jstr(v, "kind", "daily")).value_or(ChallengeKind::Daily);
            r.period = jstr(v, "period", r.kind == ChallengeKind::Daily ? r.date : std::string());
            r.bestMs = std::max<int64_t>(0, jget<int64_t>(v, "best_ms", 0));
            r.tier = static_cast<ChallengeTier>(std::clamp(jget<int>(v, "tier", 0), 0, 3));
            r.deaths = jget<int>(v, "deaths", 0);
            r.damageTaken = jget<int>(v, "damage_taken", 0);
            r.finishes = jget<int>(v, "finishes", 0);
            // 2.32 runs were all played while their challenge ran.
            r.liveFinishes = std::clamp(jget<int>(v, "live_finishes", r.finishes), 0, std::max(0, r.finishes));
            r.bestLate = jget<bool>(v, "best_late", false);
            r.bestAttemptId = jstr(v, "best_attempt");
            r.hasReplay = jget<bool>(v, "has_replay", false);
            r.bestProgress = std::clamp(jget<float>(v, "best_progress", 0.0f), 0.0f, 99.9f);
            r.bestProgressAttemptId = jstr(v, "best_progress_attempt");
            r.liveTier = jget<bool>(v, "live_tier", false);
            if (isDate(r.date) && challenge_period::valid(r.kind, r.period)) results_[id] = r;
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
    Json streaks = Json::object();
    for (int k = 0; k < kChallengeKinds; ++k)
        if (const auto& s = serverStreaks_[k])
            streaks[challengeKindId(static_cast<ChallengeKind>(k))] = {{"current", s->current}, {"best", s->best}, {"last_period", s->lastPeriod}};
    if (!streaks.empty()) st["streaks"] = streaks;
    if (rewards_.known) {
        Json v = Json::object();
        for (int k = 0; k < kChallengeKinds; ++k)
            v[challengeKindId(static_cast<ChallengeKind>(k))] = {{"gold", rewards_.atLeast[k][2]},
                                                                  {"silver", rewards_.atLeast[k][1]},
                                                                  {"bronze", rewards_.atLeast[k][0]},
                                                                  {"best_streak", rewards_.bestStreak[k]}};
        st["verified"] = v;
    }
    if (won_.known) st["won"] = won_.toJson();
    if (!streaks.empty() || rewards_.known || won_.known) st["streak_player"] = serverStreakFor_;
    if (!firstMonth_.empty()) st["first_month"] = firstMonth_;
    FileUtil::writeTextAtomic(dir_ / "state.json", st.dump(1));
}

void ChallengeService::saveResults() const {
    if (dir_.empty()) return;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json all = Json::object();
    for (const auto& [id, r] : results_)
        all[id] = {{"date", r.date},
                   {"kind", challengeKindId(r.kind)},
                   {"period", r.period},
                   {"best_ms", r.bestMs},
                   {"tier", static_cast<int>(r.tier)},
                   {"deaths", r.deaths},
                   {"damage_taken", r.damageTaken},
                   {"finishes", r.finishes},
                   {"live_finishes", r.liveFinishes},
                   {"best_late", r.bestLate},
                   {"best_attempt", r.bestAttemptId},
                   {"has_replay", r.hasReplay},
                   {"best_progress", r.bestProgress},
                   {"best_progress_attempt", r.bestProgressAttemptId},
                   {"live_tier", r.liveTier}};
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

const DailyChallenge* ChallengeService::current(ChallengeKind kind) const {
    const int64_t now = nowMs();
    const DailyChallenge* best = nullptr;
    for (const auto& [id, c] : challenges_)
        if (c.kind == kind && c.startsAtMs <= now && now < c.endsAtMs && (!best || c.startsAtMs > best->startsAtMs)) best = &c;
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
    fetchCurrent(false);
}

void ChallengeService::fetchCurrent(bool legacy) {
    // 2.33: the three kinds at once; a server still at 2.32 answers 404 there, and its route
    // of the day is read instead (the game keeps working, the day's challenge only).
    const std::string path = legacy ? "/api/v1/challenges/daily/current" : "/api/v1/challenges/current";
    online_->get(path, OnlineService::guard(life_, [this, legacy](const ApiResult& r) {
        if (!legacy && !r.ok && r.status == 404) {
            fetchCurrent(true);
            return;
        }
        if (!r.ok) {
            fetch_ = r.networkFailure() || r.status >= 500 ? Fetch::Offline : Fetch::Failed;
            fetchMessage_ = r.message;
            return;
        }
        applyCurrent(r, legacy);
    }), false);
}

void ChallengeService::applyCurrent(const ApiResult& r, bool legacy) {
    legacy_ = legacy;
    if (!r.fromCache) {
        // The server's clock: the countdown never relies on this computer's alone.
        if (auto t = parseIsoUtc(jstr(r.data, "server_time"))) {
            int64_t server = std::chrono::duration_cast<std::chrono::milliseconds>(t->time_since_epoch()).count();
            offsetMs_ = server - localNowMs();
            offsetKnown_ = true;
            saveState();
        }
    }
    if (legacy) {
        note(jfield(r.data, "challenge", Json()));
        note(jfield(r.data, "previous", Json()));
    } else {
        // { daily, weekly, monthly, previous: { daily, weekly, monthly } }, each one or null.
        const Json& previous = jfield(r.data, "previous", Json::object());
        for (int k = 0; k < kChallengeKinds; ++k) {
            const char* key = challengeKindId(static_cast<ChallengeKind>(k));
            if (const DailyChallenge* c = note(jfield(r.data, key, Json())); c && c->kind != static_cast<ChallengeKind>(k))
                Log::warn("Défis : {} donné comme défi « {} »", c->id, key);
            if (previous.is_object()) note(jfield(previous, key, Json()));
        }
    }
    // The first end among the running challenges: the next reading is due then.
    knownEndMs_ = 0;
    for (int k = 0; k < kChallengeKinds; ++k)
        if (const DailyChallenge* c = current(static_cast<ChallengeKind>(k)); c && (knownEndMs_ == 0 || c->endsAtMs < knownEndMs_)) knownEndMs_ = c->endsAtMs;
    fetch_ = r.fromCache ? Fetch::Offline : Fetch::Ok;
    fetchMessage_ = r.fromCache ? r.message : std::string();
}

// ------------------------------------------------------------------ calendar

const ChallengeService::CalendarMonth& ChallengeService::calendar(const std::string& month) const {
    static const CalendarMonth kNever;
    auto it = calendar_.find(month);
    return it == calendar_.end() ? kNever : it->second;
}

std::vector<const DailyChallenge*> ChallengeService::challengesOfMonth(const std::string& month) const {
    std::vector<const DailyChallenge*> out;
    auto first = challenge_period::firstDay(ChallengeKind::Monthly, month);
    auto next = challenge_period::firstDay(ChallengeKind::Monthly, challenge_period::addMonths(month, 1));
    if (!first || !next) return out;
    const int64_t from = *first * 86400000LL, to = *next * 86400000LL, now = nowMs();
    for (const auto& [id, c] : challenges_)
        if (c.startsAtMs <= now && c.startsAtMs < to && c.endsAtMs > from) out.push_back(&c);
    std::sort(out.begin(), out.end(), [](const DailyChallenge* a, const DailyChallenge* b) {
        return a->startsAtMs != b->startsAtMs ? a->startsAtMs < b->startsAtMs : a->kind < b->kind;
    });
    return out;
}

void ChallengeService::loadCalendar(const std::string& month, bool force) {
    if (!online_ || !isMonth(month)) return;
    CalendarMonth& m = calendar_[month];
    if (m.state == CalendarMonth::State::Loading || (!force && m.state == CalendarMonth::State::Ok)) return;
    if (!online_->usable()) {
        m.state = CalendarMonth::State::Offline;
        m.message = "Cosmic Online indisponible";
        return;
    }
    m.state = CalendarMonth::State::Loading;
    if (legacy_) {
        loadCalendarLegacy(month, challenge_period::addMonths(month, 1) + "-01", 0);
        return;
    }
    online_->get("/api/v1/challenges/calendar?month=" + month, OnlineService::guard(life_, [this, month](const ApiResult& r) {
        CalendarMonth& m = calendar_[month];
        if (!r.ok && r.status == 404 && !r.networkFailure() && r.code != "CHALLENGE_NOT_FOUND") {
            // A 2.32 server: the month's days from its list of the day's challenges.
            legacy_ = true;
            loadCalendarLegacy(month, challenge_period::addMonths(month, 1) + "-01", 0);
            return;
        }
        if (!r.ok) {
            m.state = r.networkFailure() || r.status >= 500 ? CalendarMonth::State::Offline : CalendarMonth::State::Failed;
            m.message = r.message;
            return;
        }
        const Json& list = jfield(r.data, "challenges", Json::array());
        if (list.is_array())
            for (const Json& c : list) note(c);
        std::string fm = jstr(r.data, "first_month");
        if (isMonth(fm) && fm != firstMonth_ && !r.fromCache) {
            firstMonth_ = fm;
            saveState();
        } else if (isMonth(fm) && firstMonth_.empty()) {
            firstMonth_ = fm;
        }
        // My result of each challenge (logged in): as /challenges/me reads it.
        const Json& me = jfield(r.data, "me", Json());
        if (me.is_object() && resultsMine())
            for (const auto& [id, x] : me.items())
                if (safeId(id) && x.is_object()) noteServerResult(id, x);
        m.state = r.fromCache ? CalendarMonth::State::Cached : CalendarMonth::State::Ok;
        m.message = r.fromCache ? r.message : std::string();
    }), true);
}

void ChallengeService::loadCalendarLegacy(const std::string& month, const std::string& before, int page) {
    // GET /challenges/daily?before=…&limit=30 (2.32), newest first: up to two pages cover a
    // month; a challenge older than the month tells that the calendar goes further back.
    online_->get("/api/v1/challenges/daily?before=" + before + "&limit=30", OnlineService::guard(life_, [this, month, page](const ApiResult& r) {
        CalendarMonth& m = calendar_[month];
        if (!r.ok) {
            m.state = r.networkFailure() || r.status >= 500 ? CalendarMonth::State::Offline : CalendarMonth::State::Failed;
            m.message = r.message;
            return;
        }
        const Json& list = r.data.is_array() ? r.data : jfield(r.data, "challenges", Json::array());
        const std::string start = month + "-01";
        std::string oldest;
        int n = 0;
        if (list.is_array())
            for (const Json& j : list)
                if (const DailyChallenge* c = note(j)) {
                    ++n;
                    if (oldest.empty() || c->date < oldest) oldest = c->date;
                }
        bool earlier = !oldest.empty() && oldest < start;
        if (!earlier && n >= 30 && page == 0) {
            loadCalendarLegacy(month, oldest, 1);  // the first days of the month
            return;
        }
        // Nothing before this month: it is the first one of the calendar.
        if (!earlier && n < 30 && (firstMonth_.empty() || firstMonth_ < month)) {
            std::string fm = oldest.empty() ? month : oldest.substr(0, 7);
            if (fm != firstMonth_) {
                firstMonth_ = fm;
                if (!r.fromCache) saveState();
            }
        }
        m.state = r.fromCache ? CalendarMonth::State::Cached : CalendarMonth::State::Ok;
        m.message = r.fromCache ? r.message : std::string();
    }), true);
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

ChallengeService::Streak ChallengeService::localStreak(ChallengeKind kind) const {
    // The periods of that kind with a run finished while its challenge ran (« hors délai »
    // runs never count).
    std::map<int64_t, std::string> periods;
    for (const auto& [id, r] : results_)
        if (r.kind == kind && (r.liveFinishes > 0 || r.liveTier))
            if (auto i = challenge_period::index(kind, r.period)) periods[*i] = r.period;
    Streak s;
    if (periods.empty()) return s;
    // The longest run of periods in a row.
    int run = 0;
    int64_t prev = 0;
    for (const auto& [i, p] : periods) {
        run = run > 0 && i == prev + 1 ? run + 1 : 1;
        prev = i;
        s.best = std::max(s.best, run);
    }
    auto now = challenge_period::index(kind, challenge_period::ofDay(kind, nowMs() / 86400000LL - (nowMs() < 0 ? 1 : 0)));
    if (!now) return s;
    int64_t from = periods.count(*now) ? *now : periods.count(*now - 1) ? *now - 1 : INT64_MIN;
    if (from == INT64_MIN) return s;
    for (int64_t i = from; periods.count(i); --i) ++s.current;
    s.lastPeriod = periods[from];
    return s;
}

ChallengeService::Streak ChallengeService::streak(ChallengeKind kind) const {
    const auto& server = serverStreaks_[static_cast<int>(kind)];
    if (server && online_ && online_->loggedIn() && serverStreakFor_ == online_->playerId()) {
        // The server's (read at the last /challenges/me), unless it no longer runs: its last
        // period is before the previous one.
        Streak s = *server;
        const std::string nowPeriod = challenge_period::ofDay(kind, nowMs() / 86400000LL);
        auto last = challenge_period::index(kind, s.lastPeriod), now = challenge_period::index(kind, nowPeriod);
        if (last && now && *last < *now - 1) s.current = 0;
        // The current challenge finished here since that reading (the server counts it once it
        // has the run): the streak goes on by this period.
        bool playedNow = false;
        for (const auto& [id, r] : results_) playedNow = playedNow || (r.kind == kind && r.liveFinishes > 0 && r.period == nowPeriod);
        if (last && now && *last == *now - 1 && playedNow) {
            ++s.current;
            s.best = std::max(s.best, s.current);
            s.lastPeriod = nowPeriod;
        }
        Streak local = localStreak(kind);
        if (local.current > s.current) return local;
        return s;
    }
    return localStreak(kind);
}

bool ChallengeService::resultsMine() {
    if (!online_ || !online_->loggedIn()) return false;
    if (serverStreakFor_ != online_->playerId()) {
        // Another account's: forgotten.
        serverResults_.clear();
        for (auto& s : serverStreaks_) s.reset();
        serverStreakFor_ = online_->playerId();
        setRewardProgress(ChallengeRewardProgress{});
        setWon(WonRewards{});
    }
    return true;
}

const WonRewards& ChallengeService::wonRewards() const {
    static const WonRewards none;
    // The reading of the account logged in (or of the last one, while nobody is).
    if (!won_.known || serverStreakFor_.empty()) return none;
    if (online_ && online_->loggedIn() && online_->playerId() != serverStreakFor_) return none;
    return won_;
}

void ChallengeService::setWon(WonRewards w) {
    if (w == won_) return;
    won_ = std::move(w);
    ++rewardsRevision_;
    if (onRewardsChanged) onRewardsChanged();
}

void ChallengeService::fetchWon() {
    if (!online_ || !online_->usable() || !online_->loggedIn() || wonBusy_) return;
    wonBusy_ = true;
    std::string who = online_->playerId();
    online_->get("/api/v1/players/me/rewards", OnlineService::guard(life_, [this, who](const ApiResult& r) {
                     wonBusy_ = false;
                     // An older server (no such route) or another account meanwhile: nothing changes.
                     if (!r.ok || online_->playerId() != who || !resultsMine()) return;
                     auto w = WonRewards::fromJson(jfield(r.data, "rewards", Json()));
                     if (!w) return;
                     setWon(std::move(*w));
                     if (!r.fromCache) saveState();
                 }),
                 true);
}

const ChallengeRewardProgress& ChallengeService::rewardProgress() const {
    static const ChallengeRewardProgress none;
    // The reading of the account logged in (or of the last one, while nobody is).
    if (!rewards_.known || serverStreakFor_.empty()) return none;
    if (online_ && online_->loggedIn() && online_->playerId() != serverStreakFor_) return none;
    return rewards_;
}

void ChallengeService::setRewardProgress(const ChallengeRewardProgress& p) {
    if (p == rewards_) return;
    rewards_ = p;
    ++rewardsRevision_;
    if (onRewardsChanged) onRewardsChanged();
}

std::optional<ChallengeRewardProgress> ChallengeService::rewardProgressFromJson(const Json& v) {
    if (!v.is_object()) return std::nullopt;
    ChallengeRewardProgress p;
    for (int k = 0; k < kChallengeKinds; ++k) {
        const Json& o = jfield(v, challengeKindId(static_cast<ChallengeKind>(k)));
        if (!o.is_object()) return std::nullopt;
        auto num = [&](const char* key) { return std::clamp(jget<int>(o, key, 0), 0, 1000000); };
        p.atLeast[k][2] = num("gold");
        // « At that tier or better »: never fewer silvers than golds, nor bronzes than silvers.
        p.atLeast[k][1] = std::max(num("silver"), p.atLeast[k][2]);
        p.atLeast[k][0] = std::max(num("bronze"), p.atLeast[k][1]);
        p.bestStreak[k] = num("best_streak");
    }
    p.known = true;
    return p;
}

ChallengeRewardProgress ChallengeService::rewardProgressFromResults(const Json& results) {
    ChallengeRewardProgress p;
    p.known = true;
    if (!results.is_array()) return p;
    std::array<std::set<int64_t>, kChallengeKinds> inTime;
    std::set<std::string> seen;
    for (const Json& x : results) {
        if (jstr(x, "status") != "VERIFIED") continue;
        std::string id = jstr(x, "challenge_id");
        if (!safeId(id) || !seen.insert(id).second) continue;
        // A 2.32 result has neither kind nor period: a day.
        auto kind = challengeKindFromId(jstr(x, "kind", "daily"));
        if (!kind) continue;
        const int k = static_cast<int>(*kind);
        const int tier = static_cast<int>(challengeTierFromJson(jfield(x, "tier", Json())));
        for (int t = 1; t <= tier && t <= 3; ++t) ++p.atLeast[k][t - 1];
        std::string period = jstr(x, "period", *kind == ChallengeKind::Daily ? jstr(x, "date") : std::string());
        if (tier > 0 && !jget<bool>(x, "late", false))
            if (auto i = challenge_period::index(*kind, period)) inTime[static_cast<size_t>(k)].insert(*i);
    }
    for (int k = 0; k < kChallengeKinds; ++k) {
        int run = 0;
        int64_t prev = 0;
        for (int64_t i : inTime[static_cast<size_t>(k)]) {
            run = run > 0 && i == prev + 1 ? run + 1 : 1;
            prev = i;
            p.bestStreak[k] = std::max(p.bestStreak[k], run);
        }
    }
    return p;
}

void ChallengeService::noteServerResult(const std::string& id, const Json& x) {
    ServerResult sr;
    sr.bestMs = std::max<int64_t>(0, jget<int64_t>(x, "best_time_ms", 0));
    sr.tier = challengeTierFromJson(jfield(x, "tier", Json()));
    sr.status = challengeRunStateFromServer(jstr(x, "status"));
    sr.late = jget<bool>(x, "late", false);
    if (const Json& p = jfield(x, "best_progress", Json()); p.is_number()) sr.bestProgress = std::clamp(p.get<float>(), 0.0f, 100.0f);
    serverResults_[id] = sr;
}

void ChallengeService::refreshMine(ApiCallback cb) {
    if (!online_ || !online_->usable() || !online_->loggedIn()) {
        if (cb) {
            ApiResult r;
            r.code = "NOT_LOGGED_IN";
            r.message = "Connectez-vous à Cosmic Online.";
            cb(r);
        }
        return;
    }
    if (mineBusy_) {
        // A reading is already on its way (2.35: e.g. after a run just verified): its answer
        // is this one's too.
        if (cb) mineWaiters_.push_back(std::move(cb));
        return;
    }
    mineBusy_ = true;
    std::string who = online_->playerId();
    online_->get("/api/v1/challenges/me?limit=60", OnlineService::guard(life_, [this, cb, who](const ApiResult& r) {
        mineBusy_ = false;
        if (r.ok && online_->playerId() == who && resultsMine()) {
            auto readStreak = [](const Json& s) {
                return Streak{std::max(0, jget<int>(s, "current", 0)), std::max(0, jget<int>(s, "best", 0)),
                              jstr(s, "last_period", jstr(s, "last_date")), true};
            };
            // 2.33: one streak per kind; a 2.32 server gives the day's alone ("streak").
            const Json& streaks = jfield(r.data, "streaks", Json());
            const Json& daily = jfield(r.data, "streak", Json());
            for (int k = 0; k < kChallengeKinds; ++k) {
                const Json& s = streaks.is_object() ? jfield(streaks, challengeKindId(static_cast<ChallengeKind>(k))) : k == 0 ? daily : Json();
                if (s.is_object()) serverStreaks_[k] = readStreak(s);
                else serverStreaks_[k].reset();
            }
            const Json& results = jfield(r.data, "results", Json::array());
            // 2.35: what the rewards of the challenges count (a 2.33 server: its results).
            if (!r.fromCache) rewardsStale_ = false;
            if (auto v = rewardProgressFromJson(jfield(r.data, "verified", Json()))) setRewardProgress(*v);
            else setRewardProgress(rewardProgressFromResults(results));
            bool changed = false;
            if (results.is_array())
                for (const Json& x : results) {
                    std::string id = jstr(x, "challenge_id");
                    if (!safeId(id)) continue;
                    noteServerResult(id, x);
                    ChallengeRunState st = serverResults_[id].status;
                    int64_t best = serverResults_[id].bestMs;
                    // My run of that time: its state is the server's.
                    for (auto& sub : submissions_)
                        if (!sub.attempt && sub.challengeId == id && sub.ownerId == who && sub.timeMs == best && !sub.runId.empty() && sub.state != st &&
                            (sub.state == ChallengeRunState::Sent || sub.state == ChallengeRunState::Pending)) {
                            sub.state = st;
                            changed = true;
                        }
                }
            if (changed) saveSubmissions();
            if (!r.fromCache) saveState();
            // 2.43: the rewards won at single challenges (granted once a challenge is over).
            if (!r.fromCache) fetchWon();
        }
        std::vector<ApiCallback> waiters;
        waiters.swap(mineWaiters_);
        if (cb) cb(r);
        for (auto& w : waiters) w(r);
    }), true);
}

std::optional<ChallengeService::ServerResult> ChallengeService::serverResult(const std::string& challengeId) const {
    if (!online_ || !online_->loggedIn() || serverStreakFor_ != online_->playerId()) return std::nullopt;
    auto it = serverResults_.find(challengeId);
    if (it == serverResults_.end()) return std::nullopt;
    return it->second;
}

std::optional<ChallengeRunState> ChallengeService::serverStatus(const std::string& challengeId) const {
    auto r = serverResult(challengeId);
    if (!r) return std::nullopt;
    return r->status;
}

// ------------------------------------------------------------------ runs

ChallengeService::RunReport ChallengeService::recordRun(const DailyChallenge& c, int64_t timeMs, int deaths, int damageTaken,
                                                        const std::string& levelHash, const std::string& attemptId,
                                                        const ReplayData* replay, bool sendable, const std::string& whyNot) {
    RunReport rep;
    rep.tier = c.tierFor(timeMs, damageTaken);
    // After the end (and its grace): « hors délai », in « Depuis » only, never the streak.
    rep.late = lateNow(c);
    ChallengeResult& r = results_[c.id];
    bool first = r.finishes == 0;
    r.challengeId = c.id;
    r.kind = c.kind;
    r.period = c.period;
    r.date = c.date;
    rep.previousBestMs = r.bestMs;
    rep.newBest = r.bestMs == 0 || timeMs < r.bestMs;
    bool betterTier = rep.tier > r.tier;
    ++r.finishes;
    if (!rep.late) ++r.liveFinishes;
    if (betterTier) r.tier = rep.tier;
    if (rep.newBest) {
        r.bestMs = timeMs;
        r.deaths = deaths;
        r.damageTaken = damageTaken;
        r.bestAttemptId = attemptId;
        r.bestLate = rep.late;
        r.hasReplay = false;
        if (replay && safeId(c.id)) {
            std::vector<uint8_t> bytes = replay->encode();
            std::error_code ec;
            fs::create_directories(bestReplayPath(c.id).parent_path(), ec);
            r.hasReplay = !bytes.empty() && FileUtil::writeBinary(bestReplayPath(c.id), bytes);
        }
    }
    saveResults();
    // Sent: the first run of the challenge (the streak), the first one in time after late ones,
    // a better time or a better tier.
    bool worth = first || rep.newBest || betterTier || (!rep.late && r.liveFinishes == 1);
    if (!online_ || !online_->loggedIn()) {
        rep.state = ChallengeRunState::LocalOnly;
        rep.note = "Sans compte : ni classement ni envoi. Connectez-vous à Cosmic Online pour entrer dans le classement du défi.";
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
    s.late = rep.late;
    s.createdAt = FileUtil::nowString();
    if (replay && safeId(attemptId)) {
        std::vector<uint8_t> bytes = replay->encode();
        std::error_code ec;
        fs::create_directories(uploadPath(attemptId).parent_path(), ec);
        s.hasReplay = !bytes.empty() && FileUtil::writeBinary(uploadPath(attemptId), bytes);
    }
    s.state = ChallengeRunState::Waiting;
    submissions_.push_back(s);
    trimSubmissions();
    saveSubmissions();
    rep.state = ChallengeRunState::Waiting;
    sendTimer_ = 0.0f;
    flush();
    return rep;
}

const ChallengeSubmission* ChallengeService::submission(const std::string& attemptId, bool attempt) const {
    for (const auto& s : submissions_)
        if (s.attemptId == attemptId && s.attempt == attempt) return &s;
    return nullptr;
}

ChallengeSubmission* ChallengeService::findSubmission(const std::string& attemptId, bool attempt) {
    for (auto& s : submissions_)
        if (s.attemptId == attemptId && s.attempt == attempt) return &s;
    return nullptr;
}

void ChallengeService::trimSubmissions() {
    // The oldest decided ones go beyond the limit (the waiting ones always stay).
    while (submissions_.size() > kMaxSubmissions) {
        auto it = std::find_if(submissions_.begin(), submissions_.end(), [](const ChallengeSubmission& x) {
            return x.state != ChallengeRunState::Waiting && x.state != ChallengeRunState::Sent;
        });
        if (it == submissions_.end()) break;
        submissions_.erase(it);
    }
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

fs::path ChallengeService::uploadPath(const std::string& attemptId, bool attempt) const {
    return dir_ / "upload" / (attemptId + (attempt ? "-attempt.cprz" : ".cprz"));
}

void ChallengeService::applyRun(ChallengeSubmission& s, const Json& run) {
    if (!run.is_object()) return;
    // A run's id is the server's; an attempt is known by the game's own attempt_id.
    std::string id = jstr(run, "id");
    if (safeId(id) && !s.attempt) s.runId = id;
    if (run.contains("status") && run["status"].is_string()) {
        ChallengeRunState was = s.state;
        s.state = challengeRunStateFromServer(run["status"].get<std::string>());
        // A run just verified may unlock a reward of the challenges: /challenges/me soon.
        if (!s.attempt && s.state == ChallengeRunState::Verified && was != ChallengeRunState::Verified) {
            rewardsStale_ = true;
            mineTimer_ = 0.0f;
        }
    }
    if (run.contains("tier")) s.serverTier = challengeTierFromJson(run["tier"]);
    if (run.contains("late") && run["late"].is_boolean()) s.late = run["late"].get<bool>();
    if (const Json& p = jfield(run, "progress", Json()); p.is_number()) s.serverProgress = std::clamp(p.get<float>(), 0.0f, 100.0f);
}

void ChallengeService::flush() {
    if (sending_ || !online_ || !online_->usable() || !online_->loggedIn()) return;
    sendNext();
}

void ChallengeService::sendNext() {
    const std::string me = online_->playerId();
    for (auto& s : submissions_) {
        if (s.ownerId != me) continue;
        if (s.state == ChallengeRunState::Sent && s.hasReplay && !s.replayUploaded && (s.attempt || !s.runId.empty())) {
            upload(s.attemptId, s.attempt);
            return;
        }
        if (s.state != ChallengeRunState::Waiting) continue;
        sending_ = true;
        std::string attemptId = s.attemptId;
        bool attempt = s.attempt;
        online_->send("POST", s.postPath(), s.body(), OnlineService::guard(life_, [this, attemptId, attempt](const ApiResult& r) {
            sending_ = false;
            ChallengeSubmission* sub = findSubmission(attemptId, attempt);
            if (!sub) return;
            if (r.ok) {
                applyRun(*sub, jfield(r.data, attempt ? "attempt" : "run", Json()));
                const Json& rp = jfield(r.data, "replay", Json::object());
                std::string up = rp.is_object() ? jstr(rp, "upload_path") : std::string();
                bool required = rp.is_object() && jget<bool>(rp, "required", false);
                bool accepted = rp.is_object() && jget<bool>(rp, "accepted", true);
                // An attempt's replay goes to PUT /challenges/attempts/{attempt_id}/replay.
                const std::string prefix = attempt ? "/api/v1/challenges/attempts/" : "/api/v1/challenges/runs/";
                if (attempt && up.empty()) up = prefix + attemptId + "/replay";
                if (sub->hasReplay && !sub->replayUploaded && accepted && up.rfind(prefix, 0) == 0 && up.find("..") == std::string::npos &&
                    sub->state != ChallengeRunState::Rejected) {
                    sub->uploadPath = up;
                    sub->state = ChallengeRunState::Sent;  // until its replay is there
                } else if (required && !sub->hasReplay) {
                    sub->note = attempt ? "Replay manquant : l'essai ne pourra pas être classé." : "Replay manquant : le temps ne pourra pas être vérifié.";
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
            if (r.code == "CHALLENGE_ENDED") sub->note = "Envoyé trop tard : ce serveur n'accepte pas encore les parties hors délai.";
            else if (r.code == "RULES_MISMATCH") sub->note = "Règles différentes de celles du défi.";
            else if (r.code == "LEVEL_MISMATCH") sub->note = "Version du niveau différente de celle du défi.";
            else if (r.code == "GAME_VERSION_UNSUPPORTED") sub->note = "Version du jeu trop ancienne : mettez le jeu à jour.";
            else if (r.code == "CHALLENGE_NOT_FOUND") sub->note = "Défi inconnu du serveur.";
            else if (r.code == "PROGRESS_UNAVAILABLE") sub->note = "Ce défi n'a pas de classement « Plus loin » sur le serveur.";
            else if (r.code == "ATTEMPT_REJECTED") sub->note = "Essai refusé par le serveur.";
            else if (attempt && r.status == 404) sub->note = "Ce serveur ne classe pas encore les essais (« Plus loin »).";
            else sub->note = r.message;
            saveSubmissions();
            online_->notify(OnlineService::Notification::Kind::Warning, attempt ? "Défi : essai refusé" : "Défi : temps refusé", sub->note);
            sendTimer_ = 0.0f;
        }));
        return;
    }
}

void ChallengeService::upload(const std::string& attemptId, bool attempt) {
    ChallengeSubmission* s = findSubmission(attemptId, attempt);
    if (!s) return;
    auto bytes = FileUtil::readBinary(uploadPath(attemptId, attempt));
    if (!bytes) {
        // The file is gone: nothing to upload (the server keeps the run without its proof).
        s->replayUploaded = true;
        s->state = ChallengeRunState::Pending;
        saveSubmissions();
        return;
    }
    sending_ = true;
    online_->sendBytes("PUT", s->uploadPath, std::string(bytes->begin(), bytes->end()), OnlineService::guard(life_, [this, attemptId, attempt](const ApiResult& r) {
        sending_ = false;
        ChallengeSubmission* sub = findSubmission(attemptId, attempt);
        if (!sub) return;
        if (!r.ok && unreachable(r)) {
            sendTimer_ = kRetrySeconds;
            return;
        }
        sub->replayUploaded = true;
        std::error_code ec;
        fs::remove(uploadPath(attemptId, attempt), ec);
        if (r.ok) {
            applyRun(*sub, jfield(r.data, attempt ? "attempt" : "run", Json()));
            if (sub->state == ChallengeRunState::Sent) sub->state = ChallengeRunState::Pending;
            // The server's best progress on this challenge (« Plus loin »), as it now stands.
            if (attempt && resultsMine())
                if (const Json& bp = jfield(r.data, "best_progress", Json()); bp.is_number()) {
                    ServerResult& sr = serverResults_[sub->challengeId];
                    sr.bestProgress = std::max(sr.bestProgress, std::clamp(bp.get<float>(), 0.0f, 100.0f));
                }
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
    // A challenge that just ended (or none of the day known): the next ones are asked for
    // (once a minute at most).
    if (fetch_ != Fetch::Never && fetch_ != Fetch::Loading && clock_ - lastFetch_ > kRefreshMissingSeconds &&
        (!current() || (knownEndMs_ > 0 && nowMs() >= knownEndMs_)))
        refresh();
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
        bool waiting = rewardsStale_;
        for (const auto& s : submissions_)
            waiting = waiting || (s.ownerId == online_->playerId() && (s.state == ChallengeRunState::Sent || s.state == ChallengeRunState::Pending));
        if (waiting) refreshMine();
    }
}

// ------------------------------------------------------------------ « Plus loin » (2.33)

float ChallengeService::bestProgress(const std::string& challengeId) const {
    float best = 0.0f;
    if (const ChallengeResult* r = result(challengeId)) best = r->bestMs > 0 ? 100.0f : r->bestProgress;
    if (auto s = serverResult(challengeId)) {
        if (s->bestMs > 0) best = 100.0f;
        if (s->bestProgress > best) best = s->bestProgress;
    }
    return best;
}

bool ChallengeService::beatsBestAttempt(float progress, float best) {
    // Scores have one decimal (raceScore): compared in tenths, strictly better only.
    long p = std::lround(progress * 10.0f), b = std::lround(best * 10.0f);
    return p >= 1 && p < 1000 && p > b;
}

ChallengeService::AttemptReport ChallengeService::recordAttempt(const DailyChallenge& c, float progress, int64_t timeMs, std::vector<int> bosses,
                                                                std::vector<int> waves, const std::string& levelHash, const std::string& attemptId,
                                                                const ReplayData* replay, bool sendable, const std::string& whyNot) {
    AttemptReport rep;
    rep.progress = std::clamp(std::floor(progress * 10.0f) / 10.0f, 0.0f, 99.9f);
    rep.late = lateNow(c);
    if (!c.progressAvailable) return rep;  // no route: no « Plus loin » for this challenge
    rep.tier = c.tierForProgress(rep.progress);
    rep.previousBest = bestProgress(c.id);
    rep.newBest = beatsBestAttempt(rep.progress, rep.previousBest);
    if (!rep.newBest) {
        rep.note = rep.previousBest >= 100.0f ? "Défi déjà fini : un essai qui s'arrête avant ne change rien."
                                              : "Pas plus loin que votre meilleur essai : rien n'est envoyé.";
        return rep;
    }
    ChallengeResult& r = results_[c.id];
    r.challengeId = c.id;
    r.kind = c.kind;
    r.period = c.period;
    r.date = c.date;
    r.bestProgress = rep.progress;
    r.bestProgressAttemptId = attemptId;
    // « Finir le niveau » (2.41): its tier counts here like a run's (and, in time, for the streak).
    if (rep.tier > r.tier) r.tier = rep.tier;
    if (rep.tier != ChallengeTier::None && !rep.late) r.liveTier = true;
    saveResults();
    if (!online_ || !online_->loggedIn()) {
        rep.note = "Sans compte : gardé sur ce PC. Connectez-vous à Cosmic Online pour entrer dans le classement « Plus loin ».";
        return rep;
    }
    if (!sendable) {
        rep.note = whyNot.empty() ? std::string("Non envoyé.") : whyNot;
        return rep;
    }
    ChallengeSubmission s;
    s.attempt = true;
    s.attemptId = attemptId;
    s.challengeId = c.id;
    s.ownerId = online_->playerId();
    s.timeMs = timeMs;
    s.levelHash = levelHash;
    s.rulesHash = c.rulesHash;
    s.gameVersion = COSMIC_VERSION_STRING;
    s.late = rep.late;
    s.bosses = std::move(bosses);
    s.waves = std::move(waves);
    s.progress = rep.progress;
    s.createdAt = FileUtil::nowString();
    if (replay && safeId(attemptId)) {
        std::vector<uint8_t> bytes = replay->encode();
        std::error_code ec;
        fs::create_directories(uploadPath(attemptId, true).parent_path(), ec);
        s.hasReplay = !bytes.empty() && FileUtil::writeBinary(uploadPath(attemptId, true), bytes);
    }
    // One attempt kept per challenge (the best): an older one still waiting is replaced.
    submissions_.erase(std::remove_if(submissions_.begin(), submissions_.end(),
                                      [&](const ChallengeSubmission& x) {
                                          return x.attempt && x.challengeId == c.id && x.ownerId == s.ownerId && x.state == ChallengeRunState::Waiting;
                                      }),
                       submissions_.end());
    s.state = ChallengeRunState::Waiting;
    submissions_.push_back(s);
    trimSubmissions();
    saveSubmissions();
    rep.state = ChallengeRunState::Waiting;
    sendTimer_ = 0.0f;
    flush();
    return rep;
}

// ------------------------------------------------------------------ leaderboard, ghost

std::string ChallengeService::leaderboardPath(const std::string& challengeId, const std::string& scope, bool allPeriod, bool progressBoard,
                                              bool timeExplicit) {
    // period=live and board=time are the defaults (the only ones a 2.32 server knows): left out,
    // except board=time on a « Finir le niveau » challenge (2.41), said explicitly: there the
    // progress board is the main one.
    return "/api/v1/challenges/" + challengeId + "/leaderboard?scope=" + online::encodeQueryValue(scope) + (allPeriod ? "&period=all" : "") +
           (progressBoard ? "&board=progress" : timeExplicit ? "&board=time" : "") + "&limit=50";
}

void ChallengeService::leaderboard(const std::string& challengeId, const std::string& scope, ApiCallback cb, bool allPeriod, bool progressBoard) {
    if (!online_ || !safeId(challengeId)) return;
    const DailyChallenge* c = find(challengeId);
    online_->get(leaderboardPath(challengeId, scope, allPeriod, progressBoard, c && c->finishObjective), std::move(cb), true);
}

fs::path ChallengeService::runReplayPath(const std::string& challengeId, const std::string& runId, bool attempt) const {
    return dir_ / "ghosts" / (challengeId + (attempt ? "-attempt-" : "-") + runId + ".cprz");
}

void ChallengeService::downloadRunReplay(const std::string& challengeId, const std::string& runId,
                                         std::function<void(std::optional<fs::path>, const std::string&)> cb, bool attempt) {
    if (!online_ || !safeId(challengeId) || !safeId(runId)) {
        if (cb) cb(std::nullopt, "Replay inconnu.");
        return;
    }
    fs::path file = runReplayPath(challengeId, runId, attempt);
    if (FileUtil::exists(file)) {
        if (cb) cb(file, {});
        return;
    }
    // A run's replay, or (« Plus loin ») an attempt's, by the server's id of that attempt.
    online_->getBinary(std::string(attempt ? "/api/v1/challenges/attempts/" : "/api/v1/challenges/runs/") + runId + "/replay",
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
