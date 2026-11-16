#include "game/records/KeptRecords.h"

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"

namespace cosmic {

namespace {
const std::set<std::string>& none() {
    static const std::set<std::string> empty;
    return empty;
}
bool hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
}  // namespace

void KeptRecords::loadDefault() {
    if (auto j = FileUtil::loadJson(Paths::dataDir() / "levels" / "kept_records.json")) load(*j);
}

void KeptRecords::load(const Json& j) {
    gameplay_.clear();
    ranked_.clear();
    if (!j.is_object() || !j.contains("levels") || !j["levels"].is_object()) return;
    for (const auto& [id, list] : j["levels"].items()) {
        if (!list.is_array()) continue;
        for (const Json& v : list) {
            if (!v.is_object()) continue;
            const std::string g = v.value("gameplay", std::string()), r = v.value("ranked", std::string());
            if (hex64(g)) gameplay_[id].insert(g);
            if (hex64(r)) ranked_[id].insert(r);
        }
    }
}

const std::set<std::string>& KeptRecords::gameplay(const std::string& levelId) const {
    auto it = gameplay_.find(levelId);
    return it == gameplay_.end() ? none() : it->second;
}

const std::set<std::string>& KeptRecords::ranked(const std::string& levelId) const {
    auto it = ranked_.find(levelId);
    return it == ranked_.end() ? none() : it->second;
}

}  // namespace cosmic
