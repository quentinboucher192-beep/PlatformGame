// 2.72: every character the game writes exists in its fonts (a missing one draws as an empty box):
// the texts of the i18n catalogue (tools/i18n/catalog.json, French) and their English
// (data/i18n/en.json), and every string literal of the raid screens and of the gigabosses'
// routines (their marks: « OK », « + », « ◊ », « ■ », « ◀▶ » instead of ✓ ✚ ✦ ▮ ⇄). Checked against the
// body fonts (ChakraPetch Regular and SemiBold) with SDL_ttf's own cmap lookup (TTF_FontHasGlyph).

#include <SDL3_ttf/SDL_ttf.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// The code points of a UTF-8 text (an invalid byte counts as itself).
std::vector<uint32_t> codepoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (int k = 1; k < n && i + static_cast<size_t>(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0x3Fu);
        out.push_back(cp);
        i += static_cast<size_t>(n);
    }
    return out;
}

/// The ordinary string literals of a C++ source (comments, characters and raw strings left out).
std::vector<std::string> literals(const std::string& src) {
    std::vector<std::string> out;
    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            i = src.find('\n', i);
            if (i == std::string::npos) break;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i = src.find("*/", i + 2);
            if (i == std::string::npos) break;
            ++i;
            continue;
        }
        if (c == '\'') {  // a character literal ('"' included)
            for (++i; i < src.size() && src[i] != '\''; ++i)
                if (src[i] == '\\') ++i;
            continue;
        }
        if (c == 'R' && i + 1 < src.size() && src[i + 1] == '"') {  // a raw string: skipped
            const size_t open = src.find('(', i);
            if (open == std::string::npos) break;
            const std::string end = ")" + src.substr(i + 2, open - i - 2) + "\"";
            i = src.find(end, open);
            if (i == std::string::npos) break;
            i += end.size() - 1;
            continue;
        }
        if (c != '"') continue;
        std::string lit;
        for (++i; i < src.size() && src[i] != '"'; ++i) {
            if (src[i] == '\\' && i + 1 < src.size()) {
                ++i;
                continue;  // an escape is ASCII
            }
            lit += src[i];
        }
        out.push_back(lit);
    }
    return out;
}

struct Fonts {
    std::vector<TTF_Font*> body;
    Fonts() {
        if (!TTF_WasInit()) TTF_Init();
        for (const char* name : {"ChakraPetch-Regular.ttf", "ChakraPetch-SemiBold.ttf"})
            if (TTF_Font* f = TTF_OpenFont((Paths::assetsDir() / "fonts" / name).string().c_str(), 16.0f)) body.push_back(f);
    }
    ~Fonts() {
        for (TTF_Font* f : body) TTF_CloseFont(f);
    }
    bool has(uint32_t cp) const {
        if (cp == '\n' || cp == '\t') return true;
        for (TTF_Font* f : body)
            if (!TTF_FontHasGlyph(f, cp)) return false;
        return true;
    }
};

std::string hexOf(uint32_t cp) {
    char b[16];
    std::snprintf(b, sizeof b, "U+%04X", static_cast<unsigned>(cp));
    return b;
}

}  // namespace

TEST_CASE("fonts (2.72): every text of the i18n catalogue and its English has its glyphs in the game's fonts") {
    Fonts fonts;
    CHECK(fonts.body.size() == 2);
    if (fonts.body.size() != 2) return;
    const auto root = Paths::dataDir().parent_path();
    const auto catalog = FileUtil::loadJson(root / "tools" / "i18n" / "catalog.json");
    const auto english = FileUtil::loadJson(root / "data" / "i18n" / "en.json");
    CHECK(catalog.has_value() && english.has_value());
    if (!catalog || !english) return;
    const Json& en = (*english)["strings"];
    std::map<uint32_t, std::string> missing;  // a code point → the first text that has it
    size_t texts = 0;
    for (const auto& t : (*catalog)["strings"]) {
        const std::string fr = jstr(t, "fr");
        ++texts;
        for (const std::string& s : {fr, en.is_object() && en.contains(fr) && en[fr].is_string() ? en[fr].get<std::string>() : std::string()})
            for (uint32_t cp : codepoints(s))
                if (!fonts.has(cp) && !missing.count(cp)) missing[cp] = s.substr(0, 80);
    }
    for (const auto& [cp, text] : missing) std::printf("  glyphe absent des polices : %s dans « %s »\n", hexOf(cp).c_str(), text.c_str());
    CHECK(texts > 1000);
    CHECK(missing.empty());
}

TEST_CASE("fonts (2.72): the raid screens and the gigabosses' routines only write glyphs of the game's fonts") {
    Fonts fonts;
    if (fonts.body.size() != 2) return;
    const auto root = Paths::dataDir().parent_path();
    const char* files[] = {"ui/screens/RaidGameScene.cpp",  "ui/online/RaidHud.cpp",           "ui/online/RaidWidgets.cpp",
                           "ui/screens/RaidBranchScene.cpp", "ui/screens/RaidTitanScene.cpp",  "ui/screens/RaidResultsScene.cpp",
                           "ui/screens/RaidEventScene.cpp",  "ui/screens/RaidRosterScene.cpp", "ui/screens/ClanRaids.cpp",
                           "ui/screens/AdminRaids.cpp",      "ui/screens/HudEditorScene.cpp",  "game/raid/RaidBranch.cpp",
                           "game/raid/RaidRun.cpp",          "game/raid/Gigaboss.cpp",         "game/raid/RaidTest.cpp",
                           "game/raid/VoidwormRoutine.cpp",  "game/raid/GravitonRoutine.cpp",  "game/raid/TitanusRoutine.cpp",
                           "game/raid/SkullmawRoutine.cpp",  "game/raid/DreadfangRoutine.cpp", "game/raid/HeartbreakerRoutine.cpp",
                           "saves/RaidHudLayout.cpp"};
    int read = 0;
    std::set<std::string> bad;
    for (const char* f : files) {
        const auto src = FileUtil::readText(root / f);
        if (!src) continue;  // a screen another version does not have
        ++read;
        for (const std::string& lit : literals(*src))
            for (uint32_t cp : codepoints(lit))
                if (!fonts.has(cp)) bad.insert(std::string(f) + " : " + hexOf(cp) + " dans \"" + lit.substr(0, 60) + "\"");
    }
    for (const std::string& b : bad) std::printf("  %s\n", b.c_str());
    CHECK(read >= 15);
    CHECK(bad.empty());
}
