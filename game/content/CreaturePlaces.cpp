#include "game/content/CreaturePlaces.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <set>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/LevelProgress.h"
#include "game/progression/Progression.h"

namespace cosmic {

namespace {

struct RoutePoint {
    float at = 0.0f;
    const LevelObject* object = nullptr;
};

/// The places of one level, in the order of its objects (summons right after their summoner).
std::vector<CreaturePlace> scanLevel(const ContentDatabase& content, const LevelEntry& entry, const LevelData& level, const std::string& planetId,
                                     int number, bool routes) {
    LevelProgressTrack track;
    if (routes) track = buildProgressTrack(level, content);
    // Along the route; without one (or not asked), from left to right.
    const float width = std::max(1.0f, level.bounds.w);
    auto at = [&](Vec2 p) { return track.valid() ? track.fractionAt(p) : std::clamp((p.x - level.bounds.x) / width, 0.0f, 1.0f); };
    std::vector<RoutePoint> checkpoints, tablets;
    for (const LevelObject& o : level.objects) {
        if (o.type == "checkpoint" && !o.prop<bool>("optional", false)) checkpoints.push_back({at(o.rect.bottomCenter()), &o});
        else if (o.type == "power_unlock") tablets.push_back({at(o.rect.center()), &o});
    }
    std::vector<CreaturePlace> out;
    std::set<std::string> summonedHere;
    for (const LevelObject& o : level.objects) {
        if (o.type != "enemy") continue;
        std::string id = o.propStr("enemy");
        if (id.empty()) continue;
        const EnemyDefinition* def = content.enemy(id);
        CreaturePlace p;
        p.levelId = entry.id;
        p.levelName = level.name.empty() ? entry.name : level.name;
        p.planetId = planetId;
        p.levelNumber = number;
        p.official = entry.source == ContentSource::Builtin;
        p.enemyId = id;
        p.objectId = o.id;
        p.boss = def && def->isBoss;
        p.feet = o.rect.bottomCenter();
        if (routes) {
            p.at = at(p.feet);
            const RoutePoint* before = nullptr;
            for (const RoutePoint& c : checkpoints)
                if (c.at < p.at && (!before || c.at > before->at)) before = &c;
            if (before) {
                p.checkpointId = before->object->id;
                p.checkpointLabel = before->object->propStr("label");
                p.checkpointFeet = before->object->rect.bottomCenter();
                p.checkpointAt = before->at;
            }
            // The tablets a player has picked up on the way to that checkpoint (route order).
            std::vector<RoutePoint> got;
            for (const RoutePoint& t : tablets)
                if (before && t.at <= before->at) got.push_back(t);
            std::stable_sort(got.begin(), got.end(), [](const RoutePoint& a, const RoutePoint& b) { return a.at < b.at; });
            for (const RoutePoint& t : got) p.powersBefore.push_back(t.object->propStr("power", "dash"));
        }
        out.push_back(p);
        // What it summons is met there too (once per level).
        if (def)
            for (const auto& a : def->attacks)
                if (a.type == AttackType::Summon && !a.summonId.empty() && summonedHere.insert(a.summonId).second) {
                    CreaturePlace s = p;
                    s.enemyId = a.summonId;
                    s.objectId.clear();
                    s.summoned = true;
                    const EnemyDefinition* sd = content.enemy(a.summonId);
                    s.boss = sd && sd->isBoss;
                    out.push_back(s);
                }
        // 2.57: the crystal a boss hides its soul in (the Liche's phylactery) too; 2.58: the valves
        // of a stalled boss (the Titan's).
        if (def)
            for (const BossPhase& ph : def->phases)
                for (const std::string& extra : {ph.phylactery, ph.stall})
                    if (!extra.empty() && summonedHere.insert(extra).second) {
                        CreaturePlace s = p;
                        s.enemyId = extra;
                        s.objectId.clear();
                        s.summoned = true;
                        const EnemyDefinition* sd = content.enemy(extra);
                        s.boss = sd && sd->isBoss;
                        out.push_back(s);
                    }
    }
    // The bosses' order along the route (an arena of several bosses: « 5 sur 16 »).
    std::vector<CreaturePlace*> bosses;
    for (CreaturePlace& p : out)
        if (p.boss && !p.summoned) bosses.push_back(&p);
    std::stable_sort(bosses.begin(), bosses.end(), [&](const CreaturePlace* a, const CreaturePlace* b) {
        float fa = a->at >= 0.0f ? a->at : a->feet.x, fb = b->at >= 0.0f ? b->at : b->feet.x;
        return fa < fb;
    });
    for (size_t i = 0; i < bosses.size(); ++i) {
        bosses[i]->bossRank = static_cast<int>(i) + 1;
        bosses[i]->bossCount = static_cast<int>(bosses.size());
        if (!routes) continue;
        for (size_t k = 0; k < i; ++k)
            if (bosses[k]->at > bosses[i]->checkpointAt && bosses[k]->at < bosses[i]->at) ++bosses[i]->bossesBetween;
    }
    return out;
}

}  // namespace

CreatureAtlas scanCreatures(const ContentDatabase& content, CreatureScanOptions opt) {
    CreatureAtlas atlas;
    struct Item {
        const LevelEntry* entry;
        std::string planet;
        int number;
    };
    std::vector<Item> order;
    std::set<std::string> listed;
    for (const PlanetData* p : content.planets()) {
        int n = 0;
        for (const LevelEntry* e : content.levelsForPlanet(p->id)) {
            ++n;
            if (opt.officialOnly && e->source != ContentSource::Builtin) continue;
            if (listed.insert(e->id).second) order.push_back({e, p->id, n});
        }
    }
    // The other official levels (outside the campaigns).
    for (const LevelEntry* e : content.levels())
        if (e->source == ContentSource::Builtin && listed.insert(e->id).second) order.push_back({e, std::string{}, 0});
    for (const Item& it : order) {
        auto level = content.loadLevel(it.entry->id);
        if (!level) continue;
        for (CreaturePlace& p : scanLevel(content, *it.entry, *level, it.planet, it.number, opt.routes))
            atlas.places[p.enemyId].push_back(std::move(p));
    }
    return atlas;
}

std::map<std::string, std::vector<std::string>> CreatureAtlas::planetsByCreature() const {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto& [id, list] : places)
        for (const CreaturePlace& p : list) {
            if (p.planetId.empty()) continue;
            auto& v = out[id];
            if (std::find(v.begin(), v.end(), p.planetId) == v.end()) v.push_back(p.planetId);
        }
    return out;
}

std::vector<CreaturePlace> CreatureAtlas::fightsOf(const ContentDatabase& content, const std::string& bossId) const {
    std::vector<CreaturePlace> out;
    auto add = [&](const std::string& id) {
        auto it = places.find(id);
        if (it == places.end()) return;
        for (const CreaturePlace& p : it->second)
            if (p.official) out.push_back(p);
    };
    add(bossId);
    for (const EnemyDefinition* d : content.enemies())
        if (d->id != bossId && d->variantOf == bossId) add(d->id);
    return out;
}

std::shared_ptr<const CreatureAtlas> officialCreatureAtlas(const ContentDatabase& content) {
    using Result = std::shared_ptr<const CreatureAtlas>;
    static std::mutex mutex;
    static std::shared_future<Result> job;
    std::lock_guard<std::mutex> lock(mutex);
    if (!job.valid()) {
        // The worker reads its own copy: the content may be reloaded meanwhile, the screen
        // that asked may be gone.
        auto copy = std::make_shared<const ContentDatabase>(content);
        job = std::async(std::launch::async, [copy] {
                  CreatureScanOptions opt;
                  opt.routes = true;
                  opt.officialOnly = true;
                  return Result(std::make_shared<const CreatureAtlas>(scanCreatures(*copy, opt)));
              }).share();
    }
    if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return job.get();
}

bool levelPlanetOpen(const LevelEntry& level, const ContentDatabase& content, const Progression& progression) {
    const PlanetData* p = content.planet(content.campaignPlanetOf(level.id));
    return p && progression.planetUnlocked(*p, content);
}

std::string levelLockReason(const LevelEntry& level, const ContentDatabase& content, const Progression& progression) {
    const PlanetData* p = content.planet(content.campaignPlanetOf(level.id));
    if (!p) return {};
    if (!progression.planetUnlocked(*p, content)) {
        const std::string& rule = p->unlock;
        std::vector<std::string> todo;  // planets still to finish
        auto need = [&](const std::string& id) {
            const PlanetData* other = content.planet(id);
            if (other && !progression.planetCompleted(*other, content)) todo.push_back(other->name);
        };
        if (rule.rfind("planets:", 0) == 0) {
            size_t start = 8;
            while (start <= rule.size()) {
                size_t end = rule.find(',', start);
                std::string id = rule.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!id.empty()) need(id);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        } else if (rule.rfind("planet:", 0) == 0) {
            need(rule.substr(7));
        } else if (rule.rfind("level:", 0) == 0) {
            const LevelEntry* other = content.level(rule.substr(6));
            if (other) return "Verrouillé : terminez le niveau « " + other->name + " »";
        }
        if (todo.size() == 1) return "Verrouillé : terminez la planète " + todo.front();
        if (!todo.empty()) {
            std::string names;
            for (const std::string& n : todo) names += (names.empty() ? "" : ", ") + n;
            return "Verrouillé : terminez les planètes " + names;
        }
        return "Verrouillé";
    }
    if (!progression.levelUnlocked(level, content)) return "Verrouillé : terminez le niveau précédent";
    return {};
}

}  // namespace cosmic
