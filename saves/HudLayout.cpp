#include "saves/HudLayout.h"

#include <algorithm>
#include <cmath>

namespace cosmic {

const char* HudLayout::id(HudElement e) {
    switch (e) {
        case HudElement::Vitals: return "vitals";
        case HudElement::Powers: return "powers";
        case HudElement::Collectibles: return "collectibles";
        case HudElement::Timer: return "timer";
        case HudElement::Progress: return "progress";
        case HudElement::Boss: return "boss";
        default: return "";
    }
}

const char* HudLayout::label(HudElement e) {
    switch (e) {
        case HudElement::Vitals: return "Vie et énergie";
        case HudElement::Powers: return "Pouvoirs";
        case HudElement::Collectibles: return "Éclats et reliques";
        case HudElement::Timer: return "Chronomètre";
        case HudElement::Progress: return "Barre de progression";
        case HudElement::Boss: return "Vie du boss";
        default: return "";
    }
}

Json HudLayout::toJson() const {
    Json j = Json::object();
    for (int i = 0; i < kHudElements; ++i) {
        const HudPlace& p = items[static_cast<size_t>(i)];
        Json e = {{"scale", p.scale}, {"visible", p.visible}};
        if (p.custom) {
            e["x"] = p.x;
            e["y"] = p.y;
        }
        j[id(static_cast<HudElement>(i))] = e;
    }
    return j;
}

HudLayout HudLayout::fromJson(const Json& j) {
    HudLayout l;
    if (!j.is_object()) return l;
    auto finite = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    for (int i = 0; i < kHudElements; ++i) {
        const char* key = id(static_cast<HudElement>(i));
        if (!j.contains(key) || !j[key].is_object()) continue;
        const Json& e = j[key];
        HudPlace& p = l.items[static_cast<size_t>(i)];
        p.scale = finite(jget<float>(e, "scale", 1.0f), kMinScale, kMaxScale, 1.0f);
        p.visible = jget<bool>(e, "visible", true);
        if (e.contains("x") && e.contains("y") && e["x"].is_number() && e["y"].is_number()) {
            p.custom = true;
            p.x = finite(e["x"].get<float>(), 0.0f, 1.0f, 0.0f);
            p.y = finite(e["y"].get<float>(), 0.0f, 1.0f, 0.0f);
        }
    }
    return l;
}

}  // namespace cosmic
