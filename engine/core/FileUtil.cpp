#include "engine/core/FileUtil.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#include "engine/core/Log.h"

namespace cosmic::FileUtil {

std::optional<std::string> readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::optional<std::vector<uint8_t>> readBinary(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size < 0) return std::nullopt;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0) in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) return std::nullopt;
    return data;
}

bool writeTextAtomic(const fs::path& path, const std::string& content) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::error("Cannot write {}", Paths::toUtf8(tmp));
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            Log::error("Write failed for {}", Paths::toUtf8(tmp));
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        // Fallback for filesystems refusing replace-on-rename.
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) {
            Log::error("Cannot replace {}: {}", Paths::toUtf8(path), ec.message());
            return false;
        }
    }
    return true;
}

bool writeBinary(const fs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

std::optional<Json> loadJson(const fs::path& path) {
    auto text = readText(path);
    if (!text) {
        Log::warn("JSON file not found: {}", Paths::toUtf8(path));
        return std::nullopt;
    }
    try {
        return Json::parse(*text);
    } catch (const Json::parse_error& e) {
        Log::error("JSON syntax error in {}: {}", Paths::toUtf8(path), e.what());
        return std::nullopt;
    }
}

bool saveJson(const fs::path& path, const Json& j, int indent) {
    return writeTextAtomic(path, j.dump(indent) + "\n");
}

bool exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::string extensionLower(const fs::path& path) {
    std::string e = Paths::toUtf8(path.extension());
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

std::string slugify(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) && c < 128) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == ' ' || c == '-' || c == '_' || c == '.') {
            if (!out.empty() && out.back() != '_') out.push_back('_');
        } else if (c >= 0xC0) {
            // Start byte of an accented UTF-8 letter: approximate by its base vowel class.
            if (!out.empty() && out.back() != '_') out.push_back('e');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "item";
    if (out.size() > 48) out.resize(48);
    return out;
}

std::string nowString() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

}  // namespace cosmic::FileUtil
