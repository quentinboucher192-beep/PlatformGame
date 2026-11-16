// Notifications of the main menu: new content, campaign levels modified (records dropped),
// versions and installers.

#include "game/content/ContentDatabase.h"
#include "game/progression/NewsCenter.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
NewsCenter::Snapshot snap(std::map<std::string, std::string> levels, std::vector<std::string> powers = {"dash"},
                          std::vector<std::string> enemies = {"glowmite"}, std::vector<std::string> bosses = {"sentinel"}) {
    NewsCenter::Snapshot s;
    s.levels = std::move(levels);
    for (const auto& [id, h] : s.levels) s.levelNames[id] = id;
    for (const auto& p : powers) s.powers.push_back({p, p});
    for (const auto& e : enemies) s.enemies.push_back({e, e});
    for (const auto& b : bosses) s.bosses.push_back({b, b});
    return s;
}
bool has(const NewsCenter& n, const std::string& kind) {
    for (const auto& it : n.items())
        if (it.kind == kind) return true;
    return false;
}
}  // namespace

TEST_CASE("news: versions compare numerically") {
    CHECK(versionNewer("2.10.0", "2.4.1"));
    CHECK(versionNewer("2.4.1", "2.4.0"));
    CHECK(!versionNewer("2.4.0", "2.4.0"));
    CHECK(!versionNewer("1.9.9", "2.0.0"));
}

TEST_CASE("news: first launch is quiet, then new content and modified levels are announced") {
    RecordsManager records;
    NewsCenter n;
    n.apply(snap({{"a_01", "h1"}}), records, "2.4.0", Json::object());
    CHECK(has(n, "version"));
    CHECK(!has(n, "level"));
    CHECK(!has(n, "power"));
    n.markAllRead();
    CHECK(n.unread() == 0);
    // Next version: one new level, a level changed, a new power, monster and boss.
    n.apply(snap({{"a_01", "h2"}, {"a_02", "x"}}, {"dash", "time_slow"}, {"glowmite", "golem"}, {"sentinel", "astrarch"}), records, "2.5.0",
            Json{{"2.5.0", {"ligne 1", "ligne 2"}}});
    CHECK(has(n, "level"));
    CHECK(has(n, "level_changed"));
    CHECK(has(n, "power"));
    CHECK(has(n, "enemy"));
    CHECK(has(n, "boss"));
    bool changelog = false;
    for (const auto& it : n.items()) changelog |= it.kind == "version" && it.text.find("ligne 2") != std::string::npos;
    CHECK(changelog);
    int before = n.unread();
    // The same state again adds nothing.
    n.apply(snap({{"a_01", "h2"}, {"a_02", "x"}}, {"dash", "time_slow"}, {"glowmite", "golem"}, {"sentinel", "astrarch"}), records, "2.5.0",
            Json::object());
    CHECK(n.unread() == before);
}

TEST_CASE("news: a best time set on another version of the level is dropped and announced") {
    RecordsManager records;
    RunResult run;
    run.levelId = "a_01";
    run.completed = true;
    run.time = 95.5f;
    run.medal = Medal::Gold;
    run.levelHash = "h1";
    records.submit(run);
    CHECK(records.get("a_01")->bestTime > 0.0f);
    NewsCenter n;
    n.apply(snap({{"a_01", "h1"}}), records, "2.4.0", Json::object());
    CHECK(!has(n, "record"));  // same version of the level: kept
    CHECK(records.get("a_01")->bestTime > 0.0f);
    n.apply(snap({{"a_01", "h2"}}), records, "2.4.0", Json::object());
    CHECK(has(n, "record"));
    CHECK(records.get("a_01")->bestTime == 0.0f);
    CHECK(records.get("a_01")->completions == 1);  // the history of the player stays
    // A record without fingerprint (set before 2.4.0) is from an older version.
    RunResult old;
    old.levelId = "b_01";
    old.completed = true;
    old.time = 50.0f;
    records.submit(old);
    n.apply(snap({{"a_01", "h2"}, {"b_01", "z"}}), records, "2.4.0", Json::object());
    CHECK(records.get("b_01")->bestTime == 0.0f);
}

// 2.49.1: a best time set on a revision downloaded from Cosmic Online was dropped at every start
// (compared with the bundled file): « NOUVEAU RECORD » at the next finish, though the rankings
// were kept. It is now only checked against the revisions.
TEST_CASE("news: a best time set on a downloaded revision is never dropped at start; a kept revision keeps it") {
    RecordsManager records;
    NewsCenter n;
    n.apply(snap({{"a_01", "bundled"}}), records, "2.49.1", Json::object());
    RunResult run;
    run.levelId = "a_01";
    run.completed = true;
    run.time = 80.0f;
    run.levelHash = "revision_gameplay";  // the revision downloaded, not the bundled file
    run.rankedHash = "ranked_r2";
    records.submit(run);
    for (int start = 0; start < 3; ++start) n.apply(snap({{"a_01", "bundled"}}), records, "2.49.1", Json::object());
    CHECK(records.get("a_01")->bestTime == 80.0f);
    CHECK(!has(n, "record"));
    // A slower run is no new best.
    RunResult slower = run;
    slower.time = 85.0f;
    CHECK(!records.submit(slower).newBestTime);
    // Its revision, kept: published with « Garder les records » (no major change).
    CHECK(records.carryBest("a_01", "ranked_r3"));
    CHECK(records.get("a_01")->rankedHash == "ranked_r3");
    CHECK(records.get("a_01")->bestTime == 80.0f);
    n.noteRevision("a_01", "Niveau A", "ranked_r3", true, std::nullopt);
    CHECK(has(n, "level_changed"));
    CHECK(!has(n, "record"));
    CHECK(!records.submit(slower).newBestTime);
    // The best time and its revision survive a restart of the game (saved with the records).
    LevelRecord back = LevelRecord::fromJson(records.get("a_01")->toJson());
    CHECK(back.rankedHash == "ranked_r3");
    CHECK(RunResult::fromJson(run.toJson()).rankedHash == "ranked_r2");
    // A revision with its rankings reset (a major change): the best time no longer counts.
    auto old = records.invalidateBest("a_01");
    CHECK(old && old->bestTime == 80.0f);
    n.noteRevision("a_01", "Niveau A", "ranked_r4", false, old);
    CHECK(has(n, "record"));
    CHECK(records.submit(slower).newBestTime);  // a real new best now
    // A best time on the bundled level of before an update is still dropped by the update.
    RecordsManager r2;
    NewsCenter n2;
    n2.apply(snap({{"b_01", "v1"}}), r2, "2.49.1", Json::object());
    RunResult b;
    b.levelId = "b_01";
    b.completed = true;
    b.time = 40.0f;
    b.levelHash = "v1";
    r2.submit(b);
    n2.apply(snap({{"b_01", "v2"}}), r2, "2.50.0", Json::object());
    CHECK(r2.get("b_01")->bestTime == 0.0f);
    CHECK(has(n2, "record"));
}

TEST_CASE("news: a newer installer is offered once") {
    NewsCenter n;
    n.offerRelease("9.9.9", 31000000, std::string(64, 'a'), "/api/v1/client/download/9.9.9", "");
    n.offerRelease("9.9.9", 31000000, std::string(64, 'a'), "/api/v1/client/download/9.9.9", "");
    int updates = 0;
    for (const auto& it : n.items()) updates += it.kind == "update" ? 1 : 0;
    CHECK(updates == 1);
    CHECK(n.items().front().download == "/api/v1/client/download/9.9.9");
}
