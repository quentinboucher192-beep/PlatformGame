#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "engine/core/Math.h"

namespace cosmic {

using Json = nlohmann::json;

/// Returns j[key] converted to T, or `def` if missing / of the wrong type.
template <typename T>
T jget(const Json& j, const char* key, const T& def) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception&) {
        return def;
    }
}

inline std::string jstr(const Json& j, const char* key, const std::string& def = {}) {
    return jget<std::string>(j, key, def);
}

/// Returns j[key] as it is, or `def` if j is not an object or has no such key (unlike
/// Json::value, never throws on a null or an array sent by a server).
inline Json jfield(const Json& j, const char* key, const Json& def = Json()) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    return it == j.end() ? def : *it;
}

Json toJson(Vec2 v);
Json toJson(const Rect& r);
Json toJson(Color c);
Vec2 vec2FromJson(const Json& j, Vec2 def = {});
Rect rectFromJson(const Json& j, Rect def = {});
Color colorFromJson(const Json& j, Color def = Color::white());

}  // namespace cosmic
