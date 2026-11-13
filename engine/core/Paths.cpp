#include "engine/core/Paths.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <system_error>
#include <vector>

#include "engine/core/Log.h"

namespace cosmic {

namespace {
fs::path g_install;
fs::path g_user;

bool hasGameData(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p / "assets", ec) && fs::is_directory(p / "data", ec);
}
}  // namespace

std::string Paths::toUtf8(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path Paths::fromUtf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

void Paths::init(const std::string& userDirOverride) {
    std::vector<fs::path> candidates;
    if (const char* base = SDL_GetBasePath()) {
        fs::path b = fromUtf8(base);
        candidates.push_back(b);
        candidates.push_back(b / "..");
        candidates.push_back(b / ".." / "..");
    }
    std::error_code ec;
    candidates.push_back(fs::current_path(ec));
#ifdef COSMIC_SOURCE_DIR
    candidates.push_back(fromUtf8(COSMIC_SOURCE_DIR));
#endif
    g_install.clear();
    for (const auto& c : candidates) {
        if (hasGameData(c)) {
            g_install = fs::weakly_canonical(c, ec);
            break;
        }
    }
    if (g_install.empty()) {
        g_install = candidates.empty() ? fs::path(".") : candidates.front();
        Log::error("Game data folders (assets/, data/) not found; using {}", toUtf8(g_install));
    }

    std::string userOverride = userDirOverride;
    if (userOverride.empty()) {
        if (const char* env = std::getenv("COSMIC_USER_DIR")) userOverride = env;
    }
    if (!userOverride.empty()) {
        g_user = fromUtf8(userOverride);
    } else if (char* pref = SDL_GetPrefPath("CosmicStudio", "CosmicPlatformer")) {
        g_user = fromUtf8(pref);
        SDL_free(pref);
    } else {
        g_user = g_install / "user";
    }
    for (const auto& d : {savesDir(), userLevelsDir(), userEnemiesDir(), packagesDir(), exportsDir(),
                          screenshotsDir(), autosaveDir()}) {
        fs::create_directories(d, ec);
    }
}

const fs::path& Paths::installDir() { return g_install; }
const fs::path& Paths::userDir() { return g_user; }

std::string Paths::normalizeSlashes(const std::string& p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p) out.push_back(c == '\\' ? '/' : c);
    // strip leading "./"
    while (out.rfind("./", 0) == 0) out.erase(0, 2);
    // collapse "/./"
    size_t pos;
    while ((pos = out.find("/./")) != std::string::npos) out.erase(pos, 2);
    return out;
}

bool Paths::isSafeRelative(const std::string& rel) {
    if (rel.empty() || rel.size() > 512) return false;
    for (unsigned char c : rel) {
        if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    std::string n = normalizeSlashes(rel);
    if (n.empty() || n[0] == '/') return false;
    size_t start = 0;
    while (start <= n.size()) {
        size_t end = n.find('/', start);
        if (end == std::string::npos) end = n.size();
        std::string part = n.substr(start, end - start);
        if (part.empty() && end != n.size()) return false;  // "a//b"
        if (part == ".." || part == ".") return false;
        // Windows reserved device names
        std::string upper;
        for (char ch : part.substr(0, part.find('.'))) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
            (upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) &&
             std::isdigit(static_cast<unsigned char>(upper[3]))))
            return false;
        if (!part.empty() && (part.back() == ' ' || part.back() == '.')) return false;
        start = end + 1;
    }
    return true;
}

}  // namespace cosmic
