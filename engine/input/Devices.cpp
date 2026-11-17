#include "engine/input/Devices.h"

#include <algorithm>
#include <cmath>

namespace cosmic {

std::string padTypeName(GamepadStyle style) {
    switch (style) {
        case GamepadStyle::Xbox: return "Manette Xbox";
        case GamepadStyle::PlayStation: return "Manette PlayStation";
        case GamepadStyle::Switch: return "Manette Switch";
        case GamepadStyle::Generic: break;
    }
    return "Manette";
}

std::string padLabel(const PadEntry& pad) { return padTypeName(pad.style) + " (n° " + std::to_string(pad.number) + ")"; }

// ------------------------------------------------------------------------------ registry

int DeviceRegistry::plugged(SDL_JoystickID id, const std::string& key, GamepadStyle style, const std::string& name, uint64_t nowMs) {
    if (const PadEntry* p = byId(id)) return p->number;  // already known (the event came twice)
    PadEntry* back = nullptr;
    for (PadEntry& p : pads_)
        if (!p.connected && !key.empty() && p.key == key && (!back || p.changedMs >= back->changedMs)) back = &p;
    if (!back) {
        pads_.push_back({});
        back = &pads_.back();
        back->number = static_cast<int>(pads_.size());
        back->key = key;
    }
    back->id = id;
    back->style = style;
    back->name = name;
    back->connected = true;
    back->changedMs = nowMs;
    return back->number;
}

void DeviceRegistry::unplugged(SDL_JoystickID id, uint64_t nowMs) {
    for (PadEntry& p : pads_)
        if (p.connected && p.id == id) {
            p.connected = false;
            p.changedMs = nowMs;
        }
}

const PadEntry* DeviceRegistry::byNumber(int number) const {
    return number >= 1 && number <= static_cast<int>(pads_.size()) ? &pads_[static_cast<size_t>(number - 1)] : nullptr;
}

const PadEntry* DeviceRegistry::byId(SDL_JoystickID id) const {
    for (const PadEntry& p : pads_)
        if (p.connected && p.id == id) return &p;
    return nullptr;
}

std::vector<PadEntry> DeviceRegistry::listed(uint64_t nowMs, uint64_t keepMs) const {
    std::vector<PadEntry> out;
    for (const PadEntry& p : pads_)
        if (p.connected || nowMs < p.changedMs + keepMs) out.push_back(p);
    return out;
}

std::vector<int> DeviceRegistry::connectedNumbers() const {
    std::vector<int> out;
    for (const PadEntry& p : pads_)
        if (p.connected) out.push_back(p.number);
    return out;
}

// ------------------------------------------------------------------------ the local team

int LocalTeamDevices::owner(const LocalDevice& d) const {
    if (d.isNone()) return -1;
    for (size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i] == d) return static_cast<int>(i);
    return -1;
}

int LocalTeamDevices::join(const LocalDevice& d) {
    if (!isFree(d) || count() >= kMaxPlayers) return -1;
    devices_.push_back(d);
    return count() - 1;
}

bool LocalTeamDevices::leave(int player) {
    if (player <= 0 || player >= count()) return false;
    devices_.erase(devices_.begin() + player);
    return true;
}

bool LocalTeamDevices::cycle(int player, const std::vector<LocalDevice>& available) {
    if (player < 0 || player >= count() || available.empty()) return false;
    const LocalDevice mine = device(player);
    auto at = std::find(available.begin(), available.end(), mine);
    const size_t start = at == available.end() ? available.size() - 1 : static_cast<size_t>(at - available.begin());
    for (size_t k = 1; k <= available.size(); ++k) {
        const LocalDevice& d = available[(start + k) % available.size()];
        if (d == mine) break;  // round: nothing else is free
        if (isFree(d)) {
            devices_[static_cast<size_t>(player)] = d;
            return true;
        }
    }
    return false;
}

bool LocalTeamDevices::assign(int player, const LocalDevice& d) {
    if (player < 0 || player >= count()) return false;
    const int o = owner(d);
    if (o >= 0 && o != player) return false;
    devices_[static_cast<size_t>(player)] = d;
    return true;
}

void LocalTeamDevices::dropPad(int number) {
    for (LocalDevice& d : devices_)
        if (d.isPad() && d.pad == number) d = LocalDevice::none();
}

void LocalTeamDevices::set(const std::vector<LocalDevice>& devices) {
    devices_.clear();
    for (const LocalDevice& d : devices) {
        if (static_cast<int>(devices_.size()) >= kMaxPlayers) break;
        devices_.push_back(owner(d) >= 0 ? LocalDevice::none() : d);
    }
    if (devices_.empty()) devices_.push_back(LocalDevice::keyboard());
}

// ------------------------------------------------------------------------- unplug pause

void UnplugPause::reset(int players) {
    waiting_.assign(static_cast<size_t>(std::max(0, players)), 0);
    countdown_ = 0.0f;
}

void UnplugPause::lost(int player) {
    if (player < 0) return;
    if (static_cast<size_t>(player) >= waiting_.size()) waiting_.resize(static_cast<size_t>(player) + 1, 0);
    waiting_[static_cast<size_t>(player)] = 1;
    countdown_ = 0.0f;
}

void UnplugPause::restored(int player) {
    if (!waiting(player)) return;
    waiting_[static_cast<size_t>(player)] = 0;
    if (firstWaiting() < 0) countdown_ = kCountdown;  // everyone is back: 3, 2, 1
}

void UnplugPause::update(float dt) {
    if (firstWaiting() < 0 && countdown_ > 0.0f) countdown_ = std::max(0.0f, countdown_ - dt);
}

UnplugPause::State UnplugPause::state() const {
    if (firstWaiting() >= 0) return State::Waiting;
    return countdown_ > 0.0f ? State::Countdown : State::Running;
}

bool UnplugPause::waiting(int player) const {
    return player >= 0 && static_cast<size_t>(player) < waiting_.size() && waiting_[static_cast<size_t>(player)] != 0;
}

int UnplugPause::firstWaiting() const {
    for (size_t i = 0; i < waiting_.size(); ++i)
        if (waiting_[i]) return static_cast<int>(i);
    return -1;
}

int UnplugPause::countdownNumber() const {
    if (state() != State::Countdown) return 0;
    return std::clamp(static_cast<int>(std::ceil(countdown_)), 1, 3);
}

}  // namespace cosmic
