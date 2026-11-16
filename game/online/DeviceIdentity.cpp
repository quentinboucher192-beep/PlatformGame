#include "game/online/DeviceIdentity.h"

#include <random>

#include "engine/core/FileUtil.h"

namespace cosmic {

namespace {
bool hex32(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::string randomId() {
    std::random_device rd;
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i) {
        uint32_t v = rd();
        for (int k = 0; k < 4; ++k) {
            out += digits[(v >> (k * 8)) & 15];
        }
    }
    return out;
}
}  // namespace

bool DeviceIdentity::valid() const { return hex32(id); }

Json DeviceIdentity::toJson() const {
    return Json{{"format", "cosmic.device"}, {"id", id}, {"name", name}, {"bound_player", boundPlayer}, {"bound_username", boundUsername}};
}

DeviceIdentity DeviceIdentity::fromJson(const Json& j) {
    DeviceIdentity d;
    if (!j.is_object()) return d;
    d.id = jstr(j, "id");
    std::string n = cleanName(jstr(j, "name"));
    if (!n.empty()) d.name = n;
    d.boundPlayer = jstr(j, "bound_player").substr(0, 64);
    d.boundUsername = jstr(j, "bound_username").substr(0, 32);
    return d;
}

DeviceIdentity DeviceIdentity::loadOrCreate(const fs::path& file) {
    DeviceIdentity d;
    if (auto j = FileUtil::loadJson(file)) d = fromJson(*j);
    if (!d.valid()) {
        d.id = randomId();
        d.save(file);
    }
    return d;
}

bool DeviceIdentity::save(const fs::path& file) const {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return FileUtil::writeTextAtomic(file, toJson().dump(1));
}

std::string DeviceIdentity::cleanName(const std::string& raw) {
    std::string out;
    bool space = false;
    for (unsigned char c : raw) {
        if (c < 0x20 || c == 0x7f || c == '<' || c == '>') continue;
        if (c == ' ') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += static_cast<char>(c);
    }
    // At most 40 characters (UTF-8: never cut inside one).
    size_t chars = 0, i = 0;
    for (; i < out.size(); ++i) {
        if ((static_cast<unsigned char>(out[i]) & 0xC0) != 0x80) {
            if (chars == 40) break;
            ++chars;
        }
    }
    return out.substr(0, i);
}

}  // namespace cosmic
