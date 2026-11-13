#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>

#include "engine/core/Json.h"

namespace cosmic {

/// Logical actions. Gameplay actions are rebindable; menu actions have fixed defaults
/// plus whatever device the player uses (keyboard arrows/Enter, gamepad D-pad/A/B).
enum class Action : int {
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Jump,
    Attack,
    Dash,
    Wave,
    Shield,
    Impulse,
    Teleport,
    Interact,
    Pause,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    MenuConfirm,
    MenuBack,
    MenuTabPrev,
    MenuTabNext,
    Count
};

constexpr int kActionCount = static_cast<int>(Action::Count);
constexpr int kFirstMenuAction = static_cast<int>(Action::MenuUp);

const char* actionId(Action a);
const char* actionLabel(Action a);  // French display name
bool actionIsRebindable(Action a);
Action actionFromId(const std::string& id);

enum class InputDevice { Keyboard, Gamepad };
enum class GamepadStyle { Xbox, PlayStation, Generic };

/// A gamepad binding: a button, or one direction of an axis (sticks / triggers).
struct PadBinding {
    enum class Kind { None, Button, AxisPositive, AxisNegative };
    Kind kind = Kind::None;
    int code = 0;  // SDL_GamepadButton or SDL_GamepadAxis

    static PadBinding button(SDL_GamepadButton b) { return {Kind::Button, static_cast<int>(b)}; }
    static PadBinding axisPos(SDL_GamepadAxis a) { return {Kind::AxisPositive, static_cast<int>(a)}; }
    static PadBinding axisNeg(SDL_GamepadAxis a) { return {Kind::AxisNegative, static_cast<int>(a)}; }
    bool operator==(const PadBinding&) const = default;
};

struct GamepadSettings {
    float stickDeadzone = 0.22f;
    float triggerThreshold = 0.35f;
    float sensitivity = 1.0f;  // response curve exponent divisor (higher = more responsive)
    bool invertX = false;
    bool invertY = false;
    bool rumble = true;
    bool swapSticks = false;
};

/// Complete control configuration (keyboard/mouse and gamepad are independent).
struct InputBindings {
    std::array<std::array<SDL_Scancode, 2>, kActionCount> keys{};
    std::array<std::array<PadBinding, 2>, kActionCount> pad{};
    GamepadSettings gamepad;

    static InputBindings defaults();
    void resetKeyboard();
    void resetGamepad();
    Json toJson() const;
    static InputBindings fromJson(const Json& j);
};

/// Index in the glyph sheets (assets/ui/glyphs/*.png, docs/ASSET_SPEC.md section 11).
int glyphIndex(const PadBinding& b);
std::string padBindingName(const PadBinding& b, GamepadStyle style);
std::string keyName(SDL_Scancode sc);

}  // namespace cosmic
