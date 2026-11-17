#include "game/ghost/WorldRecordSplits.h"

#include <algorithm>

#include "game/ghost/ReplayCompare.h"

namespace cosmic {

std::vector<WorldRecordEntry> worldRecordCandidates(const Json& entries) {
    std::vector<std::pair<int, WorldRecordEntry>> ranked;
    if (!entries.is_array()) return {};
    int order = 0;
    for (const Json& e : entries) {
        ++order;
        if (!e.is_object() || !jget<bool>(e, "has_replay", false)) continue;  // a replay not verified (yet): never
        WorldRecordEntry w;
        w.recordId = jstr(e, "record_id");
        w.timeMs = jget<int64_t>(e, "time_ms", 0);
        w.player = jstr(jfield(e, "player", Json::object()), "username").substr(0, 64);
        if (w.recordId.empty() || w.recordId.size() > 96 || w.timeMs <= 0) continue;
        const int rank = jget<int>(e, "rank", 0);
        ranked.push_back({rank > 0 ? rank : 100000 + order, std::move(w)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<WorldRecordEntry> out;
    for (auto& [rank, w] : ranked) out.push_back(std::move(w));
    return out;
}

std::optional<std::vector<float>> worldRecordSplits(const ReplayData& replay, const std::string& levelHash) {
    if (levelHash.empty() || replay.levelHash != levelHash) return std::nullopt;
    std::vector<float> out;
    for (int64_t ms : replaySplitsMs(replay)) out.push_back(static_cast<float>(ms) / 1000.0f);
    return out;
}

std::optional<float> cumulativeGap(const std::vector<float>& mySplits, float myTime, const std::vector<float>& otherSplits, float otherTime, size_t i) {
    if (mySplits.size() != otherSplits.size()) return std::nullopt;
    auto at = [i](const std::vector<float>& s, float t) { return i < s.size() ? s[i] : i == s.size() && t > 0.0f ? t : -1.0f; };
    const float a = at(mySplits, myTime), b = at(otherSplits, otherTime);
    if (a < 0.0f || b < 0.0f) return std::nullopt;
    return a - b;
}

}  // namespace cosmic
