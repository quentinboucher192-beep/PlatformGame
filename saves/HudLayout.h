#pragma once

#include <array>

#include "engine/core/Json.h"

namespace cosmic {

/// Elements of the in-game HUD that the player can place and size (Paramètres › HUD).
enum class HudElement : int { Vitals, Powers, Collectibles, Timer, Progress, Boss, Count };
constexpr int kHudElements = static_cast<int>(HudElement::Count);

/// Where an element goes. `custom` false: the game's default place; true: the element's
/// anchor at (x, y), fractions of the screen size (so the layout follows the resolution).
/// The size is a factor of the default size (0.5 to 2).
struct HudPlace {
    bool custom = false;
    float x = 0.0f, y = 0.0f;
    float scale = 1.0f;
    bool visible = true;  // the timer and the progress bar use their own settings
};

struct HudLayout {
    std::array<HudPlace, kHudElements> items{};

    HudPlace& operator[](HudElement e) { return items[static_cast<size_t>(e)]; }
    const HudPlace& operator[](HudElement e) const { return items[static_cast<size_t>(e)]; }

    static const char* id(HudElement e);
    static const char* label(HudElement e);
    static constexpr float kMinScale = 0.5f, kMaxScale = 2.0f;

    Json toJson() const;
    static HudLayout fromJson(const Json& j);
};

}  // namespace cosmic
