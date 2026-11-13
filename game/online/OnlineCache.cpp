#include "game/online/OnlineCache.h"

#include <algorithm>
#include <vector>

#include "engine/core/FileUtil.h"
#include "packaging/Sha256.h"

namespace cosmic {

namespace {
int64_t toEpoch(std::chrono::system_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}
}  // namespace

fs::path OnlineCache::fileFor(const std::string& key) const { return dir_ / (Sha256::hex(key).substr(0, 40) + ".json"); }

void OnlineCache::put(const std::string& key, const Json& data, const Json& meta) {
    Entry e{data, meta, std::chrono::system_clock::now()};
    mem_[key] = e;
    if (!dir_.empty()) {
        Json j = {{"format", "cosmic.online_cache"}, {"version", 1}, {"key", key}, {"fetched_at", toEpoch(e.fetchedAt)}, {"data", data}, {"meta", meta}};
        std::error_code ec;
        fs::create_directories(dir_, ec);
        FileUtil::writeTextAtomic(fileFor(key), j.dump());
    }
    if (mem_.size() > max_) prune();
}

std::optional<OnlineCache::Entry> OnlineCache::get(const std::string& key) {
    if (auto it = mem_.find(key); it != mem_.end()) return it->second;
    if (dir_.empty()) return std::nullopt;
    auto text = FileUtil::readText(fileFor(key));
    if (!text) return std::nullopt;
    Json j = Json::parse(*text, nullptr, false);
    // The key is stored to rule out hash collisions and foreign files.
    if (!j.is_object() || jstr(j, "format") != "cosmic.online_cache" || jstr(j, "key") != key) return std::nullopt;
    Entry e;
    e.data = j.value("data", Json());
    e.meta = j.value("meta", Json());
    e.fetchedAt = std::chrono::system_clock::time_point(std::chrono::seconds(jget<int64_t>(j, "fetched_at", 0)));
    mem_[key] = e;
    return e;
}

size_t OnlineCache::clear() {
    mem_.clear();
    size_t n = 0;
    if (dir_.empty()) return 0;
    std::error_code ec;
    if (!fs::exists(dir_, ec)) return 0;
    for (const auto& f : fs::directory_iterator(dir_, ec)) {
        if (f.path().extension() == ".json" && fs::remove(f.path(), ec)) ++n;
    }
    return n;
}

void OnlineCache::prune() {
    // Drop the oldest half of the memory entries; disk files are pruned the same way.
    std::vector<std::pair<std::chrono::system_clock::time_point, std::string>> order;
    for (const auto& [k, e] : mem_) order.push_back({e.fetchedAt, k});
    std::sort(order.begin(), order.end());
    for (size_t i = 0; i < order.size() / 2; ++i) mem_.erase(order[i].second);
    if (dir_.empty()) return;
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& f : fs::directory_iterator(dir_, ec))
        if (f.path().extension() == ".json") files.push_back({fs::last_write_time(f.path(), ec), f.path()});
    if (files.size() <= max_) return;
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i + max_ < files.size(); ++i) fs::remove(files[i].second, ec);
}

}  // namespace cosmic
