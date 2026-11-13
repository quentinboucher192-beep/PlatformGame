#include "game/online/OnlineRecord.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "saves/SaveFile.h"

namespace cosmic {

// ------------------------------------------------------------------ attempt

Json OnlineAttempt::toJson() const {
    Json subs = Json::array();
    for (const auto& s : submissions)
        subs.push_back({{"category", categoryId(s.category)},
                        {"status", recordStatusId(s.status)},
                        {"record_id", s.recordId},
                        {"note", s.note},
                        {"blocked", s.blocked},
                        {"rank", s.rank},
                        {"previous_rank", s.previousRank}});
    return {{"attempt_id", attemptId},   {"level_id", levelId},
            {"level_version", levelVersion}, {"level_hash", levelHash},
            {"game_version", gameVersion}, {"time_ms", timeMs},
            {"deaths", deaths},           {"damage_taken", damageTaken},
            {"collectibles", collectibles}, {"total_collectibles", totalCollectibles},
            {"kills", kills},             {"score", score},
            {"created_at", createdAt},    {"owner_id", ownerId},
            {"has_replay", hasReplay},    {"replay_uploaded", replayUploaded},
            {"submissions", subs}};
}

std::optional<OnlineAttempt> OnlineAttempt::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    OnlineAttempt a;
    a.attemptId = jstr(j, "attempt_id");
    a.levelId = jstr(j, "level_id");
    a.levelHash = jstr(j, "level_hash");
    if (a.attemptId.size() != 36 || a.levelId.empty() || a.levelHash.size() != 64) return std::nullopt;
    a.levelVersion = jget<int>(j, "level_version", 1);
    a.gameVersion = jstr(j, "game_version", COSMIC_VERSION_STRING);
    a.timeMs = jget<int64_t>(j, "time_ms", 0);
    a.deaths = jget<int>(j, "deaths", 0);
    a.damageTaken = jget<int>(j, "damage_taken", 0);
    a.collectibles = jget<int>(j, "collectibles", 0);
    a.totalCollectibles = jget<int>(j, "total_collectibles", 0);
    a.kills = jget<int>(j, "kills", 0);
    a.score = jget<int>(j, "score", 0);
    a.createdAt = jstr(j, "created_at");
    a.ownerId = jstr(j, "owner_id");
    a.hasReplay = jget<bool>(j, "has_replay", false);
    a.replayUploaded = jget<bool>(j, "replay_uploaded", false);
    if (j.contains("submissions") && j["submissions"].is_array()) {
        for (const auto& s : j["submissions"]) {
            OnlineSubmission sub;
            if (!categoryFromId(jstr(s, "category"), sub.category)) continue;
            std::string st = jstr(s, "status", "LOCAL");
            sub.status = st == "LOCAL" ? RecordStatus::Local : recordStatusFromServer(st);
            sub.recordId = jstr(s, "record_id");
            sub.note = jstr(s, "note");
            sub.blocked = jget<bool>(s, "blocked", false);
            sub.rank = jget<int>(s, "rank", 0);
            sub.previousRank = jget<int>(s, "previous_rank", 0);
            a.submissions.push_back(sub);
        }
    }
    return a;
}

Json OnlineAttempt::submitBody(RunCategory c) const {
    return {{"level_id", levelId},
            {"level_version", levelVersion},
            {"level_hash", levelHash},
            {"mode", "STANDARD"},
            {"category", categoryId(c)},
            {"time_ms", timeMs},
            {"deaths", deaths},
            {"damage_taken", damageTaken},
            {"collectibles", collectibles},
            {"total_collectibles", totalCollectibles},
            {"kills", kills},
            {"score", score},
            {"game_version", gameVersion},
            {"attempt_id", attemptId},
            {"has_replay", hasReplay}};
}

OnlineSubmission* OnlineAttempt::submission(RunCategory c) {
    for (auto& s : submissions)
        if (s.category == c) return &s;
    return nullptr;
}

const OnlineSubmission* OnlineAttempt::submission(RunCategory c) const {
    for (const auto& s : submissions)
        if (s.category == c) return &s;
    return nullptr;
}

bool OnlineAttempt::needsSync() const {
    for (const auto& s : submissions)
        if (!s.blocked && (s.status == RecordStatus::Local || s.status == RecordStatus::Pending)) return true;
    return hasReplay && !replayUploaded &&
           std::any_of(submissions.begin(), submissions.end(), [](const OnlineSubmission& s) { return !s.recordId.empty(); });
}

std::vector<RunCategory> eligibleCategories(const RunCandidate& run) {
    std::vector<RunCategory> out = {RunCategory::AnyPercent};
    if (run.totalCollectibles > 0 && run.collectibles >= run.totalCollectibles) out.push_back(RunCategory::HundredPercent);
    if (run.deaths == 0) out.push_back(RunCategory::NoDeath);
    if (run.deaths == 0 && run.damageTaken == 0) out.push_back(RunCategory::NoDamage);
    // SPEEDRUN: an uninterrupted run from the start of the level with a replay.
    if (run.fromStart && run.hasReplay) out.push_back(RunCategory::Speedrun);
    return out;
}

// -------------------------------------------------------------------- store

namespace {
SaveFile attemptsFile() { return SaveFile("online_attempts", AttemptStore::kVersion); }
}  // namespace

void AttemptStore::load() {
    attempts_.clear();
    if (file_.empty()) return;
    auto data = attemptsFile().load(file_);
    if (!data || !data->contains("attempts") || !(*data)["attempts"].is_array()) return;
    for (const auto& j : (*data)["attempts"])
        if (auto a = OnlineAttempt::fromJson(j)) attempts_.push_back(std::move(*a));
}

bool AttemptStore::save() const {
    if (file_.empty()) return true;
    Json arr = Json::array();
    for (const auto& a : attempts_) arr.push_back(a.toJson());
    return attemptsFile().save(file_, {{"attempts", arr}});
}

std::optional<int64_t> AttemptStore::bestTime(const std::string& levelHash, RunCategory c) const {
    std::optional<int64_t> best;
    for (const auto& a : attempts_) {
        if (a.levelHash != levelHash) continue;
        const OnlineSubmission* s = a.submission(c);
        if (!s || s->status == RecordStatus::Rejected) continue;
        if (!best || a.timeMs < *best) best = a.timeMs;
    }
    return best;
}

OnlineAttempt* AttemptStore::add(const RunCandidate& run, const std::string& attemptId, const std::string& ownerId) {
    OnlineAttempt a;
    a.attemptId = attemptId;
    a.levelId = run.levelId;
    a.levelVersion = run.levelVersion;
    a.levelHash = run.levelHash;
    a.gameVersion = COSMIC_VERSION_STRING;
    a.timeMs = run.timeMs;
    a.deaths = run.deaths;
    a.damageTaken = run.damageTaken;
    a.collectibles = run.collectibles;
    a.totalCollectibles = run.totalCollectibles;
    a.kills = run.kills;
    a.score = run.score;
    a.createdAt = FileUtil::nowString();
    a.ownerId = ownerId;
    a.hasReplay = run.hasReplay;
    for (RunCategory c : eligibleCategories(run)) {
        auto best = bestTime(run.levelHash, c);
        if (!best || run.timeMs < *best) a.submissions.push_back({c});
    }
    if (a.submissions.empty()) return nullptr;
    attempts_.push_back(std::move(a));
    trim();
    return find(attemptId);
}

OnlineAttempt* AttemptStore::find(const std::string& attemptId) {
    for (auto& a : attempts_)
        if (a.attemptId == attemptId) return &a;
    return nullptr;
}

int AttemptStore::unsentCount(const std::string& ownerId) const {
    int n = 0;
    for (const auto& a : attempts_) {
        if (a.ownerId != ownerId) continue;
        for (const auto& s : a.submissions)
            if (s.status == RecordStatus::Local && !s.blocked) ++n;
    }
    return n;
}

fs::path AttemptStore::replayPath(const std::string& attemptId) const { return replayDir_ / (attemptId + ".cprz"); }

void AttemptStore::removeOwner(const std::string& ownerId) {
    std::error_code ec;
    for (const auto& a : attempts_)
        if (a.ownerId == ownerId && !replayDir_.empty()) fs::remove(replayPath(a.attemptId), ec);
    attempts_.erase(std::remove_if(attempts_.begin(), attempts_.end(), [&](const OnlineAttempt& a) { return a.ownerId == ownerId; }),
                    attempts_.end());
}

void AttemptStore::trim() {
    // Keep the most recent attempts; settled ones (no pending work) go first.
    while (attempts_.size() > kMaxAttempts) {
        auto it = std::find_if(attempts_.begin(), attempts_.end(), [](const OnlineAttempt& a) { return !a.needsSync(); });
        if (it == attempts_.end()) it = attempts_.begin();
        std::error_code ec;
        if (!replayDir_.empty()) fs::remove(replayPath(it->attemptId), ec);
        attempts_.erase(it);
    }
}

}  // namespace cosmic
