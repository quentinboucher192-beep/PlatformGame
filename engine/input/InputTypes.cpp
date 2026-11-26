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
    {"power_slot_1", "Pouvoir 1"},    {"power_slot_2", "Pouvoir 2"},   {"power_slot_3", "Pouvoir 3"},
    {"power_slot_4", "Pouvoir 4"},    {"power_slot_5", "Pouvoir 5"},   {"power_slot_6", "Pouvoir 6"},
    {"power_slot_7", "Pouvoir 7"},    {"power_slot_8", "Pouvoir 8"},
    {"pause", "Pause"},               {"ghost_toggle", "Afficher / masquer les fantômes"},
    {"focus_toggle", "Mode concentré"}, {"raid_map", "Carte du raid (agrandir)"},
    {"raid_menu", "Menu du raid"},
    {"bot_orders", "Ordres aux joueurs simulés"}, {"bot_thoughts", "Pensées des joueurs simulés"},
    {"startpos_place", "Poser une startpos ici"}, {"startpos_prev", "Startpos précédente"},
    {"startpos_next", "Startpos suivante"},       {"measure_place", "Point de mesure (A puis B)"},
    {"menu_up", "Menu haut"},        {"menu_down", "Menu bas"},
    {"menu_left", "Menu gauche"},     {"menu_right", "Menu droite"},   {"menu_confirm", "Valider"},
    {"menu_back", "Retour"},          {"menu_tab_prev", "Onglet précédent"}, {"menu_tab_next", "Onglet suivant"},
};
}  // namespace

const char* actionId(Action a) { return kActions[static_cast<int>(a)].id; }
const char* actionLabel(Action a) { return kActions[static_cast<int>(a)].label; }
bool actionIsLegacyPowerKey(Action a) {
    switch (a) {
        case Action::Dash:
        case Action::Wave:
        case Action::Shield:
        case Action::Impulse:
        case Action::Teleport:
        case Action::Special: return true;
        default: return false;
    }
}
bool actionIsRebindable(Action a) {
    return static_cast<int>(a) < kFirstMenuAction && !actionIsLegacyPowerKey(a) && powerSlotOf(a) < 0;
}
bool actionIsPowerInput(Action a) { return static_cast<int>(a) >= 0 && static_cast<int>(a) <= static_cast<int>(Action::Special); }

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
    set(Action::Interact, SDL_SCANCODE_E, SDL_SCANCODE_RETURN);
    // Power slots: no key here since 2.31, each power has its own key in each level (the
    // level's choice, the player's, or the power's suggestion: game/powers/PowerKeys.h).
    set(Action::Pause, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_P);
    set(Action::GhostToggle, SDL_SCANCODE_G);
    set(Action::FocusToggle, SDL_SCANCODE_T);
    set(Action::RaidMap, SDL_SCANCODE_M);  // 2.72: the raid's large map
    set(Action::RaidMenu, SDL_SCANCODE_TAB);  // 2.75.5: the raid's menu
    set(Action::BotOrders, SDL_SCANCODE_C);    // 2.79: a raid test's order wheel
    set(Action::BotThoughts, SDL_SCANCODE_V);  // 2.79: the simulated players' thoughts
    // 2.91: training's StartPos and measure points (no gameplay key uses F5 to F8; the gamepad
    // has no button of its own for them: View held + the d-pad, see GameScene).
    set(Action::StartPosPlace, SDL_SCANCODE_F5);
    set(Action::StartPosPrev, SDL_SCANCODE_F6);
    set(Action::StartPosNext, SDL_SCANCODE_F7);
    set(Action::MeasurePlace, SDL_SCANCODE_F8);
    set(Action::MenuUp, SDL_SCANCODE_UP, SDL_SCANCODE_W);
    set(Action::MenuDown, SDL_SCANCODE_DOWN, SDL_SCANCODE_S);
    set(Action::MenuLeft, SDL_SCANCODE_LEFT, SDL_SCANCODE_A);
    set(Action::MenuRight, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D);
    set(Action::MenuConfirm, SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE);
    set(Action::MenuBack, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE);
    set(Action::MenuTabPrev, SDL_SCANCODE_Q, SDL_SCANCODE_PAGEUP);
    set(Action::MenuTabNext, SDL_SCANCODE_E, SDL_SCANCODE_PAGEDOWN);
    keepOneBinding();
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
    set(Action::Interact, B::button(SDL_GAMEPAD_BUTTON_NORTH));
    // Power slots: no button here since 2.31 (see resetKeyboard).
    set(Action::Pause, B::button(SDL_GAMEPAD_BUTTON_START));
    set(Action::GhostToggle, B::button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    // Share (Xbox Series) / capture (Switch) / microphone (PS5), or the PlayStation touchpad.
    set(Action::FocusToggle, B::button(SDL_GAMEPAD_BUTTON_MISC1), B::button(SDL_GAMEPAD_BUTTON_TOUCHPAD));
    // 2.72: the raid's large map on View / Back / Select (− on Switch, Share on PlayStation).
    set(Action::RaidMap, B::button(SDL_GAMEPAD_BUTTON_BACK));
    // 2.75.5: the raid's menu on the left stick's click (L3), the button of the ghosts of Ghost
    // Racing: never in the same game (no ghost in a raid, no raid menu out of one).
    set(Action::RaidMenu, B::button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    // 2.79: a raid test's order wheel on the right stick's click (R3); the thoughts are shown or hidden from the
    // wheel itself (and Paramètres › HUD › « Raids »), no button of their own by default.
    set(Action::BotOrders, B::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
    set(Action::MenuUp, B::button(SDL_GAMEPAD_BUTTON_DPAD_UP), B::axisNeg(SDL_GAMEPAD_AXIS_LEFTY));
    set(Action::MenuDown, B::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN), B::axisPos(SDL_GAMEPAD_AXIS_LEFTY));
    set(Action::MenuLeft, B::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT), B::axisNeg(SDL_GAMEPAD_AXIS_LEFTX));
    set(Action::MenuRight, B::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), B::axisPos(SDL_GAMEPAD_AXIS_LEFTX));
    set(Action::MenuConfirm, B::button(SDL_GAMEPAD_BUTTON_SOUTH));
    set(Action::MenuBack, B::button(SDL_GAMEPAD_BUTTON_EAST), B::button(SDL_GAMEPAD_BUTTON_START));
    set(Action::MenuTabPrev, B::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    set(Action::MenuTabNext, B::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    gamepad = GamepadSettings{};
    keepOneBinding();
}

SDL_Scancode builtInKeyAlternate(Action a) {
    switch (a) {
        case Action::MoveLeft: return SDL_SCANCODE_LEFT;
        case Action::MoveRight: return SDL_SCANCODE_RIGHT;
        case Action::MoveUp: return SDL_SCANCODE_UP;
        case Action::MoveDown: return SDL_SCANCODE_DOWN;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

PadBinding builtInPadAlternate(Action a) {
    switch (a) {
        case Action::MoveLeft: return PadBinding::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        case Action::MoveRight: return PadBinding::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        case Action::MoveUp: return PadBinding::button(SDL_GAMEPAD_BUTTON_DPAD_UP);
        case Action::MoveDown: return PadBinding::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        case Action::FocusToggle: return PadBinding::button(SDL_GAMEPAD_BUTTON_TOUCHPAD);
        default: return {};
    }
}

void InputBindings::keepOneBinding() {
    // Game commands only: the menus keep their two keys (arrows and ZQSD, Entrée and Espace…).
    for (int i = 0; i < kFirstMenuAction; ++i) {
        auto& k = keys[i];
        if (k[0] == SDL_SCANCODE_UNKNOWN) k[0] = k[1];
        k[1] = SDL_SCANCODE_UNKNOWN;
        auto& p = pad[i];
        if (p[0].kind == PadBinding::Kind::None) p[0] = p[1];
        p[1] = PadBinding{};
    }
    // The fixed alternates, unless the player gave that key / button to another command.
    for (int i = 0; i < kFirstMenuAction; ++i) {
        const Action a = static_cast<Action>(i);
        SDL_Scancode alt = builtInKeyAlternate(a);
        bool taken = alt == SDL_SCANCODE_UNKNOWN || keys[i][0] == alt;
        for (int o = 0; o < kFirstMenuAction && !taken; ++o)
            if (o != i && keys[o][0] == alt) taken = true;
        if (!taken) keys[i][1] = alt;
        PadBinding palt = builtInPadAlternate(a);
        bool ptaken = palt.kind == PadBinding::Kind::None || pad[i][0] == palt;
        for (int o = 0; o < kFirstMenuAction && !ptaken; ++o)
            if (o != i && pad[o][0] == palt) ptaken = true;
        if (!ptaken) pad[i][1] = palt;
    }
}

InputBindings InputBindings::defaults() {
    InputBindings b;
    b.resetKeyboard();
    b.resetGamepad();
    return b;
}

Json padBindingToJson(const PadBinding& p) {
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

PadBinding padBindingFromJson(const Json& j) {
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

/// A keyboard / mouse binding in the settings file: the SDL name of a key, « Mouse4 »,
/// « WheelUp », « WheelDown » (2.30.2), "" for none. An older game reading a mouse name
/// finds no key and keeps none.
std::string bindingName(SDL_Scancode sc) {
    if (sc == SDL_SCANCODE_UNKNOWN) return "";
    if (int b = mouseBindingButton(sc)) return "Mouse" + std::to_string(b);
    if (static_cast<int>(sc) == kWheelUpBinding) return "WheelUp";
    if (static_cast<int>(sc) == kWheelDownBinding) return "WheelDown";
    if (!isKeyboardScancode(sc)) return "";
    const char* n = SDL_GetScancodeName(sc);
    return n ? n : "";
}

SDL_Scancode bindingFromName(const std::string& name) {
    if (name.empty()) return SDL_SCANCODE_UNKNOWN;
    if (name == "WheelUp") return static_cast<SDL_Scancode>(kWheelUpBinding);
    if (name == "WheelDown") return static_cast<SDL_Scancode>(kWheelDownBinding);
    if (name.size() > 5 && name.size() <= 7 && name.compare(0, 5, "Mouse") == 0) {
        int b = 0;
        for (size_t i = 5; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') return SDL_SCANCODE_UNKNOWN;
            b = b * 10 + (name[i] - '0');
        }
        return b >= 1 && b <= kMouseButtonsBindable ? mouseButtonBinding(b) : SDL_SCANCODE_UNKNOWN;
    }
    if (name.size() > 64) return SDL_SCANCODE_UNKNOWN;
    return SDL_GetScancodeFromName(name.c_str());
}

Json InputBindings::toJson() const {
    Json j;
    Json k = Json::object(), p = Json::object();
    for (int i = 0; i < kActionCount; ++i) {
        // The power buttons before 2.24 are bound to nothing, and the power slots follow the
        // powers carried (2.31): neither is written.
        if (actionIsLegacyPowerKey(static_cast<Action>(i)) || powerSlotOf(static_cast<Action>(i)) >= 0) continue;
        Json ka = Json::array();
        for (auto sc : keys[i]) ka.push_back(bindingName(sc));
        k[actionId(static_cast<Action>(i))] = ka;
        Json pa = Json::array();
        for (const auto& b : pad[i]) pa.push_back(padBindingToJson(b));
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

namespace {
/// Where an action of a settings file goes: itself, or Action::Count (ignored) for a menu
/// action this file may not hold, a button of the powers before 2.24 or a power slot (their
/// keys follow the powers since 2.31: what an older file chose for them is not kept).
Action bindingTarget(const std::string& id) {
    Action a = actionFromId(id);
    if (a == Action::Count || actionIsLegacyPowerKey(a) || powerSlotOf(a) >= 0) return Action::Count;
    return a;
}
}  // namespace

InputBindings InputBindings::fromJson(const Json& j) {
    InputBindings b = defaults();
    if (!j.is_object()) return b;
    if (j.contains("keyboard") && j["keyboard"].is_object()) {
        const Json& kb = j["keyboard"];
        for (auto& [id, arr] : kb.items()) {
            Action a = bindingTarget(id);
            if (a == Action::Count || !arr.is_array()) continue;
            for (size_t s = 0; s < 2 && s < arr.size(); ++s) {
                std::string name = arr[s].is_string() ? arr[s].get<std::string>() : "";
                b.keys[static_cast<int>(a)][s] = bindingFromName(name);
            }
        }
    }
    if (j.contains("gamepad") && j["gamepad"].is_object()) {
        const Json& gp = j["gamepad"];
        for (auto& [id, arr] : gp.items()) {
            Action a = bindingTarget(id);
            if (a == Action::Count || !arr.is_array()) continue;
            for (size_t s = 0; s < 2 && s < arr.size(); ++s) b.pad[static_cast<int>(a)][s] = padBindingFromJson(arr[s]);
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
    // A file of 2.24 to 2.31 where a slot key had taken the key of a command: that command,
    // left with none, gets its own default back when no other command uses it.
    auto hadSlots = [&](const char* device) {
        if (!j.contains(device) || !j[device].is_object()) return false;
        for (int s = 0; s < kPowerSlotKeys; ++s)
            if (j[device].contains(actionId(powerSlotAction(s)))) return true;
        return false;
    };
    const InputBindings d = defaults();
    if (hadSlots("keyboard"))
        for (int i = 0; i < kFirstMenuAction; ++i) {
            if (!actionIsRebindable(static_cast<Action>(i)) || b.keys[i][0] != SDL_SCANCODE_UNKNOWN || b.keys[i][1] != SDL_SCANCODE_UNKNOWN)
                continue;
            const SDL_Scancode def = d.keys[i][0];
            bool used = def == SDL_SCANCODE_UNKNOWN;
            for (int o = 0; o < kFirstMenuAction && !used; ++o)
                used = b.keys[o][0] == def || b.keys[o][1] == def;
            if (!used) b.keys[i] = d.keys[i];
        }
    if (hadSlots("gamepad"))
        for (int i = 0; i < kFirstMenuAction; ++i) {
            if (!actionIsRebindable(static_cast<Action>(i)) || b.pad[i][0].kind != PadBinding::Kind::None ||
                b.pad[i][1].kind != PadBinding::Kind::None)
                continue;
            const PadBinding def = d.pad[i][0];
            bool used = def.kind == PadBinding::Kind::None;
            for (int o = 0; o < kFirstMenuAction && !used; ++o)
                used = b.pad[o][0] == def || b.pad[o][1] == def;
            if (!used) b.pad[i] = d.pad[i];
        }
    // 2.75.5: a file written before the raid's menu (no « raid_menu »): its key (Tab) and its button
    // (L3) only when no command of that file has them (L3 stays shared with the ghosts' toggle).
    // 2.79: the same for the order wheel and the thoughts of a raid test's simulated players (C, V, R3).
    // 2.91: the same for training's StartPos and measure points (F5 to F8).
    for (Action added : {Action::RaidMenu, Action::BotOrders, Action::BotThoughts, Action::StartPosPlace, Action::StartPosPrev, Action::StartPosNext,
                         Action::MeasurePlace}) {
        const int menu = static_cast<int>(added);
        auto lacksMenu = [&](const char* device) { return !j.contains(device) || !j[device].is_object() || !j[device].contains(actionId(added)); };
        if (lacksMenu("keyboard") && d.keys[menu][0] != SDL_SCANCODE_UNKNOWN)
            for (int o = 0; o < kFirstMenuAction; ++o)
                if (o != menu && (b.keys[o][0] == d.keys[menu][0] || b.keys[o][1] == d.keys[menu][0])) b.keys[menu] = {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN};
        if (lacksMenu("gamepad") && d.pad[menu][0].kind != PadBinding::Kind::None)
            for (int o = 0; o < kFirstMenuAction; ++o)
                if (o != menu && !(added == Action::RaidMenu && o == static_cast<int>(Action::GhostToggle)) &&
                    (b.pad[o][0] == d.pad[menu][0] || b.pad[o][1] == d.pad[menu][0]))
                    b.pad[menu] = {PadBinding{}, PadBinding{}};
    }
    b.keepOneBinding();
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

GamepadStyle gamepadStyleOf(SDL_GamepadType type) {
    switch (type) {
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE:
        case SDL_GAMEPAD_TYPE_STANDARD: return GamepadStyle::Xbox;
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: return GamepadStyle::PlayStation;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return GamepadStyle::Switch;
        default: return GamepadStyle::Generic;
    }
}

std::string padBindingName(const PadBinding& b, GamepadStyle style) {
    static const char* xbox[] = {"A", "B", "X", "Y"};
    static const char* ps[] = {"Croix", "Rond", "Carré", "Triangle"};
    static const char* gen[] = {"Bas", "Droite", "Gauche", "Haut"};
    static const char* sw[] = {"B", "A", "Y", "X"};  // Nintendo: B at the bottom, A at the right
    const bool nintendo = style == GamepadStyle::Switch;
    switch (b.kind) {
        case PadBinding::Kind::None: return "—";
        case PadBinding::Kind::Button: {
            auto btn = static_cast<SDL_GamepadButton>(b.code);
            if (btn >= SDL_GAMEPAD_BUTTON_SOUTH && btn <= SDL_GAMEPAD_BUTTON_NORTH) {
                const char** names = style == GamepadStyle::Xbox ? xbox : style == GamepadStyle::PlayStation ? ps : nintendo ? sw : gen;
                return names[btn];
            }
            if (nintendo) {
                switch (btn) {
                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L";
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R";
                    case SDL_GAMEPAD_BUTTON_START: return "+";
                    case SDL_GAMEPAD_BUTTON_BACK: return "−";
                    case SDL_GAMEPAD_BUTTON_MISC1: return "Capture";
                    default: break;
                }
            }
            switch (btn) {
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return style == GamepadStyle::PlayStation ? "L1" : "LB";
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return style == GamepadStyle::PlayStation ? "R1" : "RB";
                case SDL_GAMEPAD_BUTTON_START: return style == GamepadStyle::PlayStation ? "Options" : "Menu";
                case SDL_GAMEPAD_BUTTON_BACK: return style == GamepadStyle::PlayStation ? "Partage" : "Vue";
                case SDL_GAMEPAD_BUTTON_LEFT_STICK: return style == GamepadStyle::PlayStation ? "L3" : "LS";
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return style == GamepadStyle::PlayStation ? "R3" : "RS";
                case SDL_GAMEPAD_BUTTON_MISC1: return style == GamepadStyle::PlayStation ? "Micro" : "Partage";
                case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Pavé tactile";
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
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return style == GamepadStyle::PlayStation ? "L2" : nintendo ? "ZL" : "LT";
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return style == GamepadStyle::PlayStation ? "R2" : nintendo ? "ZR" : "RT";
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

std::string mouseBindingDetail(SDL_Scancode sc) {
    switch (mouseBindingButton(sc)) {
        case 0: break;
        case SDL_BUTTON_LEFT: return "clic gauche";
        case SDL_BUTTON_MIDDLE: return "clic de la molette";
        case SDL_BUTTON_RIGHT: return "clic droit";
        case SDL_BUTTON_X1: return "bouton latéral arrière";
        case SDL_BUTTON_X2: return "bouton latéral avant";
        default: return "bouton supplémentaire";
    }
    return "";  // a key, or the wheel (its short name says it all)
}

std::string keyName(SDL_Scancode sc) {
    if (sc == SDL_SCANCODE_UNKNOWN) return "—";
    // Mouse: « Souris 1 », « Souris 4 »… (short: it is also a keycap in the HUD).
    if (int b = mouseBindingButton(sc)) {
        std::string t = "Souris {0}";  // a model of the dictionaries (« Mouse 4 »)
        t.replace(t.find("{0}"), 3, std::to_string(b));
        return t;
    }
    if (static_cast<int>(sc) == kWheelUpBinding) return "Molette ↑";
    if (static_cast<int>(sc) == kWheelDownBinding) return "Molette ↓";
    if (!isKeyboardScancode(sc)) return "?";
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
