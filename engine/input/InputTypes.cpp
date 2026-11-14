#include "engine/input/InputTypes.h"

namespace cosmic {

namespace {
struct ActionInfo {
    const char* id;
    const char* label;
};
constexpr ActionInfo kActions[kActionCount] = {
    {"move_left", "Gauche"},          {"move_right", "Droite"},        {"move_up", "Haut / Grimper"},
    {"move_down", "Bas / Glissade"},  {"jump", "Saut"},                {"attack", "Attaque"},
    {"dash", "Dash"},                 {"wave", "Onde énergétique"},    {"shield", "Bouclier"},
    {"impulse", "Impulsion"},         {"teleport", "Téléportation"},   {"interact", "Interagir"},
    {"special", "Pouvoir spécial"},
    {"pause", "Pause"},               {"menu_up", "Menu haut"},        {"menu_down", "Menu bas"},
    {"menu_left", "Menu gauche"},     {"menu_right", "Menu droite"},   {"menu_confirm", "Valider"},
    {"menu_back", "Retour"},          {"menu_tab_prev", "Onglet précédent"}, {"menu_tab_next", "Onglet suivant"},
};
}  // namespace

const char* actionId(Action a) { return kActions[static_cast<int>(a)].id; }
const char* actionLabel(Action a) { return kActions[static_cast<int>(a)].label; }
bool actionIsRebindable(Action a) { return static_cast<int>(a) < kFirstMenuAction; }

Action actionFromId(const std::string& id) {
    for (int i = 0; i < kActionCount; ++i)
        if (id == kActions[i].id) return static_cast<Action>(i);
    return Action::Count;
}

void InputBindings::resetKeyboard() {
    for (auto& k : keys) k = {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN};
    auto set = [&](Action a, SDL_Scancode k0, SDL_Scancode k1 = SDL_SCANCODE_UNKNOWN) {
        keys[static_cast<int>(a)] = {k0, k1};
    };
    set(Action::MoveLeft, SDL_SCANCODE_A, SDL_SCANCODE_LEFT);
    set(Action::MoveRight, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT);
    set(Action::MoveUp, SDL_SCANCODE_W, SDL_SCANCODE_UP);
    set(Action::MoveDown, SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
    set(Action::Jump, SDL_SCANCODE_SPACE, SDL_SCANCODE_Z);
    set(Action::Attack, SDL_SCANCODE_J, SDL_SCANCODE_X);
    set(Action::Dash, SDL_SCANCODE_K, SDL_SCANCODE_LSHIFT);
    set(Action::Wave, SDL_SCANCODE_L, SDL_SCANCODE_C);
    set(Action::Shield, SDL_SCANCODE_O, SDL_SCANCODE_V);
    set(Action::Impulse, SDL_SCANCODE_I, SDL_SCANCODE_B);
    set(Action::Teleport, SDL_SCANCODE_U, SDL_SCANCODE_N);
    set(Action::Interact, SDL_SCANCODE_E, SDL_SCANCODE_RETURN);
    set(Action::Special, SDL_SCANCODE_F, SDL_SCANCODE_H);
    set(Action::Pause, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_P);
    set(Action::MenuUp, SDL_SCANCODE_UP, SDL_SCANCODE_W);
    set(Action::MenuDown, SDL_SCANCODE_DOWN, SDL_SCANCODE_S);
    set(Action::MenuLeft, SDL_SCANCODE_LEFT, SDL_SCANCODE_A);
    set(Action::MenuRight, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D);
    set(Action::MenuConfirm, SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE);
    set(Action::MenuBack, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE);
    set(Action::MenuTabPrev, SDL_SCANCODE_Q, SDL_SCANCODE_PAGEUP);
    set(Action::MenuTabNext, SDL_SCANCODE_E, SDL_SCANCODE_PAGEDOWN);
}

void InputBindings::resetGamepad() {
    for (auto& p : pad) p = {PadBinding{}, PadBinding{}};
    auto set = [&](Action a, PadBinding b0, PadBinding b1 = {}) { pad[static_cast<int>(a)] = {b0, b1}; };
    using B = PadBinding;
    set(Action::MoveLeft, B::axisNeg(SDL_GAMEPAD_AXIS_LEFTX), B::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    set(Action::MoveRight, B::axisPos(SDL_GAMEPAD_AXIS_LEFTX), B::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    set(Action::MoveUp, B::axisNeg(SDL_GAMEPAD_AXIS_LEFTY), B::button(SDL_GAMEPAD_BUTTON_DPAD_UP));
    set(Action::MoveDown, B::axisPos(SDL_GAMEPAD_AXIS_LEFTY), B::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    set(Action::Jump, B::button(SDL_GAMEPAD_BUTTON_SOUTH));
    set(Action::Attack, B::button(SDL_GAMEPAD_BUTTON_WEST));
    set(Action::Dash, B::button(SDL_GAMEPAD_BUTTON_EAST));
    set(Action::Wave, B::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    set(Action::Shield, B::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    set(Action::Impulse, B::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    set(Action::Teleport, B::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    set(Action::Interact, B::button(SDL_GAMEPAD_BUTTON_NORTH));
    set(Action::Special, B::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
    set(Action::Pause, B::button(SDL_GAMEPAD_BUTTON_START));
    set(Action::MenuUp, B::button(SDL_GAMEPAD_BUTTON_DPAD_UP), B::axisNeg(SDL_GAMEPAD_AXIS_LEFTY));
    set(Action::MenuDown, B::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN), B::axisPos(SDL_GAMEPAD_AXIS_LEFTY));
    set(Action::MenuLeft, B::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT), B::axisNeg(SDL_GAMEPAD_AXIS_LEFTX));
    set(Action::MenuRight, B::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), B::axisPos(SDL_GAMEPAD_AXIS_LEFTX));
    set(Action::MenuConfirm, B::button(SDL_GAMEPAD_BUTTON_SOUTH));
    set(Action::MenuBack, B::button(SDL_GAMEPAD_BUTTON_EAST), B::button(SDL_GAMEPAD_BUTTON_START));
    set(Action::MenuTabPrev, B::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    set(Action::MenuTabNext, B::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    gamepad = GamepadSettings{};
}

InputBindings InputBindings::defaults() {
    InputBindings b;
    b.resetKeyboard();
    b.resetGamepad();
    return b;
}

namespace {
Json padToJson(const PadBinding& p) {
    switch (p.kind) {
        case PadBinding::Kind::None: return nullptr;
        case PadBinding::Kind::Button:
            return {{"button", SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(p.code))}};
        case PadBinding::Kind::AxisPositive:
            return {{"axis", SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(p.code))}, {"dir", 1}};
        case PadBinding::Kind::AxisNegative:
            return {{"axis", SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(p.code))}, {"dir", -1}};
    }
    return nullptr;
}

PadBinding padFromJson(const Json& j) {
    if (!j.is_object()) return {};
    if (j.contains("button")) {
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(jstr(j, "button").c_str());
        if (b != SDL_GAMEPAD_BUTTON_INVALID) return PadBinding::button(b);
    }
    if (j.contains("axis")) {
        SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(jstr(j, "axis").c_str());
        if (a != SDL_GAMEPAD_AXIS_INVALID)
            return jget<int>(j, "dir", 1) >= 0 ? PadBinding::axisPos(a) : PadBinding::axisNeg(a);
    }
    return {};
}
}  // namespace

Json InputBindings::toJson() const {
    Json j;
    Json k = Json::object(), p = Json::object();
    for (int i = 0; i < kActionCount; ++i) {
        Json ka = Json::array();
        for (auto sc : keys[i]) ka.push_back(sc == SDL_SCANCODE_UNKNOWN ? "" : SDL_GetScancodeName(sc));
        k[actionId(static_cast<Action>(i))] = ka;
        Json pa = Json::array();
        for (const auto& b : pad[i]) pa.push_back(padToJson(b));
        p[actionId(static_cast<Action>(i))] = pa;
    }
    j["keyboard"] = k;
    j["gamepad"] = p;
    j["gamepadSettings"] = {{"stickDeadzone", gamepad.stickDeadzone}, {"triggerThreshold", gamepad.triggerThreshold},
                            {"sensitivity", gamepad.sensitivity},     {"invertX", gamepad.invertX},
                            {"invertY", gamepad.invertY},             {"rumble", gamepad.rumble},
                            {"swapSticks", gamepad.swapSticks}};
    return j;
}

InputBindings InputBindings::fromJson(const Json& j) {
    InputBindings b = defaults();
    if (!j.is_object()) return b;
    if (j.contains("keyboard") && j["keyboard"].is_object()) {
        for (auto& [id, arr] : j["keyboard"].items()) {
            Action a = actionFromId(id);
            if (a == Action::Count || !arr.is_array()) continue;
            for (size_t s = 0; s < 2 && s < arr.size(); ++s) {
                std::string name = arr[s].is_string() ? arr[s].get<std::string>() : "";
                b.keys[static_cast<int>(a)][s] = name.empty() ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(name.c_str());
            }
        }
    }
    if (j.contains("gamepad") && j["gamepad"].is_object()) {
        for (auto& [id, arr] : j["gamepad"].items()) {
            Action a = actionFromId(id);
            if (a == Action::Count || !arr.is_array()) continue;
            for (size_t s = 0; s < 2 && s < arr.size(); ++s) b.pad[static_cast<int>(a)][s] = padFromJson(arr[s]);
        }
    }
    if (j.contains("gamepadSettings")) {
        const Json& g = j["gamepadSettings"];
        b.gamepad.stickDeadzone = std::clamp(jget<float>(g, "stickDeadzone", 0.22f), 0.0f, 0.9f);
        b.gamepad.triggerThreshold = std::clamp(jget<float>(g, "triggerThreshold", 0.35f), 0.05f, 0.95f);
        b.gamepad.sensitivity = std::clamp(jget<float>(g, "sensitivity", 1.0f), 0.25f, 3.0f);
        b.gamepad.invertX = jget<bool>(g, "invertX", false);
        b.gamepad.invertY = jget<bool>(g, "invertY", false);
        b.gamepad.rumble = jget<bool>(g, "rumble", true);
        b.gamepad.swapSticks = jget<bool>(g, "swapSticks", false);
    }
    return b;
}

int glyphIndex(const PadBinding& b) {
    if (b.kind == PadBinding::Kind::Button) {
        switch (static_cast<SDL_GamepadButton>(b.code)) {
            case SDL_GAMEPAD_BUTTON_SOUTH: return 0;
            case SDL_GAMEPAD_BUTTON_EAST: return 1;
            case SDL_GAMEPAD_BUTTON_WEST: return 2;
            case SDL_GAMEPAD_BUTTON_NORTH: return 3;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 4;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 5;
            case SDL_GAMEPAD_BUTTON_BACK: return 8;
            case SDL_GAMEPAD_BUTTON_START: return 9;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 10;
            case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 11;
            case SDL_GAMEPAD_BUTTON_DPAD_UP: return 12;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 13;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 14;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 15;
            case SDL_GAMEPAD_BUTTON_GUIDE: return 18;
            default: return -1;
        }
    }
    if (b.kind == PadBinding::Kind::AxisPositive || b.kind == PadBinding::Kind::AxisNegative) {
        switch (static_cast<SDL_GamepadAxis>(b.code)) {
            case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return 6;
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return 7;
            case SDL_GAMEPAD_AXIS_LEFTX:
            case SDL_GAMEPAD_AXIS_LEFTY: return 16;
            case SDL_GAMEPAD_AXIS_RIGHTX:
            case SDL_GAMEPAD_AXIS_RIGHTY: return 17;
            default: return -1;
        }
    }
    return -1;
}

std::string padBindingName(const PadBinding& b, GamepadStyle style) {
    static const char* xbox[] = {"A", "B", "X", "Y"};
    static const char* ps[] = {"Croix", "Rond", "Carré", "Triangle"};
    static const char* gen[] = {"Bas", "Droite", "Gauche", "Haut"};
    switch (b.kind) {
        case PadBinding::Kind::None: return "—";
        case PadBinding::Kind::Button: {
            auto btn = static_cast<SDL_GamepadButton>(b.code);
            if (btn >= SDL_GAMEPAD_BUTTON_SOUTH && btn <= SDL_GAMEPAD_BUTTON_NORTH) {
                const char** names = style == GamepadStyle::Xbox ? xbox : (style == GamepadStyle::PlayStation ? ps : gen);
                return names[btn];
            }
            switch (btn) {
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return style == GamepadStyle::PlayStation ? "L1" : "LB";
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return style == GamepadStyle::PlayStation ? "R1" : "RB";
                case SDL_GAMEPAD_BUTTON_START: return style == GamepadStyle::PlayStation ? "Options" : "Menu";
                case SDL_GAMEPAD_BUTTON_BACK: return style == GamepadStyle::PlayStation ? "Partage" : "Vue";
                case SDL_GAMEPAD_BUTTON_LEFT_STICK: return style == GamepadStyle::PlayStation ? "L3" : "LS";
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return style == GamepadStyle::PlayStation ? "R3" : "RS";
                case SDL_GAMEPAD_BUTTON_DPAD_UP: return "Croix haut";
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "Croix bas";
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Croix gauche";
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Croix droite";
                default: return SDL_GetGamepadStringForButton(btn) ? SDL_GetGamepadStringForButton(btn) : "?";
            }
        }
        case PadBinding::Kind::AxisPositive:
        case PadBinding::Kind::AxisNegative: {
            bool pos = b.kind == PadBinding::Kind::AxisPositive;
            switch (static_cast<SDL_GamepadAxis>(b.code)) {
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return style == GamepadStyle::PlayStation ? "L2" : "LT";
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return style == GamepadStyle::PlayStation ? "R2" : "RT";
                case SDL_GAMEPAD_AXIS_LEFTX: return pos ? "Stick G droite" : "Stick G gauche";
                case SDL_GAMEPAD_AXIS_LEFTY: return pos ? "Stick G bas" : "Stick G haut";
                case SDL_GAMEPAD_AXIS_RIGHTX: return pos ? "Stick D droite" : "Stick D gauche";
                case SDL_GAMEPAD_AXIS_RIGHTY: return pos ? "Stick D bas" : "Stick D haut";
                default: return "?";
            }
        }
    }
    return "?";
}

std::string keyName(SDL_Scancode sc) {
    if (sc == SDL_SCANCODE_UNKNOWN) return "—";
    switch (sc) {
        case SDL_SCANCODE_SPACE: return "Espace";
        case SDL_SCANCODE_RETURN: return "Entrée";
        case SDL_SCANCODE_ESCAPE: return "Échap";
        case SDL_SCANCODE_BACKSPACE: return "Retour arr.";
        case SDL_SCANCODE_LSHIFT: return "Maj G";
        case SDL_SCANCODE_RSHIFT: return "Maj D";
        case SDL_SCANCODE_LCTRL: return "Ctrl G";
        case SDL_SCANCODE_RCTRL: return "Ctrl D";
        case SDL_SCANCODE_TAB: return "Tab";
        case SDL_SCANCODE_UP: return "Flèche haut";
        case SDL_SCANCODE_DOWN: return "Flèche bas";
        case SDL_SCANCODE_LEFT: return "Flèche gauche";
        case SDL_SCANCODE_RIGHT: return "Flèche droite";
        default: break;
    }
    // Show the character of the current keyboard layout (AZERTY friendly).
    SDL_Keycode k = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    const char* n = SDL_GetKeyName(k);
    if (n && *n) return n;
    const char* s = SDL_GetScancodeName(sc);
    return s && *s ? s : "?";
}

}  // namespace cosmic
