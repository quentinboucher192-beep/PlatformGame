#pragma once

#include <string>

#include "engine/core/Json.h"
#include "engine/input/InputTypes.h"

namespace cosmic {

/// Data description of a power (data/powers/<id>.json). `behavior` selects the C++
/// implementation (dash, wave, shield, impulse, teleport); everything else is tunable,
/// so new powers can be created as variants without touching the code.
struct PowerDefinition {
    std::string id;
    std::string behavior;
    std::string name;
    std::string description;
    int icon = 0;  // index in ui/icons_powers.png
    /// Its button before 2.24 (one per power), still what old replays press; what the
    /// player presses now is the key of the power's slot (Power::key()), or the move of
    /// the character for a power on a move (grapple: Interact).
    Action input = Action::Dash;
    float energyCost = 20.0f;
    float cooldown = 1.0f;
    float duration = 0.0f;
    Color color{57, 240, 216, 255};
    std::string sound;
    std::string soundEnd;
    Json params = Json::object();
    std::string sourcePath;

    float param(const char* key, float def) const { return jget<float>(params, key, def); }
    Json toJson() const;
    static PowerDefinition fromJson(const Json& j);
};

}  // namespace cosmic
