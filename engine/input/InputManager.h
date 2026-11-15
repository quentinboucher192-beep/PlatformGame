#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/InputTypes.h"

namespace cosmic {

/// Snapshot of the gameplay actions for one fixed simulation step. The player and the
/// editors' test arena consume this instead of reading devices directly, which keeps the
/// simulation deterministic and testable.
struct PlayerInput {
    Vec2 move;  // analog, x/y in [-1, 1]
    std::array<bool, kActionCount> held{};
    std::array<bool, kActionCount> pressed{};
    std::array<bool, kActionCount> released{};

    bool isHeld(Action a) const { return held[static_cast<int>(a)]; }
    bool isPressed(Action a) const { return pressed[static_cast<int>(a)]; }
    bool isReleased(Action a) const { return released[static_cast<int>(a)]; }
};

/// Keyboard, mouse and SDL3 gamepads (Xbox, PlayStation, XInput, generic) with
/// independent rebindable configurations and live device switching.
class InputManager {
public:
    void init();
    void shutdown();

    /// Call at the start of each frame before polling events.
    void beginFrame();
    void handleEvent(const SDL_Event& e);
    /// Call after all events of the frame: samples analog state, computes edges.
    void update(float dt);

    // ---------------------------------------------------------- gameplay
    /// Builds the input for the next fixed step and consumes pressed/released edges.
    PlayerInput consumePlayerInput();
    bool held(Action a) const { return held_[idx(a)]; }

    // ------------------------------------------------------------- menus
    /// True on press this frame (with auto-repeat for navigation actions).
    bool menuPressed(Action a) const { return framePressed_[idx(a)]; }
    bool anyMenuInput() const;
    /// SDL ticks (ms) of the last key, button, click, wheel or stick move: presence (idle).
    uint64_t lastActivityMs() const { return lastActivityMs_; }

    // ------------------------------------------------------------ devices
    InputDevice lastDevice() const { return lastDevice_; }
    bool hasGamepad() const { return gamepad_ != nullptr; }
    GamepadStyle gamepadStyle() const { return style_; }
    const std::string& gamepadName() const { return gamepadName_; }
    void rumble(float low, float high, int ms);
    /// Raw stick values (after deadzone) for the controls page visualiser.
    Vec2 leftStick() const { return leftStick_; }
    Vec2 rightStick() const { return rightStick_; }
    float leftTrigger() const { return lt_; }
    float rightTrigger() const { return rt_; }
    bool rawPadButton(SDL_GamepadButton b) const;

    // ---------------------------------------------------- keyboard / mouse
    bool keyDown(SDL_Scancode sc) const;
    bool keyPressed(SDL_Scancode sc) const;  // physical position, includes OS key repeat
    /// Shortcut by key label in the current layout (Ctrl+Z is Ctrl+Z on AZERTY too),
    /// e.g. shortcutPressed(SDLK_Z). Includes OS key repeat.
    bool shortcutPressed(SDL_Keycode key) const;
    bool ctrl() const;
    bool shift() const;
    bool alt() const;
    Vec2 mousePos() const { return mouse_; }
    Vec2 mouseDelta() const { return mouseDelta_; }
    bool mouseDown(int button) const { return mouseDown_[button & 7]; }
    bool mousePressed(int button) const { return mousePressed_[button & 7]; }
    bool mouseReleased(int button) const { return mouseReleased_[button & 7]; }
    float wheel() const { return wheel_; }
    bool mouseMovedThisFrame() const { return mouseMoved_; }
    const std::string& textInput() const { return text_; }
    void startTextInput(SDL_Window* w);
    void stopTextInput(SDL_Window* w);
    bool textInputActive() const { return textActive_; }

    // ------------------------------------------------------------ bindings
    InputBindings& bindings() { return bindings_; }
    const InputBindings& bindings() const { return bindings_; }
    /// The commands of the settings; the keys of the power slots stay as the scene that plays
    /// set them (setPowerSlotKey).
    void setBindings(const InputBindings& b);

    /// 2.31: the key and gamepad button of the power carried in slot `slot` (0-based, the order
    /// of the HUD), set by the scene that plays from the keys of the level's powers
    /// (game/powers/PowerKeys.h); nothing (UNKNOWN / None) for an empty slot. The slot's
    /// action (PowerSlot1..8) then follows that key: held, press edges, mouse, wheel. When the
    /// key changes, a key or button already held counts only once released and pressed again,
    /// and a press not yet consumed is dropped: a pickup never fires the new power by itself.
    void setPowerSlotKey(int slot, SDL_Scancode key, const PadBinding& pad);
    /// Every slot without a key (leaving a level, an editor's arena...).
    void clearPowerSlotKeys();

    /// Rebinding: the next key (or gamepad button/axis) pressed is captured.
    void beginCapture(InputDevice device);
    void cancelCapture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    bool captureDone() const { return captureDone_; }
    SDL_Scancode capturedKey() const { return capturedKey_; }
    PadBinding capturedPad() const { return capturedPad_; }

private:
    uint64_t lastActivityMs_ = 0;
    static int idx(Action a) { return static_cast<int>(a); }
    float padValue(const PadBinding& b) const;
    /// The commands bound to `code` (a mouse button or a notch of the wheel) are pressed.
    void pressBound(SDL_Scancode code);
    void openFirstGamepad();
    void setGamepad(SDL_Gamepad* pad);
    Vec2 applyDeadzone(Vec2 v) const;

    InputBindings bindings_ = InputBindings::defaults();
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepadId_ = 0;
    GamepadStyle style_ = GamepadStyle::Generic;
    std::string gamepadName_;
    InputDevice lastDevice_ = InputDevice::Keyboard;

    std::array<bool, kActionCount> held_{};
    std::array<bool, kActionCount> prevHeld_{};
    std::array<bool, kActionCount> gamePressed_{};
    std::array<bool, kActionCount> gameReleased_{};
    std::array<bool, kActionCount> framePressed_{};
    std::array<float, kActionCount> repeatTimer_{};

    std::vector<SDL_Scancode> keysPressed_;
    std::vector<SDL_Keycode> keyLabelsPressed_;
    Vec2 leftStick_, rightStick_;
    float lt_ = 0.0f, rt_ = 0.0f;

    Vec2 mouse_, mouseDelta_;
    bool mouseDown_[8]{}, mousePressed_[8]{}, mouseReleased_[8]{};
    bool mouseMoved_ = false;
    float wheel_ = 0.0f;
    std::string text_;
    bool textActive_ = false;

    bool capturing_ = false;
    bool captureDone_ = false;
    InputDevice captureDevice_ = InputDevice::Keyboard;
    SDL_Scancode capturedKey_ = SDL_SCANCODE_UNKNOWN;
    PadBinding capturedPad_;
    // The key or button just given to a command (rebinding) is used for that only: it acts
    // as nothing else (no menu, no game action) until it has been released.
    SDL_Scancode swallowKey_ = SDL_SCANCODE_UNKNOWN;
    int swallowMouse_ = 0;  // the mouse button just given to a command, until released
    // Mouse buttons held (bit n: button n, any button the system reports) and the wheel's
    // notches of this frame, for the commands bound to them.
    uint64_t mouseHeld_ = 0;
    bool wheelUpPulse_ = false, wheelDownPulse_ = false;
    PadBinding swallowPad_;
    bool swallowingPad_ = false;
    // A power slot whose key changed while it was held: nothing until it is released.
    std::array<bool, kPowerSlotKeys> slotWaitRelease_{};
};

}  // namespace cosmic
