#include "game/progression/NewBadges.h"

#include <regex>

#include "engine/core/FileUtil.h"
#include "game/progression/NewsCenter.h"

namespace cosmic {

namespace {

const std::regex kIdRe("^[a-z0-9_.:/-]{1,96}$");
const std::regex kVersionRe("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,4}$");

}  // namespace

void NewBadges::loadCatalog(const fs::path& file) {
    std::vector<Feature> out;
    if (auto j = FileUtil::loadJson(file); j && j->is_object() && j->contains("features") && (*j)["features"].is_array()) {
        for (const auto& f : (*j)["features"]) {
            if (!f.is_object() || !f.contains("id") || !f["id"].is_string()) continue;
            Feature x;
            x.id = f["id"].get<std::string>();
            if (!std::regex_match(x.id, kIdRe)) continue;
            // No version: a step of the trail (a tab, a category) between novelties and the menus.
            if (f.contains("version")) {
                if (!f["version"].is_string() || !std::regex_match(f["version"].get<std::string>(), kVersionRe)) continue;
                x.version = f["version"].get<std::string>();
            }
            if (f.contains("parents") && f["parents"].is_array())
                for (const auto& p : f["parents"])
                    if (p.is_string() && std::regex_match(p.get<std::string>(), kIdRe) && p.get<std::string>() != x.id)
                        x.parents.push_back(p.get<std::string>());
            if (x.version.empty() && x.parents.empty()) continue;  // links nothing
            out.push_back(std::move(x));
        }
    }
    setCatalog(out);
}

void NewBadges::setCatalog(const std::vector<Feature>& features) {
    features_.clear();
    children_.clear();
    cache_.clear();
    for (const auto& f : features) features_[f.id] = f;
    for (const auto& [id, f] : features_)
        for (const auto& p : f.parents) children_[p].push_back(id);
}

void NewBadges::init(const fs::path& saveFile, const std::string& previousVersion, const std::string& currentVersion) {
    file_ = saveFile;
    seen_.clear();
    baseline_.clear();
    cache_.clear();
    if (auto j = FileUtil::loadJson(file_); j && j->is_object()) {
        if (j->contains("baseline") && (*j)["baseline"].is_string() && std::regex_match((*j)["baseline"].get<std::string>(), kVersionRe))
            baseline_ = (*j)["baseline"].get<std::string>();
        if (j->contains("seen") && (*j)["seen"].is_array())
            for (const auto& s : (*j)["seen"])
                if (s.is_string() && s.get<std::string>().size() <= 120 && seen_.size() < 4000) seen_.insert(s.get<std::string>());
    }
    if (baseline_.empty()) {
        // First run with the pills: what came after the version the player had is new; on a
        // fresh install nothing is (everything is new to them, a pill everywhere says nothing).
        baseline_ = std::regex_match(previousVersion, kVersionRe) ? previousVersion : currentVersion;
        save();
    }
}

bool NewBadges::ownNew(const std::string& id) const {
    auto it = features_.find(id);
    if (it == features_.end() || it->second.version.empty()) return false;
    const Feature& f = it->second;
    return versionNewer(f.version, baseline_) && !seen_.count(f.id + "@" + f.version);
}

bool NewBadges::newAtOrBelow(const std::string& id, std::set<std::string>& visited) const {
    if (!visited.insert(id).second) return false;  // already looked at (or a cycle)
    if (ownNew(id)) return true;
    auto it = children_.find(id);
    if (it == children_.end()) return false;
    for (const auto& c : it->second)
        if (newAtOrBelow(c, visited)) return true;
    return false;
}

bool NewBadges::isNew(const std::string& id) const {
    if (auto it = cache_.find(id); it != cache_.end()) return it->second;
    // A novelty: until seen. A parent: as long as anything under it is new, whatever focus it
    // had (its own focus or click never takes the pill away while something deeper is new).
    std::set<std::string> visited;
    bool v = newAtOrBelow(id, visited);
    if (cache_.size() > 4096) cache_.clear();
    cache_[id] = v;
    return v;
}

bool NewBadges::markSeen(const std::string& id) {
    auto it = features_.find(id);
    if (it == features_.end() || it->second.version.empty()) return false;  // a step: nothing of its own
    if (!seen_.insert(id + "@" + it->second.version).second) return false;
    cache_.clear();
    return true;
}

bool NewBadges::save() const {
    if (file_.empty()) return false;
    Json j = {{"format", "cosmic.new_badges"}, {"version", 1}, {"baseline", baseline_}, {"seen", Json::array()}};
    for (const auto& s : seen_) j["seen"].push_back(s);
    return FileUtil::writeTextAtomic(file_, j.dump(1));
}

}  // namespace cosmic
