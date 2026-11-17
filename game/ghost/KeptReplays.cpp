#include "game/ghost/KeptReplays.h"

#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic::keptreplays {

std::string safeName(const std::string& s, size_t max) {
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        if (out.size() >= max) break;
    }
    return out;
}

bool validName(const std::string& s) {
    if (s.size() < 6 || s.size() > 72 || s.compare(s.size() - 5, 5, ".cprz") != 0) return false;
    std::string stem = s.substr(0, s.size() - 5);
    return !stem.empty() && safeName(stem, 72) == stem;
}

uint64_t fileSize(const fs::path& p) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

fs::path levelDir(const fs::path& root, const std::string& levelKey) {
    std::string name = safeName(levelKey, 96);
    if (root.empty() || name.empty()) return {};
    return root / name;
}

std::string write(const fs::path& root, const std::string& levelKey, const std::vector<uint8_t>& bytes, const std::string& date) {
    fs::path dir = levelDir(root, levelKey);
    if (dir.empty() || bytes.empty() || bytes.size() > kMaxFileBytes) return {};
    std::error_code ec;
    fs::create_directories(dir, ec);
    // "<date's digits>-<n>": never an existing file (a best segment may refer to it).
    std::string stem;
    for (char c : date)
        if (c >= '0' && c <= '9' && stem.size() < 14) stem += c;
    if (stem.empty()) stem = "run";
    for (int n = 1; n < 1000; ++n) {
        std::string name = stem + "-" + std::to_string(n) + ".cprz";
        if (fs::exists(dir / name, ec)) continue;
        if (!FileUtil::writeBinary(dir / name, bytes)) break;
        return name;
    }
    Log::warn("Replay du meilleur segment non enregistré : {}", Paths::toUtf8(dir));
    return {};
}

std::vector<std::pair<std::string, uint64_t>> collect(const fs::path& root, const std::map<std::string, std::set<std::string>>& used) {
    std::vector<std::pair<std::string, uint64_t>> sizes;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return sizes;
    // Listed first, then removed (never while iterating).
    std::vector<fs::path> dirs;
    for (const auto& d : fs::directory_iterator(root, ec)) {
        std::error_code e2;
        if (d.is_directory(e2)) dirs.push_back(d.path());
    }
    for (const fs::path& d : dirs) {
        const std::string dir = Paths::toUtf8(d.filename());
        std::error_code e2;
        auto u = used.find(dir);
        if (u == used.end()) {
            fs::remove_all(d, e2);
            continue;
        }
        std::vector<fs::path> files;
        for (const auto& f : fs::directory_iterator(d, e2)) files.push_back(f.path());
        uint64_t bytes = 0;
        for (const fs::path& f : files) {
            std::error_code e3;
            if (!u->second.count(Paths::toUtf8(f.filename()))) fs::remove_all(f, e3);
            else bytes += fileSize(f);
        }
        sizes.push_back({dir, bytes});
    }
    return sizes;
}

}  // namespace cosmic::keptreplays
