// Clan raids « Asterion », phase 1 (game/online/Raids): the times shown in the computer's own
// zone with its daylight saving time (and in the zones of the administrators' preview), the
// parsing of the server's UTC times and of the forms, the states and countdowns computed like
// the server's (server/src/raids.ts), and what the tab RAIDS and the sheet of an event offer.

#include <cstdlib>
#include <ctime>
#include <string>

#include "TestFramework.h"
#include "game/online/Raids.h"

using namespace cosmic;

namespace {

int64_t utcOf(int y, int m, int d, int h, int mi, int s = 0) { return raids::daysFromCivil(y, m, d) * 86400 + h * 3600 + mi * 60 + s; }

raids::Civil wall(int y, int m, int d, int h, int mi) {
    raids::Civil c;
    c.year = y;
    c.month = m;
    c.day = d;
    c.hour = h;
    c.minute = mi;
    return c;
}

const raids::Zone& zone(const std::string& name) {
    for (const raids::Zone& z : raids::previewZones())
        if (z.name == name) return z;
    static raids::Zone none;
    return none;
}

/// A world event of the server starting at `start` (UTC), standard format, lock 30 min.
Json eventJson(int64_t start, int lock = 30) {
    return Json{{"id", "e1"},
                {"kind", "world"},
                {"name", "Asterion"},
                {"format", "standard"},
                {"format_min", 45},
                {"format_max", 60},
                {"starts_at", raids::isoUtc(start)},
                {"lock_at", raids::isoUtc(start - lock * 60)},
                {"ends_at", raids::isoUtc(start + 60 * 60)},
                {"lock_minutes", lock},
                {"registration_open", true},
                {"capacity", 0},
                {"clan_capacity", 50},
                {"difficulty", 4},
                {"published", true},
                {"cancelled", false},
                {"clans", 23},
                {"players", 142},
                {"my_clan", {{"registered", true}, {"players", 11}, {"groups", 2}, {"leader", "Lyra"}}},
                {"registered", false}};
}

}  // namespace

TEST_CASE("raids: UTC to local time across the daylight saving time changes") {
    const raids::Zone& paris = zone("Paris");
    CHECK(paris.hasDst);
    // 29 March 2026, 01:00 UTC: 01:59:59 CET, then 03:00 CEST at once.
    raids::Civil a = raids::toLocal(utcOf(2026, 3, 29, 0, 59, 59), paris);
    CHECK(a.hour == 1 && a.minute == 59 && a.offsetMinutes == 60 && !a.dst);
    raids::Civil b = raids::toLocal(utcOf(2026, 3, 29, 1, 0), paris);
    CHECK(b.hour == 3 && b.minute == 0 && b.offsetMinutes == 120 && b.dst);
    // 25 October 2026, 01:00 UTC: 02:59:59 CEST, then 02:00 CET again.
    raids::Civil c = raids::toLocal(utcOf(2026, 10, 25, 0, 59, 59), paris);
    CHECK(c.hour == 2 && c.minute == 59 && c.offsetMinutes == 120);
    raids::Civil d = raids::toLocal(utcOf(2026, 10, 25, 1, 0), paris);
    CHECK(d.hour == 2 && d.minute == 0 && d.offsetMinutes == 60);
    // The maquette's event: Saturday 21 November 2026 at 21:00 in Paris is 20:00 UTC.
    CHECK(raids::fromLocal(wall(2026, 11, 21, 21, 0), paris) == utcOf(2026, 11, 21, 20, 0));
    CHECK(raids::isoUtc(raids::fromLocal(wall(2026, 11, 21, 21, 0), paris)) == "2026-11-21T20:00:00Z");
    CHECK(raids::fromLocal(wall(2026, 7, 14, 21, 0), paris) == utcOf(2026, 7, 14, 19, 0));
    // A time skipped by the change moves forward (02:30 → 03:30 CEST); a time lived twice is the first one.
    const int64_t gap = raids::fromLocal(wall(2026, 3, 29, 2, 30), paris);
    CHECK(gap == utcOf(2026, 3, 29, 1, 30));
    CHECK(raids::toLocal(gap, paris).hour == 3);
    CHECK(raids::fromLocal(wall(2026, 10, 25, 2, 30), paris) == utcOf(2026, 10, 25, 0, 30));
    // North America changes on other days (8 March, 1 November 2026).
    const raids::Zone& mtl = zone("Montréal");
    CHECK(raids::toLocal(utcOf(2026, 3, 8, 6, 59), mtl).hour == 1);
    CHECK(raids::toLocal(utcOf(2026, 3, 8, 7, 0), mtl).hour == 3);
    CHECK(raids::toLocal(utcOf(2026, 11, 21, 20, 0), mtl).hour == 15);
    CHECK(raids::toLocal(utcOf(2026, 11, 1, 5, 59), mtl).offsetMinutes == -240);
    CHECK(raids::toLocal(utcOf(2026, 11, 1, 6, 0), mtl).offsetMinutes == -300);
    // Without daylight saving time; the next day in Tokyo.
    const raids::Civil tokyo = raids::toLocal(utcOf(2026, 11, 21, 20, 0), zone("Tokyo"));
    CHECK(tokyo.hour == 5 && tokyo.day == 22 && tokyo.weekday == 0);
    CHECK(raids::toLocal(utcOf(2026, 11, 21, 20, 0), zone("São Paulo")).hour == 17);
    // Southern hemisphere: the summer time goes across the new year.
    const raids::Zone sydney{"Sydney", 600, 660, true, {10, 1, 0, 120}, {4, 1, 0, 180}};
    CHECK(raids::toLocal(utcOf(2026, 11, 21, 20, 0), sydney).offsetMinutes == 660);
    CHECK(raids::toLocal(utcOf(2027, 1, 15, 0, 0), sydney).offsetMinutes == 660);
    CHECK(raids::toLocal(utcOf(2026, 7, 1, 0, 0), sydney).offsetMinutes == 600);
    CHECK(raids::fromLocal(wall(2026, 12, 25, 18, 0), sydney) == utcOf(2026, 12, 25, 7, 0));
}

TEST_CASE("raids: the computer's own zone and its daylight saving time") {
    // Whatever the computer's zone: a local time read back gives the same instant (outside a change).
    for (int64_t u : {utcOf(2026, 1, 15, 12, 0), utcOf(2026, 7, 15, 12, 0), utcOf(2026, 11, 21, 20, 0)}) {
        const raids::Civil c = raids::toSystemLocal(u);
        CHECK(raids::fromSystemLocal(c) == u);
        CHECK(raids::wallSeconds(c) - u == static_cast<int64_t>(c.offsetMinutes) * 60);
    }
#ifndef _WIN32
    // The system's own rules (POSIX TZ: no time zone database needed).
    const char* before = std::getenv("TZ");
    const std::string saved = before ? before : "";
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    raids::Civil a = raids::toSystemLocal(utcOf(2026, 3, 29, 0, 59, 59));
    CHECK(a.hour == 1 && a.offsetMinutes == 60 && !a.dst);
    raids::Civil b = raids::toSystemLocal(utcOf(2026, 3, 29, 1, 0));
    CHECK(b.hour == 3 && b.offsetMinutes == 120 && b.dst);
    CHECK(raids::fromSystemLocal(wall(2026, 11, 21, 21, 0)) == utcOf(2026, 11, 21, 20, 0));
    CHECK(raids::fromSystemLocal(wall(2026, 3, 29, 2, 30)) == utcOf(2026, 3, 29, 1, 30));
    CHECK(raids::fromSystemLocal(wall(2026, 10, 25, 2, 30)) == utcOf(2026, 10, 25, 0, 30));
    if (before) setenv("TZ", saved.c_str(), 1);
    else unsetenv("TZ");
    tzset();
#endif
}

TEST_CASE("raids: UTC times of the server, dates and clocks of the forms, labels") {
    CHECK(raids::parseUtc("2026-11-21T20:00:00.000Z") == utcOf(2026, 11, 21, 20, 0));
    CHECK(raids::parseUtc("2026-11-21T20:00Z") == utcOf(2026, 11, 21, 20, 0));
    CHECK(raids::parseUtc("2026-11-21T20:00:30Z") == utcOf(2026, 11, 21, 20, 0, 30));
    CHECK(!raids::parseUtc("2026-11-21T21:00:00+01:00"));
    CHECK(!raids::parseUtc("2026-11-21T21:00:00"));
    CHECK(!raids::parseUtc("2026-02-29T10:00:00Z"));
    CHECK(raids::parseUtc("2028-02-29T10:00:00Z").has_value());
    CHECK(!raids::parseUtc("2026-11-21T24:00:00Z"));
    CHECK(!raids::parseUtc(""));
    CHECK(raids::isoUtc(utcOf(2026, 1, 2, 3, 4, 5)) == "2026-01-02T03:04:05Z");
    CHECK(raids::isoUtc(0) == "1970-01-01T00:00:00Z");
    int y = 0, m = 0, d = 0, h = 0, mi = 0;
    CHECK(raids::parseDate("21/11/2026", y, m, d) && y == 2026 && m == 11 && d == 21);
    CHECK(raids::parseDate(" 1/2/27 ", y, m, d) && y == 2027 && m == 2 && d == 1);
    CHECK(raids::parseDate("21-11-2026", y, m, d));
    CHECK(!raids::parseDate("31/11/2026", y, m, d));
    CHECK(!raids::parseDate("2026/11/21", y, m, d));
    CHECK(!raids::parseDate("21/11", y, m, d));
    CHECK(raids::parseClock("21:00", h, mi) && h == 21 && mi == 0);
    CHECK(raids::parseClock("9h05", h, mi) && h == 9 && mi == 5);
    CHECK(raids::parseClock("21h", h, mi) && h == 21 && mi == 0);
    CHECK(!raids::parseClock("24:00", h, mi));
    CHECK(!raids::parseClock("21:5", h, mi));
    CHECK(!raids::parseClock("21:", h, mi));
    CHECK(raids::offsetLabel(0) == "UTC");
    CHECK(raids::offsetLabel(60) == "UTC+1");
    CHECK(raids::offsetLabel(-180) == "UTC-3");
    CHECK(raids::offsetLabel(330) == "UTC+5:30");
    const raids::Civil sat = raids::civilAt(utcOf(2026, 11, 21, 20, 0), 60);
    CHECK(raids::dateLong(sat) == "samedi 21 novembre");
    CHECK(raids::dateShort(sat) == "sam. 21 nov.");
    CHECK(raids::hhmm(sat) == "21:00");
    CHECK(raids::dayMonth(sat) == "21/11");
    CHECK(raids::dateField(sat) == "21/11/2026");
    CHECK(raids::countdown(3 * 86400 + 21 * 3600 + 40 * 60 + 12) == "3 j 21 h 40 min");
    CHECK(raids::countdown(21 * 3600 + 5 * 60) == "21 h 5 min");
    CHECK(raids::countdown(40 * 60 + 12) == "40 min 12 s");
    CHECK(raids::countdown(9) == "9 s");
    CHECK(raids::countdown(0) == "maintenant");
    CHECK(raids::countdownClock(3 * 86400 + 21 * 3600 + 40 * 60 + 12) == "3 j 21:40:12");
    CHECK(raids::countdownClock(3725) == "01:02:05");
    CHECK(raids::agoText(20) == "à l'instant");
    CHECK(raids::agoText(300) == "il y a 5 min");
    CHECK(raids::agoText(7200) == "il y a 2 h");
    CHECK(raids::agoText(90000) == "hier");
    CHECK(raids::agoText(4 * 86400) == "il y a 4 j");
    CHECK(raids::reminderLabel(1440) == "24 h avant");
    CHECK(raids::reminderLabel(60) == "1 h avant");
    CHECK(raids::reminderLabel(10) == "10 min avant");
}

TEST_CASE("raids: states and countdowns as the server computes them") {
    const int64_t start = utcOf(2026, 11, 21, 20, 0);
    raids::Event e = raids::parseEvent(eventJson(start));
    CHECK(e.valid);
    CHECK(e.lockAt == start - 1800);
    CHECK(e.endsAt == start + 3600);
    CHECK(e.clanRegistered && e.clanPlayers == 11 && e.clanGroups == 2 && e.clanLeader == "Lyra");
    CHECK(raids::stateAt(e, start - 86400) == raids::State::Open);
    CHECK(raids::stateAt(e, start - 1800) == raids::State::Imminent);
    CHECK(raids::stateAt(e, start) == raids::State::Running);
    CHECK(raids::stateAt(e, start + 3600) == raids::State::Finished);
    raids::Event closed = e;
    closed.registrationOpen = false;
    CHECK(raids::stateAt(closed, start - 86400) == raids::State::Scheduled);
    raids::Event full = e;
    full.capacity = 142;
    CHECK(raids::stateAt(full, start - 86400) == raids::State::Scheduled);
    raids::Event gone = e;
    gone.cancelled = true;
    CHECK(raids::stateAt(gone, start - 86400) == raids::State::Cancelled);
    raids::Event draft = e;
    draft.published = false;
    CHECK(raids::stateAt(draft, start - 86400) == raids::State::Draft);
    CHECK(raids::stateLabel(raids::State::Open) == "Inscriptions ouvertes");
    CHECK(raids::stateLabel(raids::State::Running) == "En cours");
    const raids::Countdown before = raids::countdownOf(e, start - (3 * 86400 + 21 * 3600 + 40 * 60), false);
    CHECK(before.caption == "Début dans");
    CHECK(before.value == "3 j 21 h 40 min");
    CHECK(raids::countdownOf(e, start + 600, true).value == "00:50:00");
    CHECK(raids::countdownOf(e, start + 4000, false).value.empty());
    CHECK(raids::formatLine(e) == "Raid standard · 45 à 60 min");
    CHECK(raids::eventName(e) == "Asterion · raid standard");
    CHECK(raids::difficultyLabel(4) == "Difficulté 4 / 5");
    CHECK(raids::formatLabel("legendaire") == "Raid légendaire");
    CHECK(!raids::parseEvent(Json{{"id", "x"}, {"starts_at", "2026-11-21T21:00:00+01:00"}}).valid);
    CHECK(!raids::parseEvent(Json()).valid);
}

TEST_CASE("raids: the button of an event and the layout of the tab") {
    const int64_t start = utcOf(2026, 11, 21, 20, 0), early = start - 86400;
    raids::Event e = raids::parseEvent(eventJson(start));
    CHECK(raids::primaryAction(e, early, false, "", "").id == "login");
    CHECK(raids::primaryAction(e, early, true, "", "").id.empty());
    CHECK(raids::primaryAction(e, early, true, "", "").note == "Rejoignez un clan pour participer aux raids.");
    CHECK(raids::primaryAction(e, early, true, "c1", "member").id == "register");
    e.registered = true;
    CHECK(raids::primaryAction(e, early, true, "c1", "member").id == "unregister");
    CHECK(!raids::primaryAction(e, start - 600, true, "c1", "member").enabled);
    e.registered = false;
    CHECK(raids::primaryAction(e, start - 600, true, "c1", "member").label == "Inscriptions verrouillées");
    CHECK(raids::primaryAction(e, start + 60, true, "c1", "member").note == "Le combat arrive dans une prochaine version.");
    e.clanRegistered = false;
    CHECK(raids::primaryAction(e, early, true, "c1", "officer").id == "register_clan");
    CHECK(raids::primaryAction(e, early, true, "c1", "member").note == "Le chef ou un officier inscrit d'abord le clan.");
    e.clanRegistered = true;
    e.clanPlayers = 50;
    CHECK(raids::primaryAction(e, early, true, "c1", "member").label == "Complet");

    const int64_t s2 = start + 86400;
    Json overview = {{"world", Json::array({eventJson(start)})},
                     {"clan_events", Json::array({eventJson(start), [&] {
                                                      Json j = eventJson(s2);
                                                      j["id"] = "c2";
                                                      j["kind"] = "clan";
                                                      return j;
                                                  }()})},
                     {"clan", nullptr}};
    raids::TabLayout out = raids::tabLayout(overview, false);
    CHECK(out.hero && out.heroId == "e1");
    CHECK(!out.clanBoxes && !out.canOrganize);
    CHECK(out.clanNote == "Connectez-vous pour voir les raids de votre clan.");
    raids::TabLayout alone = raids::tabLayout(overview, true);
    CHECK(alone.clanNote == "Rejoignez un clan pour participer aux raids et en organiser.");
    overview["clan"] = {{"id", "c1"}, {"tag", "AUBE"}, {"role", "member"}};
    raids::TabLayout member = raids::tabLayout(overview, true);
    CHECK(member.clanBoxes && !member.canOrganize && !member.canAnswer);
    overview["clan"]["role"] = "officer";
    raids::TabLayout officer = raids::tabLayout(overview, true);
    CHECK(officer.canOrganize && officer.canAnswer && officer.clanNote.empty());
    const auto list = raids::clanRaidList(overview, officer.heroId);
    CHECK(list.size() == 1);
    CHECK(list[0].id == "c2");
    overview["world"] = Json::array();
    CHECK(!raids::tabLayout(overview, true).hero);
}

TEST_CASE("raids: groups of 8 and reminders") {
    Json g = Json::array({Json{{"number", 1},
                               {"leader", {{"id", "p2"}, {"username", "Lyra"}}},
                               {"members", Json::array({Json{{"id", "p2"}, {"username", "Lyra"}}, Json{{"id", "p1"}, {"username", "Orion"}}})}},
                          Json{{"number", 2}, {"leader", {{"id", "p9"}, {"username", "Orbis"}}}, {"members", Json::array()}}});
    const auto groups = raids::parseGroups(g);
    CHECK(groups.size() == 1);
    CHECK(groups[0].leader.username == "Lyra");
    CHECK(groups[0].members.size() == 2);
    CHECK(raids::reminderPrefs(Json{{"reminders", Json::array({10, 1440, 30, 10})}}) == std::vector<int>({1440, 10}));
    CHECK(raids::reminderPrefs(Json()) == raids::reminderChoices());
    CHECK(raids::reminderPrefs(Json{{"reminders", Json::array()}}).empty());
}

TEST_CASE("raids: the results of an event and the rewards to claim (2.67, validated tables only)") {
    const Json j = Json::parse(R"({
        "event": {"id": "e", "state": "en_cours", "final": false},
        "gigaboss": {"boss": "Pyroclast", "defeated": true, "fights": [{"group": "[AUBE]·1", "phase": 4}, {"group": "[NEBU]·2", "phase": 2}]},
        "clans": [{"rank": 1, "tag": "AUBE", "points": 4100, "players": 2, "gigaboss": 1}],
        "players": [
            {"rank": 1, "id": "a", "username": "Lyra", "clan_tag": "AUBE", "score": 2300, "damage": 900, "boss_damage": 700, "avoided": 40,
             "support": 6, "blocks": 4, "rescues": 2, "vents": 5, "weak_points": 2, "objectives": 4, "gigaboss": 1, "seconds": 1500, "progress": 1500},
            {"rank": 2, "id": "b", "username": "Talos", "clan_tag": "AUBE", "score": 1800, "damage": 3000, "boss_damage": 2600, "avoided": 10,
             "support": 0, "vents": 0, "objectives": 2, "gigaboss": 1, "seconds": 900, "progress": 600, "me": true}],
        "my": {"rank": 2, "pending": true, "pending_reports": 3, "validated_at": "2026-11-21T20:10:00Z"},
        "rewards": [{"id": 7, "reward": "pyroclast_badge", "name": "Badge « Main de Pyroclast »", "kind": "badge", "why": "Présent à la chute de Pyroclast", "event_id": "e"},
                    {"id": 0, "name": "sans id"}]
    })");
    const raids::Results r = raids::parseResults(j);
    CHECK(r.valid);
    CHECK(!r.final);
    CHECK(r.defeated);
    CHECK(r.fights.size() == 2);
    CHECK(r.fights[0] == "[AUBE]·1 · vaincu");
    CHECK(r.fights[1] == "[NEBU]·2 · phase 2 / 3");
    CHECK(r.pending);  // shown « en attente de validation », never as final
    CHECK(r.pendingReports == 3);
    CHECK(r.myRank == 2);
    CHECK(r.rewards.size() == 1);  // a reward without an id is never shown
    CHECK(r.rewards[0].kind == "badge");
    // The contribution does not favour the damage: the most damage is second by contribution...
    CHECK(raids::rankedBy(r, "score")[0].username == "Lyra");
    // ... first by damage, and the support counts the vents held for the group.
    CHECK(raids::rankedBy(r, "damage")[0].username == "Talos");
    CHECK(raids::categoryValue(r.players[0], "support") == 11);
    CHECK(raids::categoryText(r.players[0], "time") == "25 min");
    CHECK(raids::groupDigits(48210) == "48 210");
    CHECK(raids::resultCategories().size() >= 8);
    CHECK(raids::parseRewards(Json::array()).empty());
}
