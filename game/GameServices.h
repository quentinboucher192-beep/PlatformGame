#pragma once

#include <string>

#include "engine/core/Application.h"
#include "game/content/ContentDatabase.h"
#include "game/progression/Progression.h"
#include "game/records/Records.h"
#include "saves/Settings.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

/// Long-lived game state shared by every scene: engine services, content registry,
/// settings, progression and records.
class GameServices {
public:
    explicit GameServices(Application& app) : app(app) {}

    void loadAll();
    void applySettings();
    void saveSettings() { SettingsStore::save(settings); }
    void uiSound(const std::string& name, float volume = 0.6f) {
        app.resources().playSound("sounds/" + name + ".wav", volume);
    }
    /// Plays a music by asset path with a cross-fade.
    void playMusic(const std::string& rel);

    Application& app;
    ContentDatabase content;
    Settings settings;
    Progression progression;
    RecordsManager records;
    UIContext ui;
    /// Scripted input for automated runs (see InputScript), applied to the next level started.
    std::string inputScript;
};

}  // namespace cosmic
