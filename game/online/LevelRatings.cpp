#include "game/online/LevelRatings.h"

#include <cmath>
#include <cstdio>

namespace cosmic {

namespace {

bool validLevelId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

/// Average and count of a JSON object {average, count}; false if they make no sense.
bool readSummary(const Json& j, double& average, int& count) {
    if (!j.is_object() || !j.contains("count") || !j["count"].is_number_integer()) return false;
    count = j["count"].get<int>();
    if (count < 0) return false;
    if (count == 0) {
        average = 0.0;
        return true;
    }
    if (!j.contains("average") || !j["average"].is_number()) return false;
    average = j["average"].get<double>();
    return std::isfinite(average) && average >= 1.0 && average <= 5.0;
}

int readStars(const Json& v) {
    if (!v.is_number_integer()) return 0;
    int s = v.get<int>();
    return s >= 1 && s <= 5 ? s : 0;
}

}  // namespace

void LevelRatingCache::resetFor(const std::string& server, const std::string& player) {
    if (server != server_) {
        levels_.clear();
        mine_.clear();
        receivedAt_ = 0;
        fresh_ = false;
        server_ = server;
    }
    if (player != player_) {
        mine_.clear();
        player_ = player;
    }
}

bool LevelRatingCache::applyList(const Json& data, const std::string& server, const std::string& player, int64_t nowUnix) {
    if (!data.is_object() || !data.contains("levels") || !data["levels"].is_array()) return false;
    std::map<std::string, Entry> levels;
    for (const auto& l : data["levels"]) {
        if (!l.is_object() || !l.contains("level_id") || !l["level_id"].is_string()) continue;
        std::string id = l["level_id"].get<std::string>();
        Entry e;
        if (!validLevelId(id) || !readSummary(l, e.average, e.count)) continue;
        levels[id] = e;
    }
    std::map<std::string, int> mine;
    if (data.contains("my_ratings") && data["my_ratings"].is_object())
        for (const auto& [id, v] : data["my_ratings"].items())
            if (int s = readStars(v); s > 0 && validLevelId(id)) mine[id] = s;
    resetFor(server, player);
    levels_ = std::move(levels);
    mine_ = std::move(mine);
    receivedAt_ = nowUnix > 0 ? nowUnix : 1;
    fresh_ = true;
    return true;
}

bool LevelRatingCache::applyOne(const Json& view, const std::string& server, const std::string& player) {
    if (!view.is_object() || !view.contains("level_id") || !view["level_id"].is_string()) return false;
    std::string id = view["level_id"].get<std::string>();
    Entry e;
    if (!validLevelId(id) || !view.contains("rating") || !readSummary(view["rating"], e.average, e.count)) return false;
    resetFor(server, player);
    if (e.count > 0) levels_[id] = e;
    else levels_.erase(id);
    if (!player.empty() && view.contains("my_stars")) {
        if (int s = readStars(view["my_stars"]); s > 0) mine_[id] = s;
        else mine_.erase(id);
    }
    return true;
}

std::optional<LevelRatingInfo> LevelRatingCache::get(const std::string& levelId, const std::string& player) const {
    auto it = levels_.find(levelId);
    if (it == levels_.end() && !known()) return std::nullopt;
    LevelRatingInfo info;
    if (it != levels_.end()) {
        info.average = it->second.average;
        info.count = it->second.count;
    }
    if (!player.empty() && player == player_)
        if (auto m = mine_.find(levelId); m != mine_.end()) info.myStars = m->second;
    return info;
}

Json LevelRatingCache::toJson() const {
    Json levels = Json::object();
    for (const auto& [id, e] : levels_) levels[id] = {{"average", e.average}, {"count", e.count}};
    Json mine = Json::object();
    for (const auto& [id, s] : mine_) mine[id] = s;
    return {{"version", 1}, {"server", server_}, {"player", player_}, {"received_at", receivedAt_}, {"levels", levels}, {"mine", mine}};
}

bool LevelRatingCache::fromJson(const Json& j, const std::string& server) {
    if (!j.is_object() || jget<int>(j, "version", 0) != 1 || jstr(j, "server") != server || server.empty()) return false;
    if (!j.contains("levels") || !j["levels"].is_object()) return false;
    clear();
    server_ = server;
    player_ = jstr(j, "player");
    receivedAt_ = jget<int64_t>(j, "received_at", 0);
    for (const auto& [id, v] : j["levels"].items()) {
        Entry e;
        if (validLevelId(id) && readSummary(v, e.average, e.count) && e.count > 0) levels_[id] = e;
    }
    if (j.contains("mine") && j["mine"].is_object())
        for (const auto& [id, v] : j["mine"].items())
            if (int s = readStars(v); s > 0 && validLevelId(id)) mine_[id] = s;
    fresh_ = false;
    return true;
}

void LevelRatingCache::clear() {
    server_.clear();
    player_.clear();
    receivedAt_ = 0;
    fresh_ = false;
    levels_.clear();
    mine_.clear();
}

std::string LevelRatingCache::averageText(double average) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f", std::round(average * 10.0) / 10.0);
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string LevelRatingCache::countText(int count) {
    if (count <= 0) return "aucune note";
    return std::to_string(count) + (count == 1 ? " note" : " notes");
}

}  // namespace cosmic
