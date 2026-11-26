#include "game/records/PlayStatsViews.h"

#include <algorithm>

namespace cosmic::playstats {

int levelLastDay(const PlayStatsData& d, const std::string& levelId) {
    int day = -1;
    if (auto it = d.levels.find(levelId); it != d.levels.end() && it->second.any()) day = std::max(day, it->second.lastDay);
    if (auto it = d.runs.find(levelId); it != d.runs.end() && (it->second.attempts > 0 || !it->second.segments.empty()))
        day = std::max(day, it->second.lastDay);
    return day;
}

int bossLastDay(const BossHistory& b) {
    for (auto it = b.recent.rbegin(); it != b.recent.rend(); ++it)
        if (it->day > 0) return it->day;
    return 0;
}

std::vector<RecentPlay> recentlyPlayed(const PlayStatsData& d, size_t count) {
    std::vector<RecentPlay> all;
    std::vector<std::string> ids;
    for (const auto& [id, l] : d.levels) ids.push_back(id);
    for (const auto& [id, r] : d.runs) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (const auto& id : ids)
        if (int day = levelLastDay(d, id); day >= 0) all.push_back({RecentPlay::Kind::Level, id, day, 0});
    for (const auto& [id, b] : d.bosses)
        if (b.attempts > 0 || !b.recent.empty()) all.push_back({RecentPlay::Kind::Boss, id, bossLastDay(b), b.lastSeq});
    std::stable_sort(all.begin(), all.end(), [](const RecentPlay& a, const RecentPlay& b) {
        const bool ka = a.day > 0, kb = b.day > 0;  // an unknown day after every known one
        if (ka != kb) return ka;
        if (a.day != b.day) return a.day > b.day;
        if (a.kind != b.kind) return a.kind == RecentPlay::Kind::Level;
        if (a.kind == RecentPlay::Kind::Boss && a.seq != b.seq) return a.seq > b.seq;
        return a.id < b.id;
    });
    if (all.size() > count) all.resize(count);
    return all;
}

std::vector<double> dailyPlaySeconds(const PlayStatsData& d, int from, int to) {
    std::vector<double> out;
    if (to < from) return out;
    out.reserve(static_cast<size_t>(to - from + 1));
    for (int day = from; day <= to; ++day) {
        const StatsDay* s = d.find(day);
        out.push_back(s ? std::max(0.0, s->c.playSec) : 0.0);
    }
    return out;
}

std::vector<double> trailingSums(const std::vector<double>& v, int window) {
    std::vector<double> out(v.size(), 0.0);
    window = std::max(1, window);
    double sum = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        if (i >= static_cast<size_t>(window)) sum -= v[i - static_cast<size_t>(window)];
        out[i] = sum;
    }
    return out;
}

namespace {

/// The size of a part as written in the file (compact JSON, the same as the file's).
size_t written(const Json& j) {
    if (j.is_null()) return 0;
    return j.dump(-1, ' ', false, Json::error_handler_t::replace).size();
}

}  // namespace

FileBreakdown fileBreakdown(const Json& file, size_t fileBytes) {
    FileBreakdown out;
    out.total = fileBytes;
    const Json* data = nullptr;
    if (file.is_object() && file.contains("data") && file["data"].is_object()) data = &file["data"];
    if (!data) return out;
    out.ok = true;
    auto part = [&](const char* key) -> const Json& {
        static const Json none;
        auto it = data->find(key);
        return it == data->end() ? none : *it;
    };
    FilePart days{FilePart::Kind::Days, 0, 0, 0};
    days.bytes = written(part("names")) + written(part("days")) + written(part("months"));
    if (part("days").is_array()) days.count = static_cast<int>(part("days").size());
    if (part("months").is_array()) days.count2 = static_cast<int>(part("months").size());
    FilePart bosses{FilePart::Kind::Bosses, written(part("bosses")), 0, 0};
    if (part("bosses").is_object()) {
        bosses.count = static_cast<int>(part("bosses").size());
        for (const auto& [id, b] : part("bosses").items()) {
            // Its attempts kept: 4 characters each (PlayStatsData::toJson).
            if (b.is_object() && b.contains("r") && b["r"].is_string()) bosses.count2 += static_cast<int>(b["r"].get<std::string>().size() / 4);
        }
    }
    FilePart levels{FilePart::Kind::Levels, written(part("levels")), 0, kSlices};
    if (part("levels").is_object()) levels.count = static_cast<int>(part("levels").size());
    FilePart segs{FilePart::Kind::Segments, written(part("runs")), 0, 0};
    if (part("runs").is_object()) {
        segs.count = static_cast<int>(part("runs").size());
        for (const auto& [id, r] : part("runs").items())
            if (r.is_object() && r.contains("seg") && r["seg"].is_array()) segs.count2 += static_cast<int>(r["seg"].size());
    }
    out.parts = {days, bosses, levels, segs};
    size_t sum = 0;
    for (const auto& p : out.parts) sum += p.bytes;
    out.other = fileBytes > sum ? fileBytes - sum : 0;
    return out;
}

std::vector<SobPart> sobParts(const LevelRuns& l) {
    std::vector<SobPart> out;
    int i = 0;
    for (const auto& [from, to] : l.route()) {
        SobPart p;
        p.from = from;
        p.to = to;
        p.index = i++;
        if (const SegmentRec* s = l.segment(from, to)) {
            p.best = s->bestAll();
            if (p.best > 0.0f) p.source = s->trainBest > 0.0f && (s->best <= 0.0f || s->trainBest < s->best) ? SobSource::Training : SobSource::Run;
        }
        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace cosmic::playstats
