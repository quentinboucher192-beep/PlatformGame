// « NOUVEAU » pills (2.25): new after an update, gone once seen; a parent keeps its pill as
// long as anything under it is new (its own focus does not take it away), through steps of
// the trail as deep as needed; nothing new on a fresh install; the save survives a restart;
// the shipped catalogue leads every novelty to a menu.

#include <functional>
#include <map>
#include <set>

#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/online/Replay.h"
#include "game/powers/Power.h"
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
    // Focusing the parent does not take its pill away while a novelty under it is new.
    CHECK(!b.markSeen("menu.stats"));  // nothing of its own: nothing to save
    CHECK(b.isNew("menu.stats"));
    CHECK(b.isNew("stats.training"));
    CHECK(b.markSeen("stats.training"));
    CHECK(!b.markSeen("stats.training"));  // already seen
    CHECK(!b.isNew("stats.training"));
    CHECK(b.isNew("stats.locations"));
    CHECK(b.isNew("menu.stats"));  // one novelty still unseen under it
    b.markSeen("stats.locations");
    CHECK(!b.isNew("menu.stats"));  // nothing new below: no pill
    // A novelty with two parents: seen once, gone from both.
    CHECK(b.isNew("settings.tab.keyboard"));
    b.markSeen("settings.slots");
    CHECK(!b.isNew("settings.tab.keyboard"));
    CHECK(!b.isNew("menu.settings"));
    std::error_code ec;
    fs::remove(f, ec);
}

TEST_CASE("new badges: steps of a trail without a version, deep chains, cycles") {
    fs::path f = tempFile();
    NewBadges b;
    // menu → category → (12 steps) → achievement, plus a parent that is a novelty itself.
    std::vector<NewBadges::Feature> cat = {{"ach.deep", "2.29.0", {"step.11"}}, {"step.0", "", {"menu.achievements"}}};
    for (int i = 1; i < 12; ++i) cat.push_back({"step." + std::to_string(i), "", {"step." + std::to_string(i - 1)}});
    cat.push_back({"tab.own", "2.29.0", {"menu.collection"}});
    cat.push_back({"item.under", "2.29.0", {"tab.own"}});
    // A badly written catalogue: a loop of steps (and a novelty hanging from it).
    cat.push_back({"loop.a", "", {"loop.b"}});
    cat.push_back({"loop.b", "", {"loop.a"}});
    cat.push_back({"loop.item", "2.29.0", {"loop.a"}});
    cat.push_back({"loop.empty.a", "", {"loop.empty.b"}});
    cat.push_back({"loop.empty.b", "", {"loop.empty.a"}});
    b.setCatalog(cat);
    b.init(f, "2.28.0", "2.29.0");
    CHECK(b.isNew("menu.achievements"));
    CHECK(b.isNew("step.0"));
    CHECK(b.isNew("step.11"));
    CHECK(b.isNew("ach.deep"));
    for (int i = 0; i < 12; ++i) CHECK(!b.markSeen("step." + std::to_string(i)));
    CHECK(b.isNew("menu.achievements"));  // every step focused: the achievement is still new
    b.markSeen("ach.deep");
    CHECK(!b.isNew("ach.deep"));
    CHECK(!b.isNew("step.5"));
    CHECK(!b.isNew("menu.achievements"));
    // A parent that is a novelty too: it goes once seen AND nothing under it is new.
    CHECK(b.isNew("tab.own"));
    b.markSeen("tab.own");
    CHECK(b.isNew("tab.own"));  // seen, but its item is still new
    CHECK(b.isNew("menu.collection"));
    b.markSeen("item.under");
    CHECK(!b.isNew("tab.own"));
    CHECK(!b.isNew("menu.collection"));
    // The loops end (and still see what hangs from them).
    CHECK(b.isNew("loop.a"));
    CHECK(b.isNew("loop.b"));
    CHECK(!b.isNew("loop.empty.a"));
    b.markSeen("loop.item");
    CHECK(!b.isNew("loop.a"));
    CHECK(!b.isNew("loop.b"));
    // From a file: an entry without a version is a step, a wrong version is ignored, an entry
    // linking nothing is dropped; the save keeps what was seen.
    fs::path cf = tempFile();
    FileUtil::writeTextAtomic(cf, R"({"features": [
        {"id": "x.leaf", "version": "2.29.0", "parents": ["x.step"]},
        {"id": "x.step", "parents": ["menu.x"]},
        {"id": "x.bad", "version": "two", "parents": ["menu.x"]},
        {"id": "x.alone"},
        {"id": "Bad Id", "parents": ["menu.x"]}]})");
    fs::path cs = tempFile();
    NewBadges c;
    c.loadCatalog(cf);
    c.init(cs, "2.28.0", "2.29.0");
    CHECK(c.isNew("menu.x"));
    CHECK(c.isNew("x.step"));
    CHECK(!c.isNew("x.bad"));
    CHECK(!c.markSeen("x.alone"));
    c.markSeen("x.leaf");
    CHECK(!c.isNew("menu.x"));
    std::error_code ec;
    fs::remove(f, ec);
    fs::remove(cf, ec);
    fs::remove(cs, ec);
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

TEST_CASE("new badges: the shipped catalogue leads every novelty to a menu") {
    auto j = FileUtil::loadJson(Paths::dataDir() / "news" / "new_features.json");
    CHECK(j && j->contains("features"));
    std::map<std::string, std::vector<std::string>> parents;
    std::set<std::string> leaves;
    for (const auto& f : (*j)["features"]) {
        std::string id = f["id"].get<std::string>();
        CHECK(!parents.count(id));  // each id once
        for (const auto& p : f.value("parents", Json::array())) parents[id].push_back(p.get<std::string>());
        if (f.contains("version")) leaves.insert(id);
        else CHECK(!parents[id].empty());  // a step leads somewhere
    }
    // Every path from a novelty ends on a widget drawn by a screen (a main menu entry, the
    // guide's button, a button of the pause menu, of the level editor's toolbar), never in a loop.
    std::function<bool(const std::string&, int)> reaches = [&](const std::string& id, int depth) {
        if (depth > 16) return false;
        auto it = parents.find(id);
        if (it == parents.end() || it->second.empty())
            return id.rfind("menu.", 0) == 0 || id.rfind("pause.", 0) == 0 || id.rfind("editor.", 0) == 0 || id == "powers.guide";
        for (const auto& p : it->second)
            if (!reaches(p, depth + 1)) return false;
        return true;
    };
    for (const auto& id : leaves) CHECK(reaches(id, 0));
    // The 2.29 entries name real achievements and rewards, under their own category.
    AchievementCatalog ach;
    CHECK(ach.loadDefault());
    CosmeticCatalog cos;
    CHECK(cos.loadDefault());
    int achievements = 0, rewards = 0;
    for (const auto& id : leaves) {
        if (id.rfind("ach.", 0) == 0) {
            const AchievementDef* a = ach.find(id.substr(4));
            CHECK(a != nullptr);
            CHECK(parents[id] == std::vector<std::string>{"achievements.cat." + a->category});
            ++achievements;
        } else if (id.rfind("cos.", 0) == 0) {
            const CosmeticItem* c = cos.find(id.substr(4));
            CHECK(c != nullptr);
            CHECK(parents[id].front() == "collection.cat." + c->category);
            ++rewards;
        }
    }
    CHECK(achievements == 72 + 3);  // 2.29, and Singularis's three of 2.34
    CHECK(rewards == 22 + 1);       // 2.29, and the title « Briseur d'étoiles » of 2.34
    // The whole « Collection » category of achievements came with 2.29.
    for (const auto& a : ach.achievements())
        if (a.category == "collection") CHECK(leaves.count("ach." + a.id));
    // The guide's cards with a pill are those of the powers that do not use their slot key.
    ContentDatabase content;
    content.reload();
    int guide = 0;
    for (const auto& id : leaves)
        if (id.rfind("powers.guide.", 0) == 0) {
            const PowerDefinition* def = content.power(id.substr(13));
            CHECK(def != nullptr);
            CHECK(powerTriggerInfo(*def).kind != PowerTrigger::SlotKey);
            ++guide;
        }
    for (const auto& e : fs::directory_iterator(Paths::dataDir() / "powers"))  // the game's own powers
        if (const PowerDefinition* def = content.power(e.path().stem().string()))
            if (powerTriggerInfo(*def).kind != PowerTrigger::SlotKey) CHECK(leaves.count("powers.guide." + def->id));
    CHECK(guide == 7);
}
