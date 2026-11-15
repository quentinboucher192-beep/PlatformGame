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
            if (!f.is_object() || !f.contains("id") || !f["id"].is_string() || !f.contains("version") || !f["version"].is_string()) continue;
            Feature x;
            x.id = f["id"].get<std::string>();
            x.version = f["version"].get<std::string>();
            if (!std::regex_match(x.id, kIdRe) || !std::regex_match(x.version, kVersionRe)) continue;
            if (f.contains("parents") && f["parents"].is_array())
                for (const auto& p : f["parents"])
                    if (p.is_string() && std::regex_match(p.get<std::string>(), kIdRe) && p.get<std::string>() != x.id)
                        x.parents.push_back(p.get<std::string>());
            out.push_back(std::move(x));
        }
    }
    setCatalog(out);
}

void NewBadges::setCatalog(const std::vector<Feature>& features) {
    features_.clear();
    children_.clear();
    for (const auto& f : features) features_[f.id] = f;
    for (const auto& [id, f] : features_)
        for (const auto& p : f.parents) children_[p].push_back(id);
}

void NewBadges::init(const fs::path& saveFile, const std::string& previousVersion, const std::string& currentVersion) {
    file_ = saveFile;
    seen_.clear();
    baseline_.clear();
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
    if (it == features_.end()) return false;
    const Feature& f = it->second;
    return versionNewer(f.version, baseline_) && !seen_.count(f.id + "@" + f.version);
}

std::string NewBadges::newestUnseen(const std::string& id, int depth) const {
    std::string best;
    if (ownNew(id)) best = features_.at(id).version;
    if (depth > 8) return best;  // a badly written catalogue cannot loop
    auto it = children_.find(id);
    if (it == children_.end()) return best;
    for (const auto& c : it->second) {
        std::string v = newestUnseen(c, depth + 1);
        if (!v.empty() && (best.empty() || versionNewer(v, best))) best = v;
    }
    return best;
}

bool NewBadges::isNew(const std::string& id) const {
    if (ownNew(id)) return true;
    // A parent: new while something under it is, until it is focused itself (for that version).
    std::string v = newestUnseen(id);
    return !v.empty() && !seen_.count(id + "@" + v);
}

void NewBadges::markSeen(const std::string& id) {
    if (auto it = features_.find(id); it != features_.end()) seen_.insert(id + "@" + it->second.version);
    std::string v = newestUnseen(id);
    if (!v.empty()) seen_.insert(id + "@" + v);
}

bool NewBadges::save() const {
    if (file_.empty()) return false;
    Json j = {{"format", "cosmic.new_badges"}, {"version", 1}, {"baseline", baseline_}, {"seen", Json::array()}};
    for (const auto& s : seen_) j["seen"].push_back(s);
    return FileUtil::writeTextAtomic(file_, j.dump(1));
}

}  // namespace cosmic
