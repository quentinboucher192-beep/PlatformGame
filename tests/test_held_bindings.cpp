// The binding lists (Paramètres › Clavier / souris and Manette, « Touches de ce niveau ») light
// the key, mouse button or gamepad button / trigger / stick direction held right now: a check
// that the key is the right one, read raw from the devices (never the commands).

#include "engine/input/HeldBindings.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("held bindings: keyboard keys") {
    HeldInputs in;
    CHECK(!keyBindingHeld(SDL_SCANCODE_SPACE, in));
    in.keys.set(SDL_SCANCODE_SPACE);
    CHECK(keyBindingHeld(SDL_SCANCODE_SPACE, in));
    CHECK(!keyBindingHeld(SDL_SCANCODE_K, in));
    CHECK(!keyBindingHeld(SDL_SCANCODE_UNKNOWN, in));  // no key: never lit
    in.keys.set(SDL_SCANCODE_LSHIFT);
    CHECK(keyBindingHeld(SDL_SCANCODE_LSHIFT, in));
    CHECK(!keyBindingHeld(SDL_SCANCODE_RSHIFT, in));  // the physical key, not the modifier
}

TEST_CASE("held bindings: mouse buttons and the wheel") {
    HeldInputs in;
    CHECK(!keyBindingHeld(mouseButtonBinding(1), in));
    in.mouseButtons = (uint64_t{1} << 1) | (uint64_t{1} << 5);
    CHECK(keyBindingHeld(mouseButtonBinding(1), in));   // left click
    CHECK(keyBindingHeld(mouseButtonBinding(5), in));   // side button
    CHECK(!keyBindingHeld(mouseButtonBinding(3), in));  // right click not held
    CHECK(!keyBindingHeld(mouseButtonBinding(4), in));
    in.mouseButtons = uint64_t{1} << 12;  // a button beyond 8 (some mice)
    CHECK(keyBindingHeld(mouseButtonBinding(12), in));
    // A mouse button is not a key: the key of the same number stays off.
    CHECK(!keyBindingHeld(static_cast<SDL_Scancode>(1), in));
    const auto up = static_cast<SDL_Scancode>(kWheelUpBinding), down = static_cast<SDL_Scancode>(kWheelDownBinding);
    CHECK(!keyBindingHeld(up, in));
    in.wheelUp = true;
    CHECK(keyBindingHeld(up, in));
    CHECK(!keyBindingHeld(down, in));
    in.wheelDown = true;
    CHECK(keyBindingHeld(down, in));
}

TEST_CASE("held bindings: gamepad buttons, triggers and sticks") {
    HeldInputs in;
    const PadBinding a = PadBinding::button(SDL_GAMEPAD_BUTTON_SOUTH);
    const PadBinding rb = PadBinding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    CHECK(!padBindingHeld(a, in));
    CHECK(!padBindingHeld(PadBinding{}, in));  // no button: never lit
    in.padButtons = uint64_t{1} << SDL_GAMEPAD_BUTTON_SOUTH;
    CHECK(padBindingHeld(a, in));
    CHECK(!padBindingHeld(rb, in));

    // Triggers: past the threshold of the settings.
    const PadBinding rt = PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    in.triggerThreshold = 0.35f;
    in.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 0.2f;
    CHECK(!padBindingHeld(rt, in));
    in.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 0.6f;
    CHECK(padBindingHeld(rt, in));
    CHECK(!padBindingHeld(PadBinding::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), in));
    in.triggerThreshold = 0.8f;
    CHECK(!padBindingHeld(rt, in));

    // Sticks: past half way, in the direction of the binding only.
    const PadBinding left = PadBinding::axisNeg(SDL_GAMEPAD_AXIS_LEFTX), right = PadBinding::axisPos(SDL_GAMEPAD_AXIS_LEFTX);
    in.axes[SDL_GAMEPAD_AXIS_LEFTX] = -0.3f;
    CHECK(!padBindingHeld(left, in));
    in.axes[SDL_GAMEPAD_AXIS_LEFTX] = -0.9f;
    CHECK(padBindingHeld(left, in));
    CHECK(!padBindingHeld(right, in));
    CHECK(!padBindingHeld(PadBinding::axisNeg(SDL_GAMEPAD_AXIS_RIGHTX), in));  // the other stick
    in.axes[SDL_GAMEPAD_AXIS_LEFTX] = 0.9f;
    CHECK(padBindingHeld(right, in));
    CHECK(!padBindingHeld(left, in));
}
