#include "game/raid/ZoneGoals.h"

#include <algorithm>
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
    // Its objectives (the required ones first, the optional ones last).
    for (int pass = 0; pass < 2; ++pass)
        for (const ObjectiveInfo& o : b.objectives) {
            if (o.zone != zone || o.optional != (pass == 1)) continue;
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

std::vector<ZoneGoals> branchGoals(const Branch& b, uint32_t validated, uint32_t own) {
    std::vector<ZoneGoals> out;
    for (const ZoneInfo& z : b.zones) out.push_back(zoneGoals(b, z.id, validated, own));
    std::stable_sort(out.begin(), out.end(), [](const ZoneGoals& a, const ZoneGoals& c) { return !a.lair && c.lair; });
    return out;
}

}  // namespace cosmic::raid
