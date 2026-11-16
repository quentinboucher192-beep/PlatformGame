#include "game/online/ProgressMerge.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "game/records/BossStats.h"
#include "game/records/Records.h"

namespace cosmic {

namespace {

const Json& objectOr(const Json& j, const char* key) {
    static const Json empty = Json::object();
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : empty;
}

double num(const Json& j, const char* key) {
    if (!j.is_object()) return 0.0;
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

/// The union of two arrays of strings (sorted, each once).
Json unionStrings(const Json& a, const Json& b) {
    std::set<std::string> out;
    for (const Json* arr : {&a, &b})
        if (arr->is_array())
            for (const auto& v : *arr)
                if (v.is_string()) out.insert(v.get<std::string>());
    return Json(std::vector<std::string>(out.begin(), out.end()));
}

/// Numbers of two objects summed key by key (`max` keeps the highest instead).
Json mergeNumbers(const Json& a, const Json& b, bool max) {
    Json out = a.is_object() ? a : Json::object();
    if (!b.is_object()) return out;
    for (const auto& [k, v] : b.items()) {
        if (!v.is_number()) continue;
        if (!out.contains(k) || !out[k].is_number()) {
            out[k] = v;
            continue;
        }
        if (v.is_number_integer() && out[k].is_number_integer()) {
            const int64_t x = out[k].get<int64_t>(), y = v.get<int64_t>();
            out[k] = max ? std::max(x, y) : x + y;
        } else {
            const double x = out[k].get<double>(), y = v.get<double>();
            out[k] = max ? std::max(x, y) : x + y;
        }
    }
    return out;
}

bool better(const RunResult& a, const RunResult& b) {
    if (a.completed != b.completed) return a.completed;
    if (a.completed && a.time != b.time) return a.time < b.time;
    return a.score > b.score;
}

}  // namespace

Json mergeProgressJson(const Json& mine, const Json& theirs) {
    Json out = mine.is_object() ? mine : Json::object();
    if (!theirs.is_object()) return out;
    out["completedLevels"] = unionStrings(mine.value("completedLevels", Json::array()), theirs.value("completedLevels", Json::array()));
    out["playTimeSeconds"] = num(mine, "playTimeSeconds") + num(theirs, "playTimeSeconds");
    out["levelPlayTime"] = mergeNumbers(objectOr(mine, "levelPlayTime"), objectOr(theirs, "levelPlayTime"), false);
    out["introSeen"] = jget<bool>(mine, "introSeen", false) || jget<bool>(theirs, "introSeen", false);
    if (!out.contains("lastPlanet")) out["lastPlanet"] = theirs.value("lastPlanet", Json("sylvanox"));
    if (!out.contains("lastLevel")) out["lastLevel"] = theirs.value("lastLevel", Json(""));
    return out;
}

Json mergeRecordsJson(const Json& mine, const Json& theirs) {
    Json levels = objectOr(mine, "levels");
    for (const auto& [id, t] : objectOr(theirs, "levels").items()) {
        if (!levels.contains(id)) {
            levels[id] = t;
            continue;
        }
        LevelRecord a = LevelRecord::fromJson(levels[id]);
        const LevelRecord b = LevelRecord::fromJson(t);
        a.attempts += b.attempts;
        a.completions += b.completions;
        // The best time with what goes with it, only against a time of the same level version.
        const bool sameVersion = a.levelHash.empty() || b.levelHash.empty() || a.levelHash == b.levelHash;
        if (b.bestTime > 0.0f && (a.bestTime <= 0.0f || (sameVersion && b.bestTime < a.bestTime))) {
            a.bestTime = b.bestTime;
            a.bestTimeDate = b.bestTimeDate;
            a.bestSplits = b.bestSplits;
            a.bestTimePowers = b.bestTimePowers;
            a.levelHash = b.levelHash;
        }
        if (a.firstCompletionTime <= 0.0f) a.firstCompletionTime = b.firstCompletionTime;
        a.bestScore = std::max(a.bestScore, b.bestScore);
        if (b.fewestDeaths >= 0 && (a.fewestDeaths < 0 || b.fewestDeaths < a.fewestDeaths)) a.fewestDeaths = b.fewestDeaths;
        a.mostCollectibles = std::max(a.mostCollectibles, b.mostCollectibles);
        a.totalCollectibles = std::max(a.totalCollectibles, b.totalCollectibles);
        a.mostKills = std::max(a.mostKills, b.mostKills);
        a.mostSecrets = std::max(a.mostSecrets, b.mostSecrets);
        a.bestMedal = std::max(a.bestMedal, b.bestMedal);
        // The local top runs: both lists, the same run once, by time.
        std::vector<RunResult> runs = a.leaderboard;
        for (const RunResult& r : b.leaderboard) {
            const bool same = std::any_of(runs.begin(), runs.end(), [&](const RunResult& x) {
                return x.date == r.date && x.time == r.time && x.score == r.score;
            });
            if (!same) runs.push_back(r);
        }
        std::stable_sort(runs.begin(), runs.end(), better);
        if (runs.size() > RecordsManager::kLeaderboardSize) runs.resize(RecordsManager::kLeaderboardSize);
        a.leaderboard = runs;
        levels[id] = a.toJson();
    }
    Json out = mine.is_object() ? mine : Json::object();
    out["levels"] = levels;
    return out;
}

Json mergeBossStatsJson(const Json& mine, const Json& theirs) {
    Json bosses = objectOr(mine, "bosses");
    for (const auto& [id, t] : objectOr(theirs, "bosses").items()) {
        if (!bosses.contains(id)) {
            bosses[id] = t;
            continue;
        }
        BossRecord a = BossRecord::fromJson(bosses[id]);
        const BossRecord b = BossRecord::fromJson(t);
        a.fights += b.fights;
        a.kills += b.kills;
        a.defeats += b.defeats;
        a.noHitKills += b.noHitKills;
        if (b.phaseNames.size() > a.phaseNames.size()) a.phaseNames = b.phaseNames;
        auto sum = [](std::vector<int>& x, const std::vector<int>& y) {
            if (y.size() > x.size()) x.resize(y.size(), 0);
            for (size_t i = 0; i < y.size(); ++i) x[i] += y[i];
        };
        sum(a.phaseClears, b.phaseClears);
        sum(a.phaseNoHit, b.phaseNoHit);
        if (b.bestKill > 0.0f && (a.bestKill <= 0.0f || b.bestKill < a.bestKill)) a.bestKill = b.bestKill;
        if (b.lastKill > a.lastKill) a.lastKill = b.lastKill;  // « YYYY-MM-DD … »: in order as text
        if (a.name.empty()) a.name = b.name;
        bosses[id] = a.toJson();
    }
    Json out = mine.is_object() ? mine : Json::object();
    out["bosses"] = bosses;
    return out;
}

Json mergeAchievementsJson(const Json& mine, const Json& theirs) {
    Json out = mine.is_object() ? mine : Json{{"format", "cosmic.achievement_book"}};
    if (!theirs.is_object()) return out;
    Json unlocked = objectOr(mine, "unlocked");
    for (const auto& [id, date] : objectOr(theirs, "unlocked").items()) {
        if (!unlocked.contains(id)) {
            unlocked[id] = date;
            continue;
        }
        // The earliest date (an empty one: unknown, the other kept).
        const std::string a = unlocked[id].is_string() ? unlocked[id].get<std::string>() : std::string();
        const std::string b = date.is_string() ? date.get<std::string>() : std::string();
        if (a.empty() || (!b.empty() && b < a)) unlocked[id] = b;
    }
    out["unlocked"] = unlocked;
    out["counters"] = mergeNumbers(objectOr(mine, "counters"), objectOr(theirs, "counters"), false);
    out["power_uses"] = mergeNumbers(objectOr(mine, "power_uses"), objectOr(theirs, "power_uses"), false);
    out["maxima"] = mergeNumbers(objectOr(mine, "maxima"), objectOr(theirs, "maxima"), true);
    Json sets = objectOr(mine, "sets");
    for (const auto& [k, v] : objectOr(theirs, "sets").items()) sets[k] = unionStrings(sets.value(k, Json::array()), v);
    out["sets"] = sets;
    return out;
}

Json mergeCollectionJson(const Json& mine, const Json& theirs) {
    Json out = mine.is_object() ? mine : Json::object();
    if (!theirs.is_object()) return out;
    for (const char* key : {"heard", "met", "announced"})
        out[key] = unionStrings(mine.value(key, Json::array()), theirs.value(key, Json::array()));
    out["exported"] = static_cast<int64_t>(num(mine, "exported") + num(theirs, "exported"));
    return out;
}

}  // namespace cosmic
