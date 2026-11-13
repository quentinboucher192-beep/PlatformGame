#include "game/online/LevelFingerprint.h"

#include <algorithm>
#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "packaging/Sha256.h"

namespace cosmic {

std::optional<LevelFingerprint> fingerprintLevelJson(const Json& level) {
    if (!level.is_object()) return std::nullopt;
    LevelFingerprint f;
    f.levelId = jstr(level, "id");
    if (f.levelId.empty()) return std::nullopt;
    if (level.contains("metadata") && level["metadata"].is_object()) f.version = std::max(1, jget<int>(level["metadata"], "revision", 1));
    // nlohmann::json keeps object keys sorted and prints numbers in their shortest
    // round-trip form: the dump is a canonical text of the level.
    f.hash = Sha256::hex(level.dump());
    return f;
}

std::optional<LevelFingerprint> fingerprintLevelFile(const fs::path& file, std::string* error) {
    auto text = FileUtil::readText(file);
    if (!text) {
        if (error) *error = "lecture impossible";
        return std::nullopt;
    }
    Json j = Json::parse(*text, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "JSON invalide";
        return std::nullopt;
    }
    return fingerprintLevelJson(j);
}

const LevelFingerprint* LevelFingerprints::get(const ContentDatabase& content, const std::string& levelId) {
    auto it = cache_.find(levelId);
    if (it == cache_.end()) {
        std::optional<LevelFingerprint> fp;
        const LevelEntry* e = content.level(levelId);
        // Only the levels shipped with the game can be ranked online.
        if (e && e->source == ContentSource::Builtin) fp = fingerprintLevelFile(e->path);
        it = cache_.emplace(levelId, fp).first;
    }
    return it->second ? &*it->second : nullptr;
}

Json onlineManifestEntry(const ContentDatabase& content, const LevelData& level, const LevelFingerprint& fp) {
    AudioSystem audio;
    ResourceManager res;
    res.init(nullptr, &audio);
    WorldServices ws{res, audio, content, nullptr, 0.0f, 0.0f};
    GameWorld world(ws, WorldMode::Play);
    world.load(level);
    // Where the player stands when the timer starts (after the intro).
    constexpr float dt = 1.0f / 120.0f;
    for (int i = 0; i < 120 * 120 && !world.timer().running(); ++i) {
        if (world.cinematic().active() && world.cinematic().skippable()) world.cinematic().skip(world);
        world.fixedUpdate(PlayerInput{}, dt);
    }
    Vec2 spawn = world.player().feet();
    const RunStats& stats = world.stats();
    Rect exit{};
    for (const auto& o : level.objects)
        if (o.type == "exit") exit = o.rect;
    int order = 0;
    std::string planetName;
    if (const PlanetData* p = content.planet(level.planet)) {
        planetName = p->name;
        auto it = std::find(p->levels.begin(), p->levels.end(), level.id);
        if (it != p->levels.end()) order = static_cast<int>(it - p->levels.begin()) + 1;
    }
    bool limited = level.timerMode == TimerMode::Limited || level.timerMode == TimerMode::Challenge;
    float limit = limited ? level.timeLimit : 3600.0f;
    return {
        {"id", level.id},
        {"name", level.name},
        {"planet_id", level.planet},
        {"planet_name", planetName},
        {"order", order},
        {"version", fp.version},
        {"content_hash", fp.hash},
        {"total_collectibles", stats.totalCollectibles()},
        {"total_enemies", stats.totalEnemies},
        // Time bonuses can extend the limit during a run: generous upper bound.
        {"time_limit_ms", static_cast<int64_t>(std::lround((limit * 1.5f + 60.0f) * 1000.0f))},
        // Below 35 % of the designer's reference time a run is considered impossible.
        {"min_time_ms", static_cast<int64_t>(std::max(10000.0, std::floor(level.referenceTime * 0.35 * 1000.0)))},
        {"bounds", {level.bounds.w, level.bounds.h}},
        {"spawn", {std::round(spawn.x), std::round(spawn.y)}},
        {"exit", {exit.x, exit.y, exit.w, exit.h}},
    };
}

Json buildOnlineManifest(ContentDatabase& content, std::string* error) {
    Json levels = Json::array();
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto fp = fingerprintLevelFile(e->path);
        std::string loadError;
        auto level = content.loadLevel(e->id, &loadError);
        if (!fp || !level) {
            if (error) *error = e->id + " : " + (loadError.empty() ? "empreinte impossible" : loadError);
            return Json();
        }
        levels.push_back(onlineManifestEntry(content, *level, *fp));
    }
    return Json{{"format", "cosmic.online_manifest"}, {"version", 1}, {"game_version", COSMIC_VERSION_STRING}, {"levels", levels}};
}

}  // namespace cosmic
