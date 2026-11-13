#include "game/records/Records.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/levels/LevelTimer.h"
#include "saves/SaveFile.h"

namespace cosmic {

Json RunResult::toJson() const {
    return {{"levelId", levelId},     {"completed", completed},   {"time", time},
            {"deaths", deaths},       {"collectibles", collectibles}, {"totalCollectibles", totalCollectibles},
            {"relics", relics},       {"secrets", secrets},       {"totalSecrets", totalSecrets},
            {"kills", kills},         {"score", score},           {"medal", static_cast<int>(medal)},
            {"powersUsed", powersUsed}, {"splits", splits},       {"date", date},
            {"player", player}};
}

RunResult RunResult::fromJson(const Json& j) {
    RunResult r;
    r.levelId = jstr(j, "levelId");
    r.completed = jget<bool>(j, "completed", false);
    r.time = jget<float>(j, "time", 0.0f);
    r.deaths = jget<int>(j, "deaths", 0);
    r.collectibles = jget<int>(j, "collectibles", 0);
    r.totalCollectibles = jget<int>(j, "totalCollectibles", 0);
    r.relics = jget<int>(j, "relics", 0);
    r.secrets = jget<int>(j, "secrets", 0);
    r.totalSecrets = jget<int>(j, "totalSecrets", 0);
    r.kills = jget<int>(j, "kills", 0);
    r.score = jget<int>(j, "score", 0);
    r.medal = static_cast<Medal>(std::clamp(jget<int>(j, "medal", 0), 0, 4));
    r.powersUsed = jget<std::vector<std::string>>(j, "powersUsed", {});
    r.splits = jget<std::vector<float>>(j, "splits", {});
    r.date = jstr(j, "date");
    r.player = jstr(j, "player", "Nova");
    return r;
}

Json LevelRecord::toJson() const {
    Json lb = Json::array();
    for (const auto& r : leaderboard) lb.push_back(r.toJson());
    return {{"levelId", levelId},
            {"attempts", attempts},
            {"completions", completions},
            {"bestTime", bestTime},
            {"bestTimeDate", bestTimeDate},
            {"bestSplits", bestSplits},
            {"bestTimePowers", bestTimePowers},
            {"firstCompletionTime", firstCompletionTime},
            {"bestScore", bestScore},
            {"fewestDeaths", fewestDeaths},
            {"mostCollectibles", mostCollectibles},
            {"totalCollectibles", totalCollectibles},
            {"mostKills", mostKills},
            {"mostSecrets", mostSecrets},
            {"bestMedal", static_cast<int>(bestMedal)},
            {"leaderboard", lb}};
}

LevelRecord LevelRecord::fromJson(const Json& j) {
    LevelRecord r;
    r.levelId = jstr(j, "levelId");
    r.attempts = jget<int>(j, "attempts", 0);
    r.completions = jget<int>(j, "completions", 0);
    r.bestTime = jget<float>(j, "bestTime", 0.0f);
    r.bestTimeDate = jstr(j, "bestTimeDate");
    r.bestSplits = jget<std::vector<float>>(j, "bestSplits", {});
    r.bestTimePowers = jget<std::vector<std::string>>(j, "bestTimePowers", {});
    r.firstCompletionTime = jget<float>(j, "firstCompletionTime", 0.0f);
    r.bestScore = jget<int>(j, "bestScore", 0);
    r.fewestDeaths = jget<int>(j, "fewestDeaths", -1);
    r.mostCollectibles = jget<int>(j, "mostCollectibles", 0);
    r.totalCollectibles = jget<int>(j, "totalCollectibles", 0);
    r.mostKills = jget<int>(j, "mostKills", 0);
    r.mostSecrets = jget<int>(j, "mostSecrets", 0);
    r.bestMedal = static_cast<Medal>(std::clamp(jget<int>(j, "bestMedal", 0), 0, 4));
    if (j.contains("leaderboard") && j["leaderboard"].is_array())
        for (const auto& e : j["leaderboard"]) r.leaderboard.push_back(RunResult::fromJson(e));
    return r;
}

// ------------------------------------------------------------- leaderboard

void LocalLeaderboard::submit(const RunResult& run) {
    if (!run.completed) return;
    auto& lb = owner_.getOrCreate(run.levelId).leaderboard;
    lb.push_back(run);
    std::stable_sort(lb.begin(), lb.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    if (lb.size() > RecordsManager::kLeaderboardSize) lb.resize(RecordsManager::kLeaderboardSize);
}

std::vector<LeaderboardEntry> LocalLeaderboard::top(const std::string& levelId, size_t count) const {
    std::vector<LeaderboardEntry> out;
    if (const LevelRecord* r = owner_.get(levelId)) {
        for (const auto& run : r->leaderboard) {
            if (out.size() >= count) break;
            out.push_back({run.player, run.time, run.score, run.medal, run.date});
        }
    }
    return out;
}

// ----------------------------------------------------------------- manager

RecordsManager::RecordsManager() { providers_.push_back(std::make_unique<LocalLeaderboard>(*this)); }

Json RecordsManager::migrateV1ToV2(const Json& v1) {
    Json out = Json::object();
    Json levels = Json::object();
    if (v1.contains("levels") && v1["levels"].is_object()) {
        for (auto& [id, r] : v1["levels"].items()) {
            Json nr = r;
            if (r.contains("bestTimeMs")) {
                nr["bestTime"] = r["bestTimeMs"].get<double>() / 1000.0;
                nr.erase("bestTimeMs");
            }
            nr["levelId"] = id;
            levels[id] = nr;
        }
    }
    out["levels"] = levels;
    return out;
}

namespace {
SaveFile recordsFile() { return SaveFile("records", RecordsManager::kVersion, {&RecordsManager::migrateV1ToV2}); }
fs::path recordsPath() { return Paths::savesDir() / "records.json"; }
}  // namespace

Json RecordsManager::toJson() const {
    Json levels = Json::object();
    for (const auto& [id, r] : records_) levels[id] = r.toJson();
    return {{"levels", levels}};
}

void RecordsManager::fromJson(const Json& j) {
    records_.clear();
    if (j.contains("levels") && j["levels"].is_object())
        for (auto& [id, r] : j["levels"].items()) {
            LevelRecord rec = LevelRecord::fromJson(r);
            rec.levelId = id;
            records_[id] = rec;
        }
}

void RecordsManager::load() {
    bool migrated = false;
    if (auto data = recordsFile().load(recordsPath(), &migrated)) {
        fromJson(*data);
        if (migrated) save();
    }
}

bool RecordsManager::save() const { return recordsFile().save(recordsPath(), toJson()); }

const LevelRecord* RecordsManager::get(const std::string& levelId) const {
    auto it = records_.find(levelId);
    return it == records_.end() ? nullptr : &it->second;
}

LevelRecord& RecordsManager::getOrCreate(const std::string& levelId) {
    LevelRecord& r = records_[levelId];
    r.levelId = levelId;
    return r;
}

void RecordsManager::registerAttempt(const std::string& levelId) { getOrCreate(levelId).attempts++; }

void RecordsManager::reset(const std::string& levelId) { records_.erase(levelId); }

RecordUpdate RecordsManager::submit(const RunResult& run) {
    RecordUpdate u;
    LevelRecord& r = getOrCreate(run.levelId);
    u.previousBestTime = r.bestTime;
    u.previousBestScore = r.bestScore;
    u.previousMedal = r.bestMedal;
    if (!run.completed) return u;
    if (r.completions == 0) {
        u.firstCompletion = true;
        r.firstCompletionTime = run.time;
    }
    r.completions++;
    if (r.bestTime <= 0.0f || run.time < r.bestTime) {
        u.newBestTime = true;
        r.bestTime = run.time;
        r.bestTimeDate = run.date;
        r.bestSplits = run.splits;
        r.bestTimePowers = run.powersUsed;
    }
    if (run.score > r.bestScore) {
        u.newBestScore = true;
        r.bestScore = run.score;
    }
    if (r.fewestDeaths < 0 || run.deaths < r.fewestDeaths) r.fewestDeaths = run.deaths;
    r.mostCollectibles = std::max(r.mostCollectibles, run.collectibles);
    r.totalCollectibles = run.totalCollectibles;
    r.mostKills = std::max(r.mostKills, run.kills);
    r.mostSecrets = std::max(r.mostSecrets, run.secrets);
    if (static_cast<int>(run.medal) > static_cast<int>(r.bestMedal)) {
        u.newMedal = true;
        r.bestMedal = run.medal;
    }
    for (auto& p : providers_) p->submit(run);
    for (size_t i = 0; i < r.leaderboard.size(); ++i)
        if (r.leaderboard[i].time == run.time && r.leaderboard[i].date == run.date) u.leaderboardRank = static_cast<int>(i);
    return u;
}

Medal RecordsManager::evaluateMedal(const LevelData& level, const RunResult& run) {
    if (!run.completed) return Medal::None;
    int pct = run.totalCollectibles > 0 ? run.collectibles * 100 / run.totalCollectibles : 100;
    Medal best = Medal::None;
    for (int i = 0; i < 4; ++i) {
        const MedalRule& m = level.medals[i];
        bool ok = true;
        if (m.maxTime > 0.0f && run.time > m.maxTime) ok = false;
        if (m.maxDeaths >= 0 && run.deaths > m.maxDeaths) ok = false;
        if (pct < m.minCollectiblesPercent) ok = false;
        if (ok) best = static_cast<Medal>(i + 1);
    }
    return best;
}

int RecordsManager::computeScore(const LevelData& level, const RunStats& stats, float time, bool completed) {
    int score = completed ? 5000 : 0;
    score += stats.scoreBonus;
    score += stats.secrets * 500;
    if (completed) score += std::max(0, static_cast<int>((level.referenceTime * 2.0f - time) * 25.0f));
    score -= stats.deaths * 250;
    return std::max(0, score);
}

std::string RecordsManager::medalCondition(const LevelData& level, Medal m) {
    if (m == Medal::None) return "";
    const MedalRule& r = level.medals[static_cast<int>(m) - 1];
    std::string s;
    if (r.maxTime > 0) s += "< " + LevelTimer::format(r.maxTime, false);
    if (r.maxDeaths >= 0) s += std::string(s.empty() ? "" : ", ") + std::to_string(r.maxDeaths) + (r.maxDeaths <= 1 ? " mort" : " morts");
    if (r.minCollectiblesPercent > 0)
        s += std::string(s.empty() ? "" : ", ") + std::to_string(r.minCollectiblesPercent) + "% objets";
    if (s.empty()) s = "Terminer le niveau";
    return s;
}

}  // namespace cosmic
