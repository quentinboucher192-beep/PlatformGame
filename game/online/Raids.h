#pragma once
// Clan raids « ASTERION, LE DÉVOREUR DE MONDES », phase 1 (server/src/raids.ts): what the game
// shows of them, without drawing anything. The server decides every rule (roles, lock time,
// capacity) and stores every time in UTC; the game converts to the computer's own time zone,
// with its daylight saving time, each time it shows one (never stored), and sends UTC.
//
// Time: plain arithmetic on seconds since 1970 (UTC), the computer's zone read from the system
// (localtime), and rule-based zones for the preview of ADMINISTRATION › Raids (Paris,
// Montréal, São Paulo, Tokyo) and the tests (daylight saving time changes).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {

namespace raids {

constexpr int kGroupSize = 8;
constexpr int kMaxInstructions = 600;
constexpr int kMaxRewardsText = 300;
/// A clan raid: 2 to 50 places, 15 minutes to 90 days ahead (server: the same limits).
constexpr int kMinPlaces = 2;
constexpr int kMaxPlaces = 50;
constexpr int kMinAheadMinutes = 15;

// ------------------------------------------------------------------ time

/// A wall clock: its date and time, `weekday` 0 = dimanche, `offsetMinutes` its gap to UTC.
struct Civil {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    int weekday = 4;
    int offsetMinutes = 0;
    bool dst = false;
};

/// Days since 1970-01-01 of a date of the proleptic Gregorian calendar.
int64_t daysFromCivil(int y, int m, int d);
int daysInMonth(int y, int m);
/// The UTC clock shifted by `offsetMinutes`.
Civil civilAt(int64_t utcSeconds, int offsetMinutes);
/// The seconds of a wall clock read as if it were UTC (its offset ignored).
int64_t wallSeconds(const Civil& c);
/// "2026-11-21T20:00:00.000Z" (also without seconds or milliseconds); nothing for anything else
/// (an offset, an impossible date).
std::optional<int64_t> parseUtc(const std::string& iso);
/// "2026-11-21T20:00:00Z"
std::string isoUtc(int64_t utcSeconds);

/// A daylight saving time change: the `week`-th (1 to 4, 5 = the last) `weekday` of `month`, at
/// `minute` after midnight of the local time in force before the change.
struct DstRule {
    int month = 3, week = 5, weekday = 0, minute = 120;
};
/// A time zone given by its rules (standard offset, daylight offset and changes, in minutes).
struct Zone {
    std::string name;
    int stdOffset = 0, dstOffset = 0;
    bool hasDst = false;
    DstRule start, end;
};
/// UTC → the zone's wall clock (with its daylight saving time).
Civil toLocal(int64_t utc, const Zone& z);
/// The zone's wall clock (date and time) → UTC.
int64_t fromLocal(const Civil& wall, const Zone& z);
/// The UTC of a wall clock (seconds read as UTC) given the offset in force at each UTC instant:
/// a time skipped by a change moves forward by the gap, a time lived twice is the first one.
int64_t resolveWall(int64_t wall, const std::function<int(int64_t)>& offsetMinutesAt);
/// The zones of the preview of ADMINISTRATION › Raids: Paris, Montréal, São Paulo, Tokyo.
const std::vector<Zone>& previewZones();

/// The computer's own zone (the system's, with its daylight saving time).
Civil toSystemLocal(int64_t utc);
int64_t fromSystemLocal(const Civil& wall);

/// "UTC", "UTC+1", "UTC-3", "UTC+5:30".
std::string offsetLabel(int offsetMinutes);
std::string weekdayName(int weekday);   // « samedi »
std::string weekdayShort(int weekday);  // « sam. »
std::string monthName(int month);       // « novembre »
std::string monthShort(int month);      // « nov. »
std::string hhmm(const Civil& c);       // « 21:00 »
std::string dateLong(const Civil& c);   // « samedi 21 novembre »
std::string dateShort(const Civil& c);  // « sam. 21 nov. »
std::string dayMonth(const Civil& c);   // « 21/11 »
std::string dateField(const Civil& c);  // « 21/11/2026 » (the forms)
/// « 3 j 21 h 40 min », « 21 h 40 min », « 40 min 12 s », « 12 s », « maintenant ».
std::string countdown(int64_t seconds);
/// « 3 j 21:40:12 », « 21:40:12 ».
std::string countdownClock(int64_t seconds);
/// « à l'instant », « il y a 5 min », « il y a 2 h », « hier », « il y a 3 j ».
std::string agoText(int64_t seconds);
/// « 21/11/2026 » (also « 21/11/26 », « 21-11-2026 », « 21.11.2026 »).
bool parseDate(const std::string& s, int& y, int& m, int& d);
/// « 21:00 », « 21h00 », « 21h », « 9:05 ».
bool parseClock(const std::string& s, int& h, int& mi);

// ------------------------------------------------------------------ events

enum class State { Draft, Open, Scheduled, Imminent, Running, Finished, Cancelled };

/// 2.69 (phase 5): the official result of a world event once frozen by the server (« result » of an
/// event in GET /raids and its sheet): the outcome, the world's progress, my clan's rank (0: not ranked).
struct EventResult {
    bool valid = false;
    std::string outcome, text;  // "vaincu" / "survecu", « Asterion a survécu »
    int progress = 0, gigabossDefeated = 0, gigaboss = 7, clans = 0, players = 0, myClanRank = 0;
};
EventResult parseEventResult(const Json& j);

/// An event as the server sends it (GET /raids, /raids/events/{id}, /admin/raids).
struct Event {
    std::string id, kind = "world", raid = "asterion", name = "Asterion", displayName = "ASTERION", displayTitle = "LE DÉVOREUR DE MONDES";
    std::string format = "standard";
    int formatMin = 45, formatMax = 60;
    int64_t startsAt = 0, lockAt = 0, endsAt = 0;
    int lockMinutes = 30;
    bool registrationOpen = true;
    int capacity = 0, clanCapacity = 50, difficulty = 3;
    bool published = true, cancelled = false;
    std::string organizerId, organizerTag, organizerName;
    int clans = 0, players = 0;
    bool clanRegistered = false;
    int clanPlayers = 0, clanGroups = 0;
    std::string clanLeader;
    bool registered = false;
    // 2.69 (phase 5): suspended by the administrators (nothing counts until the resumption), the late
    // arrivals (minutes after the start a newcomer may still enter play, -1: until the end), a catch-up
    // session (« session de rattrapage ») of an official world event, its official result once frozen.
    bool suspended = false;
    std::string suspendedReason;
    int lateMinutes = -1;
    bool catchup = false;
    std::string catchupOf;
    EventResult result;
    bool valid = false;  // an id and readable times
};
Event parseEvent(const Json& j);
/// The state at `now` (UTC seconds), as the server computes it.
State stateAt(const Event& e, int64_t now);
/// « Inscriptions ouvertes », « Programmé », « Imminent », « En cours », « Terminé », « Annulé », « Brouillon ».
std::string stateLabel(State s);
/// « Raid court », « Raid standard », « Raid légendaire ».
std::string formatLabel(const std::string& format);
/// « Raid standard · 45 à 60 min ».
std::string formatLine(const Event& e);
/// « Asterion · raid court ».
std::string eventName(const Event& e);
/// « Difficulté 4 / 5 ».
std::string difficultyLabel(int difficulty);
/// The reminders a player may choose (minutes before the start) and their labels (« 24 h avant »).
const std::vector<int>& reminderChoices();
std::string reminderLabel(int minutes);

// ------------------------------------------------------------------ what the screens offer

/// The main button of an event (hero card, sheet), from its state and the player's clan.
struct Action {
    /// "register", "unregister", "register_clan", "login" or "" (nothing to do: `label` says why).
    std::string id;
    std::string label;
    bool enabled = true;
    std::string note;  // why it is disabled, or what follows
};
Action primaryAction(const Event& e, int64_t now, bool loggedIn, const std::string& clanId, const std::string& role);

/// The countdown of an event: « Début dans » 3 j 21 h 40 min, « Fin prévue dans »…
struct Countdown {
    std::string caption, value;
};
Countdown countdownOf(const Event& e, int64_t now, bool clock);

/// What the tab RAIDS shows, from GET /raids.
struct TabLayout {
    bool hero = false;         // the next world event (else the empty hero, with its explanation)
    std::string heroId;
    bool clanBoxes = false;    // the clan's raids, invitations and history (a clan is known)
    bool canOrganize = false;  // « Organiser un raid du clan… » (chief, officers)
    bool canAnswer = false;    // the invitations get « Accepter » / « Refuser »
    std::string clanNote;      // why the clan's boxes are empty or read only
};
TabLayout tabLayout(const Json& overview, bool loggedIn);
/// The clan's raids of the list (GET /raids « clan_events »), the hero's event left out.
std::vector<Event> clanRaidList(const Json& overview, const std::string& heroId);

/// A group of 8 of the sheet.
struct Member {
    std::string id, username;
};
struct Group {
    int number = 0;
    Member leader;
    std::vector<Member> members;
};
std::vector<Group> parseGroups(const Json& groups);

/// The reminders chosen (GET /raids « prefs »): minutes before the start.
std::vector<int> reminderPrefs(const Json& prefs);

// ------------------------------------------------------------------ 2.68: results and rewards

/// A reward of the raids, granted by the server from validated results only (GET /raids
/// « rewards », the results of an event), claimed with POST /raids/rewards/claim.
struct Reward {
    int id = 0;
    std::string reward, name, kind, ref, why, eventId;
    bool claimed = false;
};
std::vector<Reward> parseRewards(const Json& list);

/// A player of the results of an event (validated tables only): the mixed score and each category.
struct ResultPlayer {
    int rank = 0;
    std::string id, username, clanTag;
    int score = 0, damage = 0, bossDamage = 0, avoided = 0, support = 0, blocks = 0, rescues = 0, vents = 0, weakPoints = 0, objectives = 0;
    int gigaboss = 0, seconds = 0, progress = 0;
    bool me = false;
};
struct ResultClan {
    int rank = 0;
    std::string tag;
    int points = 0, players = 0;
    bool gigaboss = false;
};
/// 2.75: the « Tableau des contributions » of a zone finished by a group of my clan (validated by the
/// instance's majority, docs/RAIDS.md §19): its branch (catalog id), zone, group and each player.
struct ZoneResultRow {
    std::string username;
    bool me = false;
    double share = 0.0;  // of the boss damage (shareOfBoss) or of the zone's damage
    int bossDamage = 0, damage = 0, support = 0, mechanisms = 0, objectives = 0;
    // 2.78: the mini-bosses (hearts, part of bossDamage), the deaths, the revivals done and received; a
    // table of before 2.78 has none of them (`v2` false: « — »).
    int miniDamage = 0, deaths = 0, revives = 0, revived = 0;
    bool v2 = false;
};
struct ZoneResultTable {
    std::string branch, group, acceptedAt;
    int zone = 0;
    uint32_t tick = 0;
    bool shareOfBoss = false;
    std::vector<ZoneResultRow> rows;
};

/// GET /raids/events/{id}/results.
struct Results {
    bool valid = false;
    bool final = false;       // the event is over: the ranking no longer moves
    std::string state;
    bool defeated = false;    // the gigaboss fell (a fight accepted)
    std::vector<std::string> fights;  // « [AUBE]·1 · vaincu », « [NEBU]·2 · phase 2 / 3 »
    std::vector<std::string> defeatedBosses;  // 2.69 (phase 4B): the gigaboss whose fall was accepted (« Pyroclast », « Voidworm »…)
    std::vector<ResultClan> clans;
    std::vector<ResultPlayer> players;
    int myRank = 0;
    bool pending = false;     // my instance's latest table waits for its majority
    int pendingReports = 0;
    std::string validatedAt;
    std::vector<Reward> rewards;  // mine to claim for this event
    std::vector<ZoneResultTable> zoneTables;  // 2.75: my clan's groups' zone tables (newest first)
    std::vector<ZoneResultRow> raidTotals;    // 2.78: « Raid entier »: each player's zones added up
};
Results parseResults(const Json& j);

/// The categories of the individual ranking (maquette « Résultats »): never only the damage.
struct Category {
    std::string id;     // "score", "damage", "avoided", "support", "objectives", "gigaboss", "time", "rescues", "progress"
    std::string label;  // « Contribution », « Dégâts »…
};
const std::vector<Category>& resultCategories();
/// A player's value in a category (the sort key) and its text (« 2 410 », « 12 min », « oui »).
int categoryValue(const ResultPlayer& p, const std::string& category);
std::string categoryText(const ResultPlayer& p, const std::string& category);
/// The players ranked by a category (best first, then by score, then by name), with their rank in it.
std::vector<ResultPlayer> rankedBy(const Results& r, const std::string& category);
/// « 48 210 » (a space between thousands, as the maquette).
std::string groupDigits(int64_t v);

// ------------------------------------------------------------------ 2.69: the world events (phase 5)

/// « 1er », « 2ᵉ »…
std::string ordinal(int rank);
/// « Arrivée en retard : jusqu'à la fin », « … : jusqu'à 30 min après le début », « … : non ».
std::string lateRuleText(const Event& e);
/// The history's line of a frozen result: « Asterion a survécu · 42 % · [AUBE] 2ᵉ / 23 ».
std::string resultLine(const EventResult& r, const std::string& clanTag);

/// The world rankings of a world event (GET /raids/events/{id}/world), from the validated results
/// only: every clan, the groups (instances), the players in every category, the world's progress.
struct WorldClan {
    int rank = 0;
    std::string id, tag, name;
    int points = 0, players = 0, registered = 0, groups = 0, objectives = 0, first = 0, gigaboss = 0;
    bool mine = false;
};
struct WorldGroup {
    int rank = 0;
    std::string id, clanTag, branch, branchName;
    int group = 0, points = 0, players = 0, objectives = 0, bossPhase = 0;
    bool mine = false, me = false;
};
struct WorldBranch {
    std::string part, name, gigaboss;
    int objectivesDone = 0, objectives = 0, zonesControlled = 0, zones = 0;
    bool down = false;
};
struct WorldRanking {
    bool valid = false;
    std::string state;          // the server's state of the event (« en_cours », « termine »…)
    bool final = false;         // the official result, frozen by the server: it no longer moves
    bool closing = false;       // ended, its official result not frozen yet (the last reports' grace)
    bool suspended = false, catchup = false;
    std::string frozenAt, catchupOf;
    int progress = 0, objectivesDone = 0, objectives = 0, zonesControlled = 0, zones = 0, gigabossDefeated = 0, gigaboss = 7;
    int clansRegistered = 0, clansPlaying = 0, participants = 0, groupsPlaying = 0;
    std::vector<WorldBranch> branches;
    std::string outcome, outcomeText;  // "vaincu", "survecu" or "en_cours"; « Asterion a survécu »
    std::vector<WorldClan> clans;
    std::vector<WorldGroup> groups;
    std::vector<ResultPlayer> players;
    int totalClans = 0, totalGroups = 0, totalPlayers = 0;
    // What waits for its majority (never ranked); once final: what was never validated.
    int pendingTables = 0, pendingObjectives = 0, pendingInstances = 0;
    std::string myClanTag;
    int myClanRank = 0, myClanPoints = 0, myGroupRank = 0, myRank = 0;
    bool myPending = false;
    int myPendingReports = 0;
    std::vector<std::string> myClanTitles;  // « Dévoreurs dévorés · 2ᵉ mondial »
    std::vector<Reward> rewards;            // mine to claim for this event
    // A catch-up session: the official result it never changes.
    bool officialFrozen = false;
    std::string officialText;
    int officialProgress = 0;
};
WorldRanking parseWorld(const Json& j);
/// « Progression mondiale 42 % · 18 / 43 objectifs · 1 / 7 gigaboss ».
std::string worldProgressLine(const WorldRanking& w);
/// What waits for its majority, said apart from the rankings ("" when nothing waits).
std::string pendingText(const WorldRanking& w);
/// The state of the ranking: « Résultat officiel · figé le … », « Classement en cours »…
std::string rankingStateText(const WorldRanking& w);
/// The players ranked by a category (as rankedBy), from any list.
std::vector<ResultPlayer> rankedPlayers(const std::vector<ResultPlayer>& players, const std::string& category);

}  // namespace raids

}  // namespace cosmic
