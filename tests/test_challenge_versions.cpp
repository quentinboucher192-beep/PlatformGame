// 2.60: a challenge that has begun may still change (ADMINISTRATION › Calendrier des défis)
// while nobody has a result on it. The pure parts: what a reading of a challenge does to the copy
// kept here (challengeUpdate), the medals' counts read from the calendar, the periods of the
// administration's calendar (editable, why a running one is locked) and where the medals of
// the player's calendar fit (planMedals).

#include <array>
#include <cmath>

#include "game/online/DailyChallenges.h"
#include "tests/TestFramework.h"
#include "ui/screens/AdminCalendar.h"
#include "ui/screens/ChallengeUi.h"

using namespace cosmic;

namespace {

/// The day's challenge of 2026-11-17 as a 2.60 server writes it.
Json dayJson(const std::string& updatedAt, int hearts = 3, int64_t silverMs = 90000, const std::string& level = "glacior_02") {
    const std::string id = "daily-2026-11-17";
    Json rules = {{"hearts", hearts}, {"slots", 4}, {"powers", {"dash"}}, {"stations", true}, {"checkpoints", false}};
    Json j = {{"id", id},
              {"kind", "daily"},
              {"period", "2026-11-17"},
              {"date", "2026-11-17"},
              {"starts_at", "2026-11-17T00:00:00Z"},
              {"ends_at", "2026-11-18T00:00:00Z"},
              {"source", "admin"},
              {"level", {{"source", "campaign"}, {"level_id", level}, {"level_version", 8}, {"level_hash", std::string(64, 'a')}, {"title", "Grottes"}, {"author", nullptr}}},
              {"rules", rules},
              {"rules_hash", ChallengeRules::fromJson(id, rules)->hash()},
              {"tiers", {{"silver_ms", silverMs}, {"gold_ms", 70000}, {"gold_no_damage", true}}}};
    if (!updatedAt.empty()) j["updated_at"] = updatedAt;
    return j;
}

DailyChallenge day(const std::string& updatedAt, int hearts = 3, int64_t silverMs = 90000, const std::string& level = "glacior_02") {
    return *DailyChallenge::fromJson(dayJson(updatedAt, hearts, silverMs, level));
}

}  // namespace

TEST_CASE("challenge versions: a reading replaces the copy here only when it is not older") {
    const DailyChallenge v1 = day("2026-11-17T08:00:00.000Z");
    CHECK(v1.updatedAt == "2026-11-17T08:00:00.000Z");
    // Nothing kept yet: kept.
    CHECK(challengeUpdate(nullptr, v1) == ChallengeUpdate::Refresh);
    // The same answer: nothing to do.
    CHECK(challengeUpdate(&v1, day("2026-11-17T08:00:00.000Z")) == ChallengeUpdate::Keep);
    // Its title or its author's name changed: the same challenge to play.
    Json retitled = dayJson("2026-11-17T08:00:00.000Z");
    retitled["level"]["title"] = "Grottes de givre";
    CHECK(challengeUpdate(&v1, *DailyChallenge::fromJson(retitled)) == ChallengeUpdate::Refresh);
    // Changed by an administrator: its rules (their hash), its level, its tiers, its objective.
    CHECK(challengeUpdate(&v1, day("2026-11-17T09:00:00.000Z", 2)) == ChallengeUpdate::Changed);
    CHECK(challengeUpdate(&v1, day("2026-11-17T09:00:00.000Z", 3, 95000)) == ChallengeUpdate::Changed);
    CHECK(challengeUpdate(&v1, day("2026-11-17T09:00:00.000Z", 3, 90000, "pyralis_04")) == ChallengeUpdate::Changed);
    Json finish = dayJson("2026-11-17T09:00:00.000Z");
    finish["objective"] = "finish";
    finish["tiers"]["bronze_progress"] = 50;
    finish["tiers"]["silver_progress"] = 80;
    CHECK(challengeUpdate(&v1, *DailyChallenge::fromJson(finish)) == ChallengeUpdate::Changed);
    // An older answer (kept offline, an edge cache), to the millisecond: never over a newer one.
    const DailyChallenge v2 = day("2026-11-17T09:00:00.123Z", 2);
    CHECK(challengeUpdate(&v2, day("2026-11-17T09:00:00.122Z", 3)) == ChallengeUpdate::Keep);
    CHECK(challengeUpdate(&v2, v1) == ChallengeUpdate::Keep);
    // Without a time (an older server's answer) over one that has one: kept as it is.
    CHECK(challengeUpdate(&v2, day("", 3)) == ChallengeUpdate::Keep);
    // A copy of an older server (no time): the new version wins.
    const DailyChallenge old = day("", 3);
    CHECK(challengeUpdate(&old, v2) == ChallengeUpdate::Changed);
    // A malformed time is ignored (as from an older server).
    Json bad = dayJson("hier");
    CHECK(DailyChallenge::fromJson(bad)->updatedAt.empty());
    // The same version: the same level, rules, objective and tiers.
    CHECK(sameChallengeVersion(v1, day("2026-11-17T10:00:00.000Z")));
    CHECK(!sameChallengeVersion(v1, v2));
}

TEST_CASE("challenge versions: the medals' counts are whole numbers from the server, or nothing") {
    auto t = ChallengeTierCounts::fromJson({{"bronze", 12}, {"silver", 5}, {"gold", 0}});
    CHECK(t.has_value());
    CHECK(t && t->bronze == 12 && t->silver == 5 && t->gold == 0);
    CHECK(t && t->of(ChallengeTier::Silver) == 5 && t->of(ChallengeTier::None) == 0);
    CHECK(!ChallengeTierCounts::fromJson({{"bronze", -1}, {"silver", 5}, {"gold", 0}}).has_value());
    CHECK(!ChallengeTierCounts::fromJson({{"bronze", 1.5}, {"silver", 5}, {"gold", 0}}).has_value());
    CHECK(!ChallengeTierCounts::fromJson({{"bronze", 1}, {"silver", 5}}).has_value());
    CHECK(!ChallengeTierCounts::fromJson(Json("12 bronze")).has_value());
    CHECK(challenge_ui::medalsText({12, 5, 2}) == "12 bronze · 5 argent · 2 or (joueurs vérifiés)");
}

TEST_CASE("challenge versions: the periods of the administration's calendar say whether and why they may change") {
    Json challenge = dayJson("2026-11-17T08:00:00.000Z");
    Json data = {{"periods",
                  Json::array({
                      {{"id", "daily-2026-11-17"}, {"kind", "daily"}, {"period", "2026-11-17"}, {"date", "2026-11-17"}, {"end_date", "2026-11-18"},
                       {"state", "running"}, {"editable", true}, {"results_count", 0}, {"challenge", challenge}, {"rewards", Json::array()}},
                      {{"id", "weekly-2026-W47"}, {"kind", "weekly"}, {"period", "2026-W47"}, {"date", "2026-11-16"}, {"end_date", "2026-11-23"},
                       {"state", "running"}, {"editable", false}, {"results_count", 3}, {"challenge", nullptr}},
                      {{"id", "monthly-2026-11"}, {"kind", "monthly"}, {"period", "2026-11"}, {"date", "2026-11-01"}, {"end_date", "2026-12-01"},
                       {"state", "running"}, {"editable", false}, {"results_count", 1}},
                      // A 2.59 server: no count.
                      {{"id", "daily-2026-11-16"}, {"kind", "daily"}, {"period", "2026-11-16"}, {"date", "2026-11-16"}, {"end_date", "2026-11-17"},
                       {"state", "running"}, {"editable", false}},
                      // Over: never editable, whatever it says.
                      {{"id", "daily-2026-11-10"}, {"kind", "daily"}, {"period", "2026-11-10"}, {"date", "2026-11-10"}, {"end_date", "2026-11-11"},
                       {"state", "past"}, {"editable", true}, {"results_count", 0}},
                      {{"id", "daily-2026-11-20"}, {"kind", "daily"}, {"period", "2026-11-20"}, {"date", "2026-11-20"}, {"end_date", "2026-11-21"},
                       {"state", "coming"}, {"editable", true}, {"results_count", 0}, {"draws_on", "2026-11-13"}},
                      // Malformed: left out.
                      {{"id", "daily-2026-11-21"}, {"state", "soon"}},
                      Json("daily-2026-11-22"),
                  })}};
    auto list = AdminChallengePeriod::listFromJson(data);
    CHECK(list.size() == 6);
    if (list.size() != 6) return;
    const AdminChallengePeriod &open = list[0], &week = list[1], &month = list[2], &older = list[3], &past = list[4], &coming = list[5];
    CHECK(open.openWhileRunning());
    CHECK(open.parsed.has_value());
    CHECK(open.lockNote() == "En cours, personne n'a encore de résultat : vous pouvez encore le changer.");
    CHECK(open.pillTexts() == (std::vector<std::string>{"en cours · modifiable", "modifiable"}));
    CHECK(!week.editable && week.resultsCount == 3);
    CHECK(week.lockNote() == "3 joueurs ont déjà un résultat sur ce défi : il ne change plus.");
    CHECK(week.pillTexts() == (std::vector<std::string>{"en cours"}));
    CHECK(month.lockNote() == "1 joueur a déjà un résultat sur ce défi : il ne change plus.");
    CHECK(month.pillTexts().front() == "en cours · verrouillé");
    CHECK(older.resultsCount == -1);
    CHECK(older.lockNote() == "Défi commencé : lecture seule. Un défi commencé ne change plus.");
    CHECK(!past.editable);
    CHECK(past.lockNote() == "Défi passé : lecture seule.");
    CHECK(coming.editable && !coming.openWhileRunning());
    CHECK(coming.lockNote().empty());
    CHECK(coming.drawsOn == "2026-11-13");
    CHECK(coming.pillTexts() == (std::vector<std::string>{"+ choisir"}));
}

TEST_CASE("challenge versions: the medals of a calendar's cell fit their room: pills, numbers, Or alone, nothing") {
    using namespace challenge_ui;
    const float h = 20.0f;
    const std::array<float, 3> num{14.0f, 7.0f, 7.0f};  // "12", "5", "2"
    const MedalPlan pills = planMedals(1000.0f, h, num);
    CHECK(pills.fit == MedalFit::Pills);
    // One after another, never over each other, the whole within the room.
    CHECK(pills.x[0] == 0.0f);
    CHECK(pills.x[1] >= pills.x[0] + pills.w[0]);
    CHECK(pills.x[2] >= pills.x[1] + pills.w[1]);
    CHECK(std::abs(pills.width - (pills.x[2] + pills.w[2])) < 0.01f);
    // Each pill: its padding, its disk, its number.
    CHECK(pills.w[0] >= num[0] + MedalPlan::disk(h) + 2 * MedalPlan::pad(h));
    // A little less room: the numbers alone, narrower.
    const MedalPlan numbers = planMedals(pills.width - 1.0f, h, num);
    CHECK(numbers.fit == MedalFit::Numbers);
    CHECK(numbers.width < pills.width);
    CHECK(numbers.width <= pills.width - 1.0f);
    CHECK(numbers.w[0] >= num[0] && numbers.x[1] >= numbers.x[0] + numbers.w[0] && numbers.x[2] >= numbers.x[1] + numbers.w[1]);
    // Less again: the Or one alone.
    const MedalPlan gold = planMedals(numbers.width - 1.0f, h, num);
    CHECK(gold.fit == MedalFit::GoldOnly);
    CHECK(gold.w[0] == 0.0f && gold.w[1] == 0.0f && gold.w[2] > 0.0f);
    CHECK(gold.x[2] == 0.0f && gold.width == gold.w[2]);
    // Not even that: nothing (never cut).
    CHECK(planMedals(gold.width - 1.0f, h, num).fit == MedalFit::None);
    CHECK(planMedals(0.0f, h, num).fit == MedalFit::None);
    CHECK(planMedals(-5.0f, h, num).width == 0.0f);
}
