#include "game/powers/PowerStats.h"

#include <cmath>
#include <cstdio>

#include "game/powers/PowerDefinition.h"

namespace cosmic {

namespace {

/// "0,85" style: French decimal comma, no useless zeros.
std::string number(float v) {
    char b[32];
    if (std::abs(v - std::round(v)) < 0.001f) std::snprintf(b, sizeof b, "%d", static_cast<int>(std::lround(v)));
    else if (std::abs(v * 10.0f - std::round(v * 10.0f)) < 0.001f) std::snprintf(b, sizeof b, "%.1f", v);
    else std::snprintf(b, sizeof b, "%.2f", v);
    std::string s = b;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

/// "0,4 → 2,0 s": both ends with the same decimals.
std::string span(float a, float b, const char* unit) {
    std::string x = number(a), y = number(b);
    bool decimals = x.find(',') != std::string::npos || y.find(',') != std::string::npos;
    auto oneDecimal = [](float v) {
        char t[32];
        std::snprintf(t, sizeof t, "%.1f", v);
        std::string s = t;
        for (char& c : s)
            if (c == '.') c = ',';
        return s;
    };
    if (decimals && x.find(',') == std::string::npos && std::abs(a * 10.0f - std::round(a * 10.0f)) < 0.001f) x = oneDecimal(a);
    if (decimals && y.find(',') == std::string::npos && std::abs(b * 10.0f - std::round(b * 10.0f)) < 0.001f) y = oneDecimal(b);
    return x + " → " + y + unit;
}

struct Known {
    const char* key;
    const char* label;
    const char* unit;  // appended after the number ("" for none)
};

// In the order they are shown (after the energy cost, before the recharge).
const Known kKnown[] = {
    {"chargeMax", "Charge max", " s"},
    {"spinTimes", "Rotation", " s"},  // a list (tap … full charge): first → last
    {"damage", "Dégâts", ""},
    {"diveDamage", "Dégâts du piqué", ""},
    {"radius", "Rayon", " px"},
    {"range", "Portée", " px"},
    {"distance", "Distance", " px"},
    {"pierce", "Ennemis traversés", ""},
    {"bounces", "Rebonds", ""},
    {"chargeTime", "Charge", " s"},
    {"fuse", "Mèche", " s"},
    {"duration", "Durée", " s"},
    {"freeze", "Ennemis gelés", " s"},
    {"bossSlow", "Boss ralentis", " s"},
    {"window", "Fenêtre de retour", " s"},
    {"invulnerability", "Invulnérable à l'arrivée", " s"},
    {"factor", "Vitesse du temps", ""},
    {"speed", "Vitesse", " px/s"},
    {"velocity", "Élan", " px/s"},
    {"diveSpeed", "Vitesse du piqué", " px/s"},
    {"knockback", "Repousse", " px/s"},
    {"airUses", "En l'air", " fois"},
    {"jumps", "Sauts en plus", ""},
    {"dashes", "Ruées en l'air", ""},
    {"fallSpeed", "Chute en planant", " px/s"},
    {"airSpeed", "Vitesse en planant", " px/s"},
    {"hookSpeed", "Vitesse du grappin", " px/s"},
    {"pullSpeed", "Traction", " px/s"},
    {"releaseBoost", "Élan au lâcher", " px/s"},
    {"recoil", "Recul", " px/s"},
    {"hover", "Suspension", " s"},
};

}  // namespace

std::vector<PowerStat> powerStats(const PowerDefinition& d) {
    std::vector<PowerStat> out;
    out.push_back({"energy", "Énergie", d.energyCost > 0.0f ? number(d.energyCost) : "gratuit"});
    // A power whose first press is free (the recall beacon): what each press costs.
    if (d.params.contains("placeCost") && d.params["placeCost"].is_number()) {
        float place = d.params["placeCost"].get<float>();
        out.front().label = "Énergie au retour";
        out.push_back({"placeCost", "Poser la balise", place > 0.0f ? number(place) : "gratuit"});
    }
    for (const auto& k : kKnown) {
        if (!d.params.contains(k.key)) continue;
        const Json& j = d.params[k.key];
        const std::string key = k.key;
        if (j.is_array()) {
            // A value that grows with the charge (Boomerang chargé): « 0,4 → 2,0 s ».
            std::vector<float> v;
            for (const auto& x : j)
                if (x.is_number()) v.push_back(x.get<float>());
            if (v.empty()) continue;
            out.push_back({key, k.label, v.size() == 1 ? number(v.front()) + k.unit : span(v.front(), v.back(), k.unit)});
            continue;
        }
        if (!j.is_number()) continue;
        float v = j.get<float>();
        std::string value = number(v) + k.unit;
        if (key == "factor") value = "×" + number(v);
        // Reach that grows with the charge: « 260 → 360 px ».
        if (key == "range" && d.params.contains("rangeMax") && d.params["rangeMax"].is_number())
            value = span(v, d.params["rangeMax"].get<float>(), k.unit);
        // Repeated blows: « 1 + 1 toutes les 0,3 s ».
        if (key == "damage" && d.params.contains("tickInterval") && d.params["tickInterval"].is_number())
            value = number(v) + " + " + number(v) + " toutes les " + number(d.params["tickInterval"].get<float>()) + " s";
        out.push_back({key, k.label, value});
    }
    if (d.duration > 0.0f && !d.params.contains("duration")) out.push_back({"duration", "Durée", number(d.duration) + " s"});
    if (d.cooldown > 0.0f) out.push_back({"cooldown", "Recharge", number(d.cooldown) + " s"});
    return out;
}

std::string powerStatsLine(const PowerDefinition& d, size_t count) {
    std::string line;
    size_t n = 0;
    for (const auto& s : powerStats(d)) {
        if (n++ >= count) break;
        if (!line.empty()) line += "  ·  ";
        line += s.key == "energy" ? (d.energyCost > 0.0f ? s.value + " énergie" : "Sans énergie") : s.label + " " + s.value;
    }
    return line;
}

}  // namespace cosmic
