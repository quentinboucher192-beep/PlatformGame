#include "game/ghost/SegmentHistory.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/KeptReplays.h"

namespace cosmic {

namespace {

/// A gold replay matches its segment to the half millisecond (the chronometer's own values).
constexpr float kSameTime = 0.0005f;

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

/// Checkpoint ids as read back: as many as `count`, short, no control character; else none.
std::vector<std::string> readIds(const Json& j, size_t count) {
    std::vector<std::string> out;
    if (!j.is_array() || j.size() != count) return out;
    for (const Json& v : j) {
        if (!v.is_string()) return {};
        std::string s = v.get<std::string>();
        if (s.empty() || s.size() > 96) return {};
        for (unsigned char c : s)
            if (c < 0x20 || c == 0x7f) return {};
        out.push_back(std::move(s));
    }
    return out;
}

using keptreplays::fileSize;
using keptreplays::safeName;
bool validReplayName(const std::string& s) { return keptreplays::validName(s); }

/// Where segment `i` of a run starts and ends on its chronometer.
std::pair<float, float> segmentBounds(const std::vector<float>& splits, float time, size_t i) {
    float a = i == 0 ? 0.0f : splits[i - 1];
    float b = i < splits.size() ? splits[i] : time;
    return {a, b};
}

}  // namespace

SegmentHistory::SegmentHistory(fs::path file, fs::path replayDir) : file_(std::move(file)), replayDir_(std::move(replayDir)) {}

void SegmentHistory::load() {
    levels_.clear();
    if (file_.empty()) {
        file_ = Paths::savesDir() / "segments.json";
        if (replayDir_.empty()) replayDir_ = Paths::userDir() / "ghosts" / "golds";
    }
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
        if (lj.contains("best_checkpoints")) l.bestCheckpoints = readIds(lj["best_checkpoints"], l.bestSplitCount);
        // 2.60.3: the gold replays (an entry that does not describe its segment exactly is dropped).
        l.sources.assign(l.best.size(), GoldSource{});
        if (lj.contains("best_sources") && lj["best_sources"].is_array()) {
            const Json& sj = lj["best_sources"];
            for (size_t i = 0; i < sj.size() && i < l.best.size(); ++i) {
                const Json& e = sj[i];
                if (!e.is_object()) continue;
                GoldSource s;
                s.replay = jstr(e, "replay");
                s.date = jstr(e, "date").substr(0, 32);
                s.start = jget<float>(e, "start", -1.0f);
                s.end = jget<float>(e, "end", -1.0f);
                if (!validReplayName(s.replay) || !std::isfinite(s.start) || !std::isfinite(s.end) || s.start < 0.0f || s.end > 7200.0f ||
                    l.best[i] < 0.0f || std::abs((s.end - s.start) - l.best[i]) > kSameTime)
                    continue;
                l.sources[i] = std::move(s);
            }
        }
        if (lj.contains("runs") && lj["runs"].is_array()) {
            for (const Json& rj : lj["runs"]) {
                if (l.runs.size() >= kRunsKept) break;
                Run r;
                r.date = jstr(rj, "date").substr(0, 32);
                r.time = jget<float>(rj, "time", 0.0f);
                if (rj.contains("splits")) r.splits = readFloats(rj["splits"], 255);
                if (!std::isfinite(r.time) || r.time <= 0.0f || r.time > 7200.0f || !validTimes(r.splits, r.time)) continue;
                if (rj.contains("checkpoints")) r.checkpoints = readIds(rj["checkpoints"], r.splits.size());
                l.runs.push_back(std::move(r));
            }
        }
        levels_[key] = std::move(l);
    }
}

bool SegmentHistory::save() {
    if (file_.empty()) return false;
    Json levels = Json::object();
    for (const auto& [key, l] : levels_) {
        Json runs = Json::array();
        for (const Run& r : l.runs) {
            Json rj{{"date", r.date}, {"time", r.time}, {"splits", r.splits}};
            if (!r.checkpoints.empty()) rj["checkpoints"] = r.checkpoints;
            runs.push_back(std::move(rj));
        }
        levels[key] = {{"touched", l.touched}, {"best", l.best}, {"best_split_count", l.bestSplitCount}, {"runs", runs}};
        if (!l.bestCheckpoints.empty()) levels[key]["best_checkpoints"] = l.bestCheckpoints;
        if (std::any_of(l.sources.begin(), l.sources.end(), [](const GoldSource& s) { return s.has(); })) {
            Json sources = Json::array();
            for (const GoldSource& s : l.sources)
                sources.push_back(s.has() ? Json{{"replay", s.replay}, {"date", s.date}, {"start", s.start}, {"end", s.end}} : Json(nullptr));
            levels[key]["best_sources"] = std::move(sources);
        }
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    // Version 1 still: the checkpoints (2.30.2) and the gold replays (2.60.3) are optional
    // fields older games ignore.
    bool ok = FileUtil::writeTextAtomic(file_, Json{{"format", "cosmic.segments"}, {"version", 1}, {"levels", levels}}
                                                   .dump(1, ' ', false, Json::error_handler_t::replace));
    // Only once the history that refers to them is written: a replay is never deleted while
    // the file on disk still names it.
    if (ok) collectReplays();
    return ok;
}

fs::path SegmentHistory::levelDir(const std::string& levelKey) const { return keptreplays::levelDir(replayDir_, levelKey); }

fs::path SegmentHistory::replayPath(const std::string& levelKey, const std::string& replay) const {
    fs::path dir = levelDir(levelKey);
    if (dir.empty() || !validReplayName(replay)) return {};
    return dir / replay;
}

std::string SegmentHistory::writeReplay(const std::string& levelKey, const std::vector<uint8_t>& bytes, const std::string& date) const {
    return keptreplays::write(replayDir_, levelKey, bytes, date);
}

void SegmentHistory::add(const std::string& levelKey, const std::vector<float>& splits, float time, const std::string& date,
                         const std::vector<std::string>& checkpoints, const std::vector<uint8_t>* replay) {
    if (levelKey.empty() || time <= 0.0f || !validTimes(splits, time)) return;
    Level& l = levels_[levelKey];
    std::vector<std::string> ids = checkpoints.size() == splits.size() ? checkpoints : std::vector<std::string>{};
    l.runs.insert(l.runs.begin(), Run{date, splits, time, ids});
    if (l.runs.size() > kRunsKept) l.runs.resize(kRunsKept);
    l.touched = date;
    std::vector<float> seg = segmentDurations(splits, time);
    std::vector<size_t> golds;  // the segments this run now holds the best of
    if (l.best.empty() || l.bestSplitCount != splits.size()) {
        // Another route through the checkpoints: its segments are not comparable with the
        // previous best ones; the best are kept per route only when they match.
        if (l.best.empty() || l.bestSplitCount < splits.size()) {
            l.best = seg;
            l.bestSplitCount = splits.size();
            l.bestCheckpoints = ids;
            l.sources.assign(seg.size(), GoldSource{});
            for (size_t i = 0; i < seg.size(); ++i) golds.push_back(i);
        }
    } else {
        // Same number of splits: the same route (as the gold segments always assumed); its
        // checkpoints are noted when they were not known yet. The best values never change here.
        if (l.bestCheckpoints.empty()) l.bestCheckpoints = ids;
        l.sources.resize(l.best.size());
        for (size_t i = 0; i < seg.size() && i < l.best.size(); ++i)
            if (l.best[i] < 0.0f || seg[i] < l.best[i]) {
                l.best[i] = seg[i];
                golds.push_back(i);
            }
    }
    if (!golds.empty()) {
        // The run that set them: its replay is now where they come from (the previous one no
        // longer holds them, kept or not).
        std::string name = replay ? writeReplay(levelKey, *replay, date) : std::string{};
        for (size_t i : golds) {
            if (i >= l.sources.size()) continue;
            l.sources[i] = GoldSource{};
            if (name.empty()) continue;
            auto [a, b] = segmentBounds(splits, time, i);
            l.sources[i] = GoldSource{name, date, a, b};
        }
    }
    // Bounded file: the least recently played levels go first.
    while (levels_.size() > kLevelsKept) {
        auto oldest = std::min_element(levels_.begin(), levels_.end(), [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
        levels_.erase(oldest);
    }
}

bool SegmentHistory::adoptReplay(const std::string& levelKey, const fs::path& replayFile, const std::string& date,
                                 const std::vector<Adoption>& segments) {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return false;
    Level& l = it->second;
    l.sources.resize(l.best.size());
    // Only golds still without a replay, and only where the run did exactly their time.
    std::vector<Adoption> ok;
    for (const Adoption& a : segments)
        if (a.segment < l.best.size() && !l.sources[a.segment].has() && l.best[a.segment] >= 0.0f && std::isfinite(a.start) &&
            std::isfinite(a.end) && a.start >= 0.0f && std::abs((a.end - a.start) - l.best[a.segment]) <= kSameTime)
            ok.push_back(a);
    if (ok.empty()) return false;
    uint64_t n = fileSize(replayFile);
    if (n == 0 || n > kMaxReplayFileBytes) return false;
    auto bytes = FileUtil::readBinary(replayFile);
    if (!bytes) return false;
    std::string name = writeReplay(levelKey, *bytes, date);
    if (name.empty()) return false;
    for (const Adoption& a : ok) l.sources[a.segment] = GoldSource{name, date, a.start, a.end};
    return true;
}

void SegmentHistory::collectReplays() {
    if (replayDir_.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(replayDir_, ec)) return;
    // The files each level's golds refer to (by folder).
    std::map<std::string, std::set<std::string>> used;
    std::map<std::string, std::string> keyOf;  // folder → level key
    for (const auto& [key, l] : levels_) {
        std::string dir = safeName(key, 96);
        if (dir.empty()) continue;
        keyOf[dir] = key;
        for (const GoldSource& s : l.sources)
            if (s.has()) used[dir].insert(s.replay);
    }
    // Every file no gold refers to goes (and the folders of levels no longer kept).
    std::vector<std::pair<std::string, uint64_t>> sizes = keptreplays::collect(replayDir_, used);
    // Bounded: beyond kMaxReplayBytes, the least recently played levels lose their replays.
    uint64_t total = 0;
    for (const auto& s : sizes) total += s.second;
    if (total <= kMaxReplayBytes) return;
    auto touched = [&](const std::string& dir) {
        auto k = keyOf.find(dir);
        auto it = k == keyOf.end() ? levels_.end() : levels_.find(k->second);
        return it == levels_.end() ? std::string{} : it->second.touched;
    };
    std::sort(sizes.begin(), sizes.end(), [&](const auto& a, const auto& b) { return touched(a.first) < touched(b.first); });
    bool dropped = false;
    for (const auto& [dir, bytes] : sizes) {
        if (total <= kMaxReplayBytes) break;
        auto k = keyOf.find(dir);
        auto it = k == keyOf.end() ? levels_.end() : levels_.find(k->second);
        if (it != levels_.end())
            for (GoldSource& s : it->second.sources) s = GoldSource{};
        fs::remove_all(replayDir_ / Paths::fromUtf8(dir), ec);
        total -= std::min(total, bytes);
        dropped = true;
    }
    // The history must stop naming them (the next save would otherwise refer to nothing).
    if (dropped) save();
}

const std::vector<SegmentHistory::Run>* SegmentHistory::runs(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() ? nullptr : &it->second.runs;
}

std::vector<float> SegmentHistory::goldSegments(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || it->second.best.empty()) return {};
    std::vector<float> b = it->second.best;
    b.resize(it->second.bestSplitCount + 1, -1.0f);
    return b;
}

std::vector<std::string> SegmentHistory::goldCheckpoints(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() ? std::vector<std::string>{} : it->second.bestCheckpoints;
}

std::vector<SegmentHistory::GoldSource> SegmentHistory::goldSources(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || it->second.best.empty()) return {};
    std::vector<GoldSource> s = it->second.sources;
    s.resize(it->second.bestSplitCount + 1);
    if (replayDir_.empty())
        for (GoldSource& g : s) g = GoldSource{};  // replays not kept here
    return s;
}

std::vector<float> SegmentHistory::bestSegments(const std::string& levelKey, size_t splitCount) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end() || it->second.bestSplitCount != splitCount) return std::vector<float>(splitCount + 1, -1.0f);
    std::vector<float> b = it->second.best;
    b.resize(splitCount + 1, -1.0f);
    return b;
}

}  // namespace cosmic
