#include <miniz.h>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "packaging/PackageExporter.h"
#include "packaging/PackageImporter.h"
#include "packaging/Sha256.h"
#include "packaging/ZipArchive.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
fs::path tempFile(const std::string& name) {
    fs::path p = Paths::userDir() / "test_tmp";
    fs::create_directories(p);
    return p / name;
}

struct Env {
    ContentDatabase content;
    ResourceManager res;
    Env() {
        content.reload();
        res.init(nullptr, nullptr);
    }
};

fs::path exportLevel(Env& env, const std::string& name, ExportPlan* outPlan = nullptr) {
    auto level = env.content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    PackageExporter ex(env.content, env.res);
    ExportOptions opt;
    opt.author = "Testeur";
    opt.destination = tempFile(name);
    ExportPlan plan = ex.plan(*level, opt);
    std::string err;
    CHECK(plan.ok());
    CHECK(ex.write(*level, plan, opt, &err));
    if (outPlan) *outPlan = plan;
    return opt.destination;
}

/// Rewrites an archive applying `mutate` to its entries.
void rewrite(const fs::path& in, const fs::path& out,
             const std::function<void(std::string&, std::vector<uint8_t>&)>& mutate,
             const std::vector<std::pair<std::string, std::vector<uint8_t>>>& extra = {}) {
    ZipReader r;
    CHECK(r.open(in));
    ZipWriter w;
    for (const auto& e : r.entries()) {
        auto d = r.read(e, 1u << 30);
        CHECK(d.has_value());
        std::string name = e.name;
        mutate(name, *d);
        w.add(name, *d);
    }
    for (const auto& [n, d] : extra) w.add(n, d);
    CHECK(w.save(out));
}
}  // namespace

TEST_CASE("packaging: sha256 known vectors") {
    CHECK(Sha256::hex(std::string("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::hex(std::string("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string million(1000000, 'a');
    CHECK(Sha256::hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("packaging: version comparison") {
    CHECK(PackageFormat::compareVersions("1.0.0", "1.0.0") == 0);
    CHECK(PackageFormat::compareVersions("1.2", "1.10") < 0);
    CHECK(PackageFormat::compareVersions("2.0.0", "1.9.9") > 0);
}

TEST_CASE("packaging: export collects every dependency") {
    Env env;
    ExportPlan plan;
    fs::path file = exportLevel(env, "full.cplanet", &plan);
    CHECK(FileUtil::exists(file));
    CHECK(plan.countKind("level") == 1);
    CHECK(plan.countKind("planet") == 1);
    CHECK(plan.countKind("enemy") == 7);  // glowmite, sentinel, sporeflower, stalker, golem, wisp, thornwarden
    CHECK(plan.countKind("power") == 5);
    bool hasBossSheet = false, hasMusic = false, hasTiles = false, hasFont = false;
    for (const auto& i : plan.items) {
        hasBossSheet |= i.archivePath == "assets/sprites/enemies/thornwarden.png";
        hasMusic |= i.archivePath == "assets/music/sylvanox_boss.ogg";
        hasTiles |= i.archivePath == "assets/textures/tiles/crystal.png";
        hasFont |= i.archivePath == "assets/fonts/Orbitron.ttf";
    }
    CHECK(hasBossSheet);
    CHECK(hasMusic);
    CHECK(hasTiles);
    CHECK(hasFont);
}

TEST_CASE("packaging: import works on a 'PC' without the original assets") {
    Env env;
    fs::path file = exportLevel(env, "roundtrip.cplanet");
    PackageImporter imp(env.content);
    PackageInspection insp = imp.inspect(file);
    CHECK(insp.valid);
    CHECK(insp.errors.empty());
    CHECK(insp.level.has_value());
    // The level id exists already (built-in): it must be renamed, never overwritten.
    bool levelConflict = false;
    for (const auto& c : insp.conflicts) levelConflict |= c.kind == ConflictKind::Level;
    CHECK(levelConflict);
    std::string newId, err;
    CHECK(imp.install(insp, ConflictPolicy::Rename, &newId, &err));
    CHECK(newId != "sylvanox_01");
    env.content.reload();
    const LevelEntry* e = env.content.level(newId);
    CHECK(e != nullptr);
    CHECK(e->source == ContentSource::Package);
    CHECK(env.content.level("sylvanox_01")->source == ContentSource::Builtin);

    // Simulate another computer: only the package assets are mounted.
    ResourceManager isolated;
    isolated.init(nullptr, nullptr);
    isolated.pushMount(e->assetRoot);
    std::vector<std::string> needed = DependencyCollector::coreRuntimeAssets();
    auto lvl = env.content.loadLevel(newId);
    CHECK(lvl.has_value());
    DependencySet deps = DependencyCollector(env.content, env.res).collect(*lvl);
    for (const auto& a : deps.assets) {
        bool inPackage = FileUtil::exists(e->assetRoot / Paths::fromUtf8(a));
        if (!inPackage) std::cout << "  missing in package: " << a << "\n";
        CHECK(inPackage);
    }
    // Enemies of the package are registered with their package asset root.
    const EnemyDefinition* boss = env.content.enemy("thornwarden");
    CHECK(boss != nullptr);

    // An editable copy made by the level editor keeps using the package assets,
    // including after a restart (reload).
    LevelData copy = *lvl;
    copy.id = newId + "_copie";
    copy.metadata["assetPackage"] = e->packageId;
    const fs::path packageAssets = e->assetRoot;
    CHECK(env.content.saveUserLevel(copy));
    CHECK(env.content.level(copy.id) != nullptr);
    CHECK(env.content.level(copy.id)->assetRoot == packageAssets);
    env.content.reload();
    const LevelEntry* ce = env.content.level(copy.id);
    CHECK(ce != nullptr);
    CHECK(ce->source == ContentSource::User);
    CHECK(ce->assetRoot == packageAssets);
    // Neither the imported level nor the player's copy joins the Sylvanox campaign, even
    // though their "planet" field says sylvanox: they stay in the WORKSHOP.
    CHECK(copy.planet == "sylvanox");
    for (const LevelEntry* l : env.content.levelsForPlanet("sylvanox")) {
        CHECK(l->source == ContentSource::Builtin);
        CHECK(l->id != copy.id);
        CHECK(l->id != newId);
    }
    CHECK(env.content.campaignPlanetOf(copy.id).empty());
    CHECK(env.content.campaignPlanetOf(newId).empty());
    CHECK(env.content.campaignPlanetOf("sylvanox_01") == "sylvanox");
    CHECK(env.content.deleteUserLevel(copy.id));

    CHECK(env.content.removePackage(env.content.packages().front().id));
}

TEST_CASE("packaging: corrupted and malicious archives are rejected") {
    Env env;
    fs::path good = exportLevel(env, "base.cplanet");
    PackageImporter imp(env.content);

    // 1. One flipped byte in an asset -> SHA-256 mismatch.
    fs::path tampered = tempFile("tampered.cplanet");
    rewrite(good, tampered, [](std::string& name, std::vector<uint8_t>& d) {
        if (name == "COSMIC_LEVEL/assets/sounds/jump.wav" && d.size() > 100) d[100] ^= 0x5A;
    });
    PackageInspection a = imp.inspect(tampered);
    CHECK(!a.valid);
    CHECK(!a.errors.empty() && a.errors[0].find("SHA-256") != std::string::npos);

    // 2. Path traversal entry.
    fs::path evil = tempFile("evil.cplanet");
    rewrite(good, evil, [](std::string&, std::vector<uint8_t>&) {}, {{"COSMIC_LEVEL/../../evil.json", {'{', '}'}}});
    CHECK(!imp.inspect(evil).valid);

    // 3. Executable content.
    fs::path exe = tempFile("exe.cplanet");
    rewrite(good, exe, [](std::string&, std::vector<uint8_t>&) {}, {{"COSMIC_LEVEL/scripts/run.dll", {'M', 'Z'}}});
    PackageInspection c = imp.inspect(exe);
    CHECK(!c.valid);

    // 4. Too recent engine requirement.
    fs::path future = tempFile("future.cplanet");
    rewrite(good, future, [](std::string& name, std::vector<uint8_t>& d) {
        if (name != "COSMIC_LEVEL/manifest.json") return;
        Json j = Json::parse(d.begin(), d.end());
        j["minEngineVersion"] = "99.0.0";
        std::string s = j.dump();
        d.assign(s.begin(), s.end());
    });
    PackageInspection f = imp.inspect(future);
    CHECK(!f.valid);
    CHECK(f.errors[0].find("99.0.0") != std::string::npos);

    // 5. Zip bomb: highly compressible large file.
    fs::path bomb = tempFile("bomb.cplanet");
    std::vector<uint8_t> zeros(8u << 20, 0);
    rewrite(good, bomb, [](std::string&, std::vector<uint8_t>&) {}, {{"COSMIC_LEVEL/assets/huge.json", zeros}});
    CHECK(!imp.inspect(bomb).valid);

    // 6. Not a zip at all.
    fs::path junk = tempFile("junk.cplanet");
    FileUtil::writeTextAtomic(junk, "this is not a package");
    CHECK(!imp.inspect(junk).valid);
}

TEST_CASE("content: native entities are usable everywhere but never replaced") {
    Env env;
    const EnemyDefinition* native = env.content.enemy("glowmite");
    CHECK(native != nullptr);
    CHECK(native->builtin);
    CHECK(env.content.isNativeEnemy("glowmite"));
    const float nativeHp = native->hp;

    // Saving under a native id is refused.
    EnemyDefinition mod = *native;
    mod.hp = nativeHp + 50.0f;
    mod.builtin = false;
    CHECK(!env.content.saveUserEnemy(mod));
    CHECK(env.content.enemy("glowmite")->hp == nativeHp);

    // A file planted in the user folder under a native id is ignored at load.
    fs::path planted = Paths::userEnemiesDir() / "glowmite.json";
    CHECK(FileUtil::saveJson(planted, mod.toJson()));
    env.content.reload();
    CHECK(env.content.enemy("glowmite")->builtin);
    CHECK(env.content.enemy("glowmite")->hp == nativeHp);
    fs::remove(planted);

    // The player's version lives under its own id.
    mod.id = env.content.freeEnemyId("glowmite_perso");
    CHECK(mod.id != "glowmite");
    CHECK(env.content.saveUserEnemy(mod));
    CHECK(env.content.enemy(mod.id) != nullptr);
    CHECK(!env.content.enemy(mod.id)->builtin);
    CHECK(env.content.enemy("glowmite")->hp == nativeHp);
    CHECK(env.content.deleteUserEnemy(mod.id));
    CHECK(!env.content.deleteUserEnemy("glowmite"));
    CHECK(env.content.enemy("glowmite") != nullptr);

    // A native asset is never replaced by a file of the user folder or of a package.
    fs::path userCopy = Paths::userDir() / "assets" / "animations" / "glowmite.json";
    CHECK(FileUtil::saveJson(userCopy, Json{{"texture", "sprites/enemies/other.png"}}));
    auto resolved = env.res.resolve("animations/glowmite.json");
    CHECK(resolved.has_value());
    CHECK(resolved && *resolved == Paths::assetsDir() / "animations" / "glowmite.json");
    fs::remove(userCopy);
    // ...while new files of the user folder are found.
    fs::path userNew = Paths::userDir() / "assets" / "animations" / "user" / "test_only.json";
    CHECK(FileUtil::saveJson(userNew, Json{{"texture", "sprites/enemies/glowmite.png"}}));
    CHECK(env.res.resolve("animations/user/test_only.json").has_value());
    fs::remove(userNew);
}

TEST_CASE("packaging: a package never replaces a native enemy, even with 'Replace'") {
    Env env;
    fs::path base = exportLevel(env, "native.cplanet");
    fs::path step = tempFile("native_step.cplanet");
    fs::path changed = tempFile("native_changed.cplanet");
    const float nativeHp = env.content.enemy("thornwarden")->hp;
    // A consistent package (valid fingerprints) whose author changed a native boss.
    std::vector<uint8_t> enemyBytes;
    rewrite(base, step, [&](std::string& name, std::vector<uint8_t>& data) {
        if (name != "COSMIC_LEVEL/enemies/thornwarden.json") return;
        Json j = Json::parse(std::string(data.begin(), data.end()));
        j["hp"] = nativeHp * 10.0f;
        std::string out = j.dump(1);
        data.assign(out.begin(), out.end());
        enemyBytes = data;
    });
    CHECK(!enemyBytes.empty());
    rewrite(step, changed, [&](std::string& name, std::vector<uint8_t>& data) {
        if (name != "COSMIC_LEVEL/manifest.json") return;
        Json m = Json::parse(std::string(data.begin(), data.end()));
        for (auto& f : m["files"])
            if (f.value("path", "") == "enemies/thornwarden.json") {
                f["size"] = enemyBytes.size();
                f["sha256"] = Sha256::hex(enemyBytes);
            }
        std::string out = m.dump(1);
        data.assign(out.begin(), out.end());
    });
    PackageImporter imp(env.content);
    PackageInspection insp = imp.inspect(changed);
    CHECK(insp.valid);
    for (const auto& e : insp.errors) std::cout << "  " << e << "\n";
    bool enemyConflict = false;
    for (const auto& c : insp.conflicts) enemyConflict |= c.kind == ConflictKind::Enemy && c.id == "thornwarden";
    CHECK(enemyConflict);
    std::string newId, err;
    CHECK(imp.install(insp, ConflictPolicy::Replace, &newId, &err));
    env.content.reload();
    // The native boss is untouched; the package's version got its own id and its level uses it.
    CHECK(env.content.enemy("thornwarden")->builtin);
    CHECK(env.content.enemy("thornwarden")->hp == nativeHp);
    auto lvl = env.content.loadLevel(newId);
    CHECK(lvl.has_value());
    bool usesRenamed = false, usesNative = false;
    for (const auto& o : lvl->objects) {
        if (o.type != "enemy") continue;
        usesRenamed |= o.propStr("enemy").rfind("thornwarden__", 0) == 0;
        usesNative |= o.propStr("enemy") == "thornwarden";
    }
    CHECK(usesRenamed);
    CHECK(!usesNative);
    for (const auto* e : env.content.enemies())
        if (e->id.rfind("thornwarden__", 0) == 0) CHECK(e->hp == nativeHp * 10.0f);
    CHECK(env.content.removePackage(env.content.packages().front().id));
}
