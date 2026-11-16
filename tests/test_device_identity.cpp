// 2.47: this computer for Cosmic Online: a random id drawn once and kept (online/device.json),
// a name cleaned like the server does, the account it is bound to.

#include <chrono>

#include "engine/core/FileUtil.h"
#include "game/online/DeviceIdentity.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("Device identity: drawn once, kept, its name cleaned") {
    fs::path dir = fs::temp_directory_path() / ("cosmic_device_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path file = dir / "online" / "device.json";
    DeviceIdentity a = DeviceIdentity::loadOrCreate(file);
    CHECK(a.valid());
    CHECK(a.id.size() == 32);
    CHECK(a.name == "Mon ordinateur");
    CHECK(a.boundUsername.empty());
    // Kept: the same id next time (a reinstall keeps the user folder).
    DeviceIdentity b = DeviceIdentity::loadOrCreate(file);
    CHECK(b.id == a.id);
    b.boundPlayer = "p-1";
    b.boundUsername = "NovaRider";
    b.name = "PC du salon";
    CHECK(b.save(file));
    DeviceIdentity c = DeviceIdentity::loadOrCreate(file);
    CHECK(c.id == a.id && c.boundUsername == "NovaRider" && c.name == "PC du salon");
    // A broken file: a new id.
    FileUtil::writeTextAtomic(file, "{\"id\": \"pas un identifiant\"}");
    DeviceIdentity d = DeviceIdentity::loadOrCreate(file);
    CHECK(d.valid() && d.id != a.id);
    // Names as the server accepts them.
    CHECK(DeviceIdentity::cleanName("  PC   du \tsalon  ") == "PC du salon");
    CHECK(DeviceIdentity::cleanName("<b>") == "b");
    CHECK(DeviceIdentity::cleanName("   ").empty());
    CHECK(DeviceIdentity::cleanName(std::string(60, 'x')).size() == 40);
    CHECK(DeviceIdentity::cleanName("Éééééééééééééééééééééééééééééééééééééééééééé").size() == 80);  // 40 characters of 2 bytes
    std::error_code ec;
    fs::remove_all(dir, ec);
}
