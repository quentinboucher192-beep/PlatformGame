#include "game/levels/CampaignPipeline.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "game/content/ContentDatabase.h"
#include "game/levels/LevelAnalyzer.h"

namespace cosmic {

namespace {

/// The names of the powers on the station signs, as tools/levelgen/levelkit.py writes them.
std::string powerPhrase(const ContentDatabase& content, const std::string& id) {
    static const std::map<std::string, std::string> kNames = {
        {"dash", "le Dash"},           {"wave", "l'Onde"},
        {"shield", "le Bouclier"},     {"impulse", "l'Impulsion"},
        {"teleport", "la Téléportation"}, {"glide", "le Planeur"},
        {"grapple", "le Grappin"},     {"ground_pound", "la Frappe sismique"},
        {"charge_shot", "le Tir chargé"}, {"triple_jump", "le Triple saut"},
        {"time_slow", "le Ralenti"},   {"magnet", "l'Aimant"},
        {"air_dash", "la Ruée aérienne"}, {"plasma_bomb", "la Bombe plasma"},
        {"boomerang", "la Lame boomerang"}, {"frost_nova", "la Nova de givre"},
        {"decoy", "le Leurre holographique"}, {"recall", "la Balise de rappel"},
        {"boomerang_charge", "le Boomerang chargé"},
    };
    if (auto it = kNames.find(id); it != kNames.end()) return it->second;
    if (const PowerDefinition* p = content.power(id)) return p->name;
    return id;
}

std::string freeId(const LevelData& level, const std::string& base, int& n) {
    std::string id;
    do id = base + std::to_string(++n);
    while (level.findObject(id));
    return id;
}

/// The stations planned before (tablets and signs of levelkit.apply_power_slots).
bool isStation(const LevelObject& o) {
    if (o.type == "power_unlock" && o.prop<bool>("station", false)) return true;
    return o.type == "sign" && o.id.rfind("station_sign_", 0) == 0;
}

}  // namespace

LevelData prepareCampaignRevision(const LevelData& source, const ContentDatabase& content, int revision, CampaignPipelineReport* report) {
    CampaignPipelineReport rep;
    LevelData level = source;

    // 1. Seals (--seal-levels + levelkit.apply_seals): gates against the shortcuts that skip a
    // checkpoint, of the level's most common terrain material; the gates already there stay.
    {
        LevelAnalysis a = analyzeLevel(level, content);
        std::vector<Rect> gates = suggestGates(level, a);
        std::map<std::string, int> count;
        for (const auto& o : level.objects)
            if (o.type == "solid") ++count[o.propStr("material", "ruin_stone")];
        std::string material = "ruin_stone";
        int best = 0;
        for (const auto& [m, n] : count)
            if (n > best) {
                best = n;
                material = m;
            }
        int n = 0;
        for (const auto& o : level.objects)
            if (o.id.rfind("seal_", 0) == 0) n = std::max(n, std::atoi(o.id.c_str() + 5));
        for (const Rect& g : gates) {
            bool known = std::any_of(level.objects.begin(), level.objects.end(), [&](const LevelObject& o) {
                return o.type == "solid" && o.prop<bool>("seal", false) && o.rect.x == g.x && o.rect.h == g.h;
            });
            if (known) continue;
            LevelObject o;
            o.id = freeId(level, "seal_", n);
            o.type = "solid";
            o.layer = "terrain";
            o.rect = g;
            o.props = {{"material", material}, {"seal", true}};
            level.objects.push_back(std::move(o));
            ++rep.gatesAdded;
        }
        if (!a.reachable) rep.notes.push_back("La sortie n'est pas atteignable d'après l'analyseur : vérifiez le parcours.");
        for (const auto& p : a.problems)
            if (rep.notes.size() < 6) rep.notes.push_back(p);
    }

    // 2. Power stations (--plan-stations + levelkit.apply_power_slots): the generator builds a
    // level without its stations, then places those of the plan: the stations of before are
    // removed and the plan is made on the edited route (the same level always gets the same
    // stations, however many times it is published).
    if (level.powerSlots() > 0) {
        size_t before = level.objects.size();
        level.objects.erase(std::remove_if(level.objects.begin(), level.objects.end(), isStation), level.objects.end());
        rep.stationsRemoved = static_cast<int>(before - level.objects.size());
        auto stations = planPowerStations(level, content);
        int signN = 0;
        for (size_t i = 0; i < stations.size(); ++i) {
            const PowerStation& st = stations[i];
            for (size_t j = 0; j < st.tablets.size(); ++j) {
                const auto& [power, c] = st.tablets[j];
                LevelObject t;
                t.id = "station_" + std::to_string(i + 1) + "_" + std::to_string(j + 1);
                if (level.findObject(t.id)) t.id += "_r" + std::to_string(revision);
                t.type = "power_unlock";
                t.layer = "gameplay";
                t.rect = {std::round(c.x) - 12.0f, std::round(c.y) - 12.0f, 24.0f, 24.0f};
                t.props = {{"power", power}, {"message", ""}, {"station", true}};
                level.objects.push_back(std::move(t));
                ++rep.tablets;
            }
            std::string names;
            for (const auto& p : st.upcoming)
                if (!p.empty()) names += (names.empty() ? "" : ", ") + powerPhrase(content, p);
            std::string text = st.midSection ? "Station de pouvoirs : il faut maintenant " + names + ". Le pouvoir que vous laissez reste ici."
                                             : "Station de pouvoirs. Pour la suite : " + names + ". Vous portez " +
                                                   std::to_string(level.powerSlots()) + " pouvoirs au plus : celui que vous laissez reste ici.";
            LevelObject s;
            s.id = "station_sign_" + std::to_string(i + 1);
            if (level.findObject(s.id)) s.id = freeId(level, "station_sign_", signN);
            s.type = "sign";
            s.layer = "gameplay";
            s.rect = {std::round(st.sign.x) - 12.0f, std::round(st.sign.y) - 24.0f, 24.0f, 24.0f};
            s.props = {{"text", text}, {"auto", true}};
            level.objects.push_back(std::move(s));
            ++rep.stations;
        }
    }

    // 3. Its revision (part of its fingerprint) and its rating (--rate-levels).
    if (!level.metadata.is_object()) level.metadata = Json::object();
    level.metadata["revision"] = revision;
    level.metadata.erase("savedAt");
    rep.rating = rateDifficulty(level, content);
    if (report) *report = std::move(rep);
    return level;
}

}  // namespace cosmic
