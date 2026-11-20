#include "game/raid/RaidSheets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/enemies/BossDifficulty.h"
#include "game/online/AsterionBody.h"
#include "game/raid/GigabossTunables.h"
#include "game/raid/RaidBranch.h"

namespace cosmic::raid {

namespace {

/// `path` (« gaze.track ») in `rules`; null when absent.
const Json* at(const Json& rules, const std::string& path) {
    const Json* j = &rules;
    size_t from = 0;
    while (from <= path.size()) {
        const size_t dot = path.find('.', from);
        const std::string key = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
        if (!j->is_object()) return nullptr;
        auto it = j->find(key);
        if (it == j->end()) return nullptr;
        j = &*it;
        if (dot == std::string::npos) break;
        from = dot + 1;
    }
    return j;
}

bool number(const Json* j, float& out) {
    if (!j || !j->is_number()) return false;
    out = j->get<float>();
    return true;
}

std::string num(float v) { return sheetNumber(v, 2); }

/// The value of phase `phase` (1-based) of a per-phase list, as the routines read it: its last
/// value for the phases after it.
bool perPhase(const Json* list, int phase, float& out) {
    if (!list || !list->is_array() || list->empty()) return false;
    const size_t i = static_cast<size_t>(std::clamp(phase - 1, 0, static_cast<int>(list->size()) - 1));
    return number(&(*list)[i], out);
}

std::string lineText(const Json& line) {
    if (line.is_string()) return line.get<std::string>();
    if (line.is_object()) return jstr(line, "text");
    return {};
}

}  // namespace

std::string sheetValue(const std::string& ref, const Json& rules, bool* ok) {
    auto fail = [&]() {
        if (ok) *ok = false;
        return std::string("?");
    };
    if (ok) *ok = true;
    const size_t colon = ref.find(':');
    const std::string fn = colon == std::string::npos ? std::string() : ref.substr(0, colon);
    const std::string arg = colon == std::string::npos ? ref : ref.substr(colon + 1);
    float v = 0.0f;
    if (fn.empty()) {
        const Json* j = at(rules, arg);
        if (!j) return fail();
        if (j->is_number()) return num(j->get<float>());
        if (!j->is_array() || j->empty()) return fail();
        std::string out;
        for (const Json& e : *j) {
            if (!e.is_number()) return fail();
            if (!out.empty()) out += " / ";
            out += num(e.get<float>());
        }
        return out;
    }
    if (fn == "count" || fn == "gaps") {
        const Json* j = at(rules, arg);
        if (!j || !j->is_array()) return fail();
        const int n = static_cast<int>(j->size()) - (fn == "gaps" ? 1 : 0);
        return n > 0 ? std::to_string(n) : fail();
    }
    if (fn == "pct") return number(at(rules, arg), v) ? num(v * 100.0f) : fail();
    if (fn.size() == 3 && fn.rfind("at", 0) == 0 && fn[2] >= '1' && fn[2] <= '9')
        return perPhase(at(rules, arg), fn[2] - '0', v) ? num(v) : fail();
    const int n = std::atoi(arg.c_str());
    if (fn == "hpmul") {
        if (!number(at(rules, "hpPerPlayer"), v) || n < 1) return fail();
        return num(1.0f + v * static_cast<float>(n - 1));
    }
    if (fn == "slack") return n >= 1 ? num(tunables::slack(n)) : fail();
    if (fn == "plateslack") return n >= 1 ? num(tunables::plateSlack(n)) : fail();
    if (fn == "code") {
        static const std::map<std::string, int> codes = {
            {"pinMinPlayers", tunables::kPinMinPlayers}, {"pinTwoFrom", tunables::kPinTwoFrom}, {"pinMany", tunables::kPinMany},
            {"gulpTwoFrom", tunables::kGulpTwoFrom},     {"gulpMany", tunables::kGulpMany},
        };
        auto it = codes.find(arg);
        return it == codes.end() ? fail() : std::to_string(it->second);
    }
    return fail();
}

std::string sheetText(const Json& line, const Json& rules, std::vector<std::string>* problems) {
    std::string text = lineText(line);
    if (!line.is_object()) return text;
    const Json values = line.value("values", Json::array());
    if (!values.is_array()) return text;
    for (size_t i = 0; i < values.size(); ++i) {
        const std::string ref = values[i].is_string() ? values[i].get<std::string>() : std::string();
        bool ok = true;
        const std::string v = sheetValue(ref, rules, &ok);
        if (!ok && problems) problems->push_back("valeur introuvable : " + ref);
        const std::string slot = "{" + std::to_string(i) + "}";
        for (size_t p = text.find(slot); p != std::string::npos; p = text.find(slot, p + v.size())) text.replace(p, slot.size(), v);
    }
    if (problems && text.find('{') != std::string::npos && text.find('}') != std::string::npos)
        problems->push_back("emplacement sans valeur : " + text);
    return text;
}

std::vector<int> sheetPhases(const Json& spec, const Json& rules, int phaseCount, bool* ok) {
    if (ok) *ok = true;
    std::vector<int> out;
    auto fail = [&]() {
        if (ok) *ok = false;
        return std::vector<int>{};
    };
    if (spec.is_array()) {
        for (const Json& p : spec)
            if (p.is_number_integer() && p.get<int>() >= 1 && p.get<int>() <= phaseCount) out.push_back(p.get<int>());
        return out.empty() ? fail() : out;
    }
    if (!spec.is_string()) return fail();
    const std::string s = spec.get<std::string>();
    if (s == "all") {
        for (int p = 1; p <= phaseCount; ++p) out.push_back(p);
        return out;
    }
    const size_t colon = s.find(':');
    if (colon == std::string::npos) return fail();
    const std::string fn = s.substr(0, colon), path = s.substr(colon + 1);
    const Json* j = at(rules, path);
    if (fn == "range") {  // [from, until) as the rules write it (carapace, veil)
        if (!j || !j->is_array() || j->size() != 2 || !(*j)[0].is_number() || !(*j)[1].is_number()) return fail();
        for (int p = std::max(1, (*j)[0].get<int>()); p < (*j)[1].get<int>() && p <= phaseCount; ++p) out.push_back(p);
    } else if (fn == "from") {
        float v = 0.0f;
        if (!number(j, v)) return fail();
        for (int p = std::max(1, static_cast<int>(std::lround(v))); p <= phaseCount; ++p) out.push_back(p);
    } else if (fn == "nonzero") {
        for (int p = 1; p <= phaseCount; ++p) {
            float v = 0.0f;
            if (!perPhase(j, p, v)) return fail();
            if (v > 0.0f) out.push_back(p);
        }
    } else {
        return fail();
    }
    return out.empty() ? fail() : out;
}

std::string phasesLabel(const std::vector<int>& phases, int phaseCount) {
    if (phases.empty()) return {};
    if (static_cast<int>(phases.size()) == phaseCount && phaseCount > 1) return "Toutes les phases";
    if (phases.size() == 1) return "Phase " + std::to_string(phases.front());
    if (phases.size() == 2) return "Phases " + std::to_string(phases[0]) + " et " + std::to_string(phases[1]);
    std::string first;
    for (size_t i = 0; i + 1 < phases.size(); ++i) first += (i > 0 ? ", " : "") + std::to_string(phases[i]);
    return "Phases " + first + " et " + std::to_string(phases.back());
}

const GigabossSheet* RaidSheets::find(const std::string& id) const {
    for (const GigabossSheet& s : gigaboss)
        if (s.id == id) return &s;
    return !id.empty() && heart.id == id ? &heart : nullptr;
}

const GigabossSheet* RaidSheets::ofPart(const std::string& part) const {
    for (const GigabossSheet& s : gigaboss)
        if (s.part == part) return &s;
    return !part.empty() && !heart.id.empty() && heart.part == part ? &heart : nullptr;
}

const GigabossSheet* RaidSheets::ofEnemy(const std::string& enemy) const {
    for (const GigabossSheet& s : gigaboss)
        if (s.enemy == enemy) return &s;
    return !enemy.empty() && !heart.id.empty() && heart.enemy == enemy ? &heart : nullptr;
}

int sheetHealth(const SheetStats& st, int players, int relics) {
    // Gigaboss::healthFactor (the seats taken), then the heart's relics (AsterionHeartRoutine: at least the floor kept).
    float hp = st.hp * (1.0f + st.hpPerPlayer * static_cast<float>(std::clamp(players, 1, 8) - 1));
    if (st.relicShare > 0.0f && relics > 0) hp *= std::max(st.relicFloor, 1.0f - st.relicShare * static_cast<float>(relics));
    return static_cast<int>(std::lround(hp));
}

std::string RaidSheets::avoidOf(const std::string& type) const {
    for (const auto& [t, text] : avoid)
        if (t == type) return text;
    return {};
}

namespace {

/// The minutes the catalogue of a branch estimates for each zone (branch.json, « minutes »).
std::map<int, float> zoneMinutes(const std::string& part) {
    std::map<int, float> out;
    if (auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion" / part / "branch.json"))
        for (const Json& z : j->value("zones", Json::array()))
            if (z.is_object()) out[jget<int>(z, "id", 0)] = jget<float>(z, "minutes", 0.0f);
    return out;
}

/// The labels of the titan's links a branch gives (its objectives act on another branch).
std::vector<std::string> linksFrom(const std::string& branchId) {
    std::vector<std::string> out;
    const Titan* t = Titan::builtin();
    if (!t) return out;
    for (const Json& l : t->links) {
        bool mine = false;
        for (const Json& f : l.value("from", Json::array())) mine = mine || jstr(f, "branch") == branchId;
        if (mine && !jstr(l, "label").empty()) out.push_back(jstr(l, "label"));
    }
    return out;
}

/// A badge and the other rewards of a sheet, from the catalogues.
SheetReward badgeReward(const AchievementDef& a) { return {"badge", a.name, a.description, a.stars}; }
SheetReward itemReward(const CosmeticItem& c) { return {c.category, c.name, c.description, 0}; }

std::string kindOrder(const std::string& k) { return k == "badge" ? "0" : k == "title" ? "1" : "2"; }

void buildGigaboss(GigabossSheet& s, const Json& e, const ContentDatabase& content, const AchievementCatalog& achievements,
                   const CosmeticCatalog& cosmetics, const std::vector<asterion::Part>& parts, const Json& common) {
    auto problem = [&](const std::string& p) { s.problems.push_back(s.id + " : " + p); };
    s.id = jstr(e, "id");
    s.part = jstr(e, "part");
    s.enemy = jstr(e, "enemy");
    for (const asterion::Part& p : parts)
        if (p.id == s.part) {
            s.partName = p.name;
            s.effect = p.effect;
        }
    if (s.partName.empty()) problem("partie inconnue du titan : " + s.part);
    // Its definition (phases, attacks, last stand, difficulty).
    s.def = content.enemy(s.enemy);
    if (!s.def) problem("ennemi introuvable : " + s.enemy);
    // Its lair: zone `lair.zone` of branch `lair.part` (zone 8 of its part's branch by default).
    const Json lair = e.value("lair", Json::object());
    const std::string branchPart = jstr(lair, "part", s.part);
    const raid::Branch* b = raid::Branch::builtin(branchPart);
    if (b) {
        s.branchId = b->id;
        s.branchName = b->name;
        s.zones = static_cast<int>(b->zones.size());
        s.lairZone = jget<int>(lair, "zone", 0);
        if (s.lairZone == 0)
            for (const ZoneInfo& z : b->zones)
                if (z.boss) s.lairZone = z.id;
        const std::map<int, float> minutes = zoneMinutes(branchPart);
        for (const auto& [id, m] : minutes) s.branchMinutes += m;
        if (const ZoneInfo* z = b->zone(s.lairZone)) {
            s.lairName = z->name;
            s.lairSub = z->sub;
            // The expedition before it: the longest zone that leads to it.
            float best = -1.0f;
            for (const EdgeInfo* edge : b->edgesOf(s.lairZone)) {
                const int other = edge->a == s.lairZone ? edge->b : edge->a;
                const auto it = minutes.find(other);
                const float m = it == minutes.end() ? 0.0f : it->second;
                if (const ZoneInfo* oz = b->zone(other); oz && m > best) {
                    best = m;
                    s.expedition = oz->name;
                    s.expeditionMinutes = m;
                }
            }
        }
        for (const ObjectiveInfo& o : b->objectives)
            if (o.role == "gigaboss") s.objective = o.label;
        s.links = linksFrom(b->id);
        for (const EffectInfo& fx : b->effects) s.links.push_back(fx.label);
    } else {
        problem("branche introuvable : " + branchPart);
    }
    const ZoneRules* zr = b ? b->zoneRules(s.lairZone) : nullptr;
    const GigabossRules* rules = zr && zr->gigaboss.present ? &zr->gigaboss : nullptr;
    if (!rules) problem("règles du combat introuvables (antre " + std::to_string(s.lairZone) + ")");
    static const Json noRules = Json::object();
    const Json& r = rules ? rules->extra : noRules;
    std::vector<std::string>* probs = &s.problems;
    if (rules) {
        s.fx = rules->fx;
        s.fallText = rules->fallText;
        s.summary = rules->summary;
        s.exposed = rules->exposed;
        s.exposedMult = rules->exposedMult;
        s.weakShare = rules->weakShare;
        // Its weak points, by enemy (the spots of its rules).
        for (size_t k = 0; k < rules->weakSpots.size(); ++k) {
            const std::string id = rules->weakEnemyAt(k);
            auto it = std::find_if(s.weak.begin(), s.weak.end(), [&](const SheetWeakPoint& w) { return w.enemy == id; });
            if (it != s.weak.end()) {
                ++it->count;
                continue;
            }
            SheetWeakPoint w;
            w.enemy = id;
            w.count = 1;
            if (const EnemyDefinition* d = content.enemy(id)) {
                w.name = d->name;
                w.description = d->description;
            } else {
                problem("point faible introuvable : " + id);
                w.name = id;
            }
            s.weak.push_back(std::move(w));
        }
        if (s.weak.empty()) problem("aucun point faible");
    }
    const int phaseCount = s.def ? std::max<int>(1, static_cast<int>(s.def->phases.size())) : 3;
    // 2.75: where to hit it, from the rules its fight reads (and where it stands in its lair).
    if (rules && s.def) {
        s.hitboxes = bossHitboxes(*rules, *s.def, content, b ? b->level(s.lairZone) : nullptr, phaseCount);
        if (s.hitboxes.views.empty()) problem("aucun schéma des zones à frapper");
    }
    if (s.def) {
        s.name = s.def->name;
        s.title = s.def->bossTitle;
        s.description = s.def->description;
        s.stars = bossDifficulty(*s.def).stars;
        for (size_t i = 0; i < s.def->phases.size(); ++i) {
            const BossPhase& ph = s.def->phases[i];
            SheetPhase sp;
            sp.number = static_cast<int>(i) + 1;
            sp.name = ph.name;
            sp.from = i == 0 ? std::string("dès le début") : "à " + sheetNumber(ph.hpThreshold * 100.0f, 1) + " % de sa vie";
            sp.enterText = ph.enterText;
            for (const std::string& a : ph.attacks)
                if (const AttackDef* ad = s.def->attack(a)) sp.attacks.push_back(ad->name);
            s.phases.push_back(std::move(sp));
        }
        s.lastStandText = s.def->finalText;
        std::set<std::string> seen;
        for (const std::string& a : s.def->finalAttacks)
            if (const AttackDef* ad = s.def->attack(a); ad && seen.insert(a).second) s.lastStand.push_back(ad->name);
        if (s.phases.empty()) problem("aucune phase");
        if (s.def->attacks.empty()) problem("aucune attaque");
    } else {
        s.name = s.id;
    }
    // The mechanisms of its routine.
    for (const Json& m : e.value("mechanics", Json::array())) {
        SheetMechanic mc;
        mc.id = jstr(m, "id");
        mc.kind = jstr(m, "kind", "arena");
        mc.name = jstr(m, "name");
        mc.summary = sheetText(m.value("summary", Json()), r, probs);
        mc.text = sheetText(m.value("text", Json()), r, probs);
        mc.hint = sheetText(m.value("hint", Json()), r, probs);
        mc.players = sheetText(m.value("message", Json()), r, probs);
        bool ok = true;
        mc.phases = sheetPhases(m.value("phases", Json("all")), r, phaseCount, &ok);
        if (!ok) problem(mc.id + " : phases illisibles");
        for (const Json& f : m.value("facts", Json::array())) mc.facts.push_back({jstr(f, "label"), sheetText(f.value("detail", Json()), r, probs)});
        // What it asks by the players in play (a list of the rules, one value per player count):
        // its runs of equal values, « 2 à 4 joueurs » → « 2 ».
        const Json players = m.value("players", Json::object());
        mc.byPlayersLabel = jstr(players, "label");
        if (const std::string by = jstr(players, "path"); !by.empty()) {
            const Json* list = at(r, by);
            if (!list || !list->is_array() || list->empty()) problem(mc.id + " : valeurs par joueur introuvables (" + by + ")");
            else {
                const int n = static_cast<int>(list->size());
                for (int i = 0; i < n;) {
                    int j = i;
                    while (j + 1 < n && (*list)[static_cast<size_t>(j + 1)] == (*list)[static_cast<size_t>(i)]) ++j;
                    const std::string who = i == j ? (i == 0 ? std::string("1 joueur") : std::to_string(i + 1) + " joueurs")
                                                   : std::to_string(i + 1) + " à " + std::to_string(j + 1) + " joueurs";
                    float v = 0.0f;
                    number(&(*list)[static_cast<size_t>(i)], v);
                    mc.byPlayers.push_back({who, num(v)});
                    i = j + 1;
                }
            }
        }
        const Json ex = m.value("example", Json());
        if (ex.is_object()) {
            SheetSketch& k = mc.sketch;
            k.kind = jstr(ex, "kind");
            k.clip = jstr(ex, "clip");
            k.clipHeld = jstr(ex, "clipHeld");
            k.clipDone = jstr(ex, "clipDone");
            k.rise = jget<bool>(ex, "rise", false);
            k.wall = jget<bool>(ex, "wall", false);
            k.breaks = jget<bool>(ex, "break", false);
            auto value = [&](const char* key, float& out) {
                const std::string ref = jstr(ex, key);
                if (ref.empty()) return;
                bool vok = true;
                const std::string v = sheetValue(ref, r, &vok);
                if (!vok) {
                    problem(mc.id + " : valeur de l'exemple introuvable (" + ref + ")");
                    return;
                }
                std::string dot = v;
                std::replace(dot.begin(), dot.end(), ',', '.');
                out = static_cast<float>(std::atof(dot.c_str()));
            };
            float count = 0.0f, storeys = 0.0f;
            value("count", count);
            value("storeys", storeys);
            value("warn", k.warn);
            value("hold", k.hold);
            value("time", k.time);
            value("track", k.track);
            value("lock", k.lock);
            value("fire", k.fire);
            value("period", k.period);
            k.count = static_cast<int>(std::lround(count));
            if (const std::string ref = jstr(ex, "enemies"); !ref.empty()) {
                const Json* list = at(r, ref);
                if (list && list->is_array())
                    for (const Json& id : *list)
                        if (id.is_string() && content.enemy(id.get<std::string>())) k.enemies.push_back(id.get<std::string>());
                if (k.enemies.empty()) problem(mc.id + " : ennemis de l'exemple introuvables (" + ref + ")");
            }
            k.storeys = std::max(1, static_cast<int>(std::lround(storeys)));
        }
        if (mc.name.empty() || mc.text.empty()) problem("mécanisme sans nom ou sans texte : " + mc.id);
        s.mechanics.push_back(std::move(mc));
    }
    // The mechanisms on in each phase.
    for (SheetPhase& p : s.phases)
        for (const SheetMechanic& mc : s.mechanics)
            if (std::find(mc.phases.begin(), mc.phases.end(), p.number) != mc.phases.end()) p.mechanics.push_back(mc.name);
    // Its scaling (the rules' numbers) and what counts for the players.
    for (const Json& l : common.value("scaling", Json::array())) s.scaling.push_back(sheetText(l, r, probs));
    for (const Json& l : common.value("contributions", Json::array())) s.contributions.push_back(sheetText(l, r, probs));
    for (const Json& l : e.value("contributions", Json::array())) s.contributions.push_back(sheetText(l, r, probs));
    // 2.77.3: its numbers (« Statistiques »), from its definition, its lair's rules and its parts' rules.
    if (s.def && rules) {
        SheetStats& st = s.stats;
        const EnemyDefinition& d = *s.def;
        st.hp = d.hp;
        st.hpPerPlayer = rules->hpPerPlayer;
        st.weakHpPerPlayer = rules->weakHpPerPlayer;
        st.pressurePerPlayer = rules->pressurePerPlayer;
        st.exposed = rules->exposed;
        st.exposedMult = rules->exposedMult;
        st.weakShare = rules->weakShare;
        if (const Json* share = at(r, "heart.relicShare"); share && share->is_number()) st.relicShare = std::clamp(share->get<float>(), 0.0f, 0.1f);
        for (int n = SheetStats::kMinPlayers; n <= SheetStats::kMaxPlayers; ++n) st.health.push_back(sheetHealth(st, n));
        for (size_t i = 0; i < d.phases.size(); ++i) {
            const BossPhase& ph = d.phases[i];
            SheetStats::Phase p;
            p.number = static_cast<int>(i) + 1;
            p.name = ph.name;
            p.from = i == 0 ? 1.0f : ph.hpThreshold;
            p.speed = ph.speedMultiplier;
            p.enrage = ph.enrageTime;
            for (int h : st.health) p.health.push_back(static_cast<int>(std::lround(static_cast<float>(h) * p.from)));
            for (const HitboxView& v : s.hitboxes.views)
                if (v.phase == p.number && std::find(p.states.begin(), p.states.end(), v.stateName) == p.states.end()) p.states.push_back(v.stateName);
            st.phases.push_back(std::move(p));
        }
        st.contact = d.contactDamage;
        std::set<std::string> names;
        for (const AttackDef& a : d.attacks)
            if (names.insert(a.name).second) st.blows.push_back({a.name, a.damage, false});
        // The blows of its raid's rules: every block of its rules with a damage (its fists, its jets when it has
        // vents, its routine's own), named by the block or by the mechanism of its sheet that reads it.
        for (auto it = r.begin(); it != r.end(); ++it) {
            const Json& b = it.value();
            if (!b.is_object() || !b.contains("damage") || !b["damage"].is_number()) continue;
            const std::string key = it.key();
            if (key == "fists" && rules->fistPeriods.empty()) continue;
            if (key == "jet" && rules->vents.empty()) continue;
            std::string name = jstr(b, "name");
            for (const Json& m : e.value("mechanics", Json::array()))
                if (name.empty() && m.dump().find("\"" + key + ".") != std::string::npos) name = jstr(m, "name");
            if (name.empty()) name = key == "fists" ? rules->fistName : key == "jet" ? rules->jetName : std::string();
            if (name.empty()) {
                problem("coup sans nom dans ses règles : " + key);
                continue;
            }
            st.blows.push_back({name, b["damage"].get<float>(), true});
        }
        const PartRules& pr = PartRules::of(d.id);
        st.parts = pr.rules;
        st.byParts = pr.fromVersion > 0;
        st.speed = d.speed;
        st.chase = d.chaseSpeedMultiplier;
        st.size = d.size;
        st.scale = d.scale;
        st.enrage = d.enrageTime;
        st.lastStand = static_cast<int>(d.finalAttacks.size());
        if (st.health.empty() || st.phases.empty()) problem("statistiques illisibles");
    }
    // Its badge (an achievement « raid_reward »), then its own rewards. 2.77.3: the heart has none of its own
    // (the raid's titles and suits are on Asterion's sheet).
    const std::string badge = jstr(e, "badge");
    if (const AchievementDef* a = badge.empty() ? nullptr : achievements.find(badge); a && a->condition.type == "raid_reward")
        s.rewards.push_back(badgeReward(*a));
    else if (!(badge.empty() && s.part == kHeartPart))
        problem("badge introuvable : " + badge);
    for (const Json& id : e.value("rewards", Json::array()))
        if (const CosmeticItem* c = id.is_string() ? cosmetics.find(id.get<std::string>()) : nullptr) s.rewards.push_back(itemReward(*c));
        else problem("récompense introuvable");
}

}  // namespace

RaidSheets buildRaidSheets(const Json& data, const ContentDatabase& content, const AchievementCatalog& achievements, const CosmeticCatalog& cosmetics) {
    RaidSheets out;
    std::vector<asterion::Part> parts;
    if (auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion.json")) parts = asterion::parseParts(*j);
    if (parts.empty()) out.problems.push_back("les parties du titan sont illisibles");
    const Json common = data.value("asterion", Json::object());
    static const Json none = Json::object();
    const Json avoid = data.value("avoid", Json::object());
    for (auto it = avoid.begin(); it != avoid.end(); ++it) out.avoid.emplace_back(it.key(), sheetText(it.value(), none));
    // The gigaboss, in the order of the titan's body.
    std::vector<GigabossSheet> sheets;
    for (const Json& e : data.value("gigaboss", Json::array())) {
        GigabossSheet s;
        buildGigaboss(s, e, content, achievements, cosmetics, parts, common);
        out.problems.insert(out.problems.end(), s.problems.begin(), s.problems.end());
        sheets.push_back(std::move(s));
    }
    auto rank = [&](const std::string& part) {
        for (size_t i = 0; i < parts.size(); ++i)
            if (parts[i].id == part) return static_cast<int>(i);
        return static_cast<int>(parts.size());
    };
    std::stable_sort(sheets.begin(), sheets.end(), [&](const GigabossSheet& a, const GigabossSheet& b) { return rank(a.part) < rank(b.part); });
    out.gigaboss = std::move(sheets);
    // 2.77.3: the Cœur d'Asterion's sheet, and the links « modifier » of the titan on it (one per part).
    if (const Json he = data.value("heart", Json()); he.is_object()) {
        buildGigaboss(out.heart, he, content, achievements, cosmetics, parts, common);
        out.problems.insert(out.problems.end(), out.heart.problems.begin(), out.heart.problems.end());
        if (const Titan* titan = Titan::builtin()) {
            const std::string to = titan->heart.id;
            for (const Json& l : titan->links) {
                if (jstr(l, "kind") != "modifier" || to.empty() || jstr(l, "to") != to) continue;
                HeartModifier m;
                m.id = jstr(l, "id");
                m.label = jstr(l, "label");
                m.bit = jget<int>(l, "bit", -1);
                for (const Json& f : l.value("from", Json::array()))
                    for (const TitanBranch& tb : titan->branches)
                        if (tb.id == jstr(f, "branch")) m.part = tb.part;
                for (const asterion::Part& p : parts)
                    if (p.id == m.part) {
                        m.partName = p.name;
                        m.boss = p.boss;
                    }
                if (m.part.empty() || m.label.empty()) out.problems.push_back("lien du cœur illisible : " + m.id);
                out.heartModifiers.push_back(std::move(m));
            }
            std::stable_sort(out.heartModifiers.begin(), out.heartModifiers.end(),
                             [&](const HeartModifier& a, const HeartModifier& b) { return rank(a.part) < rank(b.part); });
        }
        if (out.heartModifiers.empty()) out.problems.push_back("aucun lien « modifier » vers le cœur");
    }

    // Asterion: its words, its parts, its heart, the raid's structure, the rewards.
    AsterionSheet& a = out.asterion;
    a.title = jstr(common, "title", "ASTERION");
    a.subtitle = jstr(common, "subtitle");
    for (const Json& l : common.value("lore", Json::array())) a.lore.push_back(sheetText(l, none));
    for (const Json& l : common.value("heart", Json::array())) a.heartText.push_back(sheetText(l, none));
    for (const Json& l : common.value("contributions", Json::array())) a.contributions.push_back(sheetText(l, none));
    const Titan* titan = Titan::builtin();
    for (const asterion::Part& p : parts) {
        AsterionPart ap{p.id, p.name, p.boss, p.effect, {}, {}};
        if (const GigabossSheet* s = out.ofPart(p.id)) ap.sheet = s->id;
        if (p.boss.empty()) {
            a.heart = ap;
            a.heartSheet = ap.sheet;
            continue;
        }
        if (titan)
            if (const TitanBranch* tb = titan->branch(p.id)) ap.links = linksFrom(tb->id);
        a.parts.push_back(std::move(ap));
    }
    a.players = kMaxInstancePlayers;
    bool first = true;
    for (const raid::Branch* b : Branch::all()) {
        if (b->part == kHeartPart) continue;  // the heart is its own section, not one of the seven branches
        ++a.branches;
        a.zones += static_cast<int>(b->zones.size());
        float total = 0.0f;
        for (const auto& [id, m] : zoneMinutes(b->part)) total += m;
        for (const ZoneInfo& z : b->zones)
            if (z.boss) ++a.lairs;
        a.minMinutes = first ? total : std::min(a.minMinutes, total);
        a.maxMinutes = first ? total : std::max(a.maxMinutes, total);
        first = false;
    }
    if (a.branches == 0) out.problems.push_back("aucune branche du titan");
    // Its rewards: the badges of its gigaboss, then the titles and suits of the raid.
    for (const AchievementDef& d : achievements.achievements())
        if (d.condition.type == "raid_reward") a.rewards.push_back(badgeReward(d));
    for (const CosmeticItem& c : cosmetics.items())
        if (c.raid == "asterion") a.rewards.push_back(itemReward(c));
    std::stable_sort(a.rewards.begin(), a.rewards.end(), [](const SheetReward& x, const SheetReward& y) { return kindOrder(x.kind) < kindOrder(y.kind); });
    return out;
}

RaidSheets loadRaidSheets(const ContentDatabase& content, const AchievementCatalog& achievements, const CosmeticCatalog& cosmetics) {
    auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion_sheets.json");
    if (!j || !j->is_object()) {
        RaidSheets out;
        out.problems.push_back("les mots des fiches sont illisibles");
        return out;
    }
    return buildRaidSheets(*j, content, achievements, cosmetics);
}

}  // namespace cosmic::raid
