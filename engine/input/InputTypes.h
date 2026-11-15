#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>

#include "engine/core/Json.h"

namespace cosmic {

/// Logical actions. Gameplay actions are rebindable; menu actions have fixed defaults
/// plus whatever device the player uses (keyboard arrows/Enter, gamepad D-pad/A/B).
/// The order of MoveLeft .. PowerSlot8 is the bit order of the recorded inputs
/// (InputFrame, replays): never reorder them.
enum class Action : int {
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Jump,
    Attack,
    // One button per power until 2.23. Since 2.24 the player binds the power slots instead
    // (PowerSlot1..8): these are bound to nothing; they stay for the replays recorded before
    // (their bits) and for the power definitions, which still name them (`input`).
    Dash,
    Wave,
    Shield,
    Impulse,
    Teleport,
    Interact,
    Special,  // power on the special button (time slow, plasma bomb...), see Dash
    // 2.24: the key of each power slot, in the order of the HUD: it uses the power equipped
    // in that slot, whatever it is (Paramètres › Clavier / souris, Manette).
    PowerSlot1,
    PowerSlot2,
    PowerSlot3,
    PowerSlot4,
    PowerSlot5,
    PowerSlot6,
    PowerSlot7,
    PowerSlot8,
    Pause,
    GhostToggle,  // 2.18: show / hide the ghosts of Ghost Racing (display only, never recorded)
    FocusToggle,  // 2.18: « mode concentré », the reduced HUD (display only, never recorded)
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    MenuConfirm,
    MenuBack,
    MenuTabPrev,
    MenuTabNext,
    Count
};

constexpr int kActionCount = static_cast<int>(Action::Count);
constexpr int kFirstMenuAction = static_cast<int>(Action::MenuUp);
/// Power slots with a key (LevelData::kMaxPowerSlots): « Pouvoir 1 » .. « Pouvoir 8 ».
constexpr int kPowerSlotKeys = 8;
static_assert(static_cast<int>(Action::PowerSlot8) - static_cast<int>(Action::PowerSlot1) + 1 == kPowerSlotKeys);

/// The key of a power slot (0-based, the order of the HUD); Action::Count beyond the last.
constexpr Action powerSlotAction(int slot) {
    return slot >= 0 && slot < kPowerSlotKeys ? static_cast<Action>(static_cast<int>(Action::PowerSlot1) + slot) : Action::Count;
}
/// The slot (0-based) whose key `a` is, -1 for any other action.
constexpr int powerSlotOf(Action a) {
    int i = static_cast<int>(a) - static_cast<int>(Action::PowerSlot1);
    return i >= 0 && i < kPowerSlotKeys ? i : -1;
}

const char* actionId(Action a);
const char* actionLabel(Action a);  // French display name
/// The player binds it in the settings: the gameplay actions (moves, jump, attack,
/// interact, power slots, pause...), except the buttons of the powers before 2.24.
bool actionIsRebindable(Action a);
/// One of the buttons of the powers before 2.24 (Dash, Onde, Bouclier, Impulsion,
/// Téléportation, Spécial): bound to nothing, read only from old replays and scripts.
bool actionIsLegacyPowerKey(Action a);
/// What a power definition may name as its `input` (MoveLeft .. Special).
bool actionIsPowerInput(Action a);
Action actionFromId(const std::string& id);

enum class InputDevice { Keyboard, Gamepad };
enum class GamepadStyle { Xbox, PlayStation, Generic };

/// A gamepad binding: a button, or one direction of an axis (sticks / triggers).
struct PadBinding {
    enum class Kind { None, Button, AxisPositive, AxisNegative };
    Kind kind = Kind::None;
    int code = 0;  // SDL_GamepadButton or SDL_GamepadAxis

    static PadBinding button(SDL_GamepadButton b) { return {Kind::Button, static_cast<int>(b)}; }
    static PadBinding axisPos(SDL_GamepadAxis a) { return {Kind::AxisPositive, static_cast<int>(a)}; }
    static PadBinding axisNeg(SDL_GamepadAxis a) { return {Kind::AxisNegative, static_cast<int>(a)}; }
    bool operator==(const PadBinding&) const = default;
};

struct GamepadSettings {
    float stickDeadzone = 0.22f;
    float triggerThreshold = 0.35f;
    float sensitivity = 1.0f;  // response curve exponent divisor (higher = more responsive)
    bool invertX = false;
    bool invertY = false;
    bool rumble = true;
    bool swapSticks = false;
};

/// Complete control configuration (keyboard/mouse and gamepad are independent).
struct InputBindings {
    std::array<std::array<SDL_Scancode, 2>, kActionCount> keys{};
    std::array<std::array<PadBinding, 2>, kActionCount> pad{};
    GamepadSettings gamepad;

    static InputBindings defaults();
    void resetKeyboard();
    void resetGamepad();
    Json toJson() const;
    /// Settings saved before 2.24 have no power slot keys: the six power buttons of the time
    /// (Dash, Onde, Bouclier, Impulsion, Téléportation, Spécial) become slots 1 to 6, so the
    /// player keeps the keys they chose; a key already used by another action is dropped.
    static InputBindings fromJson(const Json& j);
    /// One key and one button per command (2.29: the settings show a single column). The
    /// second place holds only the fixed alternates: the arrows (keyboard) and the d-pad
    /// (gamepad) for moving, the touchpad for the focus mode on PlayStation pads; anything else
    /// there is dropped (a lone second key becomes the first).
    void keepOneBinding();
};

/// The fixed alternate of a command (always active, not shown as a choice): an arrow for
/// moving, SDL_SCANCODE_UNKNOWN for the others.
SDL_Scancode builtInKeyAlternate(Action a);
PadBinding builtInPadAlternate(Action a);

/// Index in the glyph sheets (assets/ui/glyphs/*.png, docs/ASSET_SPEC.md section 11).
int glyphIndex(const PadBinding& b);
std::string padBindingName(const PadBinding& b, GamepadStyle style);
std::string keyName(SDL_Scancode sc);

}  // namespace cosmic
