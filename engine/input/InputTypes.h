#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
    // in that slot (InputFrame bits, replays). Since 2.31 the settings give these no key: the
    // scene that plays binds each one to the key of the power carried there, chosen per
    // level (game/powers/PowerKeys.h), so a power keeps its key whatever its slot.
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
    RaidMap,      // 2.72: the raid's large map, opened and closed (display only, never recorded)
    RaidMenu,     // 2.75.5: the raid's menu (what is left, zones finished, journal, group, titan), never recorded
    BotOrders,    // 2.79: a raid test's order wheel for the simulated players (the order rides on the frame, not this key)
    BotThoughts,  // 2.79: show / hide what the simulated players think (display only, never recorded)
    // 2.91, a training run (docs/GHOST_RACING.md): a StartPos placed here, the previous / next one
    // chosen, a measure point placed here (A, then B). Never bits of the recorded actions: the scene
    // sets the frame's mark flags from them (InputFrame::PlaceStartPos…), which the replay keeps.
    StartPosPlace,
    StartPosPrev,
    StartPosNext,
    MeasurePlace,
    // 2.93 (docs/CONTROLES.md): the keys the screens read, rebindable like the commands of the game
    // (Paramètres › Contrôles). Never bits of the recorded actions: replays never depend on them.
    Restart,            // R / Vue: the quick restart (contre-la-montre, entraînement, défis, fins d'essai)
    ReplayPlay,         // the replay viewer (and the montage « Regarder »)
    ReplayBack,
    ReplayForward,
    ReplayFramePrev,
    ReplayFrameNext,
    ReplayStart,
    ReplayEnd,
    ReplaySlower,
    ReplayFaster,
    ReplayInfo,
    ReplayCamera,
    ReplayPaths,
    ReplayAlign,
    FilmRestart,        // films and boss stories
    FilmMode,
    GiftEquip,          // a reward revealed (gifts)
    ViewZoomIn,         // views that zoom (web of the achievements, parts of an enemy)
    ViewZoomOut,
    ViewPartNext,       // parts of an enemy (hit parts)
    ViewPartPrev,
    SheetReset,         // a raid's heart sheet: back to the clan's real state
    EditorTest,         // the level editor
    EditorHelp,
    EditorSelect,
    EditorPan,
    EditorRotate,
    EditorFlip,
    EditorSnap,
    EditorMinimap,
    EditorZoomReset,
    StudioPencil,       // the Studio of the creations
    StudioEraser,
    StudioFill,
    StudioPicker,
    StudioLine,
    StudioRect,
    StudioEllipse,
    StudioSelect,
    StudioMove,
    StudioMirror,
    StudioSymmetry,
    StudioSwapColors,
    StudioPlay,
    StudioFramePrev,
    StudioFrameNext,
    StudioRename,
    StudioUndo,
    StudioRedo,
    Fullscreen,         // every screen (read by the application)
    Screenshot,
    DebugOverlay,
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
/// interact, pause...), except the buttons of the powers before 2.24 and, since 2.31, the
/// power slots (their keys follow the powers carried, see PowerSlot1).
bool actionIsRebindable(Action a);
/// One of the buttons of the powers before 2.24 (Dash, Onde, Bouclier, Impulsion,
/// Téléportation, Spécial): bound to nothing, read only from old replays and scripts.
bool actionIsLegacyPowerKey(Action a);
/// What a power definition may name as its `input` (MoveLeft .. Special).
bool actionIsPowerInput(Action a);
Action actionFromId(const std::string& id);

/// 2.93: where a command is read (docs/CONTROLES.md). Two commands may share a key only when no
/// screen reads both: a conflict is looked for among the commands whose scopes meet
/// (actionsConflict). The menus' keys are fixed and never compared (each screen reads them too).
enum BindingScope : uint32_t {
    kScopeRun = 1u << 0,          // a level played (GameScene: course, contre-la-montre, entraînement, défis)
    kScopeRaid = 1u << 1,         // a raid, its tests
    kScopeReplay = 1u << 2,       // the replay viewer, the montage « Regarder »
    kScopeFilm = 1u << 3,         // films and boss stories
    kScopeGifts = 1u << 4,        // a gift opened
    kScopeViews = 1u << 5,        // web of the achievements, parts of the enemies, heart sheet of a raid
    kScopeLevelEditor = 1u << 6,  // the level editor
    kScopeStudio = 1u << 7,       // the Studio of the creations
    kScopePlay = kScopeRun | kScopeRaid,  // moving and fighting: every game (alone, in a team, in a raid)
    kScopeEverywhere = 0xFFFFFFFFu,       // read by the application on every screen (F11, F12, F3)
};
/// The section of Paramètres › Contrôles that lists a command (the order of the list).
enum class BindingGroup { Moves, Game, Ghosts, Training, Raid, Replay, Film, Screens, LevelEditor, Studio, Everywhere, Count };
uint32_t actionScope(Action a);
BindingGroup actionGroup(Action a);
/// The title of a section (French; translated when drawn).
const char* bindingGroupTitle(BindingGroup g);
/// `a` and `b` are two different commands some screen reads together: one key (or button) cannot
/// do both. Always false for a menu action or a command that is not rebindable.
bool actionsConflict(Action a, Action b);
/// The gamepad tab lists it (false: keyboard only, the editors' and the application's keys).
bool actionHasPadBinding(Action a);
/// A key of the keyboard only (no mouse button, no wheel): the application's commands, read
/// before any screen (F11, F12, F3).
bool actionKeyboardOnly(Action a);

enum class InputDevice { Keyboard, Gamepad };
/// The glyphs of a gamepad (2.65: Switch, its pads and Joy-Con, with its own names: B at the
/// bottom, A at the right, + / −, ZL / ZR).
enum class GamepadStyle { Xbox, PlayStation, Generic, Switch };
/// The style of a gamepad type as SDL reports it.
GamepadStyle gamepadStyleOf(SDL_GamepadType type);

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
    /// The commands of the settings only: never the power slots (2.31) nor the buttons of the
    /// powers before 2.24.
    Json toJson() const;
    /// The keys of the power slots (2.24 to 2.31) and the buttons of the powers (before 2.24)
    /// of older files are ignored: the keys of the powers are chosen per level since 2.31.
    static InputBindings fromJson(const Json& j);
    /// One key and one button per command (2.29: the settings show a single column). The
    /// second place holds only the fixed alternates: the arrows (keyboard) and the d-pad
    /// (gamepad) for moving, the touchpad for the focus mode on PlayStation pads, the keypad's
    /// + and − (2.93); anything else there is dropped (a lone second key becomes the first).
    void keepOneBinding();

    /// 2.93: the key (or mouse button) shown for `a`: its first one, SDL_SCANCODE_UNKNOWN for none.
    SDL_Scancode keyOf(Action a) const;
    /// The gamepad binding shown for `a`: its first one, Kind::None for none.
    PadBinding padOf(Action a) const;
    /// The commands that would lose `key` (or `pad`) if `a` took it: those a screen reads with `a`.
    std::vector<Action> keyConflicts(Action a, SDL_Scancode key) const;
    std::vector<Action> padConflicts(Action a, const PadBinding& pad) const;
    /// `a` takes `key` / `pad` (its single binding); the commands in conflict lose it (returned, in
    /// the order of the list), the others keep it (another screen: no conflict).
    std::vector<Action> bindKey(Action a, SDL_Scancode key);
    std::vector<Action> bindPad(Action a, const PadBinding& pad);
    /// « Par défaut »: `a` alone back to its key (or button) of origin, taken from the commands in conflict.
    std::vector<Action> resetKey(Action a);
    std::vector<Action> resetPad(Action a);
};

/// 2.93: the name of `a`'s key in the hints (« R », « Souris 4 », « — » for none), and of its gamepad
/// binding (« Vue », « Y », « — »): the hints of the screens follow the player's choices.
std::string actionKeyName(const InputBindings& b, Action a);
std::string actionPadName(const InputBindings& b, Action a, GamepadStyle style);

/// 2.93: a key the menus use (arrows, ZQSD / WASD, Entrée, Espace, Échap, Retour arrière, onglets):
/// the application's commands, read before any screen, never take one (the menus would lose it).
bool isMenuKey(SDL_Scancode sc);

/// The fixed alternate of a command (always active, not shown as a choice): an arrow for
/// moving, SDL_SCANCODE_UNKNOWN for the others.
SDL_Scancode builtInKeyAlternate(Action a);
PadBinding builtInPadAlternate(Action a);

/// Index in the glyph sheets (assets/ui/glyphs/*.png, docs/ASSET_SPEC.md section 11).
int glyphIndex(const PadBinding& b);
std::string padBindingName(const PadBinding& b, GamepadStyle style);
std::string keyName(SDL_Scancode sc);

/// Mouse buttons and the wheel in the keyboard / mouse bindings (2.30.2). They take codes after
/// the keyboard's scancodes, so a binding stays one SDL_Scancode. Every button the system
/// reports counts (1 left, 2 middle, 3 right, 4 and 5 the side buttons, more on mice whose
/// driver sends them as buttons, such as some Razer or Logitech models).
constexpr int kMouseBindingBase = static_cast<int>(SDL_SCANCODE_COUNT) + 8;
constexpr int kMouseButtonsBindable = 32;
constexpr int kWheelUpBinding = kMouseBindingBase + kMouseButtonsBindable + 1;
constexpr int kWheelDownBinding = kWheelUpBinding + 1;
inline SDL_Scancode mouseButtonBinding(int button) { return static_cast<SDL_Scancode>(kMouseBindingBase + button); }
/// The mouse button of a binding (1..kMouseButtonsBindable), 0 for a key or the wheel.
inline int mouseBindingButton(SDL_Scancode sc) {
    int b = static_cast<int>(sc) - kMouseBindingBase;
    return b >= 1 && b <= kMouseButtonsBindable ? b : 0;
}
inline bool isWheelBinding(SDL_Scancode sc) { return static_cast<int>(sc) == kWheelUpBinding || static_cast<int>(sc) == kWheelDownBinding; }
inline bool isMouseBinding(SDL_Scancode sc) { return mouseBindingButton(sc) != 0 || isWheelBinding(sc); }
/// A real key of the keyboard (an index of SDL_GetKeyboardState).
inline bool isKeyboardScancode(SDL_Scancode sc) { return sc > SDL_SCANCODE_UNKNOWN && sc < SDL_SCANCODE_COUNT; }
/// What a mouse binding is, in words, for the settings (« clic gauche », « bouton latéral »…);
/// "" for a key.
std::string mouseBindingDetail(SDL_Scancode sc);

/// A keyboard / mouse binding as files write it (settings, levels' `powerKeys`): the SDL
/// name of a key (« K », « Left Shift »), « Mouse4 », « WheelUp », « WheelDown »; "" for none.
std::string bindingName(SDL_Scancode sc);
/// The binding of such a name; SDL_SCANCODE_UNKNOWN for "" or a name this game does not know.
SDL_Scancode bindingFromName(const std::string& name);
/// A gamepad binding as files write it: {"button": "b"}, {"axis": "righttrigger", "dir": 1},
/// null for none.
Json padBindingToJson(const PadBinding& b);
PadBinding padBindingFromJson(const Json& j);

}  // namespace cosmic
