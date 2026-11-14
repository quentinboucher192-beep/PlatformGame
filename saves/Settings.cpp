#include "saves/Settings.h"

#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "saves/SaveFile.h"

namespace cosmic {

bool hudVisible(const GameplaySettings& g, HudElement e) {
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
                     {"debugMode", gameplay.debugMode},
                     {"editorAutosave", gameplay.editorAutosave},
                     {"editorAutosaveSeconds", gameplay.editorAutosaveSeconds},
                     {"unlockAll", gameplay.unlockAll}};
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
        if (s.gameplay.progressBar == 3 && !s.gameplay.hud[HudElement::Progress].custom) s.gameplay.progressBar = 1;
        s.gameplay.debugMode = jget<bool>(g, "debugMode", false);
        s.gameplay.editorAutosave = jget<bool>(g, "editorAutosave", true);
        s.gameplay.editorAutosaveSeconds = std::clamp(jget<int>(g, "editorAutosaveSeconds", 60), 10, 600);
        s.gameplay.unlockAll = jget<bool>(g, "unlockAll", true);
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

}  // namespace cosmic
