#pragma once

#include <string>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// 2.47: this computer for Cosmic Online (docs/online/DEVICES.md). A Cosmic Online account is
/// private to ONE computer: the game draws a random id the first time (never a fingerprint of
/// the hardware), keeps it in the user folder (online/device.json, so a reinstall keeps it) and
/// sends it with every registration and login. `boundUsername` is the account this computer is
/// bound to (one account per computer): the login screen offers only that one.
struct DeviceIdentity {
    std::string id;                      // 32 hexadecimal characters
    std::string name = "Mon ordinateur";  // shown to its owner and to the admins, editable
    std::string boundPlayer;             // the account bound to this computer ("" : none yet)
    std::string boundUsername;

    bool valid() const;
    Json toApi() const { return Json{{"id", id}, {"name", name}}; }
    Json toJson() const;
    static DeviceIdentity fromJson(const Json& j);
    /// The file's identity; a missing or broken file gets a new random id, saved at once.
    static DeviceIdentity loadOrCreate(const fs::path& file);
    bool save(const fs::path& file) const;
    /// A name as the server accepts it: spaces folded, 1 to 40 characters, no control
    /// characters nor < >. "" when nothing is left.
    static std::string cleanName(const std::string& raw);
};

}  // namespace cosmic
