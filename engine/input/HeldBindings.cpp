#include "engine/input/HeldBindings.h"

namespace cosmic {

bool keyBindingHeld(SDL_Scancode binding, const HeldInputs& in) {
    if (isKeyboardScancode(binding)) return in.keys.test(static_cast<size_t>(binding));
    if (const int b = mouseBindingButton(binding)) return ((in.mouseButtons >> b) & 1u) != 0;
    if (static_cast<int>(binding) == kWheelUpBinding) return in.wheelUp;
    if (static_cast<int>(binding) == kWheelDownBinding) return in.wheelDown;
    return false;
}

bool padBindingHeld(const PadBinding& binding, const HeldInputs& in) {
    switch (binding.kind) {
        case PadBinding::Kind::None: return false;
        case PadBinding::Kind::Button:
            return binding.code >= 0 && binding.code < 64 && ((in.padButtons >> binding.code) & 1u) != 0;
        case PadBinding::Kind::AxisPositive:
        case PadBinding::Kind::AxisNegative: {
            if (binding.code < 0 || binding.code >= SDL_GAMEPAD_AXIS_COUNT) return false;
            float v = in.axes[static_cast<size_t>(binding.code)];
            if (binding.kind == PadBinding::Kind::AxisNegative) v = -v;
            const bool trigger = binding.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || binding.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            return v >= (trigger ? in.triggerThreshold : 0.5f);
        }
    }
    return false;
}

}  // namespace cosmic
