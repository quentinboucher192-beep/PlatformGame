#pragma once

// 2.65: the devices of the players on one computer (docs/COOP.md § 3).
//
// * DeviceRegistry: every gamepad seen during the session, with a stable number (« Manette Xbox
//   (n° 1) »): a pad unplugged and plugged again gets its number back, SDL's id changing at each
//   connection. Unplugged pads stay listed a few seconds.
// * LocalTeamDevices: who plays with what. One device per player, one player per device; the
//   keyboard and the mouse go together and belong to one player only.
// * UnplugPause: a player's device lost during a run stops the run and its chrono (no step at
//   all: outside the simulation) until every player has a device again (the same pad plugged
//   back, another free pad, the keyboard) or is left out (« Continuer sans »), then 3-2-1.
//
// Pure logic, no SDL call: tested in tests/test_local_devices.cpp.

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "engine/input/InputTypes.h"

namespace cosmic {

/// One gamepad of the session.
struct PadEntry {
    int number = 0;            // « n° 1 »: stable for the session
    SDL_JoystickID id = 0;     // SDL's id while connected (another one at each connection)
    std::string key;           // what recognises the same pad again (its GUID, its serial)
    GamepadStyle style = GamepadStyle::Generic;
    std::string name;          // the name SDL gives it
    bool connected = false;
    uint64_t changedMs = 0;    // when it was plugged or unplugged (SDL ticks)
};

/// « Manette Xbox », « Manette PlayStation », « Manette Switch », « Manette ».
std::string padTypeName(GamepadStyle style);
/// « Manette Xbox (n° 1) ».
std::string padLabel(const PadEntry& pad);

class DeviceRegistry {
public:
    /// A gamepad connected: the same pad plugged again (same key, unplugged) gets its number back
    /// (the one unplugged last among identical ones), else the next number. Returns its number.
    int plugged(SDL_JoystickID id, const std::string& key, GamepadStyle style, const std::string& name, uint64_t nowMs);
    void unplugged(SDL_JoystickID id, uint64_t nowMs);
    /// A pad by its number (connected or not), nullptr if never seen.
    const PadEntry* byNumber(int number) const;
    /// The connected pad that has this SDL id, nullptr if none.
    const PadEntry* byId(SDL_JoystickID id) const;
    int numberOf(SDL_JoystickID id) const {
        const PadEntry* p = byId(id);
        return p ? p->number : 0;
    }
    bool connected(int number) const {
        const PadEntry* p = byNumber(number);
        return p && p->connected;
    }
    /// What a list of the devices shows: the connected pads, and the ones unplugged less than
    /// `keepMs` ago, by number.
    std::vector<PadEntry> listed(uint64_t nowMs, uint64_t keepMs) const;
    /// The connected pads' numbers, in order.
    std::vector<int> connectedNumbers() const;
    const std::vector<PadEntry>& all() const { return pads_; }

private:
    std::vector<PadEntry> pads_;
};

/// A local player's device.
struct LocalDevice {
    enum class Kind : uint8_t { None, Keyboard, Pad };
    Kind kind = Kind::None;
    int pad = 0;  // the gamepad's number (DeviceRegistry), Kind::Pad only

    static LocalDevice none() { return {}; }
    static LocalDevice keyboard() { return {Kind::Keyboard, 0}; }
    static LocalDevice gamepad(int number) { return {Kind::Pad, number}; }
    bool isNone() const { return kind == Kind::None; }
    bool isKeyboard() const { return kind == Kind::Keyboard; }
    bool isPad() const { return kind == Kind::Pad; }
    bool operator==(const LocalDevice&) const = default;
};

/// The devices of the players of one computer (player 1 first). Player 1 always has a place.
class LocalTeamDevices {
public:
    static constexpr int kMaxPlayers = 3;

    LocalTeamDevices() { devices_.push_back(LocalDevice::keyboard()); }
    int count() const { return static_cast<int>(devices_.size()); }
    const LocalDevice& device(int player) const { return devices_[static_cast<size_t>(player)]; }
    const std::vector<LocalDevice>& devices() const { return devices_; }
    /// The player who has this device, -1 if it is free (a None device is never owned).
    int owner(const LocalDevice& d) const;
    bool isFree(const LocalDevice& d) const { return !d.isNone() && owner(d) < 0; }
    /// A free device asks to play (Start or A on a free pad): the next place. -1 when the device
    /// is taken (or None) or every place is.
    int join(const LocalDevice& d);
    /// A player leaves its place (B on its pad): the next ones move up. Player 1 stays.
    bool leave(int player);
    /// « Changer de périphérique »: the next free device after the player's own, in the order of
    /// `available` (the keyboard first, then the connected pads by number), going round; nothing
    /// changes when none is free. Returns whether it changed.
    bool cycle(int player, const std::vector<LocalDevice>& available);
    /// Gives a player this device (a pad that takes over, the keyboard): false when another
    /// player has it. None leaves the player without a device.
    bool assign(int player, const LocalDevice& d);
    /// Every player whose pad is `number` loses it (the pad left the session for good).
    void dropPad(int number);
    /// Rebuilds from a list (the team of the game just played); devices given twice: None.
    void set(const std::vector<LocalDevice>& devices);

private:
    std::vector<LocalDevice> devices_;
};

/// The pause of a run whose player lost its device (one PC; the solo game as player 0).
class UnplugPause {
public:
    static constexpr float kCountdown = 3.0f;
    enum class State { Running, Waiting, Countdown };

    void reset(int players);
    /// The player's device went away: the run waits for it (a countdown under way stops).
    void lost(int player);
    /// The player has a device again (the same pad, another one, the keyboard).
    void restored(int player);
    /// « Continuer sans » : the run no longer waits for that player.
    void dropped(int player) { restored(player); }
    void update(float dt);

    State state() const;
    bool paused() const { return state() != State::Running; }
    bool waiting(int player) const;
    /// The first player the run waits for, -1 if none.
    int firstWaiting() const;
    /// 3, 2, 1 during the countdown; 0 otherwise.
    int countdownNumber() const;
    float countdownLeft() const { return countdown_; }

private:
    std::vector<uint8_t> waiting_;
    float countdown_ = 0.0f;
};

}  // namespace cosmic
