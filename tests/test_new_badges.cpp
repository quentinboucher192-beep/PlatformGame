// « NOUVEAU » pills (2.25): new after an update, gone once seen, parents follow their
// novelties, nothing new on a fresh install, the save survives a restart.

#include "engine/core/FileUtil.h"
#include "game/online/Replay.h"
#include "game/progression/NewBadges.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path tempFile() { return fs::temp_directory_path() / ("cosmic_badges_" + makeUuidV4().substr(0, 8) + ".json"); }

std::vector<NewBadges::Feature> catalog() {
    return {{"stats.training", "2.25.0", {"menu.stats"}},
            {"stats.locations", "2.25.0", {"menu.stats"}},
            {"settings.slots", "2.26.0", {"menu.settings", "settings.tab.keyboard"}},
            {"old.thing", "2.10.0", {"menu.stats"}}};
}

}  // namespace

TEST_CASE("new badges: after an update, the novelties and their parents are new") {
    fs::path f = tempFile();
    NewBadges b;
    b.setCatalog(catalog());
    b.init(f, "2.24.0", "2.26.0");
    CHECK(b.baseline() == "2.24.0");
    CHECK(b.isNew("stats.training"));
    CHECK(b.isNew("settings.slots"));
    CHECK(!b.isNew("old.thing"));  // older than the version the player had
    CHECK(b.isNew("menu.stats"));
    CHECK(b.isNew("menu.settings"));
    CHECK(!b.isNew("unknown"));
    // Focusing the parent takes its pill away; the novelties under it stay new.
    b.markSeen("menu.stats");
    CHECK(!b.isNew("menu.stats"));
    CHECK(b.isNew("stats.training"));
    b.markSeen("stats.training");
    CHECK(!b.isNew("stats.training"));
    CHECK(b.isNew("stats.locations"));
    std::error_code ec;
    fs::remove(f, ec);
}

TEST_CASE("new badges: a fresh install shows nothing, the save survives a restart") {
    fs::path f = tempFile();
    {
        NewBadges fresh;
        fresh.setCatalog(catalog());
        fresh.init(f, "", "2.26.0");
        CHECK(!fresh.isNew("stats.training"));
        CHECK(!fresh.isNew("menu.settings"));
    }
    std::error_code ec;
    fs::remove(f, ec);
    {
        NewBadges b;
        b.setCatalog(catalog());
        b.init(f, "2.24.0", "2.25.0");
        b.markSeen("stats.training");
        CHECK(b.save());
    }
    {
        // Next update (2.26): the baseline stays, what was not seen is still new.
        NewBadges b;
        b.setCatalog(catalog());
        b.init(f, "2.25.0", "2.26.0");
        CHECK(b.baseline() == "2.24.0");
        CHECK(!b.isNew("stats.training"));
        CHECK(b.isNew("stats.locations"));
        CHECK(b.isNew("settings.slots"));
    }
    // A damaged file: no crash, a new baseline from the previous version.
    FileUtil::writeTextAtomic(f, "{\"baseline\": 42, \"seen\": [1, \"x\"]}");
    NewBadges junk;
    junk.setCatalog(catalog());
    junk.init(f, "2.25.0", "2.26.0");
    CHECK(junk.baseline() == "2.25.0");
    CHECK(junk.isNew("settings.slots"));
    CHECK(!junk.isNew("stats.training"));
    fs::remove(f, ec);
}
