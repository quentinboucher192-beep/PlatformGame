#include "game/ghost/StartPosStore.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"

namespace cosmic {

namespace {

bool validKey(const std::string& s) {
    if (s.empty() || s.size() > 96) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool before(const StartPosEntry& a, const StartPosEntry& b) {
    if (a.at >= 0.0f && b.at >= 0.0f && a.at != b.at) return a.at < b.at;
    if (a.state.feet.x != b.state.feet.x) return a.state.feet.x < b.state.feet.x;
    return a.id < b.id;
}

}  // namespace

StartPosStore::StartPosStore(fs::path file) : file_(std::move(file)) {}

void StartPosStore::load() {
    levels_.clear();
    readOnly_ = false;
    if (file_.empty()) file_ = Paths::savesDir() / "startpos.json";
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    auto j = FileUtil::loadJson(file_);
    if (!j || !j->is_object() || jstr(*j, "format") != "cosmic.startpos" || !(*j)["levels"].is_object()) {
        fs::path bad = file_;
        bad += ".corrupt";
        fs::remove(bad, ec);
        fs::rename(file_, bad, ec);
        return;
    }
    readOnly_ = jget<int>(*j, "version", 0) > kVersion;
    show = jget<bool>(*j, "show", true);
    for (const auto& [key, lj] : (*j)["levels"].items()) {
        if (levels_.size() >= kLevelsKept) break;
        if (!validKey(key) || !lj.is_object()) continue;
        Level l;
        l.touched = jstr(lj, "touched").substr(0, 32);
        for (const Json& ej : jfield(lj, "list", Json::array())) {
            if (l.entries.size() >= kPerLevel) break;
            auto st = StartPosState::fromJson(jfield(ej, "state"));
            const int id = jget<int>(ej, "id", 0);
            if (!st || id <= 0 || id > 1000000 ||
                std::any_of(l.entries.begin(), l.entries.end(), [&](const StartPosEntry& e) { return e.id == id; }))
                continue;
            StartPosEntry e;
            e.id = id;
            e.state = std::move(*st);
            e.at = jget<float>(ej, "at", -1.0f);
            if (!std::isfinite(e.at) || e.at < 0.0f || e.at > 1.0f) e.at = -1.0f;
            e.date = jstr(ej, "date").substr(0, 32);
            e.attempts = std::clamp(jget<int>(ej, "attempts", 0), 0, 10000000);
            l.nextId = std::max(l.nextId, id + 1);
            l.entries.push_back(std::move(e));
        }
        l.nextId = std::max(l.nextId, std::clamp(jget<int>(lj, "next", 1), 1, 1000001));
        l.active = jget<int>(lj, "active", 0);
        if (std::none_of(l.entries.begin(), l.entries.end(), [&](const StartPosEntry& e) { return e.id == l.active; })) l.active = 0;
        if (!l.entries.empty()) levels_[key] = std::move(l);
    }
}

bool StartPosStore::save() {
    if (file_.empty() || readOnly_) return false;
    Json levels = Json::object();
    for (const auto& [key, l] : levels_) {
        Json list = Json::array();
        for (const StartPosEntry& e : l.entries)
            list.push_back(Json{{"id", e.id}, {"state", e.state.toJson()}, {"at", e.at}, {"date", e.date}, {"attempts", e.attempts}});
        levels[key] = Json{{"touched", l.touched}, {"active", l.active}, {"next", l.nextId}, {"list", list}};
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    Json root{{"format", "cosmic.startpos"}, {"version", kVersion}, {"show", show}, {"levels", levels}};
    return FileUtil::writeTextAtomic(file_, root.dump(1, ' ', false, Json::error_handler_t::replace) + "\n");
}

std::vector<StartPosEntry> StartPosStore::list(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return {};
    std::vector<StartPosEntry> out = it->second.entries;
    std::sort(out.begin(), out.end(), before);
    return out;
}

int StartPosStore::rank(const std::string& levelKey, int id) const {
    const std::vector<StartPosEntry> l = list(levelKey);
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].id == id) return static_cast<int>(i);
    return -1;
}

int StartPosStore::activeId(const std::string& levelKey) const {
    auto it = levels_.find(levelKey);
    return it == levels_.end() ? 0 : it->second.active;
}

const StartPosEntry* StartPosStore::find(const std::string& levelKey, int id) const {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return nullptr;
    for (const StartPosEntry& e : it->second.entries)
        if (e.id == id) return &e;
    return nullptr;
}

int StartPosStore::add(const std::string& levelKey, const StartPosState& s, float at, const std::string& date) {
    if (!validKey(levelKey)) return 0;
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) {
        while (levels_.size() >= kLevelsKept) {
            auto oldest = std::min_element(levels_.begin(), levels_.end(), [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
            levels_.erase(oldest);
        }
        it = levels_.emplace(levelKey, Level{}).first;
    }
    Level& l = it->second;
    if (l.entries.size() >= kPerLevel) return 0;
    StartPosEntry e;
    e.id = l.nextId++;
    e.state = s;
    e.at = std::isfinite(at) && at >= 0.0f && at <= 1.0f ? at : -1.0f;
    e.date = date.substr(0, 32);
    l.entries.push_back(e);
    l.active = e.id;
    l.touched = e.date;
    return e.id;
}

void StartPosStore::setActive(const std::string& levelKey, int id) {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return;
    if (id == 0 || find(levelKey, id)) it->second.active = id;
}

int StartPosStore::cycle(const std::string& levelKey, int delta) {
    const std::vector<StartPosEntry> l = list(levelKey);
    if (l.empty() || delta == 0) return activeId(levelKey);
    const int n = static_cast<int>(l.size());
    int r = rank(levelKey, activeId(levelKey));
    r = r < 0 ? (delta > 0 ? 0 : n - 1) : ((r + (delta > 0 ? 1 : -1)) % n + n) % n;
    setActive(levelKey, l[static_cast<size_t>(r)].id);
    return l[static_cast<size_t>(r)].id;
}

bool StartPosStore::remove(const std::string& levelKey, int id) {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return false;
    auto& es = it->second.entries;
    auto e = std::find_if(es.begin(), es.end(), [&](const StartPosEntry& x) { return x.id == id; });
    if (e == es.end()) return false;
    es.erase(e);
    if (it->second.active == id) it->second.active = 0;
    if (es.empty()) levels_.erase(it);
    return true;
}

void StartPosStore::clear(const std::string& levelKey) { levels_.erase(levelKey); }

void StartPosStore::noteAttempt(const std::string& levelKey, int id) {
    auto it = levels_.find(levelKey);
    if (it == levels_.end()) return;
    for (StartPosEntry& e : it->second.entries)
        if (e.id == id) e.attempts = std::min(e.attempts + 1, 10000000);
}

}  // namespace cosmic
