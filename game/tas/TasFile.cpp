#include "game/tas/TasFile.h"

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"

namespace cosmic {

namespace {

bool hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

/// A file name alone (no folder, no "..", no drive): the replay sits next to its JSON.
bool plainFileName(const std::string& s) {
    if (s.empty() || s == "." || s == ".." || !Paths::isSafeRelative(s)) return false;
    return s.find('/') == std::string::npos && s.find('\\') == std::string::npos && s.find(':') == std::string::npos;
}

float tenth(float v) { return std::round(v * 10.0f) / 10.0f; }

}  // namespace

Json TasRecord::toJson() const {
    Json sp = Json::array();
    for (const TasSplit& s : splits) sp.push_back({{"checkpoint", s.checkpoint}, {"timeMs", s.timeMs}});
    Json rt = Json::array();
    for (Vec2 p : route) rt.push_back(Json::array({tenth(p.x), tenth(p.y)}));
    return {{"format", kTasFormat},
            {"level", levelId},
            {"levelVersion", levelVersion},
            {"levelHash", levelHash},
            {"simVersion", simVersion},
            {"category", categoryId(category)},
            {"timeMs", timeMs},
            {"splits", sp},
            {"explored", explored},
            {"computedAt", computedAt},
            {"tool", tool},
            {"routeIntervalMs", routeIntervalMs},
            {"route", rt},
            {"deaths", deaths},
            {"damageTaken", damageTaken},
            {"collectibles", collectibles},
            {"totalCollectibles", totalCollectibles},
            {"replay", replay}};
}

std::optional<TasRecord> TasRecord::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<TasRecord> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("pas un objet JSON");
    if (jstr(j, "format") != kTasFormat) return fail("format inconnu");
    TasRecord r;
    r.levelId = jstr(j, "level");
    if (r.levelId.empty()) return fail("niveau absent");
    r.levelVersion = std::max(1, jget<int>(j, "levelVersion", 1));
    r.levelHash = jstr(j, "levelHash");
    if (!hex64(r.levelHash)) return fail("empreinte du niveau invalide");
    r.simVersion = jget<int>(j, "simVersion", 0);
    if (r.simVersion <= 0) return fail("version de la simulation absente");
    if (!categoryFromId(jstr(j, "category"), r.category)) return fail("catégorie inconnue");
    r.timeMs = jget<int64_t>(j, "timeMs", 0);
    if (r.timeMs <= 0) return fail("temps absent");
    for (const Json& s : jfield(j, "splits", Json::array())) {
        if (!s.is_object()) continue;
        r.splits.push_back({jstr(s, "checkpoint"), jget<int64_t>(s, "timeMs", 0)});
    }
    r.explored = std::max<int64_t>(0, jget<int64_t>(j, "explored", 0));
    r.computedAt = jstr(j, "computedAt");
    r.tool = jstr(j, "tool");
    r.routeIntervalMs = jget<int>(j, "routeIntervalMs", kTasRouteIntervalMs);
    if (r.routeIntervalMs <= 0) r.routeIntervalMs = kTasRouteIntervalMs;
    for (const Json& p : jfield(j, "route", Json::array()))
        if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number()) r.route.push_back({p[0].get<float>(), p[1].get<float>()});
    r.deaths = std::max(0, jget<int>(j, "deaths", 0));
    r.damageTaken = std::max(0, jget<int>(j, "damageTaken", 0));
    r.collectibles = std::max(0, jget<int>(j, "collectibles", 0));
    r.totalCollectibles = std::max(0, jget<int>(j, "totalCollectibles", 0));
    r.replay = jstr(j, "replay");
    if (!plainFileName(r.replay)) return fail("nom du replay invalide");
    return r;
}

fs::path tasRoot() { return Paths::dataDir() / "tas"; }

fs::path tasFilePath(const std::string& levelId, RunCategory c, const fs::path& root) {
    return root / Paths::fromUtf8(levelId) / (std::string(categoryId(c)) + ".json");
}

std::string tasReplayFileName(RunCategory c) { return std::string(categoryId(c)) + ".cprz"; }

fs::path tasReplayPath(const TasRecord& r, const fs::path& root) { return root / Paths::fromUtf8(r.levelId) / Paths::fromUtf8(r.replay); }

std::optional<TasRecord> readTasFile(const fs::path& file, std::string* error) {
    auto text = FileUtil::readText(file);
    if (!text) {
        if (error) *error = "fichier absent";
        return std::nullopt;
    }
    Json j = Json::parse(*text, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "JSON invalide";
        return std::nullopt;
    }
    return TasRecord::fromJson(j, error);
}

bool writeTasFile(const fs::path& file, const TasRecord& r) { return FileUtil::writeTextAtomic(file, r.toJson().dump(1) + "\n"); }

std::optional<TasRecord> tasFor(const std::string& levelId, RunCategory c, const std::string& levelHash, const fs::path& root) {
    if (levelId.empty() || !Paths::isSafeRelative(levelId) || levelHash.empty()) return std::nullopt;
    auto r = readTasFile(tasFilePath(levelId, c, root));
    if (!r || r->levelId != levelId || r->category != c) return std::nullopt;
    // The user's rule: computed for another version of the level or of the rules, it is hidden.
    if (r->levelHash != levelHash || r->simVersion != kSimulationVersion) return std::nullopt;
    std::error_code ec;
    if (!fs::is_regular_file(tasReplayPath(*r, root), ec)) return std::nullopt;
    return r;
}

std::optional<TasRecord> tasFor(const ContentDatabase& content, LevelFingerprints& fingerprints, const std::string& levelId, RunCategory c,
                                const fs::path& root) {
    const LevelFingerprint* fp = fingerprints.get(content, levelId);
    if (!fp) return std::nullopt;
    return tasFor(levelId, c, fp->hash, root);
}

std::vector<TasRecord> tasForLevel(const ContentDatabase& content, LevelFingerprints& fingerprints, const std::string& levelId,
                                   const fs::path& root) {
    std::vector<TasRecord> out;
    const LevelFingerprint* fp = fingerprints.get(content, levelId);
    if (!fp) return out;
    for (RunCategory c : kAllCategories)
        if (auto r = tasFor(levelId, c, fp->hash, root)) out.push_back(std::move(*r));
    return out;
}

std::optional<ReplayData> loadTasReplay(const TasRecord& r, const fs::path& root, std::string* error) {
    auto bytes = FileUtil::readBinary(tasReplayPath(r, root));
    if (!bytes) {
        if (error) *error = "replay absent";
        return std::nullopt;
    }
    auto replay = ReplayData::decode(*bytes, error);
    if (!replay) return std::nullopt;
    if (!replay->exact() || replay->levelId != r.levelId || replay->levelHash != r.levelHash || replay->simVersion != r.simVersion) {
        if (error) *error = "replay d'une autre version du niveau";
        return std::nullopt;
    }
    return replay;
}

const char* tasTimeLabel() { return "Meilleur temps trouvé par la machine"; }

}  // namespace cosmic
