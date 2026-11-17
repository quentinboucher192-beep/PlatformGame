// 2.65: the devices of the players of one computer (engine/input/Devices.h): stable numbers of
// the pads, who plays with what, the pause of a run whose device went away, each player's own
// controls (saves/settings.json), the glyphs of each kind of pad.

#include <chrono>

#include "engine/input/Devices.h"
#include "engine/input/InputTypes.h"
#include "game/powers/PowerKeys.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/widgets/Glyphs.h"

using namespace cosmic;

TEST_CASE("Devices: a pad keeps its number, plugged again; unplugged ones listed a few seconds") {
    DeviceRegistry reg;
    CHECK(reg.plugged(10, "xbox|A1", GamepadStyle::Xbox, "Xbox Controller", 1000) == 1);
    CHECK(reg.plugged(11, "ps5|B2", GamepadStyle::PlayStation, "DualSense", 1000) == 2);
    CHECK(reg.plugged(10, "xbox|A1", GamepadStyle::Xbox, "Xbox Controller", 1200) == 1);  // the same event twice
    CHECK(reg.connectedNumbers().size() == 2);
    CHECK(padLabel(*reg.byNumber(1)) == "Manette Xbox (n° 1)");
    CHECK(padLabel(*reg.byNumber(2)) == "Manette PlayStation (n° 2)");
    reg.unplugged(10, 5000);
    CHECK(!reg.connected(1));
    CHECK(reg.byId(10) == nullptr);
    CHECK(reg.listed(9000, 10000).size() == 2);   // unplugged 4 s ago: still listed
    CHECK(reg.listed(16000, 10000).size() == 1);  // 11 s: gone from the list, its number kept
    // Plugged again: SDL gives another id, the pad its number back.
    CHECK(reg.plugged(42, "xbox|A1", GamepadStyle::Xbox, "Xbox Controller", 20000) == 1);
    CHECK(reg.byId(42) && reg.byId(42)->number == 1);
    CHECK(reg.numberOf(42) == 1 && reg.numberOf(10) == 0);
    // Another pad: the next number; two identical pads without serial: the one unplugged last
    // comes back first.
    CHECK(reg.plugged(50, "sw", GamepadStyle::Switch, "Pro Controller", 21000) == 3);
    CHECK(reg.plugged(51, "sw", GamepadStyle::Switch, "Pro Controller", 21000) == 4);
    reg.unplugged(50, 22000);
    reg.unplugged(51, 23000);
    CHECK(reg.plugged(60, "sw", GamepadStyle::Switch, "Pro Controller", 24000) == 4);
    CHECK(reg.plugged(61, "sw", GamepadStyle::Switch, "Pro Controller", 24000) == 3);
    CHECK(padTypeName(GamepadStyle::Generic) == "Manette");
    CHECK(padLabel(*reg.byNumber(3)) == "Manette Switch (n° 3)");
}

TEST_CASE("Devices: one player per device, the keyboard and the mouse to one player only") {
    LocalTeamDevices t;
    CHECK(t.count() == 1 && t.device(0).isKeyboard());  // player 1 starts with the keyboard
    CHECK(t.join(LocalDevice::keyboard()) == -1);       // already player 1's
    CHECK(t.join(LocalDevice::none()) == -1);
    CHECK(t.join(LocalDevice::gamepad(1)) == 1);        // Start / A on a free pad: the next place
    CHECK(t.join(LocalDevice::gamepad(1)) == -1);       // a pad plays for one player only
    CHECK(t.join(LocalDevice::gamepad(2)) == 2);
    CHECK(t.join(LocalDevice::gamepad(3)) == -1);       // three places
    CHECK(t.owner(LocalDevice::gamepad(2)) == 2 && t.owner(LocalDevice::gamepad(3)) == -1);
    // B leaves the place: the next player moves up; player 1 never leaves.
    CHECK(!t.leave(0));
    CHECK(t.leave(1));
    CHECK(t.count() == 2 && t.device(1) == LocalDevice::gamepad(2));
    // « Changer de périphérique »: the next free device (keyboard first, then the pads by number).
    const std::vector<LocalDevice> all{LocalDevice::keyboard(), LocalDevice::gamepad(1), LocalDevice::gamepad(2), LocalDevice::gamepad(3)};
    CHECK(t.cycle(0, all));
    CHECK(t.device(0) == LocalDevice::gamepad(1));  // the keyboard is free now
    CHECK(t.cycle(1, all));
    CHECK(t.device(1) == LocalDevice::gamepad(3));  // pad 1 is player 1's: skipped
    CHECK(t.cycle(1, all));
    CHECK(t.device(1) == LocalDevice::keyboard());  // round: the keyboard
    CHECK(!t.assign(0, LocalDevice::keyboard()));   // another player's
    CHECK(t.assign(0, LocalDevice::gamepad(2)));
    // Nothing free: nothing changes.
    const std::vector<LocalDevice> two{LocalDevice::keyboard(), LocalDevice::gamepad(2)};
    CHECK(!t.cycle(0, two));
    CHECK(t.device(0) == LocalDevice::gamepad(2));
    // A pad gone for good: its player has no device; the list of a game keeps one device each.
    t.dropPad(2);
    CHECK(t.device(0).isNone() && t.isFree(LocalDevice::gamepad(2)));
    t.set({LocalDevice::gamepad(4), LocalDevice::gamepad(4), LocalDevice::keyboard(), LocalDevice::gamepad(5)});
    CHECK(t.count() == 3);
    CHECK(t.device(0) == LocalDevice::gamepad(4) && t.device(1).isNone() && t.device(2).isKeyboard());
}

TEST_CASE("Devices: a device lost pauses the run, 3-2-1 once every player has one again") {
    UnplugPause p;
    p.reset(3);
    CHECK(!p.paused() && p.state() == UnplugPause::State::Running && p.firstWaiting() == -1);
    // Player 2's pad unplugged: the run waits (no step, the chrono stops).
    p.lost(1);
    CHECK(p.paused() && p.state() == UnplugPause::State::Waiting && p.firstWaiting() == 1 && p.countdownNumber() == 0);
    p.update(5.0f);
    CHECK(p.state() == UnplugPause::State::Waiting);  // time alone changes nothing
    p.lost(2);
    p.restored(1);  // the same pad plugged back
    CHECK(p.state() == UnplugPause::State::Waiting && p.firstWaiting() == 2);
    p.restored(2);  // A on another free pad: it takes player 3's place
    CHECK(p.state() == UnplugPause::State::Countdown && p.countdownNumber() == 3);
    p.update(1.0f);
    CHECK(p.countdownNumber() == 2);
    // Lost again during the countdown: waiting again, the countdown starts over later.
    p.lost(0);
    CHECK(p.state() == UnplugPause::State::Waiting && p.countdownNumber() == 0);
    p.restored(0);  // Entrée: the keyboard
    CHECK(p.countdownNumber() == 3);
    p.update(1.2f);
    CHECK(p.countdownNumber() == 2);
    p.update(1.0f);
    CHECK(p.countdownNumber() == 1);
    p.update(1.0f);
    CHECK(!p.paused());
    // « Continuer sans » : the run no longer waits for that player.
    p.lost(1);
    p.dropped(1);
    CHECK(p.state() == UnplugPause::State::Countdown);
    p.update(3.0f);
    CHECK(p.state() == UnplugPause::State::Running);
    p.restored(1);  // nothing waits: nothing to count down
    CHECK(p.state() == UnplugPause::State::Running);
}

TEST_CASE("Devices: taking over the place of a player whose pad went away") {
    LocalTeamDevices t;
    CHECK(t.join(LocalDevice::gamepad(1)) == 1);
    UnplugPause p;
    p.reset(t.count());
    p.lost(1);  // pad 1 unplugged
    // The keyboard is player 1's: Entrée gives player 2 nothing.
    CHECK(!t.assign(1, LocalDevice::keyboard()));
    CHECK(p.waiting(1));
    // A on pad 2 (free): it takes player 2's place, with player 2's keys.
    CHECK(t.isFree(LocalDevice::gamepad(2)) && t.assign(1, LocalDevice::gamepad(2)));
    p.restored(1);
    CHECK(p.state() == UnplugPause::State::Countdown);
    // Player 1 on a pad: the keyboard is free for a player who lost its pad.
    CHECK(t.assign(0, LocalDevice::gamepad(3)));
    p.lost(1);
    CHECK(t.assign(1, LocalDevice::keyboard()));
    p.restored(1);
    CHECK(t.owner(LocalDevice::keyboard()) == 1 && p.countdownNumber() == 3);
}

TEST_CASE("Devices: each player's controls are saved, the solo game's stay player 1's") {
    Settings s;
    CHECK(&s.playerInput(0) == &s.input);
    CHECK(&s.playerInput(1) == &s.coopInput[0] && &s.playerInput(2) == &s.coopInput[1]);
    const auto jump = static_cast<size_t>(Action::Jump);
    s.input.keys[jump][0] = SDL_SCANCODE_W;
    s.coopInput[0].keys[jump][0] = SDL_SCANCODE_K;
    s.coopInput[1].pad[jump][0] = PadBinding::button(SDL_GAMEPAD_BUTTON_EAST);
    s.coopInput[1].gamepad.rumble = false;
    s.coopInput[1].gamepad.stickDeadzone = 0.3f;
    const Settings r = Settings::fromJson(s.toJson());
    CHECK(r.input.keys[jump][0] == SDL_SCANCODE_W);
    CHECK(r.coopInput[0].keys[jump][0] == SDL_SCANCODE_K);
    CHECK(r.coopInput[1].pad[jump][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_EAST));
    CHECK(!r.coopInput[1].gamepad.rumble);
    CHECK_NEAR(r.coopInput[1].gamepad.stickDeadzone, 0.3f, 1e-4f);
    // Player 3 kept its defaults where player 2 changed them.
    CHECK(r.coopInput[1].keys[jump][0] == InputBindings::defaults().keys[jump][0]);
    // A file written before 2.65: players 2 and 3 have the default controls.
    const Settings old = Settings::fromJson(Json::parse(R"({"input":{}})"));
    CHECK(old.coopInput[0].keys[jump][0] == InputBindings::defaults().keys[jump][0]);
    CHECK(old.coopInput[1].pad[jump][0] == InputBindings::defaults().pad[jump][0]);
    // Each player's power keys: a file of its own.
    const fs::path dir = fs::temp_directory_path() / ("cosmic_pk_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    PowerKeyOverrides j1, j2;
    j2.setKey("lvl", "dash", SDL_SCANCODE_L);
    CHECK(j2.saveFile(dir / "power_keys_j2.json"));
    PowerKeyOverrides back;
    back.loadFile(dir / "power_keys_j2.json");
    CHECK(back.has("lvl", "dash") && !j1.has("lvl", "dash"));
    CHECK(powerKeyEntryKey(back.level("lvl"), "dash") == SDL_SCANCODE_L);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Devices: the glyphs follow each kind of pad") {
    CHECK(gamepadStyleOf(SDL_GAMEPAD_TYPE_XBOXONE) == GamepadStyle::Xbox);
    CHECK(gamepadStyleOf(SDL_GAMEPAD_TYPE_PS5) == GamepadStyle::PlayStation);
    CHECK(gamepadStyleOf(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == GamepadStyle::Switch);
    CHECK(gamepadStyleOf(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) == GamepadStyle::Switch);
    CHECK(gamepadStyleOf(SDL_GAMEPAD_TYPE_UNKNOWN) == GamepadStyle::Generic);
    const PadBinding south = PadBinding::button(SDL_GAMEPAD_BUTTON_SOUTH), east = PadBinding::button(SDL_GAMEPAD_BUTTON_EAST);
    const PadBinding start = PadBinding::button(SDL_GAMEPAD_BUTTON_START), lb = PadBinding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    const PadBinding lt = PadBinding::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    CHECK(padBindingName(south, GamepadStyle::Xbox) == "A" && padBindingName(east, GamepadStyle::Xbox) == "B");
    CHECK(padBindingName(south, GamepadStyle::PlayStation) == "Croix" && padBindingName(east, GamepadStyle::PlayStation) == "Rond");
    // Nintendo: B at the bottom, A at the right.
    CHECK(padBindingName(south, GamepadStyle::Switch) == "B" && padBindingName(east, GamepadStyle::Switch) == "A");
    CHECK(padBindingName(south, GamepadStyle::Generic) == "Bas");
    CHECK(padBindingName(start, GamepadStyle::Xbox) == "Menu" && padBindingName(start, GamepadStyle::PlayStation) == "Options" &&
          padBindingName(start, GamepadStyle::Switch) == "+");
    CHECK(padBindingName(lb, GamepadStyle::Switch) == "L" && padBindingName(lb, GamepadStyle::PlayStation) == "L1");
    CHECK(padBindingName(lt, GamepadStyle::Xbox) == "LT" && padBindingName(lt, GamepadStyle::PlayStation) == "L2" &&
          padBindingName(lt, GamepadStyle::Switch) == "ZL");
    CHECK(std::string(Glyphs::sheetFor(GamepadStyle::Switch)) == "ui/glyphs/generic.png");
    CHECK(std::string(Glyphs::sheetFor(GamepadStyle::PlayStation)) == "ui/glyphs/playstation.png");
}
