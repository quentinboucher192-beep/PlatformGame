// 2.93: every key and button the game reacts to is in Paramètres › Contrôles (docs/CONTROLES.md):
// the inventory and its defaults, the conflicts on the same screens only, the settings files of
// before, the hints that follow a rebinding, and the replays that never depend on the bindings.

#include <SDL3/SDL.h>

#include "engine/input/InputManager.h"
#include "game/gameplay/InputFrame.h"
#include "game/powers/PowerKeys.h"
#include "tests/TestFramework.h"
#include "ui/widgets/Glyphs.h"

using namespace cosmic;

namespace {

int at(Action a) { return static_cast<int>(a); }
using B = PadBinding;

/// The inventory (docs/CONTROLES.md): each command made rebindable in 2.93, with the key and the
/// button it had before (Kind::None: no button; SDL_SCANCODE_UNKNOWN: no key).
struct Entry {
    Action action;
    SDL_Scancode key;
    PadBinding pad;
};
const Entry kInventory[] = {
    {Action::ChatOpen, SDL_SCANCODE_T, B::button(SDL_GAMEPAD_BUTTON_MISC1)},                // 2.95: the raid's chat
    {Action::ChatChannel, SDL_SCANCODE_TAB, B::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},  // 2.95: its channel
    {Action::Restart, SDL_SCANCODE_R, B::button(SDL_GAMEPAD_BUTTON_BACK)},
    {Action::ReplayPlay, SDL_SCANCODE_K, B::button(SDL_GAMEPAD_BUTTON_WEST)},
    {Action::ReplayBack, SDL_SCANCODE_J, B::axisNeg(SDL_GAMEPAD_AXIS_RIGHTX)},
    {Action::ReplayForward, SDL_SCANCODE_L, B::axisPos(SDL_GAMEPAD_AXIS_RIGHTX)},
    {Action::ReplayFramePrev, SDL_SCANCODE_COMMA, B::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)},
    {Action::ReplayFrameNext, SDL_SCANCODE_PERIOD, B::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)},
    {Action::ReplayStart, SDL_SCANCODE_HOME, {}},
    {Action::ReplayEnd, SDL_SCANCODE_END, {}},
    {Action::ReplaySlower, SDL_SCANCODE_MINUS, {}},
    {Action::ReplayFaster, SDL_SCANCODE_EQUALS, {}},
    {Action::ReplayInfo, SDL_SCANCODE_I, B::button(SDL_GAMEPAD_BUTTON_BACK)},
    {Action::ReplayCamera, SDL_SCANCODE_C, B::button(SDL_GAMEPAD_BUTTON_NORTH)},
    {Action::ReplayPaths, SDL_SCANCODE_T, {}},
    {Action::ReplayAlign, SDL_SCANCODE_R, {}},
    {Action::ReplayTasSheet, SDL_SCANCODE_F, {}},
    {Action::FilmRestart, SDL_SCANCODE_R, B::button(SDL_GAMEPAD_BUTTON_NORTH)},
    {Action::FilmMode, SDL_SCANCODE_F, {}},
    {Action::GiftEquip, SDL_SCANCODE_Y, B::button(SDL_GAMEPAD_BUTTON_NORTH)},
    {Action::ViewZoomIn, SDL_SCANCODE_EQUALS, {}},
    {Action::ViewZoomOut, SDL_SCANCODE_MINUS, {}},
    {Action::ViewPartNext, SDL_SCANCODE_TAB, B::button(SDL_GAMEPAD_BUTTON_NORTH)},
    {Action::ViewPartPrev, SDL_SCANCODE_UNKNOWN, B::button(SDL_GAMEPAD_BUTTON_WEST)},
    {Action::SheetReset, SDL_SCANCODE_R, {}},
    {Action::EditorTest, SDL_SCANCODE_F5, {}},
    {Action::EditorHelp, SDL_SCANCODE_F1, {}},
    {Action::EditorSelect, SDL_SCANCODE_V, {}},
    {Action::EditorPan, SDL_SCANCODE_H, {}},
    {Action::EditorRotate, SDL_SCANCODE_R, {}},
    {Action::EditorFlip, SDL_SCANCODE_F, {}},
    {Action::EditorSnap, SDL_SCANCODE_G, {}},
    {Action::EditorMinimap, SDL_SCANCODE_M, {}},
    {Action::EditorZoomReset, SDL_SCANCODE_HOME, {}},
    {Action::StudioPencil, SDL_SCANCODE_B, {}},
    {Action::StudioEraser, SDL_SCANCODE_E, {}},
    {Action::StudioFill, SDL_SCANCODE_G, {}},
    {Action::StudioPicker, SDL_SCANCODE_I, {}},
    {Action::StudioLine, SDL_SCANCODE_L, {}},
    {Action::StudioRect, SDL_SCANCODE_R, {}},
    {Action::StudioEllipse, SDL_SCANCODE_O, {}},
    {Action::StudioSelect, SDL_SCANCODE_M, {}},
    {Action::StudioMove, SDL_SCANCODE_V, {}},
    {Action::StudioMirror, SDL_SCANCODE_H, {}},
    {Action::StudioSymmetry, SDL_SCANCODE_S, {}},
    {Action::StudioSwapColors, SDL_SCANCODE_X, {}},
    {Action::StudioPlay, SDL_SCANCODE_P, {}},
    {Action::StudioFramePrev, SDL_SCANCODE_COMMA, {}},
    {Action::StudioFrameNext, SDL_SCANCODE_PERIOD, {}},
    {Action::StudioRename, SDL_SCANCODE_F2, {}},
    {Action::StudioUndo, SDL_SCANCODE_UNKNOWN, B::button(SDL_GAMEPAD_BUTTON_WEST)},
    {Action::StudioRedo, SDL_SCANCODE_UNKNOWN, B::button(SDL_GAMEPAD_BUTTON_NORTH)},
    {Action::Fullscreen, SDL_SCANCODE_F11, {}},
    {Action::Screenshot, SDL_SCANCODE_F12, {}},
    {Action::DebugOverlay, SDL_SCANCODE_F3, {}},
};

SDL_Event keyDown(SDL_Scancode sc, bool repeat = false) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = sc;
    e.key.repeat = repeat;
    e.key.down = true;
    return e;
}

}  // namespace

TEST_CASE("controls (2.93): every inventoried command is rebindable, listed in a section, with its key and button of before") {
    const InputBindings d = InputBindings::defaults();
    int n = 0;
    for (const Entry& e : kInventory) {
        ++n;
        CHECK(actionIsRebindable(e.action));
        CHECK(actionFromId(actionId(e.action)) == e.action);
        CHECK(std::string(actionLabel(e.action)).size() > 2);
        CHECK(actionScope(e.action) != 0u);
        CHECK(actionGroup(e.action) != BindingGroup::Count);
        CHECK(std::string(bindingGroupTitle(actionGroup(e.action))).size() > 2);
        CHECK(at(e.action) >= InputFrame::kActions);  // never a bit of the recorded actions
        CHECK(d.keys[at(e.action)][0] == e.key);
        CHECK(d.pad[at(e.action)][0] == e.pad);
        if (!actionHasPadBinding(e.action)) CHECK(e.pad.kind == PadBinding::Kind::None);
    }
    CHECK(n == 55);
    // The names line up with the commands (the table of InputTypes.cpp is as long as the enum).
    CHECK(std::string(actionId(Action::DebugOverlay)) == "debug_overlay");
    CHECK(std::string(actionId(Action::MenuTabNext)) == "menu_tab_next");
    // The keypad's + and − stay with the speed of the replays and the zoom of the views.
    CHECK(d.keys[at(Action::ReplaySlower)][1] == SDL_SCANCODE_KP_MINUS);
    CHECK(d.keys[at(Action::ViewZoomIn)][1] == SDL_SCANCODE_KP_PLUS);
    // Every command of the settings has a section; the menus' keys stay fixed.
    for (int a = 0; a < kActionCount; ++a) {
        const Action act = static_cast<Action>(a);
        CHECK(actionIsRebindable(act) == (actionScope(act) != 0u));
        if (actionIsRebindable(act)) CHECK(actionGroup(act) != BindingGroup::Count);
        if (a >= kFirstMenuAction) CHECK(!actionIsRebindable(act));
    }
    // The application's commands: the keyboard only, never a menu's key.
    for (Action a : {Action::Fullscreen, Action::Screenshot, Action::DebugOverlay}) {
        CHECK(actionKeyboardOnly(a));
        CHECK(!actionHasPadBinding(a));
        CHECK(!isMenuKey(d.keyOf(a)));
    }
    CHECK(isMenuKey(SDL_SCANCODE_RETURN) && isMenuKey(SDL_SCANCODE_ESCAPE) && isMenuKey(SDL_SCANCODE_UP));
    CHECK(!isMenuKey(SDL_SCANCODE_F11));
}

TEST_CASE("controls (2.93): no two commands read on the same screen share a default key or button") {
    const InputBindings d = InputBindings::defaults();
    for (int a = 0; a < kFirstMenuAction; ++a)
        for (int o = 0; o < kFirstMenuAction; ++o) {
            if (!actionsConflict(static_cast<Action>(a), static_cast<Action>(o))) continue;
            for (SDL_Scancode k : d.keys[a])
                if (k != SDL_SCANCODE_UNKNOWN) CHECK(d.keys[o][0] != k && d.keys[o][1] != k);
            for (const PadBinding& p : d.pad[a])
                if (p.kind != PadBinding::Kind::None) CHECK(!(d.pad[o][0] == p) && !(d.pad[o][1] == p));
        }
    // Shared on purpose, never on the same screen: R, View, L3, T, J…
    CHECK(!actionsConflict(Action::Restart, Action::ReplayAlign));
    CHECK(!actionsConflict(Action::Restart, Action::RaidMap));
    CHECK(!actionsConflict(Action::GhostToggle, Action::RaidMenu));
    CHECK(!actionsConflict(Action::FocusToggle, Action::ReplayPaths));
    CHECK(!actionsConflict(Action::Attack, Action::ReplayBack));
    // Read together: the moves and the restart, the training's marks and a replay's measure point.
    CHECK(actionsConflict(Action::Jump, Action::Restart));
    CHECK(actionsConflict(Action::Jump, Action::RaidMap));
    CHECK(actionsConflict(Action::MeasurePlace, Action::ReplayPlay));
    CHECK(actionsConflict(Action::Screenshot, Action::StudioPencil));  // the application's: every screen
    CHECK(!actionsConflict(Action::Jump, Action::Jump));
    CHECK(!actionsConflict(Action::Jump, Action::MenuConfirm));  // the menus are never compared
}

TEST_CASE("controls (2.93): conflicts are looked for on the same screens only, « Par défaut » and « Réinitialiser »") {
    InputBindings b = InputBindings::defaults();
    // A replay's key taken by another replay command: that one loses it, the attack keeps J.
    std::vector<Action> lost = b.bindKey(Action::ReplayPlay, SDL_SCANCODE_J);
    CHECK(lost.size() == 1 && lost[0] == Action::ReplayBack);
    CHECK(b.keyOf(Action::ReplayPlay) == SDL_SCANCODE_J);
    CHECK(b.keyOf(Action::ReplayBack) == SDL_SCANCODE_UNKNOWN);
    CHECK(b.keyOf(Action::Attack) == SDL_SCANCODE_J);
    // The restart on T: the focus mode (same game) loses it, the replay's trajectories keep it.
    lost = b.bindKey(Action::Restart, SDL_SCANCODE_T);
    CHECK(lost.size() == 1 && lost[0] == Action::FocusToggle);
    CHECK(b.keyOf(Action::ReplayPaths) == SDL_SCANCODE_T);
    CHECK(b.keyConflicts(Action::FocusToggle, SDL_SCANCODE_T) == std::vector<Action>{Action::Restart});
    // A gamepad button: the replay's camera loses Y, « Interagir » keeps it.
    lost = b.bindPad(Action::ReplayInfo, PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(lost.size() == 1 && lost[0] == Action::ReplayCamera);
    CHECK(b.padOf(Action::Interact) == PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    // The application's commands are read on every screen: everyone with that key loses it.
    lost = b.bindKey(Action::Screenshot, SDL_SCANCODE_J);
    CHECK(lost.size() == 2);  // Attack and « Lecture / pause »
    CHECK(b.keyOf(Action::Attack) == SDL_SCANCODE_UNKNOWN && b.keyOf(Action::ReplayPlay) == SDL_SCANCODE_UNKNOWN);
    // A keyboard-only command takes no button.
    CHECK(b.bindPad(Action::EditorTest, PadBinding::button(SDL_GAMEPAD_BUTTON_SOUTH)).empty());
    CHECK(b.padOf(Action::EditorTest).kind == PadBinding::Kind::None);
    // « Par défaut »: its key back, taken from the command of the same game that had it.
    lost = b.resetKey(Action::FocusToggle);
    CHECK(lost.size() == 1 && lost[0] == Action::Restart);
    CHECK(b.keyOf(Action::FocusToggle) == SDL_SCANCODE_T);
    CHECK(b.keyOf(Action::Restart) == SDL_SCANCODE_UNKNOWN);
    b.resetKey(Action::Restart);
    CHECK(b.keyOf(Action::Restart) == SDL_SCANCODE_R);
    // « Réinitialiser »: every default again.
    b.resetKeyboard();
    b.resetGamepad();
    const InputBindings d = InputBindings::defaults();
    for (int a = 0; a < kActionCount; ++a) {
        CHECK(b.keys[a] == d.keys[a]);
        CHECK(b.pad[a] == d.pad[a]);
    }
}

TEST_CASE("controls (2.93): the settings keep the new keys, and a file of before gets their defaults") {
    // Rebound, saved, read back.
    InputBindings b = InputBindings::defaults();
    b.bindKey(Action::ReplayPlay, SDL_SCANCODE_P);
    b.bindPad(Action::Restart, PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));  // « Interagir » loses Y
    b.bindKey(Action::StudioPencil, mouseButtonBinding(4));
    b.bindKey(Action::Fullscreen, SDL_SCANCODE_F10);
    const InputBindings back = InputBindings::fromJson(b.toJson());
    for (int a = 0; a < kActionCount; ++a) {
        CHECK(back.keys[a] == b.keys[a]);
        CHECK(back.pad[a] == b.pad[a]);
    }
    CHECK(back.keyOf(Action::ReplayPlay) == SDL_SCANCODE_P);
    CHECK(back.padOf(Action::Restart) == PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(back.padOf(Action::Interact).kind == PadBinding::Kind::None);
    CHECK(back.keyOf(Action::StudioPencil) == mouseButtonBinding(4));
    CHECK(back.keyOf(Action::Fullscreen) == SDL_SCANCODE_F10);
    // A file of 2.92 (without the commands of 2.93), where the player put « Interagir » on R and the
    // attack on K: the restart loses R (the same game), « Recaler » keeps it (a replay's), the
    // replay's play keeps K, every other new command has its default.
    Json old = InputBindings::defaults().toJson();
    for (const Entry& e : kInventory) {
        old["keyboard"].erase(actionId(e.action));
        old["gamepad"].erase(actionId(e.action));
    }
    old["keyboard"][actionId(Action::Interact)] = Json::array({"R", ""});
    old["keyboard"][actionId(Action::Attack)] = Json::array({"K", ""});
    old["gamepad"][actionId(Action::Jump)] = Json::array({Json{{"button", "back"}}, nullptr});
    const InputBindings o = InputBindings::fromJson(old);
    CHECK(o.keyOf(Action::Interact) == SDL_SCANCODE_R);
    CHECK(o.keyOf(Action::Restart) == SDL_SCANCODE_UNKNOWN);
    CHECK(o.padOf(Action::Restart).kind == PadBinding::Kind::None);  // View is the jump there
    CHECK(o.padOf(Action::ReplayInfo) == PadBinding::button(SDL_GAMEPAD_BUTTON_BACK));
    CHECK(o.keyOf(Action::ReplayAlign) == SDL_SCANCODE_R);
    CHECK(o.keyOf(Action::ReplayPlay) == SDL_SCANCODE_K);
    const InputBindings d = InputBindings::defaults();
    for (const Entry& e : kInventory) {
        if (e.action == Action::Restart) continue;
        CHECK(o.keys[at(e.action)] == d.keys[at(e.action)]);
        CHECK(o.pad[at(e.action)] == d.pad[at(e.action)]);
    }
    // A file with almost nothing: every default.
    const InputBindings min = InputBindings::fromJson(Json::parse(R"({"keyboard": {"jump": ["Space", ""]}})"));
    for (int a = 0; a < kActionCount; ++a) CHECK(min.keys[a] == d.keys[a]);
}

TEST_CASE("controls (2.93): the screens' presses by device, the system's repeat, a rebinding, and never a bit of a replay") {
    InputManager in;
    InputBindings b = InputBindings::defaults();
    in.setBindings(b);
    in.beginFrame();
    SDL_Event e = keyDown(SDL_SCANCODE_K);
    in.handleEvent(e);
    CHECK(in.menuPressed(Action::ReplayPlay));
    CHECK(in.keyPressedFor(Action::ReplayPlay));
    CHECK(in.commandPressed(Action::ReplayPlay));
    CHECK(!in.padPressed(Action::ReplayPlay));
    // The keys of the screens never reach the simulation: the recorded frame is empty.
    CHECK(InputFrame::fromPlayerInput(in.consumePlayerInput()) == InputFrame{});
    // The system's repeat: a shortcut that repeats (commandPressed), not a new press.
    in.beginFrame();
    e = keyDown(SDL_SCANCODE_K, true);
    in.handleEvent(e);
    CHECK(!in.menuPressed(Action::ReplayPlay));
    CHECK(!in.keyPressedFor(Action::ReplayPlay));
    CHECK(in.keyPressedFor(Action::ReplayPlay, true));
    CHECK(in.commandPressed(Action::ReplayPlay));
    // Rebound: K does nothing any more, P plays.
    b.bindKey(Action::ReplayPlay, SDL_SCANCODE_P);
    in.setBindings(b);
    in.beginFrame();
    e = keyDown(SDL_SCANCODE_K);
    in.handleEvent(e);
    CHECK(!in.commandPressed(Action::ReplayPlay));
    in.beginFrame();
    e = keyDown(SDL_SCANCODE_P);
    in.handleEvent(e);
    CHECK(in.commandPressed(Action::ReplayPlay));
    // The restart's key: a command of its own, never recorded either.
    in.beginFrame();
    e = keyDown(SDL_SCANCODE_R);
    in.handleEvent(e);
    CHECK(in.menuPressed(Action::Restart));
    CHECK(InputFrame::fromPlayerInput(in.consumePlayerInput()) == InputFrame{});
    // Typing in a text field: no shortcut of a screen.
    in.startTextInput(nullptr);
    in.beginFrame();
    in.handleEvent(e);
    CHECK(!in.menuPressed(Action::Restart));
    in.stopTextInput(nullptr);
}

TEST_CASE("controls (2.93): the powers' keys keep clear of the commands of a game only, the restart's included") {
    InputBindings b = InputBindings::defaults();
    CHECK(baseCommandOfKey(b, SDL_SCANCODE_R) == actionLabel(Action::Restart));
    CHECK(baseCommandOfKey(b, SDL_SCANCODE_K).empty());  // the replay's play: never read in a level
    CHECK(baseCommandOfKey(b, SDL_SCANCODE_F12) == actionLabel(Action::Screenshot));  // read before the game
    CHECK(baseCommandOfPad(b, PadBinding::button(SDL_GAMEPAD_BUTTON_BACK)) == actionLabel(Action::Restart));
    // The restart moved to U: R is free for a power, U is not.
    b.bindKey(Action::Restart, SDL_SCANCODE_U);
    b.bindPad(Action::Restart, PadBinding::button(SDL_GAMEPAD_BUTTON_DPAD_UP));  // the move up loses its d-pad alternate
    CHECK(baseCommandOfKey(b, SDL_SCANCODE_R).empty());
    CHECK(baseCommandOfKey(b, SDL_SCANCODE_U) == "Recommencer");
    CHECK(baseCommandOfPad(b, PadBinding::button(SDL_GAMEPAD_BUTTON_BACK)).empty());  // the raid's map is a raid's
}

TEST_CASE("controls (2.93): a hint in words follows the player's key and button") {
    InputManager in;
    InputBindings b = InputBindings::defaults();
    in.setBindings(b);
    // « R / Vue : recommencer » (the training's tag, the challenges', a hardcore level's).
    CHECK(Glyphs::keyAndButton(in, Action::Restart) == keyName(SDL_SCANCODE_R) + " / " + padBindingName(PadBinding::button(SDL_GAMEPAD_BUTTON_BACK), in.gamepadStyle()));
    b.bindKey(Action::Restart, SDL_SCANCODE_U);
    b.bindPad(Action::Restart, PadBinding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    in.setBindings(b);
    CHECK(Glyphs::keyAndButton(in, Action::Restart) == keyName(SDL_SCANCODE_U) + " / " + padBindingName(PadBinding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), in.gamepadStyle()));
    // No key: « — ».
    b.bindKey(Action::Restart, SDL_SCANCODE_UNKNOWN);
    in.setBindings(b);
    CHECK(actionKeyName(in.bindings(), Action::Restart) == "—");
}
