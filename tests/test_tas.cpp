// The machine's runs (docs/TAS.md): their file format, and the rule of the screens — a run is
// shown only for the level and the simulation it was computed for.

#include <string>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/tas/TasFile.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

const std::string kHash(64, 'a');

TasRecord sampleRecord() {
    TasRecord r;
    r.levelId = "sylvanox_01";
    r.levelVersion = 2;
    r.levelHash = kHash;
    r.simVersion = kSimulationVersion;
    r.category = RunCategory::NoDeath;
    r.timeMs = 41250;
    r.splits = {{"cp_1", 8125}, {"cp_2", 20500}};
    r.explored = 1843200;
    r.computedAt = "2026-11-27";
    r.tool = "cosmic_tas 1";
    r.route = {{100.0f, 200.0f}, {131.25f, 200.0f}, {170.04f, 188.5f}};
    r.deaths = 0;
    r.damageTaken = 2;
    r.collectibles = 12;
    r.totalCollectibles = 75;
    r.replay = tasReplayFileName(r.category);
    return r;
}

fs::path freshRoot(const char* tag) {
    fs::path root = Paths::userDir() / (std::string("tas_") + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    return root;
}

void writeReplay(const TasRecord& r, const fs::path& root) {
    ReplayData d;
    d.levelId = r.levelId;
    d.levelHash = r.levelHash;
    d.attemptId = makeUuidV4();
    d.seed = 7;
    d.simVersion = static_cast<uint16_t>(r.simVersion);
    d.inputs.assign(10, InputFrame{});
    d.frames.push_back(ReplayFrame{});
    d.durationMs = static_cast<uint32_t>(r.timeMs);
    CHECK(FileUtil::writeBinary(tasReplayPath(r, root), d.encode()));
}

}  // namespace

TEST_CASE("tas: a record survives its JSON unchanged") {
    const TasRecord r = sampleRecord();
    const Json j = r.toJson();
    CHECK(jstr(j, "format") == "cosmic.tas");
    CHECK(jstr(j, "category") == "NO_DEATH");
    CHECK(jstr(j, "replay") == "NO_DEATH.cprz");
    auto back = TasRecord::fromJson(j);
    CHECK(back.has_value());
    CHECK(back->levelId == r.levelId && back->levelVersion == 2 && back->levelHash == kHash);
    CHECK(back->simVersion == kSimulationVersion && back->category == RunCategory::NoDeath && back->timeMs == 41250);
    CHECK(back->splits.size() == 2 && back->splits[1].checkpoint == "cp_2" && back->splits[1].timeMs == 20500);
    CHECK(back->explored == 1843200 && back->computedAt == "2026-11-27" && back->tool == "cosmic_tas 1");
    CHECK(back->route.size() == 3 && back->route[2].x == 170.0f && back->route[2].y == 188.5f);  // to the tenth of a pixel
    CHECK(back->damageTaken == 2 && back->collectibles == 12 && back->totalCollectibles == 75);
    // Incomplete or suspicious records are refused.
    Json bad = j;
    bad["format"] = "cosmic.other";
    CHECK(!TasRecord::fromJson(bad));
    bad = j;
    bad["category"] = "SPEEDRUN";
    CHECK(!TasRecord::fromJson(bad));
    bad = j;
    bad["replay"] = "../../saves/x.cprz";
    CHECK(!TasRecord::fromJson(bad));
    bad = j;
    bad["timeMs"] = 0;
    CHECK(!TasRecord::fromJson(bad));
    bad = j;
    bad["levelHash"] = "1234";
    CHECK(!TasRecord::fromJson(bad));
}

TEST_CASE("tas: shown only for the level and the simulation it was computed for") {
    const fs::path root = freshRoot("rule");
    TasRecord r = sampleRecord();
    // No file: nothing.
    CHECK(!tasFor(r.levelId, r.category, kHash, root));
    CHECK(writeTasFile(tasFilePath(r.levelId, r.category, root), r));
    CHECK(tasFilePath(r.levelId, r.category, root).filename() == "NO_DEATH.json");
    // The JSON without its replay: hidden (nothing to watch).
    CHECK(!tasFor(r.levelId, r.category, kHash, root));
    writeReplay(r, root);
    auto shown = tasFor(r.levelId, r.category, kHash, root);
    CHECK(shown.has_value() && shown->timeMs == 41250);
    auto replay = loadTasReplay(*shown, root);
    CHECK(replay.has_value() && replay->exact() && replay->inputs.size() == 10);
    // Another version of the level: hidden until computed again.
    CHECK(!tasFor(r.levelId, r.category, std::string(64, 'b'), root));
    // Another category or level: nothing.
    CHECK(!tasFor(r.levelId, RunCategory::AnyPercent, kHash, root));
    CHECK(!tasFor("sylvanox_02", r.category, kHash, root));
    // Computed with other rules of the simulation: hidden.
    r.simVersion = kSimulationVersion - 1;
    CHECK(writeTasFile(tasFilePath(r.levelId, r.category, root), r));
    CHECK(!tasFor(r.levelId, r.category, kHash, root));
    std::error_code ec;
    fs::remove_all(root, ec);
}
