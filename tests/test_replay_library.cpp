// « Replays téléchargés » (2.45): the online replays kept on this computer to be watched
// without a connection (game/ghost/ReplayLibrary.h): the index written and read back, the
// limits, damaged or hand-made files and indexes, deletion, and the detection of a replay
// made on another version of its level.

#include <system_error>

#include "engine/core/FileUtil.h"
#include "game/content/ContentDatabase.h"
#include "game/ghost/ReplayLibrary.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// A small but complete replay (trace, inputs) of `levelId`, made on version `hash`.
ReplayData sampleReplay(const std::string& levelId, const std::string& hash, uint32_t durationMs = 4321) {
    ReplayData r;
    r.levelId = levelId;
    r.levelHash = hash;
    r.attemptId = makeUuidV4();
    r.durationMs = durationMs;
    r.seed = 7;
    for (int i = 0; i < 6; ++i) r.frames.push_back({160.0f + static_cast<float>(i) * 8.0f, 800.0f, 0, ReplayFrame::Grounded, 5});
    for (int i = 0; i < 120; ++i) {
        InputFrame f;
        f.moveX = 127;
        f.held = 2;
        r.inputs.push_back(f);
    }
    r.keysPerSlot = true;
    return r;
}

std::string hexHash(char c) { return std::string(64, c); }

fs::path tempRoot() { return fs::temp_directory_path() / ("cosmic_replays_test_" + makeUuidV4().substr(0, 8)); }

DownloadedReplay metaFor(const std::string& source, const std::string& remoteId) {
    DownloadedReplay m;
    m.id = ReplayLibrary::makeId(source, remoteId);
    m.source = source;
    m.player = "Astra";
    m.playerId = "p-1";
    m.levelName = "Seuil glacé";
    m.category = "ANY%";
    m.achievedAt = "2026-11-12T18:04:00Z";
    return m;
}

}  // namespace

TEST_CASE("replay library: a download is kept with its description, the index is read back") {
    fs::path root = tempRoot();
    {
        ReplayLibrary lib(root);
        CHECK(lib.entries().empty());
        ReplayData rep = sampleReplay("glacior_01", hexHash('a'));
        std::vector<uint8_t> bytes = rep.encode();
        DownloadedReplay m = metaFor("record", "3f2a-91");
        m.downloadedAt = 1000;
        std::string err;
        CHECK(lib.add(bytes, m, &err));
        CHECK(err.empty());
        const DownloadedReplay* e = lib.find(m.id);
        CHECK(e != nullptr);
        // The level and its version come from the replay; the time too when the board gave none.
        CHECK(e->levelId == "glacior_01");
        CHECK(e->levelHash == hexHash('a'));
        CHECK(e->timeMs == 4321);
        CHECK(e->bytes == bytes.size());
        CHECK(e->file == "record-3f2a-91.cprz");
        CHECK(FileUtil::exists(root / "record-3f2a-91.cprz"));
        CHECK(FileUtil::exists(root / "index.json"));
        // A second one, downloaded later, comes first.
        DownloadedReplay m2 = metaFor("race", "run-2");
        m2.levelId = "glacior_02";  // the level installed here, noted by the screen
        m2.timeMs = 99000;
        m2.downloadedAt = 2000;
        CHECK(lib.add(sampleReplay("autre_id", hexHash('b')).encode(), m2, &err));
        CHECK(lib.entries().size() == 2);
        CHECK(lib.entries()[0].id == "race-run-2");
        CHECK(lib.totalBytes() > bytes.size());
        // Downloaded again: replaced, not counted twice.
        CHECK(lib.add(bytes, m, &err));
        CHECK(lib.entries().size() == 2);
    }
    // Read back by a new library (the game started again, offline).
    ReplayLibrary again(root);
    CHECK(again.entries().size() == 2);
    CHECK(again.entries()[0].id == "race-run-2");
    const DownloadedReplay* e = again.find("record-3f2a-91");
    CHECK(e != nullptr);
    CHECK(e->player == "Astra");
    CHECK(e->playerId == "p-1");
    CHECK(e->levelName == "Seuil glacé");
    CHECK(e->category == "ANY%");
    CHECK(e->achievedAt == "2026-11-12T18:04:00Z");
    CHECK(e->downloadedAt == 1000);
    const DownloadedReplay* r = again.find("race-run-2");
    CHECK(r && r->levelId == "glacior_02" && r->timeMs == 99000);
    // Played on the level noted at download, exactly as it was recorded.
    std::string err;
    auto back = again.load("race-run-2", &err);
    CHECK(back.has_value());
    CHECK(back->levelId == "glacior_02");
    CHECK(back->levelHash == hexHash('b'));
    CHECK(back->inputs.size() == 120);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("replay library: corrupted, truncated or hand-made replays and indexes are refused") {
    fs::path root = tempRoot();
    ReplayLibrary lib(root);
    std::string err;
    // Not a replay.
    std::vector<uint8_t> junk(500, 0x41);
    CHECK(!lib.add(junk, metaFor("record", "junk"), &err));
    CHECK(!err.empty());
    CHECK(!lib.has("record-junk"));
    CHECK(!FileUtil::exists(root / "record-junk.cprz"));
    // A real replay cut in the middle, or with a flipped byte in its compressed stream.
    std::vector<uint8_t> good = sampleReplay("glacior_01", hexHash('c')).encode();
    std::vector<uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 2));
    CHECK(!lib.add(cut, metaFor("record", "cut"), &err));
    std::vector<uint8_t> flipped = good;
    flipped[flipped.size() - 3] ^= 0x5A;
    CHECK(!lib.add(flipped, metaFor("record", "flip"), &err));
    // Too big to be a replay.
    std::vector<uint8_t> huge(static_cast<size_t>(ReplayLibrary::kMaxFileBytes) + 1, 0);
    CHECK(!lib.add(huge, metaFor("record", "huge"), &err));
    // Ids that are not ids never become file names.
    CHECK(ReplayLibrary::makeId("record", "../../evil").empty());
    CHECK(ReplayLibrary::makeId("record", "a/b").empty());
    CHECK(ReplayLibrary::makeId("record", "").empty());
    CHECK(ReplayLibrary::makeId("record", std::string(120, 'a')).empty());
    DownloadedReplay bad = metaFor("record", "x");
    bad.id = "../evil";
    CHECK(!lib.add(good, bad, &err));
    CHECK(lib.entries().empty());

    // A kept replay whose file was damaged afterwards: refused when read, with the reason.
    CHECK(lib.add(good, metaFor("record", "ok"), &err));
    CHECK(FileUtil::writeBinary(root / "record-ok.cprz", junk));
    CHECK(!lib.load("record-ok", &err).has_value());
    CHECK(!err.empty());
    // Replaced by another valid replay: not the version noted at download.
    CHECK(FileUtil::writeBinary(root / "record-ok.cprz", sampleReplay("glacior_01", hexHash('d')).encode()));
    CHECK(!lib.load("record-ok", &err).has_value());
    CHECK(!ReplayLibrary::loadFile(root / "record-ok.cprz", hexHash('c'), &err).has_value());
    CHECK(ReplayLibrary::loadFile(root / "record-ok.cprz", hexHash('d'), &err).has_value());

    // An index edited by hand: an entry pointing out of the folder, one without its file and
    // a broken one are left out; the valid one stays.
    Json index{{"format", "cosmic.downloaded_replays"},
               {"version", 1},
               {"replays", Json::array({Json{{"id", "record-ok"}, {"file", "record-ok.cprz"}, {"level_id", "glacior_01"}, {"level_hash", hexHash('d')}},
                                        Json{{"id", "record-out"}, {"file", "../../secret.cprz"}, {"level_id", "glacior_01"}},
                                        Json{{"id", "record-gone"}, {"file", "record-gone.cprz"}, {"level_id", "glacior_01"}},
                                        Json{{"id", 42}},
                                        Json("texte")})}};
    CHECK(FileUtil::writeTextAtomic(root / "index.json", index.dump()));
    ReplayLibrary reread(root);
    CHECK(reread.entries().size() == 1);
    CHECK(reread.has("record-ok"));
    CHECK(reread.load("record-ok", &err).has_value());
    // A damaged index: an empty list, no crash.
    CHECK(FileUtil::writeTextAtomic(root / "index.json", "{ pas du json"));
    ReplayLibrary broken(root);
    CHECK(broken.entries().empty());
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("replay library: deletion and limits") {
    fs::path root = tempRoot();
    std::string err;
    {
        ReplayLibrary lib(root);
        std::vector<uint8_t> bytes = sampleReplay("glacior_01", hexHash('e')).encode();
        CHECK(lib.add(bytes, metaFor("record", "r1"), &err));
        CHECK(lib.add(bytes, metaFor("community", "r2"), &err));
        CHECK(lib.remove("record-r1", &err));
        CHECK(!lib.has("record-r1"));
        CHECK(!FileUtil::exists(root / "record-r1.cprz"));
        CHECK(!lib.remove("record-r1", &err));  // already gone: says so
        CHECK(!err.empty());
        CHECK(!lib.load("record-r1", &err).has_value());
        CHECK(lib.roomFor(bytes.size()).empty());
        CHECK(!lib.roomFor(ReplayLibrary::kMaxTotalBytes).empty());  // beyond the size limit: an honest reason
    }
    // The deletion is in the index too.
    ReplayLibrary again(root);
    CHECK(again.entries().size() == 1);
    CHECK(again.has("community-r2"));
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("replay library: a replay of another version of its level is told apart") {
    // The rule: the same versions as the replay viewer (ranked or gameplay fingerprint).
    const std::vector<std::string> installed{hexHash('1'), hexHash('2')};
    CHECK(matchReplayLevel(hexHash('1'), true, installed) == ReplayLevelMatch::Same);
    CHECK(matchReplayLevel(hexHash('2'), true, installed) == ReplayLevelMatch::Same);
    CHECK(matchReplayLevel(hexHash('3'), true, installed) == ReplayLevelMatch::OtherRevision);
    CHECK(matchReplayLevel("", true, installed) == ReplayLevelMatch::OtherRevision);
    CHECK(matchReplayLevel(hexHash('1'), false, {}) == ReplayLevelMatch::NotInstalled);

    // With the game's levels: a replay of the installed version, of another one, of a level
    // that does not exist here.
    ContentDatabase content;
    content.reload();
    auto versions = installedLevelVersions(content, "glacior_01");
    CHECK(versions.has_value());
    CHECK(!versions->empty());
    auto level = content.loadLevel("glacior_01");
    CHECK(level.has_value());
    const std::string gameplay = fingerprintGameplay(content, *level);
    CHECK(matchReplayLevel(gameplay, true, *versions) == ReplayLevelMatch::Same);
    LevelFingerprints ranked;
    const LevelFingerprint* fp = ranked.get(content, "glacior_01");
    CHECK(fp != nullptr);
    CHECK(matchReplayLevel(fp->hash, true, *versions) == ReplayLevelMatch::Same);
    // The same level with one solid block moved: another version.
    LevelData changed = *level;
    LevelObject* block = nullptr;
    for (auto& o : changed.objects)
        if (!block && o.type == "solid") block = &o;
    CHECK(block != nullptr);
    block->rect.x += 32.0f;
    const std::string other = fingerprintGameplay(content, changed);
    CHECK(other != gameplay);
    CHECK(matchReplayLevel(other, true, *versions) == ReplayLevelMatch::OtherRevision);
    CHECK(!installedLevelVersions(content, "niveau_qui_n_existe_pas").has_value());
}
