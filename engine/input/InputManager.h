#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/input/Devices.h"
#include "engine/input/HeldBindings.h"
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
    /// Titan 13 (tests, the headless driver): `a` held as if its key were down, from the next update().
    void simulateHeld(Action a, bool on) { simHeld_[idx(a)] = on; }

    // ------------------------------------------------------------- menus
    /// True on press this frame (with auto-repeat for navigation actions).
    bool menuPressed(Action a) const { return framePressed_[idx(a)]; }
    /// 2.93, the screens' shortcuts (docs/CONTROLES.md): `a` pressed this frame by any device, or
    /// its key repeated by the system while held (as the editors' and the replays' keys always did).
    bool commandPressed(Action a) const { return framePressed_[idx(a)] || keyRepeated_[idx(a)]; }
    /// `a` pressed this frame by its key or mouse button (or a notch of the wheel); `withRepeat`: its
    /// key repeated by the system while held counts too.
    bool keyPressedFor(Action a, bool withRepeat = false) const { return keyFramePressed_[idx(a)] || (withRepeat && keyRepeated_[idx(a)]); }
    /// `a` pressed this frame by its gamepad binding (a button, a trigger or a stick past its threshold).
    bool padPressed(Action a) const { return padFramePressed_[idx(a)]; }
    /// `a`'s gamepad binding held right now (on this manager's pad): the chords « Vue + croix ».
    bool padHeld(Action a) const { return padHeld_[idx(a)]; }
    bool anyMenuInput() const;
    /// SDL ticks (ms) of the last key, button, click, wheel or stick move: presence (idle).
    uint64_t lastActivityMs() const { return lastActivityMs_; }
    /// 2.63: how many such inputs there have been (the main menu's attract mode: any change is
    /// someone back at the controls).
    uint64_t activityCount() const { return activityCount_; }

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
    /// Everything held right now, raw (keys, every mouse button, a notch of the wheel a moment
    /// ago, the player's gamepad buttons, sticks and triggers as the game reads them): the
    /// binding lists light what is pressed (keyBindingHeld / padBindingHeld). Reading it acts
    /// on nothing.
    HeldInputs heldInputs() const;
    /// 2.65: the same with the gamepad `pad` instead of this manager's (a local player's own pad,
    /// read raw; `triggerThreshold`: that player's settings). No pad: none held.
    HeldInputs heldInputs(SDL_JoystickID pad, float triggerThreshold) const;

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

    /// 2.62, cooperation on one computer: the gamepads of the other local players. They are
    /// never this manager's gamepad (no live switching to them) and their buttons press no
    /// command here: each is read by its own LocalPad. Empty: every gamepad is the player's.
    void setReservedPads(std::vector<SDL_JoystickID> ids);
    bool reservedPad(SDL_JoystickID id) const;
    SDL_JoystickID gamepadId() const { return gamepad_ ? gamepadId_ : 0; }

    /// 2.65: every gamepad of the session with its stable number (« Manette Xbox (n° 1) »),
    /// updated as they are plugged and unplugged (the reserved ones too).
    const DeviceRegistry& devices() const { return devices_; }
    /// The number of this manager's gamepad (0: none).
    int gamepadNumber() const { return gamepad_ ? devices_.numberOf(gamepadId_) : 0; }
    /// 2.65, one PC with several players: false = the gamepads only move in the menus here (each
    /// player's pad is read by its own LocalPad; the keyboard player plays with the keyboard and
    /// the mouse only). True by default.
    void setGamepadGameplay(bool on) { padGameplay_ = on; }
    /// 2.65: the number of the gamepad that went away while it was the device in use (a solo run
    /// pauses: GameScene), 0 if none. Cleared by clearLostPad(), or as soon as the keyboard, the
    /// mouse or another gamepad is used.
    int lostPad() const { return lostPad_; }
    void clearLostPad() { lostPad_ = 0; }

    /// Rebinding: the next key (or gamepad button/axis) pressed is captured.
    void beginCapture(InputDevice device);
    void cancelCapture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    bool captureDone() const { return captureDone_; }
    SDL_Scancode capturedKey() const { return capturedKey_; }
    PadBinding capturedPad() const { return capturedPad_; }

private:
    uint64_t lastActivityMs_ = 0;
    uint64_t activityCount_ = 0;
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
    // 2.93: the same by device (keyboard / mouse, gamepad), and the system's repeat of a key.
    std::array<bool, kActionCount> keyFramePressed_{};
    std::array<bool, kActionCount> keyRepeated_{};
    std::array<bool, kActionCount> prevKeyHeld_{};
    std::array<bool, kActionCount> padFramePressed_{};
    std::array<bool, kActionCount> padHeld_{};
    std::array<bool, kActionCount> prevPadHeld_{};
    std::array<float, kActionCount> repeatTimer_{};
    std::array<bool, kActionCount> simHeld_{};

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
    uint64_t wheelUpAtMs_ = 0, wheelDownAtMs_ = 0;  // last notch each way (heldInputs)
    PadBinding swallowPad_;
    bool swallowingPad_ = false;
    // A power slot whose key changed while it was held: nothing until it is released.
    std::array<bool, kPowerSlotKeys> slotWaitRelease_{};
    std::vector<SDL_JoystickID> reservedPads_;  // 2.62: the other local players' gamepads
    DeviceRegistry devices_;  // 2.65
    bool padGameplay_ = true;
    int lostPad_ = 0;
    void notePlugged(SDL_JoystickID id);
};

/// Titan 13 (« Tab en avançant ») : the keys of a panel that opens in the middle of the game (the raid's menu, its
/// contributions, a test's panel, the pause). A tab or a sort changes on a FRESH press only, never on the
/// auto-repeat of a held direction; every key already held when the panel opened is ignored until it is released
/// (moving right while opening the menu never runs through its tabs). The mouse is not concerned.
class FreshKeys {
public:
    /// The panel opens (or opens again): what is held at the next update() waits for its release.
    void reset() { armed_ = false; }
    /// Once a frame, before reading the keys (the first call after reset() takes what is held).
    void update(const InputManager& in) {
        for (int i = 0; i < kActionCount; ++i) {
            const bool h = in.held(static_cast<Action>(i));
            if (!armed_) wait_[i] = prev_[i] = h;
            else {
                prev_[i] = now_[i];
                if (!h) wait_[i] = false;
            }
            now_[i] = h;
        }
        armed_ = true;
    }
    /// A new press of `a` this frame: not its repeat, not a key held since the panel opened.
    bool fresh(const InputManager& in, Action a) const {
        const int i = static_cast<int>(a);
        return in.menuPressed(a) && !wait_[i] && !prev_[i];
    }
    /// `a` pressed this frame, its repeat included (a list scrolled), unless held since the panel opened.
    bool pressed(const InputManager& in, Action a) const { return in.menuPressed(a) && !wait_[static_cast<int>(a)]; }
    /// `a` held since the panel opened and not released yet (its focus moves, its pans: ignored).
    bool waiting(Action a) const { return wait_[static_cast<int>(a)]; }
    /// Whether a horizontal / vertical direction still waits (for UIContext::consumeNavigation).
    bool waitingH() const { return waiting(Action::MenuLeft) || waiting(Action::MenuRight) || waiting(Action::MenuTabPrev) || waiting(Action::MenuTabNext); }
    bool waitingV() const { return waiting(Action::MenuUp) || waiting(Action::MenuDown); }

private:
    bool armed_ = false;
    std::array<bool, kActionCount> wait_{}, prev_{}, now_{};
};

}  // namespace cosmic
