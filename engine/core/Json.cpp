#include "engine/core/Json.h"

namespace cosmic {

Json toJson(Vec2 v) { return Json::array({v.x, v.y}); }
Json toJson(const Rect& r) { return Json::array({r.x, r.y, r.w, r.h}); }
Json toJson(Color c) { return c.toHex(); }

Vec2 vec2FromJson(const Json& j, Vec2 def) {
    if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number())
        return {j[0].get<float>(), j[1].get<float>()};
    if (j.is_object()) return {jget<float>(j, "x", def.x), jget<float>(j, "y", def.y)};
    return def;
}

Rect rectFromJson(const Json& j, Rect def) {
    if (j.is_array() && j.size() >= 4) {
        for (int i = 0; i < 4; ++i)
            if (!j[i].is_number()) return def;
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }
    return def;
}

Color colorFromJson(const Json& j, Color def) {
    if (j.is_string()) return Color::fromHex(j.get<std::string>(), def);
    if (j.is_array() && j.size() >= 3) {
        auto c = [&](size_t i, int d) { return i < j.size() && j[i].is_number() ? j[i].get<int>() : d; };
        return {static_cast<uint8_t>(c(0, 255)), static_cast<uint8_t>(c(1, 255)),
                static_cast<uint8_t>(c(2, 255)), static_cast<uint8_t>(c(3, 255))};
    }
    return def;
}

}  // namespace cosmic
