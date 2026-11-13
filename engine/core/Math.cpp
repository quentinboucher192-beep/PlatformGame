#include "engine/core/Math.h"

#include <cstdio>

namespace cosmic {

namespace {
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

Color Color::fromHex(const std::string& hex, Color fallback) {
    std::string s = hex;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return fallback;
    uint8_t v[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < s.size() / 2; ++i) {
        int hi = hexDigit(s[i * 2]);
        int lo = hexDigit(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return fallback;
        v[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    return {v[0], v[1], v[2], v[3]};
}

std::string Color::toHex() const {
    char buf[16];
    if (a == 255)
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    else
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", r, g, b, a);
    return buf;
}

}  // namespace cosmic
