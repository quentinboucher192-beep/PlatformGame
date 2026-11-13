#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

namespace FileUtil {

std::optional<std::string> readText(const fs::path& path);
std::optional<std::vector<uint8_t>> readBinary(const fs::path& path);

/// Writes through a temporary file then renames it, so a crash never leaves a
/// half-written save behind.
bool writeTextAtomic(const fs::path& path, const std::string& content);
bool writeBinary(const fs::path& path, const std::vector<uint8_t>& data);

/// Parses a JSON file. Logs and returns nullopt on I/O or syntax errors.
std::optional<Json> loadJson(const fs::path& path);
bool saveJson(const fs::path& path, const Json& j, int indent = 2);

bool exists(const fs::path& path);
std::string extensionLower(const fs::path& path);

/// Lowercase ASCII identifier made of [a-z0-9_] (used for ids, file names).
std::string slugify(const std::string& text);

/// Current local date-time as "YYYY-MM-DD HH:MM".
std::string nowString();

}  // namespace FileUtil

}  // namespace cosmic
