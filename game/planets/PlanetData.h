#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"

namespace cosmic {

enum class PlanetType { Alien, Volcanic, Ice, Desert, Forest, Tech, Count };
const char* planetTypeId(PlanetType t);
const char* planetTypeLabel(PlanetType t);
PlanetType planetTypeFromId(const std::string& s);

/// A planet of the star map (data/planets/<id>.json). Planets are pure data: new ones
/// can be added (or imported with a level package) without touching the code.
struct PlanetData {
    std::string id;
    std::string name;
    std::string subtitle;
    std::string description;
    PlanetType type = PlanetType::Alien;
    int difficulty = 1;
    Color color{57, 240, 216, 255};
    Color atmosphere{57, 240, 216, 120};
    std::string illustration;  // ui/planets/<id>.png
    std::string banner;
    std::string music;         // planet theme (level select)
    std::vector<std::string> levels;
    std::string unlock = "always";  // always | planet:<id> | level:<id> | never
    bool teaser = false;            // shown as an undiscovered world
    float orbitRadius = 300.0f;
    float orbitSpeed = 0.05f;
    float orbitPhase = 0.0f;
    float size = 1.0f;
    std::string sourcePath;
    fs::path assetRoot;

    Json toJson() const;
    static PlanetData fromJson(const Json& j);
};

}  // namespace cosmic
