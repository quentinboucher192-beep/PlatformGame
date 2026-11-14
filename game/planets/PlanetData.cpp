#include "game/planets/PlanetData.h"

namespace cosmic {

namespace {
struct Info {
    const char* id;
    const char* label;
};
constexpr Info kTypes[] = {{"alien", "Planète extraterrestre"}, {"volcanic", "Planète volcanique"},
                           {"ice", "Planète glacée"},           {"desert", "Planète désertique"},
                           {"forest", "Planète forestière"},    {"tech", "Planète technologique"},
                           {"star", "Étoile"}};
}  // namespace

const char* planetTypeId(PlanetType t) { return kTypes[static_cast<int>(t)].id; }
const char* planetTypeLabel(PlanetType t) { return kTypes[static_cast<int>(t)].label; }
PlanetType planetTypeFromId(const std::string& s) {
    for (int i = 0; i < static_cast<int>(PlanetType::Count); ++i)
        if (s == kTypes[i].id) return static_cast<PlanetType>(i);
    return PlanetType::Alien;
}

Json PlanetData::toJson() const {
    return {{"format", "cosmic.planet"},
            {"id", id},
            {"name", name},
            {"subtitle", subtitle},
            {"description", description},
            {"type", planetTypeId(type)},
            {"difficulty", difficulty},
            {"color", color.toHex()},
            {"atmosphere", atmosphere.toHex()},
            {"illustration", illustration},
            {"banner", banner},
            {"music", music},
            {"levels", levels},
            {"unlock", unlock},
            {"teaser", teaser},
            {"orbit", {{"radius", orbitRadius}, {"speed", orbitSpeed}, {"phase", orbitPhase}}},
            {"size", size}};
}

PlanetData PlanetData::fromJson(const Json& j) {
    PlanetData p;
    p.id = jstr(j, "id");
    p.name = jstr(j, "name", p.id);
    p.subtitle = jstr(j, "subtitle");
    p.description = jstr(j, "description");
    p.type = planetTypeFromId(jstr(j, "type", "alien"));
    p.difficulty = std::clamp(jget<int>(j, "difficulty", 1), 1, 5);
    p.color = colorFromJson(j.value("color", Json()), p.color);
    p.atmosphere = colorFromJson(j.value("atmosphere", Json()), p.atmosphere);
    p.illustration = jstr(j, "illustration");
    p.banner = jstr(j, "banner");
    p.music = jstr(j, "music");
    if (j.contains("levels") && j["levels"].is_array())
        for (const auto& l : j["levels"])
            if (l.is_string()) p.levels.push_back(l.get<std::string>());
    p.unlock = jstr(j, "unlock", "always");
    p.teaser = jget<bool>(j, "teaser", false);
    if (j.contains("orbit")) {
        p.orbitRadius = jget<float>(j["orbit"], "radius", 300.0f);
        p.orbitSpeed = jget<float>(j["orbit"], "speed", 0.05f);
        p.orbitPhase = jget<float>(j["orbit"], "phase", 0.0f);
    }
    p.size = std::clamp(jget<float>(j, "size", 1.0f), 0.2f, 3.0f);
    return p;
}

}  // namespace cosmic
