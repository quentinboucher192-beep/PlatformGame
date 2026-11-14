#include "game/ghost/SegmentHistory.h"

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/ghost/GhostRace.h"

namespace cosmic {

namespace {

bool validTimes(const std::vector<float>& v, float limit) {
    float prev = 0.0f;
    for (float s : v) {
        if (!std::isfinite(s) || s < prev || s > limit) return false;
        prev = s;
    }
    return true;
}

std::vector<float> readFloats(const Json& j, size_t max) {
    std::vector<float> out;
    if (!j.is_array()) return out;
    for (const Json& v : j) {
        if (!v.is_number() || out.size() >= max) break;
        out.push_back(v.get<float>());
    }
    return out;
}

}  // namespace

SegmentHistory::SegmentHistory(fs::path file) : file_(std::move(file)) {}

void SegmentHistory::load() {
    levels_.clear();
    if (file_.empty()) file_ = Paths::savesDir() / "segments.json";
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;  // no run finished yet
    auto j = FileUtil::loadJson(file_);
    if (!j || !j->is_object() || !j->contains("levels") || !(*j)["levels"].is_object()) return;
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (key.size() > 96 || !lj.is_object() || levels_.size() >= kLevelsKept) continue;
        Level l;
        l.touched = jstr(lj, "touched");
        l.bestSplitCount = static_cast<size_t>(std::clamp(jget<int>(lj, "best_split_count", 0), 0, 255));
        if (lj.contains("best")) l.best = readFloats(lj["best"], 256);
        for (float& b : l.best)
            if (!std::isfinite(b) || b < 0.0f || b > 7200.0f) b = -1.0f;
        if (lj.contains("runs") && lj["runs"].is_array()) {
            for (const Json& rj : lj["runs"]) {
                if (l.runs.size() >= kRunsKept) break;
                Run r;
                r.date = jstr(rj, "date").substr(0, 32);
                r.time = jget<float>(rj, "time", 0.0f);
                if (rj.contains("splits")) r.splits = readFloats(rj["splits"], 255);
                if (!std::isfinite(r.time) || r.time <= 0.0f || r.time > 7200.0f || !validTimes(r.splits, r.time)) continue;
                l.runs.push_back(std::move(r));
            }
        }
        levels_[key] = std::move(l);
    }
}

bool SegmentHistory::save() const {
    if (file_.empty()) return false;
    Json levels = Json::object();
    for (const auto& [key, l] : levels_) {
        Json runs = Json::array();
        for (const Run& r : l.runs) runs.push_back({{"date", r.date}, {"time", r.time}, {"splits", r.splits}});
        levels[key] = {{"touched", l.touched}, {"best", l.best}, {"best_split_count", l.bestSplitCount}, {"runs", runs}};
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    return FileUtil::writeTextAtomic(file_, Json{{"format", "cosmic.segments"}, {"version", 1}, {"levels", levels}}.dump(1));
}

void SegmentHistory::add(const std::string& levelKey, const std::vector<float>& splits, float time, const std::string& date) {
    if (levelKey.empty() || time <= 0.0f || !validTimes(splits, time)) return;
    Level& l = levels_[levelKey];
    l.runs.insert(l.runs.begin(), Run{date, splits, time});
    if (l.runs.size() > kRunsKept) l.runs.resize(kRunsKept);
    l.touched = date;
    std::vector<float> seg = segmentDurations(splits, time);
    if (l.best.empty() || l.bestSplitCount != splits.size()) {
        // Another route through the checkpoints: its segments are not comparable with the
        // previous best ones; the best are kept per route only when they match.
        if (l.best.empty() || l.bestSplitCount < splits.size()) {
            l.best = seg;
            l.bestSplitCount = splits.size();
        }
    } else {
        for (size_t i = 0; i < seg.size() && i < l.best.size(); ++i)
            if (l.best[i] < 0.0f || seg[i] < l.best[i]) l.best[i] = seg[i];
    }
    // Bounded file: the least recently played levels go first.
    while (levels_.size() > kLevelsKept) {
        auto oldest = std::min_element(levels_.begin(), levels_.end(), [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
        levels_.erase(oldest);
    }
}

const std::vector<SegmentHistory::Run>* SegmentHistory::runs(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() ? nullptr : &it->second.runs;
}

std::vector<float> SegmentHistory::bestSegments(const std::string& levelKey, size_t splitCount) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || it->second.bestSplitCount != splitCount) return std::vector<float>(splitCount + 1, -1.0f);
    std::vector<float> b = it->second.best;
    b.resize(splitCount + 1, -1.0f);
    return b;
}

}  // namespace cosmic
