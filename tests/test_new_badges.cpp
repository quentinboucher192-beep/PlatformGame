// « NOUVEAU » pills (2.25): new after an update, gone once seen; a parent keeps its pill as
// long as anything under it is new (its own focus does not take it away), through steps of
// the trail as deep as needed; nothing new on a fresh install; the save survives a restart;
// the shipped catalogue leads every novelty to a menu. 2.69: the assets of ADMINISTRATION ›
// Assets by the same rule (data/assets/asset_versions.json, tools/assets/asset_versions.py).

#include <cstdlib>
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
#include "game/progression/NewsCenter.h"
#include "tests/TestFramework.h"

using namespace cosmic;

#ifdef _WIN32
#define COSMIC_NULL_DEVICE "NUL"
#else
#define COSMIC_NULL_DEVICE "/dev/null"
#endif

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
    CHECK(achievements == 72 + 3 + 12 + 2 + 6 + 6 + 6 + 6 + 1 + 3);  // 2.29, Singularis's three of 2.34, Les Confins' twelve (2.48-2.49), the
                                                                 // Boomerang chargé's two (2.51), the Kraken's six (2.53), the Phénix's six (2.56),
                                                                 // the Liche's six (2.57), the Titan's six (2.58), Pyroclast's badge (2.68),
                                                                 // the badges of Voidworm, Graviton and Titanus (2.69)
    CHECK(rewards == 22 + 1 + 5 + 10 + 5 + 5 + 5 + 5 + 3 + 3);  // 2.29, « Briseur d'étoiles » of 2.34, the five of the challenges of 2.35, Les
                                                                // Confins' ten, the Kraken's five (2.53), the Phénix's five (2.56), the Liche's five
                                                                // (2.57), the Titan's five (2.58), the raid's three titles (2.68), the world
                                                                // event's two suits and its clan title (2.69)
    // The whole « Collection » category of achievements came with 2.29.
    for (const auto& a : ach.achievements())
        if (a.category == "collection") CHECK(leaves.count("ach." + a.id));
    // The guide's cards with a pill: those of the powers that do not use their slot key (2.25),
    // and the powers added since (2.50: Boomerang chargé).
    ContentDatabase content;
    content.reload();
    const std::set<std::string> added = {"boomerang_charge"};
    int guide = 0;
    for (const auto& id : leaves)
        if (id.rfind("powers.guide.", 0) == 0) {
            const PowerDefinition* def = content.power(id.substr(13));
            CHECK(def != nullptr);
            if (def && !added.count(def->id)) CHECK(powerTriggerInfo(*def).kind != PowerTrigger::SlotKey);
            ++guide;
        }
    for (const auto& e : fs::directory_iterator(Paths::dataDir() / "powers"))  // the game's own powers
        if (const PowerDefinition* def = content.power(e.path().stem().string()))
            if (powerTriggerInfo(*def).kind != PowerTrigger::SlotKey || added.count(def->id))
                CHECK(leaves.count("powers.guide." + def->id));
    CHECK(guide == 7 + 1);
}

// 2.49: Les Confins came in 2.48 without a single pill. From Les Confins on, every planet,
// campaign level, boss, creature, achievement, reward and jukebox track of the game has its
// « NOUVEAU » pill in the catalogue; only what existed before (the list below, frozen at 2.47)
// may have none.
TEST_CASE("new badges: every planet, level, boss, creature, achievement, reward and track added since 2.48 has its pill") {
    auto j = FileUtil::loadJson(Paths::dataDir() / "news" / "new_features.json");
    CHECK(j && j->contains("features"));
    std::map<std::string, std::vector<std::string>> parents;
    std::set<std::string> leaves;
    for (const auto& f : (*j)["features"]) {
        std::string id = f["id"].get<std::string>();
        for (const auto& p : f.value("parents", Json::array())) parents[id].push_back(p.get<std::string>());
        if (f.contains("version")) leaves.insert(id);
    }
    auto legacy = FileUtil::loadJson(Paths::fromUtf8("tests/data/content_before_pills.json"));
    CHECK(legacy && legacy->is_array());
    std::set<std::string> before;
    for (const auto& x : *legacy) before.insert(x.get<std::string>());
    std::vector<std::string> missing;
    auto need = [&](const std::string& feature) {
        if (!before.count(feature) && !leaves.count(feature)) missing.push_back(feature);
    };
    ContentDatabase content;
    content.reload();
    for (const PlanetData* p : content.planets()) {
        need("planet." + p->id);
        need("gallery.planet." + p->id);
        for (const LevelEntry* e : content.levelsForPlanet(p->id))
            if (e->source == ContentSource::Builtin) need("level." + e->id);
    }
    for (const EnemyDefinition* d : content.enemies()) {
        // 2.64: the tutorial's creatures are never in the gallery (nor « NOUVEAU »).
        if (!d->builtin || !content.isNativeEnemy(d->id) || !d->variantOf.empty() || d->tutorial || !d->raid.empty()) continue;  // 2.68: a raid's: not in the gallery
        if (d->isBoss) {
            need("boss." + d->id);
            need("gallery.boss." + d->id);
        } else {
            need("gallery.creature." + d->id);
        }
    }
    AchievementCatalog ach;
    CHECK(ach.loadDefault());
    for (const auto& a : ach.achievements()) need("ach." + a.id);
    CosmeticCatalog cos;
    CHECK(cos.loadDefault());
    for (const auto& c : cos.items())
        if (c.souvenir.empty()) need("cos." + c.id);  // 2.64: a souvenir (the tutorial's title) is for new players: no pill
    if (auto t = FileUtil::loadJson(Paths::dataDir() / "music" / "tracks.json"); t && t->contains("tracks"))
        for (const auto& x : (*t)["tracks"]) need("track." + fs::path(x.value("file", std::string())).stem().string());
    for (const auto& m : missing) std::printf("  sans pastille NOUVEAU : %s\n", m.c_str());
    CHECK(missing.empty());
    // Les Confins themselves (2.48, 2.49, 2.53: the Kraken, 2.56: the Phénix, 2.57: the Liche, and 2.58: the Titan).
    for (const char* id : {"planet.confins", "level.confins_01", "level.confins_02", "level.confins_03", "boss.pyrrhax", "boss.monolithe",
                           "boss.kraken", "gallery.boss.kraken", "track.pyrrhax_boss", "track.monolithe_boss", "track.kraken_boss",
                           "gallery.creature.dragonnet", "gallery.creature.eclat_de_roche", "gallery.creature.photophore", "level.confins_04",
                           "boss.phenix", "gallery.boss.phenix", "track.phenix_boss", "gallery.creature.etincelle", "level.confins_05",
                           "boss.liche", "gallery.boss.liche", "track.liche_boss", "gallery.creature.spectre", "level.confins_06",
                           "boss.titan", "gallery.boss.titan", "track.titan_boss", "gallery.creature.drone_titan", "gallery.creature.vanne"})
        CHECK(leaves.count(id));
}

// 2.69: every asset newer than the version the player had carries the pill in ADMINISTRATION ›
// Assets until it is focused or clicked; its folders, the tab and the menu entry keep theirs
// while anything under them is new; nothing older than « pillsFrom »; the seen ones are saved.
TEST_CASE("new badges: assets newer than the player's version are new, their folders, the tab and the menu with them") {
    fs::path f = tempFile();
    auto setUp = [](NewBadges& b) {
        b.setCatalog({{"menu.admin", "2.40.0", {}}, {"admin.tab.assets", "2.62.0", {"menu.admin"}}});
        return b.addAssetVersions({{"sprites/enemies/pyroclast.png", "2.68.0"},
                                   {"music/asterion_film.ogg", "2.68.0"},
                                   {"fonts/Orbitron.ttf", "2.69.0"},     // capitals: a font
                                   {"ui/raids/asterion.png", "2.66.1"},  // the version the player had: not new
                                   {"ui/old_panel.png", "2.60.0"},       // before « pillsFrom »: never
                                   {"sounds/jump.wav", "1.0.0"},
                                   {"../outside.png", "2.69.0"},         // junk: ignored
                                   {"sprites/bad.png", "soon"}},
                                  "2.65.0", "admin.tab.assets");
    };
    // The asset tree of the tab, known once its catalogue is read: asset → folders → root → tab.
    auto linkTree = [](NewBadges& b) {
        const std::string root = NewBadges::assetFolderFeature("");
        b.link(NewBadges::assetFeature("sprites/enemies/pyroclast.png"), {NewBadges::assetFolderFeature("boss/raid/pyroclast")});
        b.link(NewBadges::assetFolderFeature("boss/raid/pyroclast"), {NewBadges::assetFolderFeature("boss/raid")});
        b.link(NewBadges::assetFolderFeature("boss/raid"), {root});
        b.link(NewBadges::assetFeature("music/asterion_film.ogg"), {NewBadges::assetFolderFeature("music/stories")});
        b.link(NewBadges::assetFolderFeature("music/stories"), {root});
        b.link(NewBadges::assetFeature("fonts/Orbitron.ttf"), {NewBadges::assetFolderFeature("fonts")});
        b.link(NewBadges::assetFolderFeature("fonts"), {root});
        b.link(NewBadges::assetFeature("ui/raids/asterion.png"), {NewBadges::assetFolderFeature("ui")});
        b.link(NewBadges::assetFolderFeature("ui"), {root});
        b.link(root, {"admin.tab.assets"});
        b.link(root, {"admin.tab.assets"});  // twice: no change
    };
    {
        NewBadges b;
        CHECK(setUp(b) == 4);  // pyroclast, the film, Orbitron, the raids' Asterion
        CHECK(!b.has(NewBadges::assetFeature("ui/old_panel.png")));
        CHECK(!b.has(NewBadges::assetFeature("../outside.png")));
        b.init(f, "2.66.1", "2.69.0");
        // Before the tab has read its catalogue: the asset, the tab and the menu already.
        CHECK(b.isNew(NewBadges::assetFeature("sprites/enemies/pyroclast.png")));
        CHECK(b.isNew("admin.tab.assets"));
        CHECK(b.isNew("menu.admin"));
        linkTree(b);
        CHECK(b.isNew(NewBadges::assetFeature("fonts/Orbitron.ttf")));
        CHECK(!b.isNew(NewBadges::assetFeature("ui/raids/asterion.png")));  // 2.66.1: the player had it
        CHECK(!b.isNew(NewBadges::assetFeature("sounds/jump.wav")));
        CHECK(b.isNew(NewBadges::assetFolderFeature("boss/raid/pyroclast")));
        CHECK(b.isNew(NewBadges::assetFolderFeature("boss/raid")));
        CHECK(b.isNew(NewBadges::assetFolderFeature("")));
        CHECK(!b.isNew(NewBadges::assetFolderFeature("ui")));  // nothing new under it
        // A folder's own focus takes nothing away; the asset's focus or click does.
        CHECK(!b.markSeen(NewBadges::assetFolderFeature("boss/raid")));
        CHECK(b.isNew(NewBadges::assetFolderFeature("boss/raid")));
        CHECK(b.markSeen(NewBadges::assetFeature("sprites/enemies/pyroclast.png")));
        CHECK(!b.markSeen(NewBadges::assetFeature("sprites/enemies/pyroclast.png")));
        CHECK(!b.isNew(NewBadges::assetFeature("sprites/enemies/pyroclast.png")));
        CHECK(!b.isNew(NewBadges::assetFolderFeature("boss/raid/pyroclast")));
        CHECK(!b.isNew(NewBadges::assetFolderFeature("boss/raid")));
        CHECK(b.isNew(NewBadges::assetFolderFeature("")));  // the film and Orbitron still
        CHECK(b.isNew("admin.tab.assets"));
        CHECK(b.save());
    }
    {
        // Next start (and next update): what was seen stays seen, the rest stays new.
        NewBadges b;
        setUp(b);
        b.init(f, "2.69.0", "2.70.0");
        CHECK(b.baseline() == "2.66.1");
        linkTree(b);
        CHECK(!b.isNew(NewBadges::assetFeature("sprites/enemies/pyroclast.png")));
        CHECK(b.isNew(NewBadges::assetFeature("music/asterion_film.ogg")));
        b.markSeen(NewBadges::assetFeature("music/asterion_film.ogg"));
        CHECK(b.isNew("menu.admin"));
        b.markSeen(NewBadges::assetFeature("fonts/Orbitron.ttf"));
        CHECK(!b.isNew(NewBadges::assetFolderFeature("")));
        CHECK(!b.isNew("admin.tab.assets"));
        CHECK(!b.isNew("menu.admin"));
    }
    std::error_code ec;
    fs::remove(f, ec);
    {
        // A fresh install: no asset is new.
        NewBadges b;
        setUp(b);
        b.init(f, "", "2.69.0");
        CHECK(!b.isNew(NewBadges::assetFeature("fonts/Orbitron.ttf")));
        CHECK(!b.isNew("admin.tab.assets"));
    }
    fs::remove(f, ec);
}

// The shipped data/assets/asset_versions.json: one version per installed asset (a merge that
// brings assets is followed by python3 tools/assets/code_refs.py, which refreshes it), none from
// the future, the raids' assets from 2.65.0 on carry the pill, the old ones never; the
// generator gives the same file twice.
TEST_CASE("asset versions: every installed asset has its version, the generator is deterministic") {
    auto j = FileUtil::loadJson(Paths::dataDir() / "assets" / "asset_versions.json");
    CHECK(j && j->contains("versions") && (*j)["versions"].is_object());
    if (!j || !j->contains("versions")) return;
    CHECK(j->value("pillsFrom", std::string()) == "2.65.0");
    std::map<std::string, std::string> versions;
    for (auto it = (*j)["versions"].begin(); it != (*j)["versions"].end(); ++it) versions[it.key()] = it->get<std::string>();
    std::set<std::string> installed;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(Paths::assetsDir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) installed.insert(Paths::normalizeSlashes(Paths::toUtf8(fs::relative(it->path(), Paths::assetsDir(), ec))));
    int missing = 0, extra = 0;
    for (const auto& a : installed)
        if (!versions.count(a) && ++missing <= 10) std::printf("  sans version : %s\n", a.c_str());
    for (const auto& [a, v] : versions)
        if (!installed.count(a) && ++extra <= 10) std::printf("  plus installé : %s\n", a.c_str());
    if (missing || extra) std::printf("  data/assets/asset_versions.json n'est pas à jour : python3 tools/assets/code_refs.py\n");
    CHECK(missing == 0);
    CHECK(extra == 0);
    // No version newer than the one in preparation (the newest of the game's and the changelog's).
    std::string preparing = COSMIC_VERSION_STRING;
    if (auto c = FileUtil::loadJson(Paths::dataDir() / "news" / "changelog.json"); c && c->is_object())
        for (auto it = c->begin(); it != c->end(); ++it)
            if (versionNewer(it.key(), preparing)) preparing = it.key();
    int fresh = 0;
    for (const auto& [a, v] : versions) {
        CHECK(!versionNewer(v, preparing));
        if (!versionNewer("2.65.0", v)) ++fresh;
    }
    for (const char* a : {"sprites/enemies/pyroclast.png", "music/asterion_film.ogg", "stories/asterion/torse.png", "ui/raids/asterion.png"})
        CHECK(versions.count(a) && !versionNewer("2.65.0", versions[a]));
    for (const char* a : {"sounds/jump.wav", "sprites/enemies/golem.png", "fonts/Orbitron.ttf"})
        CHECK(versions.count(a) && versionNewer("2.65.0", versions[a]));
    // Loaded by the game: exactly those from « pillsFrom » on can carry the pill.
    NewBadges b;
    b.setCatalog({});
    CHECK(b.loadAssetVersions(Paths::dataDir() / "assets" / "asset_versions.json", "admin.tab.assets") == fresh);
    CHECK(fresh >= 53);
    // The generator: up to date with assets/, and its own rules (stable order, a merge's
    // conflict read on both sides, new assets at the version in preparation, two runs alike).
    const fs::path root = Paths::installDir();
    if (!fs::exists(root / "tools" / "assets" / "asset_versions.py")) return;  // sources not here (installed game)
    if (std::system("python3 --version > " COSMIC_NULL_DEVICE " 2>&1") != 0) {
        std::printf("  (python3 absent : vérification du générateur des versions d'assets sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assets/asset_versions.py --check > " COSMIC_NULL_DEVICE " 2>&1") == 0);
    CHECK(std::system("python3 tools/assets/asset_versions.py --self-test > " COSMIC_NULL_DEVICE " 2>&1") == 0);
}
