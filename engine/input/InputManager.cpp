#include "engine/input/InputManager.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Log.h"

namespace cosmic {

namespace {
constexpr float kRepeatDelay = 0.38f;
constexpr float kRepeatRate = 0.075f;

bool isNavAction(int a) {
    return a == static_cast<int>(Action::MenuUp) || a == static_cast<int>(Action::MenuDown) ||
           a == static_cast<int>(Action::MenuLeft) || a == static_cast<int>(Action::MenuRight);
}

GamepadStyle styleFor(SDL_GamepadType t) {
    switch (t) {
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
        case SDL_GAMEPAD_TYPE_STANDARD: return GamepadStyle::Xbox;
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: return GamepadStyle::PlayStation;
        default: return GamepadStyle::Generic;
    }
}
}  // namespace

void InputManager::init() { openFirstGamepad(); }

void InputManager::shutdown() {
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    gamepad_ = nullptr;
}

void InputManager::setGamepad(SDL_Gamepad* pad) {
    if (gamepad_ && gamepad_ != pad) SDL_CloseGamepad(gamepad_);
    gamepad_ = pad;
    if (!pad) {
        gamepadName_.clear();
        return;
    }
    gamepadId_ = SDL_GetGamepadID(pad);
    const char* n = SDL_GetGamepadName(pad);
    gamepadName_ = n ? n : "Manette";
    style_ = styleFor(SDL_GetGamepadType(pad));
    Log::info("Gamepad connected: {} (type {})", gamepadName_, static_cast<int>(SDL_GetGamepadType(pad)));
}

void InputManager::openFirstGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count && !gamepad_; ++i) {
            if (SDL_Gamepad* p = SDL_OpenGamepad(ids[i])) setGamepad(p);
        }
        SDL_free(ids);
    }
}

void InputManager::beginFrame() {
    framePressed_.fill(false);
    keysPressed_.clear();
    std::fill(std::begin(mousePressed_), std::end(mousePressed_), false);
    std::fill(std::begin(mouseReleased_), std::end(mouseReleased_), false);
    wheel_ = 0.0f;
    text_.clear();
    mouseDelta_ = {};
    mouseMoved_ = false;
}

void InputManager::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            keysPressed_.push_back(e.key.scancode);
            if (!e.key.repeat) {
                lastDevice_ = InputDevice::Keyboard;
                // Event-driven edges so that taps shorter than a frame are never lost.
                for (int i = 0; i < kActionCount; ++i) {
                    if (textActive_ && i < kFirstMenuAction) continue;
                    for (auto sc : bindings_.keys[i])
                        if (sc == e.key.scancode && sc != SDL_SCANCODE_UNKNOWN) gamePressed_[i] = framePressed_[i] = true;
                }
            }
            if (capturing_ && captureDevice_ == InputDevice::Keyboard && !e.key.repeat) {
                capturedKey_ = e.key.scancode;
                capturing_ = false;
                captureDone_ = true;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            mouse_ = {e.motion.x, e.motion.y};
            mouseDelta_ += Vec2{e.motion.xrel, e.motion.yrel};
            mouseMoved_ = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mouse_ = {e.button.x, e.button.y};
            mouseDown_[e.button.button & 7] = true;
            mousePressed_[e.button.button & 7] = true;
            lastDevice_ = InputDevice::Keyboard;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            mouseDown_[e.button.button & 7] = false;
            mouseReleased_[e.button.button & 7] = true;
            break;
        case SDL_EVENT_MOUSE_WHEEL: wheel_ += e.wheel.y; break;
        case SDL_EVENT_TEXT_INPUT: text_ += e.text.text; break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_) {
                if (SDL_Gamepad* p = SDL_OpenGamepad(e.gdevice.which)) setGamepad(p);
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad_ && e.gdevice.which == gamepadId_) {
                Log::info("Gamepad disconnected");
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
                gamepadName_.clear();
                lastDevice_ = InputDevice::Keyboard;
                openFirstGamepad();
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            lastDevice_ = InputDevice::Gamepad;
            for (int i = 0; i < kActionCount; ++i)
                for (const auto& pb : bindings_.pad[i])
                    if (pb.kind == PadBinding::Kind::Button && pb.code == e.gbutton.button)
                        gamePressed_[i] = framePressed_[i] = true;
            if (gamepad_ == nullptr || e.gbutton.which != gamepadId_) {
                // A second pad pressed a button: switch to it (live device switching).
                if (SDL_Gamepad* p = SDL_GetGamepadFromID(e.gbutton.which)) setGamepad(p);
            }
            if (capturing_ && captureDevice_ == InputDevice::Gamepad) {
                capturedPad_ = PadBinding::button(static_cast<SDL_GamepadButton>(e.gbutton.button));
                capturing_ = false;
                captureDone_ = true;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (std::abs(e.gaxis.value) > 16000) {
                lastDevice_ = InputDevice::Gamepad;
                if (capturing_ && captureDevice_ == InputDevice::Gamepad) {
                    auto axis = static_cast<SDL_GamepadAxis>(e.gaxis.axis);
                    capturedPad_ = e.gaxis.value > 0 ? PadBinding::axisPos(axis) : PadBinding::axisNeg(axis);
                    capturing_ = false;
                    captureDone_ = true;
                }
            }
            break;
        default: break;
    }
}

Vec2 InputManager::applyDeadzone(Vec2 v) const {
    float len = v.length();
    float dz = bindings_.gamepad.stickDeadzone;
    if (len <= dz) return {};
    float scaled = std::clamp((len - dz) / (1.0f - dz), 0.0f, 1.0f);
    // Sensitivity shapes the response curve: >1 more responsive near the centre.
    float curved = std::pow(scaled, 1.0f / std::max(0.25f, bindings_.gamepad.sensitivity));
    return v * (curved / len);
}

float InputManager::padValue(const PadBinding& b) const {
    if (!gamepad_) return 0.0f;
    switch (b.kind) {
        case PadBinding::Kind::None: return 0.0f;
        case PadBinding::Kind::Button:
            return SDL_GetGamepadButton(gamepad_, static_cast<SDL_GamepadButton>(b.code)) ? 1.0f : 0.0f;
        case PadBinding::Kind::AxisPositive:
        case PadBinding::Kind::AxisNegative: {
            auto axis = static_cast<SDL_GamepadAxis>(b.code);
            float v;
            if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) v = lt_;
            else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) v = rt_;
            else if (axis == SDL_GAMEPAD_AXIS_LEFTX) v = leftStick_.x;
            else if (axis == SDL_GAMEPAD_AXIS_LEFTY) v = leftStick_.y;
            else if (axis == SDL_GAMEPAD_AXIS_RIGHTX) v = rightStick_.x;
            else v = rightStick_.y;
            return b.kind == PadBinding::Kind::AxisPositive ? std::max(0.0f, v) : std::max(0.0f, -v);
        }
    }
    return 0.0f;
}

void InputManager::update(float dt) {
    if (gamepad_) {
        auto ax = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(gamepad_, a) / 32767.0f; };
        Vec2 l{ax(SDL_GAMEPAD_AXIS_LEFTX), ax(SDL_GAMEPAD_AXIS_LEFTY)};
        Vec2 r{ax(SDL_GAMEPAD_AXIS_RIGHTX), ax(SDL_GAMEPAD_AXIS_RIGHTY)};
        if (bindings_.gamepad.swapSticks) std::swap(l, r);
        leftStick_ = applyDeadzone(l);
        rightStick_ = applyDeadzone(r);
        if (bindings_.gamepad.invertX) leftStick_.x = -leftStick_.x, rightStick_.x = -rightStick_.x;
        if (bindings_.gamepad.invertY) leftStick_.y = -leftStick_.y, rightStick_.y = -rightStick_.y;
        lt_ = std::clamp(ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f, 1.0f);
        rt_ = std::clamp(ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f, 1.0f);
    } else {
        leftStick_ = rightStick_ = {};
        lt_ = rt_ = 0.0f;
    }

    const bool* kb = SDL_GetKeyboardState(nullptr);
    const bool blockKeys = textActive_;  // typing in a text field must not move the player
    for (int i = 0; i < kActionCount; ++i) {
        bool h = false;
        if (!blockKeys || i >= kFirstMenuAction) {
            for (auto sc : bindings_.keys[i])
                if (sc != SDL_SCANCODE_UNKNOWN && kb[sc]) h = true;
        }
        for (const auto& pb : bindings_.pad[i]) {
            float v = padValue(pb);
            bool isTrigger = pb.kind != PadBinding::Kind::Button &&
                             (pb.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || pb.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            float threshold = pb.kind == PadBinding::Kind::Button ? 0.5f
                              : isTrigger ? bindings_.gamepad.triggerThreshold
                                          : 0.5f;
            if (v >= threshold) h = true;
        }
        if (blockKeys && i < kFirstMenuAction) h = h && lastDevice_ == InputDevice::Gamepad;
        held_[i] = h;
        if (h && !prevHeld_[i]) {
            gamePressed_[i] = true;
            framePressed_[i] = true;
            repeatTimer_[i] = kRepeatDelay;
        } else if (!h && prevHeld_[i]) {
            gameReleased_[i] = true;
        } else if (h && isNavAction(i)) {
            repeatTimer_[i] -= dt;
            if (repeatTimer_[i] <= 0.0f) {
                framePressed_[i] = true;
                repeatTimer_[i] = kRepeatRate;
            }
        }
        prevHeld_[i] = h;
    }
}

PlayerInput InputManager::consumePlayerInput() {
    PlayerInput in;
    in.held = held_;
    in.pressed = gamePressed_;
    in.released = gameReleased_;
    float x = 0.0f, y = 0.0f;
    if (held_[idx(Action::MoveLeft)]) x -= 1.0f;
    if (held_[idx(Action::MoveRight)]) x += 1.0f;
    if (held_[idx(Action::MoveUp)]) y -= 1.0f;
    if (held_[idx(Action::MoveDown)]) y += 1.0f;
    // Analog stick magnitude when it is the stick that drives the movement.
    if (std::abs(leftStick_.x) > 0.01f && lastDevice_ == InputDevice::Gamepad) x = std::clamp(leftStick_.x, -1.0f, 1.0f);
    if (std::abs(leftStick_.y) > 0.01f && lastDevice_ == InputDevice::Gamepad) y = std::clamp(leftStick_.y, -1.0f, 1.0f);
    in.move = {std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f)};
    gamePressed_.fill(false);
    gameReleased_.fill(false);
    return in;
}

bool InputManager::anyMenuInput() const {
    for (int i = kFirstMenuAction; i < kActionCount; ++i)
        if (framePressed_[i]) return true;
    return false;
}

void InputManager::rumble(float low, float high, int ms) {
    if (!gamepad_ || !bindings_.gamepad.rumble) return;
    SDL_RumbleGamepad(gamepad_, static_cast<Uint16>(std::clamp(low, 0.0f, 1.0f) * 65535),
                      static_cast<Uint16>(std::clamp(high, 0.0f, 1.0f) * 65535), static_cast<Uint32>(ms));
}

bool InputManager::rawPadButton(SDL_GamepadButton b) const { return gamepad_ && SDL_GetGamepadButton(gamepad_, b); }

bool InputManager::keyDown(SDL_Scancode sc) const { return SDL_GetKeyboardState(nullptr)[sc]; }

bool InputManager::keyPressed(SDL_Scancode sc) const {
    return std::find(keysPressed_.begin(), keysPressed_.end(), sc) != keysPressed_.end();
}

bool InputManager::ctrl() const { return (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }
bool InputManager::shift() const { return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0; }
bool InputManager::alt() const { return (SDL_GetModState() & SDL_KMOD_ALT) != 0; }

void InputManager::startTextInput(SDL_Window* w) {
    if (!textActive_) SDL_StartTextInput(w);
    textActive_ = true;
}

void InputManager::stopTextInput(SDL_Window* w) {
    if (textActive_) SDL_StopTextInput(w);
    textActive_ = false;
}

void InputManager::beginCapture(InputDevice device) {
    capturing_ = true;
    captureDone_ = false;
    captureDevice_ = device;
    capturedKey_ = SDL_SCANCODE_UNKNOWN;
    capturedPad_ = {};
}

}  // namespace cosmic
