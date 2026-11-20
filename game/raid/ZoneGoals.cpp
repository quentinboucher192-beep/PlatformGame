#include "game/raid/ZoneGoals.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "game/raid/RaidBranch.h"

namespace cosmic::raid {

namespace {

/// The label without its « (…) » precision (« Fermer la valve haute (un groupe) » → « Fermer la valve haute »).
std::string shortLabel(const std::string& label) {
    const size_t cut = label.find(" (");
    return cut == std::string::npos ? label : label.substr(0, cut);
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

}  // namespace

std::string ZoneGoals::missing() const {
    if (finished) return {};
    std::string out;
    for (const Requirement& r : items) {
        if (r.done || (r.optional && !optional)) continue;
        if (!out.empty()) out += " · ";
        out += r.progress.empty() ? r.text : r.text + " (" + r.progress + ")";
    }
    return out;
}

bool zoneFinished(const Branch& b, int zone, uint32_t mask) {
    bool required = false, any = false, all = true;
    for (const ObjectiveInfo& o : b.objectives) {
        if (o.zone != zone) continue;
        any = true;
        const bool d = Branch::done(mask, o.index);
        all = all && d;
        if (o.optional) continue;
        required = true;
        if (!d) return false;
    }
    return required || (any && all);
}

ZoneGoals zoneGoals(const Branch& b, int zone, uint32_t validated, uint32_t own) {
    ZoneGoals g;
    g.zone = zone;
    const ZoneInfo* z = b.zone(zone);
    const uint32_t mask = validated | own;
    const std::vector<bool> reach = b.reachable(mask);
    g.reachable = zone >= 0 && static_cast<size_t>(zone) < reach.size() && reach[static_cast<size_t>(zone)];
    g.name = z ? (z->secret && !g.reachable ? std::string("Zone secrète") : z->name) : "Zone " + std::to_string(zone);
    g.lair = z && z->boss;
    g.finished = zoneFinished(b, zone, mask);
    bool required = false;
    for (const ObjectiveInfo& o : b.objectives) required = required || (o.zone == zone && !o.optional);
    g.optional = !required;
    // What opens it: the locks of the passages that lead to it from a zone already open.
    if (!g.reachable) {
        std::set<std::string> locks;
        for (const EdgeInfo& e : b.edges) {
            const int other = e.a == zone ? e.b : e.b == zone ? e.a : -1;
            if (other < 0 || static_cast<size_t>(other) >= reach.size() || !reach[static_cast<size_t>(other)]) continue;
            if (!e.lock.empty() && !b.lockOpen(e.lock, mask)) locks.insert(e.lock);
            if (e.lock.empty() && e.secret) locks.insert("secret");
        }
        for (const std::string& lock : locks) {
            Requirement r;
            r.access = true;
            if (lock == "secret") {
                r.text = "Trouver le passage caché qui y mène";
            } else if (lock == "generators") {
                r.text = "Ouvrir l'accès : " + b.lockText(lock).objective;
                r.progress = std::to_string(b.generators(mask)) + " / " + std::to_string(b.generatorsNeeded);
            } else if (lock.rfind("ambushes:", 0) == 0) {
                // 2.78: the lair's door waits for every ambush of the zone before it.
                r.text = "Ouvrir l'accès : " + Branch::ambushesLeftText(b.ambushes(std::atoi(lock.c_str() + 9), mask));
            } else if (lock.rfind("objective:", 0) == 0) {
                const ObjectiveInfo* o = b.objective(lock.substr(10));
                r.text = "Ouvrir l'accès : " + (o ? shortLabel(o->label) : std::string("un objectif de la branche"));
            } else {
                const std::string why = b.lockText(lock).objective;
                r.text = "Ouvrir l'accès : " + (why.empty() ? std::string("passage verrouillé") : why);
            }
            g.items.push_back(std::move(r));
        }
    }
    // 2.78: its ambushes (the zone before a lair): one line, « Embuscades restantes : 2 sur 4 ». Titan 9: every
    // zone with an ambush (the entry's guard too): its way on opens with them.
    const Branch::AmbushCount amb = b.ambushes(zone, mask);
    if (amb.total > 0) {
        Requirement r;
        r.done = amb.left == 0;
        r.pending = r.done && b.ambushes(zone, validated).left > 0;
        const char* open = b.ambushesOpenLair(zone) ? "Toutes les embuscades sont vaincues : le passage vers l'antre est ouvert"
                                                    : "Toutes les embuscades sont vaincues : les passages vers la suite sont ouverts";
        r.text = r.done ? std::string(open) : Branch::ambushesLeftText(amb);
        g.items.push_back(std::move(r));
    }
    // Its objectives (the required ones first, the optional ones last).
    for (int pass = 0; pass < 2; ++pass)
        for (const ObjectiveInfo& o : b.objectives) {
            if (o.zone != zone || o.optional != (pass == 1) || o.role == "ambush") continue;
            Requirement r;
            r.optional = o.optional;
            r.text = o.optional ? "Facultatif : " + shortLabel(o.label.rfind("Facultatif : ", 0) == 0 ? o.label.substr(13) : o.label) : o.label;
            r.done = Branch::done(mask, o.index);
            r.pending = r.done && !Branch::done(validated, o.index);
            if (!o.pair.empty()) {
                int n = 0, total = 0;
                for (const ObjectiveInfo& x : b.objectives)
                    if (x.pair == o.pair) {
                        ++total;
                        n += Branch::done(mask, x.index);
                    }
                r.progress = std::to_string(n) + " / " + std::to_string(total);
            } else if (o.role == "generator") {
                const int n = b.generators(mask);
                r.progress = std::to_string(n) + " / " + std::to_string(b.generatorsNeeded);
                r.text = o.label + " · " + plural(n, "générateur", "générateurs") + " sur " + std::to_string(b.generatorsNeeded);
            }
            g.items.push_back(std::move(r));
        }
    return g;
}

int RouteLeft::metres() const { return static_cast<int>(std::lround(pixels / 16.0f)); }

RouteLeft routeLeft(const std::vector<Vec2>& route, Vec2 feet, Vec2 target) {
    RouteLeft out;
    if (route.size() < 2) {
        const float dx = target.x - feet.x;
        out.pixels = std::fabs(dx);
        out.dir = {dx < 0.0f ? -1.0f : 1.0f, 0.0f};
        return out;
    }
    size_t best = 0;
    float bestD = -1.0f, bestT = 0.0f;
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        const Vec2 a = route[i], d = route[i + 1] - route[i];
        const float len2 = d.x * d.x + d.y * d.y;
        const float t = len2 > 0.0f ? std::clamp(((feet.x - a.x) * d.x + (feet.y - a.y) * d.y) / len2, 0.0f, 1.0f) : 0.0f;
        const Vec2 q{a.x + d.x * t, a.y + d.y * t};
        const float dist = std::sqrt((feet.x - q.x) * (feet.x - q.x) + (feet.y - q.y) * (feet.y - q.y));
        if (bestD < 0.0f || dist < bestD - 0.01f) {
            best = i;
            bestD = dist;
            bestT = t;
        }
    }
    const Vec2 a = route[best], b = route[best + 1], q{a.x + (b.x - a.x) * bestT, a.y + (b.y - a.y) * bestT};
    auto len = [](Vec2 u, Vec2 v) { return std::sqrt((u.x - v.x) * (u.x - v.x) + (u.y - v.y) * (u.y - v.y)); };
    float left = len(q, b);
    for (size_t i = best + 1; i + 1 < route.size(); ++i) left += len(route[i], route[i + 1]);
    out.pixels = left + bestD;
    out.along = true;
    // Where to go: toward the end of this segment (the next one when it is already there).
    Vec2 next = b;
    if (len(feet, b) < 24.0f && best + 2 < route.size()) next = route[best + 2];
    const float dl = len(feet, next);
    out.dir = dl > 0.5f ? Vec2{(next.x - feet.x) / dl, (next.y - feet.y) / dl} : Vec2{1.0f, 0.0f};
    return out;
}

std::vector<ZoneGoals> branchGoals(const Branch& b, uint32_t validated, uint32_t own) {
    std::vector<ZoneGoals> out;
    for (const ZoneInfo& z : b.zones) out.push_back(zoneGoals(b, z.id, validated, own));
    std::stable_sort(out.begin(), out.end(), [](const ZoneGoals& a, const ZoneGoals& c) { return !a.lair && c.lair; });
    if (b.hasLairArmory()) {
        // 2.78: the lair's armory, between the zone before the lair and the lair: a zone of passage.
        ZoneGoals g;
        g.zone = kLairArmory;
        g.name = b.lairArmoryInfo.name;
        g.passage = g.optional = true;
        for (const ZoneGoals& z : out)
            if (z.lair) g.reachable = z.reachable;
        Requirement r;
        r.optional = true;
        r.text = "Zone de passage : ajuster ses pouvoirs pour " + (b.gigaboss.empty() ? std::string("le gigaboss") : b.gigaboss) +
                 ", puis tous les joueurs présents sur la plaque « Prêts »";
        g.items.push_back(std::move(r));
        auto at = std::find_if(out.begin(), out.end(), [](const ZoneGoals& z) { return z.lair; });
        out.insert(at, std::move(g));
    }
    return out;
}

}  // namespace cosmic::raid
