#include "saves/RaidHudLayout.h"

#include <algorithm>
#include <cmath>

namespace cosmic {

const char* RaidHudLayout::id(RaidHudElement e) {
    switch (e) {
        case RaidHudElement::Vitals: return "vitals";
        case RaidHudElement::Powers: return "powers";
        case RaidHudElement::Minimap: return "minimap";
        case RaidHudElement::Group: return "group";
        case RaidHudElement::Objectives: return "objectives";
        case RaidHudElement::Alerts: return "alerts";
        case RaidHudElement::Boss: return "boss";
        case RaidHudElement::Danger: return "danger";
        case RaidHudElement::Contributions: return "contributions";
        case RaidHudElement::Status: return "status";
        case RaidHudElement::Timer: return "timer";
        default: return "";
    }
}

const char* RaidHudLayout::label(RaidHudElement e) {
    switch (e) {
        case RaidHudElement::Vitals: return "Vie et énergie";
        case RaidHudElement::Powers: return "Pouvoirs";
        case RaidHudElement::Minimap: return "Minimap";
        case RaidHudElement::Group: return "Panneau du groupe";
        case RaidHudElement::Objectives: return "Objectifs et branche";
        case RaidHudElement::Alerts: return "Alertes du groupe";
        case RaidHudElement::Boss: return "Barre du gigaboss";
        case RaidHudElement::Danger: return "Bandeau de danger";
        case RaidHudElement::Contributions: return "Contributions";
        case RaidHudElement::Status: return "Réseau · badge TEST ADMIN";
        case RaidHudElement::Timer: return "Chronomètre du raid";
        default: return "";
    }
}

HudElement RaidHudLayout::hudElement(RaidHudElement e) {
    switch (e) {
        case RaidHudElement::Vitals: return HudElement::Vitals;
        case RaidHudElement::Powers: return HudElement::Powers;
        case RaidHudElement::Boss: return HudElement::Boss;
        default: return HudElement::Count;
    }
}

Json RaidHudLayout::toJson() const {
    Json j = Json::object();
    for (int i = 0; i < kRaidHudElements; ++i) {
        const HudPlace& p = items[static_cast<size_t>(i)];
        Json e = {{"scale", p.scale}, {"visible", p.visible}};
        if (p.custom) {
            e["x"] = p.x;
            e["y"] = p.y;
        }
        j[id(static_cast<RaidHudElement>(i))] = e;
    }
    j["arrows"] = arrows;
    return j;
}

RaidHudLayout RaidHudLayout::fromJson(const Json& j) {
    RaidHudLayout l;
    if (!j.is_object()) return l;
    auto finite = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    for (int i = 0; i < kRaidHudElements; ++i) {
        const auto e = static_cast<RaidHudElement>(i);
        const char* key = id(e);
        if (!j.contains(key) || !j[key].is_object()) continue;
        const Json& o = j[key];
        HudPlace& p = l.items[static_cast<size_t>(i)];
        p.scale = finite(jget<float>(o, "scale", 1.0f), HudLayout::kMinScale, HudLayout::kMaxScale, 1.0f);
        p.visible = !canHide(e) || jget<bool>(o, "visible", true);
        if (o.contains("x") && o.contains("y") && o["x"].is_number() && o["y"].is_number()) {
            p.custom = true;
            p.x = finite(o["x"].get<float>(), 0.0f, 1.0f, 0.0f);
            p.y = finite(o["y"].get<float>(), 0.0f, 1.0f, 0.0f);
        }
    }
    l.arrows = jget<bool>(j, "arrows", true);
    return l;
}

}  // namespace cosmic
