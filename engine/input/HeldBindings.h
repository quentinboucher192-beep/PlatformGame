#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <bitset>
#include <cstdint>

#include "engine/input/InputTypes.h"

namespace cosmic {

/// What is held right now on every device, read raw (never the commands): the lists of
/// bindings light the keys, mouse buttons and gamepad buttons / triggers / stick directions
/// pressed, so that the player checks a key is the right one without rebinding anything.
struct HeldInputs {
    std::bitset<SDL_SCANCODE_COUNT> keys;     // physical keys (scancodes)
    uint64_t mouseButtons = 0;                // bit n: mouse button n (1 left, 2 middle, 3 right, 4, 5...)
    bool wheelUp = false, wheelDown = false;  // a notch of the wheel a moment ago
    uint64_t padButtons = 0;                  // bit n: SDL_GamepadButton n of the player's gamepad
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> axes{};  // sticks -1..1 (after the dead zone), triggers 0..1
    float triggerThreshold = 0.35f;                     // GamepadSettings::triggerThreshold
};

/// A keyboard / mouse binding (a key, a mouse button, a notch of the wheel) is held in `in`.
bool keyBindingHeld(SDL_Scancode binding, const HeldInputs& in);
/// A gamepad binding is held in `in`: its button, its trigger past the threshold of the
/// settings, its stick past half way in that direction (the thresholds of the game).
bool padBindingHeld(const PadBinding& binding, const HeldInputs& in);

}  // namespace cosmic
