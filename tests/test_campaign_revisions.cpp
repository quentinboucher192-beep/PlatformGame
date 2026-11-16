// Campaign revisions published online by an administrator (2.45): a downloaded revision is
// played instead of the bundled level (content, fingerprint, offline after a restart), only
// once its SHA-256, its id, its revision number and its ranked fingerprint in this game are
// the server's; a broken or unknown file falls back to the bundled level; a challenge finds
// the revision it was drawn on; the campaign steps (seals, stations, rating) run on one level.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

#include "engine/core/FileUtil.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/CampaignPipeline.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/online/CampaignRevisions.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/Sha256.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

const char* kLevel = "aethra_01";
const char* kServer = "https://cosmic.test";

fs::path tempDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() /
                 ("cosmic_campaign_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

struct Revision {
    CampaignRevisionFile file;
    std::string text;
};

/// A revision of kLevel as an administrator's game publishes it: the bundled level renamed,
/// a decor moved, at `version`.
Revision makeRevision(const ContentDatabase& content, int version, const std::string& name = "Nom publié en ligne") {
    auto level = content.loadLevel(kLevel);
    CHECK(level.has_value());
    level->name = name;
    level->metadata["revision"] = version;
    Revision r;
    r.text = level->toJson().dump(1) + "\n";
    r.file.levelId = kLevel;
    r.file.version = version;
    r.file.contentHash = CampaignRevisionStore::rankedHashOf(content, r.text);
    r.file.sha256 = Sha256::hex(r.text);
    r.file.size = static_cast<int64_t>(r.text.size());
    r.file.minGameVersion = "2.45.0";
    r.file.path = "/api/v1/campaign/levels/" + std::string(kLevel) + "/revisions/" + std::to_string(version) + "/file";
    CHECK(r.file.contentHash.size() == 64);
    return r;
}

Json fileJson(const CampaignRevisionFile& f) {
    return {{"version", f.version}, {"content_hash", f.contentHash}, {"sha256", f.sha256}, {"size", f.size},
            {"min_game_version", f.minGameVersion}, {"path", f.path}};
}

/// GET /campaign/revisions with kLevel at `current` (and `pinned` revisions a challenge plays).
Json manifest(const Revision& current, int maxVersion, const std::vector<const Revision*>& pinned = {}) {
    Json pins = Json::array();
    for (const Revision* p : pinned) pins.push_back(fileJson(p->file));
    return {{"format", "cosmic.campaign_revisions"},
            {"levels",
             {{{"level_id", kLevel},
               {"version", current.file.version},
               {"content_hash", current.file.contentHash},
               {"max_version", maxVersion},
               {"min_game_version", current.file.minGameVersion},
               {"file", fileJson(current.file)},
               {"rating", {{"version", 2}, {"score", 3.5}}},
               {"pinned", pins}}}}};
}

int bundledRevision(const ContentDatabase& content) { return CampaignRevisionStore::revisionOf(content.bundledLevelPath(kLevel)); }

}  // namespace

TEST_CASE("Campaign revisions: a downloaded revision is played instead of the bundled level, offline too") {
    ContentDatabase content;
    content.reload();
    const fs::path bundled = content.level(kLevel)->path;
    const std::string bundledName = content.level(kLevel)->name;
    const int v = bundledRevision(content) + 1;
    Revision rev = makeRevision(content, v);
    fs::path dir = tempDir("play");

    CampaignRevisionStore store(dir);
    store.load();
    auto todo = store.applyManifest(manifest(rev, v), kServer, content, "2.45.0");
    CHECK(todo.size() == 1);
    CHECK(todo[0].version == v && todo[0].sha256 == rev.file.sha256);
    std::string err;
    CHECK(store.install(content, todo[0], rev.text, &err));
    CHECK(err.empty());
    CHECK(store.save());
    // Nothing more to download once it is there.
    CHECK(store.applyManifest(manifest(rev, v), kServer, content, "2.45.0").empty());

    auto files = store.currentFiles(content, kServer, "2.45.0");
    CHECK(files.size() == 1 && files.count(kLevel));
    CHECK(content.setCampaignRevision(kLevel, files[kLevel]));
    CHECK(content.level(kLevel)->name == "Nom publié en ligne");
    CHECK(content.level(kLevel)->source == ContentSource::Builtin);
    CHECK(content.bundledLevelPath(kLevel) == bundled);
    auto played = content.loadLevel(kLevel);
    CHECK(played && played->name == "Nom publié en ligne");
    LevelFingerprints fps;
    const LevelFingerprint* fp = fps.get(content, kLevel);
    CHECK(fp && fp->hash == rev.file.contentHash && fp->version == v);
    // Kept across a reload of the content.
    content.reload();
    CHECK(content.level(kLevel)->path == files[kLevel]);
    // The rating of the publisher's game comes with it.
    const Json* rating = store.ratingOf(kLevel);
    CHECK(rating && jget<double>(*rating, "score", 0.0) == 3.5);

    // A restart without network: the last revision downloaded keeps working.
    CampaignRevisionStore offline(dir);
    offline.load();
    auto again = offline.currentFiles(content, kServer, "2.45.0");
    CHECK(again.size() == 1 && again[kLevel] == files[kLevel]);
    // Another server's revisions are never played.
    CHECK(offline.currentFiles(content, "https://autre.test", "2.45.0").empty());

    // A challenge drawn on the bundled revision still finds it; the downloaded one too.
    std::string bundledHash = CampaignRevisionStore::rankedHashOf(content, *FileUtil::readText(bundled));
    CHECK(offline.fileFor(content, kLevel, bundledHash) == bundled);
    CHECK(offline.fileFor(content, kLevel, rev.file.contentHash) == files[kLevel]);
    CHECK(!offline.fileFor(content, kLevel, std::string(64, 'a')));

    // Back to the bundled level.
    CHECK(content.setCampaignRevision(kLevel, {}));
    CHECK(content.level(kLevel)->path == bundled && content.level(kLevel)->name == bundledName);
    CHECK(content.level(kLevel)->bundledPath.empty());
    content.reload();
    CHECK(content.level(kLevel)->path == bundled);
    fs::remove_all(dir);
}

TEST_CASE("Campaign revisions: the hashes are checked, a broken or unknown file falls back to the bundled level") {
    ContentDatabase content;
    content.reload();
    const fs::path bundled = content.level(kLevel)->path;
    const int v = bundledRevision(content) + 1;
    Revision rev = makeRevision(content, v);
    fs::path dir = tempDir("hash");
    CampaignRevisionStore store(dir);
    std::string err;

    // Bytes that are not the server's file.
    CHECK(!store.install(content, rev.file, rev.text + " ", &err));
    CHECK(err.find("SHA-256") != std::string::npos);
    // The server's bytes but another fingerprint than announced (made for another game).
    CampaignRevisionFile wrongHash = rev.file;
    wrongHash.contentHash = std::string(64, 'b');
    CHECK(!store.install(content, wrongHash, rev.text, &err));
    CHECK(err.find("empreinte") != std::string::npos);
    // A revision number that is not the one announced.
    CampaignRevisionFile wrongVersion = rev.file;
    wrongVersion.version = v + 5;
    CHECK(!store.install(content, wrongVersion, rev.text, &err));
    // Not a level, or another level.
    std::string junk = "{ pas un niveau";
    CampaignRevisionFile broken = rev.file;
    broken.sha256 = Sha256::hex(junk);
    CHECK(!store.install(content, broken, junk, &err));
    Json other = Json::parse(rev.text);
    other["id"] = "aethra_02";
    std::string otherText = other.dump(1);
    CampaignRevisionFile otherFile = rev.file;
    otherFile.sha256 = Sha256::hex(otherText);
    CHECK(!store.install(content, otherFile, otherText, &err));
    CHECK(!FileUtil::exists(dir / (std::string(kLevel) + "-" + std::to_string(v + 5) + ".json")));

    // Installed, then damaged on disk: the bundled level is played again.
    store.applyManifest(manifest(rev, v), kServer, content, "2.45.0");
    CHECK(store.install(content, rev.file, rev.text, &err));
    store.save();
    auto files = store.currentFiles(content, kServer, "2.45.0");
    CHECK(files.size() == 1);
    CHECK(FileUtil::writeTextAtomic(files[kLevel], rev.text + "triché"));
    CHECK(store.currentFiles(content, kServer, "2.45.0").empty());
    // A file that is not a level of that id is never used by the content either.
    fs::path fake = dir / "fake.json";
    FileUtil::writeTextAtomic(fake, otherText);
    CHECK(!content.setCampaignRevision(kLevel, fake));
    CHECK(content.level(kLevel)->path == bundled);
    CHECK(!content.setCampaignRevision("no_such_level", fake));
    fs::remove_all(dir);
}

TEST_CASE("Campaign revisions: what the game downloads (newer game, newer bundled level, pinned, other hosts)") {
    ContentDatabase content;
    content.reload();
    const int bundled = bundledRevision(content);
    Revision rev = makeRevision(content, bundled + 1);
    Revision older = makeRevision(content, bundled + 2, "Révision d'un défi");
    Revision current = makeRevision(content, bundled + 3, "Révision actuelle");
    fs::path dir = tempDir("manifest");
    CampaignRevisionStore store(dir);

    // Made with a newer game: skipped.
    Revision future = rev;
    future.file.minGameVersion = "99.0.0";
    CHECK(store.applyManifest(manifest(future, bundled + 1), kServer, content, "2.45.0").empty());
    // The bundled level is newer than every revision the server knows: kept.
    CHECK(store.applyManifest(manifest(rev, bundled - 1), kServer, content, "2.45.0").empty());
    // A download path on another host is refused.
    Revision elsewhere = rev;
    elsewhere.file.path = "https://ailleurs.example/level.json";
    CHECK(store.applyManifest(manifest(elsewhere, bundled + 1), kServer, content, "2.45.0").empty());
    // A level this game does not have is ignored.
    Json unknown = manifest(rev, bundled + 1);
    unknown["levels"][0]["level_id"] = "niveau_inconnu";
    CHECK(store.applyManifest(unknown, kServer, content, "2.45.0").empty());

    // The current revision and the one a challenge still plays are both downloaded and kept.
    auto todo = store.applyManifest(manifest(current, bundled + 3, {&older}), kServer, content, "2.45.0");
    CHECK(todo.size() == 2);
    std::string err;
    CHECK(store.install(content, current.file, current.text, &err));
    CHECK(store.install(content, older.file, older.text, &err));
    CHECK(store.prune() == 0);
    CHECK(store.fileFor(content, kLevel, older.file.contentHash).has_value());
    // The challenge is over (no longer pinned): its revision is removed, the current one stays.
    store.applyManifest(manifest(current, bundled + 3), kServer, content, "2.45.0");
    CHECK(store.prune() == 1);
    CHECK(!store.fileFor(content, kLevel, older.file.contentHash).has_value());
    CHECK(store.currentFiles(content, kServer, "2.45.0").size() == 1);
    fs::remove_all(dir);
}

TEST_CASE("Campaign revisions: the campaign steps (seals, power stations, rating) on one edited level") {
    ContentDatabase content;
    content.reload();
    auto level = content.loadLevel(kLevel);
    CHECK(level && level->powerSlots() > 0);
    // What --plan-stations computes for this level without its stations, and the gates
    // --seal-levels finds.
    LevelData bare = *level;
    bare.objects.erase(std::remove_if(bare.objects.begin(), bare.objects.end(),
                                      [](const LevelObject& o) {
                                          return (o.type == "power_unlock" && o.prop<bool>("station", false)) || o.id.rfind("station_sign_", 0) == 0;
                                      }),
                       bare.objects.end());
    const auto plan = planPowerStations(bare, content);
    const auto gates = suggestGates(*level, analyzeLevel(*level, content));
    CampaignPipelineReport rep;
    LevelData out = prepareCampaignRevision(*level, content, 42, &rep);
    CHECK(jget<int>(out.metadata, "revision", 0) == 42);
    CHECK(rep.rating.score > 0.0f && rep.rating.score <= 5.0f);
    CHECK(rep.gatesAdded <= static_cast<int>(gates.size()));
    // The stations of the plan replace those of before, each with its sign and tablets.
    CHECK(rep.stationsRemoved > 0);
    if (rep.gatesAdded == 0) CHECK(rep.stations == static_cast<int>(plan.size()));
    int signs = 0, tablets = 0;
    for (const auto& o : out.objects) {
        if (o.type == "sign" && o.id.rfind("station_sign_", 0) == 0) ++signs;
        if (o.type == "power_unlock" && o.prop<bool>("station", false)) ++tablets;
    }
    CHECK(signs == rep.stations && tablets == rep.tablets && rep.stations > 0);
    std::set<std::string> ids;
    for (const auto& o : out.objects) CHECK(ids.insert(o.id).second);
    // A second pass on the prepared level adds no gate and places the same stations.
    CampaignPipelineReport rep2;
    LevelData twice = prepareCampaignRevision(out, content, 42, &rep2);
    CHECK(rep2.gatesAdded == 0 && rep2.stations == rep.stations && rep2.tablets == rep.tablets);
    CHECK(twice.objects.size() == out.objects.size());
    CHECK(twice.toJson().dump() == out.toJson().dump());
    // The fingerprint follows the revision number (a new hash for every revision).
    auto a = fingerprintRankedLevel(content, out.toJson());
    Json j = out.toJson();
    j["metadata"]["revision"] = 43;
    auto b = fingerprintRankedLevel(content, j);
    CHECK(a && b && a->hash != b->hash && a->version == 42 && b->version == 43);
}
