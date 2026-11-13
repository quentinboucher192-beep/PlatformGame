#include "game/progression/Progression.h"

#include <algorithm>

#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "saves/SaveFile.h"

namespace cosmic {

namespace {
SaveFile progressFile() { return SaveFile("progress", Progression::kVersion); }
fs::path progressPath() { return Paths::savesDir() / "progress.json"; }
}  // namespace

Json Progression::toJson() const {
    return {{"completedLevels", std::vector<std::string>(completed_.begin(), completed_.end())},
            {"lastPlanet", lastPlanet},
            {"lastLevel", lastLevel},
            {"playTimeSeconds", playTimeSeconds},
            {"introSeen", introSeen}};
}

void Progression::fromJson(const Json& j) {
    completed_.clear();
    for (const auto& id : jget<std::vector<std::string>>(j, "completedLevels", {})) completed_.insert(id);
    lastPlanet = jstr(j, "lastPlanet", "sylvanox");
    lastLevel = jstr(j, "lastLevel");
    playTimeSeconds = jget<double>(j, "playTimeSeconds", 0.0);
    introSeen = jget<bool>(j, "introSeen", false);
}

void Progression::load() {
    if (auto d = progressFile().load(progressPath())) fromJson(*d);
}

bool Progression::save() const { return progressFile().save(progressPath(), toJson()); }

bool Progression::planetCompleted(const PlanetData& p, const ContentDatabase& content) const {
    auto levels = content.levelsForPlanet(p.id);
    bool any = false;
    for (const auto* l : levels) {
        if (l->source != ContentSource::Builtin) continue;
        any = true;
        if (!levelCompleted(l->id)) return false;
    }
    return any;
}

bool Progression::planetUnlocked(const PlanetData& p, const ContentDatabase& content) const {
    if (content.levelsForPlanet(p.id).empty()) return false;  // nothing to play there (teasers)
    if (unlockAll) return true;
    const std::string& rule = p.unlock;
    if (rule == "always" || rule.empty()) return true;
    if (rule == "never") return false;
    if (rule.rfind("planet:", 0) == 0) {
        const PlanetData* other = content.planet(rule.substr(7));
        return other && planetCompleted(*other, content);
    }
    if (rule.rfind("level:", 0) == 0) return levelCompleted(rule.substr(6));
    return true;
}

bool Progression::levelUnlocked(const LevelEntry& level, const ContentDatabase& content) const {
    if (level.source != ContentSource::Builtin || unlockAll) return true;
    const PlanetData* p = content.planet(level.planet);
    if (!p) return true;
    auto it = std::find(p->levels.begin(), p->levels.end(), level.id);
    if (it == p->levels.begin() || it == p->levels.end()) return true;
    return levelCompleted(*(it - 1));
}

}  // namespace cosmic
