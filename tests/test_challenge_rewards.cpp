// Rewards of the challenges (2.34, docs/online/CHALLENGES_PERIODS.md §6): cosmetics of the
// Collection unlocked by challenge results that Cosmic Online verified — the conditions of the
// catalogue, the unlock computation, what a challenge can advance, and the server's counts as
// the game reads them (`verified` of /challenges/me, or a 2.33 server's results).

#include <algorithm>
#include <chrono>

#include "engine/core/FileUtil.h"
#include "game/cosmetics/Collection.h"
#include "game/online/DailyChallenges.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

std::vector<std::string> ids(const std::vector<const CosmeticItem*>& items) {
    std::vector<std::string> out;
    for (const CosmeticItem* i : items) out.push_back(i->id);
    return out;
}

}  // namespace

TEST_CASE("challenge rewards: the five rewards of the catalogue and their conditions") {
    CosmeticCatalog c;
    std::string err;
    CHECK(c.loadDefault(&err));
    struct Want {
        const char* id;
        const char* category;
        bool streak;
        const char* kind;
        int tier, count;
    };
    const Want want[] = {{"title_defiant", "title", false, "any", 3, 1},
                         {"frame_couronne", "frame", false, "weekly", 3, 1},
                         {"trail_comete_mois", "trail", false, "monthly", 3, 1},
                         {"suit_champion", "suit", false, "any", 3, 10},
                         {"victory_serie_feu", "victory", true, "daily", 1, 7}};
    for (const Want& w : want) {
        const CosmeticItem* it = c.find(w.id);
        CHECK(it != nullptr);
        if (!it) continue;
        CHECK(it->category == w.category);
        CHECK(it->challengeReward());
        CHECK(it->challenge.streak == w.streak);
        CHECK(it->challenge.kind == w.kind);
        CHECK(it->challenge.tier == w.tier);
        CHECK(it->challenge.count == w.count);
        CHECK(cosmeticThreshold(*it, 600) == -1);
    }
    // Never on the route of the stars.
    for (const auto& m : c.milestones(600))
        for (const CosmeticItem* i : m.items) CHECK(!i->challengeReward());
}

TEST_CASE("challenge rewards: unlocked only by the results the server verified") {
    CosmeticCatalog c;
    CHECK(c.loadDefault());
    const CosmeticItem& defiant = *c.find("title_defiant");
    const CosmeticItem& crown = *c.find("frame_couronne");
    const CosmeticItem& comet = *c.find("trail_comete_mois");
    const CosmeticItem& champion = *c.find("suit_champion");
    const CosmeticItem& fire = *c.find("victory_serie_feu");
    // Every star of the game does not unlock any of them.
    CosmeticProgress p;
    p.stars = p.totalStars = 10000;
    CHECK(!cosmeticUnlocked(defiant, p));
    ChallengeRewardProgress v;  // not read from the server yet
    p.challenges = &v;
    v.atLeast[0][2] = 5;
    CHECK(!cosmeticUnlocked(defiant, p));
    CHECK(cosmeticChallengeProgress(defiant, p) == std::make_pair(0, 1));
    v = ChallengeRewardProgress{};
    v.known = true;
    CHECK(!cosmeticUnlocked(defiant, p));
    // A gold on a day: « Défiant », nothing of the week or the month.
    v.atLeast[0][0] = v.atLeast[0][1] = v.atLeast[0][2] = 1;
    CHECK(cosmeticUnlocked(defiant, p));
    CHECK(!cosmeticUnlocked(crown, p));
    CHECK(!cosmeticUnlocked(comet, p));
    CHECK(cosmeticChallengeProgress(champion, p) == std::make_pair(1, 10));
    // Silver on a month is not gold.
    v.atLeast[2][0] = v.atLeast[2][1] = 3;
    CHECK(!cosmeticUnlocked(comet, p));
    v.atLeast[2][2] = 1;
    CHECK(cosmeticUnlocked(comet, p));
    v.atLeast[1][0] = v.atLeast[1][1] = v.atLeast[1][2] = 1;
    CHECK(cosmeticUnlocked(crown, p));
    // Ten golds, of any kind (1 + 1 + 8).
    CHECK(cosmeticChallengeProgress(champion, p) == std::make_pair(3, 10));
    v.atLeast[0][2] = 8;
    CHECK(cosmeticUnlocked(champion, p));
    v.atLeast[0][2] = 20;
    CHECK(cosmeticChallengeProgress(champion, p) == std::make_pair(10, 10));  // never above what it asks
    // Seven days in a row; a series of weeks does not count for it.
    v.bestStreak[1] = 9;
    v.bestStreak[0] = 6;
    CHECK(!cosmeticUnlocked(fire, p));
    CHECK(cosmeticChallengeProgress(fire, p) == std::make_pair(6, 7));
    v.bestStreak[0] = 7;
    CHECK(cosmeticUnlocked(fire, p));
    // Unlocked: announced once (fresh), worn like the others.
    CollectionBook book(fs::temp_directory_path() / ("cosmic_chal_rewards_" + makeUuidV4().substr(0, 8) + ".json"));
    auto fresh = ids(book.fresh(c, p));
    for (const char* id : {"title_defiant", "frame_couronne", "trail_comete_mois", "suit_champion", "victory_serie_feu"})
        CHECK(std::find(fresh.begin(), fresh.end(), id) != fresh.end());
    book.equip("title", "title_defiant");
    CHECK(book.equippedIds(c, p)["title"] == "title_defiant");
    v.known = false;  // another account's computer: taken off (the default instead)
    CHECK(book.equippedIds(c, p)["title"] == "title_none");
}

TEST_CASE("challenge rewards: what a challenge can advance, its own kind first") {
    CosmeticCatalog c;
    CHECK(c.loadDefault());
    using List = std::vector<std::string>;
    const List weekly = {"frame_couronne", "title_defiant", "suit_champion"};
    const List monthly = {"trail_comete_mois", "title_defiant", "suit_champion"};
    const List daily = {"victory_serie_feu", "title_defiant", "suit_champion"};
    const List late = {"title_defiant", "suit_champion"};  // « hors délai »: never a series
    CHECK(ids(challengeRewardsFor(c, "weekly", false)) == weekly);
    CHECK(ids(challengeRewardsFor(c, "monthly", false)) == monthly);
    CHECK(ids(challengeRewardsFor(c, "daily", false)) == daily);
    CHECK(ids(challengeRewardsFor(c, "daily", true)) == late);
}

TEST_CASE("challenge rewards: a malformed condition never unlocks anything") {
    fs::path f = fs::temp_directory_path() / ("cosmic_chal_catalog_" + makeUuidV4().substr(0, 8) + ".json");
    Json j = {{"categories", {{{"id", "title"}, {"name", "Titres"}}}},
              {"items",
               {{{"id", "title_none"}, {"category", "title"}, {"name", "Sans titre"}, {"default", true}, {"look", ""}},
                {{"id", "t_year"}, {"category", "title"}, {"name", "A"}, {"look", "A"}, {"challenge", {{"kind", "yearly"}, {"tier", "gold"}, {"count", 1}}}},
                {{"id", "t_plat"}, {"category", "title"}, {"name", "B"}, {"look", "B"}, {"challenge", {{"kind", "any"}, {"tier", "platinum"}, {"count", 1}}}},
                {{"id", "t_zero"}, {"category", "title"}, {"name", "C"}, {"look", "C"}, {"challenge", {{"kind", "daily"}, {"tier", "gold"}, {"count", 0}}}},
                {{"id", "t_any"}, {"category", "title"}, {"name", "D"}, {"look", "D"}, {"challengeStreak", {{"kind", "any"}, {"days", 3}}}},
                {{"id", "t_ok"}, {"category", "title"}, {"name", "E"}, {"look", "E"}, {"challenge", {{"kind", "daily"}, {"tier", "silver"}, {"count", 2}}}}}}};
    CHECK(FileUtil::writeTextAtomic(f, j.dump()));
    CosmeticCatalog c;
    CHECK(c.load(f));
    CHECK(!c.find("t_year"));
    CHECK(!c.find("t_plat"));
    CHECK(!c.find("t_zero"));
    CHECK(!c.find("t_any"));
    const CosmeticItem* ok = c.find("t_ok");
    CHECK(ok && ok->challengeReward() && ok->challenge.tier == 2 && ok->challenge.count == 2);
    // Silver or better: two challenges of the day with silver, or gold.
    ChallengeRewardProgress v;
    v.known = true;
    v.atLeast[0][1] = 1;
    CosmeticProgress p;
    p.challenges = &v;
    CHECK(!cosmeticUnlocked(*ok, p));
    v.atLeast[0][1] = 2;
    CHECK(cosmeticUnlocked(*ok, p));
    std::error_code ec;
    fs::remove(f, ec);
}

TEST_CASE("challenge rewards: the server's counts read strictly; a 2.33 server's verified results") {
    CHECK(!ChallengeService::rewardProgressFromJson(Json()).has_value());
    CHECK(!ChallengeService::rewardProgressFromJson(Json::array()).has_value());
    Json partial = {{"daily", {{"gold", 1}}}, {"weekly", {{"gold", 0}}}};  // no month
    CHECK(!ChallengeService::rewardProgressFromJson(partial).has_value());
    Json v = {{"daily", {{"gold", 3}, {"silver", 1}, {"bronze", -4}, {"best_streak", 5}}},
              {"weekly", {{"gold", 0}, {"silver", 2}, {"bronze", 2}, {"best_streak", 1}}},
              {"monthly", {{"gold", "x"}, {"silver", 0}, {"bronze", 1}, {"best_streak", 0}}}};
    auto p = ChallengeService::rewardProgressFromJson(v);
    CHECK(p.has_value());
    CHECK(p->known);
    // « That tier or better »: never fewer silvers than golds, nor bronzes than silvers.
    CHECK(p->results("daily", 3) == 3);
    CHECK(p->results("daily", 2) == 3);
    CHECK(p->results("daily", 1) == 3);
    CHECK(p->results("weekly", 2) == 2);
    CHECK(p->results("monthly", 3) == 0);
    CHECK(p->results("any", 1) == 6);
    CHECK(p->streak("daily") == 5);
    CHECK(p->streak("any") == 0);
    // A 2.33 server: only the results whose status is VERIFIED; the series: days in time.
    Json results = Json::array({
        {{"challenge_id", "daily-2026-11-10"}, {"kind", "daily"}, {"period", "2026-11-10"}, {"tier", "gold"}, {"status", "VERIFIED"}, {"late", false}},
        {{"challenge_id", "daily-2026-11-11"}, {"kind", "daily"}, {"period", "2026-11-11"}, {"tier", "silver"}, {"status", "VERIFIED"}, {"late", false}},
        {{"challenge_id", "daily-2026-11-12"}, {"kind", "daily"}, {"period", "2026-11-12"}, {"tier", "gold"}, {"status", "VERIFIED"}, {"late", true}},
        {{"challenge_id", "daily-2026-11-13"}, {"kind", "daily"}, {"period", "2026-11-13"}, {"tier", "gold"}, {"status", "PENDING"}, {"late", false}},
        {{"challenge_id", "weekly-2026-W46"}, {"kind", "weekly"}, {"period", "2026-W46"}, {"tier", "gold"}, {"status", "REJECTED"}, {"late", false}},
        {{"challenge_id", "monthly-2026-10"}, {"kind", "monthly"}, {"period", "2026-10"}, {"tier", "bronze"}, {"status", "VERIFIED"}, {"late", false}},
        // A 2.32 result: a day's, without kind nor period.
        {{"challenge_id", "daily-2026-11-09"}, {"date", "2026-11-09"}, {"tier", "bronze"}, {"status", "VERIFIED"}},
    });
    ChallengeRewardProgress r = ChallengeService::rewardProgressFromResults(results);
    CHECK(r.known);
    CHECK(r.results("daily", 3) == 2);
    CHECK(r.results("daily", 2) == 3);
    CHECK(r.results("daily", 1) == 4);
    CHECK(r.results("weekly", 1) == 0);
    CHECK(r.results("monthly", 1) == 1);
    CHECK(r.streak("daily") == 3);  // the 9th, 10th and 11th (the 12th was late)
    CHECK(r.streak("monthly") == 1);
}

TEST_CASE("rewards of one challenge (2.43): read strictly, titles of the server as items") {
    // A challenge's rewards (its JSON `rewards`): the malformed ones are left out.
    Json rewards = Json::array({
        {{"kind", "title"}, {"ref", "srv:12"}, {"condition", "tier"}, {"value", 3}, {"title", {{"id", "srv:12"}, {"text", "Pionnier du givre"}, {"color", "cyan"}}}},
        {{"kind", "item"}, {"ref", "frame_couronne"}, {"condition", "top"}, {"value", 250}, {"item", {{"id", "frame_couronne"}, {"category", "frame"}, {"name", "Couronne"}}}},
        {{"kind", "item"}, {"ref", "trail_comete_mois"}, {"condition", "finished"}, {"value", 9}},
        {{"kind", "title"}, {"ref", "srv:13"}, {"condition", "tier"}, {"value", 1}},  // no text: left out
        {{"kind", "item"}, {"ref", "Bad Id"}, {"condition", "finished"}},
        {{"kind", "item"}, {"ref", "suit_champion"}, {"condition", "podium"}, {"value", 1}},
        {{"kind", "emote"}, {"ref", "x"}, {"condition", "finished"}},
    });
    std::vector<ChallengePrize> p = ChallengePrize::listFromJson(rewards);
    CHECK(p.size() == 3);
    if (p.size() == 3) {
        CHECK(p[0].title() && p[0].name == "Pionnier du givre" && p[0].color == "cyan" && p[0].value == 3);
        CHECK(p[1].condition == "top" && p[1].value == 100);  // clamped
        CHECK(p[2].condition == "finished" && p[2].value == 0);
        CHECK(p[2].body() == Json({{"kind", "item"}, {"ref", "trail_comete_mois"}, {"condition", "finished"}}));
        CHECK(p[1].body()["value"] == 100);
    }

    // What I won: catalogue items unlocked whatever their condition, server titles as items.
    Json won = Json::array({
        {{"challenge", {{"id", "daily-2026-11-20"}, {"kind", "daily"}, {"period", "2026-11-20"}, {"date", "2026-11-20"}}},
         {"kind", "title"},
         {"ref", "srv:12"},
         {"granted_at", "2026-11-21T03:17:00.000Z"},
         {"title", {{"id", "srv:12"}, {"text", "Pionnier du givre"}, {"color", "violet"}}}},
        {{"challenge", {{"id", "weekly-2026-W47"}, {"kind", "weekly"}, {"period", "2026-W47"}, {"date", "2026-11-16"}}},
         {"kind", "item"},
         {"ref", "suit_noyau"},
         {"granted_at", "2026-11-23T03:17:00.000Z"}},
        {{"challenge", {{"id", "daily-2026-11-18"}, {"kind", "daily"}, {"period", "2026-11-18"}, {"date", "2026-11-18"}}},
         {"kind", "title"},
         {"ref", "srv:12"},
         {"granted_at", "2026-11-19T03:17:00.000Z"},
         {"title", {{"id", "srv:12"}, {"text", "Pionnier du givre"}, {"color", "violet"}}}},
        {{"challenge", {{"id", "daily-2026-11-18"}}}, {"kind", "item"}, {"ref", "srv:99"}},  // an item is never a server title
    });
    auto w = WonRewards::fromJson(won);
    CHECK(w.has_value());
    if (!w) return;
    CHECK(w->known);
    CHECK(w->list.size() == 3);
    CHECK(w->titles.size() == 1);
    CHECK(w->has("suit_noyau") && w->has("srv:12") && !w->has("srv:99"));
    CHECK(w->wonAt("daily-2026-11-20", "srv:12") && !w->wonAt("daily-2026-11-20", "suit_noyau"));
    const CosmeticItem* t = w->title("srv:12");
    CHECK(t && t->category == "title" && t->look == "Pionnier du givre" && t->color == "violet" && t->serverTitle());
    const WonReward* origin = w->origin("srv:12");
    CHECK(origin && origin->challengeId == "daily-2026-11-18");  // the first time it was won
    auto again = WonRewards::fromJson(w->toJson());
    CHECK(again && *again == *w);
    CHECK(!WonRewards::fromJson(Json::object()).has_value());

    CosmeticCatalog cat;
    std::string err;
    CHECK(cat.loadDefault(&err));
    const CosmeticItem* noyau = cat.find("suit_noyau");
    CHECK(noyau != nullptr);
    if (!noyau || !t) return;
    CosmeticProgress none;
    CHECK(!cosmeticUnlocked(*noyau, none));
    CHECK(!cosmeticUnlocked(*t, none));
    CosmeticProgress mine;
    mine.won = &*w;
    CHECK(cosmeticUnlocked(*noyau, mine));
    CHECK(cosmeticUnlocked(*t, mine));
    // Equipped: a server title won is worn; one not won falls back to the default.
    CollectionBook book;
    book.equip("title", "srv:12");
    CHECK(book.equipped(cat, "title", mine) == t);
    CHECK(book.equipped(cat, "title", none)->isDefault);
    CHECK(book.equippedIds(cat, mine)["title"] == "srv:12");
    book.equip("title", "srv:77");
    CHECK(book.equipped(cat, "title", mine)->isDefault);
}
