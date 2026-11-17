#include "saves/Settings.h"

#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "saves/SaveFile.h"
#include "engine/core/FileUtil.h"

#include <algorithm>

namespace cosmic {

bool hudVisible(const GameplaySettings& g, HudElement e) {
    if (g.focusMode && !g.hud[e].focus) return false;
    if (e == HudElement::Timer) return g.showTimer;
    if (e == HudElement::Progress) return g.progressBar > 0;
    return g.hud[e].visible;
}

void setHudVisible(GameplaySettings& g, HudElement e, bool visible) {
    if (e == HudElement::Timer) g.showTimer = visible;
    else if (e == HudElement::Progress) g.progressBar = !visible ? 0 : (g.hud[e].custom ? 3 : (g.progressBar > 0 ? g.progressBar : 1));
    else g.hud[e].visible = visible;
}

Json Settings::toJson() const {
    Json j;
    j["language"] = {{"code", language}, {"installer", installerLanguage}};
    j["graphics"] = {{"fullscreen", graphics.fullscreen},
                     {"vsync", graphics.vsync},
                     {"windowWidth", graphics.windowWidth},
                     {"windowHeight", graphics.windowHeight},
                     {"dynamicLighting", graphics.dynamicLighting},
                     {"particleQuality", graphics.particleQuality},
                     {"screenShake", graphics.screenShake},
                     {"showFps", graphics.showFps}};
    j["audio"] = {{"master", audio.master}, {"music", audio.music}, {"effects", audio.effects}, {"ambience", audio.ambience}};
    j["gameplay"] = {{"showTimer", gameplay.showTimer},
                     {"showInputHints", gameplay.showInputHints},
                     {"progressBar", gameplay.progressBar},
                     {"hud", gameplay.hud.toJson()},
                     {"focusMode", gameplay.focusMode},
                     {"adminInvincible", gameplay.adminInvincible},
                     {"powerKeysShared", gameplay.powerKeysShared},
                     {"showMessages", gameplay.showMessages},
                     {"focusMessages", gameplay.focusMessages},
                     {"enemyArrows", gameplay.enemyArrows},
                     {"fadeHud", gameplay.fadeHud},
                     {"debugMode", gameplay.debugMode},
                     {"editorAutosave", gameplay.editorAutosave},
                     {"editorAutosaveSeconds", gameplay.editorAutosaveSeconds},
                     {"unlockAll", gameplay.unlockAll},
                     {"recordStats", gameplay.recordStats},
                     {"bossStories", gameplay.bossStories},
                     {"attractTrailer", gameplay.attractTrailer}};
    j["ghosts"] = {{"enabled", ghosts.enabled},
                   {"personalByDefault", ghosts.personalByDefault},
                   {"maxGhosts", ghosts.maxGhosts},
                   {"opacity", ghosts.opacity},
                   {"showNames", ghosts.showNames},
                   {"showPanel", ghosts.showPanel},
                   {"showDelta", ghosts.showDelta},
                   {"cacheMB", ghosts.cacheMB},
                   {"colors", {ghosts.colorMine, ghosts.colorWorld, ghosts.colorFriend, ghosts.colorOther}}};
    j["online"] = {{"enabled", online.enabled},
                   {"autoLogin", online.autoLogin},
                   {"autoSync", online.autoSync},
                   {"showPseudonyms", online.showPseudonyms},
                   {"bandwidth", online.bandwidth},
                   {"onboardingDone", online.onboardingDone},
                   {"serverOverride", online.serverOverride}};
    j["input"] = input.toJson();
    return j;
}

Settings Settings::fromJson(const Json& j) {
    Settings s;
    if (j.contains("language") && j["language"].is_object()) {
        const Json& l = j["language"];
        if (auto lang = i18n::parse(jstr(l, "code"))) s.language = i18n::code(*lang);
        if (auto lang = i18n::parse(jstr(l, "installer"))) s.installerLanguage = i18n::code(*lang);
    }
    if (j.contains("graphics")) {
        const Json& g = j["graphics"];
        s.graphics.fullscreen = jget<bool>(g, "fullscreen", false);
        s.graphics.vsync = jget<bool>(g, "vsync", true);
        s.graphics.windowWidth = std::clamp(jget<int>(g, "windowWidth", 1600), 960, 7680);
        s.graphics.windowHeight = std::clamp(jget<int>(g, "windowHeight", 900), 540, 4320);
        s.graphics.dynamicLighting = jget<bool>(g, "dynamicLighting", true);
        s.graphics.particleQuality = std::clamp(jget<int>(g, "particleQuality", 2), 0, 2);
        s.graphics.screenShake = std::clamp(jget<float>(g, "screenShake", 1.0f), 0.0f, 1.5f);
        s.graphics.showFps = jget<bool>(g, "showFps", false);
    }
    if (j.contains("audio")) {
        const Json& a = j["audio"];
        auto vol = [&](const char* k, float d) { return std::clamp(jget<float>(a, k, d), 0.0f, 1.0f); };
        s.audio.master = vol("master", 0.9f);
        s.audio.music = vol("music", 0.7f);
        s.audio.effects = vol("effects", 0.8f);
        s.audio.ambience = vol("ambience", 0.6f);
    }
    if (j.contains("gameplay")) {
        const Json& g = j["gameplay"];
        s.gameplay.showTimer = jget<bool>(g, "showTimer", true);
        s.gameplay.showInputHints = jget<bool>(g, "showInputHints", true);
        s.gameplay.progressBar = std::clamp(jget<int>(g, "progressBar", 1), 0, 3);
        if (g.contains("hud")) s.gameplay.hud = HudLayout::fromJson(g["hud"]);
        s.gameplay.focusMode = jget<bool>(g, "focusMode", false);
        s.gameplay.adminInvincible = jget<bool>(g, "adminInvincible", false);
        s.gameplay.powerKeysShared = jget<bool>(g, "powerKeysShared", true);
        s.gameplay.showMessages = jget<bool>(g, "showMessages", true);
        s.gameplay.focusMessages = jget<bool>(g, "focusMessages", false);
        s.gameplay.enemyArrows = jget<bool>(g, "enemyArrows", true);
        s.gameplay.fadeHud = jget<bool>(g, "fadeHud", true);
        if (s.gameplay.progressBar == 3 && !s.gameplay.hud[HudElement::Progress].custom) s.gameplay.progressBar = 1;
        s.gameplay.debugMode = jget<bool>(g, "debugMode", false);
        s.gameplay.editorAutosave = jget<bool>(g, "editorAutosave", true);
        s.gameplay.editorAutosaveSeconds = std::clamp(jget<int>(g, "editorAutosaveSeconds", 60), 10, 600);
        s.gameplay.unlockAll = jget<bool>(g, "unlockAll", true);
        s.gameplay.recordStats = jget<bool>(g, "recordStats", true);
        s.gameplay.bossStories = std::clamp(jget<int>(g, "bossStories", 0), 0, 1);
        s.gameplay.attractTrailer = jget<bool>(g, "attractTrailer", true);
    }
    if (j.contains("ghosts")) {
        const Json& g = j["ghosts"];
        s.ghosts.enabled = jget<bool>(g, "enabled", true);
        s.ghosts.personalByDefault = jget<bool>(g, "personalByDefault", true);
        s.ghosts.maxGhosts = std::clamp(jget<int>(g, "maxGhosts", 3), 1, 8);
        s.ghosts.opacity = std::clamp(jget<int>(g, "opacity", 1), 0, 2);
        s.ghosts.showNames = jget<bool>(g, "showNames", true);
        s.ghosts.showPanel = jget<bool>(g, "showPanel", true);
        s.ghosts.showDelta = jget<bool>(g, "showDelta", true);
        s.ghosts.cacheMB = std::clamp(jget<int>(g, "cacheMB", 64), 8, 1024);
        auto colors = jget<std::vector<int>>(g, "colors", {0, 1, 2, 3});
        colors.resize(4, 0);
        for (int& c : colors) c = std::clamp(c, 0, kGhostPaletteSize - 1);
        s.ghosts.colorMine = colors[0];
        s.ghosts.colorWorld = colors[1];
        s.ghosts.colorFriend = colors[2];
        s.ghosts.colorOther = colors[3];
    }
    if (j.contains("online")) {
        const Json& o = j["online"];
        s.online.enabled = jget<bool>(o, "enabled", true);
        s.online.autoLogin = jget<bool>(o, "autoLogin", true);
        s.online.autoSync = jget<bool>(o, "autoSync", true);
        s.online.showPseudonyms = jget<bool>(o, "showPseudonyms", true);
        s.online.bandwidth = std::clamp(jget<int>(o, "bandwidth", 1), 0, 1);
        s.online.onboardingDone = jget<bool>(o, "onboardingDone", false);
        s.online.serverOverride = jstr(o, "serverOverride").substr(0, 200);
    }
    if (j.contains("input")) s.input = InputBindings::fromJson(j["input"]);
    return s;
}

Language settingsLanguage(const Settings& s) { return i18n::parse(s.language).value_or(Language::French); }

bool applyInstallerLanguage(Settings& s, const fs::path& iniFile) {
    auto text = FileUtil::readText(iniFile);
    if (!text) return false;
    // Written by Windows (WritePrivateProfileString): ANSI, or UTF-16 for some files.
    text->erase(std::remove(text->begin(), text->end(), '\0'), text->end());
    // [Language] / Language=english (Inno Setup [INI] section, value: its language name).
    std::optional<Language> chosen;
    size_t pos = 0;
    while (pos < text->size() && !chosen) {
        size_t end = text->find('\n', pos);
        std::string line = text->substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text->size() : end + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        // Spaces, byte order mark (UTF-8 or UTF-16) at the start of the file: only letters count.
        key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')); }),
                  key.end());
        if (key == "Language" || key == "language") chosen = i18n::parse(line.substr(eq + 1));
    }
    if (!chosen) return false;
    std::string code = i18n::code(*chosen);
    if (code == s.installerLanguage) return false;  // already applied: the player's choice wins
    s.installerLanguage = code;
    s.language = code;
    Log::info("Langue choisie à l'installation : {}", code);
    return true;
}

Json SettingsStore::migrateV1ToV2(const Json& v1) {
    Json out = v1;
    if (v1.contains("audio") && v1["audio"].is_object()) {
        Json a = Json::object();
        for (auto& [k, v] : v1["audio"].items()) {
            if (v.is_number()) a[k] = std::clamp(v.get<float>() / 100.0f, 0.0f, 1.0f);
        }
        // v1 called the effects bus "sfx".
        if (a.contains("sfx")) {
            a["effects"] = a["sfx"];
            a.erase("sfx");
        }
        out["audio"] = a;
    }
    if (out.contains("input") && out["input"].is_object() && !out["input"].contains("gamepadSettings")) {
        out["input"]["gamepadSettings"] = {{"stickDeadzone", 0.22}, {"triggerThreshold", 0.35}, {"sensitivity", 1.0}};
    }
    return out;
}

namespace {
SaveFile settingsFile() { return SaveFile("settings", SettingsStore::kVersion, {&SettingsStore::migrateV1ToV2}); }
fs::path settingsPath() { return Paths::savesDir() / "settings.json"; }
}  // namespace

Settings SettingsStore::load() {
    bool migrated = false;
    auto data = settingsFile().load(settingsPath(), &migrated);
    if (!data) return Settings{};
    Settings s = Settings::fromJson(*data);
    if (migrated) save(s);
    return s;
}

bool SettingsStore::save(const Settings& s) { return settingsFile().save(settingsPath(), s.toJson()); }

Color ghostPaletteColor(int index) {
    static const Color kPalette[kGhostPaletteSize] = {
        {110, 214, 255, 255},  // cyan
        {255, 204, 84, 255},   // gold
        {184, 140, 255, 255},  // violet
        {120, 255, 156, 255},  // green
        {255, 118, 150, 255},  // pink
        {245, 245, 255, 255},  // white
        {255, 158, 74, 255},   // orange
        {74, 224, 200, 255},   // turquoise
    };
    return kPalette[std::clamp(index, 0, kGhostPaletteSize - 1)];
}

const char* ghostPaletteName(int index) {
    static const char* kNames[kGhostPaletteSize] = {"Cyan", "Or", "Violet", "Vert", "Rose", "Blanc", "Orange", "Turquoise"};
    return kNames[std::clamp(index, 0, kGhostPaletteSize - 1)];
}

float ghostOpacity(int level) {
    switch (std::clamp(level, 0, 2)) {
        case 0: return 0.28f;
        case 2: return 0.72f;
        default: return 0.48f;
    }
}

}  // namespace cosmic
