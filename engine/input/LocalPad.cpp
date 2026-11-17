#include "engine/input/LocalPad.h"

#include <algorithm>
#include <cmath>

namespace cosmic {

std::vector<SDL_JoystickID> connectedGamepads() {
    std::vector<SDL_JoystickID> out;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) out.push_back(ids[i]);
        SDL_free(ids);
    }
    return out;
}

std::string gamepadName(SDL_JoystickID id) {
    const char* n = SDL_GetGamepadNameForID(id);
    return n && *n ? n : "Manette";
}

bool LocalPad::open(SDL_JoystickID id) {
    close();
    pad_ = SDL_OpenGamepad(id);
    if (!pad_) return false;
    id_ = id;
    const char* n = SDL_GetGamepadName(pad_);
    name_ = n && *n ? n : "Manette";
    switch (SDL_GetGamepadType(pad_)) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: style_ = GamepadStyle::PlayStation; break;
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE: style_ = GamepadStyle::Xbox; break;
        default: style_ = GamepadStyle::Generic; break;
    }
    held_.fill(false);
    prev_.fill(false);
    pressed_.fill(false);
    released_.fill(false);
    // A button already held when the pad is taken (the one that joined) counts once released.
    southWas_ = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_SOUTH);
    eastWas_ = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_EAST);
    startWas_ = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_START);
    return true;
}

void LocalPad::close() {
    if (pad_) SDL_CloseGamepad(pad_);
    pad_ = nullptr;
    id_ = 0;
}

bool LocalPad::connected() const { return pad_ && SDL_GamepadConnected(pad_); }

Vec2 LocalPad::deadzone(Vec2 v, const GamepadSettings& s) const {
    float len = v.length();
    if (len <= s.stickDeadzone) return {};
    float scaled = std::clamp((len - s.stickDeadzone) / (1.0f - s.stickDeadzone), 0.0f, 1.0f);
    float curved = std::pow(scaled, 1.0f / std::max(0.25f, s.sensitivity));
    return v * (curved / len);
}

float LocalPad::value(const PadBinding& b) const {
    switch (b.kind) {
        case PadBinding::Kind::None: return 0.0f;
        case PadBinding::Kind::Button: return SDL_GetGamepadButton(pad_, static_cast<SDL_GamepadButton>(b.code)) ? 1.0f : 0.0f;
        case PadBinding::Kind::AxisPositive:
        case PadBinding::Kind::AxisNegative: {
            auto axis = static_cast<SDL_GamepadAxis>(b.code);
            float v;
            if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) v = lt_;
            else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) v = rt_;
            else if (axis == SDL_GAMEPAD_AXIS_LEFTX) v = stick_.x;
            else if (axis == SDL_GAMEPAD_AXIS_LEFTY) v = stick_.y;
            else if (axis == SDL_GAMEPAD_AXIS_RIGHTX) v = stickR_.x;
            else v = stickR_.y;
            return b.kind == PadBinding::Kind::AxisPositive ? std::max(0.0f, v) : std::max(0.0f, -v);
        }
    }
    return 0.0f;
}

void LocalPad::update(const InputBindings& bindings, float /*dt*/) {
    confirm_ = back_ = start_ = navUp_ = navDown_ = false;
    if (!connected()) {
        held_.fill(false);
        return;
    }
    const GamepadSettings& gs = bindings.gamepad;
    auto ax = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad_, a) / 32767.0f; };
    Vec2 l{ax(SDL_GAMEPAD_AXIS_LEFTX), ax(SDL_GAMEPAD_AXIS_LEFTY)};
    Vec2 r{ax(SDL_GAMEPAD_AXIS_RIGHTX), ax(SDL_GAMEPAD_AXIS_RIGHTY)};
    if (gs.swapSticks) std::swap(l, r);
    stick_ = deadzone(l, gs);
    stickR_ = deadzone(r, gs);
    if (gs.invertX) stick_.x = -stick_.x, stickR_.x = -stickR_.x;
    if (gs.invertY) stick_.y = -stick_.y, stickR_.y = -stickR_.y;
    lt_ = std::clamp(ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f, 1.0f);
    rt_ = std::clamp(ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f, 1.0f);
    for (int i = 0; i < kFirstMenuAction; ++i) {
        bool h = false;
        auto test = [&](const PadBinding& pb) {
            if (pb.kind == PadBinding::Kind::None) return;
            const bool trigger = pb.kind != PadBinding::Kind::Button &&
                                 (pb.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || pb.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (value(pb) >= (trigger ? gs.triggerThreshold : 0.5f)) h = true;
        };
        if (int s = powerSlotOf(static_cast<Action>(i)); s >= 0) {
            test(slotPads_[static_cast<size_t>(s)]);
        } else {
            for (const auto& pb : bindings.pad[static_cast<size_t>(i)]) test(pb);
            test(builtInPadAlternate(static_cast<Action>(i)));
        }
        held_[static_cast<size_t>(i)] = h;
        if (h && !prev_[static_cast<size_t>(i)]) pressed_[static_cast<size_t>(i)] = true;
        if (!h && prev_[static_cast<size_t>(i)]) released_[static_cast<size_t>(i)] = true;
        prev_[static_cast<size_t>(i)] = h;
    }
    const bool south = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_SOUTH);
    const bool east = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_EAST);
    const bool start = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_START);
    confirm_ = south && !southWas_;
    back_ = east && !eastWas_;
    start_ = start && !startWas_;
    const bool up = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_UP) || stick_.y < -0.6f;
    const bool down = SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || stick_.y > 0.6f;
    navUp_ = up && !upWas_;
    navDown_ = down && !downWas_;
    upWas_ = up;
    downWas_ = down;
    southWas_ = south;
    eastWas_ = east;
    startWas_ = start;
}

PlayerInput LocalPad::consume() {
    PlayerInput in;
    in.held = held_;
    in.pressed = pressed_;
    in.released = released_;
    float x = 0.0f, y = 0.0f;
    if (held_[static_cast<size_t>(Action::MoveLeft)]) x -= 1.0f;
    if (held_[static_cast<size_t>(Action::MoveRight)]) x += 1.0f;
    if (held_[static_cast<size_t>(Action::MoveUp)]) y -= 1.0f;
    if (held_[static_cast<size_t>(Action::MoveDown)]) y += 1.0f;
    if (std::abs(stick_.x) > 0.01f) x = std::clamp(stick_.x, -1.0f, 1.0f);
    if (std::abs(stick_.y) > 0.01f) y = std::clamp(stick_.y, -1.0f, 1.0f);
    in.move = {std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f)};
    pressed_.fill(false);
    released_.fill(false);
    return in;
}

void LocalPad::rumble(float low, float high, int ms) const {
    if (!pad_) return;
    SDL_RumbleGamepad(pad_, static_cast<Uint16>(std::clamp(low, 0.0f, 1.0f) * 65535), static_cast<Uint16>(std::clamp(high, 0.0f, 1.0f) * 65535),
                      static_cast<Uint32>(ms));
}

}  // namespace cosmic
