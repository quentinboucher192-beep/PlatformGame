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

// ------------------------------------------------------------------ 2.67: results and rewards

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
/// GET /raids/events/{id}/results.
struct Results {
    bool valid = false;
    bool final = false;       // the event is over: the ranking no longer moves
    std::string state;
    bool defeated = false;    // the gigaboss fell (a fight accepted)
    std::vector<std::string> fights;  // « [AUBE]·1 · vaincu », « [NEBU]·2 · phase 2 / 3 »
    std::vector<ResultClan> clans;
    std::vector<ResultPlayer> players;
    int myRank = 0;
    bool pending = false;     // my instance's latest table waits for its majority
    int pendingReports = 0;
    std::string validatedAt;
    std::vector<Reward> rewards;  // mine to claim for this event
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

}  // namespace raids

}  // namespace cosmic
