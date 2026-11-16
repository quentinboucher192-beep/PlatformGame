// Profile badges (2.41): the slots opened by the stars (the catalogue's steps, the same numbers
// as the server's), the badges that may be shown (unlocked, each once, within the slots, in
// the order chosen) and the choice kept in the Collection's save.

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// A book with these achievements unlocked.
AchievementBook bookWith(const std::vector<std::string>& ids) {
    AchievementBook b;
    Json unlocked = Json::object();
    for (const auto& id : ids) unlocked[id] = "2026-11-16 10:00";
    b.fromJson(Json{{"unlocked", unlocked}});
    return b;
}

/// Achievements of the catalogue worth `stars` stars or a little more (`first` before the
/// others, never `without`).
std::vector<std::string> worth(const AchievementCatalog& c, int stars, std::vector<std::string> first = {}, const std::string& without = {}) {
    int n = 0;
    for (const auto& id : first) n += c.find(id) ? c.find(id)->stars : 0;
    for (const auto& a : c.achievements()) {
        if (n >= stars) break;
        if (a.id == without || std::find(first.begin(), first.end(), a.id) != first.end()) continue;
        first.push_back(a.id);
        n += a.stars;
    }
    return first;
}

/// Just below `stars` stars (one-star achievements to finish).
std::vector<std::string> below(const AchievementCatalog& c, int stars, std::vector<std::string> first = {}) {
    int n = 0;
    for (const auto& id : first) n += c.find(id)->stars;
    for (const auto& a : c.achievements()) {
        if (std::find(first.begin(), first.end(), a.id) != first.end() || n + a.stars >= stars) continue;
        first.push_back(a.id);
        n += a.stars;
    }
    return first;
}

}  // namespace

TEST_CASE("profile badges: 2 slots from 100 stars, 3 from 250, every step reachable") {
    AchievementCatalog c;
    CHECK(c.loadDefault());
    CHECK(c.badgeSlotSteps().size() == 2);
    CHECK(c.badgeSlots(0) == 0);
    CHECK(c.badgeSlots(99) == 0);
    CHECK(c.badgeSlots(100) == 2);
    CHECK(c.badgeSlots(249) == 2);
    CHECK(c.badgeSlots(250) == 3);
    CHECK(c.badgeSlots(c.totalStars()) == 3);
    CHECK(c.maxBadgeSlots() == 3);
    CHECK(c.nextBadgeStep(0) && c.nextBadgeStep(0)->stars == 100 && c.nextBadgeStep(0)->slots == 2);
    CHECK(c.nextBadgeStep(100) && c.nextBadgeStep(100)->stars == 250 && c.nextBadgeStep(100)->slots == 3);
    CHECK(c.nextBadgeStep(250) == nullptr);
    for (const auto& st : c.badgeSlotSteps()) CHECK(st.stars <= c.totalStars());
}

TEST_CASE("profile badges: only unlocked achievements, each once, as many as the stars open") {
    AchievementCatalog c;
    CHECK(c.loadDefault());
    const std::vector<std::string> wanted = {"voyage_5", "futur_succes", "voyage_first", "voyage_5", "gardiens_first", "voyage_10"};
    // 99 stars: no slot, nothing shown (the choice is not lost: shown again with more stars).
    auto poor = below(c, 100, {"voyage_first", "voyage_5", "gardiens_first", "voyage_10"});
    AchievementBook b99 = bookWith(poor);
    CHECK(b99.stars(c) == 99);
    CHECK(shownBadges(wanted, c, b99).empty());
    // 100 stars and more: the first two valid, in the order chosen (an unknown id, a duplicate left out).
    AchievementBook b100 = bookWith(worth(c, 100, {"voyage_first", "voyage_5", "gardiens_first", "voyage_10"}));
    CHECK(b100.stars(c) >= 100);
    CHECK(b100.stars(c) < 250);
    CHECK((shownBadges(wanted, c, b100) == std::vector<std::string>{"voyage_5", "voyage_first"}));
    // 250 stars: a third one.
    AchievementBook b250 = bookWith(worth(c, 250, {"voyage_first", "voyage_5", "gardiens_first", "voyage_10"}));
    CHECK((shownBadges(wanted, c, b250) == std::vector<std::string>{"voyage_5", "voyage_first", "gardiens_first"}));
    // A badge whose achievement is not unlocked: the next one takes its place.
    AchievementBook noFirst = bookWith(worth(c, 260, {"voyage_5", "gardiens_first", "voyage_10"}, "voyage_first"));
    CHECK(!noFirst.unlocked("voyage_first"));
    CHECK((shownBadges(wanted, c, noFirst) == std::vector<std::string>{"voyage_5", "gardiens_first", "voyage_10"}));
}

TEST_CASE("profile badges: steps that are not increasing open nothing") {
    fs::path f = fs::temp_directory_path() / ("cosmic_badges_" + makeUuidV4().substr(0, 8) + ".json");
    Json j = {{"format", "cosmic.achievements"},
              {"version", 1},
              {"badgeSlots", Json::array({Json{{"stars", 100}, {"slots", 3}}, Json{{"stars", 50}, {"slots", 2}}})},
              {"categories", Json::array({Json{{"id", "voyage"}, {"name", "Voyage"}}})},
              {"achievements", Json::array({Json{{"id", "a"}, {"category", "voyage"}, {"name", "A"}, {"stars", 5}}})}};
    CHECK(FileUtil::writeTextAtomic(f, j.dump()));
    AchievementCatalog c;
    CHECK(c.load(f));
    CHECK(c.badgeSlotSteps().empty());
    CHECK(c.badgeSlots(1000) == 0);
    CHECK(c.maxBadgeSlots() == 0);
    CHECK(c.nextBadgeStep(0) == nullptr);
    std::error_code ec;
    fs::remove(f, ec);
}

TEST_CASE("profile badges: the choice is kept in the Collection's save, damage ignored") {
    fs::path f = fs::temp_directory_path() / ("cosmic_collection_" + makeUuidV4().substr(0, 8) + ".json");
    {
        CollectionBook b(f);
        b.badges = {"voyage_5", "gardiens_first"};
        CHECK(b.save());
    }
    {
        CollectionBook b(f);
        b.load();
        CHECK((b.badges == std::vector<std::string>{"voyage_5", "gardiens_first"}));
    }
    // A damaged list: only strings of a sensible length, each once.
    CollectionBook junk;
    junk.fromJson(Json{{"badges", Json::array({"voyage_5", 3, "voyage_5", std::string(80, 'x'), nullptr, "voyage_first"})}});
    CHECK((junk.badges == std::vector<std::string>{"voyage_5", "voyage_first"}));
    CollectionBook none;
    none.fromJson(Json{{"badges", "voyage_5"}});
    CHECK(none.badges.empty());
    std::error_code ec;
    fs::remove(f, ec);
}
