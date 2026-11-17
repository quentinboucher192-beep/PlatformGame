#include "game/raid/RaidBranch.h"

#include <algorithm>
#include <deque>
#include <mutex>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"

namespace cosmic::raid {

namespace {
Rect rectOf(const Json& a) {
    if (!a.is_array() || a.size() != 4) return {};
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
}
}  // namespace

bool Branch::parseCatalog(const Json& j, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    if (!j.is_object()) return fail("catalogue de la branche illisible");
    id = jstr(j, "id");
    name = jstr(j, "name");
    gigaboss = jstr(j, "gigaboss");
    revision = jget<int>(j, "revision", 0);
    startZone = jget<int>(j, "start_zone", 1);
    generatorsNeeded = jget<int>(j, "generators_needed", 2);
    zones.clear();
    edges.clear();
    objectives.clear();
    for (const auto& z : j.value("zones", Json::array())) {
        ZoneInfo i;
        i.id = jget<int>(z, "id", 0);
        i.name = jstr(z, "name");
        i.sub = jstr(z, "sub");
        i.level = jstr(z, "level");
        i.inside = jstr(z, "inside");
        i.event = jstr(z, "event");
        i.mapPos = {jget<float>(z, "x", 0.0f), jget<float>(z, "y", 0.0f)};
        i.secret = jget<bool>(z, "secret", false);
        i.boss = jget<bool>(z, "boss", false);
        i.groupsRequired = jget<int>(z, "groups_required", 1);
        if (i.id != static_cast<int>(zones.size()) + 1) return fail("zones de la branche dans le désordre");
        zones.push_back(i);
    }
    for (const auto& e : j.value("edges", Json::array()))
        edges.push_back({jget<int>(e, "a", 0), jget<int>(e, "b", 0), jstr(e, "lock"), jget<bool>(e, "secret", false)});
    for (const auto& o : j.value("objectives", Json::array())) {
        ObjectiveInfo i;
        i.id = jstr(o, "id");
        i.index = jget<int>(o, "index", -1);
        i.zone = jget<int>(o, "zone", 0);
        i.label = jstr(o, "label");
        i.optional = jget<bool>(o, "optional", false);
        i.role = jstr(o, "role");
        i.pair = jstr(o, "pair");
        if (i.index != static_cast<int>(objectives.size()) || i.index >= 32) return fail("objectifs de la branche dans le désordre");
        objectives.push_back(i);
    }
    if (zones.empty() || id.empty()) return fail("branche vide");
    return true;
}

bool Branch::load(const std::filesystem::path& dir, std::string* error) {
    auto cat = FileUtil::loadJson(dir / "branch.json");
    if (!cat || !parseCatalog(*cat, error)) {
        if (error && error->empty()) *error = "branch.json introuvable";
        return false;
    }
    levels.clear();
    rules.clear();
    for (const ZoneInfo& z : zones) {
        auto j = FileUtil::loadJson(dir / z.level);
        std::string e;
        std::optional<LevelData> l = j ? LevelData::fromJson(*j, &e) : std::nullopt;
        if (!l) {
            if (error) *error = z.level + " : " + (e.empty() ? "introuvable" : e);
            return false;
        }
        levels.push_back(std::move(*l));
        rules.push_back(parseZoneRules(levels.back(), *this));
    }
    return true;
}

const Branch* Branch::builtin() {
    static std::once_flag once;
    static Branch branch;
    static bool ok = false;
    std::call_once(once, [] { ok = branch.load(Paths::dataDir() / "raids" / "asterion" / "bras_droit"); });
    return ok ? &branch : nullptr;
}

const ZoneInfo* Branch::zone(int zid) const { return zid >= 1 && zid <= static_cast<int>(zones.size()) ? &zones[static_cast<size_t>(zid - 1)] : nullptr; }

const ObjectiveInfo* Branch::objective(const std::string& oid) const {
    for (const auto& o : objectives)
        if (o.id == oid) return &o;
    return nullptr;
}

int Branch::objectiveIndex(const std::string& oid) const {
    const ObjectiveInfo* o = objective(oid);
    return o ? o->index : -1;
}

const LevelData* Branch::level(int zid) const { return zid >= 1 && zid <= static_cast<int>(levels.size()) ? &levels[static_cast<size_t>(zid - 1)] : nullptr; }

const ZoneRules* Branch::zoneRules(int zid) const { return zid >= 1 && zid <= static_cast<int>(rules.size()) ? &rules[static_cast<size_t>(zid - 1)] : nullptr; }

int Branch::generators(uint32_t mask) const {
    int n = 0;
    for (const auto& o : objectives)
        if (o.role == "generator" && done(mask, o.index)) ++n;
    return n;
}

bool Branch::shortcut(uint32_t mask) const {
    for (const auto& o : objectives)
        if (o.role == "shortcut" && done(mask, o.index)) return true;
    return false;
}

bool Branch::lockOpen(const std::string& lock, uint32_t mask) const {
    if (lock.empty()) return true;
    if (lock == "generators") return generators(mask) >= generatorsNeeded;
    if (lock == "shortcut") return shortcut(mask);
    if (lock.rfind("objective:", 0) == 0) return done(mask, objectiveIndex(lock.substr(10)));
    return false;  // "sealed" or unknown: closed
}

bool Branch::controlled(int zid, uint32_t mask) const {
    bool any = false;
    for (const auto& o : objectives) {
        if (o.zone != zid || o.optional) continue;
        any = true;
        if (!done(mask, o.index)) return false;
    }
    return any;
}

std::vector<bool> Branch::reachable(uint32_t mask) const {
    std::vector<bool> r(zones.size() + 1, false);
    if (!zone(startZone)) return r;
    std::deque<int> todo{startZone};
    r[static_cast<size_t>(startZone)] = true;
    while (!todo.empty()) {
        const int z = todo.front();
        todo.pop_front();
        for (const auto& e : edges) {
            if (e.a != z && e.b != z) continue;
            const int other = e.a == z ? e.b : e.a;
            if (other < 1 || other >= static_cast<int>(r.size()) || r[static_cast<size_t>(other)] || !lockOpen(e.lock, mask)) continue;
            r[static_cast<size_t>(other)] = true;
            todo.push_back(other);
        }
    }
    return r;
}

std::vector<const EdgeInfo*> Branch::edgesOf(int zid) const {
    std::vector<const EdgeInfo*> out;
    for (const auto& e : edges)
        if (e.a == zid || e.b == zid) out.push_back(&e);
    return out;
}

std::string Branch::pairDone(const ObjectiveInfo& o, uint32_t mask) const {
    if (o.pair.empty()) return {};
    for (const auto& x : objectives)
        if (x.index != o.index && x.pair == o.pair && done(mask, x.index)) return x.id;
    return {};
}

ZoneRules parseZoneRules(const LevelData& level, const Branch& b) {
    ZoneRules z;
    const Json raid = level.metadata.value("raid", Json::object());
    z.zone = jget<int>(raid, "zone", 0);
    const Json spawns = raid.value("spawns", Json::object());  // kept alive while iterating
    for (const auto& it : spawns.items()) {
        std::vector<Vec2>& list = z.spawns[it.key()];
        for (const auto& p : it.value())
            if (p.is_array() && p.size() == 2) list.push_back({p[0].get<float>(), p[1].get<float>()});
    }
    for (const auto& p : raid.value("passages", Json::array()))
        z.passages.push_back({jstr(p, "id"), jget<int>(p, "to", 0), rectOf(p.value("rect", Json())), jstr(p, "lock"), jstr(p, "door")});
    for (const auto& o : raid.value("objectives", Json::array())) {
        ZoneObjective zo{jstr(o, "id"), jstr(o, "source"), jstr(o, "trigger"), -1};
        zo.index = b.objectiveIndex(zo.id);
        if (zo.index >= 0) z.objectives.push_back(zo);
    }
    const Json magma = raid.value("magma", Json());
    if (magma.is_object()) {
        z.magma.present = true;
        z.magma.zone = jstr(magma, "zone");
        z.magma.period = std::max(10.0f, jget<float>(magma, "period", 90.0f));
        z.magma.first = jget<float>(magma, "first", 45.0f);
        z.magma.warn = jget<float>(magma, "warn", 3.0f);
        z.magma.duration = std::min(z.magma.period - 1.0f, jget<float>(magma, "duration", 8.0f));
    }
    const Json sealed = raid.value("sealed", Json());
    if (sealed.is_object()) {
        z.sealedDoor = jstr(sealed, "door");
        z.sealedLabel = jstr(sealed, "label");
    }
    return z;
}

}  // namespace cosmic::raid
