#include "game/coop/CoopLevels.h"

#include <algorithm>

#include "game/content/ContentDatabase.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/online/LevelFingerprint.h"

namespace cosmic {

namespace {

void readBosses(const ContentDatabase& content, const LevelData& data, CoopLevel& l) {
    for (const auto& o : data.objects) {
        if (o.type != "enemy") continue;
        const EnemyDefinition* d = content.enemyFor(o.propStr("enemy"), data);
        if (d && d->isBoss) l.bosses.push_back(d->bossTitle.empty() ? d->name : d->bossTitle);
    }
}

}  // namespace

fs::path communityPackagePath(const fs::path& downloads, const std::string& communityId, int version) {
    return downloads / (communityId + "-v" + std::to_string(version) + ".cplanet");
}

std::vector<CoopLevel> findCoopLevels(const ContentDatabase& content, const Json& installs, const fs::path& downloads) {
    std::vector<CoopLevel> out;
    std::vector<CoopLevel> community;
    for (const LevelEntry* e : content.levels()) {
        const bool campaign = !content.campaignPlanetOf(e->id).empty();
        const Json* info = nullptr;
        if (!campaign && installs.is_object()) {
            auto it = installs.find(e->id);
            if (it != installs.end() && it->is_object()) info = &*it;
        }
        if (!campaign && !info) continue;
        auto data = content.loadLevel(e->id);
        if (!data || !data->extreme || !data->oneLife()) continue;
        CoopLevel l;
        l.id = e->id;
        l.name = data->name;
        l.planetId = data->planet;
        if (const PlanetData* p = content.planet(data->planet)) l.planet = p->name;
        if (info) {
            l.community = true;
            l.communityId = jstr(*info, "id");
            l.version = jget<int>(*info, "version", 0);
            const std::string published = jstr(*info, "level_hash");
            if (l.communityId.empty() || l.version <= 0 || published.size() != 64) continue;
            // Only a copy that plays exactly as the published version: the others of the team
            // download that version, and the run must be the same on every PC.
            l.hash = fingerprintGameplay(content, *data);
            if (l.hash != published) continue;
            if (!jstr(*info, "title").empty()) l.name = jstr(*info, "title");
            l.author = !jstr(*info, "author").empty() ? jstr(*info, "author") : !e->author.empty() ? e->author : data->author;
            std::error_code ec;
            const fs::path pkg = communityPackagePath(downloads, l.communityId, l.version);
            if (!downloads.empty() && fs::is_regular_file(pkg, ec)) l.package = pkg;
        }
        readBosses(content, *data, l);
        (l.community ? community : out).push_back(std::move(l));
    }
    std::stable_sort(community.begin(), community.end(), [](const CoopLevel& a, const CoopLevel& b) { return a.name < b.name; });
    for (auto& l : community) out.push_back(std::move(l));
    return out;
}

const CoopLevel* findRoomLevel(const std::vector<CoopLevel>& levels, const std::string& communityId, int version, const std::string& hash) {
    for (const CoopLevel& l : levels)
        if (l.community && l.communityId == communityId && l.version == version && !hash.empty() && l.hash == hash) return &l;
    return nullptr;
}

}  // namespace cosmic
