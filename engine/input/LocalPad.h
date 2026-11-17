#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

#include "engine/input/InputManager.h"
#include "engine/input/InputTypes.h"

namespace cosmic {

/// 2.62, cooperation on one computer: the gamepad of another local player (player 2 or 3),
/// read on its own with the gamepad settings of the game (InputBindings::pad, dead zone,
/// sensitivity, inversion) and its own buttons for the power slots. The InputManager never
/// switches to it (InputManager::setReservedPads), so each player keeps their own pad.
class LocalPad {
public:
    LocalPad() = default;
    ~LocalPad() { close(); }
    LocalPad(const LocalPad&) = delete;
    LocalPad& operator=(const LocalPad&) = delete;

    bool open(SDL_JoystickID id);
    void close();
    bool connected() const;
    SDL_JoystickID id() const { return id_; }
    const std::string& name() const { return name_; }
    GamepadStyle style() const { return style_; }

    /// The buttons of the power slots (PowerSlot1..8) of the powers this player carries.
    void setSlotPads(const std::array<PadBinding, kPowerSlotKeys>& pads) { slotPads_ = pads; }
    /// Samples the gamepad (once per frame, after the events): held actions and their edges.
    void update(const InputBindings& bindings, float dt);
    /// The input of the next fixed step; consumes the press / release edges.
    PlayerInput consume();
    /// This frame: the menu buttons (lobby), South / East and Start.
    bool confirmPressed() const { return confirm_; }
    bool backPressed() const { return back_; }
    bool startPressed() const { return start_; }
    /// This frame: up / down on the d-pad or the left stick (choices in a list).
    bool navUp() const { return navUp_; }
    bool navDown() const { return navDown_; }
    bool heldJump() const { return held_[static_cast<size_t>(Action::Jump)]; }
    void rumble(float low, float high, int ms) const;

private:
    Vec2 deadzone(Vec2 v, const GamepadSettings& s) const;
    float value(const PadBinding& b) const;

    SDL_Gamepad* pad_ = nullptr;
    SDL_JoystickID id_ = 0;
    std::string name_;
    GamepadStyle style_ = GamepadStyle::Generic;
    std::array<PadBinding, kPowerSlotKeys> slotPads_{};
    std::array<bool, kActionCount> held_{}, prev_{}, pressed_{}, released_{};
    Vec2 stick_, stickR_;
    float lt_ = 0.0f, rt_ = 0.0f;
    bool confirm_ = false, back_ = false, start_ = false;
    bool southWas_ = false, eastWas_ = false, startWas_ = false;
    bool navUp_ = false, navDown_ = false, upWas_ = false, downWas_ = false;
};

/// Every gamepad connected now (ids), in SDL's order.
std::vector<SDL_JoystickID> connectedGamepads();
/// The name SDL gives a gamepad (« Manette » when it has none).
std::string gamepadName(SDL_JoystickID id);

}  // namespace cosmic
