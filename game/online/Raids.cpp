#include "game/online/Raids.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "game/raid/Difficulty.h"

namespace cosmic::raids {

namespace {

int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

int weekdayOfDays(int64_t days) { return static_cast<int>(((days % 7) + 7 + 4) % 7); }

/// The day (since 1970) of a change in year `y`.
int64_t ruleDay(int y, const DstRule& r) {
    if (r.week >= 5) {
        const int64_t last = daysFromCivil(y, r.month, daysInMonth(y, r.month));
        return last - ((weekdayOfDays(last) - r.weekday + 7) % 7);
    }
    const int64_t first = daysFromCivil(y, r.month, 1);
    return first + ((r.weekday - weekdayOfDays(first) + 7) % 7) + 7 * (r.week - 1);
}

std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

/// Digits of `s` from `i` (at most `max`), moved past; -1 when there is none.
int digits(const std::string& s, size_t& i, int max) {
    int v = 0, n = 0;
    while (i < s.size() && n < max && std::isdigit(static_cast<unsigned char>(s[i]))) {
        v = v * 10 + (s[i] - '0');
        ++i;
        ++n;
    }
    return n == 0 ? -1 : v;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

int64_t timeOf(const Json& j, const char* key) {
    auto t = parseUtc(jstr(j, key));
    return t ? *t : 0;
}

}  // namespace

// ------------------------------------------------------------------ time

int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int daysInMonth(int y, int m) {
    static const int k[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : k[m - 1];
}

Civil civilAt(int64_t utcSeconds, int offsetMinutes) {
    const int64_t t = utcSeconds + static_cast<int64_t>(offsetMinutes) * 60;
    const int64_t days = floorDiv(t, 86400);
    const int64_t secs = t - days * 86400;
    Civil c;
    civilFromDays(days, c.year, c.month, c.day);
    c.hour = static_cast<int>(secs / 3600);
    c.minute = static_cast<int>((secs / 60) % 60);
    c.second = static_cast<int>(secs % 60);
    c.weekday = weekdayOfDays(days);
    c.offsetMinutes = offsetMinutes;
    return c;
}

int64_t wallSeconds(const Civil& c) { return daysFromCivil(c.year, c.month, c.day) * 86400 + c.hour * 3600 + c.minute * 60 + c.second; }

std::optional<int64_t> parseUtc(const std::string& s) {
    // YYYY-MM-DDTHH:MM[:SS[.fff]]Z
    if (s.size() < 17 || s.back() != 'Z') return std::nullopt;
    size_t i = 0;
    const int y = digits(s, i, 4);
    if (y < 0 || i != 4 || s[i++] != '-') return std::nullopt;
    const int mo = digits(s, i, 2);
    if (mo < 1 || mo > 12 || i >= s.size() || s[i++] != '-') return std::nullopt;
    const int d = digits(s, i, 2);
    if (d < 1 || d > daysInMonth(y, mo) || i >= s.size() || s[i++] != 'T') return std::nullopt;
    const int h = digits(s, i, 2);
    if (h < 0 || h > 23 || i >= s.size() || s[i++] != ':') return std::nullopt;
    const int mi = digits(s, i, 2);
    if (mi < 0 || mi > 59) return std::nullopt;
    int se = 0;
    if (i < s.size() && s[i] == ':') {
        ++i;
        se = digits(s, i, 2);
        if (se < 0 || se > 59) return std::nullopt;
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (digits(s, i, 3) < 0) return std::nullopt;
        }
    }
    if (i != s.size() - 1) return std::nullopt;
    return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
}

std::string isoUtc(int64_t utcSeconds) {
    const Civil c = civilAt(utcSeconds, 0);
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", c.year, c.month, c.day, c.hour, c.minute, c.second);
    return b;
}

Civil toLocal(int64_t utc, const Zone& z) {
    if (!z.hasDst) return civilAt(utc, z.stdOffset);
    const int y = civilAt(utc, z.stdOffset).year;
    const int64_t start = ruleDay(y, z.start) * 86400 + z.start.minute * 60 - static_cast<int64_t>(z.stdOffset) * 60;
    const int64_t end = ruleDay(y, z.end) * 86400 + z.end.minute * 60 - static_cast<int64_t>(z.dstOffset) * 60;
    // Northern hemisphere: summer inside the year; southern: across the new year.
    const bool dst = start < end ? (utc >= start && utc < end) : (utc >= start || utc < end);
    Civil c = civilAt(utc, dst ? z.dstOffset : z.stdOffset);
    c.dst = dst;
    return c;
}

int64_t resolveWall(int64_t wall, const std::function<int(int64_t)>& offsetAt) {
    const int64_t before = static_cast<int64_t>(offsetAt(wall - 86400)) * 60;
    const int64_t after = static_cast<int64_t>(offsetAt(wall + 86400)) * 60;
    auto lands = [&](int64_t u) { return u + static_cast<int64_t>(offsetAt(u)) * 60 == wall; };
    const int64_t a = wall - before, b = wall - after;
    const bool va = lands(a), vb = lands(b);
    if (va && vb) return std::min(a, b);
    if (va) return a;
    if (vb) return b;
    return wall - before;  // skipped by the change: as late as the gap
}

int64_t fromLocal(const Civil& wall, const Zone& z) {
    return resolveWall(wallSeconds(wall), [&z](int64_t u) { return toLocal(u, z).offsetMinutes; });
}

const std::vector<Zone>& previewZones() {
    static const std::vector<Zone> k = {
        // Europe: last Sunday of March 2:00 → last Sunday of October 3:00.
        {"Paris", 60, 120, true, {3, 5, 0, 120}, {10, 5, 0, 180}},
        // North America: second Sunday of March 2:00 → first Sunday of November 2:00.
        {"Montréal", -300, -240, true, {3, 2, 0, 120}, {11, 1, 0, 120}},
        {"São Paulo", -180, -180, false, {}, {}},
        {"Tokyo", 540, 540, false, {}, {}},
    };
    return k;
}

Civil toSystemLocal(int64_t utc) {
    const std::time_t t = static_cast<std::time_t>(utc);
    std::tm m{};
#ifdef _WIN32
    if (localtime_s(&m, &t) != 0) return civilAt(utc, 0);
#else
    if (!localtime_r(&t, &m)) return civilAt(utc, 0);
#endif
    Civil c;
    c.year = m.tm_year + 1900;
    c.month = m.tm_mon + 1;
    c.day = m.tm_mday;
    c.hour = m.tm_hour;
    c.minute = m.tm_min;
    c.second = std::min(m.tm_sec, 59);
    c.weekday = m.tm_wday;
    c.dst = m.tm_isdst > 0;
    c.offsetMinutes = static_cast<int>(floorDiv(wallSeconds(c) - utc, 60));
    return c;
}

int64_t fromSystemLocal(const Civil& wall) {
    return resolveWall(wallSeconds(wall), [](int64_t u) { return toSystemLocal(u).offsetMinutes; });
}

std::string offsetLabel(int offsetMinutes) {
    if (offsetMinutes == 0) return "UTC";
    const int a = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    std::string s = std::string("UTC") + (offsetMinutes < 0 ? "-" : "+") + std::to_string(a / 60);
    if (a % 60) s += ":" + two(a % 60);
    return s;
}

std::string weekdayName(int wd) {
    static const char* k[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
    return k[((wd % 7) + 7) % 7];
}
std::string weekdayShort(int wd) {
    static const char* k[] = {"dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam."};
    return k[((wd % 7) + 7) % 7];
}
std::string monthName(int m) {
    static const char* k[] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"};
    return m >= 1 && m <= 12 ? k[m - 1] : "";
}
std::string monthShort(int m) {
    static const char* k[] = {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."};
    return m >= 1 && m <= 12 ? k[m - 1] : "";
}
std::string hhmm(const Civil& c) { return two(c.hour) + ":" + two(c.minute); }
std::string dateLong(const Civil& c) { return weekdayName(c.weekday) + " " + std::to_string(c.day) + " " + monthName(c.month); }
std::string dateShort(const Civil& c) { return weekdayShort(c.weekday) + " " + std::to_string(c.day) + " " + monthShort(c.month); }
std::string dayMonth(const Civil& c) { return two(c.day) + "/" + two(c.month); }
std::string dateField(const Civil& c) { return two(c.day) + "/" + two(c.month) + "/" + std::to_string(c.year); }

std::string countdown(int64_t s) {
    if (s <= 0) return "maintenant";
    const int64_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (d > 0) return std::to_string(d) + " j " + std::to_string(h) + " h " + std::to_string(m) + " min";
    if (h > 0) return std::to_string(h) + " h " + std::to_string(m) + " min";
    if (m > 0) return std::to_string(m) + " min " + std::to_string(sec) + " s";
    return std::to_string(sec) + " s";
}

std::string countdownClock(int64_t s) {
    if (s < 0) s = 0;
    const int64_t d = s / 86400;
    const std::string clock = two(static_cast<int>((s % 86400) / 3600)) + ":" + two(static_cast<int>((s % 3600) / 60)) + ":" + two(static_cast<int>(s % 60));
    return d > 0 ? std::to_string(d) + " j " + clock : clock;
}

std::string agoText(int64_t s) {
    if (s < 60) return "à l'instant";
    if (s < 3600) return "il y a " + std::to_string(s / 60) + " min";
    if (s < 86400) return "il y a " + std::to_string(s / 3600) + " h";
    if (s < 2 * 86400) return "hier";
    return "il y a " + std::to_string(s / 86400) + " j";
}

bool parseDate(const std::string& raw, int& y, int& m, int& d) {
    const std::string s = trimmed(raw);
    size_t i = 0;
    const int dd = digits(s, i, 2);
    if (dd < 0 || i >= s.size() || (s[i] != '/' && s[i] != '-' && s[i] != '.')) return false;
    ++i;
    const int mm = digits(s, i, 2);
    if (mm < 0 || i >= s.size() || (s[i] != '/' && s[i] != '-' && s[i] != '.')) return false;
    ++i;
    const size_t at = i;
    int yy = digits(s, i, 4);
    if (yy < 0 || i != s.size()) return false;
    if (i - at == 2) yy += 2000;
    else if (i - at != 4) return false;
    if (mm < 1 || mm > 12 || dd < 1 || dd > daysInMonth(yy, mm)) return false;
    y = yy;
    m = mm;
    d = dd;
    return true;
}

bool parseClock(const std::string& raw, int& h, int& mi) {
    const std::string s = trimmed(raw);
    size_t i = 0;
    const int hh = digits(s, i, 2);
    if (hh < 0 || hh > 23) return false;
    int mm = 0;
    if (i < s.size()) {
        if (s[i] != ':' && s[i] != 'h' && s[i] != 'H') return false;
        ++i;
        if (i < s.size()) {
            const size_t at = i;
            mm = digits(s, i, 2);
            if (mm < 0 || mm > 59 || i - at != 2 || i != s.size()) return false;
        } else if (s[i - 1] == ':') return false;
    }
    h = hh;
    mi = mm;
    return true;
}

// ------------------------------------------------------------------ events

Event parseEvent(const Json& j) {
    Event e;
    if (!j.is_object()) return e;
    e.id = jstr(j, "id");
    e.kind = jstr(j, "kind", "world");
    e.raid = jstr(j, "raid", "asterion");
    e.name = jstr(j, "name", "Asterion");
    e.displayName = jstr(j, "display_name", "ASTERION");
    e.displayTitle = jstr(j, "display_title", "LE DÉVOREUR DE MONDES");
    e.format = jstr(j, "format", "standard");
    e.formatMin = jget<int>(j, "format_min", 45);
    e.formatMax = jget<int>(j, "format_max", 60);
    e.startsAt = timeOf(j, "starts_at");
    e.lockAt = timeOf(j, "lock_at");
    e.endsAt = timeOf(j, "ends_at");
    e.lockMinutes = jget<int>(j, "lock_minutes", 30);
    e.registrationOpen = jget<bool>(j, "registration_open", true);
    e.capacity = jget<int>(j, "capacity", 0);
    e.clanCapacity = jget<int>(j, "clan_capacity", 50);
    e.difficulty = std::clamp(jget<int>(j, "difficulty", 3), 1, 5);
    e.published = jget<bool>(j, "published", true);
    e.cancelled = jget<bool>(j, "cancelled", false);
    const Json org = jfield(j, "organizer", Json());
    if (org.is_object()) {
        e.organizerId = jstr(org, "id");
        e.organizerTag = jstr(org, "tag");
        e.organizerName = jstr(org, "name");
    }
    e.clans = jget<int>(j, "clans", 0);
    e.players = jget<int>(j, "players", 0);
    const Json mine = jfield(j, "my_clan", Json());
    if (mine.is_object()) {
        e.clanRegistered = jget<bool>(mine, "registered", false);
        e.clanPlayers = jget<int>(mine, "players", 0);
        e.clanGroups = jget<int>(mine, "groups", 0);
        e.clanLeader = jstr(mine, "leader");
    }
    e.registered = jget<bool>(j, "registered", false);
    // 2.69 (phase 5): suspended, the late arrivals, a catch-up session, the official result.
    e.suspended = jget<bool>(j, "suspended", false);
    e.suspendedReason = jstr(j, "suspended_reason");
    {
        const Json late = jfield(j, "late_minutes", Json());
        e.lateMinutes = late.is_number_integer() ? std::clamp(late.get<int>(), 0, 1440) : -1;
    }
    e.catchup = jget<bool>(j, "catchup", false);
    e.catchupOf = jstr(j, "catchup_of");
    e.result = parseEventResult(jfield(j, "result", Json()));
    e.valid = !e.id.empty() && e.startsAt > 0 && e.lockAt > 0 && e.endsAt > 0;
    return e;
}

EventResult parseEventResult(const Json& j) {
    EventResult r;
    if (!j.is_object()) return r;
    r.outcome = jstr(j, "outcome");
    r.text = jstr(j, "text");
    r.progress = std::clamp(jget<int>(j, "progress", 0), 0, 100);
    r.gigabossDefeated = std::max(0, jget<int>(j, "gigaboss_defeated", 0));
    r.gigaboss = std::max(1, jget<int>(j, "gigaboss", 7));
    r.clans = std::max(0, jget<int>(j, "clans", 0));
    r.players = std::max(0, jget<int>(j, "players", 0));
    r.myClanRank = std::max(0, jget<int>(j, "my_clan_rank", 0));
    r.valid = (r.outcome == "vaincu" || r.outcome == "survecu") && !r.text.empty();
    return r;
}

State stateAt(const Event& e, int64_t now) {
    if (e.cancelled) return State::Cancelled;
    if (!e.published) return State::Draft;
    if (now >= e.endsAt) return State::Finished;
    if (now >= e.startsAt) return State::Running;
    if (now >= e.lockAt) return State::Imminent;
    const bool full = e.capacity > 0 && e.players >= e.capacity;
    return e.registrationOpen && !full ? State::Open : State::Scheduled;
}

std::string stateLabel(State s) {
    switch (s) {
        case State::Draft: return "Brouillon";
        case State::Open: return "Inscriptions ouvertes";
        case State::Scheduled: return "Programmé";
        case State::Imminent: return "Imminent";
        case State::Running: return "En cours";
        case State::Finished: return "Terminé";
        case State::Cancelled: return "Annulé";
    }
    return "";
}

std::string formatLabel(const std::string& f) {
    if (f == "court") return "Raid court";
    if (f == "legendaire") return "Raid légendaire";
    return "Raid standard";
}

std::string formatLine(const Event& e) { return formatLabel(e.format) + " · " + std::to_string(e.formatMin) + " à " + std::to_string(e.formatMax) + " min"; }

std::string eventName(const Event& e) {
    const std::string f = e.format == "court" ? "raid court" : e.format == "legendaire" ? "raid légendaire" : "raid standard";
    return e.name + " · " + f;
}

std::string difficultyLabel(int d) { return "Difficulté " + std::to_string(std::clamp(d, 1, 5)) + " / 5"; }

std::string difficultyEffects(int d) {
    const raid::DifficultyScale s = raid::difficultyScale(d);
    auto pct = [](float f) { return std::to_string(static_cast<int>(std::lround(f * 100.0f))) + " %"; };
    return "Vie des Gigaboss : " + pct(s.health) + " · Dégâts reçus : " + pct(s.damage) + " · combat visé ≈ " + std::to_string(s.minutes) + " min";
}

const std::vector<int>& reminderChoices() {
    static const std::vector<int> k = {1440, 60, 10};
    return k;
}

std::string reminderLabel(int minutes) {
    if (minutes >= 60) return std::to_string(minutes / 60) + " h avant";
    return std::to_string(minutes) + " min avant";
}

Action primaryAction(const Event& e, int64_t now, bool loggedIn, const std::string& clanId, const std::string& role) {
    const State s = stateAt(e, now);
    switch (s) {
        case State::Cancelled: return {"", "Raid annulé", false, "Ce raid a été annulé."};
        case State::Finished: return {"", "Terminé", false, ""};
        case State::Draft: return {"", "Brouillon", false, "Pas encore publié."};
        default: break;
    }
    // 2.69 (phase 5): suspended by the administrators: nothing to do until the resumption.
    if (e.suspended && s == State::Running)
        return {"", "Événement suspendu", false, "Les administrateurs ont suspendu l'événement : rien ne compte jusqu'à la reprise."};
    if (s == State::Running) {
        if (e.registered) return {"", "Inscrit · en jeu", false, "Rejoignez le combat depuis la carte du titan."};
        // A catch-up session takes registrations until its end (a member's registration enters the clan).
        if (!e.catchup || now >= e.lockAt) return {"", "En cours", false, "Les inscriptions sont closes : le raid a commencé."};
        if (!loggedIn) return {"login", "Se connecter", true, "Un compte Cosmic Online et un clan sont nécessaires pour participer."};
        if (clanId.empty()) return {"", "Je m'inscris", false, "Rejoignez un clan pour participer aux raids."};
        if (e.clanCapacity > 0 && e.clanPlayers >= e.clanCapacity) return {"", "Complet", false, "Votre clan a atteint " + std::to_string(e.clanCapacity) + " inscrits."};
        return {"register", "Je m'inscris", true, "Session de rattrapage : votre inscription inscrit aussi votre clan."};
    }
    if (!loggedIn) return {"login", "Se connecter", true, "Un compte Cosmic Online et un clan sont nécessaires pour participer."};
    if (e.registered) {
        if (s == State::Imminent) return {"", "Inscrit", false, "Inscriptions verrouillées : rendez-vous au début."};
        return {"unregister", "Me désinscrire", true, ""};
    }
    if (s == State::Imminent) return {"", "Inscriptions verrouillées", false, "Verrouillées " + std::to_string(e.lockMinutes) + " min avant le début."};
    if (clanId.empty()) return {"", "Je m'inscris", false, "Rejoignez un clan pour participer aux raids."};
    const bool manager = role == "leader" || role == "officer";
    if (!e.clanRegistered) {
        if (e.kind == "clan") return {"", "Je m'inscris", false, "Votre clan ne participe pas à ce raid."};
        if (!e.registrationOpen) return {"", "Inscriptions fermées", false, "Les administrateurs du jeu ouvriront les inscriptions."};
        if (manager) return {"register_clan", "Inscrire mon clan", true, "Ensuite, chaque membre s'inscrit lui-même."};
        return {"", "Je m'inscris", false, "Le chef ou un officier inscrit d'abord le clan."};
    }
    if (!e.registrationOpen) return {"", "Inscriptions fermées", false, "Les inscriptions sont fermées pour l'instant."};
    if (e.capacity > 0 && e.players >= e.capacity) return {"", "Complet", false, "Toutes les places sont prises."};
    if (e.clanPlayers >= e.clanCapacity) return {"", "Complet", false, "Votre clan a atteint " + std::to_string(e.clanCapacity) + " inscrits."};
    return {"register", "Je m'inscris", true, ""};
}

Countdown countdownOf(const Event& e, int64_t now, bool clock) {
    auto fmt = [clock](int64_t s) { return clock ? countdownClock(s) : countdown(s); };
    switch (stateAt(e, now)) {
        case State::Cancelled: return {"Raid annulé", ""};
        case State::Finished: return {"Terminé", ""};
        case State::Running: return {"En cours · fin prévue dans", fmt(e.endsAt - now)};
        default: return {"Début dans", fmt(e.startsAt - now)};
    }
}

TabLayout tabLayout(const Json& overview, bool loggedIn) {
    TabLayout t;
    const Json world = jfield(overview, "world", Json::array());
    if (world.is_array()) {
        for (const Json& w : world) {
            const Event e = parseEvent(w);
            if (!e.valid) continue;
            t.hero = true;
            t.heroId = e.id;
            break;
        }
    }
    const Json clan = jfield(overview, "clan", Json());
    const std::string role = clan.is_object() ? jstr(clan, "role") : std::string();
    t.clanBoxes = loggedIn && clan.is_object();
    t.canAnswer = t.clanBoxes && (role == "leader" || role == "officer");
    if (!loggedIn) t.clanNote = "Connectez-vous pour voir les raids de votre clan.";
    else if (!clan.is_object()) t.clanNote = "Rejoignez un clan pour participer aux événements de raid.";
    return t;
}

std::vector<Event> clanRaidList(const Json& overview, const std::string& heroId) {
    std::vector<Event> out;
    const Json list = jfield(overview, "clan_events", Json::array());
    if (!list.is_array()) return out;
    for (const Json& j : list) {
        Event e = parseEvent(j);
        if (e.valid && e.id != heroId) out.push_back(std::move(e));
    }
    return out;
}

std::vector<Group> parseGroups(const Json& groups) {
    std::vector<Group> out;
    if (!groups.is_array()) return out;
    for (const Json& g : groups) {
        if (!g.is_object()) continue;
        Group x;
        x.number = jget<int>(g, "number", static_cast<int>(out.size()) + 1);
        const Json l = jfield(g, "leader", Json());
        if (l.is_object()) x.leader = {jstr(l, "id"), jstr(l, "username")};
        const Json m = jfield(g, "members", Json::array());
        if (m.is_array())
            for (const Json& p : m)
                if (p.is_object() && !jstr(p, "id").empty()) x.members.push_back({jstr(p, "id"), jstr(p, "username")});
        if (!x.members.empty()) out.push_back(std::move(x));
    }
    return out;
}

std::vector<int> reminderPrefs(const Json& prefs) {
    std::vector<int> out;
    const Json list = jfield(prefs, "reminders", Json());
    if (!list.is_array()) return reminderChoices();
    for (const Json& v : list)
        if (v.is_number_integer()) {
            const int m = v.get<int>();
            if (std::find(reminderChoices().begin(), reminderChoices().end(), m) != reminderChoices().end() &&
                std::find(out.begin(), out.end(), m) == out.end())
                out.push_back(m);
        }
    std::sort(out.begin(), out.end(), std::greater<int>());
    return out;
}

// ------------------------------------------------------------------ 2.68: results and rewards

std::vector<Reward> parseRewards(const Json& list) {
    std::vector<Reward> out;
    if (!list.is_array()) return out;
    for (const Json& r : list) {
        if (!r.is_object() || out.size() >= 100) continue;
        Reward x;
        x.id = jget<int>(r, "id", 0);
        x.reward = jstr(r, "reward");
        x.name = jstr(r, "name");
        x.kind = jstr(r, "kind");
        x.ref = jstr(r, "ref");
        x.why = jstr(r, "why");
        x.eventId = jstr(r, "event_id");
        x.claimed = jget<bool>(r, "claimed", false);
        if (x.id > 0 && !x.name.empty()) out.push_back(std::move(x));
    }
    return out;
}

namespace {
/// 2.78: the four numbers of a table of 2.78 (null in a table of before: `v2` stays false, shown « — »).
void readZoneV2(const Json& x, ZoneResultRow& row) {
    const Json d = jfield(x, "deaths", Json());
    if (!d.is_number()) return;
    row.v2 = true;
    row.deaths = std::max(0, jget<int>(x, "deaths", 0));
    row.revives = std::max(0, jget<int>(x, "revives", 0));
    row.revived = std::max(0, jget<int>(x, "revived", 0));
    row.miniDamage = std::max(0, jget<int>(x, "mini_damage", 0));
}
}  // namespace

Results parseResults(const Json& j) {
    Results r;
    if (!j.is_object()) return r;
    // 2.75: the zones' tables (bounded: the server sends 40 at most).
    if (const Json zt = jfield(j, "zone_tables", Json()); zt.is_array())
        for (const Json& t : zt) {
            if (r.zoneTables.size() >= 40) break;
            ZoneResultTable z;
            z.branch = jstr(t, "branch");
            z.group = jstr(t, "group");
            z.acceptedAt = jstr(t, "accepted_at");
            z.zone = std::clamp(jget<int>(t, "zone", 0), 0, 32);
            z.tick = static_cast<uint32_t>(std::max(0, jget<int>(t, "tick", 0)));
            z.shareOfBoss = jstr(t, "share_of") == "boss";
            if (const Json rows = jfield(t, "rows", Json()); rows.is_array())
                for (const Json& x : rows) {
                    if (z.rows.size() >= 32) break;
                    ZoneResultRow row;
                    row.username = jstr(x, "username");
                    row.me = jget<bool>(x, "me", false);
                    row.share = std::clamp(jget<double>(x, "share", 0.0), 0.0, 1.0);
                    row.bossDamage = std::max(0, jget<int>(x, "boss_damage", 0));
                    row.damage = std::max(0, jget<int>(x, "damage", 0));
                    row.support = std::max(0, jget<int>(x, "support", 0));
                    row.mechanisms = std::max(0, jget<int>(x, "mechanisms", 0));
                    row.objectives = std::max(0, jget<int>(x, "objectives", 0));
                    readZoneV2(x, row);
                    z.rows.push_back(std::move(row));
                }
            r.zoneTables.push_back(std::move(z));
        }
    // 2.78: « Raid entier » (each player's zones of my clan's groups added up).
    if (const Json rt = jfield(j, "raid_totals", Json()); rt.is_array())
        for (const Json& x : rt) {
            if (r.raidTotals.size() >= 64) break;
            ZoneResultRow row;
            row.username = jstr(x, "username");
            row.me = jget<bool>(x, "me", false);
            row.bossDamage = std::max(0, jget<int>(x, "boss_damage", 0));
            row.damage = std::max(0, jget<int>(x, "damage", 0));
            row.support = std::max(0, jget<int>(x, "support", 0));
            row.mechanisms = std::max(0, jget<int>(x, "mechanisms", 0));
            row.objectives = std::max(0, jget<int>(x, "objectives", 0));
            readZoneV2(x, row);
            r.raidTotals.push_back(std::move(row));
        }
    const Json ev = jfield(j, "event", Json());
    r.state = jstr(ev, "state");
    r.final = jget<bool>(ev, "final", false);
    const Json giga = jfield(j, "gigaboss", Json());
    r.defeated = jget<bool>(giga, "defeated", false);
    const Json fights = jfield(giga, "fights", Json());
    if (fights.is_array())
        for (const Json& f : fights) {
            const int phase = jget<int>(f, "phase", 0);
            const std::string boss = jstr(f, "boss");  // 2.69 (phase 4B): several gigaboss
            r.fights.push_back((boss.empty() ? std::string() : boss + " · ") + jstr(f, "group") + " · " +
                               (phase >= 4 ? std::string("vaincu") : "phase " + std::to_string(std::clamp(phase, 1, 3)) + " / 3"));
        }
    for (const Json& b : jfield(giga, "defeated_bosses", Json::array()))
        if (b.is_string()) r.defeatedBosses.push_back(b.get<std::string>());
    const Json clans = jfield(j, "clans", Json());
    if (clans.is_array())
        for (const Json& c : clans) {
            if (!c.is_object()) continue;
            r.clans.push_back({jget<int>(c, "rank", static_cast<int>(r.clans.size()) + 1), jstr(c, "tag"), jget<int>(c, "points", 0), jget<int>(c, "players", 0),
                               jget<int>(c, "gigaboss", 0) > 0});
        }
    const Json players = jfield(j, "players", Json());
    if (players.is_array())
        for (const Json& p : players) {
            if (!p.is_object()) continue;
            ResultPlayer x;
            x.rank = jget<int>(p, "rank", static_cast<int>(r.players.size()) + 1);
            x.id = jstr(p, "id");
            x.username = jstr(p, "username");
            x.clanTag = jstr(p, "clan_tag");
            x.score = jget<int>(p, "score", 0);
            x.damage = jget<int>(p, "damage", 0);
            x.bossDamage = jget<int>(p, "boss_damage", 0);
            x.avoided = jget<int>(p, "avoided", 0);
            x.support = jget<int>(p, "support", 0);
            x.blocks = jget<int>(p, "blocks", 0);
            x.rescues = jget<int>(p, "rescues", 0);
            x.vents = jget<int>(p, "vents", 0);
            x.weakPoints = jget<int>(p, "weak_points", 0);
            x.objectives = jget<int>(p, "objectives", 0);
            x.gigaboss = jget<int>(p, "gigaboss", 0);
            x.seconds = jget<int>(p, "seconds", 0);
            x.progress = jget<int>(p, "progress", 0);
            x.me = jget<bool>(p, "me", false);
            if (!x.id.empty()) r.players.push_back(std::move(x));
        }
    const Json my = jfield(j, "my", Json());
    r.myRank = jget<int>(my, "rank", 0);
    r.pending = jget<bool>(my, "pending", false);
    r.pendingReports = jget<int>(my, "pending_reports", 0);
    r.validatedAt = jstr(my, "validated_at");
    r.rewards = parseRewards(jfield(j, "rewards", Json()));
    r.valid = true;
    return r;
}

const std::vector<Category>& resultCategories() {
    static const std::vector<Category> list = {
        {"score", "Contribution"},     {"damage", "Dégâts"},       {"avoided", "Dégâts évités"}, {"support", "Soutien"},
        {"objectives", "Objectifs"},    {"gigaboss", "Gigaboss vaincus"}, {"time", "Temps de participation"}, {"rescues", "Sauvetages"},
        {"progress", "Progression"},
    };
    return list;
}

int categoryValue(const ResultPlayer& p, const std::string& c) {
    if (c == "damage") return p.damage;
    if (c == "avoided") return p.avoided;
    if (c == "support") return p.support + p.vents;  // shield blocks, rescues and vents held for the group
    if (c == "objectives") return p.objectives;
    if (c == "gigaboss") return p.gigaboss;
    if (c == "time") return p.seconds;
    if (c == "rescues") return p.rescues;
    if (c == "progress") return p.progress;
    return p.score;
}

std::string groupDigits(int64_t v) {
    const bool neg = v < 0;
    std::string d = std::to_string(neg ? -v : v), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += " ";
        out += d[i];
    }
    return neg ? "-" + out : out;
}

std::string categoryText(const ResultPlayer& p, const std::string& c) {
    if (c == "gigaboss") return p.gigaboss > 0 ? "présent à une chute" : "—";
    if (c == "time") return p.seconds >= 60 ? std::to_string(p.seconds / 60) + " min" : std::to_string(p.seconds) + " s";
    if (c == "damage") return groupDigits(p.damage) + " (" + groupDigits(p.bossDamage) + " gigaboss)";
    if (c == "support") return std::to_string(p.blocks) + " blocages · " + std::to_string(p.rescues) + " sauv. · " + std::to_string(p.vents) + " mécanismes";
    if (c == "objectives") return std::to_string(p.objectives) + (p.weakPoints > 0 ? " · " + std::to_string(p.weakPoints) + " points faibles" : "");
    return groupDigits(categoryValue(p, c));
}

std::vector<ResultPlayer> rankedBy(const Results& r, const std::string& c) {
    std::vector<ResultPlayer> out = r.players;
    std::stable_sort(out.begin(), out.end(), [&](const ResultPlayer& a, const ResultPlayer& b) {
        const int va = categoryValue(a, c), vb = categoryValue(b, c);
        if (va != vb) return va > vb;
        if (a.score != b.score) return a.score > b.score;
        return a.username < b.username;
    });
    for (size_t i = 0; i < out.size(); ++i) out[i].rank = static_cast<int>(i) + 1;
    return out;
}

// ------------------------------------------------------------------ 2.69: the world events (phase 5)

// 2.78.1: « 2e », never « ᵉ » (not in the game's fonts: an empty box).
std::string ordinal(int rank) { return rank == 1 ? "1er" : std::to_string(std::max(0, rank)) + "e"; }

std::string lateRuleText(const Event& e) {
    if (e.lateMinutes < 0) return "Arrivée en retard : jusqu'à la fin";
    if (e.lateMinutes == 0) return "Arrivée en retard : non";
    return "Arrivée en retard : jusqu'à " + std::to_string(e.lateMinutes) + " min après le début";
}

std::string resultLine(const EventResult& r, const std::string& clanTag) {
    if (!r.valid) return "";
    std::string t = r.text + " · " + std::to_string(r.progress) + " %";
    if (r.myClanRank > 0) t += " · " + (clanTag.empty() ? std::string("votre clan") : "[" + clanTag + "]") + " " + ordinal(r.myClanRank) + " / " + std::to_string(r.clans);
    return t;
}

namespace {

std::vector<ResultPlayer> parsePlayers(const Json& list) {
    std::vector<ResultPlayer> out;
    if (!list.is_array()) return out;
    for (const Json& p : list) {
        if (!p.is_object() || out.size() >= 200) continue;
        ResultPlayer x;
        x.rank = jget<int>(p, "rank", static_cast<int>(out.size()) + 1);
        x.id = jstr(p, "id");
        x.username = jstr(p, "username");
        x.clanTag = jstr(p, "clan_tag");
        x.score = jget<int>(p, "score", 0);
        x.damage = jget<int>(p, "damage", 0);
        x.bossDamage = jget<int>(p, "boss_damage", 0);
        x.avoided = jget<int>(p, "avoided", 0);
        x.support = jget<int>(p, "support", 0);
        x.blocks = jget<int>(p, "blocks", 0);
        x.rescues = jget<int>(p, "rescues", 0);
        x.vents = jget<int>(p, "vents", 0);
        x.weakPoints = jget<int>(p, "weak_points", 0);
        x.objectives = jget<int>(p, "objectives", 0);
        x.gigaboss = jget<int>(p, "gigaboss", 0);
        x.seconds = jget<int>(p, "seconds", 0);
        x.progress = jget<int>(p, "progress", 0);
        x.me = jget<bool>(p, "me", false);
        if (!x.id.empty()) out.push_back(std::move(x));
    }
    return out;
}

}  // namespace

WorldRanking parseWorld(const Json& j) {
    WorldRanking w;
    if (!j.is_object()) return w;
    const Json ev = jfield(j, "event", Json());
    w.state = jstr(ev, "state");
    w.final = jget<bool>(ev, "final", false);
    w.closing = jget<bool>(ev, "closing", false);
    w.suspended = jget<bool>(ev, "suspended", false);
    w.catchup = jget<bool>(ev, "catchup", false);
    w.frozenAt = jstr(ev, "frozen_at");
    w.catchupOf = jstr(ev, "catchup_of");
    const Json wd = jfield(j, "world", Json());
    w.progress = std::clamp(jget<int>(wd, "progress", 0), 0, 100);
    w.objectivesDone = jget<int>(wd, "objectives_done", 0);
    w.objectives = jget<int>(wd, "objectives", 0);
    w.zonesControlled = jget<int>(wd, "zones_controlled", 0);
    w.zones = jget<int>(wd, "zones", 0);
    w.gigabossDefeated = jget<int>(wd, "gigaboss_defeated", 0);
    w.gigaboss = std::max(1, jget<int>(wd, "gigaboss", 7));
    w.clansRegistered = jget<int>(wd, "clans", 0);
    w.clansPlaying = jget<int>(wd, "clans_playing", 0);
    w.participants = jget<int>(wd, "players", 0);
    w.groupsPlaying = jget<int>(wd, "groups", 0);
    const Json branches = jfield(wd, "branches", Json());
    if (branches.is_array())
        for (const Json& b : branches) {
            if (!b.is_object() || w.branches.size() >= 16) continue;
            w.branches.push_back({jstr(b, "part"), jstr(b, "name"), jstr(b, "gigaboss"), jget<int>(b, "objectives_done", 0), jget<int>(b, "objectives", 0),
                                  jget<int>(b, "zones_controlled", 0), jget<int>(b, "zones", 0), jget<bool>(b, "gigaboss_down", false)});
        }
    const Json oc = jfield(j, "outcome", Json());
    w.outcome = jstr(oc, "id", "en_cours");
    w.outcomeText = jstr(oc, "text");
    const Json clans = jfield(j, "clans", Json());
    if (clans.is_array())
        for (const Json& c : clans) {
            if (!c.is_object() || w.clans.size() >= 200) continue;
            WorldClan x;
            x.rank = jget<int>(c, "rank", static_cast<int>(w.clans.size()) + 1);
            x.id = jstr(c, "id");
            x.tag = jstr(c, "tag");
            x.name = jstr(c, "name");
            x.points = jget<int>(c, "points", 0);
            x.players = jget<int>(c, "players", 0);
            x.registered = jget<int>(c, "registered", 0);
            x.groups = jget<int>(c, "groups", 0);
            x.objectives = jget<int>(c, "objectives", 0);
            x.first = jget<int>(c, "first", 0);
            x.gigaboss = jget<int>(c, "gigaboss", 0);
            x.mine = jget<bool>(c, "mine", false);
            if (!x.id.empty()) w.clans.push_back(std::move(x));
        }
    const Json groups = jfield(j, "groups", Json());
    if (groups.is_array())
        for (const Json& g : groups) {
            if (!g.is_object() || w.groups.size() >= 200) continue;
            WorldGroup x;
            x.rank = jget<int>(g, "rank", static_cast<int>(w.groups.size()) + 1);
            x.id = jstr(g, "id");
            x.clanTag = jstr(g, "clan_tag");
            x.branch = jstr(g, "branch");
            x.branchName = jstr(g, "branch_name");
            x.group = jget<int>(g, "group", 1);
            x.points = jget<int>(g, "points", 0);
            x.players = jget<int>(g, "players", 0);
            x.objectives = jget<int>(g, "objectives", 0);
            x.bossPhase = std::clamp(jget<int>(g, "boss_phase", 0), 0, 4);
            x.mine = jget<bool>(g, "mine", false);
            x.me = jget<bool>(g, "me", false);
            if (!x.id.empty()) w.groups.push_back(std::move(x));
        }
    w.players = parsePlayers(jfield(j, "players", Json()));
    const Json totals = jfield(j, "totals", Json());
    w.totalClans = jget<int>(totals, "clans", static_cast<int>(w.clans.size()));
    w.totalGroups = jget<int>(totals, "groups", static_cast<int>(w.groups.size()));
    w.totalPlayers = jget<int>(totals, "players", static_cast<int>(w.players.size()));
    const Json pending = jfield(j, "pending", Json());
    w.pendingTables = std::max(0, jget<int>(pending, "tables", 0));
    w.pendingObjectives = std::max(0, jget<int>(pending, "objectives", 0));
    w.pendingInstances = std::max(0, jget<int>(pending, "instances", 0));
    const Json my = jfield(j, "my", Json());
    w.myClanTag = jstr(my, "clan_tag");
    w.myClanRank = jget<int>(my, "clan_rank", 0);
    w.myClanPoints = jget<int>(my, "clan_points", 0);
    w.myGroupRank = jget<int>(my, "group_rank", 0);
    w.myRank = jget<int>(my, "rank", 0);
    w.myPending = jget<bool>(my, "pending", false);
    w.myPendingReports = jget<int>(my, "pending_reports", 0);
    const Json titles = jfield(my, "clan_titles", Json());
    if (titles.is_array())
        for (const Json& t : titles)
            if (t.is_object() && !jstr(t, "title").empty() && w.myClanTitles.size() < 8)
                w.myClanTitles.push_back("« " + jstr(t, "title") + " » · " + ordinal(jget<int>(t, "rank", 1)) + " mondial");
    w.rewards = parseRewards(jfield(j, "rewards", Json()));
    const Json off = jfield(j, "official", Json());
    if (off.is_object()) {
        w.officialFrozen = jget<bool>(off, "frozen", false);
        w.officialText = jstr(off, "text");
        w.officialProgress = std::clamp(jget<int>(off, "progress", 0), 0, 100);
    }
    w.valid = true;
    return w;
}

std::string worldProgressLine(const WorldRanking& w) {
    return "Progression mondiale " + std::to_string(w.progress) + " % · " + std::to_string(w.objectivesDone) + " / " + std::to_string(w.objectives) + " objectifs · " +
           std::to_string(w.gigabossDefeated) + " / " + std::to_string(w.gigaboss) + " gigaboss";
}

std::string pendingText(const WorldRanking& w) {
    if (w.pendingTables <= 0 && w.pendingObjectives <= 0) return "";
    std::string what;
    if (w.pendingTables > 0) what = std::to_string(w.pendingTables) + (w.pendingTables > 1 ? " tableaux de contributions" : " tableau de contributions");
    if (w.pendingObjectives > 0)
        what += (what.empty() ? "" : " et ") + std::to_string(w.pendingObjectives) + (w.pendingObjectives > 1 ? " objectifs" : " objectif");
    const bool many = w.pendingTables + w.pendingObjectives > 1;
    if (w.final)
        return what + (many ? " jamais validés par la majorité de leur instance : ils ne comptent pas dans le résultat officiel."
                            : " jamais validé par la majorité de son instance : il ne compte pas dans le résultat officiel.");
    return what + (many ? " en attente de validation (majorité de leur instance) : ils ne comptent pas encore."
                        : " en attente de validation (majorité de son instance) : il ne compte pas encore.");
}

std::string rankingStateText(const WorldRanking& w) {
    if (w.catchup) return "Session de rattrapage · ce classement ne change pas le résultat officiel";
    if (w.final) {
        const auto t = parseUtc(w.frozenAt);
        return t ? "Résultat officiel · figé le " + dateShort(toSystemLocal(*t)) + " " + hhmm(toSystemLocal(*t)) : std::string("Résultat officiel");
    }
    if (w.closing) return "Terminé · résultat officiel en préparation (derniers rapports)";
    if (w.suspended) return "Événement suspendu · rien ne compte jusqu'à la reprise";
    return "Classement en cours · résultats validés seulement";
}

std::vector<ResultPlayer> rankedPlayers(const std::vector<ResultPlayer>& players, const std::string& c) {
    Results r;
    r.players = players;
    return rankedBy(r, c);
}

namespace {
Json& titanStore() {
    static Json titan = Json::object();
    return titan;
}
}  // namespace

void rememberTitan(const Json& data) { titanStore() = data.is_object() ? data : Json::object(); }
const Json& lastTitan() { return titanStore(); }

}  // namespace cosmic::raids
