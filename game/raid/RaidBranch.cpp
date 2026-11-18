#include "game/raid/RaidBranch.h"

#include <algorithm>
#include <deque>
#include <memory>
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
    lockTexts.clear();
    const Json locks = j.value("lock_texts", Json::object());  // 2.68 (kept alive while iterating)
    for (const auto& it : locks.items())
        lockTexts[it.key()] = {jstr(it.value(), "hint"), jstr(it.value(), "message"), jstr(it.value(), "objective")};
    for (const auto& o : j.value("objectives", Json::array())) {
        ObjectiveInfo i;
        i.id = jstr(o, "id");
        i.index = jget<int>(o, "index", -1);
        i.zone = jget<int>(o, "zone", 0);
        i.label = jstr(o, "label");
        i.optional = jget<bool>(o, "optional", false);
        i.role = jstr(o, "role");
        i.pair = jstr(o, "pair");
        if (i.index != static_cast<int>(objectives.size()) || i.index >= kLinkBit0) return fail("objectifs de la branche dans le désordre");
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

const Branch* Branch::builtin(const std::string& part) {
    // Each branch once (null when it cannot be read); the titan's links attached.
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Branch>> loaded;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = loaded.find(part);
    if (it == loaded.end()) {
        auto b = std::make_unique<Branch>();
        const bool safe = !part.empty() && part.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") == std::string::npos;
        if (!safe || !b->load(Paths::dataDir() / "raids" / "asterion" / part)) b.reset();
        if (b) {
            b->part = part;
            if (auto titan = FileUtil::loadJson(Paths::dataDir() / "raids/asterion/titan.json")) b->attachTitan(*titan);
        }
        it = loaded.emplace(part, std::move(b)).first;
    }
    return it->second.get();
}

void Branch::attachTitan(const Json& titan) {
    titanRevision = jget<int>(titan, "revision", 0);
    effects.clear();
    for (const auto& l : titan.value("links", Json::array())) {
        if (jstr(l, "to") != id) continue;
        EffectInfo e;
        e.id = jstr(l, "id");
        e.kind = jstr(l, "kind");
        e.label = jstr(l, "label");
        e.bit = jget<int>(l, "bit", -1);
        if (e.bit < kLinkBit0 || e.bit >= 32) continue;
        for (const auto& z : l.value("zones", Json::array()))
            if (z.is_number_integer()) e.zones.push_back(z.get<int>());
        for (const auto& o : l.value("objects", Json::array()))
            if (o.is_string()) e.objects.push_back(o.get<std::string>());
        for (const auto& f : l.value("from", Json::array())) e.from.emplace_back(jstr(f, "branch"), jstr(f, "objective_id"));
        effects.push_back(std::move(e));
    }
}

bool EffectInfo::inZone(int z) const { return std::find(zones.begin(), zones.end(), z) != zones.end(); }

const EffectInfo* Branch::effect(const std::string& eid) const {
    for (const auto& e : effects)
        if (e.id == eid) return &e;
    return nullptr;
}

bool Branch::hazardOff(int zid, uint32_t mask) const {
    for (const auto& e : effects)
        if (e.kind == "hazard_off" && on(mask, e) && e.inZone(zid)) return true;
    return false;
}

LockText Branch::lockText(const std::string& lock) const {
    if (auto it = lockTexts.find(lock); it != lockTexts.end()) return it->second;
    // The BRAS DROIT (its catalog has no texts): its two generators, the forearm's lever.
    if (lock == "generators")
        return {"il faut les deux générateurs", "Les deux générateurs tournent : le Poignet-verrou s'ouvre",
                "allumer les deux générateurs (Veine, Avant-bras)"};
    if (lock == "shortcut")
        return {"le levier de l'avant-bras l'ouvre", "Le raccourci Veine ⇄ Avant-bras est ouvert", "raccourci fermé (le levier est dans l'Avant-bras)"};
    if (lock.rfind("link:", 0) == 0) {
        const EffectInfo* e = effect(lock.substr(5));
        return {"une autre branche du titan l'ouvre", e ? e->label : std::string(), "fermé : une autre branche du titan l'ouvre"};
    }
    return {};
}

const Titan* Titan::builtin() {
    static std::once_flag once;
    static Titan titan;
    static bool ok = false;
    std::call_once(once, [] {
        auto j = FileUtil::loadJson(Paths::dataDir() / "raids/asterion/titan.json");
        ok = j && titan.parse(*j);
    });
    return ok ? &titan : nullptr;
}

bool Titan::parse(const Json& j, std::string* error) {
    if (!j.is_object() || jstr(j, "format") != "cosmic.raid_titan") {
        if (error) *error = "titan.json illisible";
        return false;
    }
    revision = jget<int>(j, "revision", 0);
    branches.clear();
    for (const auto& b : j.value("branches", Json::array())) branches.push_back({jstr(b, "part"), jstr(b, "id"), jstr(b, "dir")});
    links = j.value("links", Json::array());
    return !branches.empty();
}

const TitanBranch* Titan::branch(const std::string& part) const {
    for (const auto& b : branches)
        if (b.part == part) return &b;
    return nullptr;
}

const std::vector<const Branch*>& Branch::all() {
    // The titan's catalog (titan.json) lists its branches in the order of its body: a branch added
    // there (and to the game's data) appears in every list built from this one.
    static std::once_flag once;
    static std::vector<const Branch*> list;
    std::call_once(once, [] {
        if (const Titan* t = Titan::builtin())
            for (const TitanBranch& tb : t->branches)
                if (const Branch* b = builtin(tb.part)) list.push_back(b);
        if (list.empty())
            if (const Branch* b = builtin()) list.push_back(b);
    });
    return list;
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
    if (lock.rfind("link:", 0) == 0) {  // 2.68: opened from another branch of the titan
        const EffectInfo* e = effect(lock.substr(5));
        return e && e->kind == "open" && on(mask, *e);
    }
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
        if (!jstr(magma, "name").empty()) z.magma.name = jstr(magma, "name");
        if (!jstr(magma, "hint").empty()) z.magma.hint = jstr(magma, "hint");
        if (!jstr(magma, "message").empty()) z.magma.message = jstr(magma, "message");
    }
    for (const auto& c : raid.value("coop", Json::array())) {  // 2.68
        CoopPlate p;
        p.id = jstr(c, "id");
        p.label = jstr(c, "label");
        p.message = jstr(c, "message");
        p.action = jstr(c, "action", "open");
        p.rect = rectOf(c.value("rect", Json()));
        p.players = std::clamp(jget<int>(c, "players", 2), 1, kMaxInstancePlayers);
        for (const auto& t : c.value("targets", Json::array()))
            if (t.is_string()) p.targets.push_back(t.get<std::string>());
        p.group = jstr(c, "group");  // 2.68 (phase 3): plates held together
        z.coops.push_back(std::move(p));
    }
    for (const auto& c : raid.value("checkpoints", Json::array()))
        if (c.is_string()) z.checkpoints.push_back(c.get<std::string>());
    const Json sealed = raid.value("sealed", Json());
    if (sealed.is_object()) {
        z.sealedDoor = jstr(sealed, "door");
        z.sealedLabel = jstr(sealed, "label");
    }
    // 2.68: the arena of a gigaboss (the seven branches write the same format).
    const Json arena = raid.value("arena", Json());
    if (arena.is_object()) {
        z.arena.present = true;
        z.arena.rect = rectOf(arena.value("rect", Json()));
        z.arena.floor = jget<float>(arena, "floor", 0.0f);
        z.arena.door = jstr(arena, "door");
        const Json entry = arena.value("entry", Json());
        if (entry.is_array() && entry.size() == 2) z.arena.entry = {entry[0].get<float>(), entry[1].get<float>()};
    }
    // 2.68: the gigaboss of the zone, its vents and its weak points.
    const Json giga = raid.value("gigaboss", Json());
    if (giga.is_object()) {
        GigabossRules& g = z.gigaboss;
        g.boss = jstr(giga, "boss");
        g.name = jstr(giga, "name");
        g.objective = jstr(giga, "objective");
        g.present = !g.boss.empty();
        for (const auto& v : giga.value("vents", Json::array())) {
            g.ventIds.push_back(jstr(v, "id"));
            g.vents.push_back(rectOf(v.value("rect", Json())));
        }
        const Json carapace = giga.value("carapace", Json::array());
        if (carapace.is_array() && carapace.size() == 2) {
            g.carapaceFrom = carapace[0].get<int>();
            g.carapaceUntil = carapace[1].get<int>();
        }
        g.hold = std::clamp(jget<float>(giga, "hold", g.hold), 0.5f, 10.0f);
        g.reopen = std::clamp(jget<float>(giga, "reopen", g.reopen), 3.0f, 60.0f);
        g.exposed = std::clamp(jget<float>(giga, "exposed", g.exposed), 2.0f, 30.0f);
        g.exposedMult = std::clamp(jget<float>(giga, "exposedMult", g.exposedMult), 1.0f, 4.0f);
        const Json jet = giga.value("jet", Json());
        if (jet.is_object()) {
            g.jetPeriod = std::clamp(jget<float>(jet, "period", g.jetPeriod), 2.0f, 30.0f);
            g.jetWarn = std::clamp(jget<float>(jet, "warn", g.jetWarn), 0.2f, g.jetPeriod * 0.5f);
            g.jetTime = std::clamp(jget<float>(jet, "duration", g.jetTime), 0.1f, g.jetPeriod * 0.4f);
            g.jetHeight = jget<float>(jet, "height", g.jetHeight);
            g.jetDamage = jget<float>(jet, "damage", g.jetDamage);
        }
        const Json fists = giga.value("fists", Json());
        if (fists.is_object()) {
            for (const auto& v : fists.value("periods", Json::array()))
                if (v.is_number()) g.fistPeriods.push_back(std::clamp(v.get<float>(), 2.0f, 60.0f));
            g.fistWarn = std::clamp(jget<float>(fists, "warn", g.fistWarn), 0.5f, 4.0f);
            g.fistWidth = std::clamp(jget<float>(fists, "width", g.fistWidth), 16.0f, 160.0f);
            g.fistDamage = jget<float>(fists, "damage", g.fistDamage);
        }
        g.weakEnemy = jstr(giga, "weakEnemy");
        for (const auto& p : giga.value("weakSpots", Json::array()))
            if (p.is_array() && p.size() == 2) g.weakSpots.push_back({p[0].get<float>(), p[1].get<float>()});
        g.weakShare = std::clamp(jget<float>(giga, "weakShare", g.weakShare), 0.0f, 0.2f);
        g.hpPerPlayer = std::clamp(jget<float>(giga, "hpPerPlayer", g.hpPerPlayer), 0.0f, 2.0f);
        g.weakHpPerPlayer = std::clamp(jget<float>(giga, "weakHpPerPlayer", g.weakHpPerPlayer), 0.0f, 2.0f);
        g.pressurePerPlayer = std::clamp(jget<float>(giga, "pressurePerPlayer", g.pressurePerPlayer), 0.0f, 0.3f);
        g.kind = jstr(giga, "kind");  // 2.69 (phase 4B): its own fight (raid::BossMechanic)
        g.params = giga;
    }
    return z;
}

}  // namespace cosmic::raid
