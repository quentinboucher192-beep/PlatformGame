#include "game/online/LevelFingerprint.h"

#include <algorithm>
#include <cmath>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "packaging/Package.h"
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

std::optional<LevelFingerprint> fingerprintRankedLevel(const ContentDatabase& content, const Json& levelJson) {
    auto base = fingerprintLevelJson(levelJson);
    if (!base) return std::nullopt;
    auto level = LevelData::fromJson(levelJson);
    if (!level) return std::nullopt;
    ResourceManager res;
    res.init(nullptr, nullptr);
    DependencySet deps = DependencyCollector(content, res).collect(*level);
    Json enemies = Json::object(), powers = Json::object();
    for (const auto& id : deps.enemies)
        if (const EnemyDefinition* d = content.enemy(id)) enemies[id] = d->toJson();
    for (const auto& id : deps.powers)
        if (const PowerDefinition* d = content.power(id)) powers[id] = d->toJson();
    Json bundle = {{"format", "cosmic.ranked-level"}, {"simulation", kSimulationVersion}, {"level", levelJson},
                   {"enemies", enemies},          {"powers", powers},                  {"player", content.playerConfig()}};
    base->hash = Sha256::hex(bundle.dump());
    return base;
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
        if (e && e->source == ContentSource::Builtin)
            if (auto text = FileUtil::readText(e->path)) {
                Json j = Json::parse(*text, nullptr, false);
                if (!j.is_discarded()) fp = fingerprintRankedLevel(content, j);
            }
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
        std::optional<LevelFingerprint> fp;
        if (auto text = FileUtil::readText(e->path)) {
            Json j = Json::parse(*text, nullptr, false);
            if (!j.is_discarded()) fp = fingerprintRankedLevel(content, j);
        }
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

namespace cosmic {

std::string fingerprintGameplay(const ContentDatabase& content, const LevelData& level) {
    Json enemies = Json::object(), powers = Json::object();
    auto defKey = [](Json j) {
        j.erase("id");
        j.erase("name");
        j.erase("description");
        return std::make_pair(Sha256::hex(j.dump()).substr(0, 24), j);
    };
    auto enemyKey = [&](const std::string& id) -> std::string {
        const EnemyDefinition* d = content.enemy(id);
        if (!d) return "missing:" + id;
        auto [k, j] = defKey(d->toJson());
        enemies[k] = j;
        return k;
    };
    auto powerKey = [&](const std::string& id) -> std::string {
        const PowerDefinition* d = content.power(id);
        if (!d) return "missing:" + id;
        auto [k, j] = defKey(d->toJson());
        powers[k] = j;
        return k;
    };
    Json objects = Json::array();
    for (const auto& o : level.objects) {
        Json oj = o.toJson();
        if (o.type == "enemy") oj["props"]["enemy"] = enemyKey(o.propStr("enemy"));
        if (o.type == "power_unlock" || o.type == "power_gate") oj["props"]["power"] = powerKey(o.propStr("power"));
        if (o.type == "combat_zone") oj["props"].erase("music");
        objects.push_back(oj);
    }
    Json events = Json::array();
    for (const auto& e : level.events) {
        Json ej = e.toJson();
        if (content.power(e.source)) ej["source"] = powerKey(e.source);
        for (auto& a : ej["actions"])
            if (a.contains("params") && a["params"].is_object() && a["params"].contains("power") && a["params"]["power"].is_string())
                a["params"]["power"] = powerKey(a["params"]["power"].get<std::string>());
        events.push_back(ej);
    }
    Json start = Json::array();
    for (const auto& p : level.startPowers) start.push_back(powerKey(p));
    Json full = level.toJson();
    Json bundle = {{"format", "cosmic.gameplay-level"},
                   {"simulation", kSimulationVersion},
                   {"level",
                    {{"bounds", full.value("bounds", Json())},
                     {"killY", full.value("killY", Json())},
                     {"timer", full.value("timer", Json())},
                     {"startPowers", start},
                     {"powerSlots", level.powerSlots()},
                     {"objects", objects},
                     {"events", events},
                     {"intro", full.value("intro", Json())},
                     {"outro", full.value("outro", Json())}}},
                   {"enemies", enemies},
                   {"powers", powers},
                   {"player", content.playerConfig()}};
    return Sha256::hex(bundle.dump());
}

}  // namespace cosmic
